#pragma once
#include "replay.h"
#include <array>
#include <functional>
#include <memory>
#include <stdexcept>

namespace kt::community {
struct RoundInputFailure : std::runtime_error { using std::runtime_error::runtime_error; };
struct RoundInput {
    std::vector<std::uint32_t> boards;
    std::uint64_t score{};
    std::array<std::byte, 64> comment{};
    std::vector<std::byte> game_info, record;
    std::string build;
};
struct JournalRound {
    Json metadata;
    std::vector<std::byte> record, attachment;
};
// Worker-owned durable producer. Stage commits the entire round before any
// request is exposed. No network calls or guest pointers; not yet a game hook.
// A write failure makes this instance unusable until transaction recovery in a
// new instance. Callers must not send using a pre-failure in-memory snapshot.
class RoundJournal {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    static constexpr std::size_t kMaxRounds = 16;
    explicit RoundJournal(const std::filesystem::path& root,
                          std::function<void(std::string_view)> checkpoint = {});
    ~RoundJournal();
    RoundJournal(const RoundJournal&) = delete;
    RoundJournal& operator=(const RoundJournal&) = delete;
    std::string Stage(const Config& config, RoundInput input);
    std::vector<JournalRound> Pending() const;
    JournalRound Get(std::string_view round) const;
    bool Healthy() const noexcept;
    bool MatchesEndpoint(std::string_view round, std::string_view endpoint) const;
    // The worker must obtain this identity for the persisted configured name,
    // not for a subsequently edited config. It can be bound only once.
    void BindIdentity(std::string_view round, std::string nickname, std::uint64_t account);
    Json ScoreRequest(std::string_view round, std::uint32_t board) const;
    Json ReplayRequest(std::string_view round, std::uint32_t board) const;
    void ScoreDelivered(std::string_view round, std::uint32_t board, const Json& request, const Json& receipt);
    void ReplayDelivered(std::string_view round, std::uint32_t board, const Json& request, const Json& receipt);
    bool Complete(std::string_view round) const;
    void Archive(std::string_view round);
};
bool RoundJournalSelfTest(const std::filesystem::path& directory);
bool RoundJournalFixture(const std::filesystem::path& directory, std::string_view phase);
bool RoundJournalSmoke(const std::filesystem::path& config, const std::filesystem::path& directory, std::string_view phase);
}
