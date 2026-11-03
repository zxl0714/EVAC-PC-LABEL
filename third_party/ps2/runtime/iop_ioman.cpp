#include "emulator/imports/iop_ioman.h"

#include "emulator/core/iop_cpu.h"
#include "emulator/core/iop_memory.h"
#include "emulator/services/iop_rpc.h"
#include "ps2x/iop/iop_host.h"
#include "../../../backends/ps2/audio_trace.h"

#include <algorithm>

namespace ps2x::iop::detail
{
    IopIoman::IopIoman(IopMemory &memory, IopHost &host) noexcept
        : m_memory(memory), m_host(host)
    {
    }

    IopIoman::~IopIoman() { reset(); }

    void IopIoman::reset()
    {
        m_devices.clear();
        for (auto &[id, file] : m_files) m_host.closeHostFile(file.handle);
        m_files.clear();
        m_nextFile = 3;
    }

    bool IopIoman::dispatchImport(uint16_t ordinal, IopCpuState &cpu, IopGuestExecutor &executor)
    {
        constexpr size_t kMaxDevices = 16u;
        const uint32_t a0 = cpu.gpr[4];
        const auto setV0 = [&](uint32_t value)
        {
            cpu.gpr[2] = value;
        };

        switch (ordinal)
        {
        case 4: // open
        {
            const auto path = m_memory.readString(a0, 1024u);
            if (cave::ps2::audio_trace::enabled) {
                static unsigned logged = 0;
                if (++logged <= 32u) std::osyncstream(std::cerr) << "[ps2-iop-open] " << path << '\n';
            }
            if ((cpu.gpr[5] & 3u) != 1u) { setV0(uint32_t(-30)); return true; }
            const auto translated = m_host.translateGuestPath(path);
            const auto handle = m_host.openHostFile(translated);
            uint64_t size{};
            if (!handle || !m_host.hostFileSize(handle, size)) {
                if (handle) m_host.closeHostFile(handle);
                setV0(uint32_t(-2)); return true;
            }
            const int id = m_nextFile++;
            m_files.emplace(id, File{handle, size, 0});
            setV0(uint32_t(id)); return true;
        }
        case 5: // close
        {
            const auto file = m_files.find(int(a0));
            if (file == m_files.end()) { setV0(uint32_t(-9)); return true; }
            m_host.closeHostFile(file->second.handle); m_files.erase(file);
            setV0(0); return true;
        }
        case 6: // read
        {
            const auto found = m_files.find(int(a0));
            if (found == m_files.end()) { setV0(uint32_t(-9)); return true; }
            auto &file = found->second;
            const uint32_t requested = cpu.gpr[6];
            if (requested > IopMemory::RamSize || !m_memory.ownsRamRange(cpu.gpr[5], requested))
            { setV0(uint32_t(-14)); return true; }
            const auto size = size_t(std::min<uint64_t>(requested, file.size - std::min(file.offset, file.size)));
            std::vector<uint8_t> bytes(size); size_t read{};
            if (size && !m_host.readHostFile(file.handle, file.offset, bytes.data(), size, read))
            { setV0(uint32_t(-5)); return true; }
            if (read) (void)m_memory.writeRam(cpu.gpr[5], bytes.data(), read);
            file.offset += read; setV0(uint32_t(read)); return true;
        }
        case 7: // write (disc files are read-only)
            setV0(uint32_t(-30)); return true;
        case 8: // lseek
        {
            const auto found = m_files.find(int(a0));
            if (found == m_files.end()) { setV0(uint32_t(-9)); return true; }
            auto &file = found->second;
            const auto whence = cpu.gpr[6];
            if (whence > 2u) { setV0(uint32_t(-22)); return true; }
            const int64_t offset = (whence == 0u ? 0ll : whence == 1u ? int64_t(file.offset) : int64_t(file.size)) + int32_t(cpu.gpr[5]);
            if (offset < 0 || offset > INT32_MAX) { setV0(uint32_t(-22)); return true; }
            file.offset = uint64_t(offset); setV0(uint32_t(offset)); return true;
        }
        case 20: // AddDrv
        {
            if (a0 == 0u || m_devices.size() >= kMaxDevices)
            {
                setV0(0xFFFFFFFFu);
                return true;
            }

            const uint32_t nameAddress = m_memory.read32(a0);
            const uint32_t operations = m_memory.read32(a0 + 16u);
            const std::string name = m_memory.readString(nameAddress, 64u);
            if (nameAddress == 0u || operations == 0u || name.empty())
            {
                setV0(0xFFFFFFFFu);
                return true;
            }

            m_devices.push_back({a0, cpu.gpr[28], name});
            const uint32_t init = m_memory.read32(operations);
            if (init != 0u)
            {
                const int32_t result = static_cast<int32_t>(
                    executor.executeGuestFunction(init, a0, 0u, 0u, 0u, cpu.gpr[28]));
                if (result < 0)
                {
                    m_devices.pop_back();
                    setV0(0xFFFFFFFFu);
                    return true;
                }
            }

            setV0(0u);
            return true;
        }
        case 21: // DelDrv
        {
            const std::string name = m_memory.readString(a0, 64u);
            const auto device = std::find_if(
                m_devices.begin(), m_devices.end(),
                [&](const Device &candidate)
                { return candidate.name == name; });
            if (device == m_devices.end())
            {
                setV0(0xFFFFFFFFu);
                return true;
            }

            const uint32_t operations = m_memory.read32(device->address + 16u);
            const uint32_t deinit = operations != 0u ? m_memory.read32(operations + 4u) : 0u;
            if (deinit != 0u)
                (void)executor.executeGuestFunction(deinit, device->address, 0u, 0u, 0u, device->gp);
            m_devices.erase(device);
            setV0(0u);
            return true;
        }
        default:
            return false;
        }
    }
}
