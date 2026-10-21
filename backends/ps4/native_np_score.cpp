#include "native_np_score.h"
#include "native_memory.h"
#include "native_user.h"
#include "native_community_rounds.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

#define KT_SYSV __attribute__((sysv_abi))
namespace {
using namespace kt::community;
constexpr int Code(unsigned value) { return static_cast<int>(value); }
constexpr int kInit = Code(0x80550702), kMemory = Code(0x80550703), kArgument = Code(0x80550704);
constexpr int kLogin = Code(0x80550705), kLimit = Code(0x80550706), kAbort = Code(0x80550707);
constexpr int kResponse = Code(0x80550708), kServer = Code(0x8055070a), kId = Code(0x8055070e);
constexpr int kBusy = Code(0x80550717), kNotBest = Code(0x80550815);
constexpr int kRankingNotFound = Code(0x80550812);
constexpr int kNoData = Code(0x8055080e), kTooLarge = Code(0x8055081a);
constexpr std::uint64_t kUnixTick = 62135596800000000ull, kLastUnixUs = 253402300799999999ull;
struct OnlineId { char text[16], term; unsigned char padding[3]; };
struct Rank {
    OnlineId online;
    unsigned char reserved0[16], reserved[49], padding0[3];
    std::int32_t pc{};
    std::uint32_t serial{}, rank{}, highest{};
    std::int32_t has_data{};
    unsigned char padding1[4];
    std::int64_t score{};
    std::uint64_t date{}, account{};
    unsigned char padding2[8];
};
struct PlayerRank { std::int32_t present{}; unsigned char padding[4]; Rank value; };
struct Comment { char bytes[64]; };
struct GameInfo { std::uint64_t size{}; std::byte bytes[189], padding[3]; };
static_assert(sizeof(OnlineId) == 20 && sizeof(Rank) == 144 && sizeof(PlayerRank) == 152);
static_assert(offsetof(Rank, pc) == 88 && offsetof(Rank, score) == 112 && offsetof(Rank, account) == 128);
static_assert(sizeof(Comment) == 64 && sizeof(GameInfo) == 200 && offsetof(PlayerRank, value) == 8);
void Require(bool value) { if (!value) { throw std::runtime_error("Invalid native community response"); } }
bool Read(const void* source, void* target, std::size_t bytes) {
    SIZE_T copied{};
    return source && ReadProcessMemory(GetCurrentProcess(), source, target, bytes, &copied) && copied == bytes;
}
bool Writable(const void* target, std::size_t bytes) { return target && NativeMemoryWritable(target, bytes); }
bool BoardFor(std::string_view game,unsigned id) {
    if (game=="ketsui") return id == 99 || (id <= 52 && id % 10 <= 2);
    if (game=="daioujou") return id==99 || (id<=66 && id%10<=6);
    return false;
}
unsigned U32(const Json& value, bool positive = false) {
    Require(value.is_number_integer());
    const auto number = value.get<std::int64_t>();
    Require(number >= (positive ? 1 : 0) && number <= 0xffffffffll);
    return static_cast<unsigned>(number);
}
std::uint64_t Tick(const Json& value) { return kUnixTick + Decimal(value.get<std::string>(), kLastUnixUs); }
struct Output { void* address{}; std::size_t size{}; };
struct Write { Output target; std::vector<std::byte> data; };
template<class T> std::vector<std::byte> Bytes(const T& value) {
    const auto view = std::as_bytes(std::span(&value, 1)); return {view.begin(), view.end()};
}
bool Distinct(std::span<const Output> outputs) {
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        const auto a = reinterpret_cast<std::uintptr_t>(outputs[i].address);
        if (!a || outputs[i].size > (std::numeric_limits<std::uintptr_t>::max)() - a) { return false; }
        for (std::size_t j = 0; j < i; ++j) {
            const auto b = reinterpret_cast<std::uintptr_t>(outputs[j].address);
            if (a < b + outputs[j].size && b < a + outputs[i].size) { return false; }
        }
    }
    return true;
}
bool Commit(std::span<const Write> writes) {
    std::vector<Output> outputs;
    for (const auto& write : writes) { outputs.push_back(write.target); }
    if (!Distinct(outputs)) { return false; }
    std::vector<NativeHostWrite> leases;
    for (const auto& write : writes) {
        if (write.data.size() != write.target.size) { return false; }
        auto lease = BeginNativeHostWrite(write.target.address, write.target.size);
        if (!lease) { return false; }
        leases.push_back(std::move(lease));
    }
    // All destinations are held and checked before the first byte is copied.
    for (const auto& write : writes) { std::memcpy(write.target.address, write.data.data(), write.data.size()); }
    return true;
}
enum class Kind { Idle, Score, Range, Account, Upload, Download };
const char* KindName(Kind kind) {
    switch (kind) {
    case Kind::Idle: return "idle";
    case Kind::Score: return "score";
    case Kind::Range: return "range";
    case Kind::Account: return "account";
    case Kind::Upload: return "upload";
    case Kind::Download: return "download";
    }
    return "unknown";
}
struct Query {
    Kind kind = Kind::Idle;
    unsigned board{}, start{}, count{};
    std::uint64_t account{};
    Output ranks, comments, info, date, total, temporary_rank;
};
struct Receipt {
    unsigned board{}; std::int64_t score{}; std::string submission, entry, nickname; bool eligible{};
    NativeCommunityRoundHandle round;
};
struct Selection {
    unsigned board{}; std::uint64_t account{}; std::string entry, digest; std::size_t size{};
    std::uint64_t score{};
};
struct Download {
    Selection selected;
    std::vector<std::byte> payload;
    std::size_t cursor{}, advance{};
};
struct Request {
    int id{}, context{};
    bool complete{}, delivered{}, aborted{};
    int result{};
    Query query;
    std::future<Reply> future;
    std::vector<Write> writes;
    std::string submission;
    std::int64_t submitted_score{};
    std::string submitted_nickname;
    std::uint64_t submitted_account{};
    std::optional<Receipt> receipt;
    std::vector<Selection> selections;
    std::optional<Download> download;
    std::string upload_entry;
    std::size_t upload_size{};
    NativeCommunityRoundHandle round;
};
using State = std::function<Identity()>;
using Send = std::function<std::future<Reply>(std::string, Json, bool)>;
using Upload = std::function<std::future<Reply>(Json, std::vector<std::byte>, ReplayExpectation)>;
using Fetch = std::function<std::future<Reply>(std::string, std::size_t, std::string, ReplayExpectation)>;
class Bridge {
    struct Context { int id{}; std::vector<Receipt> receipts; std::vector<Selection> selections; };
    std::mutex gate_;
    State state_;
    Send send_;
    Upload upload_;
    Fetch fetch_;
    std::string game_{"ketsui"};
    bool Board(unsigned id) const {return BoardFor(game_,id);}
    std::array<Context, 32> contexts_{};
    std::array<Request, 32> requests_{};
    int next_context_ = 0x3a000001, next_request_ = 0x3b000001;
    Context* ContextFor(int id) {
        for (auto& entry : contexts_) { if (entry.id && entry.id == id) { return &entry; } } return nullptr;
    }
    Request* RequestFor(int id) {
        for (auto& entry : requests_) { if (entry.id && entry.id == id) { return &entry; } } return nullptr;
    }
    int Ready(Request* request, bool online = true) {
        if (!request) { return kId; }
        if (request->aborted) { return kAbort; }
        if (request->query.kind != Kind::Idle && !request->delivered) { return kBusy; }
        return !online || (state_ && state_().ready) ? 0 : kLogin;
    }
    int Start(Request& request, Query query, std::future<Reply> future) {
        request.query = query; request.complete = request.delivered = false; request.result = 0;
        request.writes.clear(); request.future = std::move(future);
        request.receipt.reset(); request.selections.clear(); request.download.reset();
        request.upload_entry.clear(); request.upload_size = 0;
        request.round.reset();
        std::printf("{\"event\":\"native_score_operation\",\"phase\":\"started\",\"request\":%d,\"kind\":\"%s\",\"board\":%u,\"count\":%u}\n",
                    request.id, KindName(query.kind), query.board, query.count);
        return 0;
    }
    int Start(Request& request, Query query, std::string path, Json body = nullptr, bool durable = false) {
        return Start(request, query, send_(std::move(path), std::move(body), durable));
    }
    std::vector<Write> Ranking(const Reply& reply, const Query& query, int& result, std::vector<Selection>& selections) {
        const auto& body = reply.body;
        Require(body.at("board") == query.board && body.at("rules") == "standard" && U32(body.at("start"), true) == query.start);
        const auto total = U32(body.at("total"));
        const auto date = Tick(body.at("sorted_us"));
        const auto& entries = body.at("entries");
        Require(entries.is_array() && entries.size() <= query.count && entries.size() <= total);
        std::vector<std::byte> ranks(query.ranks.size), comments(query.count * sizeof(Comment)), infos(query.count * sizeof(GameInfo));
        unsigned previous{};
        std::vector<std::uint64_t> accounts;
        for (std::size_t index = 0; index < entries.size(); ++index) {
            const auto& item = entries[index];
            Rank rank{};
            const auto nickname = item.at("nickname").get<std::string>();
            Require(ValidNicknameBytes(nickname) && nickname.size() <= 48 && Identifier(item.at("entry_id").get<std::string>()));
            const auto shortened = ShortOnlineId(nickname);
            std::memcpy(rank.online.text, shortened.data(), shortened.size());
            rank.account = Decimal(item.at("account_id").get<std::string>());
            Require(rank.account == AccountId(nickname,game_) && std::find(accounts.begin(), accounts.end(), rank.account) == accounts.end());
            accounts.push_back(rank.account);
            rank.serial = U32(item.at("serial_rank"), true); rank.rank = U32(item.at("rank"), true); rank.highest = rank.rank;
            Require(rank.serial <= total && rank.rank <= rank.serial);
            if (query.kind == Kind::Range) { Require(rank.serial == query.start + index && (!index || rank.serial > previous)); }
            else { Require(rank.account == query.account); }
            previous = rank.serial;
            rank.score = static_cast<std::int64_t>(Decimal(item.at("score").get<std::string>())); rank.date = Tick(item.at("created_us"));
            Require(rank.date <= date);
            const auto& replay = item.at("replay");
            Selection selected{query.board, rank.account, item.at("entry_id").get<std::string>(), {}, 0, static_cast<std::uint64_t>(rank.score)};
            if (!replay.is_null()) {
                const auto digest = replay.at("sha256").get<std::string>();
                Require(digest.size() == 64 && std::all_of(digest.begin(), digest.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) &&
                        U32(replay.at("size"), true) <= kReplayLimit && replay.at("format").get<std::string>() == ProtocolFor(game_).replay_format);
                rank.has_data = 1;
                selected.digest = digest; selected.size = U32(replay.at("size"), true);
            }
            selections.push_back(std::move(selected));
            if (query.kind == Kind::Range) { std::memcpy(ranks.data() + index * sizeof(Rank), &rank, sizeof(rank)); }
            else { const PlayerRank player{1, {}, rank}; std::memcpy(ranks.data(), &player, sizeof(player)); }
            const auto comment = Unbase64(item.at("comment_base64").get<std::string>(), sizeof(Comment));
            Require(comment.size() == sizeof(Comment) && comment.back() == std::byte{});
            std::memcpy(comments.data() + index * sizeof(Comment), comment.data(), comment.size());
            const auto information = Unbase64(item.at("game_info_base64").get<std::string>(), 189);
            GameInfo info{}; info.size = information.size(); std::copy(information.begin(), information.end(), info.bytes);
            std::memcpy(infos.data() + index * sizeof(GameInfo), &info, sizeof(info));
        }
        // This title explicitly handles GAME_RANKING_NOT_FOUND as an empty
        // personal result. A successful zero-count personal lookup instead
        // constructs an entry from its uninitialized local metadata and loses
        // the menu coroutine. Keep range-empty success distinct from absence
        // of the requested player's record (1.02: +0x1e5fc0 / +0x1e6d10).
        result = query.kind == Kind::Account && entries.empty() ? kRankingNotFound : static_cast<int>(entries.size());
        std::vector<Write> writes{{query.ranks, std::move(ranks)}};
        if (query.comments.address) { writes.push_back({query.comments, std::move(comments)}); }
        if (query.info.address) { writes.push_back({query.info, std::move(infos)}); }
        if (query.date.address) { writes.push_back({query.date, Bytes(date)}); }
        if (query.total.address) { writes.push_back({query.total, Bytes(total)}); }
        return writes;
    }
    void Chunk(Request& request, const Query& query) {
        auto& download = *request.download;
        const auto size = static_cast<std::uint64_t>(download.payload.size());
        const auto bytes = (std::min)(query.ranks.size, download.payload.size() - download.cursor);
        std::vector<Write> writes{{query.total, Bytes(size)}};
        if (bytes) {
            writes.push_back({{query.ranks.address, bytes},
                {download.payload.begin() + download.cursor, download.payload.begin() + download.cursor + bytes}});
        }
        request.writes = std::move(writes); request.query = query; request.delivered = false; download.advance = bytes;
        request.result = 0; request.complete = true;
    }
    void Complete(Request& request, Reply reply) {
        request.complete = true;
        if (!reply) {
            request.result = reply.error == Error::Cancelled ? kAbort : reply.error == Error::Protocol ? kResponse : kServer;
            return;
        }
        if (request.query.kind == Kind::Download) {
            Require(request.download && reply.bytes.size() == request.download->selected.size);
            request.download->payload = std::move(reply.bytes); Chunk(request, request.query); return;
        }
        if (request.query.kind == Kind::Upload) {
            Require(reply.body.at("entry_id") == request.upload_entry && U32(reply.body.at("size"), true) == request.upload_size &&
                    reply.body.at("format").get<std::string>() == ProtocolFor(game_).replay_format);
            request.result = 0; return;
        }
        if (request.query.kind != Kind::Score) {
            request.writes = Ranking(reply, request.query, request.result, request.selections); return;
        }
        const auto& body = reply.body;
        Require(body.at("accepted").is_boolean() && body.at("upload_eligible").is_boolean());
        const auto entry = body.at("entry_id").get<std::string>();
        const auto rank = U32(body.at("serial_rank"), true);
        Require(Identifier(entry) && Decimal(body.at("account_id").get<std::string>()) == request.submitted_account);
        const auto accepted = body.at("accepted").get<bool>(), eligible = body.at("upload_eligible").get<bool>();
        Require(accepted || !eligible);
        request.result = accepted ? 0 : kNotBest;
        if (accepted) {
            if (request.query.temporary_rank.address) { request.writes.push_back({request.query.temporary_rank, Bytes(rank)}); }
            request.receipt = Receipt{request.query.board, request.submitted_score, request.submission, entry,
                                      request.submitted_nickname, eligible, request.round};
        }
    }
public:
    bool ContextsLive() {
        std::lock_guard lock(gate_);
        return std::any_of(contexts_.begin(), contexts_.end(), [](const auto& context) { return context.id != 0; });
    }
    bool Install(State state, Send send, Upload upload, Fetch fetch,std::string_view game="ketsui") {
        std::lock_guard lock(gate_);
        if (state_ || !state || !send || !upload || !fetch) { return false; }
        (void)ProtocolFor(game);game_=game;
        state_ = std::move(state); send_ = std::move(send);
        upload_ = std::move(upload); fetch_ = std::move(fetch); return true;
    }
    int CreateContext(unsigned label, int user) {
        std::lock_guard lock(gate_);
        if (label || user != 0x1000) { return kArgument; }
        if (!state_) { return kInit; }
        // Explicit whole-round mode issues a local operation handle, not an
        // online identity. Queries still require ready, and only a factory-
        // scoped score may use offline acceptance.
        if (!state_().ready && !NativeCommunityRoundsEnabled()) { return kLogin; }
        if (next_context_ == (std::numeric_limits<int>::max)()) { return kLimit; }
        for (auto& context : contexts_) { if (!context.id) { context.id = next_context_++; return context.id; } }
        return kLimit;
    }
    int DeleteContext(int id) {
        std::lock_guard lock(gate_);
        auto* context = ContextFor(id); if (!context) { return kId; }
        for (const auto& request : requests_) { if (request.id && request.context == id) { return kBusy; } }
        *context = {}; return 0;
    }
    int CreateRequest(int context) {
        std::lock_guard lock(gate_);
        if (!ContextFor(context)) { return kId; }
        if (next_request_ == (std::numeric_limits<int>::max)()) { return kLimit; }
        for (auto& request : requests_) {
            if (!request.id) { request.id = next_request_++; request.context = context; return request.id; }
        }
        return kLimit;
    }
    int DeleteRequest(int id) {
        std::lock_guard lock(gate_);
        auto* request = RequestFor(id); if (!request) { return kId; }
        *request = {}; return 0; // The worker owns no destination pointers.
    }
    int Abort(int id) {
        std::lock_guard lock(gate_);
        auto* request = RequestFor(id); if (!request) { return kId; }
        if (!request->complete || !request->delivered) {
            request->aborted = request->complete = true; request->result = kAbort;
            request->future = {}; request->writes.clear(); request->query = {};
            request->round.reset();
            request->receipt.reset(); request->selections.clear(); request->download.reset();
        }
        return 0;
    }
    int Poll(int id, int* output) {
        if (!Writable(output, sizeof(*output))) { return kArgument; }
        std::lock_guard lock(gate_);
        auto* request = RequestFor(id); if (!request) { return kId; }
        if (!request->complete) {
            if (!request->round && !request->future.valid()) { return kId; }
            if (!request->round && request->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { return 1; }
            try {
                if (request->round) {
                    auto result=PollNativeCommunityRoundScore(request->round,request->query.board);
                    if (!result.ready) return 1;
                    if (result.reply) {
                        Require(result.identity && result.identity->ready && ValidNicknameBytes(result.identity->nickname) &&
                                result.identity->account==AccountId(result.identity->nickname));
                        Require(!request->submitted_account || request->submitted_account==result.identity->account);
                        request->submitted_account=result.identity->account;
                        request->submitted_nickname=result.identity->nickname;
                    }
                    request->submission=std::move(result.submission); Complete(*request,std::move(result.reply));
                } else Complete(*request, request->future.get());
            }
            catch (...) {
                request->complete = true; request->result = kResponse; request->writes.clear();
                request->receipt.reset(); request->selections.clear(); request->download.reset();
            }
        }
        // Prepare context changes before acquiring output leases. Publishing
        // receipts/selections must not allocate or fail after guest writes.
        auto* context = ContextFor(request->context);
        std::optional<std::vector<Receipt>> receipts;
        std::optional<std::vector<Selection>> selections;
        if (!request->delivered && request->receipt) {
            receipts = context->receipts;
            std::erase_if(*receipts, [&](const auto& receipt) { return receipt.board == request->query.board; });
            receipts->push_back(*request->receipt);
        }
        if (!request->delivered && (request->result >= 0 || request->result == kRankingNotFound) &&
            (request->query.kind == Kind::Range || request->query.kind == Kind::Account)) {
            selections = context->selections;
            if (request->query.kind == Kind::Account) {
                std::erase_if(*selections, [&](const auto& entry) {
                    return entry.board == request->query.board && entry.account == request->query.account;
                });
            }
            for (const auto& selected : request->selections) {
                std::erase_if(*selections, [&](const auto& entry) { return entry.board == selected.board && entry.account == selected.account; });
                selections->push_back(selected);
            }
            if (selections->size() > 256) { selections->erase(selections->begin(), selections->end() - 256); }
        }
        auto writes = request->delivered ? std::vector<Write>{} : request->writes;
        writes.push_back({{output, sizeof(*output)}, Bytes(request->result)});
        if (!Commit(writes)) { return kArgument; }
        if (receipts) { context->receipts.swap(*receipts); }
        if (selections) { context->selections.swap(*selections); }
        if (!request->delivered && request->download && request->result == 0) {
            request->download->cursor += request->download->advance; request->download->advance = 0;
        }
        if (!request->delivered) {
            std::printf("{\"event\":\"native_score_operation\",\"phase\":\"delivered\",\"request\":%d,\"kind\":\"%s\",\"board\":%u,\"result\":%d}\n",
                        request->id, KindName(request->query.kind), request->query.board, request->result);
        }
        request->receipt.reset(); request->selections.clear();
        request->delivered = true; request->writes.clear(); return 0;
    }
    int Score(int id, unsigned board, std::int64_t score, const Comment* comment, const GameInfo* info,
              unsigned* temporary_rank, const std::uint64_t* compare, void* option) {
        if (!Board(board) || score < 0 || compare || option || (temporary_rank && !Writable(temporary_rank, sizeof(*temporary_rank)))) { return kArgument; }
        if (game_=="daioujou" && (board/10==1 || score>(board==99 ? 3960000 : 999999999))) return kArgument;
        Comment copied{}; GameInfo information{};
        if ((comment && !Read(comment, &copied, sizeof(copied))) || copied.bytes[63] ||
            (info && (!Read(info, &information, sizeof(information)) || information.size > 189))) { return kArgument; }
        std::lock_guard lock(gate_);
        const auto round=CurrentNativeCommunityRound();
        if (NativeCommunityRoundsEnabled() && !round) return kArgument;
        auto* request = RequestFor(id); if (const auto error = Ready(request,!round)) { return error; }
        // Concurrent submissions to one board have no native receipt argument
        // to disambiguate a later upload. Keep that sequence single-flight.
        for (const auto& other : requests_) {
            if (other.id && other.context == request->context && other.query.kind == Kind::Score &&
                other.query.board == board && !other.delivered && !other.aborted) { return kBusy; }
        }
        const auto identity = state_();
        if (!identity.account && !round) return kLogin;
        if (round) {
            if (!BeginNativeCommunityRoundScore(round,board,static_cast<std::uint64_t>(score),std::span<const std::byte,64>(reinterpret_cast<const std::byte*>(&copied),sizeof(copied)),
                                                std::span(information.bytes,static_cast<std::size_t>(information.size)))) return kArgument;
            Query query; query.kind=Kind::Score; query.board=board; query.temporary_rank={temporary_rank,sizeof(*temporary_rank)};
            request->submission.clear(); request->submitted_score=score; request->submitted_nickname=identity.nickname; request->submitted_account=identity.account;
            const auto started=Start(*request,query,std::future<Reply>{}); request->round=round;
            auto& receipts=ContextFor(request->context)->receipts;
            std::erase_if(receipts,[&](const auto& receipt) { return receipt.board==board; });
            return started;
        }
        const auto submission = NewId();
        Json body{{"submission_id", submission}, {"nickname", identity.nickname}, {"board", board}, {"rules", "standard"},
                  {"score", std::to_string(score)}, {"comment_base64", Base64(std::as_bytes(std::span(&copied, 1)))},
                  {"game_info_base64", Base64(std::span(information.bytes, static_cast<std::size_t>(information.size)))},
                  {"build", game_+"-native-"+std::string(ProtocolFor(game_).version)}};
        Query query; query.kind = Kind::Score; query.board = board; query.temporary_rank = {temporary_rank, sizeof(*temporary_rank)};
        // Finish allocations needed for the submission reference before enqueue.
        request->submission = submission; request->submitted_score = score;
        request->submitted_nickname = identity.nickname; request->submitted_account = identity.account;
        const auto started = Start(*request, query, "/scores", std::move(body), true);
        // A failed/lower/equal new attempt must never inherit an older receipt.
        auto& receipts = ContextFor(request->context)->receipts;
        std::erase_if(receipts, [&](const auto& receipt) { return receipt.board == board; });
        return started;
    }
    int UploadReplay(int id, unsigned board, std::int64_t score, std::uint64_t total,
                     std::uint64_t size, const void* data, void* option) {
        if (!Board(board) || score < 0 || !total || total != size || option) { return kArgument; }
        if (size > kReplayLimit) { return kTooLarge; }
        std::lock_guard lock(gate_);
        // A score receipt was already delivered by the server. A subsequent
        // connection failure must not prevent owning/staging the attachment
        // for another eligible board in that same submission batch. Keep the
        // session offline; transport failure is still returned by Poll.
        auto* request = RequestFor(id); if (const auto error = Ready(request, false)) { return error; }
        const auto& receipts = ContextFor(request->context)->receipts;
        const auto receipt = std::find_if(receipts.begin(), receipts.end(), [&](const auto& value) { return value.board == board; });
        if (receipt == receipts.end() || !receipt->eligible || receipt->score != score || receipt->nickname != state_().nickname) { return kNotBest; }
        std::vector<std::byte> payload(static_cast<std::size_t>(size));
        if (!Read(data, payload.data(), payload.size())) { return kArgument; }
        Json metadata{{"entry_id", receipt->entry}, {"submission_id", receipt->submission}, {"nickname", receipt->nickname}};
        auto entry = receipt->entry;
        Query query; query.kind = Kind::Upload; query.board = board;
        const auto canonical_size=receipt->round ? NativeCommunityRoundReplayBytes(receipt->round) : static_cast<std::size_t>(size);
        auto future = receipt->round ? ProveNativeCommunityRoundReplay(receipt->round,board,static_cast<std::uint64_t>(score),std::move(payload)) :
                      upload_(std::move(metadata), std::move(payload), {board, static_cast<std::uint64_t>(score)});
        Start(*request, query, std::move(future));
        request->upload_entry = std::move(entry); request->upload_size = canonical_size; return 0;
    }
    int DownloadReplay(int id, unsigned board, std::uint64_t account, std::uint64_t* total,
                       std::uint64_t size, void* data, void* option) {
        if (!Board(board) || !account || account > 0x7fffffffffffffffull || !size || size > kReplayLimit || option) { return kArgument; }
        const std::array outputs{Output{total, sizeof(*total)}, Output{data, static_cast<std::size_t>(size)}};
        if (!Distinct(outputs) || !Writable(total, sizeof(*total)) || !Writable(data, static_cast<std::size_t>(size))) { return kArgument; }
        std::lock_guard lock(gate_);
        auto* request = RequestFor(id);
        const bool continuation = request && request->query.kind == Kind::Download && request->download &&
            !request->download->payload.empty();
        if (const auto error = Ready(request, !continuation)) { return error; }
        Query query; query.kind = Kind::Download; query.board = board; query.account = account;
        query.ranks = outputs[1]; query.total = outputs[0];
        if (continuation) {
            if (request->download->selected.board != board || request->download->selected.account != account) { return kArgument; }
            Chunk(*request, query); return 0;
        }
        const auto& selections = ContextFor(request->context)->selections;
        const auto found = std::find_if(selections.begin(), selections.end(), [&](const auto& value) {
            return value.board == board && value.account == account;
        });
        if (found == selections.end() || !found->size) { return kNoData; }
        Download download; download.selected = *found;
        auto future = fetch_(found->entry, found->size, found->digest, {found->board, found->score});
        Start(*request, query, std::move(future)); request->download = std::move(download); return 0;
    }
    int Ranking(int id, unsigned board, unsigned start, void* ranks, std::uint64_t rank_bytes,
                Comment* comments, std::uint64_t comment_bytes, GameInfo* info, std::uint64_t info_bytes,
                std::uint64_t count, std::uint64_t* date, unsigned* total, void* option, std::uint64_t account = 0) {
        const auto stride = account ? sizeof(PlayerRank) : sizeof(Rank);
        if (!Board(board) || !start || !count || count > 100 || (account && count != 1) || option || rank_bytes != count * stride ||
            (comments ? comment_bytes != count * sizeof(Comment) : comment_bytes != 0) ||
            (info ? info_bytes != count * sizeof(GameInfo) : info_bytes != 0)) { return kArgument; }
        std::vector<Output> outputs{{ranks, static_cast<std::size_t>(rank_bytes)}};
        if (comments) { outputs.push_back({comments, static_cast<std::size_t>(comment_bytes)}); }
        if (info) { outputs.push_back({info, static_cast<std::size_t>(info_bytes)}); }
        if (date) { outputs.push_back({date, sizeof(*date)}); }
        if (total) { outputs.push_back({total, sizeof(*total)}); }
        if (!Distinct(outputs) || !std::all_of(outputs.begin(), outputs.end(), [](const auto& out) { return Writable(out.address, out.size); })) { return kArgument; }
        std::lock_guard lock(gate_);
        auto* request = RequestFor(id); if (const auto error = Ready(request)) { return error; }
        Query query; query.kind = account ? Kind::Account : Kind::Range;
        query.board = board; query.start = start; query.count = static_cast<unsigned>(count); query.account = account;
        query.ranks = outputs[0]; query.comments = {comments, static_cast<std::size_t>(comment_bytes)};
        query.info = {info, static_cast<std::size_t>(info_bytes)}; query.date = {date, sizeof(*date)}; query.total = {total, sizeof(*total)};
        auto path = "/leaderboards?board=" + std::to_string(board) + "&rules=standard";
        if (account) { path += "&account_id=" + std::to_string(account); }
        else { path += "&start=" + std::to_string(start) + "&count=" + std::to_string(count); }
        return Start(*request, query, std::move(path));
    }
};
Bridge bridge;
template<class F> int Guard(F&& body) noexcept {
    try { return body(); } catch (const std::bad_alloc&) { return kMemory; } catch (...) { return kResponse; }
}
int KT_SYSV CreateContext(unsigned label, int user) { return Guard([&] { return NativeUserInitialized() ? bridge.CreateContext(label, user) : kLogin; }); }
int KT_SYSV DeleteContext(int id) { return Guard([&] { return bridge.DeleteContext(id); }); }
int KT_SYSV CreateRequest(int id) { return Guard([&] { return bridge.CreateRequest(id); }); }
int KT_SYSV DeleteRequest(int id) { return Guard([&] { return bridge.DeleteRequest(id); }); }
int KT_SYSV Abort(int id) { return Guard([&] { return bridge.Abort(id); }); }
int KT_SYSV Poll(int id, int* output) { return Guard([&] { return bridge.Poll(id, output); }); }
int KT_SYSV Score(int id, unsigned board, std::int64_t score, const Comment* comment, const GameInfo* info,
                  unsigned* rank, const std::uint64_t* compare, void* option) {
    return Guard([&] { return bridge.Score(id, board, score, comment, info, rank, compare, option); });
}
int KT_SYSV RecordData(int id, unsigned board, std::int64_t score, std::uint64_t total,
                       std::uint64_t size, const void* data, void* option) {
    return Guard([&] { return bridge.UploadReplay(id, board, score, total, size, data, option); });
}
int KT_SYSV GetData(int id, unsigned board, std::uint64_t account, std::uint64_t* total,
                    std::uint64_t size, void* data, void* option) {
    return Guard([&] { return bridge.DownloadReplay(id, board, account, total, size, data, option); });
}
int KT_SYSV Range(int id, unsigned board, unsigned start, Rank* ranks, std::uint64_t rank_bytes, Comment* comments,
                  std::uint64_t comment_bytes, GameInfo* info, std::uint64_t info_bytes, std::uint64_t count,
                  std::uint64_t* date, unsigned* total, void* option) {
    return Guard([&] { return bridge.Ranking(id, board, start, ranks, rank_bytes, comments, comment_bytes, info, info_bytes, count, date, total, option); });
}
int KT_SYSV Account(int id, unsigned board, const std::uint64_t* accounts, std::uint64_t account_bytes, PlayerRank* ranks,
                    std::uint64_t rank_bytes, Comment* comments, std::uint64_t comment_bytes, GameInfo* info,
                    std::uint64_t info_bytes, std::uint64_t count, std::uint64_t* date, unsigned* total, void* option) {
    return Guard([&] {
        std::uint64_t account{};
        if (count != 1 || account_bytes != sizeof(account) || !Read(accounts, &account, sizeof(account)) || !account || account > 0x7fffffffffffffffull) { return kArgument; }
        return bridge.Ranking(id, board, 1, ranks, rank_bytes, comments, comment_bytes, info, info_bytes, count, date, total, option, account);
    });
}
}
std::uint64_t ResolveScoreImport(std::string_view identifier, std::string_view library, std::string_view module) {
    if (library != "libSceNpScore" || module != "libSceNpScore") { return 0; }
    const auto id = identifier.substr(0, identifier.find('#'));
    if (id == "GWnWQNXZH5M") { return reinterpret_cast<std::uint64_t>(&CreateContext); }
    if (id == "G0pE+RNCwfk") { return reinterpret_cast<std::uint64_t>(&DeleteContext); }
    if (id == "gW8qyjYrUbk") { return reinterpret_cast<std::uint64_t>(&CreateRequest); }
    if (id == "dK8-SgYf6r4") { return reinterpret_cast<std::uint64_t>(&DeleteRequest); }
    if (id == "1i7kmKbX6hk") { return reinterpret_cast<std::uint64_t>(&Abort); }
    if (id == "m1DfNRstkSQ") { return reinterpret_cast<std::uint64_t>(&Poll); }
    if (id == "ANJssPz3mY0") { return reinterpret_cast<std::uint64_t>(&Score); }
    if (id == "y5ja7WI05rs") { return reinterpret_cast<std::uint64_t>(&Range); }
    if (id == "dRszNNyGWkw") { return reinterpret_cast<std::uint64_t>(&Account); }
    if (id == "1gL5PwYzrrw") { return reinterpret_cast<std::uint64_t>(&RecordData); }
    if (id == "PP9jx8s0574") { return reinterpret_cast<std::uint64_t>(&GetData); }
    return 0;
}
bool ConfigureNativeScore(std::shared_ptr<kt::community::Client> client) {
    if (!client) { return false; }
    return bridge.Install([client] { return client->State(); },
        [client](std::string path, Json body, bool durable) { return client->Request(std::move(path), std::move(body), durable); },
        [client](Json body, std::vector<std::byte> payload, ReplayExpectation expected) {
            return client->UploadReplayEnvelope(std::move(body), std::move(payload), expected);
        },
        [client](std::string entry, std::size_t size, std::string digest, ReplayExpectation expected) {
            return client->DownloadReplayEnvelope(std::move(entry), size, std::move(digest), expected);
        },client->Game());
}
bool NativeScoreContextsLive() { return bridge.ContextsLive(); }

