#include "native_screenshot.h"
#include "native_diagnostics.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <mutex>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
using Microsoft::WRL::ComPtr;
constexpr int kNotLoaded = static_cast<int>(0x805a1001u);
constexpr int kBusy = static_cast<int>(0x80020010u);
constexpr int kMemory = static_cast<int>(0x8002000cu);
constexpr std::uint64_t kMaximumBytes = 256ull * 1024 * 1024;
std::mutex gate;
unsigned references{}, active_captures{};
bool enabled{true};

int KT_SYSV Enable() {
    std::lock_guard lock(gate);
    if (!references) { UnsupportedApi("sceScreenShotEnable_without_module", 0x9c, __builtin_return_address(0)); }
    enabled = true;
    return 0;
}
int KT_SYSV Disable() {
    std::lock_guard lock(gate);
    if (!references) { UnsupportedApi("sceScreenShotDisable_without_module", 0x9c, __builtin_return_address(0)); }
    enabled = false;
    return 0;
}
int KT_SYSV IsDisabled() { std::lock_guard lock(gate); return !references || !enabled; }

struct PngError { HRESULT code; };
void Check(HRESULT code) { if (FAILED(code)) { throw PngError{code}; } }
struct ComScope {
    HRESULT status{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
    ComScope() { if (status != RPC_E_CHANGED_MODE) { Check(status); } }
    ~ComScope() { if (SUCCEEDED(status)) { CoUninitialize(); } }
};
struct CaptureScope {
    ~CaptureScope() { std::lock_guard lock(gate); --active_captures; }
};

void WriteNewFile(const std::filesystem::path& target, const unsigned char* bytes, DWORD size) {
    static std::atomic<unsigned> serial{};
    auto temporary = target;
    temporary += L".partial-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++serial);
    const auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { throw PngError{HRESULT_FROM_WIN32(GetLastError())}; }
    DWORD offset = 0;
    HRESULT result = S_OK;
    while (offset < size) {
        DWORD written{};
        if (!WriteFile(file, bytes + offset, size - offset, &written, nullptr) || !written) {
            result = HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_WRITE_FAULT);
            break;
        }
        offset += written;
    }
    if (SUCCEEDED(result) && !FlushFileBuffers(file)) { result = HRESULT_FROM_WIN32(GetLastError()); }
    CloseHandle(file);
    if (SUCCEEDED(result) && !MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
        result = HRESULT_FROM_WIN32(GetLastError());
    }
    if (FAILED(result)) {
        // Only the exact scratch file created above is removed. No replace flag
        // is used for the final move, so an existing screenshot is preserved.
        DeleteFileW(temporary.c_str());
        throw PngError{result};
    }
}

void EncodePng(const std::filesystem::path& target, unsigned width, unsigned height, unsigned stride,
               std::span<const std::uint8_t> bgra) {
    ComScope apartment;
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf())));
    ComPtr<IStream> stream;
    Check(CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf()));
    ComPtr<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf()));
    Check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    Check(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr));
    Check(frame->Initialize(nullptr));
    Check(frame->SetSize(width, height));
    auto format = GUID_WICPixelFormat32bppBGRA;
    Check(frame->SetPixelFormat(&format));
    if (format != GUID_WICPixelFormat32bppBGRA) { throw PngError{WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT}; }
    Check(frame->WritePixels(height, stride, static_cast<UINT>(bgra.size()), const_cast<BYTE*>(bgra.data())));
    Check(frame->Commit());
    Check(encoder->Commit());
    STATSTG info{};
    Check(stream->Stat(&info, STATFLAG_NONAME));
    if (!info.cbSize.QuadPart || info.cbSize.QuadPart > kMaximumBytes + 1024 * 1024) { throw PngError{E_OUTOFMEMORY}; }
    HGLOBAL memory{};
    Check(GetHGlobalFromStream(stream.Get(), &memory));
    auto* bytes = static_cast<unsigned char*>(GlobalLock(memory));
    if (!bytes) { throw PngError{E_OUTOFMEMORY}; }
    try { WriteNewFile(target, bytes, static_cast<DWORD>(info.cbSize.QuadPart)); }
    catch (...) { GlobalUnlock(memory); throw; }
    GlobalUnlock(memory);
}
} // namespace

