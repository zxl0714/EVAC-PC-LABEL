#pragma once

#include "ps2x/iop/iop_host.h"
#include "ps2x/iop/iop_types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <span>

namespace cave::ps2 { struct IopRpcReply; }

namespace ps2x::iop::detail
{
    class IopEmulator
    {
    public:
        explicit IopEmulator(IopHost &host);
        ~IopEmulator();
        std::vector<uint8_t> saveState() const;
        void loadState(std::span<const uint8_t> bytes);

        IopEmulator(const IopEmulator &) = delete;
        IopEmulator &operator=(const IopEmulator &) = delete;

        void reset();
        [[nodiscard]] ModuleLoadResult loadModule(std::string_view path, const void *arguments, uint32_t argumentSize);
        [[nodiscard]] ModuleLoadResult loadModuleBuffer(uint32_t guestAddress, const void *arguments, uint32_t argumentSize);
        [[nodiscard]] ModuleLoadResult loadModuleFromIopRam(uint32_t address, const void *arguments, uint32_t argumentSize);
        [[nodiscard]] bool stopModule(int32_t moduleId, int32_t *result);
        void runEeCycles(uint64_t eeCycles) noexcept;
        void setAudioVolumes(unsigned stream, unsigned voices) noexcept;
        void discardAudio() noexcept;
        size_t readPcm(void *stereo, size_t frames) noexcept;
        [[nodiscard]] RpcResult handleRpc(const RpcRequest &request);
        void completeEeRpc(const cave::ps2::IopRpcReply& reply, std::span<const uint8_t> bytes);
        [[nodiscard]] bool hasRpcServer(uint32_t sid) const noexcept;
        void onSifTransfer(const SifTransfer &transfer);

        [[nodiscard]] uint32_t allocateMemory(uint32_t size, uint32_t alignment = 16u);
        [[nodiscard]] bool freeMemory(uint32_t address);
        [[nodiscard]] bool readMemory(uint32_t address, void *destination, size_t size) const;
        [[nodiscard]] bool writeMemory(uint32_t address, const void *source, size_t size);
        [[nodiscard]] bool zeroMemory(uint32_t address, size_t size);
        [[nodiscard]] bool isMemoryRange(uint32_t address, size_t size) const;

        [[nodiscard]] uint64_t cycles() const noexcept;
        [[nodiscard]] uint64_t instructions() const noexcept;
        [[nodiscard]] uint32_t loadedModuleCount() const noexcept;
        [[nodiscard]] uint32_t threadCount() const noexcept;
        [[nodiscard]] uint32_t rpcServerCount() const noexcept;

    private:
        class Impl;
        template<class Archive> static void transferState(Archive& ar, Impl& state);
        std::unique_ptr<Impl> m_impl;
        unsigned m_streamVolume = 100, m_voiceVolume = 100;
    };
}
