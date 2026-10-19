#pragma once
#include "mf_reader.h"
#include <array>
#include <functional>

namespace kt::media {
enum class PlayState { Ready, Playing, Paused, Stopped, Ended, Failed };
enum class PlayTransition { None, Ended, Looped };
struct Delivery {
    std::shared_ptr<const Frame> frame;
    std::uint64_t token{};
    bool more_due{};
};
// Decoder, pending samples and clock are accessed only on the MF MTA worker.
// Requests retain immutable packets until the guest buffer copy is committed.
class PlaybackSession {
public:
    using NowFunction = std::function<std::int64_t()>;
    explicit PlaybackSession(std::shared_ptr<content::ReadOnlyFile> source, NowFunction now = {});
    const std::vector<Stream>& Streams() const { return reader_.Streams(); }
    std::uint64_t Duration() const { return reader_.Duration(); }
    bool Enable(unsigned index);
    bool Start();
    bool Pause();
    bool Resume();
    bool Stop();
    void SetLooping(bool value) { looping_ = value; }
    PlayState State() const { return state_; }
    std::int64_t Position() const;
    Delivery Request(Kind kind);
    bool Commit(Kind kind, std::uint64_t token);
    PlayTransition Poll();
    bool Drained() const;
private:
    struct Slot {
        unsigned index = UINT32_MAX;
        bool enabled{}, eof{};
        std::shared_ptr<const Frame> candidate, future;
        std::uint64_t token{};
        std::int64_t end{};
    };
    void Reset();
    std::shared_ptr<const Frame> Decode(Slot& slot);
    Reader reader_;
    NowFunction now_;
    std::array<Slot, 2> slots_{};
    PlayState state_ = PlayState::Ready;
    std::int64_t started_{}, offset_{};
    std::uint64_t next_token_ = 1;
    bool looping_{};
};
bool PlaybackSessionSelfTest(std::shared_ptr<content::ReadOnlyFile> source);
}
