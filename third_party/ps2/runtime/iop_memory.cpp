#include "emulator/core/iop_memory.h"
#include "../../../backends/ps2/audio_trace.h"
#include "../../../backends/ps2/spu_device.h"
#include "../../../backends/ps2/pcm_queue.h"

#include <algorithm>
#include <cstring>
#include <atomic>
#include <iostream>
#include <sstream>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kDmaSpu0Chcr = 0x1F8010C8u;
        constexpr uint32_t kDmaSpu1Chcr = 0x1F801508u;
        constexpr uint32_t kDmaStart = 1u << 24u;
        constexpr int kDmaSpu0Irq = 0x24;
        constexpr int kDmaSpu1Irq = 0x28;

        uint32_t alignUp(uint32_t value, uint32_t alignment)
        {
            return (value + alignment - 1u) & ~(alignment - 1u);
        }
    }

    class IopMemory::AudioState
    {
    public:
        cave::ps2::SpuDevice spu;
        cave::ps2::PcmQueue queue;
        std::array<uint16_t, 0x8000> writtenRegisters{};
        struct Dma { uint64_t bytes = 0; uint32_t address = 0; bool toSpu = false; bool decrement = false; };
        std::array<Dma, 2> dma{};
        static constexpr uint64_t CyclesPerFrame = 768;
        static constexpr size_t QuantumFrames = 64;
        uint64_t nextCycle = CyclesPerFrame * QuantumFrames;
        uint64_t frames = 0, nonzero = 0, dropped = 0;
        std::atomic<uint64_t> readFrames{}, underruns{};
        std::array<int16_t, QuantumFrames * 2> samples{};
    };

    IopMemory::IopMemory()
        : m_audio(std::make_unique<AudioState>()), m_ram(RamSize), m_owned(RamSize), m_scratch(ScratchSize)
    {
        reset();
    }

    IopMemory::~IopMemory() = default;

    void IopMemory::reset()
    {
        std::fill(m_ram.begin(), m_ram.end(), uint8_t{0});
        std::fill(m_owned.begin(), m_owned.end(), uint8_t{0});
        std::fill(m_scratch.begin(), m_scratch.end(), uint8_t{0});
        m_hardware.clear();
        m_allocations.clear();
        m_heapCursor = HeapBase;
        m_interruptStatus = 0;
        m_interruptMask = 0;
        m_interruptControl = 1;
        m_dmaStarts.clear();
        m_audio->spu.Reset();
        m_audio->queue.DiscardFromProducer();
        m_audio->writtenRegisters.fill(0);
        m_audio->dma = {};
        m_audio->nextCycle = AudioState::CyclesPerFrame * AudioState::QuantumFrames;
        m_audio->frames = m_audio->nonzero = m_audio->dropped = 0;
        m_audio->readFrames.store(0, std::memory_order_relaxed);
        m_audio->underruns.store(0, std::memory_order_relaxed);
    }

    uint32_t IopMemory::physicalAddress(uint32_t address) noexcept
    {
        return address & 0x1FFFFFFFu;
    }

    uint8_t IopMemory::read8(uint32_t address) const
    {
        const uint32_t phys = physicalAddress(address);
        if (phys >= Spu2Base && phys < Spu2Base + 0x10000u)
            return uint8_t(m_audio->spu.Read16(phys & ~1u) >> ((phys & 1u) * 8u));
        if (phys < RamSize)
            return m_ram[phys];
        if (phys >= ScratchBase && phys < ScratchBase + ScratchSize)
            return m_scratch[phys - ScratchBase];
        const uint32_t value = readHardware32(phys & ~3u);
        return static_cast<uint8_t>(value >> ((phys & 3u) * 8u));
    }

    uint16_t IopMemory::read16(uint32_t address) const
    {
        const uint32_t phys = physicalAddress(address);
        if (phys >= Spu2Base && phys < Spu2Base + 0x10000u && !(phys & 1u))
            return m_audio->spu.Read16(phys);
        if (phys + 1u < RamSize)
        {
            uint16_t value;
            std::memcpy(&value, m_ram.data() + phys, sizeof(value));
            return value;
        }
        return static_cast<uint16_t>(read8(address) | (static_cast<uint16_t>(read8(address + 1u)) << 8u));
    }

    uint32_t IopMemory::read32Slow(uint32_t address) const
    {
        const uint32_t phys = physicalAddress(address);
        if (phys >= Spu2Base && phys < Spu2Base + 0xfffeu && !(phys & 1u))
            return uint32_t(read16(phys)) | (uint32_t(read16(phys + 2u)) << 16u);
        if ((phys & 3u) == 0u && phys >= ScratchBase && phys + 3u < ScratchBase + ScratchSize)
        {
            uint32_t value;
            std::memcpy(&value, m_scratch.data() + (phys - ScratchBase), sizeof(value));
            return value;
        }
        if ((phys & 3u) == 0u && isHardwareAddress(phys))
            return readHardware32(phys);

        return static_cast<uint32_t>(read8(address)) |
               (static_cast<uint32_t>(read8(address + 1u)) << 8u) |
               (static_cast<uint32_t>(read8(address + 2u)) << 16u) |
               (static_cast<uint32_t>(read8(address + 3u)) << 24u);
    }

    void IopMemory::write8(uint32_t address, uint8_t value)
    {
        const uint32_t phys = physicalAddress(address);
        if (phys >= Spu2Base && phys < Spu2Base + 0x10000u)
        {
            const auto index = (phys - Spu2Base) / 2u;
            const auto shift = (phys & 1u) * 8u;
            const auto old = m_audio->writtenRegisters[index];
            writeSpu16(phys & ~1u, uint16_t((old & ~(0xffu << shift)) | (uint32_t(value) << shift)));
            return;
        }
        if (phys < RamSize)
        {
            m_ram[phys] = value;
            markOwned(phys, sizeof(value));
            return;
        }
        if (phys >= ScratchBase && phys < ScratchBase + ScratchSize)
        {
            m_scratch[phys - ScratchBase] = value;
            return;
        }
        const uint32_t aligned = phys & ~3u;
        uint32_t current = readHardware32(aligned);
        const uint32_t shift = (phys & 3u) * 8u;
        current = (current & ~(0xFFu << shift)) | (static_cast<uint32_t>(value) << shift);
        writeHardware32(aligned, current);
    }

    void IopMemory::write16(uint32_t address, uint16_t value)
    {
        const uint32_t phys = physicalAddress(address);
        if (phys >= Spu2Base && phys < Spu2Base + 0x10000u && !(phys & 1u))
        {
            writeSpu16(phys, value);
            return;
        }
        if (phys + 1u < RamSize)
        {
            std::memcpy(m_ram.data() + phys, &value, sizeof(value));
            markOwned(phys, sizeof(value));
            return;
        }
        write8(address, static_cast<uint8_t>(value));
        write8(address + 1u, static_cast<uint8_t>(value >> 8u));
    }

    void IopMemory::write32(uint32_t address, uint32_t value)
    {
        const uint32_t phys = physicalAddress(address);
        if (phys >= Spu2Base && phys < Spu2Base + 0xfffeu && !(phys & 1u))
        {
            writeSpu16(phys, uint16_t(value));
            writeSpu16(phys + 2u, uint16_t(value >> 16u));
            return;
        }
        if ((phys & 3u) == 0u && phys + 3u < RamSize)
        {
            std::memcpy(m_ram.data() + phys, &value, sizeof(value));
            markOwned(phys, sizeof(value));
            return;
        }
        if ((phys & 3u) == 0u && phys >= ScratchBase && phys + 3u < ScratchBase + ScratchSize)
        {
            std::memcpy(m_scratch.data() + (phys - ScratchBase), &value, sizeof(value));
            return;
        }
        if ((phys & 3u) == 0u)
        {
            writeHardware32(phys, value);
            return;
        }
        write8(address, static_cast<uint8_t>(value));
        write8(address + 1u, static_cast<uint8_t>(value >> 8u));
        write8(address + 2u, static_cast<uint8_t>(value >> 16u));
        write8(address + 3u, static_cast<uint8_t>(value >> 24u));
    }

    bool IopMemory::readRam(uint32_t address, void *destination, size_t size) const
    {
        const uint32_t phys = physicalAddress(address);
        if ((!destination && size != 0u) || phys > RamSize || size > RamSize - phys)
            return false;
        if (size != 0u)
            std::memcpy(destination, m_ram.data() + phys, size);
        return true;
    }

    bool IopMemory::writeRam(uint32_t address, const void *source, size_t size)
    {
        const uint32_t phys = physicalAddress(address);
        if ((!source && size != 0u) || phys > RamSize || size > RamSize - phys)
            return false;
        if (size != 0u)
        {
            std::memcpy(m_ram.data() + phys, source, size);
            markOwned(phys, size);
        }
        return true;
    }

    bool IopMemory::zeroRam(uint32_t address, size_t size)
    {
        const uint32_t phys = physicalAddress(address);
        if (phys > RamSize || size > RamSize - phys)
            return false;
        if (size != 0u)
        {
            std::memset(m_ram.data() + phys, 0, size);
            markOwned(phys, size);
        }
        return true;
    }

    bool IopMemory::ownsRamRange(uint32_t address, size_t size) const
    {
        const uint32_t phys = physicalAddress(address);
        if (phys > RamSize || size > RamSize - phys)
            return false;
        return std::all_of(m_owned.begin() + phys, m_owned.begin() + phys + size,
                           [](uint8_t value)
                           { return value != 0u; });
    }

    void IopMemory::markOwned(uint32_t address, size_t size)
    {
        if (address > RamSize || size > RamSize - address)
            return;
        std::fill(m_owned.begin() + address, m_owned.begin() + address + size, uint8_t{1});
    }

    bool IopMemory::isHardwareAddress(uint32_t address) const
    {
        const uint32_t phys = physicalAddress(address);
        return (phys >= HardwareBase && phys < HardwareEnd) ||
               (phys >= Spu2Base && phys < Spu2End) ||
               (phys >= SifBase && phys < SifEnd);
    }

    uint32_t IopMemory::readHardware32(uint32_t address) const
    {
        const auto value = m_hardware.find(address);
        if (value != m_hardware.end())
            return value->second;
        switch (address)
        {
        case 0x1F801070u:
            return m_interruptStatus;
        case 0x1F801074u:
            return m_interruptMask;
        case 0x1F801078u:
            return m_interruptControl;
        default:
            return 0u;
        }
    }

    void IopMemory::writeHardware32(uint32_t address, uint32_t value)
    {
        switch (address)
        {
        case 0x1F801070u:
            m_interruptStatus &= value;
            return;
        case 0x1F801074u:
            m_interruptMask = value;
            return;
        case 0x1F801078u:
            m_interruptControl = value & 1u;
            return;
        default:
            break;
        }

        m_hardware[address] = value;
        if ((address != kDmaSpu0Chcr && address != kDmaSpu1Chcr) || (value & kDmaStart) == 0u)
            return;

        const bool secondCore = address == kDmaSpu1Chcr;
        const uint32_t blockControlAddress = address - sizeof(uint32_t);
        uint32_t blockControl = 0u;
        if (const auto current = m_hardware.find(blockControlAddress); current != m_hardware.end())
            blockControl = current->second;
        const uint32_t wordsPerBlock = (blockControl & 0xFFFFu) ? (blockControl & 0xFFFFu) : 0x10000u;
        const uint32_t blockCount = ((value >> 9u) & 3u) == 0u ? 1u :
            ((blockControl >> 16u) ? (blockControl >> 16u) : 0x10000u);
        const uint64_t transferWords = static_cast<uint64_t>(wordsPerBlock) * blockCount;
        const uint32_t tsaBase=0x1F9001A8u+(secondCore?0x400u:0u);
        cave::ps2::audio_trace::Dma(secondCore?1u:0u,readHardware32(address-8u),transferWords,value,
            (uint32_t(read16(tsaBase))<<17u)|(uint32_t(read16(tsaBase+2u))<<1u));
        auto &dma = m_audio->dma[secondCore ? 1u : 0u];
        dma = {transferWords * 4u, readHardware32(address - 8u) & (RamSize - 4u),
               (value & 1u) != 0u, (value & 2u) != 0u};
        serviceSpuDma(secondCore ? 1u : 0u);
    }

    std::optional<IopMemory::DmaStart> IopMemory::takeDmaStart() noexcept
    {
        if (m_dmaStarts.empty()) return std::nullopt;
        const auto result = m_dmaStarts.front();
        m_dmaStarts.pop_front();
        return result;
    }

    void IopMemory::writeSpu16(uint32_t address, uint16_t value)
    {
        m_audio->writtenRegisters[(address - Spu2Base) / 2u] = value;
        cave::ps2::audio_trace::Register(address, value);
        m_audio->spu.Write16(address, value);
        // A driver may configure ATTR after starting its DMA channel.
        if ((address & 0x3ffu) == 0x19au || (address & 0x3ffu) == 0x1b0u)
            serviceSpuDma((address & 0x400u) ? 1u : 0u);
    }

    void IopMemory::serviceSpuDma(unsigned core)
    {
        auto &dma = m_audio->dma[core];
        const auto chcr = core ? kDmaSpu1Chcr : kDmaSpu0Chcr;
        if (!dma.bytes || !(readHardware32(chcr) & kDmaStart)) return;
        uint64_t transferred = 0;
        while (dma.bytes)
        {
            const auto bytes = size_t(std::min<uint64_t>(dma.bytes,
                dma.decrement ? 4u : RamSize - dma.address));
            const auto done = m_audio->spu.Transfer(core,
                std::span<uint8_t>(m_ram.data() + dma.address, bytes), dma.toSpu);
            if (!dma.toSpu && done) markOwned(dma.address, done);
            if (done) dma.address = (dma.address + (dma.decrement ? uint32_t(-4) : uint32_t(done))) & (RamSize - 1u);
            dma.bytes -= done;
            transferred += done;
            if (done < bytes) break;
        }
        m_hardware[chcr - 8u] = dma.address;
        if (!dma.bytes)
        {
            m_hardware[chcr] &= ~kDmaStart;
            m_dmaStarts.push_back({core ? kDmaSpu1Irq : kDmaSpu0Irq,
                                  std::max<uint64_t>(transferred / 2u, 64u)});
        }
    }

    void IopMemory::advanceAudioTo(uint64_t cycles)
    {
        while (cycles >= m_audio->nextCycle)
        {
            m_audio->nextCycle += AudioState::CyclesPerFrame * AudioState::QuantumFrames;
            m_audio->spu.Render(m_audio->samples);
            m_audio->frames += AudioState::QuantumFrames;
            m_audio->nonzero += std::count_if(m_audio->samples.begin(), m_audio->samples.end(),
                                             [](int16_t sample) { return sample != 0; });
            m_audio->dropped += AudioState::QuantumFrames - m_audio->queue.Write(m_audio->samples);
            serviceSpuDma(0);
            serviceSpuDma(1);
            if (cave::ps2::audio_trace::enabled && m_audio->frames % 96000u == 0u)
                std::osyncstream(std::cerr) << "[ps2-audio-pcm] frames=" << m_audio->frames
                    << " nonzero=" << m_audio->nonzero << " dropped=" << m_audio->dropped
                    << " read=" << m_audio->readFrames.load(std::memory_order_relaxed)
                    << " underruns=" << m_audio->underruns.load(std::memory_order_relaxed) << '\n';
        }
    }

    uint64_t IopMemory::nextAudioCycle() const noexcept { return m_audio->nextCycle; }
    bool IopMemory::audioIrqPending() const noexcept { return m_audio->spu.IrqPending(); }
    size_t IopMemory::readPcm(void *stereo, size_t frames) noexcept
    {
        if (!stereo || !frames) return 0;
        auto output = std::span<int16_t>(static_cast<int16_t *>(stereo), frames * 2u);
        const auto read = m_audio->queue.Read(output);
        std::fill(output.begin() + read * 2u, output.end(), int16_t{});
        m_audio->readFrames.fetch_add(read, std::memory_order_relaxed);
        if (read < frames) m_audio->underruns.fetch_add(1, std::memory_order_relaxed);
        return read;
    }

    uint32_t IopMemory::allocate(uint32_t size, uint32_t alignment, std::optional<uint32_t> fixed)
    {
        if (size > HeapLimit - HeapBase)
            return 0u;
        size = alignUp(std::max(size, 1u), 16u);
        alignment = std::max<uint32_t>(alignment, 4u);
        if ((alignment & (alignment - 1u)) != 0u || alignment > HeapLimit)
            return 0u;
        if (fixed)
        {
            const uint32_t address = *fixed;
            if (address < HeapBase || address > HeapLimit || size > HeapLimit - address ||
                (address & (alignment - 1u)) != 0u)
                return 0u;
            for (const auto &block : m_allocations)
                if (address < block.address + block.size && block.address < address + size)
                    return 0u;
            m_allocations.push_back({address, size});
            markOwned(address, size);
            return address;
        }

        // IRX services repeatedly allocate and release stream/RPC buffers.
        // A high-water cursor permanently loses every released region.
        uint32_t candidate = alignUp(HeapBase, alignment);
        for (;;)
        {
            bool overlap = false;
            for (const auto &block : m_allocations)
            {
                if (candidate < block.address + block.size && block.address < candidate + size)
                {
                    candidate = alignUp(block.address + block.size, alignment);
                    overlap = true;
                    break;
                }
            }
            if (!overlap)
                break;
        }
        if (candidate > HeapLimit || size > HeapLimit - candidate)
        {
            static std::atomic<unsigned> failures{};
            if (failures.fetch_add(1,std::memory_order_relaxed)<4u)
            {
                std::ostringstream line;
                line << "[ps2-iop-heap-layout] request=" << size
                     << " largest=" << maxFreeMemory() << " blocks=" << m_allocations.size()
                     << " range=0x" << std::hex << HeapBase << "..0x" << HeapLimit;
                for (const auto& block:m_allocations)
                    line << " [0x" << block.address << ",0x" << block.size << ']';
                line << '\n';
                std::cerr << line.str();
            }
            return 0u;
        }
        m_allocations.push_back({candidate, size});
        markOwned(candidate, size);
        m_heapCursor = std::max(m_heapCursor, candidate + size);
        return candidate;
    }

    bool IopMemory::freeAllocation(uint32_t address)
    {
        const auto block = std::find_if(m_allocations.begin(), m_allocations.end(),
                                        [&](const Allocation &candidate)
                                        { return candidate.address == address; });
        if (block == m_allocations.end())
            return false;
        std::fill(m_owned.begin() + block->address,
                  m_owned.begin() + block->address + block->size,
                  uint8_t{0});
        m_allocations.erase(block);
        return true;
    }

    uint32_t IopMemory::maxFreeMemory() const
    {
        auto blocks=m_allocations;
        std::sort(blocks.begin(),blocks.end(),[](const Allocation& a,const Allocation& b) {
            return a.address<b.address;
        });
        uint32_t cursor=HeapBase, largest=0u;
        for (const auto& block:blocks)
        {
            largest=std::max(largest,block.address-cursor);
            cursor=block.address+block.size;
        }
        return std::max(largest,HeapLimit-cursor);
    }

    std::optional<IopMemory::Allocation> IopMemory::allocationContaining(uint32_t address) const
    {
        const auto block = std::find_if(m_allocations.begin(), m_allocations.end(),
                                        [&](const Allocation &candidate)
                                        {
                                            return address >= candidate.address &&
                                                   address < candidate.address + candidate.size;
                                        });
        if (block == m_allocations.end())
            return std::nullopt;
        return *block;
    }

    std::string IopMemory::readString(uint32_t address, size_t limit) const
    {
        std::string result;
        result.reserve(std::min<size_t>(limit, 64u));
        for (size_t i = 0; i < limit; ++i)
        {
            const char ch = static_cast<char>(read8(address + static_cast<uint32_t>(i)));
            if (ch == '\0')
                break;
            result.push_back(ch);
        }
        return result;
    }
}
