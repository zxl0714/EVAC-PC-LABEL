#pragma once
#include <cstdint>
#include <optional>

namespace sdoj::latency {
// The adapter serializes this model, not guest execution. A completed command
// buffer is immutable until the original render-call counter wakes its writer.
struct NativeState {
    std::uint32_t frame{}, renderer{}, callback{}, serial{}, calls{}, countdown{};
    std::uint32_t writer{}, finished{}, published{}, cursor{}, status{}, busy{};
};
struct Completion {
    std::uint64_t generation{}, scene{}, logic{}, batch{};
    std::uint32_t renderer{}, callback{}, buffer{}, cursor{}, serial{}, baseline{}, depth{};
    bool operator==(const Completion&) const = default;
};
struct Publication {
    Completion completion;
    std::uint32_t frame{}, calls{}, receipt_calls{};
    bool operator==(const Publication&) const = default;
};
// A native resource wait inside a logic update is a quiescent yield, not a
// completed draw buffer. Its original frame ticks must continue to make progress.
struct IntermediateWait {
    std::uint64_t generation{}, logic{};
    std::uint32_t renderer{}, callback{}, baseline{}, depth{};
};
enum class Gate { Ready, Inactive, Original, Pending, Reading, NoCompletion,
                  Scene, Countdown, Writer, Serial, Wake, Metadata };
inline const char* GateName(Gate value) {
    switch (value) {
    case Gate::Ready: return "ready";
    case Gate::Inactive: return "inactive";
    case Gate::Original: return "original";
    case Gate::Pending: return "pending_receipt";
    case Gate::Reading: return "reader_active";
    case Gate::NoCompletion: return "no_completion";
    case Gate::Scene: return "scene";
    case Gate::Countdown: return "countdown";
    case Gate::Writer: return "writer";
    case Gate::Serial: return "serial";
    case Gate::Wake: return "already_woken";
    case Gate::Metadata: return "metadata";
    }
    return "invalid";
}
class Handoff {
public:
    // A native DLL can remain mapped after the guest module is unloaded. Never
    // use a C++ static's constructor as the guest-module lifetime boundary.
    bool Load(unsigned mode) {
        if (publishing_) return false;
        ++generation_; scene_ = 0; logic_ = batch_ = 0;
        mode_ = mode; active_ = true; reading_ = false;
        completion_.reset(); parked_.reset(); intermediate_.reset(); receipt_.reset(); writer_ = 0; renderer_ = callback_ = 0;
        return true;
    }
    void Retire() {
        active_ = false; completion_.reset(); parked_.reset(); intermediate_.reset();
        // A publication already performed must still settle its late call.
        // Retiring is not permission to execute that swap a second time.
    }
    void Logic(const NativeState& native) {
        if (!active_) return;
        if (native.renderer != renderer_ || native.callback != callback_) {
            ++scene_; renderer_ = native.renderer; callback_ = native.callback;
        }
        ++logic_; writer_ = 0; completion_.reset(); parked_.reset(); intermediate_.reset();
    }
    void Commands(std::uint32_t buffer, std::uint32_t serial) {
        if (!active_ || !logic_) return;
        ++batch_; writer_ = buffer; serial_ = serial;
    }
    bool Park(const NativeState& native, std::uint32_t baseline, std::uint32_t depth,
              bool complete_sentinel) {
        if (!active_ || !logic_ || !writer_ || writer_ == 0xffffffff ||
            !depth || depth > 16 || !complete_sentinel ||
            native.renderer != renderer_ || native.callback != callback_ ||
            native.serial != serial_ || native.finished != writer_ ||
            native.writer != 0xffffffff || native.calls != baseline) return false;
        completion_ = Completion{generation_, scene_, logic_, batch_, renderer_, callback_,
            writer_, native.cursor, serial_, baseline, depth};
        parked_ = completion_;
        intermediate_.reset();
        return true;
    }
    bool ParkIntermediate(const NativeState& native, std::uint32_t baseline, std::uint32_t depth) {
        if (!active_ || mode_!=1 || !logic_ || !depth || depth>16 || completion_ || parked_ ||
            publishing_ || native.calls!=baseline || native.renderer!=renderer_ || native.callback!=callback_)
            return false;
        intermediate_=IntermediateWait{generation_,logic_,renderer_,callback_,baseline,depth};
        return true;
    }
    void Unpark() { completion_.reset(); parked_.reset(); intermediate_.reset(); }
    void ReadBegin() { reading_ = true; }
    void ReadEnd() { reading_ = false; }
    Gate Eligible(const NativeState& native, bool current_complete=false) const {
        if (!active_) return Gate::Inactive;
        if (mode_ == 3) return Gate::Original;
        if (publishing_ || receipt_) return Gate::Pending;
        if (reading_) return Gate::Reading;
        if (!completion_) return Gate::NoCompletion;
        const auto& ready = *completion_;
        if (ready.generation != generation_ || ready.scene != scene_ ||
            ready.renderer != native.renderer || ready.callback != native.callback) return Gate::Scene;
        const bool fresh_low=mode_==1 && current_complete &&
            native.calls==ready.baseline && native.countdown==ready.depth;
        if (native.countdown != 1 && !fresh_low) return Gate::Countdown;
        if (native.writer != 0xffffffff) return Gate::Writer;
        if (native.serial != ready.serial) return Gate::Serial;
        if (std::uint32_t(native.calls - ready.baseline) >= ready.depth) return Gate::Wake;
        if (native.finished != ready.buffer || native.cursor != ready.cursor ||
            native.published == ready.buffer || !(native.status & 0x10) || native.busy)
            return Gate::Metadata;
        return Gate::Ready;
    }
    std::optional<Publication> BeginPublication(const NativeState& native, bool current_complete=false) {
        if (Eligible(native,current_complete) != Gate::Ready) return {};
        // Low may display a completed depth>1 result immediately, but its
        // native wait and eventual callback still consume the original ticks.
        const auto receipt_calls=std::uint32_t(native.calls+native.countdown-1);
        publishing_ = Publication{*completion_, native.frame, native.calls,receipt_calls};
        return publishing_;
    }
    // Only the complete native swap is accepted. The adapter executes it with
    // no model mutex held, then validates all of these ownership boundaries.
    bool Commit(const Publication& publication, const NativeState& after, std::uint32_t result) {
        if (!publishing_ || *publishing_ != publication) return false;
        publishing_.reset();
        const auto& ready = publication.completion;
        if (result != 1 || after.serial != std::uint32_t(ready.serial + 1) ||
            after.published != ready.buffer || !after.writer || after.writer == 0xffffffff ||
            after.writer == ready.buffer || after.calls != publication.calls ||
            after.renderer != ready.renderer || after.callback != ready.callback) {
            active_ = false; completion_.reset(); return false;
        }
        receipt_ = publication; completion_.reset();
        return true;
    }
    bool HasReceipt() const { return receipt_.has_value(); }
    // Match the original callback invocation, not a scene flag which may have
    // changed after a successful early swap. A successful swap cannot be undone.
    bool Consume(const NativeState& native, std::uint32_t callback) {
        if (!receipt_) return false;
        const auto& receipt = *receipt_;
        // A pause can suppress the callback for many host frames while its
        // native wake counter remains unchanged. Match that invocation instead
        // of requiring the original host frame number at eventual settlement.
        if (receipt.completion.generation != generation_ ||
            receipt.receipt_calls != native.calls || receipt.completion.callback != callback ||
            std::uint32_t(receipt.completion.serial + 1) != native.serial ||
            receipt.completion.buffer != native.published) return false;
        receipt_.reset(); return true;
    }
    const std::optional<Completion>& completed() const { return completion_; }
    const std::optional<Completion>& parked() const { return parked_; }
    const std::optional<IntermediateWait>& intermediate() const { return intermediate_; }
    bool active() const { return active_; }
    bool reading() const { return reading_; }
    std::uint64_t logic() const { return logic_; }
    std::uint64_t generation() const { return generation_; }
private:
    bool active_{}, reading_{};
    unsigned mode_{3};
    std::uint64_t generation_{}, scene_{}, logic_{}, batch_{};
    std::uint32_t writer_{}, serial_{}, renderer_{}, callback_{};
    std::optional<Completion> completion_, parked_;
    std::optional<IntermediateWait> intermediate_;
    std::optional<Publication> publishing_, receipt_;
};
}
