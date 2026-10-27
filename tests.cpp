#include "media.hpp"
#include "themes.hpp"
#include <mfapi.h>
#include <cmath>
#include <cstdio>

static int failures = 0, checks = 0;
static void check(bool ok, const char* label) { ++checks; if (!ok) { ++failures; printf("FAIL: %s\n", label); } }
static double luminance(COLORREF color) {
    auto linear = [](BYTE c) { double s = c / 255.0; return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4); };
    return .2126 * linear(GetRValue(color)) + .7152 * linear(GetGValue(color)) + .0722 * linear(GetBValue(color));
}
static double contrast(COLORREF a, COLORREF b) { double x = luminance(a), y = luminance(b); return x > y ? (x+.05)/(y+.05) : (y+.05)/(x+.05); }
int wmain(int argc, wchar_t** argv) {
    for (const auto& p : palettes) {
        check(contrast(p.fg, p.bg) >= 7, "theme background text has at least 7:1 contrast");
        check(contrast(p.fg, p.panel) >= 7, "theme panel text has at least 7:1 contrast");
        check(contrast(p.fg, p.button) >= 7, "theme button text has at least 7:1 contrast");
        check(contrast(p.selected_text, p.selection) >= 7, "theme selection text has at least 7:1 contrast");
    }
    double rate = 0;
    for (auto text : {L"", L" ", L"-1", L"NaN", L"inf", L"1e3", L"12x", L".", L"1.001", L"1000001", L"1,000", L"+1"}) check(!parse_rate(text, rate), "reject invalid money");
    check(parse_rate(L" 40.50 ", rate) && rate == 40.5, "decimal money and whitespace");
    check(parse_rate(L"0", rate) && rate == 0, "zero rate");
    check(parse_rate(L"1000000.00", rate), "rate upper bound");
    check(duration_text(5400000) == L"01:30:00", "90 minute time");
    check(hours_minutes(5400000) == L"1 h 30 min", "90 minute human time");
    check(duration_text(3599600) == L"01:00:00" && hours_minutes(3599600) == L"1 h 00 min", "rounding at hour boundary agrees");
    check(duration_text(360000000) == L"100:00:00", "durations beyond 24 hours");
    check(std::abs(timed_charge(3200, 40) - 0.0355555555556) < 1e-10, "exact prorated milliseconds");
    std::vector<Media> files(4); files[0].kind = Kind::Video; files[0].milliseconds = 5400000;
    files[1].kind = Kind::Audio; files[1].milliseconds = 90000; files[2].kind = files[3].kind = Kind::Photo;
    Totals t = calculate_totals(files);
    check(t.videos == 1 && t.audio == 1 && t.photos == 2 && t.video_ms == 5400000 && t.audio_ms == 90000, "mixed categories and photo exclusion from time");
    check(std::abs(grand_charge_total(t, 40, 40, 1) - 63.0) < 1e-8, "grand total at shared hourly rate");
    check(std::abs(grand_charge_total(t, 40, 80, 2.50) - 67.0) < 1e-8, "grand total at independent rates and custom photo rate");
    Totals short_clips; short_clips.video_ms = short_clips.audio_ms = 3200; short_clips.photos = 2;
    check(std::abs(grand_charge_total(short_clips, 40, 40, 1) - 2.08) < 1e-8, "grand total equals displayed rounded category charges");
    check(grand_charge_total({}, 0, 0, 1) == 0, "empty grand total");
    files.erase(files.begin()); t = calculate_totals(files); check(!t.videos && !t.video_ms && t.audio_ms == 90000 && t.photos == 2, "removal recalculates totals");
    check(calculate_totals({}).photos == 0, "clear totals");
    if (argc == 2) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); check(SUCCEEDED(hr), "COM startup");
        HRESULT mf = MFStartup(MF_VERSION); check(SUCCEEDED(mf), "Media Foundation startup");
        std::wstring dir = std::wstring(argv[1]) + L"\\", error; Media m;
        check(inspect_media(dir + L"video.mp4", m, error) && m.kind == Kind::Video && m.milliseconds >= 3190 && m.milliseconds <= 3210, "native MP4 duration");
        check(m.details.find(L"H.264") != std::wstring::npos && m.details.find(L"AAC") != std::wstring::npos && m.details.find(L"kbps") != std::wstring::npos, "native video and audio codec and bitrate");
        check(inspect_media(dir + L"audio.mp3", m, error) && m.kind == Kind::Audio && m.milliseconds >= 3100 && m.milliseconds <= 3400, "native CBR MP3 duration");
        check(m.details.find(L"MP3 / MPEG Layer III") != std::wstring::npos, "native MP3 codec");
        check(inspect_media(dir + L"vbr.mp3", m, error) && m.kind == Kind::Audio && m.milliseconds >= 3100 && m.milliseconds <= 3400, "native VBR MP3 duration");
        check(inspect_media(dir + L"photo.jpg", m, error) && m.kind == Kind::Photo && !m.milliseconds && m.details.find(L"64 x 48") != std::wstring::npos, "JPEG dimensions and photo category");
        check(inspect_media(dir + L"photo.JPEG", m, error) && m.kind == Kind::Photo, "uppercase JPEG extension");
        check(!inspect_media(dir + L"bad.mp4", m, error), "reject broken MP4");
        check(!inspect_media(dir + L"bad.mp3", m, error), "reject broken MP3");
        check(!inspect_media(dir + L"bad.jpg", m, error), "reject broken JPEG");
        check(!inspect_media(dir + L"missing.mp4", m, error), "reject missing file");
        check(!inspect_media(dir + L"unsupported.wav", m, error), "reject unsupported file");
        if (SUCCEEDED(mf)) MFShutdown(); if (SUCCEEDED(hr)) CoUninitialize();
    }
    printf("%d checks, %d failures\n", checks, failures); return failures ? 1 : 0;
}
