#include "ps2_iop_host.h"

#include "ps2_runtime.h"
#include "native_host.h"
#include "ps2_stubs.h"
#include "ps2x/iop/iop_subsystem.h"
#include "raylib.h"
#include "sdl_pcm_feed.h"
#include <SDL3/SDL_init.h>
#include <stdexcept>
#include "Kernel/Stubs/SIF.h"
#include "runtime/ps2_memory.h"
#include "Kernel/Stubs/MemoryCard.h"
#include "Kernel/Syscalls/Common.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <utility>
#include <fstream>
#include "../../../backends/ps2/iso_disc.h"
#include "../../../backends/ps2/frame_trace.h"

#if !defined(_WIN32)
#include <sys/types.h>
#endif

class PS2IopHostAdapter::AudioOutput
{
public:
    explicit AudioOutput(ps2x::iop::IopSubsystem &source) : feed{&source,
        [](void *state, int16_t *pcm, size_t frames) {
            static_cast<ps2x::iop::IopSubsystem *>(state)->readPcm(pcm, frames);
        }} {}
    bool Open(float gain)
    {
        initialized = SDL_InitSubSystem(SDL_INIT_AUDIO);
        if (!initialized) return false;
        constexpr SDL_AudioSpec source{SDL_AUDIO_S16, 2, 48000};
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &source,
                                           cave::ps2::SdlPcmFeed::Pull, &feed);
        if (!stream || !SDL_SetAudioStreamGain(stream, gain) || !feed.Prime(stream)) return false;
        SDL_AudioSpec device{};
        int period = 0;
        if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &device, &period))
            std::cerr << "[ps2-audio-device] backend=SDL3 driver=" << SDL_GetCurrentAudioDriver()
                      << " source_hz=48000 device_hz=" << device.freq << " channels=" << device.channels
                      << " period_frames=" << period << '\n';
        return SDL_ResumeAudioStreamDevice(stream);
    }
    ~AudioOutput()
    {
        // Destroying the default-device stream unbinds and closes the logical
        // device, synchronizing with its last callback before iop is released.
        SDL_DestroyAudioStream(stream);
        if (initialized) SDL_QuitSubSystem(SDL_INIT_AUDIO);
        std::cerr << "[ps2-audio-device-summary] callbacks=" << feed.callbacks
                  << " largest_request_frames=" << feed.largestRequest << " put_failures=" << feed.putFailures << '\n';
    }
private:
    cave::ps2::SdlPcmFeed feed;
    SDL_AudioStream *stream = nullptr;
    bool initialized = false;
};

void PS2IopHostAdapter::startAudio(ps2x::iop::IopSubsystem &iop, float gain)
{
    stopAudio();
    auto output = std::make_unique<AudioOutput>(iop);
    if (output->Open(gain)) m_audioOutput = std::move(output);
    else std::cerr << "[ps2-audio-device] unavailable: " << SDL_GetError() << '\n';
}
void PS2IopHostAdapter::stopAudio() { m_audioOutput.reset(); }

PS2IopHostAdapter::CallScope::CallScope(PS2IopHostAdapter &owner, R5900Context *context, uint8_t *rdram)
    : m_lock(owner.m_callMutex),
      m_owner(&owner),
      m_previousContext(owner.m_activeContext),
      m_previousRdram(owner.m_activeRdram),
      m_previousToken(owner.m_activeToken)
{
    m_token = owner.m_nextToken++;
    if (m_token == 0)
    {
        m_token = owner.m_nextToken++;
    }
    owner.m_activeContext = context;
    owner.m_activeRdram = rdram;
    owner.m_activeToken = m_token;
}

PS2IopHostAdapter::CallScope::~CallScope()
{
    release();
}

PS2IopHostAdapter::CallScope::CallScope(CallScope &&other) noexcept
    : m_lock(std::move(other.m_lock)),
      m_owner(std::exchange(other.m_owner, nullptr)),
      m_previousContext(other.m_previousContext),
      m_previousRdram(other.m_previousRdram),
      m_previousToken(other.m_previousToken),
      m_token(other.m_token)
{
}

