#include "client.h"
#include "round_delivery.h"
#include "replay.h"
#include "../storage/save_store.h"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

namespace kt::community {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t kPendingBytes = 64 * 1024 * 1024, kPendingCount = 32;
void Require(bool value, const char* error) { if (!value) { throw std::runtime_error(error); } }
Reply Failed(Error error, unsigned windows_error = 0) { Reply result; result.error = error; result.windows_error = windows_error; return result; }
bool Path(std::string_view path) {
    if (path == "/health" || path == "/boards" || path == "/identity" || path == "/scores" || path == "/replays") { return true; }
    if (path.starts_with("/entries/") && path.ends_with("/replay")) {
        return path.size() == 9 + 32 + 7 && Identifier(path.substr(9, 32));
    }
    if (!path.starts_with("/leaderboards?") || path.size() > 256) { return false; }
    return std::all_of(path.begin() + 14, path.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '=' || c == '&' || c == '_' || c == '-' || c == '.';
    });
}
void Metadata(const Json& body) {
    if (body.is_null()) { return; }
    Require(body.is_object() && body.size() <= 12, "Invalid community metadata");
    for (const auto& item : body.items()) {
        Require(item.key().size() <= 64 && item.value().is_primitive(), "Invalid community metadata field");
        if (item.value().is_string()) { Require(item.value().get_ref<const std::string&>().size() <= 4096, "Community metadata field too large"); }
    }
}
std::wstring Header(HINTERNET request, DWORD id, const wchar_t* name = WINHTTP_HEADER_NAME_BY_INDEX) {
    DWORD bytes{};
    if (WinHttpQueryHeaders(request, id, name, nullptr, &bytes, WINHTTP_NO_HEADER_INDEX)) { return {}; }
    if (GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND) { return {}; }
    Require(GetLastError() == ERROR_INSUFFICIENT_BUFFER && bytes <= 4096, "Invalid HTTP response header");
    std::wstring result(bytes / sizeof(wchar_t), L'\0');
    Require(WinHttpQueryHeaders(request, id, name, result.data(), &bytes, WINHTTP_NO_HEADER_INDEX), "Cannot read HTTP response header");
    result.resize(bytes / sizeof(wchar_t));
    while (!result.empty() && result.back() == L'\0') { result.pop_back(); }
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t c) { return c >= L'A' && c <= L'Z' ? c + (L'a' - L'A') : c; });
    return result;
}
std::uint32_t Unsigned(const Json& value, bool nonzero = false) {
    Require(value.is_number_integer(), "Invalid community integer");
    const auto number = value.get<std::int64_t>();
    Require(number >= (nonzero ? 1 : 0) && number <= 0xffffffffll, "Community integer outside bounds");
    return static_cast<std::uint32_t>(number);
}
void Receipt(std::string_view path, const Json& request, const Reply& reply,std::string_view game) {
    Require(reply.body.is_object(), "Invalid community reply");
    if (path == "/scores") {
        const auto& body = reply.body;
        Require(Identifier(body.at("entry_id").get<std::string>()) && body.at("accepted").is_boolean() &&
                body.at("upload_eligible").is_boolean() && Unsigned(body.at("serial_rank"), true), "Invalid score receipt");
        Require(body.at("accepted").get<bool>() || !body.at("upload_eligible").get<bool>(), "Inconsistent score receipt");
        Require(Decimal(body.at("account_id").get<std::string>()) == AccountId(request.at("nickname").get<std::string>(),game), "Score receipt identity mismatch");
    } else if (path == "/replays") {
        Require(reply.body.at("entry_id") == request.at("entry_id") && reply.body.at("sha256") == request.at("sha256") &&
                reply.body.at("format").get<std::string>() == ProtocolFor(game).replay_format && Unsigned(reply.body.at("size"), true) == Unbase64(request.at("payload_base64").get<std::string>(), kReplayLimit).size(), "Replay receipt mismatch");
    }
}
struct Internet {
    HINTERNET value{};
    ~Internet() { if (value) { WinHttpCloseHandle(value); } }
};
class Http {
    const Endpoint endpoint_;
    const TitleProtocol& title_;
    std::atomic<bool>& stopping_;
    std::mutex active_gate_;
    HINTERNET active_{};
public:
    Http(const Config& config, std::atomic<bool>& stopping) : endpoint_(ParseEndpoint(config.endpoint,config.game)), title_(ProtocolFor(config.game)), stopping_(stopping) {}
    void Cancel() {
        std::lock_guard lock(active_gate_);
        if (active_) { WinHttpCloseHandle(active_); active_ = nullptr; }
    }
    Reply Perform(std::string_view path, const Json& body, bool binary = false) {
        Reply reply;
        try {
            Require(Path(path), "Invalid community request path");
            const auto encoded = body.is_null() ? std::string{} : body.dump();
            Require(encoded.size() <= kBodyLimit, "Community request body too large");
            Internet session{WinHttpOpen(L"CavePs4Community/1", endpoint_.secure ? WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY : WINHTTP_ACCESS_TYPE_NO_PROXY,
                                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
            if (!session.value) { return Failed(Error::Network, GetLastError()); }
            if (!WinHttpSetTimeouts(session.value, 2000, 2000, 3000, 3000)) { return Failed(Error::Network, GetLastError()); }
            Internet connection{WinHttpConnect(session.value, endpoint_.host.c_str(), endpoint_.port, 0)};
            if (!connection.value) { return Failed(Error::Network, GetLastError()); }
            std::wstring target = endpoint_.path;
            target.append(path.begin(), path.end());
            const auto request = WinHttpOpenRequest(connection.value, body.is_null() ? L"GET" : L"POST", target.c_str(),
                nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, endpoint_.secure ? WINHTTP_FLAG_SECURE : 0);
            if (!request) { return Failed(Error::Network, GetLastError()); }
            {
                std::lock_guard lock(active_gate_);
                if (stopping_) { WinHttpCloseHandle(request); return Failed(Error::Cancelled); }
                active_ = request;
            }
            const auto release = [this, request](void*) {
                std::lock_guard lock(active_gate_);
                if (active_ == request) { WinHttpCloseHandle(request); active_ = nullptr; }
            };
            std::unique_ptr<void, decltype(release)> guard(request, release);
            DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
            if (!WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)) ||
                !WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled))) { return Failed(Error::Network, GetLastError()); }
            constexpr wchar_t headers[] = L"Content-Type: application/json; charset=utf-8\r\nAccept-Encoding: identity\r\n";
            if (!WinHttpSendRequest(request, headers, DWORD(-1), encoded.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(encoded.data()),
                                    static_cast<DWORD>(encoded.size()), static_cast<DWORD>(encoded.size()), 0) ||
                !WinHttpReceiveResponse(request, nullptr)) { return Failed(stopping_ ? Error::Cancelled : Error::Network, GetLastError()); }
            DWORD status{}, status_bytes = sizeof(status);
            if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_bytes, WINHTTP_NO_HEADER_INDEX)) { return Failed(Error::Network, GetLastError()); }
            reply.status = status;
            const auto content_type = Header(request, WINHTTP_QUERY_CONTENT_TYPE);
            const auto encoding = Header(request, WINHTTP_QUERY_CONTENT_ENCODING);
            Require(encoding.empty() || encoding == L"identity", "Unexpected HTTP content encoding");
            const bool raw = binary && status == 200;
            Require(raw ? content_type == L"application/octet-stream" : content_type == L"application/json" || content_type == L"application/json; charset=utf-8", "Unexpected community response type");
            const auto limit = raw ? kReplayLimit : kJsonReplyLimit;
            const auto length = Header(request, WINHTTP_QUERY_CONTENT_LENGTH);
            std::optional<std::uint64_t> expected;
            if (!length.empty()) {
                Require(std::all_of(length.begin(), length.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }), "Invalid HTTP response length");
                expected = Decimal(std::string(length.begin(), length.end()), limit);
            }
            std::array<std::byte, 65536> buffer{};
            for (;;) {
                DWORD received{};
                if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &received)) { reply.error = stopping_ ? Error::Cancelled : Error::Network; reply.windows_error = GetLastError(); return reply; }
                if (!received) { break; }
                Require(received <= limit - reply.bytes.size(), "Community response body exceeds limit");
                reply.bytes.insert(reply.bytes.end(), buffer.begin(), buffer.begin() + received);
            }
            Require(!expected || *expected == reply.bytes.size(), "Truncated community HTTP response");
            if (raw) {
                const auto digest = Header(request, WINHTTP_QUERY_CUSTOM, L"X-Replay-SHA256");
                const auto format = Header(request, WINHTTP_QUERY_CUSTOM, L"X-Replay-Format");
                const auto actual_digest = Sha256(reply.bytes);
                Require(!reply.bytes.empty() && digest == std::wstring(actual_digest.begin(), actual_digest.end()), "Community replay digest mismatch");
                Require(format == std::wstring(title_.replay_format.begin(),title_.replay_format.end()), "Community replay format mismatch");
            }
            if (!raw) {
                reply.body = ParseJson(std::string_view(reinterpret_cast<const char*>(reply.bytes.data()), reply.bytes.size()));
                Require(reply.body.is_object(), "Invalid community response object");
                reply.bytes.clear();
            }
            if (status != 200) { reply.error = Error::Network; }
            return reply;
        } catch (...) { reply.error = Error::Protocol; return reply; }
    }
};
std::string Slot(std::string_view id) {
    Require(Identifier(id), "Invalid outbox identity");
    constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyz234567";
    unsigned accumulator{}, bits{};
    std::string result = "q";
    for (const auto digit : id) {
        accumulator = (accumulator << 4) | static_cast<unsigned>(digit <= '9' ? digit - '0' : digit - 'a' + 10);
        bits += 4;
        if (bits >= 5) { bits -= 5; result += alphabet[(accumulator >> bits) & 31]; }
    }
    if (bits) { result += alphabet[(accumulator << (5 - bits)) & 31]; }
    return result;
}
struct Pending { std::string id, slot, path; Json body; std::size_t bytes{}; Clock::time_point next{}; unsigned attempts{}; };
class Outbox {
    kt::save::Store store_;
    const std::string endpoint_;
    Json Read(std::string_view name) {
        bool created{};
        auto mount = store_.MountSlot(name, false, false, false, 0, created);
        std::string text;
        {
            auto file = store_.Open(mount, "request.json", {.read=true});
            Require(file->Info().size <= kBodyLimit + 4096, "Outbox record exceeds limit");
            text.resize(static_cast<std::size_t>(file->Info().size));
            Require(file->ReadAt(0, std::as_writable_bytes(std::span(text.data(), text.size()))) == text.size(), "Outbox record truncated");
        }
        store_.Unmount(mount);
        return ParseJson(text, kBodyLimit + 4096);
    }
    void Write(std::string_view name, const Json& value, bool create) {
        const auto text = value.dump();
        Require(text.size() <= kBodyLimit + 4096, "Outbox record exceeds limit");
        bool created{};
        auto mount = store_.MountSlot(name, true, create, create, (kBodyLimit + 65535) / 32768, created);
        {
            auto file = store_.Open(mount, "request.json", {.write=true, .create=true, .truncate=true});
            std::uint64_t end{};
            Require(file->WriteAt(0, std::as_bytes(std::span(text.data(), text.size())), end) == text.size(), "Outbox write incomplete");
            file->Flush();
        }
        store_.Unmount(mount);
    }
public:
    std::vector<Pending> pending;
    Outbox(const std::filesystem::path& root, std::string_view endpoint) : store_([&] {
        kt::save::Store::CreateRoot(root); return root; }()), endpoint_(Sha256(endpoint)) {
        std::size_t total{};
        for (const auto& entry : store_.Search("%", kt::save::SortKey::Name, false)) {
            const auto record = Read(entry.name);
            Require(record.at("format") == 1 && Identifier(record.at("id").get<std::string>()) &&
                    Slot(record.at("id").get<std::string>()) == entry.name, "Invalid outbox record identity");
            if (record.at("state") == "delivered") { store_.Delete(entry.name); continue; }
            Require(record.at("state") == "pending", "Unknown outbox record state");
            const auto path = record.at("path").get<std::string>();
            const auto body = record.at("body");
            const auto bytes = body.dump();
            Require((path == "/scores" || path == "/replays") && body.is_object() &&
                    Sha256(bytes) == record.at("body_sha256").get<std::string>(), "Invalid outbox record body");
            total += bytes.size();
            Require(total <= kPendingBytes && pending.size() < kPendingCount, "Community outbox capacity exceeded");
            // Other endpoints are retained, never sent to the newly configured server.
            const auto next = record.at("endpoint_sha256").get<std::string>() == endpoint_ ? Clock::now() : Clock::time_point::max();
            pending.push_back({record.at("id").get<std::string>(), entry.name, path, body, bytes.size(), next});
        }
    }
    std::string Stage(std::string path, Json body) {
        const auto bytes = body.dump();
        std::size_t total = bytes.size();
        for (const auto& record : pending) { total += record.bytes; }
        Require(pending.size() < kPendingCount && total <= kPendingBytes, "Community outbox is full");
        const auto id = NewId(), slot = Slot(id);
        Write(slot, Json{{"format", 1}, {"id", id}, {"state", "pending"}, {"endpoint_sha256", endpoint_},
                        {"path", path}, {"body", body}, {"body_sha256", Sha256(bytes)}}, true);
        pending.push_back({id, slot, std::move(path), std::move(body), bytes.size(), Clock::now() + std::chrono::seconds(5)});
        return id;
    }
    void Delivered(std::string_view id, const Reply& reply) {
        const auto found = std::find_if(pending.begin(), pending.end(), [&](const auto& record) { return record.id == id; });
        Require(found != pending.end(), "Unknown outbox completion");
        // Shrink acknowledged payloads before the recoverable move to .trash.
        Write(found->slot, Json{{"format", 1}, {"id", found->id}, {"state", "delivered"}, {"receipt", reply.body}}, false);
        store_.Delete(found->slot);
        pending.erase(found);
    }
};
}

