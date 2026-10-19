#include "round_journal.h"
#include "client.h"
#include <windows.h>
#include <zlib.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>
#include <chrono>
#include <thread>

namespace kt::community {
namespace {
void Require(bool value) { if (!value) { throw std::runtime_error("Community round fixture failed"); } }
void Word(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) { std::memcpy(bytes.data()+offset,&value,4); }
RoundInput Authored() {
    RoundInput result; result.boards = {0,1}; result.score = 9001; result.build = "authored-round-fixture";
    const char comment[] = "U1=4,U0=0,U3=4,U2=0,";
    std::memcpy(result.comment.data(),comment,sizeof(comment));
    result.game_info = {std::byte{'A'},std::byte{'B'}};
    // One explicitly non-playable authored state block, not a captured core.
    const std::array<unsigned char,15> state{8,'A','U','T','H','O','R','E','D',2,0,0,0,'A','B'};
    auto size = compressBound(static_cast<uLong>(state.size())); std::vector<std::byte> packed(size);
    Require(compress2(reinterpret_cast<Bytef*>(packed.data()),&size,state.data(),static_cast<uLong>(state.size()),6)==Z_OK);
    packed.resize(size); result.record.resize(kReplayRecordSize);
    Word(result.record,0,0x10001); Word(result.record,4,0x10001); Word(result.record,16,1024); Word(result.record,20,600);
    Word(result.record,24,static_cast<std::uint32_t>(packed.size())); Word(result.record,28,1); Word(result.record,32,1);
    std::memcpy(result.record.data()+48,"fixture",8); std::memcpy(result.record.data()+80,"ARCADE_01000",13);
    Word(result.record,108,9001); std::copy(packed.begin(),packed.end(),result.record.begin()+kReplayDataOffset); return result;
}
Config Configuration() { return {true,"Round-Fixture","http://127.0.0.1:32123/ketsui/api/v1"}; }
Json ScoreResponse(unsigned board, bool eligible=true) {
    return Json{{"entry_id",std::string(31,'a')+static_cast<char>('0'+board)}, {"accepted",true}, {"serial_rank",1},
                {"upload_eligible",eligible},{"account_id",std::to_string(AccountId("Round-Fixture"))}};
}
Json ReplayResponse(const Json& request) {
    return Json{{"entry_id",request.at("entry_id")},{"format",kReplayFormat},{"sha256",request.at("sha256")},
                {"size",Unbase64(request.at("payload_base64").get<std::string>(),kReplayLimit).size()}};
}
void Bind(RoundJournal& journal, const std::string& id) { journal.BindIdentity(id,"Round-Fixture",AccountId("Round-Fixture")); }
void Snapshot(RoundJournal& journal) {
    Json rounds=Json::array();
    for (const auto& r : journal.Pending()) {
        rounds.push_back(Json{{"metadata",r.metadata},{"raw_sha256",Sha256(r.record)},{"attachment_sha256",Sha256(r.attachment)},
                              {"raw_bytes",r.record.size()},{"attachment_bytes",r.attachment.size()},{"complete",journal.Complete(r.metadata.at("id").get<std::string>())}});
    }
    std::puts(Json{{"event","community_round_fixture_snapshot"},{"rounds",rounds}}.dump().c_str());
}
}
bool RoundJournalSelfTest(const std::filesystem::path& directory) {
    Require(directory.is_absolute() && !std::filesystem::exists(directory));
    unsigned cases{}; bool okay=true;
    const auto check=[&](bool value,const char* name) {
        ++cases; okay &= value;
        std::printf("{\"event\":\"community_round_journal_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n",name,value?"true":"false");
    };
    const auto rejects=[](auto operation) { try { operation(); return false; } catch (...) { return true; } };
    const auto config=Configuration(); const auto input=Authored(); std::string id; JournalRound original;
    {
        RoundJournal journal(directory/"journal"); id=journal.Stage(config,input); original=journal.Get(id);
        check(rejects([&] { RoundJournal concurrent(directory/"journal"); }),"exclusive_journal_lease_prevents_stale_parallel_writers");
        check(original.record==input.record && InspectReplayEnvelope(original.attachment).record==input.record,"whole_round_and_replay_committed_before_any_identity_or_http");
        check(original.metadata.at("identity").is_null() && original.metadata.at("configured_nickname")==config.nickname,"first_offline_stage_needs_no_account_handshake");
        std::set<std::string> ids{id}; for (const auto& b: original.metadata.at("boards")) ids.insert(b.at("submission_id").get<std::string>());
        check(ids.size()==3 && std::all_of(ids.begin(),ids.end(),Identifier),"each_board_has_an_independent_persisted_reference");
        check(rejects([&] { journal.ScoreRequest(id,0); }) && rejects([&] { journal.ReplayRequest(id,0); }) &&
              rejects([&] { journal.Archive(id); }),"unbound_or_unconfirmed_round_cannot_expose_requests_or_be_archived");
        auto other=config; other.endpoint="http://127.0.0.1:32124/ketsui/api/v1";
        check(journal.MatchesEndpoint(id,config.endpoint) && !journal.MatchesEndpoint(id,other.endpoint),"endpoint_change_never_retargets_a_staged_round");
        Bind(journal,id); Bind(journal,id);
        check(rejects([&] { journal.BindIdentity(id,"Another",AccountId("Another")); }),"bound_identity_is_immutable_and_idempotent");
        const auto request=journal.ScoreRequest(id,0);
        check(request.at("submission_id")==original.metadata.at("boards")[0].at("submission_id") && request.at("score")=="9001" &&
              Unbase64(request.at("comment_base64").get<std::string>(),64)==std::vector<std::byte>(input.comment.begin(),input.comment.end()),"score_request_uses_the_original_reference_and_exact_comment");
        auto wrong=request; wrong["submission_id"]=NewId();
        check(rejects([&] { journal.ScoreDelivered(id,0,wrong,ScoreResponse(0)); }),"same_score_under_another_submission_is_not_this_operation");
        auto bad=ScoreResponse(0); bad["account_id"]=std::to_string(AccountId("Another"));
        check(rejects([&] { journal.ScoreDelivered(id,0,request,bad); }),"wrong_identity_receipt_is_rejected");
        journal.ScoreDelivered(id,0,request,ScoreResponse(0)); journal.ScoreDelivered(id,0,request,ScoreResponse(0));
        bad=ScoreResponse(0); bad["entry_id"]=NewId();
        check(rejects([&] { journal.ScoreDelivered(id,0,request,bad); }),"score_receipt_cannot_be_replaced_by_another_entry");
        const auto replay=journal.ReplayRequest(id,0);
        check(replay.at("submission_id")==request.at("submission_id") && replay.at("entry_id")==ScoreResponse(0).at("entry_id") &&
              Unbase64(replay.at("payload_base64").get<std::string>(),kReplayLimit)==original.attachment,"replay_is_bound_to_exact_entry_and_owned_canonical_bytes");
        auto altered=replay; altered["sha256"]=Sha256("wrong");
        check(rejects([&] { journal.ReplayDelivered(id,0,altered,ReplayResponse(replay)); }),"foreign_replay_request_cannot_acknowledge_this_round");
        bad=ReplayResponse(replay); bad["size"]=1;
        check(rejects([&] { journal.ReplayDelivered(id,0,replay,bad); }),"replay_acknowledgement_size_is_checked");
        journal.ReplayDelivered(id,0,replay,ReplayResponse(replay)); journal.ReplayDelivered(id,0,replay,ReplayResponse(replay));
        check(!journal.Complete(id),"one_board_finishing_does_not_complete_the_round");
        const auto second=journal.ScoreRequest(id,1); journal.ScoreDelivered(id,1,second,ScoreResponse(1,false));
        check(journal.Complete(id) && rejects([&] { journal.ReplayRequest(id,1); }),"noneligible_board_needs_no_attachment_and_cannot_request_one");
    }
    {
        RoundJournal journal(directory/"journal"); const auto restored=journal.Get(id);
        check(restored.record==original.record && restored.attachment==original.attachment && journal.Complete(id),"restart_preserves_bodies_receipts_and_whole_round_completion");
        check(restored.metadata.at("boards")[0].at("submission_id")==original.metadata.at("boards")[0].at("submission_id"),"restart_does_not_generate_new_submission_ids");
        journal.Archive(id); check(journal.Pending().empty(),"completed_round_is_recoverably_archived");
        bool small=true; for (const auto& p: std::filesystem::recursive_directory_iterator(directory/"journal/.trash")) {
            if (p.is_regular_file() && (p.path().filename()=="record.bin" || p.path().filename()=="attachment.zlib")) small &= p.file_size()==0;
        }
        check(small,"archive_shrinks_both_large_bodies_before_retiring_the_slot");
    }
    {
        RoundJournal journal(directory/"journal"); check(journal.Pending().empty(),"archived_round_is_not_resubmitted_after_restart");
    }
    {
        RoundJournal journal(directory/"invalid");
        auto bad=input; bad.boards={0,0}; check(rejects([&] { journal.Stage(config,bad); }),"duplicate_board_rejected_before_any_commit");
        bad=input; bad.boards={60}; check(rejects([&] { journal.Stage(config,bad); }),"custom_without_rule_fingerprint_stays_unavailable");
        bad=input; bad.score=9002; check(rejects([&] { journal.Stage(config,bad); }),"record_score_and_submission_score_must_match");
        bad=input; bad.score+=(1ull<<32); check(rejects([&] { journal.Stage(config,bad); }),"full_64bit_score_is_not_truncated_to_the_record_field");
        bad=input; bad.record.resize(kReplayRecordSize-1); check(rejects([&] { journal.Stage(config,bad); }),"incomplete_raw_record_rejected");
        bad=input; bad.record[0]=std::byte{}; check(rejects([&] { journal.Stage(config,bad); }),"unsupported_core_version_rejected");
        bad=input; bad.game_info.resize(190); check(rejects([&] { journal.Stage(config,bad); }),"oversized_native_game_info_rejected");
        check(journal.Pending().empty(),"invalid_input_does_not_create_a_pending_round");
    }
    {
        RoundJournal journal(directory/"same-score"); const auto first=journal.Stage(config,input), second=journal.Stage(config,input);
        check(first!=second && journal.Get(first).metadata.at("boards")[0].at("submission_id")!=journal.Get(second).metadata.at("boards")[0].at("submission_id"),"equal_score_equal_record_rounds_still_have_distinct_operation_identity");
    }
    {
        auto large=input; std::uint32_t state=0x1977abcd;
        // Randomize only unused bytes, leaving the authored snapshot and its
        // zero-edge key directory intact. This stresses the encoder's bound.
        for (std::size_t i=2048;i<large.record.size();++i) { state^=state<<13; state^=state>>17; state^=state<<5; large.record[i]=static_cast<std::byte>(state&255); }
        RoundJournal journal(directory/"large"); const auto large_id=journal.Stage(config,large); const auto stored=journal.Get(large_id);
        check(stored.attachment.size()>500000 && stored.attachment.size()<=kReplayRecordSize+1024 &&
              InspectReplayEnvelope(stored.attachment).record==large.record,"incompressible_unused_record_bytes_fit_the_fixed_envelope_bound");
    }
    bool inject{}; std::string uncertain;
    {
        RoundJournal journal(directory/"interrupted",[&](std::string_view point) { if (inject && point=="ready") throw std::runtime_error("Authored interruption"); });
        uncertain=journal.Stage(config,input); Bind(journal,uncertain); inject=true;
        const auto request=journal.ScoreRequest(uncertain,0);
        check(rejects([&] { journal.ScoreDelivered(uncertain,0,request,ScoreResponse(0)); }),"prepared_commit_failure_is_reported_not_faked_as_acknowledged");
        check(rejects([&] { journal.Get(uncertain); }) && rejects([&] { journal.ScoreRequest(uncertain,0); }),"uncertain_disk_commit_poisoning_blocks_further_sends");
    }
    {
        RoundJournal journal(directory/"interrupted");
        check(journal.Get(uncertain).metadata.at("boards")[0].at("score_receipt")==ScoreResponse(0),"prepared_transaction_is_finished_by_reopen_before_requests_are_exposed");
    }
    {
        RoundJournal journal(directory/"capacity"); for (std::size_t i=0;i<RoundJournal::kMaxRounds;++i) journal.Stage(config,input);
        check(journal.Pending().size()==RoundJournal::kMaxRounds && rejects([&] { journal.Stage(config,input); }),"pending_round_capacity_is_bounded_and_does_not_drop_existing_rounds");
    }
    std::printf("{\"event\":\"community_round_journal_self_test\",\"passed\":%s,\"cases\":%u}\n",okay?"true":"false",cases);
    return okay;
}
bool RoundJournalFixture(const std::filesystem::path& directory, std::string_view phase) {
    constexpr char mark[]="Ketsui authored round journal fixture v1\n";
    const bool seed=phase=="seed", inspect=phase=="inspect", fresh=phase.starts_with("new_"), archive=phase.starts_with("archive_");
    const auto target=(fresh || archive || phase.starts_with("score_")) ? phase.substr(phase.find('_')+1) : std::string_view{};
    Require(seed || inspect || ((fresh || archive || phase.starts_with("score_")) &&
            (target=="file_flush" || target=="ready" || target=="previous_moved" || target=="published")));
    Require(directory.is_absolute()); const auto marker=directory/"fixture.marker";
    if (seed) {
        Require(!std::filesystem::exists(directory)); std::filesystem::create_directories(directory);
        std::ofstream out(marker,std::ios::binary); out.write(mark,sizeof(mark)-1); Require(bool(out));
    } else {
        Require(std::filesystem::is_regular_file(marker) && std::filesystem::file_size(marker)==sizeof(mark)-1);
        std::ifstream in(marker,std::ios::binary); std::string value(sizeof(mark)-1,'\0'); in.read(value.data(),static_cast<std::streamsize>(value.size())); Require(value==mark);
    }
    bool active{};
    RoundJournal journal(directory/"journal",[&](std::string_view point) {
        if (!active || point!=target) return;
        std::puts(Json{{"event","community_round_fixture_checkpoint"},{"point",phase}}.dump().c_str()); std::fflush(stdout);
        for (;;) { Sleep(INFINITE); } // Explicit marked authored fixture only.
    });
    if (seed) { const auto id=journal.Stage(Configuration(),Authored()); Bind(journal,id); Snapshot(journal); return true; }
    if (inspect) { Snapshot(journal); return true; }
    const auto pending=journal.Pending(); Require(pending.size()==1); const auto id=pending[0].metadata.at("id").get<std::string>();
    if (archive) {
        for (unsigned board=0;board<2;++board) { journal.ScoreDelivered(id,board,journal.ScoreRequest(id,board),ScoreResponse(board,false)); }
    }
    active=true;
    if (fresh) journal.Stage(Configuration(),Authored());
    else if (archive) journal.Archive(id);
    else journal.ScoreDelivered(id,0,journal.ScoreRequest(id,0),ScoreResponse(0));
    throw std::runtime_error("Authored round checkpoint was not reached");
}
bool RoundJournalSmoke(const std::filesystem::path& config_path, const std::filesystem::path& directory, std::string_view phase) {
    const auto config=LoadConfig(config_path); const auto endpoint=ParseEndpoint(config.endpoint);
    const bool create=phase=="create" || phase=="stage";
    Require(config.enabled && endpoint.host==L"127.0.0.1" && !endpoint.secure && directory.is_absolute() && (create || phase=="drain"));
    constexpr char mark[]="Ketsui authored round journal HTTP fixture v1\n"; const auto marker=directory/"fixture.marker";
    if (create) {
        Require(!std::filesystem::exists(directory)); std::filesystem::create_directories(directory);
        std::ofstream out(marker,std::ios::binary); out.write(mark,sizeof(mark)-1); Require(bool(out));
    } else {
        Require(std::filesystem::is_regular_file(marker) && std::filesystem::file_size(marker)==sizeof(mark)-1);
        std::ifstream in(marker,std::ios::binary); std::string value(sizeof(mark)-1,'\0'); in.read(value.data(),static_cast<std::streamsize>(value.size())); Require(value==mark);
    }
    RoundJournal journal(directory/"journal"); if (create) journal.Stage(config,Authored());
    if (phase=="stage") {
        std::puts("{\"event\":\"community_round_journal_staged\",\"passed\":true,\"authored_data_only\":true,\"http_started\":false}"); return true;
    }
    // Refuse changed endpoint/name before constructing transport. A production
    // worker will resolve the persisted name explicitly instead of rebinding it.
    for (const auto& r: journal.Pending()) {
        Require(journal.MatchesEndpoint(r.metadata.at("id").get<std::string>(),config.endpoint) && r.metadata.at("configured_nickname")==config.nickname);
    }
    Client client(config,directory/"transport"); const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while (!client.State().ready && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto identity=client.State(); Require(identity.ready);
    const auto await=[](std::future<Reply> future) { Require(future.wait_for(std::chrono::seconds(10))==std::future_status::ready); auto reply=future.get(); Require(bool(reply)); return reply; };
    for (const auto& r: journal.Pending()) {
        const auto id=r.metadata.at("id").get<std::string>(); journal.BindIdentity(id,identity.nickname,identity.account);
        for (const auto& b: r.metadata.at("boards")) {
            const auto board=b.at("board").get<std::uint32_t>();
            if (b.at("score_receipt").is_null()) {
                const auto request=journal.ScoreRequest(id,board); const auto reply=await(client.Request("/scores",request));
                journal.ScoreDelivered(id,board,request,reply.body);
            }
            const auto current=journal.Get(id); const auto& live_board=current.metadata.at("boards")[board==0 ? 0 : 1];
            if (live_board.at("score_receipt").at("upload_eligible").get<bool>() && live_board.at("replay_receipt").is_null()) {
                const auto request=journal.ReplayRequest(id,board);
                const Json upload{{"entry_id",request.at("entry_id")},{"submission_id",request.at("submission_id")},{"nickname",request.at("nickname")}};
                const auto reply=await(client.UploadReplayEnvelope(upload,current.attachment,ReplayExpectation{board,9001}));
                journal.ReplayDelivered(id,board,request,reply.body);
            }
        }
        Require(journal.Complete(id)); journal.Archive(id);
    }
    client.Stop();
    std::puts("{\"event\":\"community_round_journal_smoke\",\"passed\":true,\"authored_data_only\":true,\"native_game_integration\":false}"); return true;
}
bool ClientRoundSmoke(const std::filesystem::path& config_path,const std::filesystem::path& directory,std::string_view phase) {
    const auto config=LoadConfig(config_path); const auto endpoint=ParseEndpoint(config.endpoint);
    Require(config.enabled && endpoint.host==L"127.0.0.1" && !endpoint.secure && directory.is_absolute());
    Require(phase=="create" || phase=="drain" || phase=="offline" || phase=="failure" || phase=="cancel" || phase=="queue-cancel" || phase=="invalid" || phase=="retained" || phase=="disabled" || phase=="mode-off" || phase=="not-best" || phase=="recover" || phase=="capacity" || phase=="storage" || phase=="proof");
    const bool existing=phase=="drain" || phase=="retained" || phase=="storage";
    constexpr char mark[]="Ketsui authored worker round fixture v1\n"; const auto marker=directory/"fixture.marker";
    if (!existing) {
        Require(!std::filesystem::exists(directory)); std::filesystem::create_directories(directory);
        std::ofstream out(marker,std::ios::binary); out.write(mark,sizeof(mark)-1); Require(bool(out));
    } else {
        Require(std::filesystem::is_regular_file(marker) && std::filesystem::file_size(marker)==sizeof(mark)-1);
        std::ifstream in(marker,std::ios::binary); std::string value(sizeof(mark)-1,'\0'); in.read(value.data(),static_cast<std::streamsize>(value.size())); Require(value==mark);
    }
    const auto root=directory/"transport";
    if (phase=="disabled" || phase=="mode-off") {
        auto disabled=config; disabled.enabled=phase!="disabled"; Client client(disabled,root,phase!="mode-off"); const auto submitted=client.SubmitRound(Authored());
        Require(submitted.accepted.get().error==Error::Disabled && submitted.first_attempt.get().error==Error::Disabled);
        client.Stop(); Require(!std::filesystem::exists(root/"community-rounds") && (phase!="disabled" || !std::filesystem::exists(root)));
        std::puts("{\"event\":\"community_round_client_smoke\",\"passed\":true,\"disabled\":true,\"authored_data_only\":true}"); return true;
    }
    Client client(config,root,true);
    if (phase=="storage") {
        const auto rejected=client.SubmitRound(Authored());
        Require(rejected.accepted.wait_for(std::chrono::seconds(10))==std::future_status::ready);
        Require(rejected.accepted.get().error==Error::Storage && !rejected.accepted.get().durable && rejected.first_attempt.get().error==Error::Storage);
        client.Stop(); std::puts("{\"event\":\"community_round_client_smoke\",\"passed\":true,\"storage_error\":true}"); return true;
    }
    if (phase=="capacity") {
        for (std::size_t i=0;i<=RoundJournal::kMaxRounds;++i) {
            const auto submitted=client.SubmitRound(Authored()); const auto accepted=submitted.accepted.get(); const auto first=submitted.first_attempt.get();
            if (i<RoundJournal::kMaxRounds) Require(accepted.durable && accepted.error==Error::None && first.error==Error::Network);
            else Require(!accepted.durable && accepted.error==Error::QueueFull && first.error==Error::QueueFull);
        }
        Require(client.PendingRoundCount()==RoundJournal::kMaxRounds); client.Stop();
        std::puts("{\"event\":\"community_round_client_smoke\",\"passed\":true,\"pending_rounds\":16,\"capacity_rejected\":true}"); return true;
    }
    if (phase=="queue-cancel") {
        std::vector<RoundSubmission> submissions; for (unsigned i=0;i<18;++i) submissions.push_back(client.SubmitRound(Authored()));
        std::puts("{\"event\":\"community_round_queue_waiting\"}"); std::fflush(stdout); Require(std::getchar()=='!');
        const auto began=std::chrono::steady_clock::now(); client.Stop(); unsigned cancelled{},full{};
        for (auto& s:submissions) {
            const auto accepted=s.accepted.get(); const auto attempted=s.first_attempt.get(); Require(!accepted.durable && accepted.error==attempted.error);
            cancelled+=accepted.error==Error::Cancelled; full+=accepted.error==Error::QueueFull;
        }
        Require(cancelled==16 && full==2);
        std::puts(Json{{"event","community_round_client_smoke"},{"passed",true},{"cancelled",cancelled},{"queue_full",full},
                       {"stop_ms",std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-began).count()}}.dump().c_str()); return true;
    }
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(12);
    if (existing) {
        while ((!client.State().ready || (phase=="drain" ? client.PendingRoundCount()!=0 : client.PendingRoundCount()==0)) && std::chrono::steady_clock::now()<until)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Require(client.State().ready && (phase=="drain" ? client.PendingRoundCount()==0 : client.PendingRoundCount()==1));
        if (phase=="retained") std::this_thread::sleep_for(std::chrono::milliseconds(600));
        const auto pending=client.PendingRoundCount(); client.Stop();
        std::puts(Json{{"event","community_round_client_smoke"},{"passed",true},{"pending_rounds",pending},{"authored_data_only",true},
                       {"native_game_integration",false}}.dump().c_str()); return true;
    }
    if (phase!="offline") {
        while (!client.State().ready && std::chrono::steady_clock::now()<until) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Require(client.State().ready);
    }
    const auto before=client.State();
    if (phase=="invalid") {
        auto bad=Authored(); bad.score++;
        const auto rejected=client.SubmitRound(std::move(bad)); Require(rejected.accepted.get().error==Error::Protocol && rejected.first_attempt.get().error==Error::Protocol);
    }
    const auto submitted=client.SubmitRound(Authored());
    Require(submitted.accepted.wait_for(std::chrono::seconds(10))==std::future_status::ready);
    const auto accepted=submitted.accepted.get(); Require(accepted.error==Error::None && accepted.durable && Identifier(accepted.reference));
    std::puts("{\"event\":\"community_round_client_accepted\",\"durable\":true}"); std::fflush(stdout);
    if (phase=="cancel") { Require(std::getchar()=='!'); client.Stop(); }
    Require(submitted.first_attempt.wait_for(std::chrono::seconds(10))==std::future_status::ready);
    const auto attempted=submitted.first_attempt.get(); Require(attempted.reference==accepted.reference && attempted.boards.size()==2);
    if (attempted.identity) Require(attempted.identity->ready && ValidNicknameBytes(attempted.identity->nickname) &&
                                   attempted.identity->account==AccountId(attempted.identity->nickname));
    if (phase=="proof") {
        const auto raw=Authored().record; auto size=compressBound(static_cast<uLong>(raw.size())); std::vector<std::byte> original(size);
        Require(compress2(reinterpret_cast<Bytef*>(original.data()),&size,reinterpret_cast<const Bytef*>(raw.data()),static_cast<uLong>(raw.size()),0)==Z_OK); original.resize(size);
        auto proved=client.ProveRoundReplay(submitted,0,9001,original).get();
        Require(bool(proved) && proved.body==attempted.boards[0].replay->body && original.size()!=accepted.attachment_bytes);
        Require(client.ProveRoundReplay(submitted,0,9002,original).get().error==Error::Protocol);
        auto other=raw; other[300]=std::byte{1}; auto capacity=compressBound(static_cast<uLong>(other.size())); std::vector<std::byte> wrong(capacity);
        Require(compress2(reinterpret_cast<Bytef*>(wrong.data()),&capacity,reinterpret_cast<const Bytef*>(other.data()),static_cast<uLong>(other.size()),6)==Z_OK); wrong.resize(capacity);
        Require(client.ProveRoundReplay(submitted,0,9001,std::move(wrong)).get().error==Error::Protocol);
    }
    Json statuses=Json::array(); for (const auto& b:attempted.boards) statuses.push_back(Json{{"board",b.board},
        {"score_status",b.score ? Json(b.score->status) : Json(nullptr)},{"replay_status",b.replay ? Json(b.replay->status) : Json(nullptr)}});
    if (phase=="recover") {
        Require(attempted.error==Error::Network && !attempted.complete && client.PendingRoundCount()==1);
        std::puts("{\"event\":\"community_round_client_first_failure\"}"); std::fflush(stdout); Require(std::getchar()=='!');
        const auto recovery_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while (client.PendingRoundCount() && std::chrono::steady_clock::now()<recovery_deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Require(client.PendingRoundCount()==0 && submitted.first_attempt.get().error==attempted.error && !submitted.first_attempt.get().complete);
    }
    else if (phase=="offline") Require(attempted.error==Error::Network && !attempted.complete && client.PendingRoundCount()==1);
    else if (phase=="failure") Require(attempted.error!=Error::None && !attempted.complete && client.PendingRoundCount()==1);
    else if (phase=="cancel") Require(attempted.error==Error::Cancelled && !attempted.complete && client.PendingRoundCount()==1);
    else {
        Require(attempted.error==Error::None && attempted.complete && client.PendingRoundCount()==0);
        if (phase=="not-best") Require(std::all_of(attempted.boards.begin(),attempted.boards.end(),[](const auto& b) { return b.score && bool(*b.score) && b.score->body.at("accepted")==false && !b.replay; }));
        else Require(std::all_of(attempted.boards.begin(),attempted.boards.end(),[](const auto& b) { return b.score && bool(*b.score) && b.replay && bool(*b.replay); }));
        const auto after=client.State(); Require(after.nickname==before.nickname && after.account==before.account);
    }
    const auto pending=client.PendingRoundCount(); client.Stop();
    std::puts(Json{{"event","community_round_client_smoke"},{"passed",true},{"durable",accepted.durable},{"first_attempt_complete",attempted.complete},
                   {"first_attempt_error",static_cast<unsigned>(attempted.error)},{"first_identity_verified",attempted.identity.has_value()},
                   {"pending_rounds",pending},{"statuses",statuses},
                   {"caller_thread_id",GetCurrentThreadId()},{"authored_data_only",true},{"native_game_integration",false}}.dump().c_str()); return true;
}
}