PS2IopHostAdapter::CallScope &PS2IopHostAdapter::CallScope::operator=(CallScope &&other) noexcept
{
    if (this != &other)
    {
        release();
        m_lock = std::move(other.m_lock);
        m_owner = std::exchange(other.m_owner, nullptr);
        m_previousContext = other.m_previousContext;
        m_previousRdram = other.m_previousRdram;
        m_previousToken = other.m_previousToken;
        m_token = other.m_token;
    }
    return *this;
}

void PS2IopHostAdapter::CallScope::release()
{
    if (m_owner)
    {
        m_owner->m_activeContext = m_previousContext;
        m_owner->m_activeRdram = m_previousRdram;
        m_owner->m_activeToken = m_previousToken;
        m_owner = nullptr;
    }
}

PS2IopHostAdapter::PS2IopHostAdapter(PS2Runtime &runtime)
    : m_runtime(runtime)
{
}

PS2IopHostAdapter::~PS2IopHostAdapter()
{
    stopAudio();
    std::lock_guard<std::mutex> lock(m_hostFileMutex);
    for (auto &[handle, file] : m_hostFiles)
    {
        (void)handle;
        if (file.stream)
        {
            std::fclose(file.stream);
        }
    }
    m_hostFiles.clear();
}

PS2IopHostAdapter::CallScope PS2IopHostAdapter::enterCall(R5900Context *context, uint8_t *rdram)
{
    return CallScope(*this, context, rdram);
}

bool PS2IopHostAdapter::guestRange(uint32_t address, size_t size, uint8_t *&begin) const
{
    begin = nullptr;
    uint8_t *const rdram = m_activeRdram
                               ? m_activeRdram
                               : m_runtime.memory().getRDRAM();
    if (!rdram)
    {
        return false;
    }
    if (size == 0)
    {
        begin = getMemPtr(rdram, address);
        return begin != nullptr;
    }
    if (size - 1 > std::numeric_limits<uint32_t>::max() - address)
    {
        return false;
    }
    uint8_t *const first = getMemPtr(rdram, address);
    uint8_t *const last = getMemPtr(rdram, address + static_cast<uint32_t>(size - 1));
    if (!first || !last || last < first || static_cast<size_t>(last - first) != size - 1)
    {
        return false;
    }
    begin = first;
    return true;
}

bool PS2IopHostAdapter::readGuest(uint32_t address, void *destination, size_t size) const
{
    if (!destination && size != 0)
    {
        return false;
    }
    uint8_t *source = nullptr;
    if (!guestRange(address, size, source))
    {
        return false;
    }
    if (size != 0)
    {
        std::memcpy(destination, source, size);
    }
    return true;
}

bool PS2IopHostAdapter::writeGuest(uint32_t address, const void *source, size_t size)
{
    if (!source && size != 0)
    {
        return false;
    }
    uint8_t *destination = nullptr;
    if (!guestRange(address, size, destination))
    {
        return false;
    }
    if (size != 0)
    {
        uint8_t *const rdram = m_activeRdram ? m_activeRdram : m_runtime.memory().getRDRAM();
        ps2TraceGuestRangeWrite(rdram, address, static_cast<uint32_t>(size), "IopHost::writeGuest", nullptr);
        std::memcpy(destination, source, size);
    }
    return true;
}

bool PS2IopHostAdapter::zeroGuest(uint32_t address, size_t size)
{
    uint8_t *destination = nullptr;
    if (!guestRange(address, size, destination))
    {
        return false;
    }
    if (size != 0)
    {
        uint8_t *const rdram = m_activeRdram ? m_activeRdram : m_runtime.memory().getRDRAM();
        ps2TraceGuestRangeWrite(rdram, address, static_cast<uint32_t>(size), "IopHost::zeroGuest", nullptr);
        std::memset(destination, 0, size);
    }
    return true;
}

