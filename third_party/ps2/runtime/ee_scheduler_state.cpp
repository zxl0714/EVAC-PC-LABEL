#include "runtime/ee_scheduler.h"
#include "ee_wait_state.h"
#include <algorithm>

namespace {
using cave::ps2::StateReader;
using cave::ps2::StateWriter;
using Clock = std::chrono::steady_clock;

template<class Archive>
void Context(Archive& ar, R5900Context& c) {
    const auto vector = [&](auto& reg) {
        static_assert(sizeof(reg) == 16);
        ar.Bytes({reinterpret_cast<uint8_t*>(&reg), sizeof(reg)});
    };
    for (auto& reg : c.r) vector(reg);
    ar.Fields(c.pc, c.insn_count, c.hi, c.lo, c.hi1, c.lo1, c.sa);
    for (auto& reg : c.vu0_vf) vector(reg);
    for (auto& reg : c.vi) ar.Field(reg);
    ar.Fields(c.vu0_q, c.vu0_p, c.vu0_i);
    vector(c.vu0_r);
    vector(c.vu0_acc);
    ar.Fields(c.vu0_status, c.vu0_mac_flags, c.vu0_clip_flags, c.vu0_clip_flags2, c.vu0_cmsar0,
              c.vu0_cmsar1, c.vu0_cmsar2, c.vu0_cmsar3, c.vu0_vpu_stat, c.vu0_vpu_stat2,
              c.vu0_vpu_stat3, c.vu0_vpu_stat4, c.vu0_tpc, c.vu0_tpc2, c.vu0_fbrst,
              c.vu0_fbrst2, c.vu0_fbrst3, c.vu0_fbrst4, c.vu0_itop, c.vu0_top, c.vu0_info,
              c.vu0_xitop, c.vu0_pc);
    for (auto& reg : c.vu0_cf) ar.Field(reg);
    ar.Fields(c.cop0_index, c.cop0_random, c.cop0_entrylo0, c.cop0_entrylo1, c.cop0_context,
              c.cop0_pagemask, c.cop0_wired, c.cop0_badvaddr, c.cop0_count, c.cop0_entryhi,
              c.cop0_compare, c.cop0_status, c.cop0_cause, c.cop0_epc, c.cop0_prid, c.cop0_config,
              c.cop0_badpaddr, c.cop0_debug, c.cop0_perf, c.cop0_taglo, c.cop0_taghi, c.cop0_errorepc,
              c.llbit, c.lladdr, c.in_delay_slot, c.branch_pc);
    for (auto& reg : c.cop2_ccr) ar.Field(reg);
    for (auto& reg : c.f) ar.Field(reg);
    ar.Fields(c.f_acc, c.fcr31);
}

template<class Archive, class Enum> void EnumField(Archive& ar, Enum& value, Enum last) {
    auto byte = static_cast<uint8_t>(value);
    ar.Field(byte);
    if (byte > static_cast<uint8_t>(last)) throw std::invalid_argument("Unknown EE scheduler enum");
    if constexpr (std::is_same_v<Archive, StateReader>) value = static_cast<Enum>(byte);
}

template<class Archive>
void Invocation(Archive& ar, GuestInvocation& call) {
    EnumField(ar, call.kind, GuestInvocationKind::SifCommand);
    ar.Fields(call.stackSlot, call.sequence, call.tag);
    Context(ar, call.context);
    uint8_t tag = static_cast<uint8_t>(call.returnAction.index());
    ar.Field(tag);
    if constexpr (std::is_same_v<Archive, StateReader>) {
        switch (tag) {
        case 0: call.returnAction = std::monostate{}; break;
        case 1: call.returnAction = EeExitThread{}; break;
        case 2: call.returnAction = EeReturnV0{}; break;
        case 3: call.returnAction = EeFreeGuest{}; break;
        case 4: call.returnAction = EeFinishIpuInit{}; break;
        case 5: call.returnAction = cave::ps2::EeIopRpcReturn{}; break;
        case 6: call.returnAction = cave::ps2::EeRpcReturn{}; break;
        default: throw std::invalid_argument("Unknown EE invocation return action");
        }
    }
    if (auto* exit = std::get_if<EeExitThread>(&call.returnAction)) ar.Field(exit->erase);
    if (auto* free = std::get_if<EeFreeGuest>(&call.returnAction)) ar.Field(free->address);
    if (auto* rpc = std::get_if<cave::ps2::EeIopRpcReturn>(&call.returnAction)) {
        auto& r = rpc->reply;
        ar.Fields(rpc->packet, rpc->receiveSize, r.epoch, r.client, r.receive, r.thread,
                  r.endFunction, r.endArgument, r.gp, r.blocking);
        if (rpc->receiveSize > cave::ps2::maxIopRpcBytes) throw std::invalid_argument("Invalid IOP RPC reply size");
    }
    if (auto* r = std::get_if<cave::ps2::EeRpcReturn>(&call.returnAction)) {
        ar.Fields(r->clientPtr, r->serverPtr, r->sid, r->rpcNum, r->mode, r->sendBuf, r->sendSize,
                  r->receiveBuffer, r->receiveSize, r->endFunction, r->endParameter, r->completionSemaphore,
                  r->guestDefaultResult, r->resultPointer, r->callback, r->handledByIop, r->serverDispatched,
                  r->suppressCallback, r->handled, r->copiedFallback, r->zeroedFallback);
    }
}

template<class Archive, class Map, class Visit>
void Objects(Archive& ar, Map& map, size_t minimum, Visit visit) {
    if constexpr (std::is_same_v<Archive, StateWriter>) {
        std::vector<int> ids;
        ids.reserve(map.size());
        for (const auto& entry : map) ids.push_back(entry.first);
        std::sort(ids.begin(), ids.end());
        ar.Items(ids, UINT32_MAX, minimum, [&](int id) { visit(map.at(id)); });
    } else {
        for (auto n = ar.Count(UINT32_MAX, minimum); n; --n) {
            typename Map::mapped_type item{};
            visit(item);
            const auto id = item.id;
            if (!id || !map.emplace(id, std::move(item)).second)
                throw std::invalid_argument("Invalid or duplicate EE kernel object ID");
        }
    }
}

// Store the remaining time, including overdue events. The clock can have a
// different epoch after a relaunch or a reboot.
void Time(StateWriter& ar, Clock::time_point now, Clock::time_point value) {
    ar.Field(static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(value - now).count()));
}
void Time(StateReader& ar, Clock::time_point now, Clock::time_point& value) {
    int64_t nanos{};
    ar.Field(nanos);
    using namespace std::chrono;
    const auto delta = duration_cast<Clock::duration>(nanoseconds(nanos));
    if ((delta > Clock::duration::zero() && now.time_since_epoch() > Clock::duration::max() - delta) ||
        (delta < Clock::duration::zero() && now.time_since_epoch() < Clock::duration::min() - delta))
        throw std::invalid_argument("Invalid EE clock deadline");
    value = now + delta;
}
}

