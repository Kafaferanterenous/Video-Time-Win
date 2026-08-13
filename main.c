#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

#define APP_TITLE L"VideoTime"
#define ID_ADD 101
#define ID_REMOVE 102
#define ID_CLEAR 103
#define ID_LIST 104
#define ID_TOTAL 105
#define ID_MONO 106
#define ID_RATE_LABEL 107
#define ID_RATE 108

static HWND g_list;
static HWND g_total;
static uint64_t g_total_ms;
static HFONT g_ui_font;
static HFONT g_mono_font;
static HFONT g_total_font;
static HFONT g_total_mono_font;

static HFONT make_font(HWND window, const wchar_t *face, int points, int weight)
{
    HDC dc = GetDC(window); int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(window, dc);
    return CreateFontW(-MulDiv(points, dpi, 72), 0, 0, 0, weight, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

static void apply_font(HWND window)
{
    HFONT font = IsDlgButtonChecked(window, ID_MONO) == BST_CHECKED ?
        g_mono_font : g_ui_font;
    int ids[] = {ID_ADD, ID_REMOVE, ID_CLEAR, ID_MONO, ID_LIST, ID_RATE_LABEL, ID_RATE};
    size_t index;
    for (index = 0; index < sizeof(ids) / sizeof(ids[0]); index++)
        SendMessageW(GetDlgItem(window, ids[index]), WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(g_total, WM_SETFONT, (WPARAM)(
        IsDlgButtonChecked(window, ID_MONO) == BST_CHECKED ?
        g_total_mono_font : g_total_font), TRUE);
    InvalidateRect(window, NULL, TRUE);
}

static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t be64(const unsigned char *p)
{
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

static int read_at(FILE *f, uint64_t offset, void *data, size_t size)
{
    return _fseeki64(f, (__int64)offset, SEEK_SET) == 0 &&
           fread(data, 1, size, f) == size;
}

static int find_mvhd(FILE *f, uint64_t start, uint64_t end,
                     uint64_t *payload, uint64_t *payload_size)
{
    uint64_t pos = start;
    while (pos + 8 <= end) {
        unsigned char header[16];
        uint64_t size, header_size = 8;
        uint32_t size32, type;
        if (!read_at(f, pos, header, 8)) return 0;
        size32 = be32(header); type = be32(header + 4); size = size32;
        if (size32 == 1) {
            if (pos + 16 > end || !read_at(f, pos + 8, header + 8, 8)) return 0;
            size = be64(header + 8); header_size = 16;
        } else if (size32 == 0) {
            size = end - pos;
        }
        if (size < header_size || size > end - pos) return 0;
        if (type == 0x6d766864u) {
            *payload = pos + header_size; *payload_size = size - header_size;
            return 1;
        }
        if (type == 0x6d6f6f76u &&
            find_mvhd(f, pos + header_size, pos + size, payload, payload_size))
            return 1;
        pos += size;
    }
    return 0;
}

static int mp4_duration_ms(const wchar_t *path, uint64_t *milliseconds)
{
    FILE *f = NULL; __int64 length; uint64_t payload, payload_size, duration;
    unsigned char data[32]; uint32_t timescale; unsigned int version;
    if (_wfopen_s(&f, path, L"rb") != 0) return 0;
    if (_fseeki64(f, 0, SEEK_END) != 0 || (length = _ftelli64(f)) < 8 ||
        !find_mvhd(f, 0, (uint64_t)length, &payload, &payload_size) ||
        payload_size < 20 || !read_at(f, payload, data, sizeof(data))) {
        fclose(f); return 0;
    }
    fclose(f); version = data[0];
    if (version == 0) {
        timescale = be32(data + 12); duration = be32(data + 16);
    } else if (version == 1 && payload_size >= 32) {
        timescale = be32(data + 20); duration = be64(data + 24);
    } else return 0;
    if (!timescale || duration == UINT64_MAX) return 0;
    *milliseconds = (duration / timescale) * 1000u +
                    ((duration % timescale) * 1000u + timescale / 2u) / timescale;
    return 1;
}

static void format_duration(uint64_t milliseconds, wchar_t text[32])
{
    uint64_t seconds = (milliseconds + 500u) / 1000u;
    swprintf_s(text, 32, L"%02llu:%02llu:%02llu",
        (unsigned long long)(seconds / 3600u),
        (unsigned long long)((seconds / 60u) % 60u),
        (unsigned long long)(seconds % 60u));
}

static void update_total(void)
{
    wchar_t duration[32], label[160], rate_text[64], *end = NULL;
    double rate;
    format_duration(g_total_ms, duration);
    GetWindowTextW(GetDlgItem(GetParent(g_total), ID_RATE), rate_text, 64);
    rate = wcstod(rate_text, &end);
    while (end && (*end == L' ' || *end == L'\t')) end++;
    if (!rate_text[0] || !end || *end || rate < 0.0) {
        swprintf_s(label, 160, L"Total time: %s    Charge: enter a valid hourly rate", duration);
    } else {
        double charge = rate * (double)g_total_ms / 3600000.0;
        swprintf_s(label, 160, L"Total time: %s    Charge at $%.2f/hour: $%.2f",
            duration, rate, charge);
    }
    SetWindowTextW(g_total, label);
}

static void add_path(const wchar_t *path)
{
    uint64_t duration; wchar_t formatted[32]; LVITEMW item = {0};
    const wchar_t *name = wcsrchr(path, L'\\'); int row;
    if (!mp4_duration_ms(path, &duration)) {
        wchar_t message[1024];
        swprintf_s(message, 1024, L"Could not read an MP4 movie duration from:\n\n%s", path);
        MessageBoxW(NULL, message, APP_TITLE, MB_OK | MB_ICONWARNING); return;
    }
    name = name ? name + 1 : path; format_duration(duration, formatted);
    item.mask = LVIF_TEXT | LVIF_PARAM; item.iItem = ListView_GetItemCount(g_list);
    item.pszText = (wchar_t *)name; item.lParam = (LPARAM)duration;
    row = ListView_InsertItem(g_list, &item);
    ListView_SetItemText(g_list, row, 1, formatted);
    ListView_SetItemText(g_list, row, 2, (wchar_t *)path);
    g_total_ms += duration; update_total();
}

static void choose_files(HWND owner)
{
    enum { BUFFER_CHARS = 65536 }; static wchar_t files[BUFFER_CHARS];
    OPENFILENAMEW dialog = {0}; wchar_t *p, directory[MAX_PATH];
    files[0] = 0; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"MP4 video (*.mp4)\0*.mp4\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = files; dialog.nMaxFile = BUFFER_CHARS;
    dialog.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST |
                   OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&dialog)) return;
    p = files + wcslen(files) + 1;
    if (!*p) { add_path(files); return; }
    wcscpy_s(directory, MAX_PATH, files);
    while (*p) {
        wchar_t path[32768];
        swprintf_s(path, 32768, L"%s\\%s", directory, p); add_path(path);
        p += wcslen(p) + 1;
    }
}

static void remove_selected(void)
{
    int row;
    while ((row = ListView_GetNextItem(g_list, -1, LVNI_SELECTED)) >= 0) {
        LVITEMW item = {0}; item.mask = LVIF_PARAM; item.iItem = row;
        if (ListView_GetItem(g_list, &item)) g_total_ms -= (uint64_t)item.lParam;
        ListView_DeleteItem(g_list, row);
    }
    update_total();
}

static void layout(HWND window)
{
    RECT r; int w, h; GetClientRect(window, &r); w = r.right; h = r.bottom;
    MoveWindow(GetDlgItem(window, ID_ADD), 12, 12, 150, 38, TRUE);
    MoveWindow(GetDlgItem(window, ID_REMOVE), 170, 12, 170, 38, TRUE);
    MoveWindow(GetDlgItem(window, ID_CLEAR), 348, 12, 100, 38, TRUE);
    MoveWindow(GetDlgItem(window, ID_MONO), 466, 14, 175, 34, TRUE);
    MoveWindow(GetDlgItem(window, ID_RATE_LABEL), 650, 18, 125, 28, TRUE);
    MoveWindow(GetDlgItem(window, ID_RATE), 775, 14, 105, 34, TRUE);
    MoveWindow(g_list, 12, 62, w - 24, h - 126, TRUE);
    MoveWindow(g_total, 12, h - 52, w - 24, 40, TRUE);
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message) {
    case WM_CREATE: {
        LVCOLUMNW column = {LVCF_TEXT | LVCF_WIDTH, 0, 220, L"File", 0, 0, 0, 0};
        CreateWindowW(L"BUTTON", L"1. Add videos...", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window, (HMENU)ID_ADD, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Remove selected", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window, (HMENU)ID_REMOVE, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Clear", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window, (HMENU)ID_CLEAR, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Monospace font", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 0, 0, window, (HMENU)ID_MONO, NULL, NULL);
        CreateWindowW(L"STATIC", L"Hourly rate ($):", WS_CHILD | WS_VISIBLE | SS_RIGHT,
            0, 0, 0, 0, window, (HMENU)ID_RATE_LABEL, NULL, NULL);
        CreateWindowW(L"EDIT", L"40.00", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_RIGHT | ES_AUTOHSCROLL,
            0, 0, 0, 0, window, (HMENU)ID_RATE, NULL, NULL);
        g_list = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER |
            LVS_REPORT | LVS_SHOWSELALWAYS, 0, 0, 0, 0, window, (HMENU)ID_LIST, NULL, NULL);
        g_total = CreateWindowW(L"STATIC", L"Total: 00:00:00", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window, (HMENU)ID_TOTAL, NULL, NULL);
        g_ui_font = make_font(window, L"Segoe UI", 13, FW_NORMAL);
        g_mono_font = make_font(window, L"Consolas", 13, FW_NORMAL);
        g_total_font = make_font(window, L"Segoe UI", 15, FW_BOLD);
        g_total_mono_font = make_font(window, L"Consolas", 15, FW_BOLD);
        if (!g_ui_font || !g_mono_font || !g_total_font || !g_total_mono_font) return -1;
        apply_font(window);
        ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        ListView_InsertColumn(g_list, 0, &column); column.cx = 105; column.pszText = L"Duration";
        ListView_InsertColumn(g_list, 1, &column); column.cx = 500; column.pszText = L"Full path";
        ListView_InsertColumn(g_list, 2, &column); update_total(); return 0;
    }
    case WM_SIZE: layout(window); return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_ADD) choose_files(window);
        else if (LOWORD(wp) == ID_REMOVE) remove_selected();
        else if (LOWORD(wp) == ID_CLEAR) { ListView_DeleteAllItems(g_list); g_total_ms = 0; update_total(); }
        else if (LOWORD(wp) == ID_MONO) apply_font(window);
        else if (LOWORD(wp) == ID_RATE && HIWORD(wp) == EN_CHANGE) update_total();
        return 0;
    case WM_DESTROY:
        if (g_ui_font) DeleteObject(g_ui_font);
        if (g_mono_font) DeleteObject(g_mono_font);
        if (g_total_font) DeleteObject(g_total_font);
        if (g_total_mono_font) DeleteObject(g_total_mono_font);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show)
{
    WNDCLASSEXW wc = {sizeof(wc)}; HWND window; MSG message;
    (void)previous; (void)command; InitCommonControls();
    wc.lpfnWndProc = window_proc; wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hIcon = LoadIcon(instance, MAKEINTRESOURCE(1));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.lpszClassName = L"VideoTimeWindow";
    if (!RegisterClassExW(&wc)) return 1;
    window = CreateWindowExW(0, wc.lpszClassName, APP_TITLE,
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 560,
        NULL, NULL, instance, NULL);
    if (!window) return 1;
    ShowWindow(window, show); UpdateWindow(window);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    return (int)message.wParam;
}