bool PS2IopHostAdapter::normalizeGuestAddress(uint32_t address, uint32_t &normalized) const
{
    bool scratchpad = false;
    if (!ps2ResolveGuestPointer(address, normalized, scratchpad) || scratchpad)
    {
        normalized = 0;
        return false;
    }
    return true;
}

bool PS2IopHostAdapter::readIopMemory(uint32_t address, void *destination, size_t size) const
{
    return m_runtime.readIopMemory(address, destination, size);
}

bool PS2IopHostAdapter::writeIopMemory(uint32_t address, const void *source, size_t size)
{
    return m_runtime.writeIopMemory(address, source, size);
}

bool PS2IopHostAdapter::zeroIopMemory(uint32_t address, size_t size)
{
    return m_runtime.zeroIopMemory(address, size);
}

bool PS2IopHostAdapter::normalizeIopAddress(uint32_t address, uint32_t &normalized) const
{
    if (!m_runtime.isIopMemoryRange(address, 0u))
    {
        normalized = 0u;
        return false;
    }
    normalized = address & 0x1FFFFFFFu;
    return true;
}

uint32_t PS2IopHostAdapter::allocateIopHandle(ps2x::iop::IopHandleKind kind)
{
    uint8_t *const rdram = m_activeRdram
                               ? m_activeRdram
                               : m_runtime.memory().getRDRAM();
    if (!rdram)
    {
        return 0;
    }
    return kind == ps2x::iop::IopHandleKind::RpcPacket
               ? rpcAllocPacketAddr(rdram)
               : rpcAllocServerAddr(rdram);
}

uint32_t PS2IopHostAdapter::allocateGuest(uint32_t size, uint32_t alignment)
{
    return m_runtime.guestMalloc(size, alignment);
}

void PS2IopHostAdapter::freeGuest(uint32_t address)
{
    m_runtime.guestFree(address);
}

void PS2IopHostAdapter::audioCommand(uint32_t sid,
                                     uint32_t function,
                                     ps2x::iop::GuestBuffer send,
                                     ps2x::iop::GuestBuffer receive)
{
    // This upstream HLE service plays decoded VAG clips through raylib.
    // Titles running their original IOP sound module use the SDL PCM stream.
    if (!m_legacyAudioAttempted) {
        m_legacyAudioAttempted = true;
        InitAudioDevice();
        m_runtime.audioBackend().setAudioReady(IsAudioDeviceReady());
        if (IsAudioDeviceReady()) SetMasterVolume(m_runtime.nativeHost().Volume());
    }
    uint8_t *sendPointer = nullptr;
    uint8_t *receivePointer = nullptr;
    if (send.address && !guestRange(send.address, send.size, sendPointer))
    {
        sendPointer = nullptr;
    }
    if (receive.address && !guestRange(receive.address, receive.size, receivePointer))
    {
        receivePointer = nullptr;
    }
    m_runtime.audioBackend().onSoundCommand(sid,
                                            function,
                                            sendPointer,
                                            send.size,
                                            receivePointer,
                                            receive.size);
}

std::string PS2IopHostAdapter::hostPath(ps2x::iop::HostPathKind kind) const
{
    const PS2Runtime::IoPaths &paths = PS2Runtime::getIoPaths();
    switch (kind)
    {
    case ps2x::iop::HostPathKind::CdRoot:
        return paths.cdRoot.string();
    case ps2x::iop::HostPathKind::CdImage:
        return paths.cdImage.string();
    case ps2x::iop::HostPathKind::HostRoot:
        return paths.hostRoot.string();
    case ps2x::iop::HostPathKind::MemoryCardRoot:
        return paths.mcRoot.string();
    case ps2x::iop::HostPathKind::ElfDirectory:
    default:
        return paths.elfDirectory.string();
    }
}