template<class Archive>
void EeScheduler::transferState(Archive& ar, EeSchedulerState& s, Clock::time_point now) {
    const auto ids = [&](auto& list) {
        ar.Items(list, UINT32_MAX, 4, [&](int& id) { ar.Field(id); });
    };
    const auto call = [&](GuestInvocation& inv) { Invocation(ar, inv); };
    for (auto& queue : s.m_readyQueues) ids(queue);
    Objects(ar, s.m_threads, 1024, [&](GuestThread& t) {
        ar.Field(t.id);
        Context(ar, t.context);
        ar.Fields(t.entry, t.stack, t.stackSize, t.gp, t.attr, t.option, t.arg, t.initialPriority, t.currentPriority);
        if (t.initialPriority < 0 || t.initialPriority >= kPriorityCount ||
            t.currentPriority < 0 || t.currentPriority >= kPriorityCount)
            throw std::invalid_argument("Invalid EE thread priority");
        EnumField(ar, t.status, EeThreadStatus::Dormant);
        ar.Fields(t.suspendCount, t.wakeupCount, t.ownsStack, t.tlsBase);
        cave::ps2::TransferEeWait(ar, t.wait);
        cave::ps2::TransferEeCompletion(ar, t.resumeCompletion);
        ar.Items(t.invocations, UINT32_MAX, 1024, call);
    });
    Objects(ar, s.m_semaphores, 28, [&](EeSemaphore& sem) {
        ar.Fields(sem.id, sem.count, sem.maxCount, sem.initCount, sem.attr, sem.option);
        ids(sem.waiters);
    });
    Objects(ar, s.m_eventFlags, 24, [&](EeEventFlag& f) {
        ar.Fields(f.id, f.attr, f.option, f.initBits, f.bits);
        ids(f.waiters);
    });
    Objects(ar, s.m_alarms, 22, [&](EeAlarm& a) { ar.Fields(a.id, a.ticks, a.handler, a.argument, a.gp, a.sp); });
    const auto irq = [&](EeIrqHandler& h) { ar.Fields(h.id, h.cause, h.handler, h.argument, h.gp, h.sp, h.enabled, h.order); };
    Objects(ar, s.m_intcHandlers, 29, irq);
    Objects(ar, s.m_dmacHandlers, 29, irq);
    ar.Fields(s.m_nextThreadId, s.m_nextInvocationThreadId, s.m_nextSemaphoreId, s.m_nextEventFlagId,
              s.m_nextAlarmId, s.m_nextIntcHandlerId, s.m_nextDmacHandlerId, s.m_intcHeadOrder,
              s.m_intcTailOrder, s.m_dmacHeadOrder, s.m_dmacTailOrder, s.m_enabledIntcMask,
              s.m_enabledDmacMask, s.m_currentThreadId, s.m_rescheduleRequested, s.m_timeSliceExpired,
              s.m_pendingEeTimerInterrupts, s.m_eeCycle, s.m_sliceEndCycle);
    Time(ar, now, s.m_hostClockOrigin);
    const auto event = [&](EeEvent& e) {
        EnumField(ar, e.type, EeEventType::GsStatus);
        ar.Fields(e.id, e.value);
    };
    const auto timed = [&](ScheduledEvent& e) {
        ar.Field(e.deadlineCycle);
        Time(ar, now, e.hostDeadline);
        event(e.event);
        ar.Field(e.sequence);
    };
    ar.Items(s.m_events, UINT32_MAX, 13, event);
    ar.Items(s.m_deadlines, UINT32_MAX, 37, timed);
    bool deferred = s.m_deferredVBlank.has_value();
    ar.Field(deferred);
    if (deferred) {
        if constexpr (std::is_same_v<Archive, StateReader>) s.m_deferredVBlank.emplace();
        timed(*s.m_deferredVBlank);
    }
    ar.Fields(s.m_pendingScanoutTick, s.m_previousDispfb2, s.m_eventSequence, s.m_invocationSequence,
              s.m_vsyncTick, s.m_vsyncFlagAddress, s.m_vsyncTickAddress, s.m_gsVSyncCallback,
              s.m_gsVSyncCallbackGp, s.m_gsVSyncCallbackSp);
    ar.Items(s.m_pendingInvocations, UINT32_MAX, 1024, call);
    if constexpr (std::is_same_v<Archive, StateReader>) {
        s.m_invocationStackSlots.resize(ar.Count(UINT32_MAX, 1));
        for (size_t i = 0; i < s.m_invocationStackSlots.size(); ++i) {
            bool used{};
            ar.Field(used);
            s.m_invocationStackSlots[i] = used;
        }
    } else {
        ar.Field(static_cast<uint32_t>(s.m_invocationStackSlots.size()));
        for (bool used : s.m_invocationStackSlots) ar.Field(used);
    }
}

