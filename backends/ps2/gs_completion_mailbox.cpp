#include "gs_completion_mailbox.h"
#include "state_archive.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

namespace cave::ps2 {
class GsCompletionMailbox::Impl {
public:
    std::atomic<uint64_t> generation=1;
    std::atomic<bool> ready=false;
    std::mutex mutex;
    std::condition_variable changed;
    GsCompletions pending;
    bool outstandingSignal=false,closed=false;
    std::function<void()> notify;
    bool Current(uint64_t value) const{return value==generation.load(std::memory_order_relaxed);}
};
GsCompletionMailbox::GsCompletionMailbox():impl(std::make_unique<Impl>()){}
GsCompletionMailbox::~GsCompletionMailbox(){Close();}
uint64_t GsCompletionMailbox::Generation() const noexcept{return impl->generation.load(std::memory_order_acquire);}
void GsCompletionMailbox::SetNotifier(std::function<void()> notify) {
    std::function<void()> wake;
    {
        std::lock_guard lock(impl->mutex);impl->notify=std::move(notify);
        if(impl->ready.load(std::memory_order_relaxed))wake=impl->notify;
    }
    if(wake)wake();
}
bool GsCompletionMailbox::Signal(uint64_t generation,uint64_t payload,const std::function<void()>& pump) {
    std::unique_lock lock(impl->mutex);
    while(impl->Current(generation) && !impl->closed && impl->outstandingSignal) {
        if(pump) {
            impl->changed.wait_for(lock,std::chrono::milliseconds(1));
            lock.unlock();pump();lock.lock();
        }else impl->changed.wait(lock);
    }
    if(impl->closed)throw std::runtime_error("GS completion mailbox closed");
    if(!impl->Current(generation))return false;
    impl->outstandingSignal=true;
    impl->pending.signal=true;impl->pending.signalPayload=payload;
    impl->pending.generation=generation;
    auto wake=impl->ready.exchange(true,std::memory_order_acq_rel)?std::function<void()>{}:impl->notify;
    lock.unlock();if(wake)wake();
    return true;
}
void GsCompletionMailbox::Finish(uint64_t generation) {
    std::unique_lock lock(impl->mutex);
    if(impl->closed || !impl->Current(generation))return;
    impl->pending.finish=true;impl->pending.generation=generation;
    auto wake=impl->ready.exchange(true,std::memory_order_acq_rel)?std::function<void()>{}:impl->notify;
    lock.unlock();if(wake)wake();
}
void GsCompletionMailbox::Label(uint64_t generation,uint64_t payload) {
    std::unique_lock lock(impl->mutex);
    if(impl->closed || !impl->Current(generation))return;
    const auto mask=uint32_t(payload>>32),value=uint32_t(payload);
    const auto previous=uint32_t(impl->pending.labelPayload);
    const auto previousMask=uint32_t(impl->pending.labelPayload>>32);
    impl->pending.labelPayload=(uint64_t(previousMask|mask)<<32)|((previous&~mask)|(value&mask));
    impl->pending.label=true;impl->pending.generation=generation;
    auto wake=impl->ready.exchange(true,std::memory_order_acq_rel)?std::function<void()>{}:impl->notify;
    lock.unlock();if(wake)wake();
}
GsCompletions GsCompletionMailbox::Take() {
    if(!impl->ready.load(std::memory_order_acquire))return {};
    std::lock_guard lock(impl->mutex);
    auto pending=impl->pending;impl->pending={};
    impl->ready.store(false,std::memory_order_release);
    return pending;
}
void GsCompletionMailbox::AcknowledgeSignal() {
    AcknowledgeStatus(1);
}
void GsCompletionMailbox::AcknowledgeStatus(uint32_t mask) {
    std::lock_guard lock(impl->mutex);
    if(mask&1u){impl->outstandingSignal=false;impl->pending.signal=false;}
    if(mask&2u)impl->pending.finish=false;
    impl->ready.store(bool(impl->pending),std::memory_order_release);
    impl->changed.notify_all();
}
uint64_t GsCompletionMailbox::Reset() {
    std::lock_guard lock(impl->mutex);
    if(impl->closed)throw std::runtime_error("GS completion mailbox closed");
    impl->pending={};impl->outstandingSignal=false;impl->ready.store(false,std::memory_order_release);
    const auto generation=impl->generation.fetch_add(1,std::memory_order_acq_rel)+1;
    impl->changed.notify_all();return generation;
}

std::vector<uint8_t> GsCompletionMailbox::SaveState() const {
    std::lock_guard lock(impl->mutex);
    if (impl->closed) throw std::runtime_error("GS completion mailbox closed");
    std::vector<uint8_t> data;
    StateWriter out(data);
    const auto& p = impl->pending;
    out.Fields(uint32_t{1}, impl->outstandingSignal, p.signal, p.finish, p.label,
               p.signalPayload, p.labelPayload);
    return data;
}

void GsCompletionMailbox::LoadState(std::span<const uint8_t> state) {
    StateReader in(state);
    uint32_t version{};
    bool outstanding{};
    GsCompletions pending;
    in.Fields(version, outstanding, pending.signal, pending.finish, pending.label,
              pending.signalPayload, pending.labelPayload);
    in.Finish();
    if (version != 1 || (pending.signal && !outstanding))
        throw std::invalid_argument("Invalid GS completion state");

    std::function<void()> wake;
    {
        std::lock_guard lock(impl->mutex);
        if (impl->closed) throw std::runtime_error("GS completion mailbox closed");
        // Queued work is drained before restore. A producer waiting on an old
        // acknowledgement must never deliver its signal into the restored run.
        pending.generation = impl->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
        impl->pending = pending;
        impl->outstandingSignal = outstanding;
        impl->ready.store(bool(pending), std::memory_order_release);
        if (pending) wake = impl->notify;
    }
    impl->changed.notify_all();
    if (wake) wake();
}

void GsCompletionMailbox::Close() {
    std::lock_guard lock(impl->mutex);impl->closed=true;impl->outstandingSignal=false;
    impl->pending={};impl->ready.store(false,std::memory_order_release);impl->changed.notify_all();
}
}
