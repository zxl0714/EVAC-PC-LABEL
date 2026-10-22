#pragma once
#include "handoff.h"
#include <algorithm>

namespace sdoj::latency {
// The video clock and the renderer share steady_clock. Keep the existing final
// native wait; this budget only borrows otherwise idle time BEFORE rendering.
constexpr std::uint64_t WaitBudget(std::uint64_t now, std::uint64_t deadline) {
    constexpr std::uint64_t DrawingReserve = 4000, MaximumWait = 2000;
    return deadline > now && deadline-now > DrawingReserve
        ? std::min(MaximumWait, deadline-now-DrawingReserve) : 0;
}
struct Request {
    std::uint64_t generation{}, number{}, previous_logic{}, input_epoch{};
    std::uint32_t frame{}, renderer{}, callback{}, calls{};
    bool wakes_writer{},resumes_intermediate{};
    bool operator==(const Request&) const = default;
};
// Re-enter early scheduling after a native-late completion. Draw that result
// first, then move its still-pending late wake to the NEXT video's early slot.
// No extra parser invocation, dropped batch, fabricated metrics or extra wake.
// This receipt authorizes only that same frame's late-driver cut, and only
// after the exact completed batch was published and really parsed.
class DrawRecovery {
public:
    void Reset() { pending_.reset(); }
    bool armed() const { return pending_.has_value(); }
    bool Arm(const Completion& ready,const NativeState& native) {
        pending_.reset();
        if(!ready.depth || native.countdown!=1 || native.renderer!=ready.renderer ||
           native.callback!=ready.callback ||
           std::uint32_t(native.calls-ready.baseline)!=ready.depth-1)return false;
        pending_=Pending{ready,native.frame,native.calls};return true;
    }
    bool Defer(const std::optional<Completion>& ready,const NativeState& native,
               bool parsed,bool swap_receipt) {
        const auto pending=pending_;pending_.reset();
        return pending && ready && *ready==pending->ready && parsed && swap_receipt &&
            native.frame==pending->frame && native.calls==pending->calls && native.countdown==1 &&
            native.renderer==ready->renderer && native.callback==ready->callback &&
            native.serial==std::uint32_t(ready->serial+1) && native.published==ready->buffer &&
            native.writer && native.writer!=0xffffffff && native.writer!=ready->buffer &&
            (native.status&0x10) && !native.busy;
    }
private:
    struct Pending { Completion ready; std::uint32_t frame,calls; };
    std::optional<Pending> pending_;
};
// Per-step receipts are deliberately independent. The native prefix runs once;
// callback/masks/helper/countdown and the wake counter run once; peripherals
// remain in the late driver and must NEVER be skipped with the moved steps.
class LowHandoff {
public:
    void Load(std::uint64_t generation) {
        generation_=generation; number_=0; active_=true; disabled_=false;
        request_.reset(); late_.reset(); draining_.reset(); prefix_.reset();
    }
    void Retire() { active_=false; }
    bool CanRequest() const { return active_ && !disabled_ && !request_ && !late_ && !draining_ && !prefix_; }
    std::optional<Request> Begin(const Completion& parked, const NativeState& state, std::uint64_t input_epoch=0) {
        if (!CanRequest() || parked.generation!=generation_ || !parked.depth ||
            parked.renderer!=state.renderer || parked.callback!=state.callback ||
            std::uint32_t(state.calls-parked.baseline)>=parked.depth) return {};
        const auto remaining=parked.depth-std::uint32_t(state.calls-parked.baseline);
        const bool unpublished=state.serial==parked.serial && state.finished==parked.buffer &&
            state.writer==0xffffffff;
        const bool published=state.serial==std::uint32_t(parked.serial+1) && state.published==parked.buffer &&
            state.writer && state.writer!=0xffffffff && state.writer!=parked.buffer;
        // The due callback must have a valid next writer before waking it.
        // If the bootstrap swap was rejected (busy/ownership/reader gate), do
        // not bypass those gates by letting the early callback attempt it.
        // Middle slowdown ticks may retain their still-unpublished result.
        if (state.countdown!=remaining || (!unpublished && !published) ||
            (remaining==1 && !published)) return {};
        request_=Request{generation_,++number_,parked.logic,input_epoch,state.frame,state.renderer,state.callback,
                         state.calls,std::uint32_t(state.calls-parked.baseline+1)>=parked.depth};
        return request_;
    }
    std::optional<Request> BeginIntermediate(const IntermediateWait& wait,const NativeState& state,
                                              std::uint64_t input_epoch=0) {
        if(!CanRequest() || wait.generation!=generation_ || !wait.depth ||
           wait.renderer!=state.renderer || wait.callback!=state.callback ||
           std::uint32_t(state.calls-wait.baseline)>=wait.depth)return {};
        const auto remaining=wait.depth-std::uint32_t(state.calls-wait.baseline);
        // The previous successful swap cleared the DRAW countdown. This
        // internal wait watches only the independent native wake counter;
        // countdown is not reset until the unfinished update completes.
        if(state.countdown!=0)return {};
        // Resume this same unfinished update; do not allocate another writer
        // or publish its partial command stream before advancing the wait.
        request_=Request{generation_,++number_,wait.logic,input_epoch,state.frame,state.renderer,state.callback,
                         state.calls,remaining==1,true};
        return request_;
    }
    bool PrefixDone(const Request& request) {
        if (!request_ || *request_!=request || prefix_) return false;
        prefix_=request; return true;
    }
    void CancelBeforeDrive(const Request& request) {
        if (request_ && *request_==request) request_.reset();
    }
    bool DriveDone(const Request& request, const NativeState& after, std::uint32_t result) {
        if (!request_ || *request_!=request || !prefix_ || result!=1 ||
            after.calls!=std::uint32_t(request.calls+1) || after.callback!=request.callback ||
            after.renderer!=request.renderer) return false;
        late_=request; request_.reset(); return true;
    }
    bool IsComplete(const Request& request, const std::optional<Completion>& ready) const {
        return ready && ready->generation==request.generation &&
            (request.resumes_intermediate ? ready->logic==request.previous_logic : ready->logic>request.previous_logic) &&
            ready->baseline==std::uint32_t(request.calls+1) && ready->renderer==request.renderer &&
            ready->callback==request.callback;
    }
    bool IsIntermediate(const Request& request, const std::optional<IntermediateWait>& wait) const {
        return wait && wait->generation==request.generation &&
            (request.resumes_intermediate ? wait->logic==request.previous_logic : wait->logic>request.previous_logic) &&
            wait->baseline==std::uint32_t(request.calls+1) && wait->renderer==request.renderer &&
            wait->callback==request.callback;
    }
    void Timeout(const Request& request) { draining_=request; }
    bool Recover(const std::optional<Completion>& ready) {
        if (!draining_ || !IsComplete(*draining_,ready)) return false;
        draining_.reset(); return true;
    }
    bool RecoverIntermediate(const std::optional<IntermediateWait>& wait) {
        if (!draining_ || !IsIntermediate(*draining_,wait)) return false;
        draining_.reset(); return true;
    }
    bool ConsumePrefix(std::uint32_t frame, std::uint32_t renderer) {
        if (!prefix_ || prefix_->generation!=generation_ || prefix_->frame!=frame || prefix_->renderer!=renderer)
            return false;
        prefix_.reset(); return true;
    }
    bool ConsumeDriver(std::uint32_t frame, std::uint32_t calls) {
        if (!late_ || late_->generation!=generation_ || late_->frame!=frame ||
            std::uint32_t(late_->calls+1)!=calls) return false;
        late_.reset(); return true;
    }
    bool SuppressWriter() const { return draining_.has_value(); }
    bool HasPrefix() const { return prefix_.has_value(); }
    bool HasDriver() const { return late_.has_value(); }
    void Disable() { disabled_=true; }
    bool disabled() const { return disabled_; }
private:
    std::uint64_t generation_{}, number_{};
    bool active_{}, disabled_{};
    std::optional<Request> request_, prefix_, late_, draining_;
};
}