struct Client::Impl {
    const Config config;
    const std::filesystem::path directory;
    enum class ReplayCheck { Opaque, Upload, Download };
    struct Job {
        std::string path; Json body; bool durable{}, raw{}; std::vector<std::byte> upload; std::promise<Reply> result;
        ReplayCheck replay_check = ReplayCheck::Opaque;
        std::size_t replay_bytes{};
        std::string replay_digest;
        std::optional<ReplayExpectation> replay_expected;
        std::shared_ptr<RoundWork> round;
        std::optional<RoundSubmission> round_proof;
        std::uint32_t proof_board{};
        std::uint64_t proof_score{};
    };
    mutable std::mutex gate;
    std::mutex stop_gate;
    std::condition_variable wake;
    std::deque<Job> jobs;
    Identity identity;
    Error terminal = Error::None;
    std::atomic<bool> stopping{};
    std::atomic<std::size_t> pending_count{};
    std::atomic<std::size_t> round_count{};
    const bool enable_rounds;
    Http http;
    std::thread worker;
    Impl(Config value, std::filesystem::path root, bool rounds) : config(std::move(value)), directory(std::move(root)), enable_rounds(rounds), http(config, stopping) {
        Require(!enable_rounds || config.game=="ketsui","Whole-round recovery is not implemented for this title");
        Require(!config.enabled || (ValidNicknameBytes(config.nickname) && directory.is_absolute()), "Enabled community requires a nickname and absolute user directory");
        if (config.enabled) { worker = std::thread([this] { Run(); }); }
    }
    Identity Snapshot() const { std::lock_guard lock(gate); return identity; }
    bool Handshake() {
        try {
            const auto health = http.Perform("/health", nullptr);
            Require(bool(health) && health.body.at("protocol").is_number_integer() && health.body.at("protocol") == 1 &&
                    health.body.at("title_id").get<std::string>() == ProtocolFor(config.game).title_id &&
                    health.body.at("game_version").get<std::string>() == ProtocolFor(config.game).version, "Community protocol unavailable");
            const auto response = http.Perform("/identity", Json{{"nickname", config.nickname}});
            Require(bool(response), "Community nickname unavailable");
            const auto name = response.body.at("nickname").get<std::string>();
            const auto account = Decimal(response.body.at("account_id").get<std::string>());
            Require(ValidNicknameBytes(name) && name.size() <= 48 && account == AccountId(name,config.game), "Invalid community identity mapping");
            std::lock_guard lock(gate);
            identity = {true, name, account};
            return true;
        } catch (...) { std::lock_guard lock(gate); identity.ready = false; return false; }
    }
    void MarkOffline() { std::lock_guard lock(gate); identity.ready = false; }
    void Log(std::string_view path, const Reply& reply, Clock::time_point began) {
        const char* operation = path.starts_with("/entries/") ? "download" : path.starts_with("/leaderboards?") ? "leaderboard" :
            path == "/scores" ? "score" : path == "/replays" ? "replay" : "query";
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - began).count();
        std::printf("{\"event\":\"native_community_request\",\"operation\":\"%s\",\"http_status\":%u,\"error\":%u,\"windows_error\":%u,\"duration_us\":%lld}\n",
                    operation, reply.status, static_cast<unsigned>(reply.error), reply.windows_error, static_cast<long long>(us));
    }
    Reply Send(std::string_view path, const Json& body, bool raw = false) {
        const auto began = Clock::now();
        auto reply = http.Perform(path, body, raw);
        if (reply && (path == "/scores" || path == "/replays")) {
            try { Receipt(path, body, reply,config.game); } catch (...) { reply.error = Error::Protocol; }
        }
        if (reply.error == Error::Network && (!reply.status || reply.status >= 500)) { MarkOffline(); }
        Log(path, reply, began);
        return reply;
    }
    bool CheckUpload(Job& job) {
        if (job.replay_check != ReplayCheck::Upload) { return true; }
        bool valid{};
        Json reason = nullptr;
        Json header = nullptr;
        try {
            Require(job.body.is_object() && job.body.size() == 3 &&
                    Identifier(job.body.at("entry_id").get<std::string>()) &&
                    Identifier(job.body.at("submission_id").get<std::string>()) &&
                    ValidNicknameBytes(job.body.at("nickname").get<std::string>()), "Invalid typed replay receipt metadata");
            const auto replay = InspectReplayEnvelope(job.upload,config.game);
            header = {{"bytes",replay.record.size()},{"version",replay.version},{"data_version",replay.data_version},
                {"region",replay.region},{"kind",replay.kind},{"mode",replay.mode},
                {"score",replay.score},{"character",replay.character},{"stage",replay.stage}};
            if (job.replay_expected) { MatchReplayMetadata(replay, *job.replay_expected); }
            job.body["sha256"] = Sha256(job.upload);
            job.body["format"] = ProtocolFor(config.game).replay_format;
            valid = true;
        } catch (const std::exception& error) { reason = std::string(error.what()).substr(0,256); }
          catch (...) { reason = "unknown_validation_error"; }
        const Json diagnostic{{"event","native_community_replay_validation"},{"operation","upload"},
            {"game",config.game},{"passed",valid},{"metadata_required",bool(job.replay_expected)},
            {"payload_bytes",job.upload.size()},{"reason",reason},{"header",header},
            {"expected_board",job.replay_expected ? Json(job.replay_expected->board) : Json(nullptr)},
            {"expected_score",job.replay_expected ? Json(job.replay_expected->score) : Json(nullptr)},
            {"host_thread_id",GetCurrentThreadId()},{"native_playback_validated",false}};
        std::puts(diagnostic.dump().c_str());
        return valid;
    }
    void CheckDownload(const Job& job, Reply& reply) {
        if (job.replay_check != ReplayCheck::Download || !reply) { return; }
        bool valid{};
        const auto payload_bytes = reply.bytes.size();
        Json reason = nullptr;
        Json header = nullptr;
        try {
            Require(reply.bytes.size() == job.replay_bytes && Sha256(reply.bytes) == job.replay_digest,
                    "Downloaded replay differs from the selected immutable entry");
            const auto replay = InspectReplayEnvelope(reply.bytes,config.game);
            header = {{"bytes",replay.record.size()},{"version",replay.version},{"data_version",replay.data_version},
                {"region",replay.region},{"kind",replay.kind},{"mode",replay.mode},
                {"score",replay.score},{"character",replay.character},{"stage",replay.stage}};
            if (job.replay_expected) { MatchReplayMetadata(replay, *job.replay_expected); }
            valid = true;
        } catch (const std::exception& error) { reason = std::string(error.what()).substr(0,256); }
          catch (...) { reason = "unknown_validation_error"; }
        if (!valid) { reply.error = Error::Protocol; reply.bytes.clear(); reply.body = nullptr; }
        const Json diagnostic{{"event","native_community_replay_validation"},{"operation","download"},
            {"game",config.game},{"passed",valid},{"metadata_required",bool(job.replay_expected)},
            {"payload_bytes",payload_bytes},{"reason",reason},{"header",header},
            {"expected_board",job.replay_expected ? Json(job.replay_expected->board) : Json(nullptr)},
            {"expected_score",job.replay_expected ? Json(job.replay_expected->score) : Json(nullptr)},
            {"host_thread_id",GetCurrentThreadId()},{"native_playback_validated",false}};
        std::puts(diagnostic.dump().c_str());
    }
    void Run() noexcept {
        std::unique_ptr<RoundDelivery> rounds;
        try {
            Outbox outbox(directory / "community-outbox", config.endpoint);
            pending_count = outbox.pending.size();
            if (enable_rounds) rounds = std::make_unique<RoundDelivery>(config,directory/"community-rounds",round_count);
            auto health_due = Clock::now();
            bool round_turn=true;
            for (;;) {
                if (stopping) { return; }
                if (Clock::now() >= health_due) {
                    const auto ready = Handshake();
                    health_due = Clock::now() + std::chrono::seconds(ready ? 60 : 5);
                    std::printf("{\"event\":\"native_community_session\",\"ready\":%s,\"sony_psn\":false}\n", ready ? "true" : "false");
                }
                std::optional<Job> job;
                {
                    std::unique_lock lock(gate);
                    if (jobs.empty()) { wake.wait_for(lock, std::chrono::milliseconds(250), [&] { return stopping || !jobs.empty(); }); }
                    if (stopping) { return; }
                    if (!jobs.empty()) { job.emplace(std::move(jobs.front())); jobs.pop_front(); }
                }
                if (job && job->round) {
                    if (stopping) job->round->Reject(Error::Cancelled);
                    else rounds->Stage(job->round,Snapshot().ready);
                } else if (job && job->round_proof) {
                    Reply reply;
                    try {
                        const auto& accepted=job->round_proof->accepted.get();
                        const auto& attempted=job->round_proof->first_attempt.get();
                        Require(accepted.durable && accepted.error==Error::None && accepted.reference==attempted.reference,"Invalid round proof reference");
                        const auto replay=InspectReplayEnvelope(job->upload);
                        MatchReplayMetadata(replay,{job->proof_board,job->proof_score});
                        Require(Sha256(replay.record)==accepted.record_sha256,"Original replay differs from captured round");
                        const auto board=std::find_if(attempted.boards.begin(),attempted.boards.end(),[&](const auto& b) { return b.board==job->proof_board; });
                        Require(board!=attempted.boards.end(),"Unknown round proof board");
                        if (!board->replay) reply=Failed(attempted.error==Error::None ? Error::Protocol : attempted.error);
                        else {
                            reply=*board->replay;
                            if (reply) Require(board->score && bool(*board->score) && reply.body.at("entry_id")==board->score->body.at("entry_id") &&
                                reply.body.at("sha256")==accepted.attachment_sha256 && Unsigned(reply.body.at("size"),true)==accepted.attachment_bytes &&
                                reply.body.at("format").get<std::string>()==kReplayFormat,"Invalid round proof receipt");
                        }
                    } catch (...) { reply=Failed(Error::Protocol); }
                    std::printf("{\"event\":\"native_community_round_replay_proof\",\"board\":%u,\"error\":%u,\"host_thread_id\":%lu}\n",
                                job->proof_board,static_cast<unsigned>(reply.error),GetCurrentThreadId());
                    job->result.set_value(std::move(reply));
                } else if (job) {
                    Reply reply;
                    std::string staged;
                    try {
                        const auto current = Snapshot();
                        if (!current.account) { reply = Failed(Error::Network); }
                        else if (!CheckUpload(*job)) { reply = Failed(Error::Protocol); }
                        else {
                            if (job->body.is_object() && job->body.contains("nickname")) {
                                const auto requested_name = job->body.at("nickname").get<std::string>();
                                Require(requested_name == config.nickname || requested_name == current.nickname, "Request uses a different nickname");
                                job->body["nickname"] = current.nickname;
                            }
                            if (job->path == "/replays" && !job->upload.empty()) { job->body["payload_base64"] = Base64(job->upload); }
                            if (job->durable) { staged = outbox.Stage(job->path, job->body); }
                            pending_count = outbox.pending.size();
                            reply = Send(job->path, job->body, job->raw);
                            CheckDownload(*job, reply);
                            if (reply && !staged.empty()) {
                                try { outbox.Delivered(staged, reply); }
                                catch (...) { std::puts("{\"event\":\"native_community_archive_deferred\"}"); }
                                pending_count = outbox.pending.size();
                            }
                        }
                    } catch (...) { reply = Failed(Error::Storage); }
                    job->result.set_value(std::move(reply));
                }
                if (stopping) return;
                const auto advance_round=[&] { return rounds && rounds->Tick(Snapshot().ready,[&](std::string_view path,const Json& body) { return Send(path,body); }); };
                bool maintenance=round_turn && advance_round();
                if (!maintenance && Snapshot().ready) {
                    for (auto& pending : outbox.pending) {
                        if (stopping || pending.next > Clock::now()) { continue; }
                        const auto id = pending.id;
                        const auto reply = Send(pending.path, pending.body);
                        if (reply) {
                            try { outbox.Delivered(id, reply); } catch (...) { pending.next = Clock::now() + std::chrono::seconds(60); }
                        } else {
                            pending.attempts = (std::min)(pending.attempts + 1, 4u);
                            pending.next = Clock::now() + std::chrono::seconds(5u << pending.attempts);
                        }
                        pending_count = outbox.pending.size();
                        maintenance=true;
                        break; // One retry per iteration; fresh game work is not starved.
                    }
                }
                if (!round_turn && !maintenance) advance_round();
                round_turn=!round_turn;
                if (!Snapshot().ready) { health_due = (std::min)(health_due, Clock::now() + std::chrono::seconds(5)); }
            }
        } catch (...) {
            if (rounds) rounds->Stop(Error::Storage);
            std::lock_guard lock(gate);
            terminal = Error::Storage;
            identity.ready = false;
            for (auto& job : jobs) { Reject(job,terminal); }
            jobs.clear();
            std::puts("{\"event\":\"native_community_worker_unavailable\",\"reason\":\"storage_or_internal_error\"}");
        }
    }
    static void Reject(Job& job,Error error) {
        if (job.round) job.round->Reject(error);
        else job.result.set_value(Failed(error));
    }
    RoundSubmission SubmitRound(RoundInput input) {
        auto work=std::make_shared<RoundWork>();
        RoundSubmission result{work->accepted.get_future().share(),work->first.get_future().share()};
        const auto invalid=input.boards.empty() || input.boards.size()>18 || input.record.size()!=kReplayRecordSize ||
                           input.game_info.size()>189 || input.build.empty() || input.build.size()>80;
        if (invalid) { work->Reject(Error::Protocol); return result; }
        work->input=std::move(input);
        {
            std::lock_guard lock(gate);
            const auto error=!config.enabled || !enable_rounds ? Error::Disabled : stopping ? Error::Cancelled :
                terminal!=Error::None ? terminal : jobs.size()>=16 ? Error::QueueFull : Error::None;
            if (error!=Error::None) { work->Reject(error); return result; }
            Job job; job.round=std::move(work); jobs.push_back(std::move(job));
        }
        wake.notify_one(); return result;
    }
    std::future<Reply> Queue(Job job) {
        auto future = job.result.get_future();
        try {
            Require(Path(job.path), "Invalid community path");
            Metadata(job.body);
            if (job.round_proof) {
                Require(enable_rounds && job.round_proof->accepted.valid() && job.round_proof->first_attempt.valid() &&
                    job.round_proof->accepted.wait_for(std::chrono::seconds(0))==std::future_status::ready &&
                    job.round_proof->first_attempt.wait_for(std::chrono::seconds(0))==std::future_status::ready &&
                    !job.upload.empty(),"Round replay proof requires a completed first attempt");
            }
            Require(!job.durable || ((job.path == "/scores" || job.path == "/replays") && job.body.is_object()), "Invalid durable community operation");
            Require(job.upload.size() <= kReplayLimit, "Community replay too large");
            if (job.replay_check == ReplayCheck::Upload) {
                Require(job.path == "/replays" && job.durable && !job.upload.empty(), "Invalid typed replay upload");
            } else if (job.replay_check == ReplayCheck::Download) {
                Require(job.body.is_null() && job.replay_bytes && job.replay_bytes <= kReplayLimit && job.replay_digest.size() == 64 &&
                    std::all_of(job.replay_digest.begin(), job.replay_digest.end(), [](char c) {
                        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                    }), "Invalid typed replay download expectation");
            }
            job.raw = job.path.starts_with("/entries/");
        } catch (...) { job.result.set_value(Failed(Error::Protocol)); return future; }
        {
            std::lock_guard lock(gate);
            const auto error = !config.enabled ? Error::Disabled : stopping ? Error::Cancelled : terminal != Error::None ? terminal :
                jobs.size() >= 16 ? Error::QueueFull : Error::None;
            if (error != Error::None) { job.result.set_value(Failed(error)); return future; }
            jobs.push_back(std::move(job));
        }
        wake.notify_one();
        return future;
    }
    void Stop() {
        std::lock_guard stop_lock(stop_gate);
        stopping = true;
        http.Cancel();
        {
            std::lock_guard lock(gate);
            identity.ready = false;
            for (auto& job : jobs) { Reject(job,Error::Cancelled); }
            jobs.clear();
        }
        wake.notify_all();
        if (worker.joinable()) { worker.join(); }
    }
};
Client::Client(Config config, std::filesystem::path directory, bool rounds) : impl_(std::make_unique<Impl>(std::move(config), std::move(directory), rounds)) {}
Client::~Client() { Stop(); }
Identity Client::State() const { return impl_->Snapshot(); }
std::string_view Client::Game() const { return impl_->config.game; }
std::size_t Client::PendingCount() const { return impl_->pending_count.load(); }
std::size_t Client::PendingRoundCount() const { return impl_->round_count.load(); }
RoundSubmission Client::SubmitRound(RoundInput input) { return impl_->SubmitRound(std::move(input)); }
std::future<Reply> Client::ProveRoundReplay(RoundSubmission submission,std::uint32_t board,std::uint64_t score,std::vector<std::byte> original) {
    Impl::Job job; job.path="/replays"; job.round_proof=std::move(submission); job.proof_board=board; job.proof_score=score; job.upload=std::move(original);
    return impl_->Queue(std::move(job));
}
std::future<Reply> Client::Request(std::string path, Json body, bool durable) {
    Impl::Job job; job.path = std::move(path); job.body = std::move(body); job.durable = durable;
    return impl_->Queue(std::move(job));
}
std::future<Reply> Client::Upload(Json metadata, std::vector<std::byte> payload) {
    Impl::Job job; job.path = "/replays"; job.body = std::move(metadata); job.upload = std::move(payload); job.durable = true;
    return impl_->Queue(std::move(job));
}
std::future<Reply> Client::UploadReplayEnvelope(Json metadata, std::vector<std::byte> payload, std::optional<ReplayExpectation> expected) {
    Impl::Job job; job.path = "/replays"; job.body = std::move(metadata); job.upload = std::move(payload); job.durable = true;
    job.replay_check = Impl::ReplayCheck::Upload;
    job.replay_expected = expected;
    return impl_->Queue(std::move(job));
}
std::future<Reply> Client::DownloadReplayEnvelope(std::string entry, std::size_t bytes, std::string sha256, std::optional<ReplayExpectation> expected) {
    Impl::Job job; job.path = "/entries/" + entry + "/replay"; job.body = nullptr;
    job.replay_check = Impl::ReplayCheck::Download; job.replay_bytes = bytes; job.replay_digest = std::move(sha256);
    job.replay_expected = expected;
    return impl_->Queue(std::move(job));
}
void Client::Stop() { impl_->Stop(); }

