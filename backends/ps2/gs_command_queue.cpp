#include "gs_command_queue.h"
#include "gs_completion_mailbox.h"
#include "frame_trace.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace cave::ps2 {
namespace {
constexpr size_t VramBytes=4u*1024u*1024u;
enum class Kind { Initialize,Reset,Gif,Register,Image,Fifo,Snapshot,WriteVram,Frame,ConsumerTask,GifBurst };
struct Reply {
    std::atomic<bool> settled=false;
    std::promise<std::vector<uint8_t>> result;
    void Complete(std::vector<uint8_t> bytes) {
        if(!settled.exchange(true))result.set_value(std::move(bytes));
    }
    void Fail(std::exception_ptr error) {
        if(!settled.exchange(true))result.set_exception(error);
    }
};
struct Command {
    Kind kind{};
    GifPathId path{};
    uint8_t address{};
    uint64_t value{};
    size_t offset=0,cost=0,submissions=1;
    uint64_t serial=0;
    uint64_t generation=1;
    uint64_t field=0;
    std::array<uint64_t,4> setup{};
    GsFrame frame{};
    std::vector<uint8_t> bytes;
    std::vector<GifTransferRange> ranges;
    std::shared_ptr<Reply> reply;
    std::function<void()> task;
};
}
class GsCommandQueue::Impl {
public:
    const size_t capacityBytes,capacityCommands;
    mutable std::mutex mutex;
    std::condition_variable readable,writable;
    std::vector<Command> slots;
    size_t readSlot = 0, writeSlot = 0, unread = 0;
    // A burst often exceeds the per-slot allowance. Reuse a few across slots
    // so the producer need not wait for the ring to wrap to reuse storage.
    struct BurstStorage {
        std::vector<uint8_t> bytes;
        std::vector<GifTransferRange> ranges;
    };
    std::array<BurstStorage, 8> freeBursts;
    size_t freeBurstCount = 0;
    size_t burstStorageLimit() const { return std::min<size_t>(32 * 1024, capacityBytes / 16); }
    Stats stats;
    std::exception_ptr failure;
    struct Active {
        const Command& command;
        Active* parent = nullptr;
    };
    Active* active = nullptr;
    std::thread::id consumerThread;
    std::shared_ptr<GsCompletionMailbox> completions;
    std::function<void()> frameNotifier;
    bool frameWaiter=false;
    unsigned waitingWriters=0;
    Impl(size_t bytes,size_t count,std::shared_ptr<GsCompletionMailbox> mailbox)
        :capacityBytes(bytes),capacityCommands(count),completions(mailbox?std::move(mailbox):std::make_shared<GsCompletionMailbox>()) {
        if(!bytes || !count)throw std::invalid_argument("GS queue capacity must be positive");
        slots.resize(count);
    }
    void CheckOpen() const {if(stats.closed)std::rethrow_exception(failure);}
    // Moving a command leaves its serial in the slot for safe buffer recycling.
    void Enqueue(Command&& command, std::span<const uint8_t> payload = {},
                 std::span<const GifTransferRange> ranges = {}) {
        auto& slot = slots[writeSlot];
        command.bytes.swap(slot.bytes);
        command.ranges.swap(slot.ranges);
        if (command.kind == Kind::GifBurst && freeBurstCount &&
            payload.size() > command.bytes.capacity() && payload.size() <= burstStorageLimit()) {
            auto& storage = freeBursts[--freeBurstCount];
            command.bytes.swap(storage.bytes);
            command.ranges.swap(storage.ranges);
        }
        command.bytes.assign(payload.begin(), payload.end());
        command.ranges.assign(ranges.begin(), ranges.end());
        command.serial = stats.submitted + 1;
        slot = std::move(command);
        if (++writeSlot == capacityCommands) writeSlot = 0;
        ++unread;
    }
    // Adjacent asynchronous GIF writes may share transport storage. Keep
    // every range for the consumer, including consecutive uses of one path.
    bool AppendGif(const Command& next, std::span<const uint8_t> payload) {
        if (!unread || next.kind != Kind::Gif || payload.empty() || input_trace::Recording(next.field))
            return false;
        auto& tail = slots[writeSlot ? writeSlot - 1 : capacityCommands - 1];
        if ((tail.kind != Kind::Gif && tail.kind != Kind::GifBurst) || tail.generation != next.generation ||
            tail.field != next.field || tail.bytes.empty() || tail.bytes.size() + payload.size() > 16 * 1024)
            return false;
        const bool first = tail.kind == Kind::Gif;
        const size_t extra = payload.size() + (first ? 2 : 1) * sizeof(GifTransferRange);
        if (extra > capacityBytes - stats.pendingBytes) return false;
        const size_t count = tail.ranges.size() + (first ? 2 : 1);
        if (count > tail.ranges.capacity())
            tail.ranges.reserve(std::max(count, 2 * tail.ranges.capacity()));
        const auto oldSize = tail.bytes.size();
        tail.bytes.insert(tail.bytes.end(), payload.begin(), payload.end());
        if (first) tail.ranges.push_back({tail.path, static_cast<uint32_t>(oldSize)});
        tail.ranges.push_back({next.path, static_cast<uint32_t>(payload.size())});
        tail.kind = Kind::GifBurst;
        tail.cost += extra;
        ++tail.submissions;
        stats.pendingBytes += extra;
        return true;
    }
    void Push(Command command,std::span<const uint8_t> payload={},std::span<const GifTransferRange> ranges={}) {
        command.cost=std::max(command.cost,payload.size());
        if(command.cost>capacityBytes)throw std::length_error("GS command exceeds queue byte capacity");
        const bool frame=command.kind==Kind::Frame;
        std::unique_lock lock(mutex);
        const auto available=[&]{return stats.closed ||
            (stats.commands<capacityCommands && command.cost<=capacityBytes-stats.pendingBytes &&
             (!frame || !stats.frameOutstanding));};
        if(!available()) {
            ++waitingWriters;
            writable.wait(lock,available);
            --waitingWriters;
        }
        CheckOpen();
        command.generation=completions->Generation();
        command.field=input_trace::CurrentField();
        // Allocate only after capacity is reserved; blocked writers do not keep
        // an extra unbounded copy of their guest DMA buffers.
        const bool wakeReader = unread == 0;
        const auto cost = command.cost;
        if (!AppendGif(command, payload)) {
            Enqueue(std::move(command), payload, ranges);
            stats.pendingBytes += cost;
        }
        ++stats.commands;++stats.submitted;
        stats.highWaterBytes=std::max(stats.highWaterBytes,stats.pendingBytes);
        stats.highWaterCommands=std::max(stats.highWaterCommands,stats.commands);
        if(frame)stats.frameOutstanding=true;
        lock.unlock();if(wakeReader)readable.notify_one();
    }
    std::vector<uint8_t> Request(Command command,std::span<const uint8_t> payload={}) {
        command.reply=std::make_shared<Reply>();
        auto future=command.reply->result.get_future();
        Push(std::move(command),payload);
        return future.get();
    }
};
GsCommandQueue::GsCommandQueue(size_t bytes,size_t count,std::shared_ptr<GsCompletionMailbox> mailbox)
    :impl(std::make_unique<Impl>(bytes,count,std::move(mailbox))){}
