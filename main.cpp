#include "media.hpp"
#include "resource.h"
#include "themes.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <mfapi.h>
#include <algorithm>
#include <memory>
#include <cmath>
#include <cerrno>
#include <climits>
#include <cwchar>

static const wchar_t* TITLE = L"VideoTime 1.2.0";
static const wchar_t* theme_names[] = {L"Light", L"Dark", L"Blue", L"Navy", L"Pastel"};
static const wchar_t* faces[] = {L"Segoe UI", L"Arial", L"Tahoma", L"Verdana", L"Consolas", L"Calibri"};
static HWND main_window, list_window, rows[4];
static HFONT ui_font, total_font;
static HBRUSH bg_brush, panel_brush;
static int theme_index = 0, font_points = 13, dpi = 96;
static std::wstring font_face = L"Segoe UI", ini_path;
static double photo_rate = 1.0;
static bool remember_window_placement = false;
static std::vector<Media> files;
static Palette colors = palettes[0];

static int px(int value) { return MulDiv(value, dpi, 96); }
static std::wstring window_text(HWND window) {
    int len = GetWindowTextLengthW(window); std::wstring s((size_t)len + 1, L'\0');
    GetWindowTextW(window, &s[0], len + 1); s.resize(len); return s;
}
static std::wstring money(double value) { wchar_t b[80]; swprintf_s(b, L"$%.2f", std::round(value * 100.0) / 100.0); return b; }