bool OutboxSelfTest(const std::filesystem::path& directory) {
    const auto check = [](bool value, const char* name) {
        std::printf("{\"event\":\"community_client_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false");
        return value;
    };
    bool okay = true;
    const auto root = directory / "outbox-fixture";
    const auto other = directory / "outbox-other-endpoint";
    const Json body{{"submission_id", NewId()}, {"nickname", "Authored"}, {"score", "9007199254740993"}};
    std::string id;
    {
        Outbox box(root, "http://127.0.0.1:1/ketsui/api/v1");
        id = box.Stage("/scores", body);
        okay &= check(box.pending.size() == 1 && box.pending[0].body == body, "outbox_commit_preserves_body_before_any_transport");
    }
    {
        Outbox box(root, "http://127.0.0.1:1/ketsui/api/v1");
        okay &= check(box.pending.size() == 1 && box.pending[0].id == id && box.pending[0].body == body,
                      "outbox_reopen_keeps_same_submission_reference_and_64bit_text");
        Reply acknowledgement; acknowledgement.status = 200; acknowledgement.body = Json{{"entry_id", NewId()}};
        box.Delivered(id, acknowledgement);
        okay &= check(box.pending.empty(), "acknowledged_payload_is_shrunk_then_recoverably_retired");
    }
    {
        Outbox box(root, "http://127.0.0.1:1/ketsui/api/v1");
        okay &= check(box.pending.empty() && std::filesystem::is_directory(root / ".trash"), "retired_receipt_not_retransmitted_on_reopen");
    }
    {
        Outbox box(other, "http://127.0.0.1:1/ketsui/api/v1");
        (void)box.Stage("/scores", body);
    }
    {
        Outbox box(other, "http://127.0.0.1:2/ketsui/api/v1");
        okay &= check(box.pending.size() == 1 && box.pending[0].next == Clock::time_point::max(),
                      "changed_server_retains_but_does_not_transmit_old_pending_data");
    }
    const auto corrupt_root = directory / "outbox-corrupt-fixture";
    std::string corrupt_slot;
    {
        Outbox box(corrupt_root, "http://127.0.0.1:1/ketsui/api/v1");
        corrupt_slot = Slot(box.Stage("/scores", body));
    }
    {
        // Corrupt only this authored fixture through the same transaction API.
        kt::save::Store store(corrupt_root);
        bool created{};
        auto mount = store.MountSlot(corrupt_slot, true, false, false, 0, created);
        Json record;
        {
            auto file = store.Open(mount, "request.json", {.read=true});
            std::string text(static_cast<std::size_t>(file->Info().size), '\0');
            Require(file->ReadAt(0, std::as_writable_bytes(std::span(text.data(), text.size()))) == text.size(), "Authored corruption read failed");
            record = ParseJson(text);
        }
        record["body"]["score"] = "1"; // Leave its old digest deliberately.
        const auto text = record.dump();
        {
            auto file = store.Open(mount, "request.json", {.write=true, .truncate=true});
            std::uint64_t end{};
            Require(file->WriteAt(0, std::as_bytes(std::span(text.data(), text.size())), end) == text.size(), "Authored corruption write failed");
            file->Flush();
        }
        store.Unmount(mount);
    }
    bool rejected{};
    try { Outbox bad(corrupt_root, "http://127.0.0.1:1/ketsui/api/v1"); }
    catch (...) { rejected = true; }
    okay &= check(rejected && std::filesystem::is_regular_file(corrupt_root / corrupt_slot / "request.json"),
                  "corrupt_pending_digest_is_refused_without_removing_evidence");
    return okay;
}
}
