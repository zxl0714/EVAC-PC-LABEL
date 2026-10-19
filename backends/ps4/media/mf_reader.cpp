#include "mf_reader.h"
#include <windows.h>
#include <objidl.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>

namespace kt::media {
namespace {
using Microsoft::WRL::ComPtr;
void Check(HRESULT result, const char* operation) { if (FAILED(result)) { throw Failure(result, operation); } }
class ReadStream final : public IStream {
public:
    explicit ReadStream(std::shared_ptr<content::ReadOnlyFile> file, std::uint64_t position = 0) : file_(std::move(file)), position_(position) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** output) override {
        if (!output) { return E_POINTER; }
        *output = nullptr;
        if (id != __uuidof(IUnknown) && id != __uuidof(ISequentialStream) && id != __uuidof(IStream)) { return E_NOINTERFACE; }
        *output = static_cast<IStream*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto count = --references_; if (!count) { delete this; } return count; }
    HRESULT STDMETHODCALLTYPE Read(void* data, ULONG bytes, ULONG* received) override {
        if (received) { *received = 0; }
        if (!data && bytes) { return STG_E_INVALIDPOINTER; }
        try {
            std::lock_guard lock(gate_);
            const auto count = file_->ReadAt(position_, {static_cast<std::byte*>(data), bytes});
            position_ += count;
            if (received) { *received = static_cast<ULONG>(count); }
            return count == bytes ? S_OK : S_FALSE;
        } catch (...) { return STG_E_READFAULT; }
    }
    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG* written) override { if (written) { *written = 0; } return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER displacement, DWORD origin, ULARGE_INTEGER* result) override {
        std::lock_guard lock(gate_);
        const auto base = origin == STREAM_SEEK_SET ? 0ull : origin == STREAM_SEEK_CUR ? position_ : file_->Size();
        if (origin > STREAM_SEEK_END) { return STG_E_INVALIDFUNCTION; }
        std::uint64_t next{};
        if (displacement.QuadPart < 0) {
            const auto backward = static_cast<std::uint64_t>(-(displacement.QuadPart + 1)) + 1;
            if (backward > base) { return STG_E_SEEKERROR; }
            next = base - backward;
        } else {
            const auto forward = static_cast<std::uint64_t>(displacement.QuadPart);
            if (base > INT64_MAX || forward > INT64_MAX - base) { return STG_E_SEEKERROR; }
            next = base + forward;
        }
        position_ = next; if (result) { result->QuadPart = next; } return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream* destination, ULARGE_INTEGER wanted, ULARGE_INTEGER* read, ULARGE_INTEGER* written) override {
        if (read) { read->QuadPart = 0; } if (written) { written->QuadPart = 0; }
        if (!destination) { return STG_E_INVALIDPOINTER; }
        std::array<std::byte, 65536> buffer{};
        std::uint64_t remaining = wanted.QuadPart;
        while (remaining) {
            ULONG received{}, sent{};
            const auto count = static_cast<ULONG>((std::min)(remaining, std::uint64_t{buffer.size()}));
            const auto result = Read(buffer.data(), count, &received);
            if (read) { read->QuadPart += received; }
            if (FAILED(result)) { return result; }
            const auto output = destination->Write(buffer.data(), received, &sent);
            if (written) { written->QuadPart += sent; }
            if (FAILED(output)) { return output; }
            if (sent != received) { return STG_E_WRITEFAULT; }
            remaining -= received;
            if (received < count) { return S_FALSE; }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE Revert() override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* result, DWORD flags) override {
        if (!result) { return STG_E_INVALIDPOINTER; }
        if (flags & ~STATFLAG_NONAME) { return STG_E_INVALIDFLAG; }
        STATSTG info{}; info.type = STGTY_STREAM; info.cbSize.QuadPart = file_->Size(); info.grfMode = STGM_READ;
        if (!(flags & STATFLAG_NONAME)) {
            constexpr wchar_t name[] = L"source.mp4";
            info.pwcsName = static_cast<wchar_t*>(CoTaskMemAlloc(sizeof(name)));
            if (!info.pwcsName) { return STG_E_INSUFFICIENTMEMORY; }
            std::memcpy(info.pwcsName, name, sizeof(name));
        }
        *result = info; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream** result) override {
        if (!result) { return E_POINTER; } *result = nullptr;
        try { std::lock_guard lock(gate_); *result = new ReadStream(file_, position_); return S_OK; }
        catch (...) { return E_OUTOFMEMORY; }
    }
private:
    std::atomic<ULONG> references_{1};
    std::shared_ptr<content::ReadOnlyFile> file_;
    std::mutex gate_;
    std::uint64_t position_{};
};
}
struct Reader::Impl {
    ComPtr<IMFSourceReader> reader;
    std::vector<Stream> streams;
    std::uint64_t duration{};
    Stream Current(unsigned index) {
        ComPtr<IMFMediaType> type;
        Check(reader->GetCurrentMediaType(index, &type), "MF current media type");
        GUID major{}, subtype{};
        Check(type->GetGUID(MF_MT_MAJOR_TYPE, &major), "MF major type");
        Check(type->GetGUID(MF_MT_SUBTYPE, &subtype), "MF decoded subtype");
        Stream result{}; result.index = index;
        if (major == MFMediaType_Video && subtype == MFVideoFormat_NV12) {
            result.kind = Kind::Video;
            Check(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &result.width, &result.height), "MF video extent");
            Check(MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &result.rate_numerator, &result.rate_denominator), "MF video rate");
            UINT32 stride{};
            if (FAILED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) { stride = result.width; }
            result.stride = stride;
            if (!result.width || !result.height || result.width % 2 || result.height % 2 || result.width > 4096 || result.height > 4096 ||
                result.stride < result.width || result.stride > 16384 || !result.rate_numerator || !result.rate_denominator) {
                throw Failure(MF_E_INVALIDMEDIATYPE, "NV12 video profile");
            }
        } else if (major == MFMediaType_Audio && subtype == MFAudioFormat_PCM) {
            result.kind = Kind::Audio;
            UINT32 bits{}, block{};
            Check(type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &result.channels), "MF audio channels");
            Check(type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &result.sample_rate), "MF audio rate");
            Check(type->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits), "MF audio bits");
            Check(type->GetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, &block), "MF audio alignment");
            if (!result.channels || result.channels > 2 || result.sample_rate < 8000 || result.sample_rate > 192000 || bits != 16 || block != result.channels * 2) {
                throw Failure(MF_E_INVALIDMEDIATYPE, "PCM16 audio profile");
            }
        } else { throw Failure(MF_E_INVALIDMEDIATYPE, "Decoded media profile"); }
        return result;
    }
};
Reader::Reader(std::shared_ptr<content::ReadOnlyFile> file) : impl_(std::make_unique<Impl>()) {
    if (!file || file->Info().directory || !file->Size() || file->Size() > 256ull * 1024 * 1024) { throw Failure(E_INVALIDARG, "Media content size"); }
    ComPtr<IStream> stream; stream.Attach(new ReadStream(std::move(file)));
    ComPtr<IMFByteStream> bytes;
    Check(MFCreateMFByteStreamOnStream(stream.Get(), &bytes), "MF read-only byte stream");
    Check(MFCreateSourceReaderFromByteStream(bytes.Get(), nullptr, &impl_->reader), "MF source reader");
    PROPVARIANT duration{};
    const auto duration_result = impl_->reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &duration);
    if (SUCCEEDED(duration_result) && duration.vt == VT_UI8) { impl_->duration = duration.uhVal.QuadPart; }
    PropVariantClear(&duration);
    Check(duration_result, "MF source duration");
    if (!impl_->duration || impl_->duration > 86400ull * 10000000) { throw Failure(MF_E_INVALIDMEDIATYPE, "Finite media duration"); }
    Check(impl_->reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE), "MF deselect streams");
    bool ended{};
    for (unsigned index = 0; index < 16; ++index) {
        ComPtr<IMFMediaType> native;
        const auto native_result = impl_->reader->GetNativeMediaType(index, 0, &native);
        if (native_result == MF_E_INVALIDSTREAMNUMBER) { ended = true; break; }
        Check(native_result, "MF native media type");
        GUID major{}, subtype{};
        Check(native->GetGUID(MF_MT_MAJOR_TYPE, &major), "MF native major type");
        Check(native->GetGUID(MF_MT_SUBTYPE, &subtype), "MF native codec");
        ComPtr<IMFMediaType> decoded;
        Check(MFCreateMediaType(&decoded), "MF output type");
        Check(decoded->SetGUID(MF_MT_MAJOR_TYPE, major), "MF output major type");
        if (major == MFMediaType_Video && subtype == MFVideoFormat_H264) {
            Check(decoded->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12), "MF NV12 selection");
        } else if (major == MFMediaType_Audio && subtype == MFAudioFormat_AAC) {
            Check(decoded->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM), "MF PCM selection");
            Check(decoded->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16), "MF PCM16 selection");
        } else { throw Failure(MF_E_INVALIDMEDIATYPE, "H264/AAC source profile"); }
        Check(impl_->reader->SetCurrentMediaType(index, nullptr, decoded.Get()), "MF decoder initialization");
        Check(impl_->reader->SetStreamSelection(index, TRUE), "MF select decoded stream");
        impl_->streams.push_back(impl_->Current(index));
    }
    if (!ended || impl_->streams.empty()) { throw Failure(MF_E_INVALIDSTREAMNUMBER, "Bounded media streams"); }
}
Reader::~Reader() = default;
const std::vector<Stream>& Reader::Streams() const { return impl_->streams; }
std::uint64_t Reader::Duration() const { return impl_->duration; }
Frame Reader::Read(unsigned index) {
    const auto found = std::find_if(impl_->streams.begin(), impl_->streams.end(), [&](const auto& stream) { return stream.index == index; });
    if (found == impl_->streams.end()) { throw Failure(MF_E_INVALIDSTREAMNUMBER, "Read media stream"); }
    Frame frame{};
    ComPtr<IMFSample> sample;
    DWORD actual{}, flags{};
    Check(impl_->reader->ReadSample(index, 0, &actual, &flags, &frame.timestamp, &sample), "MF decode sample");
    if (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_NEWSTREAM)) { throw Failure(E_FAIL, "MF source changed or failed"); }
    if (actual != index) { throw Failure(MF_E_INVALIDSTREAMNUMBER, "MF sample stream identity"); }
    *found = impl_->Current(index); frame.stream = *found;
    frame.end = (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0;
    frame.tick = (flags & MF_SOURCE_READERF_STREAMTICK) != 0;
    frame.format_changed = (flags & (MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED | MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)) != 0;
    if (!sample) {
        if (!frame.end && !frame.tick && !frame.format_changed) { throw Failure(E_UNEXPECTED, "MF sample missing without event"); }
        return frame;
    }
    (void)sample->GetSampleDuration(&frame.duration);
    ComPtr<IMFMediaBuffer> buffer;
    Check(sample->ConvertToContiguousBuffer(&buffer), "MF contiguous sample");
    BYTE* data{}; DWORD size{};
    Check(buffer->Lock(&data, nullptr, &size), "MF sample lock");
    struct Unlock { IMFMediaBuffer* buffer; ~Unlock() { buffer->Unlock(); } } unlock{buffer.Get()};
    if (!size || size > 64 * 1024 * 1024) { throw Failure(E_INVALIDARG, "MF sample size"); }
    if (frame.stream.kind == Kind::Video && size != std::uint64_t{frame.stream.stride} * frame.stream.height * 3 / 2) { throw Failure(MF_E_INVALIDMEDIATYPE, "NV12 sample layout"); }
    if (frame.stream.kind == Kind::Audio && size % (frame.stream.channels * 2)) { throw Failure(MF_E_INVALIDMEDIATYPE, "PCM16 sample extent"); }
    frame.bytes.resize(size); std::memcpy(frame.bytes.data(), data, size);
    return frame;
}
void Reader::Rewind() {
    PROPVARIANT start{}; start.vt = VT_I8; start.hVal.QuadPart = 0;
    Check(impl_->reader->SetCurrentPosition(GUID_NULL, start), "MF media rewind");
}
}