std::vector<uint8_t> EeScheduler::saveState() {
    if (!isExecutorThread() || m_guestExecuting.load() || !m_rdram)
        throw std::logic_error("Save requires a parked EE dispatcher");
    std::vector<uint8_t> bytes;
    StateWriter ar(bytes);
    ar.Field(uint32_t{1});
    std::lock_guard lock(m_eventMutex);
    transferState(ar, *this, m_parkedAt.value_or(Clock::now()));
    return bytes;
}

void EeScheduler::loadState(std::span<const uint8_t> bytes) {
    if (!isExecutorThread() || m_guestExecuting.load() || !m_rdram)
        throw std::logic_error("Restore requires a parked EE dispatcher");
    StateReader ar(bytes);
    uint32_t version{};
    ar.Field(version);
    if (version != 1) throw std::invalid_argument("Incompatible EE scheduler state");
    EeSchedulerState next;
    transferState(ar, next, m_parkedAt.value_or(Clock::now()));
    ar.Finish();
    if (next.m_currentThreadId && !next.m_threads.contains(next.m_currentThreadId))
        throw std::invalid_argument("Unknown running EE thread");
    std::vector<bool> used(next.m_invocationStackSlots.size());
    const auto stack = [&](const GuestInvocation& inv) {
        if (inv.stackSlot < GuestInvocation::PendingStack) throw std::invalid_argument("Invalid EE invocation stack");
        if (inv.stackSlot < 0) return;
        const auto index = static_cast<size_t>(inv.stackSlot);
        if (index >= used.size() || used[index]) throw std::invalid_argument("Invalid or shared EE invocation stack");
        used[index] = true;
    };
    for (const auto& [id, t] : next.m_threads)
        for (const auto& invocation : t.invocations) stack(invocation);
    for (const auto& invocation : next.m_pendingInvocations) stack(invocation);
    if (used != next.m_invocationStackSlots) throw std::invalid_argument("Inconsistent EE invocation stacks");
    for (size_t i = 0; i < next.m_readyQueues.size(); ++i) {
        for (int id : next.m_readyQueues[i]) {
            auto it = next.m_threads.find(id);
            if (it == next.m_threads.end() || it->second.status != EeThreadStatus::Ready ||
                it->second.currentPriority != static_cast<int>(i))
                throw std::invalid_argument("Inconsistent EE ready queue");
        }
    }
    {
        std::lock_guard lock(m_eventMutex);
        static_assert(std::is_nothrow_move_assignable_v<EeSchedulerState>);
        static_cast<EeSchedulerState&>(*this) = std::move(next);
        updateNextDeadline();
        m_checkpointPending.store(true, std::memory_order_release);
    }
    copyMainContextToRuntime();
    publishSnapshot();
}
