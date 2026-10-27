#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

enum class Kind { Video, Audio, Photo };
struct Media {
    std::wstring path, name, details;
    Kind kind = Kind::Video;
    uint64_t milliseconds = 0;
};
struct Totals {
    uint64_t video_ms = 0, audio_ms = 0;
    size_t videos = 0, audio = 0, photos = 0;
};
bool inspect_media(const std::wstring& path, Media& result, std::wstring& error);
bool parse_rate(const std::wstring& text, double& value);
std::wstring duration_text(uint64_t ms);
std::wstring hours_minutes(uint64_t ms);
Totals calculate_totals(const std::vector<Media>& files);
double timed_charge(uint64_t ms, double hourly);
double grand_charge_total(const Totals& totals, double video_rate, double audio_rate, double photo_rate);
