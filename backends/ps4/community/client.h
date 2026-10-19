#pragma once
#include "protocol.h"
#include "replay.h"
#include "round_journal.h"
#include <future>
#include <memory>
#include <utility>

namespace kt::community {
enum class Error : unsigned { None, Disabled, Cancelled, QueueFull, Network, Protocol, Storage };
struct Reply {
    unsigned status{}, windows_error{};
    Error error = Error::None;
    Json body;
    std::vector<std::byte> bytes;
    explicit operator bool() const noexcept { return status == 200 && error == Error::None; }
};
struct Identity {
    bool ready{};
    std::string nickname;
    std::uint64_t account{};
};
struct RoundAcceptance {
    RoundAcceptance() = default;
    RoundAcceptance(Error value,bool committed,std::string id) : error(value),durable(committed),reference(std::move(id)) {}
    Error error = Error::None;
    bool durable{};
    std::string reference;
    std::vector<std::pair<std::uint32_t,std::string>> submissions;
    std::size_t attachment_bytes{};
    std::string record_sha256, attachment_sha256;
};
struct RoundBoardAttempt {
    std::uint32_t board{};
    // Empty means no deliverable response, never a fabricated HTTP success.
    std::optional<Reply> score, replay;
};
struct RoundAttempt {
    Error error = Error::None;
    bool complete{};
    std::string reference;
    std::vector<RoundBoardAttempt> boards;
    // Set only after this round's configured nickname has a validated server
    // identity. Offline acceptance does not manufacture an account.
    std::optional<Identity> identity;
};
struct RoundSubmission {
    std::shared_future<RoundAcceptance> accepted;
    std::shared_future<RoundAttempt> first_attempt;
};
// Owns one worker. Transport, serialization, outbox recovery and disk writes
// stay on that worker; callers only enqueue copied metadata/owned replay bytes.
// This class never retains or writes guest pointers.
class Client {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    Client(Config config, std::filesystem::path user_directory, bool enable_rounds = false);
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Identity State() const;
    std::size_t PendingCount() const;
    std::size_t PendingRoundCount() const;
    // Explicit producer opt-in. Acceptance only confirms the local commit;
    // first_attempt contains actual HTTP results or explicit missing results.
    // A failed first attempt leaves the round durable for background recovery.
    RoundSubmission SubmitRound(RoundInput input);
    // Native completion adapter only: validate the original caller's owned
    // envelope on the worker, then return this round's already received reply.
    // No HTTP or second upload; first_attempt must already be ready.
    std::future<Reply> ProveRoundReplay(RoundSubmission submission, std::uint32_t board,
                                      std::uint64_t score, std::vector<std::byte> original);
    std::future<Reply> Request(std::string path, Json metadata = nullptr, bool durable = false);
    std::future<Reply> Upload(Json metadata, std::vector<std::byte> payload);
    // Typed envelope work stays on the same bounded worker. These methods do
    // not claim core state compatibility or hand data to a native player.
    // Metadata contains exactly entry_id, submission_id and nickname. The
    // worker supplies format and sha256 after checking the owned bytes.
    std::future<Reply> UploadReplayEnvelope(Json metadata, std::vector<std::byte> payload, std::optional<ReplayExpectation> expected = {});
    std::future<Reply> DownloadReplayEnvelope(std::string entry, std::size_t bytes, std::string sha256,
                                               std::optional<ReplayExpectation> expected = {});
    void Stop();
};
bool ClientSelfTest(const std::filesystem::path& directory);
bool ClientSmoke(const std::filesystem::path& config, const std::filesystem::path& directory);
bool ClientDrainSmoke(const std::filesystem::path& config, const std::filesystem::path& directory);
bool ClientCancelSmoke(const std::filesystem::path& config, const std::filesystem::path& directory);
bool ClientReplayEnvelopeSmoke(const std::filesystem::path& config, const std::filesystem::path& directory,
                               const std::filesystem::path& payload);
bool ClientRoundSmoke(const std::filesystem::path& config, const std::filesystem::path& directory, std::string_view phase);
}