std::string PS2IopHostAdapter::translateGuestPath(std::string_view path) const
{
    const PS2Runtime::IoPaths &paths = PS2Runtime::getIoPaths();
    const PS2VfsMounts mounts{paths.hostRoot, paths.cdRoot, paths.mcRoot};
    std::filesystem::path hostPath;
    if (!m_runtime.vfs().resolveHostPath(path, mounts, hostPath))
        return {};
    return hostPath.string();
}

uint64_t PS2IopHostAdapter::openHostFile(std::string_view path)
{
    if (path.empty())
    {
        return 0u;
    }

    const std::filesystem::path hostPath{std::string(path)};
    const auto &paths = PS2Runtime::getIoPaths();
#if defined(_WIN32)
    std::FILE *stream = ::_wfopen(hostPath.c_str(), hostPath == paths.cdImage ? L"rbS" : L"rb");
#else
    std::FILE *stream = std::fopen(hostPath.string().c_str(), "rb");
#endif
    std::error_code error;
    uint64_t size = stream ? std::filesystem::file_size(hostPath, error) : 0u;
    uint64_t base = 0u;
    if (!stream)
    {
        const auto relative = hostPath.lexically_normal().lexically_relative(paths.cdRoot.lexically_normal());
        if (paths.cdImage.empty() || relative.empty() || relative.is_absolute() ||
            std::find(relative.begin(), relative.end(), "..") != relative.end()) return 0u;
        const auto imageSize = std::filesystem::file_size(paths.cdImage, error);
        if (error) return 0u;
        if (relative == ".") size = imageSize;
        else
        {
            std::ifstream image(paths.cdImage, std::ios::binary);
            const auto file = cave::ps2::FindIsoFile(image, imageSize, relative.generic_string());
            if (!file) return 0u;
            base = uint64_t(file->lba) * 2048u; size = file->size;
        }
#if defined(_WIN32)
        stream = ::_wfopen(paths.cdImage.c_str(), L"rbS");
#else
        stream = std::fopen(paths.cdImage.string().c_str(), "rb");
#endif
        if (!stream) return 0u;
    }
    if (error)
    {
        std::fclose(stream);
        return 0u;
    }

    std::lock_guard<std::mutex> lock(m_hostFileMutex);
    uint64_t handle = m_nextHostFileHandle++;
    if (handle == 0u)
    {
        handle = m_nextHostFileHandle++;
    }
    m_hostFiles.emplace(handle, HostFile{stream, size, base});
    return handle;
}

bool PS2IopHostAdapter::hostFileSize(uint64_t handle, uint64_t &size) const
{
    size = 0u;
    std::lock_guard<std::mutex> lock(m_hostFileMutex);
    const auto it = m_hostFiles.find(handle);
    if (it == m_hostFiles.end() || !it->second.stream)
    {
        return false;
    }
    size = it->second.size;
    return true;
}

bool PS2IopHostAdapter::readHostFile(uint64_t handle,
                                     uint64_t offset,
                                     void *destination,
                                     size_t size,
                                     size_t &bytesRead)
{
    cave::ps2::frame_trace::SlowScope slow("disc-read",size);
    bytesRead = 0u;
    if (!destination && size != 0u)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_hostFileMutex);
    const auto it = m_hostFiles.find(handle);
    if (it == m_hostFiles.end() || !it->second.stream)
    {
        return false;
    }

    size = size_t(std::min<uint64_t>(size, it->second.size - std::min(offset, it->second.size)));
    offset += it->second.base;
    if (it->second.position != offset)
    {
#if defined(_WIN32)
        if (offset > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
            _fseeki64(it->second.stream, static_cast<int64_t>(offset), SEEK_SET) != 0)
#else
        if (offset > static_cast<uint64_t>(std::numeric_limits<off_t>::max()) ||
            fseeko(it->second.stream, static_cast<off_t>(offset), SEEK_SET) != 0)
#endif
        {
            it->second.position = UINT64_MAX;
            return false;
        }
    }

    bytesRead = size == 0u
                    ? 0u
                    : std::fread(destination, 1u, size, it->second.stream);
    it->second.position = offset + bytesRead;
    if (bytesRead < size && std::ferror(it->second.stream))
    {
        it->second.position = UINT64_MAX;
        std::clearerr(it->second.stream);
        return false;
    }
    return true;
}

