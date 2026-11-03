#include "emulator/core/iop_kernel.h"

#include "emulator/core/iop_memory.h"
#include "emulator/iop_emulator_const.h"

#include <algorithm>
#include <sstream>
#include "../../../backends/ps2/audio_trace.h"

namespace ps2x::iop::detail
{
    namespace
    {
        uint32_t alignUp(uint32_t value, uint32_t alignment)
        {
            if (alignment <= 1u)
                return value;
            const uint32_t mask = alignment - 1u;
            return (value + mask) & ~mask;
        }
    }

    IopKernel::IopKernel(IopMemory &memory) noexcept
        : m_memory(memory)
    {
    }

    void IopKernel::reset()
    {
        m_readyCacheValid = false;
        m_cachedReady = nullptr;
        m_nextDelayedWake = UINT64_MAX;
        m_threads.clear();
        m_semaphores.clear();
        m_eventFlags.clear();
        m_mailboxes.clear();
        for (const auto &[id, pool] : m_fixedPools)
            (void)m_memory.freeAllocation(pool.base);
        m_fixedPools.clear();
        m_nextThreadId = 1;
        m_nextSemaphoreId = 1;
        m_nextEventFlagId = 1;
        m_nextMailboxId = 1;
        m_nextFixedPoolId = 1;
        m_nextWaitOrder = 1;
        m_currentThread = nullptr;
    }