GsCommandQueue::~GsCommandQueue(){Close();}
void GsCommandQueue::Close(std::exception_ptr error) {
    if(!error)error=std::make_exception_ptr(GsQueueClosed());
    std::lock_guard lock(impl->mutex);
    if(impl->stats.closed)return;
    impl->failure=error;impl->stats.closed=true;
    impl->completions->Close();
    for(auto& command:impl->slots)if(command.reply)command.reply->Fail(error);
    for (auto* active = impl->active; active; active = active->parent)
        if (active->command.reply) active->command.reply->Fail(error);
    impl->slots.clear();
    impl->unread = 0;
    impl->stats.commands=0;impl->stats.pendingBytes=0;impl->stats.frameOutstanding=false;
    impl->readable.notify_all();impl->writable.notify_all();
}
GsCommandQueue::Stats GsCommandQueue::GetStats() const {
    std::lock_guard lock(impl->mutex);return impl->stats;
}
void GsCommandQueue::SetFrameNotifier(std::function<void()> notify) {
    std::lock_guard lock(impl->mutex);impl->frameNotifier=std::move(notify);
}
void GsCommandQueue::RunOnConsumer(std::function<void()> task) {
    Command command;
    command.kind=Kind::ConsumerTask;
    command.task=std::move(task);
    impl->Request(std::move(command));
}
bool GsCommandQueue::ConsumeOne(GsCommandBackend& consumer,std::chrono::milliseconds timeout) {
    Command command;
    size_t slotIndex;
    Impl::Active active{command};
    {
        std::unique_lock lock(impl->mutex);
        auto& owner=impl->consumerThread;
        if(owner==std::thread::id{})owner=std::this_thread::get_id();
        else if(owner!=std::this_thread::get_id())throw std::logic_error("GS queue requires one consumer thread");
        if (!impl->unread && !impl->readable.wait_for(lock, timeout, [&] { return impl->stats.closed || impl->unread; }))
            return false;
        if (impl->stats.closed) return false;
        slotIndex = impl->readSlot;
        command = std::move(impl->slots[slotIndex]);
        if (++impl->readSlot == impl->capacityCommands) impl->readSlot = 0;
        --impl->unread;
        active.parent = impl->active;
        impl->active = &active;
    }
    std::vector<uint8_t> answer;
    const auto tracedStart=input_trace::Recording(command.field)?input_trace::Stamp():0;
    try {
        consumer.BeginCommand(command.generation);
        switch(command.kind) {
        case Kind::Initialize:consumer.Initialize(command.bytes);break;
        case Kind::Reset:consumer.Reset();break;
        case Kind::Gif:consumer.Gif(command.path,command.bytes);break;
        case Kind::GifBurst:consumer.GifBurst(command.ranges,command.bytes);break;
        case Kind::Register:consumer.Register(command.address,command.value);break;
        case Kind::Image:consumer.Image(command.setup,command.bytes);break;
        case Kind::Fifo: {
            answer.resize(command.cost);
            const auto actual=consumer.ReadFifo(answer);
            if(actual>answer.size())throw std::runtime_error("GS FIFO consumer exceeded requested capacity");
            answer.resize(actual);break;
        }
        case Kind::Snapshot:
            consumer.Snapshot(answer);
            if(answer.size()!=VramBytes)throw std::runtime_error("GS snapshot requires complete 4 MiB VRAM");
            break;
        case Kind::WriteVram:consumer.WriteVram(command.offset,command.bytes);break;
        case Kind::Frame:consumer.Frame(command.frame);break;
        case Kind::ConsumerTask:command.task();break;
        }
    }catch(...) {
        auto error=std::current_exception();
        Close(error);
        std::lock_guard lock(impl->mutex);
        impl->active = active.parent;
        throw;
    }
    if(tracedStart)input_trace::Record(input_trace::Kind::GsCommand,
        command.kind==Kind::Frame?command.frame.vsyncTick:command.field,
        uint64_t(command.kind),uint64_t(input_trace::Stamp()-tracedStart),command.cost);
    std::function<void()> frameNotifier;
    bool wakeWriters=false;
    {
        std::lock_guard lock(impl->mutex);
        if(!impl->stats.closed) {
            // Retain small transfer buffers for this slot. A bulk texture or
            // VRAM upload cannot permanently raise the queue's memory budget.
            const auto retained = command.bytes.capacity() + command.ranges.capacity() * sizeof(GifTransferRange);
            if (retained <= impl->capacityBytes / impl->capacityCommands &&
                impl->slots[slotIndex].serial == command.serial) {
                impl->slots[slotIndex].bytes.swap(command.bytes);
                impl->slots[slotIndex].ranges.swap(command.ranges);
            } else if (command.kind == Kind::GifBurst && retained <= impl->burstStorageLimit() &&
                       impl->freeBurstCount < impl->freeBursts.size()) {
                auto& storage = impl->freeBursts[impl->freeBurstCount++];
                storage.bytes.swap(command.bytes);
                storage.ranges.swap(command.ranges);
            }
            impl->stats.pendingBytes-=command.cost;impl->stats.commands-=command.submissions;
            impl->stats.completed+=command.submissions;
            if(command.kind==Kind::Frame)impl->stats.frameOutstanding=false;
            if(command.kind==Kind::Frame || (impl->frameWaiter && !impl->stats.frameOutstanding &&
                                             impl->stats.commands<impl->capacityCommands)) {
                impl->frameWaiter=false;frameNotifier=impl->frameNotifier;
            }
        }
        impl->active = active.parent;
        wakeWriters=impl->waitingWriters!=0;
    }
    if(command.reply)command.reply->Complete(std::move(answer));
    if(wakeWriters)impl->writable.notify_all();
    if(frameNotifier)frameNotifier();
    return true;
}
void GsCommandQueue::Initialize(std::span<const uint8_t> bytes) {
    if(bytes.size()!=VramBytes)throw std::invalid_argument("GS initialization requires 4 MiB VRAM");
    Command command;command.kind=Kind::Initialize;impl->Request(std::move(command),bytes);
}
void GsCommandQueue::Reset(){
    // Release any owner blocked on an old SIGNAL before enqueuing RESET behind
    // that command. Already queued work keeps its original generation.
    impl->completions->Reset();
    Command command;command.kind=Kind::Reset;impl->Request(std::move(command));
}
GsCompletions GsCommandQueue::CollectCompletions(){return impl->completions->Take();}
void GsCommandQueue::AcknowledgeSignal(){impl->completions->AcknowledgeSignal();}
void GsCommandQueue::AcknowledgeStatus(uint32_t mask){impl->completions->AcknowledgeStatus(mask);}
void GsCommandQueue::Gif(GifPathId path,std::span<const uint8_t> bytes) {
    if(unsigned(path)<1 || unsigned(path)>3 || bytes.size()%16)throw std::invalid_argument("Invalid queued GIF transfer");
    Command command;command.kind=Kind::Gif;command.path=path;impl->Push(std::move(command),bytes);
}
void GsCommandQueue::GifBurst(std::span<const GifTransferRange> ranges, std::span<const uint8_t> bytes) {
    size_t remaining = bytes.size();
    for (const auto& range : ranges) {
        if (unsigned(range.path) < 1 || unsigned(range.path) > 3 || !range.size ||
            range.size % 16 || range.size > remaining)
            throw std::invalid_argument("Invalid queued GIF burst");
        remaining -= range.size;
    }
    if (remaining) throw std::invalid_argument("Incomplete GIF burst");
    if (ranges.empty()) return;
    if (bytes.size() > impl->capacityBytes || ranges.size_bytes() > impl->capacityBytes - bytes.size()) {
        GsCommandBackend::GifBurst(ranges, bytes);
        return;
    }
    Command command;
    command.kind = Kind::GifBurst;
    command.cost = bytes.size() + ranges.size_bytes();
    impl->Push(std::move(command), bytes, ranges);
}
void GsCommandQueue::Register(uint8_t address,uint64_t value) {
    if(address>=128)throw std::out_of_range("Invalid queued GS register");
    Command command;command.kind=Kind::Register;command.address=address;command.value=value;impl->Push(std::move(command));
}
void GsCommandQueue::Image(const std::array<uint64_t,4>& setup,std::span<const uint8_t> bytes) {
    Command command;command.kind=Kind::Image;command.setup=setup;impl->Push(std::move(command),bytes);
}
size_t GsCommandQueue::ReadFifo(std::span<uint8_t> bytes) {
    Command command;command.kind=Kind::Fifo;command.cost=bytes.size();
    auto answer=impl->Request(std::move(command));std::copy(answer.begin(),answer.end(),bytes.begin());return answer.size();
}
void GsCommandQueue::Snapshot(std::vector<uint8_t>& bytes) {
    Command command;command.kind=Kind::Snapshot;command.cost=VramBytes;bytes=impl->Request(std::move(command));
}
void GsCommandQueue::WriteVram(size_t offset,std::span<const uint8_t> bytes) {
    if(offset>VramBytes || bytes.size()>VramBytes-offset)throw std::out_of_range("Queued GS VRAM range exceeds 4 MiB");
    Command command;command.kind=Kind::WriteVram;command.offset=offset;impl->Push(std::move(command),bytes);
}
void GsCommandQueue::Frame(const GsFrame& request) {
    if(auto* row=frame_trace::Get(request.vsyncTick))row->enqueued=frame_trace::Stamp();
    Command command;command.kind=Kind::Frame;command.frame=request;impl->Request(std::move(command));
}
bool GsCommandQueue::TryFrame(const GsFrame& request) {
    std::unique_lock lock(impl->mutex);
    impl->CheckOpen();
    if(impl->stats.frameOutstanding || impl->stats.commands==impl->capacityCommands){impl->frameWaiter=true;return false;}
    Command command;command.kind=Kind::Frame;command.frame=request;command.generation=impl->completions->Generation();
    const bool wakeReader = impl->unread == 0;
    impl->Enqueue(std::move(command));
    auto& stats=impl->stats;++stats.commands;++stats.submitted;stats.frameOutstanding=true;
    stats.highWaterCommands=std::max(stats.highWaterCommands,stats.commands);
    if(auto* row=frame_trace::Get(request.vsyncTick)) {
        auto& enqueued=request.Prepared()?row->readyEnqueued:row->enqueued;
        enqueued=frame_trace::Stamp();
    }
    input_trace::Record(request.Prepared()?input_trace::Kind::ReadyQueue:input_trace::Kind::FrameQueue,
                        request.vsyncTick,request.dispfb1,request.dispfb2,request.field);
    lock.unlock();if(wakeReader)impl->readable.notify_one();return true;
}
}
