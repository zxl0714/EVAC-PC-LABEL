#pragma once
#include "gs_completions.h"
#include <functional>
#include <memory>

namespace cave::ps2 {
// Fixed-size producer/consumer mailbox. Only completed GS events are published;
// it does not itself imply that a CPU-parsed command has completed on the GPU.
class GsCompletionMailbox {
public:
    GsCompletionMailbox();
    ~GsCompletionMailbox();
    GsCompletionMailbox(const GsCompletionMailbox&)=delete;
    GsCompletionMailbox& operator=(const GsCompletionMailbox&)=delete;
    uint64_t Generation() const noexcept;
    void SetNotifier(std::function<void()> notify);
    // A second SIGNAL waits for EE's CSR acknowledgement. An owner-thread
    // message pump can run during the wait without touching frontend GS state.
    bool Signal(uint64_t generation,uint64_t payload,const std::function<void()>& pump={});
    void Finish(uint64_t generation);
    void Label(uint64_t generation,uint64_t payload);
    GsCompletions Take();
    void AcknowledgeSignal();
    void AcknowledgeStatus(uint32_t mask);
    uint64_t Reset();
    void Close();
private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
}