static void load_settings() {
    wchar_t path[32768], text[128]; GetModuleFileNameW(nullptr, path, 32768);
    ini_path = path; ini_path.erase(ini_path.find_last_of(L"\\/") + 1); ini_path += L"videotime.ini";
    int face = (int)GetPrivateProfileIntW(L"Settings", L"Font", 0, ini_path.c_str());
    if (face >= 0 && face < (int)_countof(faces)) font_face = faces[face];
    font_points = (int)GetPrivateProfileIntW(L"Settings", L"Points", 13, ini_path.c_str());
    if (font_points < 10 || font_points > 18) font_points = 13;
    theme_index = (int)GetPrivateProfileIntW(L"Settings", L"Theme", 0, ini_path.c_str());
    if (theme_index < 0 || theme_index >= (int)_countof(palettes)) theme_index = 0;
    GetPrivateProfileStringW(L"Settings", L"PhotoRate", L"1.00", text, 128, ini_path.c_str());
    if (!parse_rate(text, photo_rate)) photo_rate = 1.0;
    remember_window_placement = GetPrivateProfileIntW(L"Settings", L"RememberWindow", 0, ini_path.c_str()) != 0;
}
static bool save_window_placement(HWND window) {
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    if (!GetWindowPlacement(window, &placement)) return false;
    const RECT& r = placement.rcNormalPosition;
    if (r.right <= r.left || r.bottom <= r.top) return false;
    const bool maximized = IsZoomed(window) || (IsIconic(window) && (placement.flags & WPF_RESTORETOMAXIMIZED));
    bool ok = WritePrivateProfileStringW(L"Window", L"Left", std::to_wstring(r.left).c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"Window", L"Top", std::to_wstring(r.top).c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"Window", L"Right", std::to_wstring(r.right).c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"Window", L"Bottom", std::to_wstring(r.bottom).c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"Window", L"Maximized", maximized ? L"1" : L"0", ini_path.c_str()) != FALSE;
    return ok;
}
static bool save_settings() {
    int index = 0; while (index < (int)_countof(faces) - 1 && font_face != faces[index]) ++index;
    wchar_t rate[80]; swprintf_s(rate, L"%.2f", photo_rate);
    bool ok = WritePrivateProfileStringW(L"Settings", L"Font", std::to_wstring(index).c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"Settings", L"Points", std::to_wstring(font_points).c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"Settings", L"Theme", std::to_wstring(theme_index).c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"Settings", L"PhotoRate", rate, ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"Settings", L"RememberWindow", remember_window_placement ? L"1" : L"0", ini_path.c_str()) != FALSE;
    if (remember_window_placement) ok &= save_window_placement(main_window);
    else ok &= WritePrivateProfileStringW(L"Window", nullptr, nullptr, ini_path.c_str()) != FALSE;
    return ok;
}
static bool read_window_number(const wchar_t* key, int& value) {
    wchar_t text[64];
    if (!GetPrivateProfileStringW(L"Window", key, L"", text, (DWORD)_countof(text), ini_path.c_str())) return false;
    wchar_t* end = nullptr; errno = 0;
    long parsed = wcstol(text, &end, 10);
    if (end == text || *end != L'\0' || errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX) return false;
    value = (int)parsed; return true;
}
static void restore_window_placement(HWND window) {
    if (!remember_window_placement) return;
    int left, top, right, bottom, maximized;
    if (!read_window_number(L"Left", left) || !read_window_number(L"Top", top) ||
        !read_window_number(L"Right", right) || !read_window_number(L"Bottom", bottom) ||
        !read_window_number(L"Maximized", maximized)) return;
    if (left < -100000 || top < -100000 || right > 100000 || bottom > 100000 ||
        left > 100000 || top > 100000 || right < -100000 || bottom < -100000) return;
    const int width = right - left, height = bottom - top;
    if (
        width < 200 || width > 16000 || height < 200 || height > 16000 ||
        (maximized != 0 && maximized != 1)) return;
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    if (!GetWindowPlacement(window, &placement)) return;
    placement.rcNormalPosition = {left, top,
        left + (std::max)(width, px(font_points >= 16 ? 1380 : 1120)),
        top + (std::max)(height, px(620))};
    placement.showCmd = maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    SetWindowPlacement(window, &placement);
}
static HFONT make_font(int points, int weight) {
    return CreateFontW(-MulDiv(points, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, font_face.c_str());
}
static BOOL CALLBACK set_font(HWND child, LPARAM font) { SendMessageW(child, WM_SETFONT, font, TRUE); return TRUE; }
static void layout() {
    RECT r; GetClientRect(main_window, &r); int w = r.right, h = r.bottom, pad = px(12);
    int top = px(font_points >= 16 ? 46 : 40), row = px(font_points * 2 + 14);
    int add = px(font_points >= 16 ? 180 : 150), remove = px(font_points >= 16 ? 220 : 180), clear = px(95);
    MoveWindow(GetDlgItem(main_window, ID_ADD), pad, pad, add, top, TRUE);
    MoveWindow(GetDlgItem(main_window, ID_REMOVE), pad * 2 + add, pad, remove, top, TRUE);
    MoveWindow(GetDlgItem(main_window, ID_CLEAR), pad * 3 + add + remove, pad, clear, top, TRUE);
    int rate_w = px(100), label_w = px(font_points >= 16 ? 180 : 130);
    int audio_x = w - pad - rate_w, video_x = audio_x - rate_w - label_w - pad;
    MoveWindow(GetDlgItem(main_window, ID_RATE_LABEL), video_x - label_w, pad, label_w, top, TRUE);
    MoveWindow(GetDlgItem(main_window, ID_RATE), video_x, pad, rate_w, top, TRUE);
    MoveWindow(GetDlgItem(main_window, ID_AUDIO_RATE_LABEL), audio_x - label_w, pad, label_w, top, TRUE);
    MoveWindow(GetDlgItem(main_window, ID_AUDIO_RATE), audio_x, pad, rate_w, top, TRUE);
    int hint_y = pad + top + px(8), list_y = hint_y + px(font_points * 2 + 4);
    MoveWindow(GetDlgItem(main_window, ID_HINT), pad, hint_y, w - 2 * pad, px(font_points * 2 + 4), TRUE);
    int totals_y = h - pad - row * 4;
    MoveWindow(list_window, pad, list_y, w - 2 * pad, (std::max)(px(50), totals_y - list_y - px(10)), TRUE);
    for (int i = 0; i < 4; ++i) MoveWindow(rows[i], pad, totals_y + row * i, w - 2 * pad, row, TRUE);
    ListView_SetColumnWidth(list_window, 0, px(270));
    ListView_SetColumnWidth(list_window, 1, px(90));
    ListView_SetColumnWidth(list_window, 2, px(120));
    ListView_SetColumnWidth(list_window, 3, (std::max)(px(220), w - px(520)));
}
static void resize_minimum() {
    RECT r; GetWindowRect(main_window, &r);
    int minimum_w = px(font_points >= 16 ? 1380 : 1120), minimum_h = px(620);
    if (r.right - r.left < minimum_w || r.bottom - r.top < minimum_h)
        SetWindowPos(main_window, nullptr, 0, 0, (std::max)(minimum_w, (int)(r.right - r.left)), (std::max)(minimum_h, (int)(r.bottom - r.top)), SWP_NOMOVE | SWP_NOZORDER);
}
static void apply_appearance() {
    colors = palettes[theme_index];
    if (bg_brush) DeleteObject(bg_brush); if (panel_brush) DeleteObject(panel_brush);
    bg_brush = CreateSolidBrush(colors.bg); panel_brush = CreateSolidBrush(colors.panel);
    HFONT old_ui = ui_font, old_total = total_font;
    ui_font = make_font(font_points, FW_NORMAL); total_font = make_font(font_points, FW_BOLD);
    EnumChildWindows(main_window, set_font, (LPARAM)ui_font);
    for (HWND row : rows) SendMessageW(row, WM_SETFONT, (WPARAM)total_font, TRUE);
    if (old_ui) DeleteObject(old_ui); if (old_total) DeleteObject(old_total);
    ListView_SetBkColor(list_window, colors.panel); ListView_SetTextBkColor(list_window, colors.panel); ListView_SetTextColor(list_window, colors.fg);
    // Custom painting keeps the list header and buttons legible in every palette.
    SetWindowTheme(list_window, L"", L""); SetWindowTheme(ListView_GetHeader(list_window), L"", L"");
    BOOL dark = colors.dark; DwmSetWindowAttribute(main_window, 20, &dark, sizeof(dark));
    HWND tip = ListView_GetToolTips(list_window);
    if (tip) { SetWindowTheme(tip, L"", L""); SendMessageW(tip, TTM_SETTIPBKCOLOR, colors.panel, 0); SendMessageW(tip, TTM_SETTIPTEXTCOLOR, colors.fg, 0); SendMessageW(tip, TTM_SETMAXTIPWIDTH, 0, px(760)); }
    resize_minimum(); layout(); RedrawWindow(main_window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}
static void update_totals() {
    if (!rows[0]) return;
    Totals t = calculate_totals(files); double video_rate = 0, audio_rate = 0;
    std::wstring video_entered = window_text(GetDlgItem(main_window, ID_RATE));
    std::wstring audio_entered = window_text(GetDlgItem(main_window, ID_AUDIO_RATE));
    bool video_valid = parse_rate(video_entered, video_rate), audio_valid = parse_rate(audio_entered, audio_rate);
    auto timed_row = [&](const wchar_t* label, size_t count, uint64_t ms, const std::wstring& entered, bool valid, double rate) {
        std::wstring charge = valid ? money(timed_charge(ms, rate)) + L" at " + money(rate) + L"/hour" :
            (entered.empty() ? L"enter hourly rate" : L"invalid hourly rate");
        return std::wstring(label) + L": " + std::to_wstring(count) + L"    Total time: " + duration_text(ms) + L" (" + hours_minutes(ms) + L")    Charge: " + charge;
    };
    SetWindowTextW(rows[0], timed_row(L"Videos", t.videos, t.video_ms, video_entered, video_valid, video_rate).c_str());
    SetWindowTextW(rows[1], timed_row(L"MP3 audio", t.audio, t.audio_ms, audio_entered, audio_valid, audio_rate).c_str());
    std::wstring photo = L"Photos: " + std::to_wstring(t.photos) + L"    Rate: " + money(photo_rate) + L"/photo    Charge: " + money((double)t.photos * photo_rate);
    SetWindowTextW(rows[2], photo.c_str());
    bool complete = (!t.videos || video_valid) && (!t.audio || audio_valid);
    double grand_charge = grand_charge_total(t, video_rate, audio_rate, photo_rate);
    std::wstring grand = L"Grand total: " + std::to_wstring(t.videos + t.audio + t.photos) + L" files    Time: " + duration_text(t.video_ms + t.audio_ms) + L" (" + hours_minutes(t.video_ms + t.audio_ms) + L")    Charge: " + (complete ? money(grand_charge) : L"enter valid rates for selected media");
    SetWindowTextW(rows[3], grand.c_str());
}
static void refresh_list() {
    SendMessageW(list_window, WM_SETREDRAW, FALSE, 0); ListView_DeleteAllItems(list_window);
    for (size_t i = 0; i < files.size(); ++i) {
        const auto& m = files[i]; LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = (int)i; item.pszText = const_cast<wchar_t*>(m.name.c_str());
        int row = ListView_InsertItem(list_window, &item);
        ListView_SetItemText(list_window, row, 1, const_cast<wchar_t*>(m.kind == Kind::Video ? L"Video" : m.kind == Kind::Audio ? L"MP3" : L"Photo"));
        std::wstring duration = m.kind == Kind::Photo ? L"Per photo" : duration_text(m.milliseconds);
        ListView_SetItemText(list_window, row, 2, &duration[0]);
        ListView_SetItemText(list_window, row, 3, const_cast<wchar_t*>(m.path.c_str()));
    }
    SendMessageW(list_window, WM_SETREDRAW, TRUE, 0); InvalidateRect(list_window, nullptr, TRUE); update_totals();
}
static void import_paths(const std::vector<std::wstring>& paths) {
    std::wstring failures; size_t failed = 0, duplicates = 0;
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    for (const auto& path : paths) {
        if (std::any_of(files.begin(), files.end(), [&](const Media& m) { return _wcsicmp(m.path.c_str(), path.c_str()) == 0; })) { ++duplicates; continue; }
        Media m; std::wstring error;
        if (inspect_media(path, m, error)) files.push_back(std::move(m));
        else { ++failed; if (failed <= 8) failures += path.substr(path.find_last_of(L"\\/") + 1) + L": " + error + L"\n\n"; }
    }
    SetCursor(old); refresh_list();
    if (failed || duplicates) {
        std::wstring message = std::to_wstring(failed) + L" unreadable/unsupported file(s); " + std::to_wstring(duplicates) + L" duplicate(s) skipped.\n\n" + failures;
        MessageBoxW(main_window, message.c_str(), TITLE, MB_OK | MB_ICONINFORMATION);
    }
}
static void choose_files() {
    std::vector<wchar_t> buffer(262144); OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = main_window;
    ofn.lpstrFilter = L"Supported media (*.mp4;*.mp3;*.jpg;*.jpeg)\0*.mp4;*.mp3;*.jpg;*.jpeg\0MP4 videos\0*.mp4\0MP3 audio\0*.mp3\0JPEG photos\0*.jpg;*.jpeg\0\0";
    ofn.lpstrFile = buffer.data(); ofn.nMaxFile = (DWORD)buffer.size();
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) {
        if (CommDlgExtendedError()) MessageBoxW(main_window, L"The file selection could not be opened. Try selecting fewer files at once.", TITLE, MB_OK | MB_ICONWARNING);
        return;
    }
    std::vector<std::wstring> paths; wchar_t* p = buffer.data() + wcslen(buffer.data()) + 1;
    if (!*p) paths.emplace_back(buffer.data());
    else while (*p) { paths.push_back(std::wstring(buffer.data()) + L"\\" + p); p += wcslen(p) + 1; }
    import_paths(paths);
}
static LRESULT color_control(HDC dc) {
    SetTextColor(dc, colors.fg); SetBkColor(dc, colors.panel); return (LRESULT)panel_brush;
}
static LRESULT draw_button(DRAWITEMSTRUCT* draw) {
    if (draw->CtlType != ODT_BUTTON) return FALSE;
    bool selected = (draw->itemState & ODS_SELECTED) != 0;
    HBRUSH brush = CreateSolidBrush(selected ? colors.selection : colors.button);
    FillRect(draw->hDC, &draw->rcItem, brush); DeleteObject(brush);
    HPEN pen = CreatePen(PS_SOLID, 1, colors.fg); HGDIOBJ old_pen = SelectObject(draw->hDC, pen), old_brush = SelectObject(draw->hDC, GetStockObject(NULL_BRUSH));
    Rectangle(draw->hDC, draw->rcItem.left, draw->rcItem.top, draw->rcItem.right, draw->rcItem.bottom);
    SelectObject(draw->hDC, old_brush); SelectObject(draw->hDC, old_pen); DeleteObject(pen);
    SetTextColor(draw->hDC, selected ? colors.selected_text : colors.fg); SetBkMode(draw->hDC, TRANSPARENT);
    std::wstring text = window_text(draw->hwndItem); RECT r = draw->rcItem;
    DrawTextW(draw->hDC, text.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (draw->itemState & ODS_FOCUS) { InflateRect(&r, -4, -4); DrawFocusRect(draw->hDC, &r); }
    return TRUE;
}
static INT_PTR CALLBACK settings_proc(HWND dialog, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        for (auto face : faces) SendDlgItemMessageW(dialog, IDC_FACE, CB_ADDSTRING, 0, (LPARAM)face);
        int index = 0; while (index < (int)_countof(faces) - 1 && font_face != faces[index]) ++index;
        SendDlgItemMessageW(dialog, IDC_FACE, CB_SETCURSEL, index, 0);
        for (int i = 10; i <= 18; ++i) SendDlgItemMessageW(dialog, IDC_POINTS, CB_ADDSTRING, 0, (LPARAM)std::to_wstring(i).c_str());
        SendDlgItemMessageW(dialog, IDC_POINTS, CB_SETCURSEL, font_points - 10, 0);
        for (auto name : theme_names) SendDlgItemMessageW(dialog, IDC_THEME, CB_ADDSTRING, 0, (LPARAM)name);
        SendDlgItemMessageW(dialog, IDC_THEME, CB_SETCURSEL, theme_index, 0);
        wchar_t rate[64]; swprintf_s(rate, L"%.2f", photo_rate); SetDlgItemTextW(dialog, IDC_PHOTO_RATE, rate);
        SendDlgItemMessageW(dialog, IDC_PHOTO_RATE, EM_SETLIMITTEXT, 12, 0);
        SetDlgItemTextW(dialog, IDC_VIDEO_RATE, window_text(GetDlgItem(main_window, ID_RATE)).c_str());
        SetDlgItemTextW(dialog, IDC_AUDIO_RATE, window_text(GetDlgItem(main_window, ID_AUDIO_RATE)).c_str());
        SendDlgItemMessageW(dialog, IDC_VIDEO_RATE, EM_SETLIMITTEXT, 12, 0);
        SendDlgItemMessageW(dialog, IDC_AUDIO_RATE, EM_SETLIMITTEXT, 12, 0);
        CheckDlgButton(dialog, IDC_REMEMBER_WINDOW, remember_window_placement ? BST_CHECKED : BST_UNCHECKED);
        SetWindowTheme(GetDlgItem(dialog, IDC_REMEMBER_WINDOW), L"", L"");
        BOOL dark = colors.dark; DwmSetWindowAttribute(dialog, 20, &dark, sizeof(dark));
        RECT parent, child; GetWindowRect(main_window, &parent); GetWindowRect(dialog, &child);
        SetWindowPos(dialog, nullptr, parent.left + ((parent.right - parent.left) - (child.right - child.left)) / 2,
            parent.top + ((parent.bottom - parent.top) - (child.bottom - child.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
        if (LOWORD(wp) == IDOK) {
            double rate;
            std::wstring video = window_text(GetDlgItem(dialog, IDC_VIDEO_RATE)), audio = window_text(GetDlgItem(dialog, IDC_AUDIO_RATE));
            if ((!video.empty() && !parse_rate(video, rate)) || (!audio.empty() && !parse_rate(audio, rate))) {
                SetDlgItemTextW(dialog, IDC_ERROR, L"Use valid hourly rates (up to 2 decimals), or leave them blank."); return TRUE;
            }
            if (!parse_rate(window_text(GetDlgItem(dialog, IDC_PHOTO_RATE)), rate)) {
                SetDlgItemTextW(dialog, IDC_ERROR, L"Enter a photo rate from 0 to 1,000,000 (up to 2 decimals).");
                SetFocus(GetDlgItem(dialog, IDC_PHOTO_RATE)); return TRUE;
            }
            int face = (int)SendDlgItemMessageW(dialog, IDC_FACE, CB_GETCURSEL, 0, 0);
            int points = (int)SendDlgItemMessageW(dialog, IDC_POINTS, CB_GETCURSEL, 0, 0);
            int theme = (int)SendDlgItemMessageW(dialog, IDC_THEME, CB_GETCURSEL, 0, 0);
            if (face < 0 || points < 0 || theme < 0) return TRUE;
            font_face = faces[face]; font_points = points + 10; theme_index = theme; photo_rate = rate;
            remember_window_placement = IsDlgButtonChecked(dialog, IDC_REMEMBER_WINDOW) == BST_CHECKED;
            SetDlgItemTextW(main_window, ID_RATE, video.c_str());
            SetDlgItemTextW(main_window, ID_AUDIO_RATE, audio.c_str());
            EndDialog(dialog, IDOK); return TRUE;
        }
        break;
    case WM_CTLCOLORDLG: return (INT_PTR)bg_brush;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN: return color_control((HDC)wp);
    case WM_DRAWITEM: return draw_button((DRAWITEMSTRUCT*)lp);
    }
    return FALSE;
}
static void show_settings() {
    if (DialogBoxW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS), main_window, settings_proc) == IDOK) {
        apply_appearance(); update_totals();
        if (!save_settings()) MessageBoxW(main_window, L"Settings apply for this session. To save them, move VideoTime to a writable folder.", TITLE, MB_OK | MB_ICONINFORMATION);
    }
}
static LRESULT CALLBACK list_subclass(HWND window, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_NOTIFY) {
        NMHDR* hdr = (NMHDR*)lp;
        if (hdr->code == NM_CUSTOMDRAW) {
            NMCUSTOMDRAW* draw = (NMCUSTOMDRAW*)lp;
            if (draw->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
                FillRect(draw->hdc, &draw->rc, bg_brush); SetTextColor(draw->hdc, colors.fg); SetBkMode(draw->hdc, TRANSPARENT);
                wchar_t text[128]; HDITEMW item{}; item.mask = HDI_TEXT; item.pszText = text; item.cchTextMax = 128;
                Header_GetItem(hdr->hwndFrom, (int)draw->dwItemSpec, &item); RECT r = draw->rc; r.left += px(8);
                DrawTextW(draw->hdc, text, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE); return CDRF_SKIPDEFAULT;
            }
        }
    }
    return DefSubclassProc(window, msg, wp, lp);
}
static LRESULT CALLBACK window_proc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        main_window = window;
        HMENU menu = CreateMenu(), settings = CreatePopupMenu(); AppendMenuW(settings, MF_STRING, ID_SETTINGS, L"Preferences...\tCtrl+,"); AppendMenuW(menu, MF_POPUP, (UINT_PTR)settings, L"&Settings"); SetMenu(window, menu);
        auto control = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
            return CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, window, (HMENU)(INT_PTR)id, nullptr, nullptr);
        };
        control(L"BUTTON", L"&Add files...", WS_TABSTOP | BS_OWNERDRAW, ID_ADD);
        control(L"BUTTON", L"&Remove selected", WS_TABSTOP | BS_OWNERDRAW, ID_REMOVE);
        control(L"BUTTON", L"&Clear", WS_TABSTOP | BS_OWNERDRAW, ID_CLEAR);
        control(L"STATIC", L"Video $/hour: ", SS_RIGHT | SS_CENTERIMAGE, ID_RATE_LABEL);
        control(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_RIGHT | ES_AUTOHSCROLL, ID_RATE);
        SendDlgItemMessageW(window, ID_RATE, EM_SETLIMITTEXT, 12, 0);
        control(L"STATIC", L"Audio $/hour: ", SS_RIGHT | SS_CENTERIMAGE, ID_AUDIO_RATE_LABEL);
        control(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_RIGHT | ES_AUTOHSCROLL, ID_AUDIO_RATE);
        SendDlgItemMessageW(window, ID_AUDIO_RATE, EM_SETLIMITTEXT, 12, 0);
        control(L"STATIC", L"MP4 / MP3 / JPG / JPEG     |     Hover a filename for codecs and bitrate. Double-click for full details.", SS_LEFT, ID_HINT);
        list_window = control(WC_LISTVIEWW, L"", WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS, ID_LIST);
        ListView_SetExtendedListViewStyle(list_window, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
        SetWindowSubclass(list_window, list_subclass, 1, 0);
        const wchar_t* headers[] = {L"File", L"Type", L"Duration", L"Full path"};
        for (int i = 0; i < 4; ++i) { LVCOLUMNW col{}; col.mask = LVCF_TEXT | LVCF_WIDTH; col.pszText = const_cast<wchar_t*>(headers[i]); col.cx = px(150); ListView_InsertColumn(list_window, i, &col); }
        rows[0] = control(L"STATIC", L"", SS_RIGHT | SS_CENTERIMAGE, ID_VIDEO_TOTAL);
        rows[1] = control(L"STATIC", L"", SS_RIGHT | SS_CENTERIMAGE, ID_AUDIO_TOTAL);
        rows[2] = control(L"STATIC", L"", SS_RIGHT | SS_CENTERIMAGE, ID_PHOTO_TOTAL);
        rows[3] = control(L"STATIC", L"", SS_RIGHT | SS_CENTERIMAGE, ID_GRAND_TOTAL);
        apply_appearance(); update_totals(); return 0;
    }
    case WM_GETMINMAXINFO: {
        auto info = (MINMAXINFO*)lp; info->ptMinTrackSize.x = px(font_points >= 16 ? 1380 : 1120); info->ptMinTrackSize.y = px(620); return 0;
    }
    case WM_SIZE: if (list_window) layout(); return 0;
    case WM_ERASEBKGND: { RECT r; GetClientRect(window, &r); FillRect((HDC)wp, &r, bg_brush ? bg_brush : (HBRUSH)(COLOR_WINDOW + 1)); return 1; }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: return color_control((HDC)wp);
    case WM_DRAWITEM: return draw_button((DRAWITEMSTRUCT*)lp);
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_ADD: choose_files(); break;
        case ID_REMOVE:
            for (int i = (int)files.size() - 1; i >= 0; --i) if (ListView_GetItemState(list_window, i, LVIS_SELECTED)) files.erase(files.begin() + i);
            refresh_list(); break;
        case ID_CLEAR: files.clear(); refresh_list(); break;
        case ID_RATE:
            if (HIWORD(wp) == EN_CHANGE) {
                update_totals();
            }
            break;
        case ID_AUDIO_RATE: if (HIWORD(wp) == EN_CHANGE) update_totals(); break;
        case ID_SETTINGS: show_settings(); break;
        }
        return 0;
    case WM_NOTIFY: {
        NMHDR* hdr = (NMHDR*)lp;
        if (hdr->idFrom != ID_LIST) break;
        if (hdr->code == LVN_GETINFOTIPW) {
            auto tip = (NMLVGETINFOTIPW*)lp;
            if (tip->iItem >= 0 && tip->iItem < (int)files.size()) {
                const Media& m = files[tip->iItem]; std::wstring detail = m.name + L"\r\n" + m.details;
                wcsncpy_s(tip->pszText, (size_t)tip->cchTextMax, detail.c_str(), _TRUNCATE);
            }
        } else if (hdr->code == NM_DBLCLK) {
            int index = ((NMITEMACTIVATE*)lp)->iItem;
            if (index >= 0 && index < (int)files.size()) MessageBoxW(window, (files[index].name + L"\n\n" + files[index].details).c_str(), L"File details", MB_OK);
        } else if (hdr->code == NM_CUSTOMDRAW) {
            auto draw = (NMLVCUSTOMDRAW*)lp;
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                bool selected = (ListView_GetItemState(list_window, (int)draw->nmcd.dwItemSpec, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                draw->clrText = selected ? colors.selected_text : colors.fg; draw->clrTextBk = selected ? colors.selection : colors.panel;
                draw->nmcd.uItemState &= ~CDIS_SELECTED; return CDRF_NEWFONT;
            }
        } else if (hdr->code == LVN_KEYDOWN) {
            auto key = (NMLVKEYDOWN*)lp;
            if (key->wVKey == VK_DELETE) SendMessageW(window, WM_COMMAND, ID_REMOVE, 0);
        }
        return 0;
    }
    case WM_CLOSE:
        if (remember_window_placement && !save_window_placement(window))
            MessageBoxW(window, L"Window size and position could not be saved. Move VideoTime to a writable folder.", TITLE, MB_OK | MB_ICONINFORMATION);
        DestroyWindow(window); return 0;
    case WM_DESTROY:
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, msg, wp, lp);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware(); HDC dc = GetDC(nullptr); dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return 1;
    HRESULT mf = MFStartup(MF_VERSION);
    if (FAILED(mf)) { MessageBoxW(nullptr, L"VideoTime requires Windows Media Foundation. On Windows N, install the Microsoft Media Feature Pack.", TITLE, MB_OK | MB_ICONERROR); CoUninitialize(); return 1; }
    INITCOMMONCONTROLSEX init{sizeof(init), ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&init); load_settings();
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = window_proc; wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1)); wc.hIconSm = wc.hIcon; wc.lpszClassName = L"VideoTimeWindow11";
    RegisterClassExW(&wc);
    HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, TITLE, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, px(1220), px(650), nullptr, nullptr, instance, nullptr);
    if (!window) { MFShutdown(); CoUninitialize(); return 1; }
    ShowWindow(window, show); restore_window_placement(window); UpdateWindow(window);
    // Local paths may be supplied for opening files and for reproducible local QA.
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) { std::vector<std::wstring> paths; for (int i = 1; i < argc; ++i) paths.emplace_back(argv[i]); import_paths(paths); }
    if (argv) LocalFree(argv);
    ACCEL accelerators[] = {{FCONTROL | FVIRTKEY, VK_OEM_COMMA, ID_SETTINGS}, {FCONTROL | FVIRTKEY, 'O', ID_ADD}};
    HACCEL accel = CreateAcceleratorTableW(accelerators, (int)_countof(accelerators));
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!TranslateAcceleratorW(window, accel, &message) && !IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    DestroyAcceleratorTable(accel);
    if (ui_font) DeleteObject(ui_font); if (total_font) DeleteObject(total_font);
    if (bg_brush) DeleteObject(bg_brush); if (panel_brush) DeleteObject(panel_brush);
    MFShutdown(); CoUninitialize(); return (int)message.wParam;
}
