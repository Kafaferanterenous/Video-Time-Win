#include "media.hpp"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cmath>
#include <cwctype>
#include <cerrno>
using Microsoft::WRL::ComPtr;

bool parse_rate(const std::wstring& text, double& value) {
    // Deliberately accept decimal money only: no NaN, infinity or exponents.
    size_t a = text.find_first_not_of(L" \t"), b = text.find_last_not_of(L" \t");
    if (a == std::wstring::npos) return false;
    bool dot = false, digit = false; unsigned decimals = 0;
    for (size_t i = a; i <= b; ++i) {
        wchar_t c = text[i];
        if (c == L'.' && !dot) { dot = true; continue; }
        if (c < L'0' || c > L'9') return false;
        digit = true; if (dot && ++decimals > 2) return false;
    }
    if (!digit) return false;
    errno = 0; value = wcstod(text.c_str(), nullptr);
    return !errno && std::isfinite(value) && value >= 0 && value <= 1000000.0;
}

std::wstring duration_text(uint64_t ms) {
    uint64_t s = (ms + 500) / 1000; wchar_t buf[80];
    swprintf_s(buf, L"%02llu:%02llu:%02llu", s / 3600, (s / 60) % 60, s % 60);
    return buf;
}
std::wstring hours_minutes(uint64_t ms) {
    // Match the rounded HH:MM:SS display; charge always uses exact milliseconds.
    uint64_t minutes = ((ms + 500) / 1000) / 60; wchar_t buf[80];
    swprintf_s(buf, L"%llu h %02llu min", minutes / 60, minutes % 60); return buf;
}
Totals calculate_totals(const std::vector<Media>& files) {
    Totals t;
    for (const auto& m : files) {
        if (m.kind == Kind::Video) { ++t.videos; t.video_ms += m.milliseconds; }
        else if (m.kind == Kind::Audio) { ++t.audio; t.audio_ms += m.milliseconds; }
        else ++t.photos;
    }
    return t;
}
double timed_charge(uint64_t ms, double hourly) { return hourly * (double)ms / 3600000.0; }
double grand_charge_total(const Totals& t, double video_rate, double audio_rate, double photo_rate) {
    // Add the displayed category charges, each rounded to cents.
    return (std::round(timed_charge(t.video_ms, video_rate) * 100.0) +
        std::round(timed_charge(t.audio_ms, audio_rate) * 100.0) +
        std::round((double)t.photos * photo_rate * 100.0)) / 100.0;
}

static std::wstring codec_name(const GUID& id) {
    if (id == MFVideoFormat_H264) return L"H.264 / AVC";
    if (id == MFVideoFormat_HEVC) return L"H.265 / HEVC";
    if (id == MFVideoFormat_MPEG2) return L"MPEG-2";
    if (id == MFVideoFormat_MP4V) return L"MPEG-4 Visual";
    if (id == MFAudioFormat_MP3) return L"MP3 / MPEG Layer III";
    if (id == MFAudioFormat_AAC) return L"AAC";
    if (id == MFAudioFormat_PCM) return L"PCM";
    if (id == MFAudioFormat_Float) return L"IEEE float";
    wchar_t guid[64]; StringFromGUID2(id, guid, 64);
    return std::wstring(L"Codec ") + guid;
}