void PS2IopHostAdapter::closeHostFile(uint64_t handle)
{
    std::FILE *stream = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_hostFileMutex);
        const auto it = m_hostFiles.find(handle);
        if (it == m_hostFiles.end())
        {
            return;
        }
        stream = it->second.stream;
        m_hostFiles.erase(it);
    }
    if (stream)
    {
        std::fclose(stream);
    }
}

int32_t PS2IopHostAdapter::memoryCard(const ps2x::iop::MemoryCardRequest &request)
{
    using Handler = void (*)(uint8_t *, R5900Context *, PS2Runtime *);
    Handler handler = nullptr;
    switch (request.operation)
    {
    case ps2x::iop::MemoryCardOperation::Init:
        handler = ps2_stubs::sceMcInit;
        break;
    case ps2x::iop::MemoryCardOperation::GetInfo:
        handler = ps2_stubs::sceMcGetInfo;
        break;
    case ps2x::iop::MemoryCardOperation::Open:
        handler = ps2_stubs::sceMcOpen;
        break;
    case ps2x::iop::MemoryCardOperation::Close:
        handler = ps2_stubs::sceMcClose;
        break;
    case ps2x::iop::MemoryCardOperation::Seek:
        handler = ps2_stubs::sceMcSeek;
        break;
    case ps2x::iop::MemoryCardOperation::Read:
        handler = ps2_stubs::sceMcRead;
        break;
    case ps2x::iop::MemoryCardOperation::Write:
        handler = ps2_stubs::sceMcWrite;
        break;
    case ps2x::iop::MemoryCardOperation::Flush:
        handler = ps2_stubs::sceMcFlush;
        break;
    case ps2x::iop::MemoryCardOperation::Chdir:
        handler = ps2_stubs::sceMcChdir;
        break;
    case ps2x::iop::MemoryCardOperation::GetDir:
        handler = ps2_stubs::sceMcGetDir;
        break;
    case ps2x::iop::MemoryCardOperation::SetFileInfo:
        handler = ps2_stubs::sceMcSetFileInfo;
        break;
    case ps2x::iop::MemoryCardOperation::Delete:
        handler = ps2_stubs::sceMcDelete;
        break;
    case ps2x::iop::MemoryCardOperation::Format:
        handler = ps2_stubs::sceMcFormat;
        break;
    case ps2x::iop::MemoryCardOperation::Unformat:
        handler = ps2_stubs::sceMcUnformat;
        break;
    case ps2x::iop::MemoryCardOperation::Mkdir:
        handler = ps2_stubs::sceMcMkdir;
        break;
    }
    if (!handler)
    {
        return -1;
    }

    R5900Context context{};
    for (size_t i = 0; i < 4u; ++i)
    {
        setRegU32(&context, static_cast<int>(4 + i), request.arguments[i]);
    }

    // EE n32 ABI: the fifth argument travels in $t0, matching the sceMc* stubs.
    setRegU32(&context, 8, request.arguments[4]);

    handler(m_activeRdram ? m_activeRdram : m_runtime.memory().getRDRAM(),
            &context,
            &m_runtime);
    return ps2_stubs::getMemoryCardDebugSnapshot().lastResult;
}

bool PS2IopHostAdapter::hasGuestFunction(uint32_t address) const
{
    return m_runtime.hasFunction(address);
}