    bool IopKernel::dispatchThreadImport(uint16_t ordinal, IopCpuState &cpu, uint64_t currentCycle)
    {
        m_readyCacheValid = false;
        const auto setV0 = [&](int32_t value)
        {
            cpu.gpr[2] = static_cast<uint32_t>(value);
        };

        switch (ordinal)
        {
        case 4: // CreateThread
        {
            const uint32_t descriptor = cpu.gpr[4];
            IopThread thread;
            thread.id = static_cast<int>(m_nextThreadId++);
            thread.attr = m_memory.read32(descriptor + 0u);
            thread.option = m_memory.read32(descriptor + 4u);
            thread.entry = m_memory.read32(descriptor + 8u);
            thread.stackSize = std::max<uint32_t>(m_memory.read32(descriptor + 12u), 0x100u);
            thread.priority = std::clamp<uint32_t>(m_memory.read32(descriptor + 16u), 1u, 126u);
            thread.initialPriority = thread.priority;
            thread.stackBase = m_memory.allocate(thread.stackSize + kStackGuardBytes, 16u);
            if (thread.stackBase == 0u)
            {
                setV0(-400);
                return true;
            }
            const int id = thread.id;
            m_threads.emplace(id, std::move(thread));
            setV0(id);
            return true;
        }
        case 5: // DeleteThread
        {
            const int id = static_cast<int>(cpu.gpr[4]);
            const auto it = m_threads.find(id);
            if (it == m_threads.end())
            {
                setV0(-1);
                return true;
            }
            if (it->second.stackBase != 0u)
                (void)m_memory.freeAllocation(it->second.stackBase);
            m_threads.erase(it);
            setV0(0);
            return true;
        }
        case 6: // StartThread
        case 7: // StartThreadArgs
        {
            const int id = static_cast<int>(cpu.gpr[4]);
            const auto it = m_threads.find(id);
            if (it == m_threads.end())
            {
                setV0(-1);
                return true;
            }
            IopThread &thread = it->second;
            const uint32_t argumentSize = ordinal == 7 ? cpu.gpr[5] : 0u;
            std::vector<uint8_t> arguments(argumentSize <= thread.stackSize - 32u ? argumentSize : 0u);
            if (argumentSize && (arguments.size() != argumentSize ||
                !m_memory.readRam(cpu.gpr[6], arguments.data(), arguments.size())))
            {
                setV0(-403);
                return true;
            }
            thread.cpu = {};
            thread.cpu.pc = thread.entry;
            thread.cpu.gpr[4] = cpu.gpr[5];
            thread.cpu.gpr[5] = ordinal == 7 ? cpu.gpr[6] : 0u;
            thread.cpu.gpr[28] = cpu.gpr[28];
            thread.cpu.gpr[29] = alignUp(thread.stackBase + thread.stackSize, 16u) - 16u;
            if (ordinal == 7)
            {
                const uint32_t argumentAddress = (thread.cpu.gpr[29] - argumentSize) & ~15u;
                if (argumentSize) (void)m_memory.writeRam(argumentAddress, arguments.data(), arguments.size());
                thread.cpu.gpr[5] = argumentAddress;
                thread.cpu.gpr[29] = argumentAddress - 16u;
            }
            thread.cpu.gpr[31] = kThreadReturnSentinel;
            thread.state = IopThreadState::Ready;
            if (cave::ps2::audio_trace::enabled)
                std::osyncstream(std::cerr) << "[ps2-iop-start-thread] id=" << id << " ordinal=" << ordinal
                    << " entry=0x" << std::hex << thread.entry << " args=0x" << thread.cpu.gpr[4]
                    << ",0x" << thread.cpu.gpr[5] << std::dec << '\n';
            setV0(0);
            return true;
        }
        case 8: // ExitThread
        case 9: // ExitDeleteThread
            if (m_currentThread != nullptr)
            {
                m_currentThread->state = ordinal == 9 ? IopThreadState::Dead : IopThreadState::Dormant;
                cpu.stopped = true;
                cpu.yielded = true;
            }
            setV0(0);
            return true;
        case 10:
        case 11: // TerminateThread
        {
            const int id = static_cast<int>(cpu.gpr[4]);
            const auto it = m_threads.find(id);
            if (it == m_threads.end())
            {
                setV0(-1);
                return true;
            }
            it->second.state = IopThreadState::Dormant;
            setV0(0);
            return true;
        }
        case 12:
        case 13:
            setV0(0);
            return true;
        case 14:
        case 15:
        {
            int id = static_cast<int>(cpu.gpr[4]);
            if (id == 0 && m_currentThread != nullptr)
                id = m_currentThread->id;
            const auto it = m_threads.find(id);
            if (it == m_threads.end())
            {
                setV0(-1);
                return true;
            }
            it->second.priority = std::clamp<uint32_t>(cpu.gpr[5], 1u, 126u);
            setV0(0);
            return true;
        }
        case 16:
        case 17:
            setV0(0);
            cpu.yielded = true;
            return true;
        case 18:
        case 19:
        {
            const int id = static_cast<int>(cpu.gpr[4]);
            const auto it = m_threads.find(id);
            if (it == m_threads.end())
            {
                setV0(-1);
                return true;
            }
            if (it->second.state == IopThreadState::Sleep ||
                it->second.state == IopThreadState::Delay ||
                it->second.state == IopThreadState::Semaphore ||
                it->second.state == IopThreadState::EventFlag ||
                it->second.state == IopThreadState::Mailbox ||
                it->second.state == IopThreadState::FixedPool)
            {
                it->second.state = IopThreadState::Ready;
                it->second.waitId = 0;
                it->second.waitResultAddress = 0;
                it->second.cpu.gpr[2] = static_cast<uint32_t>(-418);
            }
            setV0(0);
            return true;
        }
        case 20:
            setV0(m_currentThread != nullptr ? m_currentThread->id : 0);
            return true;
        case 21:
            setV0(0x1000);
            return true;
        case 22:
        case 23:
            setV0(referThreadStatus(static_cast<int>(cpu.gpr[4]), cpu.gpr[5]) ? 0 : -1);
            return true;
        case 24: // SleepThread
            if (m_currentThread != nullptr)
            {
                if (m_currentThread->wakeupCount > 0)
                    --m_currentThread->wakeupCount;
                else
                    sleepCurrent(cpu);
            }
            setV0(0);
            return true;
        case 25:
        case 26:
        {
            const int id = static_cast<int>(cpu.gpr[4]);
            const auto it = m_threads.find(id);
            if (it == m_threads.end())
            {
                setV0(-1);
                return true;
            }
            if (it->second.state == IopThreadState::Sleep)
                it->second.state = IopThreadState::Ready;
            else
                ++it->second.wakeupCount;
            setV0(0);
            return true;
        }
        case 27:
        case 28:
        {
            const int id = static_cast<int>(cpu.gpr[4]);
            const auto it = m_threads.find(id);
            if (it == m_threads.end())
            {
                setV0(-1);
                return true;
            }
            const int old = it->second.wakeupCount;
            it->second.wakeupCount = 0;
            setV0(old);
            return true;
        }
        case 29:
        case 30: // SuspendThread / iSuspendThread
        {
            const int id = static_cast<int>(cpu.gpr[4]);
            const auto it = m_threads.find(id);
            if (it == m_threads.end())
            {
                setV0(-1);
                return true;
            }
            it->second.state = IopThreadState::Suspended;
            if (m_currentThread == &it->second)
                cpu.yielded = true;
            setV0(0);
            return true;
        }
        case 31:
        case 32: // ResumeThread / iResumeThread
        {
            const int id = static_cast<int>(cpu.gpr[4]);
            const auto it = m_threads.find(id);
            if (it == m_threads.end())
            {
                setV0(-1);
                return true;
            }
            if (it->second.state == IopThreadState::Suspended)
                it->second.state = IopThreadState::Ready;
            setV0(0);
            return true;
        }
        case 33: // DelayThread
            if (m_currentThread != nullptr)
            {
                const uint64_t delayCycles = (static_cast<uint64_t>(cpu.gpr[4]) * kIopClockHz + 999'999ull) / 1'000'000ull;
                delayCurrentUntil(currentCycle + std::max<uint64_t>(delayCycles, 1u), cpu);
            }
            setV0(0);
            return true;
        case 34: // GetSystemTime
            m_memory.write32(cpu.gpr[4], static_cast<uint32_t>(currentCycle));
            m_memory.write32(cpu.gpr[4] + 4u, static_cast<uint32_t>(currentCycle >> 32u));
            setV0(0);
            return true;
        case 35:
        case 36:
        case 37:
        case 38:
            setV0(0);
            return true;
        case 39: // USec2SysClock
        {
            const uint64_t cycles = (static_cast<uint64_t>(cpu.gpr[4]) * kIopClockHz) / 1'000'000ull;
            m_memory.write32(cpu.gpr[5], static_cast<uint32_t>(cycles));
            m_memory.write32(cpu.gpr[5] + 4u, static_cast<uint32_t>(cycles >> 32u));
            setV0(0);
            return true;
        }
        case 40:
        {
            const uint64_t cycles = static_cast<uint64_t>(m_memory.read32(cpu.gpr[4])) | (static_cast<uint64_t>(m_memory.read32(cpu.gpr[4] + 4u)) << 32u);
            const uint64_t usec = (cycles * 1'000'000ull) / kIopClockHz;
            if (cpu.gpr[5] != 0u)
                m_memory.write32(cpu.gpr[5], static_cast<uint32_t>(usec / 1'000'000ull));
            if (cpu.gpr[6] != 0u)
                m_memory.write32(cpu.gpr[6], static_cast<uint32_t>(usec % 1'000'000ull));
            setV0(0);
            return true;
        }
        case 41:
            setV0(0);
            return true;
        default:
            return false;
        }
    }

    bool IopKernel::referThreadStatus(int id, uint32_t outputAddress)
    {
        if (id == 0 && m_currentThread != nullptr)
            id = m_currentThread->id;
        const auto it = m_threads.find(id);
        if (it == m_threads.end() || outputAddress == 0u)
            return false;

        const IopThread &thread = it->second;
        uint32_t status = 0x10u;
        switch (thread.state)
        {
        case IopThreadState::Running:
            status = 0x01u;
            break;
        case IopThreadState::Ready:
            status = 0x02u;
            break;
        case IopThreadState::Sleep:
        case IopThreadState::Delay:
        case IopThreadState::Semaphore:
        case IopThreadState::EventFlag:
        case IopThreadState::Mailbox:
        case IopThreadState::FixedPool:
            status = 0x04u;
            break;
        case IopThreadState::Suspended:
            status = 0x08u;
            break;
        default:
            status = 0x10u;
            break;
        }

        m_memory.write32(outputAddress + 0u, thread.attr);
        m_memory.write32(outputAddress + 4u, thread.option);
        m_memory.write32(outputAddress + 8u, status);
        m_memory.write32(outputAddress + 12u, thread.entry);
        m_memory.write32(outputAddress + 16u, thread.stackBase);
        m_memory.write32(outputAddress + 20u, thread.stackSize);
        m_memory.write32(outputAddress + 24u, thread.cpu.gpr[28]);
        m_memory.write32(outputAddress + 28u, thread.initialPriority);
        m_memory.write32(outputAddress + 32u, thread.priority);
        m_memory.write32(outputAddress + 36u, thread.state == IopThreadState::Sleep ? 1u : thread.state == IopThreadState::Delay   ? 2u
                                                                                       : thread.state == IopThreadState::Semaphore ? 3u
                                                                                       : thread.state == IopThreadState::EventFlag ? 4u
                                                                                       : thread.state == IopThreadState::Mailbox ? 5u
                                                                                       : thread.state == IopThreadState::FixedPool ? 7u
                                                                                                                                   : 0u);
        m_memory.write32(outputAddress + 40u, static_cast<uint32_t>(thread.waitId));
        m_memory.write32(outputAddress + 44u, static_cast<uint32_t>(thread.wakeupCount));
        return true;
    }

    bool IopKernel::dispatchSemaphoreImport(uint16_t ordinal, IopCpuState &cpu)
    {
        m_readyCacheValid = false;
        const auto setV0 = [&](int32_t value)
        {
            cpu.gpr[2] = static_cast<uint32_t>(value);
        };

        switch (ordinal)
        {
        case 4:
        {
            const uint32_t descriptor = cpu.gpr[4];
            Semaphore semaphore;
            semaphore.id = static_cast<int>(m_nextSemaphoreId++);
            semaphore.attr = m_memory.read32(descriptor + 0u);
            semaphore.option = m_memory.read32(descriptor + 4u);
            semaphore.current = static_cast<int>(m_memory.read32(descriptor + 8u));
            semaphore.maximum = std::max(1, static_cast<int>(m_memory.read32(descriptor + 12u)));
            m_semaphores.emplace(semaphore.id, semaphore);
            setV0(semaphore.id);
            return true;
        }
        case 5:
            setV0(m_semaphores.erase(static_cast<int>(cpu.gpr[4])) != 0u ? 0 : -1);
            return true;
        case 6:
        case 7:
        {
            const int id = static_cast<int>(cpu.gpr[4]);
            const auto it = m_semaphores.find(id);
            if (it == m_semaphores.end())
            {
                setV0(-1);
                return true;
            }
            if (it->second.current < it->second.maximum)
                ++it->second.current;
            wakeOneSemaphore(id);
            setV0(0);
            return true;
        }
        case 8:
        case 9:
        {
            const int id = static_cast<int>(cpu.gpr[4]);
            const auto it = m_semaphores.find(id);
            if (it == m_semaphores.end())
            {
                setV0(-1);
                return true;
            }
            if (it->second.current > 0)
            {
                --it->second.current;
                setV0(0);
            }
            else if (ordinal == 9)
                setV0(-419);
            else if (m_currentThread != nullptr)
            {
                m_currentThread->state = IopThreadState::Semaphore;
                m_currentThread->waitId = id;
                cpu.yielded = true;
                setV0(0);
            }
            return true;
        }
        case 11:
        case 12:
        {
            const int id = static_cast<int>(cpu.gpr[4]);
            const auto it = m_semaphores.find(id);
            if (it == m_semaphores.end())
            {
                setV0(-1);
                return true;
            }
            const uint32_t outputAddress = cpu.gpr[5];
            if (outputAddress != 0u)
            {
                m_memory.write32(outputAddress + 0u, it->second.attr);
                m_memory.write32(outputAddress + 4u, it->second.option);
                m_memory.write32(outputAddress + 8u, 0u);
                m_memory.write32(outputAddress + 12u, static_cast<uint32_t>(it->second.maximum));
                m_memory.write32(outputAddress + 16u, static_cast<uint32_t>(it->second.current));
                uint32_t waiters = 0u;
                for (const auto &[threadId, thread] : m_threads)
                {
                    if (thread.state == IopThreadState::Semaphore && thread.waitId == id)
                        ++waiters;
                }
                m_memory.write32(outputAddress + 20u, waiters);
            }
            setV0(0);
            return true;
        }
        default:
            return false;
        }
    }

    void IopKernel::wakeOneSemaphore(int id)
    {
        IopThread *best = nullptr;
        for (auto &[threadId, thread] : m_threads)
        {
            if (thread.state != IopThreadState::Semaphore || thread.waitId != id)
                continue;
            if (best == nullptr || thread.priority < best->priority)
                best = &thread;
        }

        const auto semaphore = m_semaphores.find(id);
        if (best != nullptr && semaphore != m_semaphores.end() && semaphore->second.current > 0)
        {
            --semaphore->second.current;
            best->state = IopThreadState::Ready;
            best->waitId = 0;
        }
    }

    bool IopKernel::eventSatisfied(const EventFlag &event, uint32_t bits, uint32_t mode)
    {
        if (bits == 0u)
            return false;
        return (mode & 1u) != 0u ? (event.bits & bits) != 0u : (event.bits & bits) == bits;
    }

    void IopKernel::wakeEventWaiters(EventFlag &event)
    {
        for (auto &[threadId, thread] : m_threads)
        {
            if (thread.state != IopThreadState::EventFlag || thread.waitId != event.id)
                continue;
            if (!eventSatisfied(event, thread.waitBits, thread.waitMode))
                continue;

            if (thread.waitResultAddress != 0u)
                m_memory.write32(thread.waitResultAddress, event.bits);
            thread.cpu.gpr[2] = 0u;
            if ((thread.waitMode & 0x10u) != 0u)
                event.bits = 0u;
            thread.state = IopThreadState::Ready;
            thread.waitId = 0;
            thread.waitBits = 0;
            thread.waitMode = 0;
            thread.waitResultAddress = 0;
            if (event.bits == 0u)
                break;
        }
    }

    int IopKernel::createInternalEventFlag(uint32_t attr, uint32_t option, uint32_t bits)
    {
        m_readyCacheValid = false;
        EventFlag event;
        event.id = static_cast<int>(m_nextEventFlagId++);
        event.attr = attr;
        event.option = option;
        event.bits = bits;
        const int id = event.id;
        m_eventFlags.emplace(id, event);
        return id;
    }

    bool IopKernel::setInternalEventFlag(int id, uint32_t bits)
    {
        m_readyCacheValid = false;
        const auto event = m_eventFlags.find(id);
        if (event == m_eventFlags.end())
            return false;
        event->second.bits |= bits;
        wakeEventWaiters(event->second);
        return true;
    }

    bool IopKernel::dispatchEventImport(uint16_t ordinal, IopCpuState &cpu)
    {
        m_readyCacheValid = false;
        const auto setV0 = [&](int32_t value)
        {
            cpu.gpr[2] = static_cast<uint32_t>(value);
        };

        switch (ordinal)
        {
        case 4:
        {
            const uint32_t descriptor = cpu.gpr[4];
            setV0(createInternalEventFlag(m_memory.read32(descriptor + 0u),
                                          m_memory.read32(descriptor + 4u),
                                          m_memory.read32(descriptor + 8u)));
            return true;
        }
        case 5:
        {
            const int id = static_cast<int>(cpu.gpr[4]);
            if (m_eventFlags.erase(id) == 0u)
            {
                setV0(-1);
                return true;
            }
            for (auto &[threadId, thread] : m_threads)
            {
                if (thread.state == IopThreadState::EventFlag && thread.waitId == id)
                {
                    thread.state = IopThreadState::Ready;
                    thread.waitId = 0;
                    thread.cpu.gpr[2] = static_cast<uint32_t>(-1);
                }
            }
            setV0(0);
            return true;
        }
        case 6:
        case 7:
        {
            if (!setInternalEventFlag(static_cast<int>(cpu.gpr[4]), cpu.gpr[5]))
            {
                setV0(-1);
                return true;
            }
            setV0(0);
            return true;
        }
        case 8:
        case 9:
        {
            const auto event = m_eventFlags.find(static_cast<int>(cpu.gpr[4]));
            if (event == m_eventFlags.end())
            {
                setV0(-1);
                return true;
            }
            // IOP ClearEventFlag applies a mask: callers pass ~bitsToClear.
            event->second.bits &= cpu.gpr[5];
            setV0(0);
            return true;
        }
        case 10: // WaitEventFlag
        case 11: // PollEventFlag
        {
            const auto event = m_eventFlags.find(static_cast<int>(cpu.gpr[4]));
            if (event == m_eventFlags.end())
            {
                setV0(-1);
                return true;
            }
            const uint32_t bits = cpu.gpr[5];
            const uint32_t mode = cpu.gpr[6];
            if (eventSatisfied(event->second, bits, mode))
            {
                if (cpu.gpr[7] != 0u)
                    m_memory.write32(cpu.gpr[7], event->second.bits);
                if ((mode & 0x10u) != 0u)
                    event->second.bits = 0u;
                setV0(0);
            }
            else if (ordinal == 11)
                setV0(-418);
            else if (m_currentThread != nullptr)
            {
                m_currentThread->state = IopThreadState::EventFlag;
                m_currentThread->waitId = event->second.id;
                m_currentThread->waitBits = bits;
                m_currentThread->waitMode = mode;
                m_currentThread->waitResultAddress = cpu.gpr[7];
                setV0(0);
                cpu.yielded = true;
            }
            else
                setV0(-418);
            return true;
        }
        case 13:
        case 14:
        {
            const auto event = m_eventFlags.find(static_cast<int>(cpu.gpr[4]));
            if (event == m_eventFlags.end())
            {
                setV0(-1);
                return true;
            }
            if (cpu.gpr[5] != 0u)
            {
                uint32_t waiters = 0u;
                for (const auto &[threadId, thread] : m_threads)
                {
                    if (thread.state == IopThreadState::EventFlag && thread.waitId == event->second.id)
                        ++waiters;
                }
                m_memory.write32(cpu.gpr[5] + 0u, event->second.attr);
                m_memory.write32(cpu.gpr[5] + 4u, event->second.option);
                m_memory.write32(cpu.gpr[5] + 8u, event->second.bits);
                m_memory.write32(cpu.gpr[5] + 12u, event->second.bits);
                m_memory.write32(cpu.gpr[5] + 16u, waiters);
            }
            setV0(0);
            return true;
        }
        default:
            return false;
        }
    }

    bool IopKernel::dispatchMailboxImport(uint16_t ordinal, IopCpuState &cpu)
    {
        // PS2SDK thmsgbx ABI: receive/poll take (void **message, int mailbox).
        if (ordinal < 4u || ordinal == 10u || ordinal > 12u)
            return false;
        m_readyCacheValid = false;
        const auto result = [&](int value) { cpu.gpr[2] = static_cast<uint32_t>(value); };
        if (ordinal == 4u)
        {
            const uint32_t attr = m_memory.read32(cpu.gpr[4]);
            if (attr & ~5u)
            {
                result(-401);
                return true;
            }
            const int id = static_cast<int>(m_nextMailboxId++);
            m_mailboxes.emplace(id, Mailbox{attr, m_memory.read32(cpu.gpr[4] + 4u), {}});
            result(id);
            return true;
        }
        const bool receive = ordinal == 8u || ordinal == 9u;
        const int id = static_cast<int>(cpu.gpr[receive ? 5 : 4]);
        auto found = m_mailboxes.find(id);
        if (found == m_mailboxes.end())
        {
            result(-410);
            return true;
        }
        Mailbox &mailbox = found->second;
        if (ordinal == 5u)
        {
            for (auto &[tid, thread] : m_threads)
                if (thread.state == IopThreadState::Mailbox && thread.waitId == id)
                {
                    thread.state = IopThreadState::Ready;
                    thread.waitId = 0;
                    thread.waitResultAddress = 0;
                    thread.cpu.gpr[2] = static_cast<uint32_t>(-425);
                }
            m_mailboxes.erase(found);
        }
        else if (ordinal == 6u || ordinal == 7u)
        {
            const uint32_t message = cpu.gpr[5];
            IopThread *waiter = nullptr;
            for (auto &[tid, thread] : m_threads)
                if (thread.state == IopThreadState::Mailbox && thread.waitId == id)
                {
                    const bool samePriority = waiter && thread.priority == waiter->priority;
                    if (!waiter || ((mailbox.attr & 1u) && thread.priority < waiter->priority) ||
                        ((!(mailbox.attr & 1u) || samePriority) && thread.waitOrder < waiter->waitOrder))
                        waiter = &thread;
                }
            if (waiter)
            {
                m_memory.write32(waiter->waitResultAddress, message);
                waiter->waitResultAddress = 0;
                waiter->waitId = 0;
                waiter->state = IopThreadState::Ready;
                waiter->cpu.gpr[2] = 0;
            }
            else
            {
                auto position = mailbox.messages.end();
                if (mailbox.attr & 4u)
                    position = std::find_if(mailbox.messages.begin(), mailbox.messages.end(),
                        [&](uint32_t queued) { return m_memory.read8(message + 4u) < m_memory.read8(queued + 4u); });
                mailbox.messages.insert(position, message);
                // Kernel owns the packet link while it is in the mailbox.
                for (size_t i = 0; i < mailbox.messages.size(); ++i)
                    m_memory.write32(mailbox.messages[i], mailbox.messages[(i + 1u) % mailbox.messages.size()]);
            }
        }
        else if (receive)
        {
            if (!mailbox.messages.empty())
            {
                m_memory.write32(cpu.gpr[4], mailbox.messages.front());
                mailbox.messages.erase(mailbox.messages.begin());
                if (!mailbox.messages.empty())
                    m_memory.write32(mailbox.messages.back(), mailbox.messages.front());
            }
            else if (ordinal == 9u)
            {
                result(-424);
                return true;
            }
            else if (!m_currentThread)
            {
                result(-100);
                return true;
            }
            else
            {
                m_currentThread->state = IopThreadState::Mailbox;
                m_currentThread->waitId = id;
                m_currentThread->waitResultAddress = cpu.gpr[4];
                m_currentThread->waitOrder = m_nextWaitOrder++;
                cpu.yielded = true;
            }
        }
        else // ReferMbxStatus / iReferMbxStatus
        {
            uint32_t waiting = 0;
            for (const auto &[tid, thread] : m_threads)
                waiting += thread.state == IopThreadState::Mailbox && thread.waitId == id;
            const uint32_t output = cpu.gpr[5];
            m_memory.write32(output, mailbox.attr);
            m_memory.write32(output + 4u, mailbox.option);
            m_memory.write32(output + 8u, waiting);
            m_memory.write32(output + 12u, static_cast<uint32_t>(mailbox.messages.size()));
            m_memory.write32(output + 16u, mailbox.messages.empty() ? 0u : mailbox.messages.front());
            m_memory.write32(output + 20u, 0u);
            m_memory.write32(output + 24u, 0u);
        }
        result(0);
        return true;
    }

    bool IopKernel::dispatchFixedPoolImport(uint16_t ordinal, IopCpuState &cpu)
    {
        if (ordinal < 4u || ordinal == 10u || ordinal > 12u)
            return false;
        m_readyCacheValid = false;
        const auto result = [&](int32_t value) { cpu.gpr[2] = static_cast<uint32_t>(value); };
        if (ordinal == 4u)
        {
            FixedPool pool;
            const uint32_t descriptor = cpu.gpr[4];
            pool.attr = m_memory.read32(descriptor);
            pool.option = m_memory.read32(descriptor + 4u);
            pool.blockSize = m_memory.read32(descriptor + 8u);
            const uint32_t blocks = m_memory.read32(descriptor + 12u);
            if (pool.attr & ~0x201u) { result(-401); return true; }
            if (!pool.blockSize || pool.blockSize > 0x1f0000u || !blocks ||
                uint64_t(alignUp(pool.blockSize, 4u)) * blocks > 0x1f0000u)
            { result(-403); return true; }
            pool.stride = alignUp(pool.blockSize, 4u);
            pool.base = m_memory.allocate(pool.stride * blocks, 4u);
            if (!pool.base) { result(-400); return true; }
            pool.allocated.resize(blocks);
            const int id = static_cast<int>(m_nextFixedPoolId++);
            m_fixedPools.emplace(id, std::move(pool));
            result(id);
            return true;
        }
        const int id = static_cast<int>(cpu.gpr[4]);
        auto found = m_fixedPools.find(id);
        if (found == m_fixedPools.end()) { result(-411); return true; }
        FixedPool &pool = found->second;
        if (ordinal == 5u)
        {
            for (auto &[tid, thread] : m_threads)
                if (thread.state == IopThreadState::FixedPool && thread.waitId == id)
                {
                    thread.state = IopThreadState::Ready;
                    thread.waitId = 0;
                    thread.cpu.gpr[2] = static_cast<uint32_t>(-425);
                }
            (void)m_memory.freeAllocation(pool.base);
            m_fixedPools.erase(found);
        }
        else if (ordinal >= 6u && ordinal <= 8u)
        {
            const auto free = std::find(pool.allocated.begin(), pool.allocated.end(), false);
            if (free != pool.allocated.end())
            {
                const uint32_t address = pool.base + uint32_t(free - pool.allocated.begin()) * pool.stride;
                *free = true;
                result(static_cast<int32_t>(address));
                return true;
            }
            if (ordinal != 6u) { result(-426); return true; }
            if (!m_currentThread) { result(-100); return true; }
            m_currentThread->state = IopThreadState::FixedPool;
            m_currentThread->waitId = id;
            m_currentThread->waitOrder = m_nextWaitOrder++;
            cpu.yielded = true;
        }
        else if (ordinal == 9u)
        {
            const uint32_t address = cpu.gpr[5];
            const uint32_t offset = address - pool.base;
            if (address < pool.base || offset % pool.stride || offset / pool.stride >= pool.allocated.size() ||
                !pool.allocated[offset / pool.stride])
            { result(-438); return true; }
            IopThread *waiter = nullptr;
            for (auto &[tid, thread] : m_threads)
                if (thread.state == IopThreadState::FixedPool && thread.waitId == id &&
                    (!waiter || ((pool.attr & 1u) && thread.priority < waiter->priority) ||
                     ((!(pool.attr & 1u) || thread.priority == waiter->priority) && thread.waitOrder < waiter->waitOrder)))
                    waiter = &thread;
            if (waiter)
            {
                waiter->state = IopThreadState::Ready;
                waiter->waitId = 0;
                waiter->cpu.gpr[2] = address;
            }
            else pool.allocated[offset / pool.stride] = false;
        }
        else
        {
            const uint32_t output = cpu.gpr[5];
            uint32_t waiters = 0;
            for (const auto &[tid, thread] : m_threads)
                waiters += thread.state == IopThreadState::FixedPool && thread.waitId == id;
            m_memory.write32(output, pool.attr);
            m_memory.write32(output + 4u, pool.option);
            m_memory.write32(output + 8u, pool.blockSize);
            m_memory.write32(output + 12u, static_cast<uint32_t>(pool.allocated.size()));
            m_memory.write32(output + 16u, static_cast<uint32_t>(std::count(pool.allocated.begin(), pool.allocated.end(), false)));
            m_memory.write32(output + 20u, waiters);
            for (unsigned offset = 24u; offset < 40u; offset += 4u) m_memory.write32(output + offset, 0);
        }
        result(0);
        return true;
    }

    std::string IopKernel::debugState() const
    {
        std::ostringstream out;
        for (const auto &[id, thread] : m_threads)
            out << "[ps2-iop-thread] id=" << id << " state=" << int(thread.state)
                << " priority=" << thread.priority << " wait=" << thread.waitId
                << " entry=0x" << std::hex << thread.entry << std::dec
                << " pc=0x" << std::hex << thread.cpu.pc << " ra=0x" << thread.cpu.gpr[31]
                << " s0=0x" << thread.cpu.gpr[16] << " s1=0x" << thread.cpu.gpr[17]
                << " v0=0x" << thread.cpu.gpr[2] << std::dec << '\n';
        return out.str();
    }

    void IopKernel::sleepCurrent(IopCpuState &cpu)
    {
        m_readyCacheValid = false;
        if (m_currentThread == nullptr)
            return;
        m_currentThread->state = IopThreadState::Sleep;
        cpu.yielded = true;
    }

    void IopKernel::delayCurrentUntil(uint64_t wakeCycle, IopCpuState &cpu)
    {
        m_readyCacheValid = false;
        if (m_currentThread == nullptr)
            return;
        m_currentThread->wakeCycle = wakeCycle;
        m_currentThread->state = IopThreadState::Delay;
        cpu.yielded = true;
    }

    IopThread *IopKernel::beginNextReady(uint64_t currentCycle)
    {
        // EE checkpoints may advance only one IOP instruction. Reuse the same
        // priority election until a kernel operation or delayed wake changes
        // readiness; no guest execution or event deadline is batched away.
        if (!m_readyCacheValid || currentCycle >= m_nextDelayedWake)
        {
            m_cachedReady = nullptr;
            m_nextDelayedWake = UINT64_MAX;
            for (auto &[id, thread] : m_threads)
            {
                if (thread.state == IopThreadState::Delay)
                {
                    if (thread.wakeCycle <= currentCycle)
                        thread.state = IopThreadState::Ready;
                    else
                        m_nextDelayedWake = std::min(m_nextDelayedWake, thread.wakeCycle);
                }
                if (thread.state != IopThreadState::Ready)
                    continue;
                if (m_cachedReady == nullptr || thread.priority < m_cachedReady->priority ||
                    (thread.priority == m_cachedReady->priority && thread.id < m_cachedReady->id))
                    m_cachedReady = &thread;
            }
            m_readyCacheValid = true;
        }
        IopThread *next = m_cachedReady;
        if (next == nullptr)
            return nullptr;

        m_currentThread = next;
        next->state = IopThreadState::Running;
        next->cpu.stopped = false;
        next->cpu.yielded = false;
        return next;
    }

    uint64_t IopKernel::nextWakeCycle(uint64_t fallback) const
    {
        uint64_t nextWake = fallback;
        for (const auto &[id, thread] : m_threads)
        {
            if (thread.state == IopThreadState::Delay)
                nextWake = std::min(nextWake, thread.wakeCycle);
        }
        return nextWake;
    }

    void IopKernel::endTimeslice(IopThread &thread, uint32_t returnSentinel)
    {
        if (thread.cpu.pc == returnSentinel || thread.cpu.stopped)
        {
            thread.state = IopThreadState::Dormant;
            m_readyCacheValid = false;
        }
        else if (thread.state == IopThreadState::Running)
            thread.state = IopThreadState::Ready;
        else
            m_readyCacheValid = false;
        m_currentThread = nullptr;
        if (!m_readyCacheValid)
            cleanupDeadThreads();
    }

    void IopKernel::cleanupDeadThreads()
    {
        m_readyCacheValid = false;
        for (auto thread = m_threads.begin(); thread != m_threads.end();)
        {
            if (thread->second.state != IopThreadState::Dead)
            {
                ++thread;
                continue;
            }
            if (thread->second.stackBase != 0u)
                (void)m_memory.freeAllocation(thread->second.stackBase);
            thread = m_threads.erase(thread);
        }
    }

    void IopKernel::terminateThreadsInRange(uint32_t base, uint32_t size)
    {
        m_readyCacheValid = false;
        for (auto &[id, thread] : m_threads)
        {
            const uint32_t pc = IopMemory::physicalAddress(thread.cpu.pc);
            if (pc >= base && pc < base + size)
                thread.state = IopThreadState::Dead;
        }
    }
}