bool inspect_media(const std::wstring& path, Media& result, std::wstring& error) {
    Media m; m.path = path;
    size_t sep = path.find_last_of(L"\\/"); m.name = path.substr(sep == std::wstring::npos ? 0 : sep + 1);
    size_t dot = path.find_last_of(L'.'); std::wstring ext = dot == std::wstring::npos ? L"" : path.substr(dot);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    if (ext != L".mp4" && ext != L".mp3" && ext != L".jpg" && ext != L".jpeg") {
        error = L"Choose MP4 video, MP3 audio or JPG/JPEG photos."; return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        error = L"The file is missing or cannot be read."; return false;
    }
    uint64_t bytes = ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
    if (ext == L".jpg" || ext == L".jpeg") {
        ComPtr<IWICImagingFactory> factory; ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame; UINT width = 0, height = 0; GUID container{};
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (SUCCEEDED(hr)) hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
        if (SUCCEEDED(hr)) hr = decoder->GetContainerFormat(&container);
        if (SUCCEEDED(hr) && container != GUID_ContainerFormatJpeg) hr = E_FAIL;
        if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
        if (SUCCEEDED(hr)) hr = frame->GetSize(&width, &height);
        if (FAILED(hr) || !width || !height) { error = L"Could not read a valid JPEG photo."; return false; }
        wchar_t text[160]; swprintf_s(text, L"Photo: JPEG\r\nDimensions: %u x %u pixels\r\nSize: %llu bytes\r\nCharged per photo; no duration or bitrate.", width, height, bytes);
        m.kind = Kind::Photo; m.details = text; result = std::move(m); return true;
    }
    ComPtr<IMFAttributes> attributes; ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);
    if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromURL(path.c_str(), attributes.Get(), &reader);
    PROPVARIANT duration; PropVariantInit(&duration);
    if (SUCCEEDED(hr)) hr = reader->GetPresentationAttribute((DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &duration);
    if (FAILED(hr) || duration.vt != VT_UI8 || duration.uhVal.QuadPart == 0) {
        PropVariantClear(&duration); error = L"Windows could not read this media duration. Check the file and Windows Media Foundation support."; return false;
    }
    m.milliseconds = (duration.uhVal.QuadPart + 5000) / 10000; PropVariantClear(&duration);
    if (!m.milliseconds) { error = L"The media duration is too short to measure."; return false; }
    bool video = false, audio = false;
    for (DWORD stream = 0; stream < 128; ++stream) {
        ComPtr<IMFMediaType> type;
        if (FAILED(reader->GetNativeMediaType(stream, 0, &type))) break;
        GUID major{}, sub{}; type->GetGUID(MF_MT_MAJOR_TYPE, &major); type->GetGUID(MF_MT_SUBTYPE, &sub);
        if (major != MFMediaType_Video && major != MFMediaType_Audio) continue;
        bool is_video = major == MFMediaType_Video; video |= is_video; audio |= !is_video;
        m.details += std::wstring(is_video ? L"Video: " : L"Audio: ") + codec_name(sub);
        UINT32 bitrate = 0, avg_bytes = 0;
        if (FAILED(type->GetUINT32(MF_MT_AVG_BITRATE, &bitrate)) && !is_video && SUCCEEDED(type->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &avg_bytes))) bitrate = avg_bytes * 8;
        if (bitrate) { wchar_t b[80]; swprintf_s(b, L" | %.1f kbps", bitrate / 1000.0); m.details += b; }
        else m.details += L" | stream bitrate unavailable";
        if (is_video) {
            UINT32 w = 0, h = 0; if (SUCCEEDED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h))) m.details += L" | " + std::to_wstring(w) + L" x " + std::to_wstring(h);
        } else {
            UINT32 hz = 0, ch = 0; type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &hz); type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
            m.details += L" | " + std::to_wstring(hz) + L" Hz | " + std::to_wstring(ch) + L" channels";
        }
        m.details += L"\r\n";
    }
    if ((ext == L".mp3" && (!audio || video)) || (ext == L".mp4" && !video)) {
        error = L"The media streams do not match the selected file type."; return false;
    }
    m.kind = video ? Kind::Video : Kind::Audio;
    if (!audio) m.details += L"Audio: none\r\n";
    wchar_t overall[128]; swprintf_s(overall, L"Overall file average: %.1f kbps (includes container)\r\nSize: %llu bytes", (double)bytes * 8.0 / (double)m.milliseconds, bytes);
    m.details += overall; result = std::move(m); return true;
}