bool PS2IopHostAdapter::invokeGuestFunction(uint64_t callToken,
                                            uint32_t address,
                                            uint32_t a0,
                                            uint32_t a1,
                                            uint32_t a2,
                                            uint32_t a3,
                                            uint32_t *resultAddress)
{
    (void)callToken;
    (void)address;
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    if (resultAddress)
    {
        *resultAddress = 0u;
    }
    return false;
}

bool PS2IopHostAdapter::sendSifCommand(uint32_t commandId,
                                       const void *packet,
                                       size_t packetSize)
{
    uint8_t *const rdram = m_activeRdram
                               ? m_activeRdram
                               : m_runtime.memory().getRDRAM();
    return ps2_stubs::dispatchSifCommand(rdram,
                                         &m_runtime,
                                         commandId,
                                         packet,
                                         packetSize);
}

bool PS2IopHostAdapter::FindDiscFile(std::string_view path,uint32_t& lba,uint32_t& size) const
{
    const auto& imagePath=PS2Runtime::getIoPaths().cdImage;
    std::error_code error;
    const auto imageSize=std::filesystem::file_size(imagePath,error);
    if(error)return false;
    std::ifstream image(imagePath,std::ios::binary);
    const auto file=cave::ps2::FindIsoFile(image,imageSize,path);
    if(!file)return false;
    lba=file->lba;size=file->size;return true;
}

uint32_t PS2IopHostAdapter::FindEeRpc(uint32_t sid) const
{
    std::lock_guard lock(g_rpc_mutex);
    const auto found = g_rpc_servers.find(sid);
    return found == g_rpc_servers.end() ? 0u : found->second.sd_ptr;
}

bool PS2IopHostAdapter::QueueEeRpc(uint32_t server, uint32_t function, std::span<const uint8_t> request,
                                  uint32_t receiveSize, std::function<void(std::span<const uint8_t>)> complete)
{
    t_SifRpcServerData descriptor{};
    if (!server || !readGuest(server, &descriptor, sizeof(descriptor)) || !hasGuestFunction(descriptor.func))
        return false;
    const uint32_t packet = allocateGuest(std::max<uint32_t>(16u, uint32_t(request.size())), 64u);
    if (!packet) return false;
    if (!request.empty() && !writeGuest(packet, request.data(), request.size())) { freeGuest(packet); return false; }
    auto &scheduler = m_runtime.eeScheduler();
    GuestInvocation invocation{};
    invocation.kind = GuestInvocationKind::RpcCallback;
    if (const auto *context = scheduler.currentContext()) invocation.context = *context;
    invocation.context.pc = descriptor.func;
    SET_GPR_U32(&invocation.context, 4, function);
    SET_GPR_U32(&invocation.context, 5, packet);
    SET_GPR_U32(&invocation.context, 6, uint32_t(request.size()));
    SET_GPR_U32(&invocation.context, 7, 0u);
    SET_GPR_U32(&invocation.context, 29, 0u);
    SET_GPR_U32(&invocation.context, 31, 0u);
    invocation.onComplete = [this, packet, receiveSize, complete = std::move(complete)]
        (const R5900Context &returned, R5900Context &) {
        std::vector<uint8_t> reply(receiveSize);
        const uint32_t address = uint32_t(_mm_cvtsi128_si32(returned.r[2]));
        if (receiveSize && !readGuest(address, reply.data(), reply.size()))
            throw std::runtime_error("EE RPC returned an invalid response buffer");
        freeGuest(packet);
        complete(reply);
    };
    scheduler.queueInvocation(std::move(invocation));
    return true;
}

void PS2IopHostAdapter::log(ps2x::iop::LogLevel level, std::string_view message)
{
    const char *prefix = "[ps2xIOP]";
    if (level == ps2x::iop::LogLevel::Warning)
    {
        prefix = "[ps2xIOP:warning]";
    }
    else if (level == ps2x::iop::LogLevel::Error)
    {
        prefix = "[ps2xIOP:error]";
    }
    std::cerr << prefix << ' ' << message << std::endl;
}
