#include "emulator/iop_emulator.h"
#include "emulator/imports/iop_cdvd.h"
#include "emulator/core/iop_cpu.h"
#include "emulator/imports/iop_heaplib.h"
#include "emulator/imports/iop_imports.h"
#include "emulator/imports/iop_intrman.h"
#include "emulator/imports/iop_ioman.h"
#include "emulator/core/iop_kernel.h"
#include "emulator/imports/iop_loadcore.h"
#include "emulator/core/iop_memory.h"
#include "emulator/services/iop_module_loader.h"
#include "emulator/services/iop_rpc.h"
#include "emulator/imports/iop_stdio.h"
#include "emulator/imports/iop_sysclib.h"
#include "emulator/imports/iop_sysmem.h"
#include "emulator/imports/iop_timrman.h"
#include "emulator/imports/iop_vblank.h"
#include "emulator/iop_emulator_const.h"
#include "../../../backends/ps2/iop_profile.h"
#include "../../../backends/ps2/audio_trace.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <span>
#include <sstream>
#include <utility>
#include <limits>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kRamSize = IopMemory::RamSize;
        constexpr uint32_t kKernelHeapBase = IopMemory::HeapBase;
        constexpr uint32_t kKernelHeapLimit = IopMemory::HeapLimit;
        constexpr uint32_t kCallStackBase = kKernelHeapLimit;
        constexpr uint32_t kCallStackLimit = 0x001FFF00u;
        constexpr uint32_t kCallStackSize = 0x2000u;
        constexpr uint32_t kCallStackCapacity = (kCallStackLimit - kCallStackBase) / kCallStackSize;
        constexpr uint64_t kCdvdCompletionCycles = 128u;

        uint32_t physicalAddress(uint32_t address)
        {
            return IopMemory::physicalAddress(address);
        }

        int32_t sign16(uint32_t value)
        {
            return static_cast<int16_t>(value & 0xFFFFu);
        }

        bool iequals(std::string_view lhs, std::string_view rhs)
        {
            if (lhs.size() != rhs.size())
                return false;
            for (size_t i = 0; i < lhs.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(lhs[i])) !=
                    std::tolower(static_cast<unsigned char>(rhs[i])))
                    return false;
            }
            return true;
        }

    }

    class IopEmulator::Impl final : public IopGuestExecutor
    {
    public:
        using CpuState = IopCpuState;

        struct Module
        {
            int id = 0;
            std::string path;
            std::string name;
            uint32_t base = 0;
            uint32_t size = 0;
            uint32_t entry = 0;
            uint32_t gp = 0;
            bool resident = false;
        };

        struct GuestCallback
        {
            uint32_t function = 0;
            uint32_t gp = 0;
        };

        struct ScheduledGuestCallback
        {
            uint32_t function = 0u;
            uint32_t gp = 0u;
            uint32_t argument = 0u;
            bool interrupt = false;
        };

        explicit Impl(IopHost &hostRef)
            : host(hostRef),
              sysmem(host, memory),
              kernel(memory),
              cdvd(host, memory, kernel),
              vblank(kernel),
              rpc(host, memory, kernel),
              sysclib(memory),
              stdio(host, memory),
              heaplib(memory),
              intrman(memory),
              timrman(),
              ioman(memory, host),
              cpuCore(memory),
              imports(memory),
              loadcore(memory, imports)
        {
            reset();
        }

        void reset()
        {
            memory.reset();
            kernel.reset();
            modules.clear();
            imports.reset();
            rpc.reset();
            cdvd.reset();
            intrman.reset();
            timrman.reset();
            nextTimerCycle = UINT64_MAX;
            ioman.reset();
            pendingDmaInterrupts.clear();
            pendingGuestCallbacks.clear();
            nextModuleId = 1;
            moduleCursor = kModuleLoadBase;
            totalCycles = 0;
            requestedCycles = 0;
            idleUntil = 0;
            totalInstructions = 0;
            profile.Reset();
            eeCycleCarry = 0;
            activeCpu = nullptr;
            lastError.clear();
            missingImportCounts.clear();
            servicingDmaInterrupts = false;
            servicingGuestCallbacks = false;
            servicingAudioInterrupt = audioIrqDelivered = false;
            callDepth = 0u;
            secrMcCommandHandler = {};
            secrMcDevIdHandler = {};
            checkKelfPathCallback = {};
        }

        uint8_t read8(uint32_t address) const
        {
            return memory.read8(address);
        }

        uint16_t read16(uint32_t address) const
        {
            return memory.read16(address);
        }

        uint32_t read32(uint32_t address) const
        {
            return memory.read32(address);
        }

        void write8(uint32_t address, uint8_t value)
        {
            memory.write8(address, value);
            schedulePendingDma();
        }

        void write16(uint32_t address, uint16_t value)
        {
            memory.write16(address, value);
            schedulePendingDma();
        }

        void write32(uint32_t address, uint32_t value)
        {
            memory.write32(address, value);
            schedulePendingDma();
        }

        void schedulePendingDma()
        {
            while (const auto dma = memory.takeDmaStart())
                pendingDmaInterrupts[dma->irq] = totalCycles + dma->delayCycles;
        }

        bool readRam(uint32_t address, void *destination, size_t size) const
        {
            return memory.readRam(address, destination, size);
        }

        bool writeRam(uint32_t address, const void *source, size_t size)
        {
            return memory.writeRam(address, source, size);
        }

        bool zeroRam(uint32_t address, size_t size)
        {
            return memory.zeroRam(address, size);
        }

        bool isHardwareAddress(uint32_t phys) const
        {
            return memory.isHardwareAddress(phys);
        }

        uint32_t allocate(uint32_t size, uint32_t alignment = 16u, std::optional<uint32_t> fixed = std::nullopt)
        {
            return memory.allocate(size, alignment, fixed);
        }

        bool freeAllocation(uint32_t address)
        {
            return memory.freeAllocation(address);
        }

        void log(LogLevel level, std::string_view text)
        {
            host.log(level, text);
        }

        bool checkInterrupt(CpuState &cpu)
        {
            const uint32_t status = cpu.cop0[12];
            if ((status & 1u) == 0u)
                return false;
            if ((status & 0x2u) != 0u)
                return false;
            const bool pending = memory.interruptControl() != 0u && (memory.interruptStatus() & memory.interruptMask()) != 0u;
            if (!pending)
                return false;
            cpu.cop0[13] |= 0x400u;
            cpuCore.raiseException(cpu, 0u, cpu.pc, false);
            return true;
        }

        enum class ImportDisposition
        {
            Handled,
            JumpToGuest,
            Missing,
        };

        ImportDisposition dispatchImport(const IopImportCall &call, CpuState &cpu)
        {
            const uint32_t a0 = cpu.gpr[4];
            auto setV0 = [&](uint32_t value)
            {
                cpu.gpr[2] = value;
            };

            if (iequals(call.library, "sysmem") && sysmem.dispatchImport(call.ordinal, cpu))
                return ImportDisposition::Handled;

            if (iequals(call.library, "cdvdman") && cdvd.dispatchImport(call.ordinal, cpu))
            {
                if (const auto callback = cdvd.takeCompletionCallback())
                {
                    pendingGuestCallbacks.emplace(
                        totalCycles + kCdvdCompletionCycles,
                        ScheduledGuestCallback{
                            callback->address,
                            callback->gp,
                            callback->reason,
                        });
                }
                return ImportDisposition::Handled;
            }

            if (iequals(call.library, "loadcore") && loadcore.dispatchImport(call.ordinal, cpu))
                return ImportDisposition::Handled;

            if (iequals(call.library, "thbase") || iequals(call.library, "threadman"))
            {
                return kernel.dispatchThreadImport(call.ordinal, cpu, totalCycles)
                           ? ImportDisposition::Handled
                           : ImportDisposition::Missing;
            }
            if (iequals(call.library, "thsemap"))
            {
                return kernel.dispatchSemaphoreImport(call.ordinal, cpu)
                           ? ImportDisposition::Handled
                           : ImportDisposition::Missing;
            }
            if (iequals(call.library, "thevent"))
            {
                return kernel.dispatchEventImport(call.ordinal, cpu)
                           ? ImportDisposition::Handled
                           : ImportDisposition::Missing;
            }
            if (iequals(call.library, "thmsgbx") && kernel.dispatchMailboxImport(call.ordinal, cpu))
                return ImportDisposition::Handled;
            if (iequals(call.library, "thfpool") && kernel.dispatchFixedPoolImport(call.ordinal, cpu))
                return ImportDisposition::Handled;
            if (iequals(call.library, "sifcmd"))
            {
                return rpc.dispatchSifCmdImport(call.ordinal, cpu, *this)
                           ? ImportDisposition::Handled
                           : ImportDisposition::Missing;
            }
            if (iequals(call.library, "intrman") && intrman.dispatchImport(call.ordinal, cpu, *this))
                return ImportDisposition::Handled;
            if (iequals(call.library, "secrman"))
            {
                switch (call.ordinal)
                {
                case 4: // SecrSetMcCommandHandler
                    secrMcCommandHandler = {a0, cpu.gpr[28]};
                    setV0(0);
                    return ImportDisposition::Handled;
                case 5: // SecrSetMcDevIDHandler
                    secrMcDevIdHandler = {a0, cpu.gpr[28]};
                    setV0(0);
                    return ImportDisposition::Handled;
                default:
                    break;
                }
            }
            if (iequals(call.library, "modload") && call.ordinal == 13u)
            {
                checkKelfPathCallback = {a0, cpu.gpr[28]};
                setV0(0);
                return ImportDisposition::Handled;
            }
            if (iequals(call.library, "ioman") && ioman.dispatchImport(call.ordinal, cpu, *this))
                return ImportDisposition::Handled;
            if (iequals(call.library, "sifman"))
            {
                // ps2sdk sifman ordinal 32: complete the IOP -> EE copy first,
                // then deliver its interrupt callback after CpuResumeIntr.
                if (call.ordinal == 32u) {
                    const auto callback = cpu.gpr[6], argument = cpu.gpr[7], gp = cpu.gpr[28];
                    (void)rpc.dispatchSifManImport(7u, cpu);
                    if (cpu.gpr[2] && callback)
                        pendingGuestCallbacks.emplace(totalCycles + 32u,
                            ScheduledGuestCallback{callback, gp, argument, true});
                    return ImportDisposition::Handled;
                }
                return rpc.dispatchSifManImport(call.ordinal, cpu)
                           ? ImportDisposition::Handled
                           : ImportDisposition::Missing;
            }
            if (iequals(call.library, "vblank") && vblank.dispatchImport(call.ordinal, cpu, totalCycles))
                return ImportDisposition::Handled;
            if (iequals(call.library, "timrman") && timrman.dispatchImport(call.ordinal, cpu, totalCycles))
            {
                nextTimerCycle = timrman.nextEventCycle(UINT64_MAX);
                return ImportDisposition::Handled;
            }
            if (iequals(call.library, "dmacman"))
            {
                setV0(0);
                return ImportDisposition::Handled;
            }
            if (iequals(call.library, "stdio") && stdio.dispatchImport(call.ordinal, cpu))
                return ImportDisposition::Handled;
            if (iequals(call.library, "sysclib"))
            {
                return sysclib.dispatchImport(call.ordinal, cpu)
                           ? ImportDisposition::Handled
                           : ImportDisposition::Missing;
            }
            if (iequals(call.library, "heaplib") && heaplib.dispatchImport(call.ordinal, cpu))
                return ImportDisposition::Handled;

            const uint32_t target = imports.resolve(call.library, call.ordinal, call.version);
            if (target != 0u)
            {
                cpu.pc = target;
                cpu.branchPending = false;
                return ImportDisposition::JumpToGuest;
            }

            const auto key = std::string(call.library) + ':' + std::to_string(call.ordinal) + ':' + std::to_string(call.version);
            const auto count = ++missingImportCounts[key];
            if ((count & (count - 1u)) == 0u)
            {
                std::ostringstream out;
                out << "[IOP] unhandled import " << call.library << ':' << call.ordinal
                    << " version=0x" << std::hex << call.version << " pc=0x" << cpu.pc
                    << std::dec << " occurrences=" << count;
                log(LogLevel::Warning, out.str());
            }
            setV0(0);
            return ImportDisposition::Missing;
        }

        bool step(CpuState &cpu)
        {
            if (cpu.stopped)
                return false;
            if (cpu.pc == kThreadReturnSentinel || cpu.pc == kCallReturnSentinel)
            {
                cpu.stopped = true;
                return false;
            }
            if (physicalAddress(cpu.pc) >= kRamSize)
            {
                std::ostringstream out;
                out << "[IOP] execution outside RAM pc=0x" << std::hex << cpu.pc;
                log(LogLevel::Error, out.str());
                cpu.stopped = true;
                return false;
            }
            if (checkInterrupt(cpu))
                return true;

            const uint32_t instruction = memory.read32(cpu.pc);
            const auto import = instruction == 0x03E00008u ? imports.decode(cpu.pc) : std::nullopt;
            if (import)
            {
                // The intercepted veneer is JR RA + its ordinal delay slot.
                // A load in the caller's JAL delay slot retires during JR,
                // before the imported routine observes its arguments.
                const uint32_t returnAddress = cpu.gpr[31];
                if (cpu.pendingLoad)
                {
                    if (cpu.pendingLoadReg != 0u) cpu.gpr[cpu.pendingLoadReg] = cpu.pendingLoadValue;
                    cpu.pendingLoad = false;
                }
                cpu.gpr[0] = 0;
                cpu.yielded = false;
                cpu.exception = false;
                const ImportDisposition disposition = dispatchImport(*import, cpu);
                ++totalInstructions;
                ++totalCycles;
                if (disposition == ImportDisposition::JumpToGuest)
                    return true;
                cpu.pc = returnAddress;
                cpu.branchPending = false;
                return !cpu.stopped;
            }

            const bool running = cpuCore.executeInstruction(cpu, instruction);
            // Only store instructions can start hardware DMA. Other producers
            // (audio advancement and host writes) drain their starts directly.
            if ((instruction >> 26u) >= 0x28u)
                schedulePendingDma();
            ++totalInstructions;
            ++totalCycles;
            return running;
        }

        void serviceAudio()
        {
            if (totalCycles >= memory.nextAudioCycle())
            {
                memory.advanceAudioTo(totalCycles);
                schedulePendingDma();
            }
            if (!memory.audioIrqPending())
                audioIrqDelivered = false;
            else if (!servicingAudioInterrupt && !audioIrqDelivered && memory.interruptControl())
            {
                servicingAudioInterrupt = true;
                try { audioIrqDelivered = intrman.dispatchInterrupt(9, *this); }
                catch (...) { servicingAudioInterrupt = false; throw; }
                servicingAudioInterrupt = false;
            }
        }

        uint32_t runCpu(CpuState &cpu, uint32_t instructionBudget)
        {
            CpuState *previous = activeCpu;
            activeCpu = &cpu;
            const uint64_t start = totalInstructions;
            while (!cpu.stopped && !cpu.yielded && totalInstructions - start < instructionBudget)
            {
                if (cave::ps2::audio_trace::enabled && (totalInstructions & 1023u) == 0u)
                    profile.Record(cpu.pc);
                if (totalCycles >= memory.nextAudioCycle()) serviceAudio();
                if (!step(cpu))
                    break;
                if (!servicingDmaInterrupts && !pendingDmaInterrupts.empty())
                    servicePendingDmaInterrupts();
                if (!servicingGuestCallbacks && !pendingGuestCallbacks.empty())
                    servicePendingGuestCallbacks();
            }
            activeCpu = previous;
            return static_cast<uint32_t>(totalInstructions - start);
        }

        uint32_t callFunction(uint32_t address,
                              uint32_t a0,
                              uint32_t a1,
                              uint32_t a2,
                              uint32_t a3,
                              uint32_t gp,
                              uint32_t budget = kMaxCallInstructions)
        {
            idleUntil = 0;
            struct CallDepthGuard
            {
                uint32_t &depth;
                ~CallDepthGuard() { --depth; }
            };

            const uint32_t depth = callDepth++;
            const CallDepthGuard depthGuard{callDepth};
            CpuState cpu{};
            cpu.pc = address;
            cpu.gpr[4] = a0;
            cpu.gpr[5] = a1;
            cpu.gpr[6] = a2;
            cpu.gpr[7] = a3;
            cpu.gpr[28] = gp;
            if (depth < kCallStackCapacity)
            {
                const uint32_t stackTop = kCallStackLimit - depth * kCallStackSize;
                cpu.gpr[29] = stackTop - 32u;
            }
            else if (activeCpu && activeCpu->gpr[29] > kCallStackBase + kStackGuardBytes)
            {
                // Extremely deep re-entrancy borrows unused space below the
                // suspended caller's live frame. Stack growth remains away
                // from the caller, so its saved registers stay intact.
                cpu.gpr[29] = (activeCpu->gpr[29] - kStackGuardBytes) & ~15u;
            }
            else
            {
                cpu.gpr[29] = kCallStackBase - 32u;
            }
            cpu.gpr[31] = kCallReturnSentinel;
            runCpu(cpu, budget);
            if (cave::ps2::audio_trace::enabled && cpu.pc != kCallReturnSentinel)
            {
                std::osyncstream line(std::cerr);
                line << "[ps2-iop-call-incomplete] entry=0x" << std::hex << address << " pc=0x" << cpu.pc
                    << " v0=0x" << cpu.gpr[2] << " a0=0x" << cpu.gpr[4]
                    << std::dec << " yielded=" << cpu.yielded << " stopped=" << cpu.stopped << '\n';
            }
            return cpu.gpr[2];
        }

        uint32_t executeGuestFunction(uint32_t address,
                                      uint32_t a0,
                                      uint32_t a1,
                                      uint32_t a2,
                                      uint32_t a3,
                                      uint32_t gp) override
        {
            return callFunction(address, a0, a1, a2, a3, gp);
        }

        uint32_t executeGuestFunctionWithBudget(uint32_t address,
                                                uint32_t a0,
                                                uint32_t a1,
                                                uint32_t a2,
                                                uint32_t a3,
                                                uint32_t gp,
                                                uint32_t instructionBudget) override
        {
            return callFunction(address, a0, a1, a2, a3, gp, instructionBudget);
        }

        // Not that good to use exception handling for control flow but will do for now
        void servicePendingDmaInterrupts()
        {
            // CpuSuspendIntr protects the original driver's queue updates.
            // A completion can become due while suspended, but its handler
            // must remain pending until CpuResumeIntr restores delivery.
            if (servicingDmaInterrupts || pendingDmaInterrupts.empty() || !memory.interruptControl())
                return;

            servicingDmaInterrupts = true;

            std::vector<int> completed;
            for (auto it = pendingDmaInterrupts.begin(); it != pendingDmaInterrupts.end();)
            {
                if (it->second > totalCycles)
                {
                    ++it;
                    continue;
                }
                completed.push_back(it->first);
                it = pendingDmaInterrupts.erase(it);
            }
            try
            {
                for (const int irq : completed)
                    (void)intrman.dispatchInterrupt(irq, *this);
            }
            catch (...)
            {
                servicingDmaInterrupts = false;
                throw;
            }
            servicingDmaInterrupts = false;
        }

        void servicePendingGuestCallbacks()
        {
            if (servicingGuestCallbacks || pendingGuestCallbacks.empty())
                return;

            std::vector<ScheduledGuestCallback> callbacks;
            for (auto it = pendingGuestCallbacks.begin(); it != pendingGuestCallbacks.end();)
            {
                if (it->first > totalCycles)
                    break;
                if (it->second.interrupt && !memory.interruptControl()) { ++it; continue; }
                callbacks.push_back(it->second);
                it = pendingGuestCallbacks.erase(it);
            }
            if (callbacks.empty())
                return;

            servicingGuestCallbacks = true;
            try
            {
                for (const ScheduledGuestCallback &callback : callbacks)
                {
                    if (callback.function != 0u)
                    {
                        (void)callFunction(callback.function,
                                           callback.argument,
                                           0u,
                                           0u,
                                           0u,
                                           callback.gp,
                                           100000u);
                    }
                }
            }
            catch (...)
            {
                servicingGuestCallbacks = false;
                throw;
            }
            servicingGuestCallbacks = false;
        }

        void runCycles(uint64_t cycles) noexcept
        {
            // EE slices, synchronous RPCs, module entries and interrupt
            // callbacks all share one IOP clock. A call outside runCycles can
            // already have advanced it: repay that work before running more.
            // A relative slice/debt only sees callbacks inside this method
            // and silently adds every external RPC's cost to the audio rate.
            requestedCycles += cycles;
            const uint64_t target = requestedCycles;
            if(totalCycles>=target)return;
            try
            {
                // No ready thread and no event can change that state before
                // idleUntil. EE checkpoints can be only a few IOP cycles apart;
                // do not rescan the same sleeping threads at each checkpoint.
                if (idleUntil > totalCycles)
                {
                    totalCycles = std::min(target, idleUntil);
                    if (totalCycles == target) return;
                }
                while (totalCycles < target)
                {
                    serviceAudio();
                    servicePendingDmaInterrupts();
                    servicePendingGuestCallbacks();
                    if (totalCycles >= nextTimerCycle)
                    {
                        timrman.serviceDue(totalCycles, *this);
                        nextTimerCycle = timrman.nextEventCycle(UINT64_MAX);
                    }
                    // A callback above may already have spent the budget.
                    // Otherwise target-totalCycles underflows for a ready
                    // worker, or the idle path rewinds the hardware clock.
                    if(totalCycles>=target)break;
                    IopThread *next = kernel.beginNextReady(totalCycles);
                    if (!next)
                    {
                        uint64_t nextWake = kernel.nextWakeCycle(std::numeric_limits<uint64_t>::max());
                        for (const auto &[irq, completionCycle] : pendingDmaInterrupts)
                            nextWake = std::min(nextWake, completionCycle);
                        if (!pendingGuestCallbacks.empty())
                            nextWake = std::min(nextWake, pendingGuestCallbacks.begin()->first);
                        nextWake = std::min(nextWake, nextTimerCycle);
                        nextWake = std::min(nextWake, memory.nextAudioCycle());
                        idleUntil = std::max(totalCycles + 1u, nextWake);
                        totalCycles = std::min(target, idleUntil);
                        continue;
                    }
                    idleUntil = 0;
                    const uint64_t before = totalCycles;
                    runCpu(next->cpu, static_cast<uint32_t>(std::min<uint64_t>(kDefaultSlice, target - totalCycles)));
                    kernel.endTimeslice(*next, kThreadReturnSentinel);
                    if (totalCycles == before)
                        ++totalCycles;
                }
            }
            catch (...)
            {
                // Runtime scheduling must never throw through EeScheduler::accountCycles().
            }
        }

        ModuleLoadResult loadImage(std::string path, std::span<const uint8_t> image, const void *arguments, uint32_t argumentSize)
        {
            ModuleLoadResult result{true, -1, -1};
            if ((!arguments && argumentSize) || argumentSize > IopMemory::RamSize ||
                path.size() >= IopMemory::RamSize)
                return result;
            const IopImageLoadResult loaded = IopModuleLoader::load(image, memory, moduleCursor);
            moduleCursor = loaded.nextModuleCursor;
            if (!loaded)
            {
                if (loaded.error == IopImageLoadError::InvalidElf)
                    log(LogLevel::Error, "[IOP] rejected invalid/non-MIPS IRX ELF");
                else if (loaded.error == IopImageLoadError::ArenaExhausted)
                    log(LogLevel::Error, "[IOP] module arena exhausted");
                return result;
            }
            if (!loaded.relocationsComplete)
                log(LogLevel::Warning, "[IOP] one or more IRX relocations were unsupported");

            Module module;
            module.id = nextModuleId++;
            module.path = std::move(path);
            const size_t slash = module.path.find_last_of("/\\:");
            module.name = slash == std::string::npos ? module.path : module.path.substr(slash + 1u);
            module.base = loaded.base;
            module.size = loaded.size;
            module.entry = loaded.entry;
            module.gp = loaded.gp;

            // LoadModule's argument payload is a sequence of NUL-terminated
            // strings. The IRX entry receives argc/argv, including its path as
            // argv[0], rather than the EE transport's byte count and raw bytes.
            std::vector<uint32_t> offsets{0u};
            const auto pathBytes = uint32_t(module.path.size() + 1u);
            const auto *payload = static_cast<const char *>(arguments);
            for (uint32_t position = 0; position < argumentSize;)
            {
                offsets.push_back(pathBytes + position);
                const auto *end = std::find(payload + position, payload + argumentSize, '\0');
                position = uint32_t(end - payload) + 1u;
            }
            const uint64_t stringBytes = uint64_t(pathBytes) + argumentSize + 1u;
            const uint64_t argvOffset = (stringBytes + 3u) & ~uint64_t(3u);
            const uint64_t storageBytes = argvOffset + (offsets.size() + 1u) * sizeof(uint32_t);
            const auto storage = storageBytes <= IopMemory::RamSize ? allocate(uint32_t(storageBytes), 16u) : 0u;
            if (!storage)
            {
                freeAllocation(loaded.base);
                return result;
            }
            writeRam(storage, module.path.c_str(), pathBytes);
            if (argumentSize) writeRam(storage + pathBytes, arguments, argumentSize);
            write8(storage + pathBytes + argumentSize, 0u);
            const auto argv = storage + uint32_t(argvOffset);
            for (size_t i = 0; i < offsets.size(); ++i)
                write32(argv + uint32_t(i * sizeof(uint32_t)), storage + offsets[i]);
            write32(argv + uint32_t(offsets.size() * sizeof(uint32_t)), 0u);
            const uint32_t startResult = callFunction(module.entry, uint32_t(offsets.size()), argv, 0u, 0u, module.gp);
            freeAllocation(storage);
            module.resident = startResult == 0u || startResult == 2u;
            result.moduleId = module.id;
            result.startResult = static_cast<int32_t>(startResult);
            modules[module.id] = std::move(module);

            std::ostringstream out;
            out << "[IOP] loaded IRX id=" << result.moduleId
                << " entry=0x" << std::hex << modules[result.moduleId].entry
                << " base=0x" << modules[result.moduleId].base
                << " start=" << std::dec << result.startResult;
            log(LogLevel::Info, out.str());
            return result;
        }

        ModuleLoadResult loadModule(std::string_view path, const void *arguments, uint32_t argumentSize)
        {
            std::vector<uint8_t> image;
            if (!IopModuleLoader::readWholeHostFile(host, path, image))
            {
                log(LogLevel::Warning, std::string("[IOP] failed to open IRX '") + std::string(path) + "'");
                return {true, -1, -1};
            }
            return loadImage(std::string(path), image, arguments, argumentSize);
        }

        ModuleLoadResult loadModuleBuffer(uint32_t guestAddress, const void *arguments, uint32_t argumentSize)
        {
            std::vector<uint8_t> image;
            if (!IopModuleLoader::readElfFromGuest(host, guestAddress, image))
                return {true, -1, -1};
            std::ostringstream tag;
            tag << "buffer@0x" << std::hex << guestAddress;
            return loadImage(tag.str(), image, arguments, argumentSize);
        }

        bool stopModule(int32_t moduleId, int32_t *result)
        {
            auto it = modules.find(moduleId);
            if (it == modules.end())
                return false;
            // A removable IRX normally exposes a stop entry through module metadata. We do not guess it; terminate owned execution and release the image cleanly.
            kernel.terminateThreadsInRange(it->second.base, it->second.size);
            rpc.removeServersInRange(it->second.base, it->second.size);
            imports.eraseRange(it->second.base, it->second.size);
            modules.erase(it);
            kernel.cleanupDeadThreads();
            if (result)
                *result = 0;
            return true;
        }

        IopHost &host;
        IopMemory memory;
        IopSysmem sysmem;
        IopKernel kernel;
        IopCdvd cdvd;
        IopVblank vblank;
        IopRpcBridge rpc;
        IopSysclib sysclib;
        IopStdio stdio;
        IopHeaplib heaplib;
        IopIntrman intrman;
        IopTimrman timrman;
        IopIoman ioman;
        IopCpuCore cpuCore;
        IopImportRegistry imports;
        IopLoadcore loadcore;
        std::map<int, Module> modules;
        std::map<int, uint64_t> pendingDmaInterrupts;
        std::multimap<uint64_t, ScheduledGuestCallback> pendingGuestCallbacks;
        uint32_t nextModuleId = 1;
        uint32_t moduleCursor = kModuleLoadBase;
        uint64_t totalCycles = 0;
        uint64_t requestedCycles = 0;
        uint64_t idleUntil = 0;
        uint64_t nextTimerCycle = UINT64_MAX;
        uint64_t totalInstructions = 0;
        cave::ps2::IopProfile profile;
        uint64_t eeCycleCarry = 0;
        CpuState *activeCpu = nullptr;
        std::string lastError;
        std::map<std::string, uint64_t> missingImportCounts;
        bool servicingAudioInterrupt = false;
        bool audioIrqDelivered = false;
        bool servicingDmaInterrupts = false;
        bool servicingGuestCallbacks = false;
        uint32_t callDepth = 0u;
        GuestCallback secrMcCommandHandler;
        GuestCallback secrMcDevIdHandler;
        GuestCallback checkKelfPathCallback;
    };

    IopEmulator::IopEmulator(IopHost &host)
        : m_impl(std::make_unique<Impl>(host))
    {
    }

    IopEmulator::~IopEmulator() = default;

    void IopEmulator::reset()
    {
        m_impl->reset();
    }

    ModuleLoadResult IopEmulator::loadModule(std::string_view path, const void *arguments, uint32_t argumentSize)
    {
        return m_impl->loadModule(path, arguments, argumentSize);
    }

    ModuleLoadResult IopEmulator::loadModuleBuffer(uint32_t guestAddress, const void *arguments, uint32_t argumentSize)
    {
        return m_impl->loadModuleBuffer(guestAddress, arguments, argumentSize);
    }

    bool IopEmulator::stopModule(int32_t moduleId, int32_t *result)
    {
        m_impl->idleUntil = 0;
        return m_impl->stopModule(moduleId, result);
    }

    void IopEmulator::runEeCycles(uint64_t eeCycles) noexcept
    {
        const uint64_t total = m_impl->eeCycleCarry + eeCycles;
        const uint64_t iopCycles = total / 8u;
        m_impl->eeCycleCarry = total % 8u;
        if (iopCycles)
            m_impl->runCycles(iopCycles);
    }

    size_t IopEmulator::readPcm(void *stereo, size_t frames) noexcept
    {
        return m_impl->memory.readPcm(stereo, frames);
    }

    RpcResult IopEmulator::handleRpc(const RpcRequest &request)
    {
        m_impl->idleUntil = 0;
        bool tracePacket = false;
        if (cave::ps2::audio_trace::enabled)
        {
            const auto key = "rpc:" + std::to_string(request.sid) + ':' + std::to_string(request.function);
            tracePacket = ++m_impl->missingImportCounts[key] <= 8u;
            if (tracePacket)
            {
                std::array<uint32_t, 8> words{};
                m_impl->host.readGuest(request.send.address, words.data(), std::min<size_t>(request.send.size, sizeof(words)));
                std::osyncstream line(std::cerr);
                line << "[ps2-iop-rpc-request] sid=0x" << std::hex << request.sid << " fn=0x" << request.function << " words=";
                for (auto word : words) line << word << ',';
                line << std::dec << '\n';
                if (request.function == 3u)
                {
                    line << "[ps2-iop-rpc-object-before] words=" << std::hex;
                    for (unsigned i=0; i<20u; ++i) line << m_impl->memory.read32(words[0]+i*4u) << ',';
                    line << std::dec << '\n';
                }
            }
        }
        const bool traceTransfer = cave::ps2::audio_trace::enabled &&
            request.sid == 0x80000701u && request.function == 0x80d0u;
        if (traceTransfer)
        {
            std::array<uint32_t, 16> words{};
            m_impl->host.readGuest(request.send.address, words.data(),
                                  std::min<size_t>(request.send.size, sizeof(words)));
            std::osyncstream line(std::cerr);
            line << "[ps2-audio-bank] ms=" << cave::ps2::audio_trace::Milliseconds() << " packet=" << std::hex;
            for (auto word : words) line << word << ',';
            line << std::dec << '\n';
        }
        auto result = m_impl->rpc.handleRpc(request, *m_impl);
        if (tracePacket)
        {
            std::array<uint32_t, 8> words{};
            m_impl->host.readGuest(request.receive.address, words.data(), std::min<size_t>(request.receive.size, sizeof(words)));
            std::osyncstream line(std::cerr);
            line << "[ps2-iop-rpc-result] sid=0x" << std::hex << request.sid << " fn=0x" << request.function << " words=";
            for (auto word : words) line << word << ',';
            line << std::dec << " handled=" << result.handled << '\n';
            if (request.function == 6u || (request.sid == 0x209904u && request.function == 15u)) line << m_impl->kernel.debugState();
        }
        if (traceTransfer)
        {
            std::array<uint32_t, 4> words{};
            m_impl->host.readGuest(request.receive.address, words.data(),
                                  std::min<size_t>(request.receive.size, sizeof(words)));
            std::osyncstream line(std::cerr);
            line << "[ps2-audio-bank-result] handled=" << result.handled << " packet=" << std::hex;
            for (auto word : words) line << word << ',';
            line << std::dec << '\n';
        }
        return result;
    }

    bool IopEmulator::hasRpcServer(uint32_t sid) const noexcept
    {
        return m_impl->rpc.hasServer(sid);
    }

    void IopEmulator::onSifTransfer(const SifTransfer &transfer)
    {
        m_impl->idleUntil = 0;
        m_impl->rpc.onSifTransfer(transfer);
    }

    uint32_t IopEmulator::allocateMemory(uint32_t size, uint32_t alignment)
    {
        return m_impl->memory.allocate(size, alignment);
    }

    bool IopEmulator::freeMemory(uint32_t address)
    {
        return m_impl->memory.freeAllocation(address);
    }

    bool IopEmulator::readMemory(uint32_t address, void *destination, size_t size) const
    {
        return isMemoryRange(address, size) &&
               m_impl->memory.readRam(address, destination, size);
    }

    bool IopEmulator::writeMemory(uint32_t address, const void *source, size_t size)
    {
        m_impl->idleUntil = 0;
        return isMemoryRange(address, size) &&
               m_impl->memory.writeRam(address, source, size);
    }

    bool IopEmulator::zeroMemory(uint32_t address, size_t size)
    {
        m_impl->idleUntil = 0;
        return isMemoryRange(address, size) &&
               m_impl->memory.zeroRam(address, size);
    }

    bool IopEmulator::isMemoryRange(uint32_t address, size_t size) const
    {
        const bool physicalSegment = address < IopMemory::RamSize;
        const bool cachedSegment = address >= 0x80000000u && address < 0x80200000u;
        const bool uncachedSegment = address >= 0xA0000000u && address < 0xA0200000u;
        if (!physicalSegment && !cachedSegment && !uncachedSegment)
            return false;
        const uint32_t physical = IopMemory::physicalAddress(address);
        return physical <= IopMemory::RamSize && size <= IopMemory::RamSize - physical;
    }

    uint64_t IopEmulator::cycles() const noexcept
    {
        return m_impl->totalCycles;
    }

    uint64_t IopEmulator::instructions() const noexcept
    {
        return m_impl->totalInstructions;
    }

    uint32_t IopEmulator::loadedModuleCount() const noexcept
    {
        return static_cast<uint32_t>(m_impl->modules.size());
    }

    uint32_t IopEmulator::threadCount() const noexcept
    {
        return static_cast<uint32_t>(m_impl->kernel.threadCount());
    }

    uint32_t IopEmulator::rpcServerCount() const noexcept
    {
        return static_cast<uint32_t>(m_impl->rpc.serverCount());
    }

}
