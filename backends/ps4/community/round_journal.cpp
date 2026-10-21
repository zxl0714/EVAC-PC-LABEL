#include "round_journal.h"
#include "../storage/save_store.h"
#include <windows.h>
#include <algorithm>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>
#include <utility>

namespace kt::community {
namespace {
constexpr std::size_t kMetadataLimit = 32 * 1024, kEncodedLimit = kReplayRecordSize + 1024;
constexpr std::uint64_t kSlotBlocks = 96; // Save Store's minimum logical quota.
void Require(bool value) { if (!value) { throw std::runtime_error("Invalid community round journal"); } }
void Fields(const Json& value, std::initializer_list<std::string_view> names) {
    Require(value.is_object() && value.size() == names.size());
    for (const auto name : names) { Require(value.contains(name)); }
}
bool Digest(std::string_view text) {
    return text.size() == 64 && std::all_of(text.begin(), text.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
std::string Slot(std::string_view id) {
    Require(Identifier(id));
    constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyz234567";
    unsigned accumulator{}, bits{};
    std::string slot = "r";
    for (const auto c : id) {
        accumulator = (accumulator << 4) | static_cast<unsigned>(c <= '9' ? c - '0' : c - 'a' + 10);
        bits += 4;
        if (bits >= 5) { bits -= 5; slot += alphabet[(accumulator >> bits) & 31]; }
    }
    if (bits) { slot += alphabet[(accumulator << (5 - bits)) & 31]; }
    return slot;
}
std::uint64_t Number(const Json& value, std::uint64_t limit) {
    Require(value.is_number_integer());
    if (value.is_number_unsigned()) { const auto n = value.get<std::uint64_t>(); Require(n <= limit); return n; }
    const auto n = value.get<std::int64_t>(); Require(n >= 0 && static_cast<std::uint64_t>(n) <= limit);
    return static_cast<std::uint64_t>(n);
}
const Json& Board(const Json& metadata, std::uint32_t board) {
    const auto& boards = metadata.at("boards");
    const auto found = std::find_if(boards.begin(), boards.end(), [&](const auto& b) { return b.at("board") == board; });
    Require(found != boards.end()); return *found;
}
Json& Board(Json& metadata, std::uint32_t board) { return const_cast<Json&>(Board(std::as_const(metadata), board)); }
bool CompleteMetadata(const Json& metadata) {
    for (const auto& board : metadata.at("boards")) {
        if (board.at("score_receipt").is_null()) { return false; }
        if (board.at("score_receipt").at("upload_eligible").template get<bool>() && board.at("replay_receipt").is_null()) { return false; }
    }
    return true;
}
void ScoreReceipt(const Json& metadata, const Json& receipt) {
    Fields(receipt, {"entry_id", "accepted", "serial_rank", "upload_eligible", "account_id"});
    Require(!metadata.at("identity").is_null() && Identifier(receipt.at("entry_id").get<std::string>()) &&
            receipt.at("accepted").is_boolean() && receipt.at("upload_eligible").is_boolean());
    Require(Number(receipt.at("serial_rank"), 100000) != 0 &&
            (receipt.at("accepted").get<bool>() || !receipt.at("upload_eligible").get<bool>()));
    Require(receipt.at("account_id") == metadata.at("identity").at("account_id"));
}
void ReplayReceipt(const Json& metadata, const Json& board, const Json& receipt, std::size_t bytes) {
    Fields(receipt, {"entry_id", "format", "sha256", "size"});
    const auto& score = board.at("score_receipt");
    Require(!score.is_null() && score.at("upload_eligible").get<bool>() &&
            receipt.at("entry_id") == score.at("entry_id") && receipt.at("format").get<std::string>() == kReplayFormat &&
            receipt.at("sha256") == metadata.at("attachment_sha256") && Number(receipt.at("size"), kEncodedLimit) == bytes);
}
void Validate(const JournalRound& round) {
    const auto& m = round.metadata;
    Fields(m, {"format", "state", "id", "endpoint_sha256", "configured_nickname", "identity", "build", "score",
               "comment_base64", "game_info_base64", "record_sha256", "attachment_sha256", "boards"});
    Require(m.at("format") == 1 && m.at("state") == "pending" && Identifier(m.at("id").get<std::string>()) &&
            Digest(m.at("endpoint_sha256").get<std::string>()) && ValidNicknameBytes(m.at("configured_nickname").get<std::string>()));
    const auto build = m.at("build").get<std::string>();
    Require(!build.empty() && build.size() <= 80 && std::all_of(build.begin(), build.end(), [](unsigned char c) { return c >= 0x20 && c <= 0x7e; }));
    Require(Unbase64(m.at("comment_base64").get<std::string>(), 64).size() == 64);
    (void)Unbase64(m.at("game_info_base64").get<std::string>(), 189);
    const auto score = Decimal(m.at("score").get<std::string>());
    if (!m.at("identity").is_null()) {
        const auto& identity = m.at("identity"); Fields(identity, {"nickname", "account_id"});
        const auto name = identity.at("nickname").get<std::string>();
        Require(ValidNicknameBytes(name) && name.size() <= 48 && Decimal(identity.at("account_id").get<std::string>()) == AccountId(name));
    }
    Require(round.record.size() == kReplayRecordSize && !round.attachment.empty() && round.attachment.size() <= kEncodedLimit &&
            Sha256(round.record) == m.at("record_sha256").get<std::string>() && Sha256(round.attachment) == m.at("attachment_sha256").get<std::string>());
    const auto replay = InspectReplayEnvelope(round.attachment); Require(replay.record == round.record);
    const auto& boards = m.at("boards"); Require(boards.is_array() && !boards.empty() && boards.size() <= 18);
    std::set<std::uint32_t> seen; std::set<std::string> references, entries;
    Require(references.insert(m.at("id").get<std::string>()).second);
    for (const auto& b : boards) {
        Fields(b, {"board", "submission_id", "score_receipt", "replay_receipt"});
        const auto number = static_cast<std::uint32_t>(Number(b.at("board"), 62));
        const auto reference = b.at("submission_id").get<std::string>();
        Require(number % 10 <= 2 && seen.insert(number).second && Identifier(reference) && references.insert(reference).second);
        MatchReplayMetadata(replay, {number, score});
        if (!b.at("score_receipt").is_null()) {
            ScoreReceipt(m, b.at("score_receipt")); Require(entries.insert(b.at("score_receipt").at("entry_id").get<std::string>()).second);
        }
        if (!b.at("replay_receipt").is_null()) { ReplayReceipt(m, b, b.at("replay_receipt"), round.attachment.size()); }
    }
    Require(m.dump().size() <= kMetadataLimit);
}
void Archived(const Json& metadata) {
    Fields(metadata, {"format", "state", "id", "receipts"});
    const auto& boards = metadata.at("receipts"); Require(boards.is_array() && !boards.empty() && boards.size() <= 18);
    std::set<std::uint32_t> seen; std::set<std::string> references, entries;
    std::string account;
    for (const auto& b : boards) {
        Fields(b, {"board", "submission_id", "score_receipt", "replay_receipt"});
        const auto board = Number(b.at("board"), 62);
        Require(board % 10 <= 2 && seen.insert(static_cast<std::uint32_t>(board)).second &&
                Identifier(b.at("submission_id").get<std::string>()) && references.insert(b.at("submission_id").get<std::string>()).second);
        const auto& score = b.at("score_receipt");
        Fields(score, {"entry_id", "accepted", "serial_rank", "upload_eligible", "account_id"});
        Require(Identifier(score.at("entry_id").get<std::string>()) && entries.insert(score.at("entry_id").get<std::string>()).second &&
                score.at("accepted").is_boolean() && score.at("upload_eligible").is_boolean() && Number(score.at("serial_rank"),100000));
        const auto name = score.at("account_id").get<std::string>(); Require(Decimal(name) && (account.empty() || account == name)); account = name;
        if (score.at("upload_eligible").get<bool>()) {
            Require(score.at("accepted").get<bool>());
            const auto& replay = b.at("replay_receipt"); Fields(replay, {"entry_id", "format", "sha256", "size"});
            Require(replay.at("entry_id") == score.at("entry_id") && replay.at("format").get<std::string>() == kReplayFormat &&
                    Digest(replay.at("sha256").get<std::string>()) && Number(replay.at("size"),kEncodedLimit));
        } else { Require(b.at("replay_receipt").is_null()); }
    }
}
struct JournalLease {
    HANDLE handle = INVALID_HANDLE_VALUE;
    explicit JournalLease(const std::filesystem::path& root) {
        Require(root.is_absolute()); kt::save::Store::CreateRoot(root);
        handle = CreateFileW((root/".round-journal.lock").c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,
                             OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        Require(handle != INVALID_HANDLE_VALUE);
        FILE_ATTRIBUTE_TAG_INFO info{};
        if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandleEx(handle,FileAttributeTagInfo,&info,sizeof(info)) ||
            (info.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))) {
            CloseHandle(handle); handle = INVALID_HANDLE_VALUE; Require(false);
        }
    }
    ~JournalLease() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
}
struct RoundJournal::Impl {
    JournalLease lease;
    kt::save::Store store;
    std::vector<JournalRound> rounds;
    bool poisoned{};
    explicit Impl(const std::filesystem::path& root, std::function<void(std::string_view)> checkpoint)
        : lease(root), store(root, std::move(checkpoint)) {
        for (const auto& entry : store.Search("%", kt::save::SortKey::Name, false)) {
            bool created{}; auto mount = store.MountSlot(entry.name, false, false, false, 0, created);
            const auto text = Read(mount, "round.json", kMetadataLimit);
            auto m = ParseJson(std::string_view(reinterpret_cast<const char*>(text.data()), text.size()), kMetadataLimit);
            Require(m.is_object() && m.at("format") == 1 && Slot(m.at("id").get<std::string>()) == entry.name);
            if (m.at("state") == "delivered") {
                Archived(m); Require(Read(mount,"record.bin",0).empty() && Read(mount,"attachment.zlib",0).empty());
                store.Unmount(mount); store.Delete(entry.name); continue;
            }
            Require(rounds.size() < kMaxRounds);
            JournalRound round{std::move(m), Read(mount, "record.bin", kReplayRecordSize), Read(mount, "attachment.zlib", kEncodedLimit)};
            store.Unmount(mount); Validate(round); Unique(round); rounds.push_back(std::move(round));
        }
    }
    void Ready() const { Require(!poisoned); }
    void Unique(const JournalRound& round) const {
        std::set<std::string> references{round.metadata.at("id").get<std::string>()};
        for (const auto& b : round.metadata.at("boards")) references.insert(b.at("submission_id").get<std::string>());
        for (const auto& old : rounds) {
            Require(!references.contains(old.metadata.at("id").get<std::string>()));
            for (const auto& b : old.metadata.at("boards")) Require(!references.contains(b.at("submission_id").get<std::string>()));
        }
    }
    std::size_t Index(std::string_view id) const {
        Ready(); const auto it = std::find_if(rounds.begin(), rounds.end(), [&](const JournalRound& r) { return r.metadata.at("id").get_ref<const std::string&>() == id; });
        Require(it != rounds.end()); return static_cast<std::size_t>(it - rounds.begin());
    }
    std::vector<std::byte> Read(const std::shared_ptr<kt::save::Mount>& mount, std::string_view name, std::size_t limit) {
        auto file = store.Open(mount, name, {.read=true}); const auto size = file->Info().size; Require(size <= limit);
        std::vector<std::byte> bytes(static_cast<std::size_t>(size)); Require(file->ReadAt(0, bytes) == bytes.size()); return bytes;
    }
    void WriteFile(const std::shared_ptr<kt::save::Mount>& mount, std::string_view name, std::span<const std::byte> bytes) {
        auto file = store.Open(mount, name, {.write=true,.create=true,.truncate=true}); std::uint64_t end{};
        Require(file->WriteAt(0, bytes, end) == bytes.size()); file->Flush();
    }
    void Write(const JournalRound& round, bool create, bool archive = false) {
        Ready();
        try {
            bool created{}; auto mount = store.MountSlot(Slot(round.metadata.at("id").get<std::string>()), true, create, create, kSlotBlocks, created);
            const auto text = round.metadata.dump(); Require(text.size() <= kMetadataLimit);
            WriteFile(mount, "round.json", std::as_bytes(std::span(text.data(), text.size())));
            if (create || archive) {
                WriteFile(mount, "record.bin", archive ? std::span<const std::byte>{} : std::span<const std::byte>(round.record));
                WriteFile(mount, "attachment.zlib", archive ? std::span<const std::byte>{} : std::span<const std::byte>(round.attachment));
            }
            store.Unmount(mount);
        } catch (...) { poisoned = true; throw; }
    }
    void Update(std::size_t index, JournalRound round) { Validate(round); Write(round, false); rounds[index] = std::move(round); }
};
RoundJournal::RoundJournal(const std::filesystem::path& root, std::function<void(std::string_view)> checkpoint)
    : impl_(std::make_unique<Impl>(root, std::move(checkpoint))) {}
RoundJournal::~RoundJournal() = default;
std::string RoundJournal::Stage(const Config& config, RoundInput input) {
    impl_->Ready(); Require(impl_->rounds.size() < kMaxRounds);
    JournalRound round; const auto id = NewId();
    try {
        Require(config.enabled && ValidNicknameBytes(config.nickname));
        Require(!input.boards.empty() && input.boards.size() <= 18 && input.game_info.size() <= 189);
        (void)ParseEndpoint(config.endpoint);
        round.record = std::move(input.record); round.attachment = EncodeReplayEnvelope(round.record);
        Json boards = Json::array();
        for (const auto board : input.boards) { boards.push_back(Json{{"board",board},{"submission_id",NewId()},{"score_receipt",nullptr},{"replay_receipt",nullptr}}); }
        round.metadata = Json{{"format",1},{"state","pending"},{"id",id},{"endpoint_sha256",Sha256(config.endpoint)},
        {"configured_nickname",config.nickname},{"identity",nullptr},{"build",input.build},{"score",std::to_string(input.score)},
        {"comment_base64",Base64(input.comment)},{"game_info_base64",Base64(input.game_info)},
        {"record_sha256",Sha256(round.record)},{"attachment_sha256",Sha256(round.attachment)},{"boards",std::move(boards)}};
        Validate(round); impl_->Unique(round);
    } catch (const std::bad_alloc&) { throw; }
    catch (const std::exception&) { throw RoundInputFailure("Invalid owned round input"); }
    // Allocate before committing: a successful disk commit cannot be hidden by
    // a vector-growth failure before the caller receives its fixed reference.
    impl_->rounds.reserve(impl_->rounds.size()+1); impl_->Write(round, true); impl_->rounds.push_back(std::move(round)); return id;
}
std::vector<JournalRound> RoundJournal::Pending() const { impl_->Ready(); return impl_->rounds; }
JournalRound RoundJournal::Get(std::string_view id) const { return impl_->rounds[impl_->Index(id)]; }
bool RoundJournal::Healthy() const noexcept { return !impl_->poisoned; }
bool RoundJournal::MatchesEndpoint(std::string_view id, std::string_view endpoint) const {
    (void)ParseEndpoint(endpoint); return impl_->rounds[impl_->Index(id)].metadata.at("endpoint_sha256") == Sha256(endpoint);
}
void RoundJournal::BindIdentity(std::string_view id, std::string nickname, std::uint64_t account) {
    const auto index = impl_->Index(id); auto round = impl_->rounds[index];
    Require(ValidNicknameBytes(nickname) && nickname.size() <= 48 && AccountId(nickname) == account);
    const Json identity{{"nickname",std::move(nickname)},{"account_id",std::to_string(account)}};
    if (!round.metadata.at("identity").is_null()) { Require(round.metadata.at("identity") == identity); return; }
    round.metadata["identity"] = identity; impl_->Update(index, std::move(round));
}
Json RoundJournal::ScoreRequest(std::string_view id, std::uint32_t board) const {
    const auto& m = impl_->rounds[impl_->Index(id)].metadata; const auto& b = Board(m,board); Require(!m.at("identity").is_null());
    return Json{{"submission_id",b.at("submission_id")},{"nickname",m.at("identity").at("nickname")},{"board",board},
                {"rules","standard"},{"score",m.at("score")},{"comment_base64",m.at("comment_base64")},
                {"game_info_base64",m.at("game_info_base64")},{"build",m.at("build")}};
}
Json RoundJournal::ReplayRequest(std::string_view id, std::uint32_t board) const {
    const auto& r = impl_->rounds[impl_->Index(id)]; const auto& m = r.metadata; const auto& b = Board(m,board);
    Require(!b.at("score_receipt").is_null() && b.at("score_receipt").at("upload_eligible").get<bool>());
    return Json{{"submission_id",b.at("submission_id")},{"entry_id",b.at("score_receipt").at("entry_id")},
                {"nickname",m.at("identity").at("nickname")},{"format",kReplayFormat},
                {"sha256",m.at("attachment_sha256")},{"payload_base64",Base64(r.attachment)}};
}
void RoundJournal::ScoreDelivered(std::string_view id, std::uint32_t board, const Json& request, const Json& receipt) {
    const auto index = impl_->Index(id); Require(request == ScoreRequest(id,board)); auto round = impl_->rounds[index];
    ScoreReceipt(round.metadata,receipt); auto& stored = Board(round.metadata,board)["score_receipt"];
    if (!stored.is_null()) { Require(stored == receipt); return; }
    stored = receipt; impl_->Update(index,std::move(round));
}
void RoundJournal::ReplayDelivered(std::string_view id, std::uint32_t board, const Json& request, const Json& receipt) {
    const auto index = impl_->Index(id); Require(request == ReplayRequest(id,board)); auto round = impl_->rounds[index];
    auto& b = Board(round.metadata,board); ReplayReceipt(round.metadata,b,receipt,round.attachment.size());
    auto& stored = b["replay_receipt"]; if (!stored.is_null()) { Require(stored == receipt); return; }
    stored = receipt; impl_->Update(index,std::move(round));
}
bool RoundJournal::Complete(std::string_view id) const { return CompleteMetadata(impl_->rounds[impl_->Index(id)].metadata); }
void RoundJournal::Archive(std::string_view id) {
    const auto index = impl_->Index(id); const auto& round = impl_->rounds[index]; Require(CompleteMetadata(round.metadata));
    JournalRound archived{Json{{"format",1},{"state","delivered"},{"id",id},{"receipts",round.metadata.at("boards")}}, {}, {}};
    impl_->Write(archived,false,true);
    try { impl_->store.Delete(Slot(id)); } catch (...) { impl_->poisoned = true; throw; }
    impl_->rounds.erase(impl_->rounds.begin()+static_cast<std::ptrdiff_t>(index));
}
}
