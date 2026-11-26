#include "emulator/imports/iop_ioman.h"

#include "emulator/core/iop_cpu.h"
#include "emulator/core/iop_memory.h"
#include "emulator/services/iop_rpc.h"
#include "ps2x/iop/iop_host.h"
#include "ps2x/iop/ps2_path.h"
#include "../../../backends/ps2/audio_trace.h"
#include "../../../backends/ps2/state_archive.h"

#include <algorithm>

namespace ps2x::iop::detail
{
    namespace {
        constexpr uint32_t kMaxDevices = 16;
        bool isMountedFile(std::string_view path)
        {
            const auto device = parsePs2Path(path).device;
            return device == Ps2PathDevice::Cdrom || device == Ps2PathDevice::Host ||
                   device == Ps2PathDevice::MemoryCard0;
        }
    }
    IopIoman::IopIoman(IopMemory &memory, IopHost &host) noexcept
        : m_memory(memory), m_host(host)
    {
    }

    IopIoman::~IopIoman() { reset(); }

    void IopIoman::reset()
    {
        m_devices.clear();
        for (auto &[id, file] : m_files)
            if (file.handle) m_host.closeHostFile(file.handle);
        m_files.clear();
        m_nextFile = 3;
    }

    std::vector<uint8_t> IopIoman::saveState() const
    {
        std::vector<uint8_t> state;
        cave::ps2::StateWriter writer(state);
        writer.Fields(uint32_t{1}, m_nextFile, static_cast<uint32_t>(m_devices.size()));
        for (const auto &device : m_devices)
        {
            writer.Fields(device.address, device.gp);
            writer.String(device.name);
        }
        writer.Field(static_cast<uint32_t>(m_files.size()));
        for (const auto &[id, file] : m_files)
        {
            if (!isMountedFile(file.guestPath))
                throw std::runtime_error("An open IOP file has no guest mount");
            writer.Fields(id, file.size, file.offset);
            writer.String(file.guestPath);
        }
        return state;
    }

    void IopIoman::loadState(std::span<const uint8_t> state)
    {
        IopIoman next(m_memory, m_host);
        cave::ps2::StateReader reader(state);
        uint32_t version{};
        reader.Fields(version, next.m_nextFile);
        if (version != 1 || next.m_nextFile < 3)
            throw std::invalid_argument("Invalid IOP file descriptor state");
        const auto drivers = reader.Count(kMaxDevices, 13);
        for (uint32_t i = 0; i < drivers; ++i)
        {
            auto &driver = next.m_devices.emplace_back();
            reader.Fields(driver.address, driver.gp);
            driver.name = reader.String(64);
            if (!driver.address || driver.name.empty())
                throw std::invalid_argument("Invalid IOP driver state");
        }
        const auto openFiles = reader.Count(static_cast<uint32_t>(next.m_nextFile - 3), 25);
        for (uint32_t i = 0; i < openFiles; ++i)
        {
            int id{};
            reader.Field(id);
            if (id < 3 || id >= next.m_nextFile || next.m_files.contains(id))
                throw std::invalid_argument("Invalid IOP file descriptor");
            auto &file = next.m_files[id];
            reader.Fields(file.size, file.offset);
            file.guestPath = reader.String(1024);
            if (!isMountedFile(file.guestPath) || file.offset > std::max<uint64_t>(file.size, INT32_MAX))
                throw std::invalid_argument("Invalid IOP open file state");
        }
        reader.Finish();
        for (auto &[id, file] : next.m_files)
        {
            const auto translated = m_host.translateGuestPath(file.guestPath);
            file.handle = translated.empty() ? 0 : m_host.openHostFile(translated);
            uint64_t size{};
            if (!file.handle || !m_host.hostFileSize(file.handle, size) || file.size != size)
                throw std::runtime_error("Cannot reopen IOP file from state");
        }
        m_files.swap(next.m_files);
        m_devices.swap(next.m_devices);
        m_nextFile = next.m_nextFile;
    }

    bool IopIoman::dispatchImport(uint16_t ordinal, IopCpuState &cpu, IopGuestExecutor &executor)
    {
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
            if (m_nextFile == INT32_MAX) { setV0(uint32_t(-24)); return true; }
            const auto translated = m_host.translateGuestPath(path);
            const auto handle = m_host.openHostFile(translated);
            uint64_t size{};
            if (!handle || !m_host.hostFileSize(handle, size)) {
                if (handle) m_host.closeHostFile(handle);
                setV0(uint32_t(-2)); return true;
            }
            const int id = m_nextFile++;
            m_files.emplace(id, File{handle, size, 0, path});
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