namespace {
Json AuthoredEntry(std::string_view name = "Alice", unsigned serial = 1) {
    return Json{{"entry_id", "0123456789abcdef0123456789abcdef"}, {"nickname", name},
        {"account_id", std::to_string(AccountId(name))}, {"score", "9007199254740993"}, {"serial_rank", serial}, {"rank", serial},
        {"created_us", "1700000000000000"}, {"comment_base64", Base64(std::array<std::byte, 64>{})},
        {"game_info_base64", ""}, {"replay", nullptr}};
}
Reply AuthoredPage(Json entries = Json::array({AuthoredEntry()})) {
    Reply reply; reply.status = 200;
    const auto total = entries.size();
    reply.body = Json{{"board", 0}, {"rules", "standard"}, {"start", 1}, {"total", total},
                      {"sorted_us", "1700000000000001"}, {"entries", std::move(entries)}};
    return reply;
}
Reply AuthoredReceipt(bool accepted = true) {
    Reply reply; reply.status = 200;
    reply.body = Json{{"entry_id", "0123456789abcdef0123456789abcdef"}, {"accepted", accepted},
        {"upload_eligible", accepted}, {"serial_rank", 1}, {"account_id", std::to_string(AccountId("Alice"))}};
    return reply;
}
struct AuthoredTransport {
    bool online = true;
    std::vector<std::promise<Reply>> replies;
    std::vector<Json> bodies;
    std::vector<std::string> paths;
    std::vector<bool> durable;
    std::vector<std::vector<std::byte>> uploads;
    std::size_t fulfilled{};
    std::future<Reply> Send(std::string path, Json body, bool persisted) {
        paths.push_back(std::move(path)); bodies.push_back(std::move(body)); durable.push_back(persisted);
        replies.emplace_back(); return replies.back().get_future();
    }
    bool Install(Bridge& target=bridge,std::string_view game="ketsui") {
        return target.Install([this,game=std::string(game)] { return Identity{online, "Alice", AccountId("Alice",game)}; },
            [this](std::string path, Json body, bool persisted) { return Send(std::move(path), std::move(body), persisted); },
            [this](Json body, std::vector<std::byte> payload, ReplayExpectation) {
                uploads.push_back(std::move(payload)); return Send("/replays", std::move(body), true);
            },
            [this](std::string entry, std::size_t size, std::string digest, ReplayExpectation) {
                return Send("/entries/" + entry + "/replay", Json{{"size", size}, {"sha256", std::move(digest)}}, false);
            },game);
    }
    void Complete(Reply reply) { replies.at(fulfilled++).set_value(std::move(reply)); }
};
template<class T> struct Guarded { T value{}; std::array<unsigned char, 16> tail; Guarded() { tail.fill(0xa7); } };
template<class T> bool Tail(const Guarded<T>& value) { return std::all_of(value.tail.begin(), value.tail.end(), [](auto c) { return c == 0xa7; }); }
int AwaitScore(int id) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    int result = 73;
    for (;;) {
        const auto polled = Poll(id, &result);
        Require(polled == 0 || polled == 1);
        if (!polled) { return result; }
        Require(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
void InitializeAuthoredUser() {
    using Initialize = int (KT_SYSV *)(const void*);
    const auto initialize = reinterpret_cast<Initialize>(ResolveUserImport("j3YMu1MVNNo", "libSceUserService", "libSceUserService"));
    Require(initialize && initialize(nullptr) == 0);
}
}
bool ScoreProfileSelfTest() {
    bool okay=true;unsigned cases{};
    const auto check=[&](bool passed,const char* name) {
        std::printf("{\"event\":\"np_score_profile_case\",\"name\":\"%s\",\"passed\":%s}\n",name,passed?"true":"false");
        ++cases;okay &= passed;
    };
    check(BoardFor("daioujou",0) && BoardFor("daioujou",6) && BoardFor("daioujou",66) &&
        BoardFor("daioujou",99) && !BoardFor("daioujou",7) && !BoardFor("daioujou",98) &&
        !BoardFor("ketsui",6) && BoardFor("ketsui",99),
        "title_board_sets_are_distinct");
    Bridge second;AuthoredTransport transport;
    check(transport.Install(second,"daioujou"),"second_title_owns_independent_native_bridge");
    const auto context=second.CreateContext(0,0x1000),request=second.CreateRequest(context);
    check(context>0 && request>0,"native_context_and_request_created");
    Guarded<unsigned> rank;rank.value=73;
    check(second.Score(request,10,123,nullptr,nullptr,&rank.value,nullptr,nullptr)==kArgument &&
        second.Score(request,6,1000000000,nullptr,nullptr,&rank.value,nullptr,nullptr)==kArgument && transport.paths.empty(),
        "custom_and_non_native_score_rejected_before_enqueue");
    check(second.Score(request,6,123456789,nullptr,nullptr,&rank.value,nullptr,nullptr)==0 &&
        transport.bodies.back().at("board")==6 && transport.bodies.back().at("score")=="123456789" &&
        transport.bodies.back().at("build")=="daioujou-native-1.04", "native_integer_and_combined_board_are_not_rewritten");
    transport.Complete(AuthoredReceipt());
    Guarded<int> result;result.value=73;
    check(second.Poll(request,&result.value)==0 && result.value==kResponse && rank.value==73 && Tail(rank),
        "peer_title_account_receipt_cannot_publish_native_rank");
    Require(second.Score(request,6,123456789,nullptr,nullptr,&rank.value,nullptr,nullptr)==0);
    auto receipt=AuthoredReceipt();receipt.body["account_id"]=std::to_string(AccountId("Alice","daioujou"));
    transport.Complete(std::move(receipt));
    check(second.Poll(request,&result.value)==0 && result.value==0 && rank.value==1 && Tail(rank) && Tail(result),
        "title_bound_receipt_publishes_exact_rank");
    Guarded<Rank> entry;std::uint64_t date{};unsigned total{};
    Require(second.Ranking(request,6,1,&entry.value,sizeof(entry.value),nullptr,0,nullptr,0,1,&date,&total,nullptr)==0);
    auto source=AuthoredEntry();source["account_id"]=std::to_string(AccountId("Alice","daioujou"));source["score"]="123456789";
    auto page=AuthoredPage(Json::array({source}));page.body["board"]=6;transport.Complete(std::move(page));
    check(second.Poll(request,&result.value)==0 && result.value==1 && total==1 &&
        entry.value.account==AccountId("Alice","daioujou") && entry.value.score==123456789 && Tail(entry),
        "native_board_readback_preserves_title_identity_and_score_units");
    const auto queued=transport.paths.size();
    check(second.Score(request,99,3960001,nullptr,nullptr,nullptr,nullptr,nullptr)==kArgument && transport.paths.size()==queued,
        "challenge_points_cannot_exceed_66_scenes_six_teams_gold_bound");
    Require(second.Score(request,99,10003,nullptr,nullptr,&rank.value,nullptr,nullptr)==0);
    check(transport.bodies.back().at("board")==99 && transport.bodies.back().at("score")=="10003",
        "challenge_points_use_their_own_board_without_score_scaling");
    receipt=AuthoredReceipt();receipt.body["account_id"]=std::to_string(AccountId("Alice","daioujou"));
    receipt.body["upload_eligible"]=false;transport.Complete(std::move(receipt));
    check(second.Poll(request,&result.value)==0 && result.value==0 && rank.value==1,
        "challenge_score_without_replay_is_successful");
    std::array<std::byte,1> empty_attachment{};
    check(second.UploadReplay(request,99,10003,1,1,empty_attachment.data(),nullptr)==kNotBest,
        "challenge_score_does_not_publish_a_playable_replay");
    Require(second.Ranking(request,99,1,&entry.value,sizeof(entry.value),nullptr,0,nullptr,0,1,&date,&total,nullptr)==0);
    source=AuthoredEntry();source["account_id"]=std::to_string(AccountId("Alice","daioujou"));source["score"]="10003";
    page=AuthoredPage(Json::array({source}));page.body["board"]=99;transport.Complete(std::move(page));
    check(second.Poll(request,&result.value)==0 && result.value==1 && entry.value.score==10003 && !entry.value.has_data && Tail(entry),
        "challenge_readback_keeps_exact_points_and_no_replay_badge");
    check(second.DeleteRequest(request)==0 && second.DeleteContext(context)==0 && !second.ContextsLive(),
        "title_local_contexts_are_released");
    std::printf("{\"event\":\"np_score_profile_self_test\",\"passed\":%s,\"cases\":%u,\"authored_data_only\":true,\"native_game_execution\":false}\n",okay?"true":"false",cases);
    return okay;
}
bool ScoreSelfTest() {
    bool okay = true; unsigned cases{};
    const auto check = [&](bool passed, const char* name) {
        std::printf("{\"event\":\"np_score_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, passed ? "true" : "false");
        ++cases; okay &= passed;
    };
    InitializeAuthoredUser();
    check(CreateContext(0, 0x1000) == kInit && !ConfigureNativeScore(nullptr), "disabled_provider_does_not_issue_context");
    check(!ResolveScoreImport("y5ja7WI05rs", "wrong", "libSceNpScore") &&
          !ResolveScoreImport("y5ja7WI05rs", "libSceNpScore", "wrong") &&
          !ResolveScoreImport("unknown", "libSceNpScore", "libSceNpScore") &&
          ResolveScoreImport("PP9jx8s0574", "libSceNpScore", "libSceNpScore") &&
          ResolveScoreImport("1gL5PwYzrrw", "libSceNpScore", "libSceNpScore"), "namespace_and_exact_replay_imports");
    const auto transport = std::make_shared<AuthoredTransport>();
    check(transport->Install(), "explicit_authored_transport_attached");
    const auto context = CreateContext(0, 0x1000);
    check(context > 0 && CreateContext(1, 0x1000) == kArgument && CreateContext(0, 42) == kArgument, "context_label_and_local_user");
    const auto request = CreateRequest(context);
    check(request > 0 && DeleteContext(context) == kBusy && CreateRequest(42) == kId, "parent_context_cannot_be_deleted_with_requests");
    auto range = reinterpret_cast<decltype(&Range)>(ResolveScoreImport("y5ja7WI05rs#tag", "libSceNpScore", "libSceNpScore"));
    auto personal = reinterpret_cast<decltype(&Account)>(ResolveScoreImport("dRszNNyGWkw", "libSceNpScore", "libSceNpScore"));
    auto score = reinterpret_cast<decltype(&Score)>(ResolveScoreImport("ANJssPz3mY0", "libSceNpScore", "libSceNpScore"));
    Guarded<std::array<Rank, 2>> ranks; ranks.value[0].score = 73;
    Guarded<std::array<Comment, 2>> comments;
    Guarded<std::array<GameInfo, 2>> info;
    Guarded<std::uint64_t> date; date.value = 73;
    Guarded<unsigned> total; total.value = 73;
    Guarded<int> result; result.value = 73;
    const auto submit_range = [&] {
        return range(request, 0, 1, ranks.value.data(), sizeof(ranks.value), comments.value.data(), sizeof(comments.value),
                     info.value.data(), sizeof(info.value), 2, &date.value, &total.value, nullptr);
    };
    check(submit_range() == 0 && ranks.value[0].score == 73 && date.value == 73 && total.value == 73,
          "sysv_thirteen_arguments_do_not_write_on_submit");
    check(Poll(request, &result.value) == 1 && result.value == 73 && submit_range() == kBusy,
          "busy_poll_preserves_result_and_duplicate_submit_rejected");
    transport->Complete(AuthoredPage());
    check(ranks.value[0].score == 73, "worker_completion_does_not_touch_guest_output");
    check(Poll(request, &result.value) == 0 && result.value == 1 && total.value == 1 && date.value == kUnixTick + 1700000000000001ull &&
          ranks.value[0].score == 9007199254740993ll && ranks.value[0].serial == 1 && ranks.value[0].online.term == 0 &&
          ranks.value[0].account == AccountId("Alice") && ranks.value[1].account == 0 &&
          Tail(ranks) && Tail(comments) && Tail(info) && Tail(date) && Tail(total) && Tail(result),
          "poll_commits_exact_144byte_rows_64bit_values_zero_unused_and_preserves_tails");
    ranks.value[0].score = 92;
    check(Poll(request, &result.value) == 0 && ranks.value[0].score == 92, "repeated_poll_does_not_rewrite_delivered_buffers");
    Guarded<PlayerRank> player;
    Guarded<Comment> own_comment;
    const auto account = AccountId("Alice");
    check(personal(request, 0, &account, sizeof(account), &player.value, sizeof(player.value), &own_comment.value,
                   sizeof(own_comment.value), nullptr, 0, 1, &date.value, &total.value, nullptr) == 0,
          "sysv_fourteen_argument_personal_query_submitted");
    transport->Complete(AuthoredPage());
    check(Poll(request, &result.value) == 0 && result.value == 1 && player.value.present == 1 && player.value.value.account == account &&
          Tail(player) && Tail(own_comment) && transport->paths.back().find("&start=") == std::string::npos &&
          transport->paths.back().find("&count=") == std::string::npos, "personal_152byte_layout_and_stack_outputs");
    check(personal(request, 0, &account, 7, &player.value, sizeof(player.value), nullptr, 0, nullptr, 0, 1, nullptr, nullptr, nullptr) == kArgument &&
          range(request, 0, 1, ranks.value.data(), 1, nullptr, 0, nullptr, 0, 2, nullptr, nullptr, nullptr) == kArgument &&
          range(request, 60, 1, ranks.value.data(), sizeof(ranks.value), nullptr, 0, nullptr, 0, 2, nullptr, nullptr, nullptr) == kArgument,
          "invalid_capacity_and_unfingerprinted_custom_rejected");
    Comment comment{}; std::memcpy(comment.bytes, "U0=7,", 5);
    GameInfo information{}; information.size = 3; information.bytes[0] = std::byte{0xa5};
    Guarded<unsigned> temporary; temporary.value = 73;
    check(score(request, 0, 9007199254740993ll, &comment, &information, &temporary.value, nullptr, nullptr) == 0 && temporary.value == 73 &&
          transport->durable.back() && transport->bodies.back().at("score") == "9007199254740993", "sysv_eight_argument_score_queued_durably_without_output");
    comment.bytes[0] = 'X'; information.bytes[0] = std::byte{};
    check(Unbase64(transport->bodies.back().at("comment_base64").get<std::string>(), 64)[0] == std::byte{'U'} &&
          Unbase64(transport->bodies.back().at("game_info_base64").get<std::string>(), 189)[0] == std::byte{0xa5}, "submit_copies_guest_comment_and_info_before_enqueue");
    transport->Complete(AuthoredReceipt());
    check(Poll(request, &result.value) == 0 && result.value == 0 && temporary.value == 1 && Tail(temporary), "accepted_score_delivers_temporary_rank");
    temporary.value = 73;
    check(score(request, 0, 42, nullptr, nullptr, &temporary.value, nullptr, nullptr) == 0, "completed_request_can_be_reused");
    transport->Complete(AuthoredReceipt(false));
    check(Poll(request, &result.value) == 0 && result.value == kNotBest && temporary.value == 73, "lower_score_keeps_not_best_error_without_rank_write");
    check(submit_range() == 0 && Abort(request) == 0, "pending_request_can_be_aborted");
    transport->Complete(AuthoredPage()); ranks.value[0].score = 73;
    check(Poll(request, &result.value) == 0 && result.value == kAbort && ranks.value[0].score == 73 && submit_range() == kAbort,
          "late_completion_after_abort_never_writes_old_destination");
    check(DeleteRequest(request) == 0 && Poll(request, &result.value) == kId && DeleteRequest(request) == kId,
          "deleted_request_identity_stays_invalid");
    const auto deleted = CreateRequest(context);
    check(range(deleted, 0, 1, ranks.value.data(), sizeof(ranks.value), nullptr, 0, nullptr, 0, 2, nullptr, nullptr, nullptr) == 0 &&
          DeleteRequest(deleted) == 0, "active_request_deletion_does_not_wait_for_network");
    transport->Complete(AuthoredPage());
    check(Poll(deleted, &result.value) == kId && ranks.value[0].score == 73, "late_completion_after_delete_has_no_reused_identity_or_write");
    const auto malformed = CreateRequest(context);
    auto bad = AuthoredPage(Json::array({AuthoredEntry(), AuthoredEntry("Bob", 2)}));
    bad.body["entries"][1]["score"] = "not-a-number";
    check(range(malformed, 0, 1, ranks.value.data(), sizeof(ranks.value), nullptr, 0, nullptr, 0, 2, &date.value, &total.value, nullptr) == 0,
          "malformed_response_fixture_queued");
    transport->Complete(std::move(bad)); date.value = 73; total.value = 73;
    check(Poll(malformed, &result.value) == 0 && result.value == kResponse && ranks.value[0].score == 73 && date.value == 73 && total.value == 73,
          "bad_second_row_does_not_partially_publish_first_row_or_metadata");
    (void)DeleteRequest(malformed);
    const auto protected_request = CreateRequest(context);
    auto* protected_rank = static_cast<Rank*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Require(protected_rank != nullptr); protected_rank->score = 73;
    Require(range(protected_request, 0, 1, protected_rank, sizeof(Rank), nullptr, 0, nullptr, 0, 1, &date.value, &total.value, nullptr) == 0);
    transport->Complete(AuthoredPage());
    DWORD previous{}; Require(VirtualProtect(protected_rank, 4096, PAGE_READONLY, &previous) != 0);
    result.value = 73;
    check(Poll(protected_request, &result.value) == kArgument && result.value == 73 && protected_rank->score == 73 && total.value == 73,
          "poll_rechecks_destination_permissions_before_any_output");
    Require(Abort(protected_request) == 0 && VirtualProtect(protected_rank, 4096, PAGE_READWRITE, &previous));
    check(Poll(protected_request, &result.value) == 0 && result.value == kAbort && protected_rank->score == 73,
          "abort_discards_prepared_but_undelivered_outputs_after_failed_poll");
    (void)DeleteRequest(protected_request); Require(VirtualFree(protected_rank, 0, MEM_RELEASE) != 0);
    const auto empty = CreateRequest(context);
    Require(range(empty, 0, 1, ranks.value.data(), sizeof(ranks.value), nullptr, 0, nullptr, 0, 2, nullptr, &total.value, nullptr) == 0);
    transport->Complete(AuthoredPage(Json::array()));
    check(Poll(empty, &result.value) == 0 && result.value == 0 && total.value == 0 && ranks.value[0].account == 0,
          "successful_empty_page_is_distinct_from_network_error");
    Require(range(empty, 0, 1, ranks.value.data(), sizeof(ranks.value), nullptr, 0, nullptr, 0, 2, nullptr, nullptr, nullptr) == 0);
    Reply offline; offline.error = Error::Network; transport->Complete(std::move(offline));
    check(Poll(empty, &result.value) == 0 && result.value == kServer, "network_failure_not_published_as_empty_success");
    player.value.present = 73;
    Require(personal(empty, 0, &account, sizeof(account), &player.value, sizeof(player.value), nullptr, 0, nullptr, 0, 1, nullptr, nullptr, nullptr) == 0);
    transport->Complete(AuthoredPage(Json::array()));
    check(Poll(empty, &result.value) == 0 && result.value == kRankingNotFound && player.value.present == 0 && player.value.value.account == 0,
          "unknown_personal_score_uses_native_not_found_and_never_constructs_a_phantom_row");
    ranks.value[0].score = 73;
    Require(range(empty, 0, 1, ranks.value.data(), sizeof(ranks.value), nullptr, 0, nullptr, 0, 2, nullptr, nullptr, nullptr) == 0);
    transport->Complete(AuthoredPage());
    check(Poll(empty, reinterpret_cast<int*>(ranks.value.data())) == kArgument && ranks.value[0].score == 73 &&
          Poll(empty, &result.value) == 0 && result.value == 1 && ranks.value[0].score == 9007199254740993ll,
          "overlapping_poll_result_rejected_before_any_write_and_can_retry_valid_destination");
    information.size = 190;
    check(score(empty, 0, 1, nullptr, &information, nullptr, nullptr, nullptr) == kArgument &&
          score(empty, 0, 1, nullptr, nullptr, nullptr, &account, nullptr) == kArgument &&
          score(empty, 0, 1, nullptr, nullptr, nullptr, nullptr, &result.value) == kArgument &&
          range(empty, 0, 1, ranks.value.data(), sizeof(ranks.value), nullptr, 0, nullptr, 0, 2, nullptr, nullptr, &result.value) == kArgument,
          "game_info_limit_and_separate_stack_option_compare_parameters_rejected");
    player.value.present = 73;
    Require(personal(empty, 0, &account, sizeof(account), &player.value, sizeof(player.value), nullptr, 0, nullptr, 0, 1, nullptr, nullptr, nullptr) == 0);
    transport->Complete(AuthoredPage(Json::array({AuthoredEntry("Bob")})));
    check(Poll(empty, &result.value) == 0 && result.value == kResponse && player.value.present == 73,
          "personal_reply_for_another_account_rejected_without_output");
    (void)DeleteRequest(empty);
    std::array<int, 32> requests{};
    bool capacity = true;
    for (auto& id : requests) { id = CreateRequest(context); capacity &= id > 0 && id != request && id != deleted; }
    check(capacity && CreateRequest(context) == kLimit, "bounded_request_registry_and_monotonic_nonreuse");
    for (const auto id : requests) { Require(DeleteRequest(id) == 0); }
    transport->online = false;
    check(CreateContext(0, 0x1000) == kLogin && DeleteContext(context) == 0, "real_unavailable_state_prevents_new_context_without_leaking_old_one");
    transport->online = true;
    std::array<int, 32> contexts{};
    capacity = true;
    for (auto& id : contexts) { id = CreateContext(0, 0x1000); capacity &= id > 0 && id != context; }
    check(capacity && CreateContext(0, 0x1000) == kLimit, "bounded_context_registry_and_no_stale_context_reuse");
    for (const auto id : contexts) { Require(DeleteContext(id) == 0); }
    std::printf("{\"event\":\"np_score_self_test\",\"passed\":%s,\"cases\":%u,\"authored_data_only\":true}\n", okay ? "true" : "false", cases);
    return okay;
}
bool ScoreSmoke(const std::filesystem::path& path, const std::filesystem::path& directory) {
    const auto config = LoadConfig(path); const auto endpoint = ParseEndpoint(config.endpoint);
    Require(config.enabled && endpoint.host == L"127.0.0.1" && !endpoint.secure && directory.is_absolute() && !std::filesystem::exists(directory));
    InitializeAuthoredUser();
    const auto client = std::make_shared<Client>(config, directory);
    Require(ConfigureNativeScore(client));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!client->State().ready && std::chrono::steady_clock::now() < deadline) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    Require(client->State().ready);
    const auto context = CreateContext(0, 0x1000), request = CreateRequest(context);
    Require(context > 0 && request > 0);
    Guarded<unsigned> rank; rank.value = 73;
    Require(Score(request, 0, 9007199254740993ll, nullptr, nullptr, &rank.value, nullptr, nullptr) == 0 && rank.value == 73);
    Require(AwaitScore(request) == 0 && rank.value == 1 && Tail(rank));
    Guarded<std::array<Rank, 3>> page; Guarded<std::array<Comment, 3>> comments;
    Guarded<unsigned> total; Guarded<std::uint64_t> date;
    Require(Range(request, 0, 1, page.value.data(), sizeof(page.value), comments.value.data(), sizeof(comments.value), nullptr, 0, 3, &date.value, &total.value, nullptr) == 0);
    Require(AwaitScore(request) == 1 && total.value == 1 && page.value[0].score == 9007199254740993ll &&
            page.value[0].account == client->State().account && Tail(page) && Tail(comments) && Tail(total) && Tail(date));
    const auto account = client->State().account;
    Guarded<PlayerRank> own;
    Require(Account(request, 0, &account, sizeof(account), &own.value, sizeof(own.value), nullptr, 0, nullptr, 0, 1, nullptr, nullptr, nullptr) == 0);
    Require(AwaitScore(request) == 1 && own.value.present == 1 && own.value.value.score == page.value[0].score && Tail(own));
    Require(Score(request, 0, 42, nullptr, nullptr, nullptr, nullptr, nullptr) == 0 && AwaitScore(request) == kNotBest);
    Require(DeleteRequest(request) == 0 && DeleteContext(context) == 0);
    client->Stop();
    std::puts("{\"event\":\"np_score_smoke\",\"passed\":true,\"authored_data_only\":true,\"native_menu_integration\":false}");
    return true;
}
bool ScoreReplaySelfTest() {
    bool okay = true; unsigned cases{};
    const auto check = [&](bool passed, const char* name) {
        std::printf("{\"event\":\"np_replay_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, passed ? "true" : "false");
        ++cases; okay &= passed;
    };
    InitializeAuthoredUser();
    const auto transport = std::make_shared<AuthoredTransport>(); Require(transport->Install());
    const auto context = CreateContext(0, 0x1000), scoring = CreateRequest(context), transfer = CreateRequest(context), listing = CreateRequest(context);
    Require(context > 0 && scoring > 0 && transfer > 0 && listing > 0);
    auto upload = reinterpret_cast<decltype(&RecordData)>(ResolveScoreImport("1gL5PwYzrrw#tag", "libSceNpScore", "libSceNpScore"));
    auto download = reinterpret_cast<decltype(&GetData)>(ResolveScoreImport("PP9jx8s0574", "libSceNpScore", "libSceNpScore"));
    check(upload && download && !ResolveScoreImport("1gL5PwYzrrw", "wrong", "libSceNpScore"), "exact_seven_argument_sysv_replay_imports");
    std::vector<std::byte> payload(70);
    for (std::size_t index = 0; index < payload.size(); ++index) { payload[index] = std::byte(index); }
    const auto original = payload;
    Guarded<unsigned> temporary; temporary.value = 73;
    Guarded<int> result; result.value = 73;
    Guarded<std::uint64_t> total; total.value = 73;
    Guarded<Rank> rank;
    const auto account = AccountId("Alice");
    check(upload(transfer, 0, 9001, payload.size(), payload.size(), payload.data(), nullptr) == kNotBest && transport->paths.empty(),
          "upload_requires_a_delivered_matching_score_receipt");
    Require(Score(scoring, 0, 9001, nullptr, nullptr, &temporary.value, nullptr, nullptr) == 0);
    check(Score(transfer, 0, 9002, nullptr, nullptr, nullptr, nullptr, nullptr) == kBusy && transport->paths.size() == 1,
          "same_board_score_receipts_are_single_flight");
    transport->Complete(AuthoredReceipt());
    check(Poll(scoring, reinterpret_cast<int*>(&temporary.value)) == kArgument && temporary.value == 73 &&
          upload(transfer, 0, 9001, payload.size(), payload.size(), payload.data(), nullptr) == kNotBest &&
          Score(scoring, 0, 9002, nullptr, nullptr, nullptr, nullptr, nullptr) == kBusy,
          "failed_poll_does_not_publish_receipt_or_allow_request_reuse");
    Require(Abort(scoring) == 0);
    check(Poll(scoring, &result.value) == 0 && result.value == kAbort &&
          upload(transfer, 0, 9001, payload.size(), payload.size(), payload.data(), nullptr) == kNotBest,
          "abort_discards_prepared_undelivered_score_receipt");
    Require(DeleteRequest(scoring) == 0);
    const auto accepted = CreateRequest(context);
    Require(Score(accepted, 0, 9001, nullptr, nullptr, &temporary.value, nullptr, nullptr) == 0);
    const auto submission = transport->bodies.back().at("submission_id");
    transport->Complete(AuthoredReceipt()); Require(Poll(accepted, &result.value) == 0 && result.value == 0);
    check(upload(transfer, 0, 9001, payload.size(), payload.size(), payload.data(), nullptr) == 0 &&
          transport->bodies.back().at("submission_id") == submission && transport->bodies.back().size() == 3 &&
          transport->uploads.back() == original && transport->durable.back(), "upload_pins_exact_entry_submission_and_owned_payload");
    payload[0] = std::byte{0xff};
    check(transport->uploads.back() == original && Poll(transfer, &result.value) == 1,
          "upload_worker_does_not_borrow_mutable_guest_bytes");
    Reply uploaded; uploaded.status = 200;
    uploaded.body = Json{{"entry_id", "0123456789abcdef0123456789abcdef"}, {"size", original.size()}, {"format", kReplayFormat}};
    transport->Complete(std::move(uploaded));
    check(Poll(transfer, &result.value) == 0 && result.value == 0 && Tail(result), "upload_completion_uses_four_byte_result_only");
    transport->online = false;
    const auto before_offline = transport->paths.size();
    check(upload(transfer, 0, 9001, payload.size(), payload.size(), payload.data(), nullptr) == 0 &&
          transport->paths.size() == before_offline + 1 && transport->durable.back() &&
          transport->bodies.back().at("submission_id") == submission,
          "offline_upload_can_stage_only_its_already_delivered_exact_receipt");
    Reply disconnected; disconnected.error = Error::Network;
    transport->Complete(std::move(disconnected));
    check(Poll(transfer, &result.value) == 0 && result.value == kServer && !transport->online,
          "offline_staging_does_not_report_server_success_or_change_online_state");
    const auto offline_paths = transport->paths.size();
    check(upload(transfer, 1, 9001, payload.size(), payload.size(), payload.data(), nullptr) == kNotBest &&
          Score(accepted, 0, 9002, nullptr, nullptr, nullptr, nullptr, nullptr) == kLogin &&
          Range(listing, 0, 1, &rank.value, sizeof(rank.value), nullptr, 0, nullptr, 0, 1, nullptr, nullptr, nullptr) == kLogin &&
          transport->paths.size() == offline_paths,
          "offline_receipt_exception_does_not_enable_new_scores_queries_or_unreceipted_uploads");
    transport->online = true;
    const auto count = transport->paths.size();
    check(upload(transfer, 0, 9002, 70, 70, payload.data(), nullptr) == kNotBest &&
          upload(transfer, 0, 9001, 70, 69, payload.data(), nullptr) == kArgument &&
          upload(transfer, 0, 9001, 0, 0, payload.data(), nullptr) == kArgument &&
          upload(transfer, 0, 9001, kReplayLimit + 1, kReplayLimit + 1, payload.data(), nullptr) == kTooLarge &&
          upload(transfer, 0, 9001, 70, 70, nullptr, nullptr) == kArgument &&
          upload(transfer, 0, 9001, 70, 70, payload.data(), &result.value) == kArgument && transport->paths.size() == count,
          "upload_rejects_wrong_score_partial_size_oversize_pointer_and_stack_option");
    Require(Score(accepted, 0, 9001, nullptr, nullptr, nullptr, nullptr, nullptr) == 0);
    transport->Complete(AuthoredReceipt(false)); Require(Poll(accepted, &result.value) == 0 && result.value == kNotBest);
    check(upload(transfer, 0, 9001, 70, 70, payload.data(), nullptr) == kNotBest,
          "same_score_not_best_attempt_does_not_inherit_old_upload_receipt");
    Require(Score(accepted, 0, 9002, nullptr, nullptr, nullptr, nullptr, nullptr) == 0);
    auto ineligible = AuthoredReceipt(); ineligible.body["upload_eligible"] = false;
    transport->Complete(std::move(ineligible)); Require(Poll(accepted, &result.value) == 0 && result.value == 0);
    check(upload(transfer, 0, 9002, 70, 70, payload.data(), nullptr) == kNotBest, "accepted_but_ineligible_score_cannot_upload");
    auto entry = AuthoredEntry(); entry["replay"] = Json{{"size", original.size()}, {"sha256", Sha256(original)}, {"format", kReplayFormat}};
    const auto page = [&] { return AuthoredPage(Json::array({entry})); };
    const auto list = [&] { return Range(listing, 0, 1, &rank.value, sizeof(rank.value), nullptr, 0, nullptr, 0, 1, nullptr, nullptr, nullptr); };
    auto* buffer = static_cast<std::byte*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Require(buffer != nullptr); std::fill(buffer, buffer + 4096, std::byte{0xa7});
    Require(list() == 0); transport->Complete(page());
    check(Poll(listing, reinterpret_cast<int*>(&rank.value)) == kArgument &&
          download(transfer, 0, account, &total.value, 17, buffer, nullptr) == kNoData,
          "undelivered_ranking_does_not_publish_attachment_selection");
    Require(Abort(listing) == 0 && DeleteRequest(listing) == 0);
    const auto refreshed = CreateRequest(context);
    Require(Range(refreshed, 0, 1, &rank.value, sizeof(rank.value), nullptr, 0, nullptr, 0, 1, nullptr, nullptr, nullptr) == 0);
    transport->Complete(page()); Require(Poll(refreshed, &result.value) == 0);
    check(rank.value.has_data == 1 && download(transfer, 0, account, &total.value, 17, buffer, nullptr) == 0 &&
          transport->paths.back() == "/entries/0123456789abcdef0123456789abcdef/replay" &&
          transport->bodies.back().at("size") == original.size() && transport->bodies.back().at("sha256") == Sha256(original) &&
          total.value == 73 && buffer[0] == std::byte{0xa7}, "download_pins_delivered_entry_size_digest_without_early_output");
    const auto waiting_fetch = transport->fulfilled;
    check(Poll(transfer, &result.value) == 1 && download(transfer, 0, account, &total.value, 17, buffer, nullptr) == kBusy,
          "pending_download_cannot_be_restarted");
    entry["entry_id"] = "fedcba9876543210fedcba9876543210"; entry["replay"]["sha256"] = std::string(64, '1');
    Require(Range(refreshed, 0, 1, &rank.value, sizeof(rank.value), nullptr, 0, nullptr, 0, 1, nullptr, nullptr, nullptr) == 0);
    transport->replies.at(waiting_fetch + 1).set_value(page()); Require(Poll(refreshed, &result.value) == 0);
    Reply downloaded; downloaded.status = 200; downloaded.bytes = original;
    transport->replies.at(waiting_fetch).set_value(std::move(downloaded)); transport->fulfilled += 2;
    DWORD previous{}; Require(VirtualProtect(buffer, 4096, PAGE_READONLY, &previous)); result.value = 73;
    check(Poll(transfer, &result.value) == kArgument && total.value == 73 && result.value == 73 && buffer[0] == std::byte{0xa7},
          "readonly_download_destination_prevents_total_result_and_cursor_commit");
    Require(VirtualProtect(buffer, 4096, PAGE_READWRITE, &previous));
    check(Poll(transfer, reinterpret_cast<int*>(&total.value)) == kArgument && total.value == 73 && buffer[0] == std::byte{0xa7},
          "overlapping_download_completion_result_does_not_advance_cursor");
    Require(Poll(transfer, &result.value) == 0 && result.value == 0);
    std::vector<std::byte> received(buffer, buffer + 17);
    check(total.value == 70 && Tail(total) && std::equal(received.begin(), received.end(), original.begin()) && buffer[17] == std::byte{0xa7},
          "first_chunk_commits_exact_prefix_and_64bit_total_after_failed_polls");
    buffer[0] = std::byte{0xff}; Require(Poll(transfer, &result.value) == 0);
    check(buffer[0] == std::byte{0xff}, "repeated_download_poll_never_rewrites_or_advances");
    check(download(transfer, 0, AccountId("Bob"), &total.value, 17, buffer, nullptr) == kArgument &&
          download(transfer, 1, account, &total.value, 17, buffer, nullptr) == kArgument &&
          download(transfer, 0, account, &total.value, 17, buffer, &result.value) == kArgument &&
          download(transfer, 0, account, reinterpret_cast<std::uint64_t*>(buffer), 17, buffer, nullptr) == kArgument,
          "continuation_rejects_account_board_option_and_output_overlap_without_reset");
    const auto fetch_count = transport->paths.size(); transport->online = false;
    while (received.size() < original.size()) {
        std::fill(buffer, buffer + 18, std::byte{0xa7});
        Require(download(transfer, 0, account, &total.value, 17, buffer, nullptr) == 0 && Poll(transfer, &result.value) == 0 && result.value == 0);
        const auto bytes = (std::min)(std::size_t{17}, original.size() - received.size());
        received.insert(received.end(), buffer, buffer + bytes);
        Require(buffer[bytes] == std::byte{0xa7});
    }
    check(received == original && transport->paths.size() == fetch_count && total.value == 70,
          "continuations_keep_original_entry_after_refresh_and_work_offline_without_more_http");
    std::fill(buffer, buffer + 18, std::byte{0xa7});
    check(download(transfer, 0, account, &total.value, 17, buffer, nullptr) == 0 && Poll(transfer, &result.value) == 0 &&
          result.value == 0 && buffer[0] == std::byte{0xa7}, "eof_returns_total_with_no_extra_data");
    transport->online = true;
    const auto late = CreateRequest(context);
    Require(download(late, 0, account, &total.value, 17, buffer, nullptr) == 0);
    check(transport->paths.back() == "/entries/fedcba9876543210fedcba9876543210/replay", "new_request_uses_latest_delivered_selection");
    Require(Abort(late) == 0); Reply discarded; discarded.status = 200; discarded.bytes = original; transport->Complete(std::move(discarded));
    check(Poll(late, &result.value) == 0 && result.value == kAbort && buffer[0] == std::byte{0xa7},
          "aborted_download_discards_late_worker_payload_and_all_destinations");
    Require(DeleteRequest(late) == 0);
    const auto malformed = CreateRequest(context);
    Require(download(malformed, 0, account, &total.value, 17, buffer, nullptr) == 0);
    Reply short_reply; short_reply.status = 200; short_reply.bytes = std::vector<std::byte>(69); transport->Complete(std::move(short_reply));
    check(Poll(malformed, &result.value) == 0 && result.value == kResponse && buffer[0] == std::byte{0xa7},
          "worker_contract_size_mismatch_cannot_reach_guest_memory");
    Require(DeleteRequest(malformed) == 0);
    const auto deleted = CreateRequest(context);
    Require(download(deleted, 0, account, &total.value, 17, buffer, nullptr) == 0 && DeleteRequest(deleted) == 0);
    Reply after_delete; after_delete.status = 200; after_delete.bytes = original; transport->Complete(std::move(after_delete));
    check(Poll(deleted, &result.value) == kId && buffer[0] == std::byte{0xa7}, "deleted_download_never_publishes_late_worker_bytes");
    const auto prepared = CreateRequest(context);
    Require(download(prepared, 0, account, &total.value, 17, buffer, nullptr) == 0);
    Reply before_abort; before_abort.status = 200; before_abort.bytes = original; transport->Complete(std::move(before_abort));
    Require(VirtualProtect(buffer, 4096, PAGE_READONLY, &previous));
    const auto failed_poll = Poll(prepared, &result.value);
    Require(Abort(prepared) == 0 && VirtualProtect(buffer, 4096, PAGE_READWRITE, &previous));
    check(failed_poll == kArgument && Poll(prepared, &result.value) == 0 && result.value == kAbort && buffer[0] == std::byte{0xa7},
          "abort_discards_download_prepared_before_a_failed_memory_commit");
    Require(DeleteRequest(prepared) == 0);
    entry["replay"] = nullptr;
    Require(Range(refreshed, 0, 1, &rank.value, sizeof(rank.value), nullptr, 0, nullptr, 0, 1, nullptr, nullptr, nullptr) == 0);
    transport->Complete(page()); Require(Poll(refreshed, &result.value) == 0);
    const auto missing = CreateRequest(context);
    check(rank.value.has_data == 0 && download(missing, 0, account, &total.value, 17, buffer, nullptr) == kNoData,
          "delivered_no_replay_row_removes_old_attachment_availability");
    check(download(missing, 0, account, nullptr, 17, buffer, nullptr) == kArgument &&
          download(missing, 0, account, &total.value, 0, buffer, nullptr) == kArgument &&
          download(missing, 0, account, &total.value, kReplayLimit + 1, buffer, nullptr) == kArgument &&
          download(missing, 0, 0, &total.value, 17, buffer, nullptr) == kArgument &&
          download(missing, 0, account, &total.value, 17, nullptr, nullptr) == kArgument,
          "download_rejects_invalid_totals_capacity_account_and_buffer_before_queueing");
    Require(DeleteRequest(missing) == 0 && DeleteRequest(accepted) == 0 && DeleteRequest(transfer) == 0 &&
            DeleteRequest(refreshed) == 0 && DeleteContext(context) == 0 && VirtualFree(buffer, 0, MEM_RELEASE));
    std::printf("{\"event\":\"np_replay_self_test\",\"passed\":%s,\"cases\":%u,\"authored_data_only\":true,\"native_playback_validated\":false}\n", okay ? "true" : "false", cases);
    return okay;
}
bool ChallengeScoreSmoke(const std::filesystem::path& path,const std::filesystem::path& directory) {
    const auto config=LoadConfig(path,"daioujou");const auto endpoint=ParseEndpoint(config.endpoint,"daioujou");
    Require(config.enabled && endpoint.host==L"127.0.0.1" && !endpoint.secure &&
        directory.is_absolute() && !std::filesystem::exists(directory));
    InitializeAuthoredUser();
    const auto client=std::make_shared<Client>(config,directory);Require(ConfigureNativeScore(client));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while (!client->State().ready && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    Require(client->State().ready);
    const auto account=client->State().account;
    const auto context=CreateContext(0,0x1000),scoring=CreateRequest(context),listing=CreateRequest(context);
    Require(context>0 && scoring>0 && listing>0);
    Guarded<std::array<Rank,10>> rows;Guarded<unsigned> total,position;
    const auto range=[&](unsigned board,unsigned start) {
        return Range(listing,board,start,rows.value.data(),sizeof(rows.value),nullptr,0,nullptr,0,10,nullptr,&total.value,nullptr);
    };
    Require(range(99,1)==0 && AwaitScore(listing)==0 && total.value==0 && Tail(rows) && Tail(total));
    Guarded<PlayerRank> personal;
    const auto mine=[&] { return Account(listing,99,&account,sizeof(account),&personal.value,sizeof(personal.value),
        nullptr,0,nullptr,0,1,nullptr,&total.value,nullptr); };
    Require(mine()==0 && AwaitScore(listing)==kRankingNotFound && !personal.value.present && Tail(personal));
    Require(Score(scoring,99,0,nullptr,nullptr,&position.value,nullptr,nullptr)==0 && AwaitScore(scoring)==0 && position.value==1);
    Require(Score(scoring,99,10003,nullptr,nullptr,&position.value,nullptr,nullptr)==0 && AwaitScore(scoring)==0 && position.value==1);
    Require(Score(scoring,99,10003,nullptr,nullptr,nullptr,nullptr,nullptr)==0 && AwaitScore(scoring)==kNotBest);
    Require(Score(scoring,99,3,nullptr,nullptr,nullptr,nullptr,nullptr)==0 && AwaitScore(scoring)==kNotBest);
    Require(range(99,1)==0 && AwaitScore(listing)==1 && total.value==1 && rows.value[0].score==10003 &&
        rows.value[0].account==account && !rows.value[0].has_data && Tail(rows) && Tail(total));
    Require(mine()==0 && AwaitScore(listing)==1 && personal.value.present==1 && personal.value.value.score==10003 &&
        !personal.value.value.has_data && personal.value.value.account==account && Tail(personal));
    Require(range(99,11)==0 && AwaitScore(listing)==0 && total.value==1);
    Require(range(0,1)==0 && AwaitScore(listing)==0 && total.value==0);
    Require(DeleteRequest(scoring)==0 && DeleteRequest(listing)==0 && DeleteContext(context)==0);
    client->Stop();
    std::puts(Json{{"event","np_challenge_smoke"},{"passed",true},{"game","daioujou"},{"board",99},
        {"native_game_execution",false},{"native_menu_integration",false},{"authored_data_only",true}}.dump().c_str());
    return true;
}
bool ScoreReplaySmoke(const std::filesystem::path& path, const std::filesystem::path& directory,
                      const std::filesystem::path& payload_path,std::string_view game) {
    const auto config = LoadConfig(path,game); const auto endpoint = ParseEndpoint(config.endpoint,game);
    const unsigned board=game=="daioujou"?6:0;
    Require(config.enabled && endpoint.host == L"127.0.0.1" && !endpoint.secure && directory.is_absolute() && !std::filesystem::exists(directory));
    std::ifstream file(payload_path, std::ios::binary); Require(bool(file));
    std::vector<std::byte> payload(kReplayLimit + 1);
    file.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    Require(!file.bad()); payload.resize(static_cast<std::size_t>(file.gcount()));
    Require(!payload.empty() && payload.size() <= kReplayLimit);
    InitializeAuthoredUser();
    const auto client = std::make_shared<Client>(config, directory); Require(ConfigureNativeScore(client));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!client->State().ready && std::chrono::steady_clock::now() < deadline) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    Require(client->State().ready);
    const auto account = client->State().account;
    const auto context = CreateContext(0, 0x1000), scoring = CreateRequest(context), transfer = CreateRequest(context), listing = CreateRequest(context);
    Require(context > 0 && scoring > 0 && transfer > 0 && listing > 0);
    Require(Score(scoring, board, 9001, nullptr, nullptr, nullptr, nullptr, nullptr) == 0 && AwaitScore(scoring) == 0);
    Require(RecordData(transfer, board, 9001, payload.size(), payload.size(), payload.data(), nullptr) == 0 && AwaitScore(transfer) == 0);
    Guarded<Rank> rank;
    Require(Range(listing, board, 1, &rank.value, sizeof(rank.value), nullptr, 0, nullptr, 0, 1, nullptr, nullptr, nullptr) == 0);
    Require(AwaitScore(listing) == 1 && rank.value.has_data == 1 && rank.value.account == account);
    Guarded<PlayerRank> personal;
    Require(Account(listing,board,&account,sizeof(account),&personal.value,sizeof(personal.value),
        nullptr,0,nullptr,0,1,nullptr,nullptr,nullptr)==0);
    Require(AwaitScore(listing)==1 && personal.value.present==1 && personal.value.value.score==9001 &&
        personal.value.value.has_data==1 && personal.value.value.account==account && Tail(personal));
    Guarded<std::array<std::byte, 17>> buffer; Guarded<std::uint64_t> total;
    buffer.value.fill(std::byte{0xa7}); total.value = 73;
    Require(GetData(transfer, board, account, &total.value, buffer.value.size(), buffer.value.data(), nullptr) == 0 && total.value == 73);
    // Refresh the same account to a newer entry while the old transfer is
    // pinned. Neither its first delivery nor continuations may change games.
    Require(Score(scoring, board, 9002, nullptr, nullptr, nullptr, nullptr, nullptr) == 0 && AwaitScore(scoring) == 0);
    const auto unavailable = CreateRequest(context);
    // This receipt belongs to 9002, but the owned replay says 9001. Metadata
    // validation must reject it on the worker before durable staging/HTTP.
    Require(RecordData(unavailable, board, 9002, payload.size(), payload.size(), payload.data(), nullptr) == 0 && AwaitScore(unavailable) == kResponse);
    Require(Range(listing, board, 1, &rank.value, sizeof(rank.value), nullptr, 0, nullptr, 0, 1, nullptr, nullptr, nullptr) == 0);
    Require(AwaitScore(listing) == 1 && rank.value.has_data == 0 && rank.value.score == 9002);
    std::vector<std::byte> received;
    for (;;) {
        Require(AwaitScore(transfer) == 0 && total.value == payload.size() && Tail(total) && Tail(buffer));
        const auto bytes = (std::min)(buffer.value.size(), payload.size() - received.size());
        received.insert(received.end(), buffer.value.begin(), buffer.value.begin() + bytes);
        Require(std::all_of(buffer.value.begin() + bytes, buffer.value.end(), [](auto value) { return value == std::byte{0xa7}; }));
        if (received.size() == payload.size()) { break; }
        buffer.value.fill(std::byte{0xa7});
        Require(GetData(transfer, board, account, &total.value, buffer.value.size(), buffer.value.data(), nullptr) == 0);
    }
    Require(received == payload);
    Require(GetData(unavailable, board, account, &total.value, buffer.value.size(), buffer.value.data(), nullptr) == kNoData);
    Require(Score(scoring, board, 9002, nullptr, nullptr, nullptr, nullptr, nullptr) == 0 && AwaitScore(scoring) == kNotBest);
    Require(RecordData(unavailable, board, 9002, payload.size(), payload.size(), payload.data(), nullptr) == kNotBest);
    Require(DeleteRequest(unavailable) == 0 && DeleteRequest(scoring) == 0 && DeleteRequest(transfer) == 0 &&
            DeleteRequest(listing) == 0 && DeleteContext(context) == 0);
    client->Stop();
    std::puts(Json{{"event","np_replay_smoke"},{"passed",true},{"game",game},{"board",board},
        {"received_sha256",Sha256(received)},{"native_game_execution",false},
        {"native_menu_integration",false},{"native_playback_validated",false}}.dump().c_str());
    return true;
}
