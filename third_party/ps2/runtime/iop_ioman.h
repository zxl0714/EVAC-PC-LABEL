#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <map>
namespace ps2x::iop { class IopHost; }

namespace ps2x::iop::detail
{
    struct IopCpuState;
    class IopGuestExecutor;
    class IopMemory;

    class IopIoman
    {
    public:
        explicit IopIoman(IopMemory &memory, IopHost &host) noexcept;
        ~IopIoman();

        [[nodiscard]] std::vector<uint8_t> saveState() const;
        void loadState(std::span<const uint8_t> state);
        void reset();
        [[nodiscard]] bool dispatchImport(uint16_t ordinal, IopCpuState &cpu, IopGuestExecutor &executor);

    private:
        struct Device
        {
            uint32_t address = 0u;
            uint32_t gp = 0u;
            std::string name;
        };

        IopMemory &m_memory;
        IopHost &m_host;
        struct File
        {
            uint64_t handle = 0, size = 0, offset = 0;
            std::string guestPath;
        };
        std::map<int, File> m_files;
        int m_nextFile = 3;
        std::vector<Device> m_devices;
    };
}
