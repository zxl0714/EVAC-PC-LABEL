#pragma once

#include <cstdint>
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
        struct File { uint64_t handle, size, offset; };
        std::map<int, File> m_files;
        int m_nextFile = 3;
        std::vector<Device> m_devices;
    };
}