int ScreenshotModule(CaptureModuleAction action) {
    std::lock_guard lock(gate);
    if (action == CaptureModuleAction::IsLoaded) { return references ? 0 : kNotLoaded; }
    if (action == CaptureModuleAction::Load) {
        if (references == 0xffffffffu) { return kMemory; }
        if (!references) { enabled = true; }
        ++references;
    } else {
        if (!references) { return kNotLoaded; }
        if (references == 1 && active_captures) { return kBusy; }
        --references;
    }
    std::printf("{\"event\":\"native_system_module\",\"operation\":\"%s\",\"id\":156,"
                "\"name\":\"libSceScreenShot\",\"references\":%u}\n",
                action == CaptureModuleAction::Load ? "load" : "unload", references);
    return 0;
}

std::uint64_t ResolveScreenshotImport(std::string_view identifier, std::string_view library, std::string_view module) {
    if (library != "libSceScreenShot" || module != library) { return 0; }
    const auto hash = identifier.substr(0, identifier.find('#'));
    if (hash == "2xxUtuC-RzE") { return reinterpret_cast<std::uint64_t>(&Enable); }
    if (hash == "tIYf0W5VTi8") { return reinterpret_cast<std::uint64_t>(&Disable); }
    if (hash == "-SV-oTNGFQk") { return reinterpret_cast<std::uint64_t>(&IsDisabled); }
    // SetParam/overlay/capture ABI paths remain explicit import diagnostics until
    // their layout and guest resource integration are implemented.
    return 0;
}

CaptureResult ExportScreenshot(const std::filesystem::path& target, unsigned width, unsigned height,
                               unsigned stride, std::span<const std::uint8_t> bgra) noexcept {
    {
        std::lock_guard lock(gate);
        if (!references) { return CaptureResult::NotLoaded; }
        if (!enabled) { return CaptureResult::Disabled; }
        if (!target.is_absolute() || !width || !height || width > 16384 || height > 16384 ||
            stride < static_cast<std::uint64_t>(width) * 4 ||
            static_cast<std::uint64_t>(stride) * height > kMaximumBytes ||
            bgra.size() < static_cast<std::uint64_t>(stride) * height || bgra.size() > kMaximumBytes) {
            return CaptureResult::InvalidFrame;
        }
        ++active_captures;
    }
    CaptureScope capture;
    try {
        EncodePng(target, width, height, stride, bgra);
        std::printf("{\"event\":\"native_screenshot_saved\",\"width\":%u,\"height\":%u}\n", width, height);
        return CaptureResult::Saved;
    } catch (const PngError& error) {
        std::printf("{\"event\":\"native_screenshot_error\",\"hresult\":\"0x%lx\"}\n", static_cast<unsigned long>(error.code));
    } catch (...) {
        std::puts("{\"event\":\"native_screenshot_error\",\"reason\":\"host_exception\"}");
    }
    return CaptureResult::Failed;
}

bool ScreenshotSelfTest(const std::filesystem::path& directory) {
    // Two padded BGRA rows: red/green, blue/half-transparent white.
    const std::array<std::uint8_t, 24> pixels{0,0,255,255, 0,255,0,255, 99,99,99,99,
                                            255,0,0,255, 255,255,255,128, 88,88,88,88};
    bool passed = true;
    const auto check = [&](const char* name, bool result) {
        std::printf("{\"event\":\"screenshot_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, result ? "true" : "false");
        passed &= result;
    };
    const auto file = directory / "pixels.png";
    check("unloaded", ExportScreenshot(file, 2, 2, 12, pixels) == CaptureResult::NotLoaded);
    check("load", ScreenshotModule(CaptureModuleAction::Load) == 0 && ScreenshotModule(CaptureModuleAction::IsLoaded) == 0);
    check("disabled", Disable() == 0 && IsDisabled() == 1 &&
          ExportScreenshot(directory / "disabled.png", 2, 2, 12, pixels) == CaptureResult::Disabled);
    check("invalid_frame", Enable() == 0 && IsDisabled() == 0 &&
          ExportScreenshot(file, 2, 2, 4, pixels) == CaptureResult::InvalidFrame &&
          ExportScreenshot(file, 2, 3, 12, pixels) == CaptureResult::InvalidFrame);
    check("png_saved", ExportScreenshot(file, 2, 2, 12, pixels) == CaptureResult::Saved);
    check("preserve_existing", ExportScreenshot(file, 2, 2, 12, pixels) == CaptureResult::Failed);
    check("unload_and_reload", ScreenshotModule(CaptureModuleAction::Unload) == 0 &&
          ScreenshotModule(CaptureModuleAction::IsLoaded) == kNotLoaded &&
          ScreenshotModule(CaptureModuleAction::Load) == 0 && !IsDisabled() && ScreenshotModule(CaptureModuleAction::Unload) == 0);
    std::printf("{\"event\":\"screenshot_self_test\",\"passed\":%s}\n", passed ? "true" : "false");
    return passed;
}
