#include "round_delivery.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <new>
#include <stdexcept>
#include <windows.h>

namespace kt::community {
void RoundWork::Accept(RoundAcceptance value) {
    if (!accepted_done) { accepted.set_value(std::move(value)); accepted_done = true; }
}
void RoundWork::Finish(Error error, bool complete) {
    if (!first_done) { progress.error = error; progress.complete = complete; first.set_value(std::move(progress)); first_done = true; }
}
void RoundWork::Reject(Error error) { Accept({error,false,{}}); Finish(error); }
namespace {
using Clock = std::chrono::steady_clock;
void Require(bool value) { if (!value) throw std::runtime_error("Invalid round delivery response"); }
}
struct RoundDelivery::Impl {
    struct State {
        std::string id;
        std::shared_ptr<RoundWork> owner;
        Clock::time_point next{};
        unsigned attempts{};
    };
    const Config config;
    RoundJournal journal;
    std::atomic<std::size_t>& count;
    std::vector<State> states;
    std::size_t cursor{};
    Impl(Config value,const std::filesystem::path& root,std::atomic<std::size_t>& total)
        : config(std::move(value)),journal(root),count(total) {
        states.reserve(RoundJournal::kMaxRounds);
        for (const auto& r : journal.Pending()) states.push_back({r.metadata.at("id").get<std::string>(),{},Clock::now()});
        count = states.size();
    }
    void Finish(State& state,Error error,bool complete=false) {
        if (state.owner) { state.owner->Finish(error,complete); state.owner.reset(); }
    }
    void Log(const State& state,const char* operation,unsigned board,Error error) {
        std::printf("{\"event\":\"native_community_round_step\",\"reference\":\"%s\",\"operation\":\"%s\",\"board\":%u,\"error\":%u,\"host_thread_id\":%lu}\n",
                    state.id.c_str(),operation,board,static_cast<unsigned>(error),GetCurrentThreadId());
    }
    void Retry(State& state,Error error) {
        Finish(state,error);
        state.attempts=(std::min)(state.attempts+1,4u);
        state.next=Clock::now()+std::chrono::seconds(5u<<state.attempts);
    }
};
RoundDelivery::RoundDelivery(Config config,const std::filesystem::path& root,std::atomic<std::size_t>& count)
    : impl_(std::make_unique<Impl>(std::move(config),root,count)) {}
RoundDelivery::~RoundDelivery() { Stop(Error::Cancelled); }
void RoundDelivery::Stage(const std::shared_ptr<RoundWork>& work,bool online) {
    if (impl_->states.size() >= RoundJournal::kMaxRounds) { work->Reject(Error::QueueFull); return; }
    try {
        work->progress.boards.reserve(work->input.boards.size());
        for (const auto board : work->input.boards) work->progress.boards.push_back({board,{},{}});
        auto id=impl_->journal.Stage(impl_->config,std::move(work->input));
        work->progress.reference=id;
        impl_->states.push_back({id,work,Clock::now()}); impl_->count=impl_->states.size();
        const auto stored=impl_->journal.Get(id);
        RoundAcceptance accepted{Error::None,true,id};
        for (const auto& board:stored.metadata.at("boards")) accepted.submissions.emplace_back(board.at("board").get<std::uint32_t>(),board.at("submission_id").get<std::string>());
        accepted.attachment_bytes=stored.attachment.size();
        accepted.record_sha256=stored.metadata.at("record_sha256").get<std::string>();
        accepted.attachment_sha256=stored.metadata.at("attachment_sha256").get<std::string>();
        work->Accept(std::move(accepted));
        std::printf("{\"event\":\"native_community_round_staged\",\"reference\":\"%s\",\"boards\":%zu,\"durable\":true,\"host_thread_id\":%lu}\n",id.c_str(),work->progress.boards.size(),GetCurrentThreadId());
        if (!online) impl_->Finish(impl_->states.back(),Error::Network);
    } catch (const RoundInputFailure&) { work->Reject(Error::Protocol); }
    catch (...) { work->Reject(Error::Storage); throw; }
}
bool RoundDelivery::Tick(bool online,const std::function<Reply(std::string_view,const Json&)>& send) {
    if (impl_->states.empty()) return false;
    const auto size=impl_->states.size();
    for (std::size_t offset=0;offset<size;++offset) {
        const auto index=(impl_->cursor+offset)%size; auto& state=impl_->states[index];
        if (state.next>Clock::now()) continue;
        if (!impl_->journal.MatchesEndpoint(state.id,impl_->config.endpoint)) { state.next=Clock::time_point::max(); continue; }
        if (!online && !impl_->journal.Complete(state.id)) continue;
        impl_->cursor=(index+1)%size;
        try {
            if (impl_->journal.Complete(state.id)) {
                impl_->journal.Archive(state.id); impl_->Log(state,"archive",0,Error::None);
                auto owner=std::move(state.owner);
                impl_->states.erase(impl_->states.begin()+static_cast<std::ptrdiff_t>(index)); impl_->count=impl_->states.size();
                if (owner) owner->Finish(Error::None,true);
                return true;
            }
            const auto round=impl_->journal.Get(state.id);
            if (round.metadata.at("identity").is_null()) {
                auto reply=send("/identity",Json{{"nickname",round.metadata.at("configured_nickname")}});
                if (!reply) { impl_->Log(state,"identity",0,reply.error); impl_->Retry(state,reply.error); return true; }
                const auto name=reply.body.at("nickname").get<std::string>();
                const auto account=Decimal(reply.body.at("account_id").get<std::string>());
                Require(reply.body.is_object() && reply.body.size()==2 && ValidNicknameBytes(name) && name.size()<=48 && account==AccountId(name));
                impl_->journal.BindIdentity(state.id,name,account);
                if (state.owner) state.owner->progress.identity=Identity{true,name,account};
                impl_->Log(state,"identity",0,Error::None); state.attempts=0; return true;
            }
            const auto& boards=round.metadata.at("boards");
            for (std::size_t i=0;i<boards.size();++i) {
                const auto& b=boards[i]; const auto board=b.at("board").get<std::uint32_t>();
                if (b.at("score_receipt").is_null()) {
                    const auto request=impl_->journal.ScoreRequest(state.id,board); auto reply=send("/scores",request);
                    impl_->Log(state,"score",board,reply.error);
                    if (!reply) {
                        if (state.owner) state.owner->progress.boards[i].score=reply;
                        impl_->Retry(state,reply.error); return true;
                    }
                    impl_->journal.ScoreDelivered(state.id,board,request,reply.body);
                    if (state.owner) state.owner->progress.boards[i].score=std::move(reply);
                    state.attempts=0; return true;
                }
                if (b.at("score_receipt").at("upload_eligible").get<bool>() && b.at("replay_receipt").is_null()) {
                    const auto request=impl_->journal.ReplayRequest(state.id,board); auto reply=send("/replays",request);
                    impl_->Log(state,"replay",board,reply.error);
                    if (!reply) {
                        if (state.owner) state.owner->progress.boards[i].replay=reply;
                        impl_->Retry(state,reply.error); return true;
                    }
                    impl_->journal.ReplayDelivered(state.id,board,request,reply.body);
                    if (state.owner) state.owner->progress.boards[i].replay=std::move(reply);
                    state.attempts=0; return true;
                }
            }
            Require(false);
        } catch (const std::bad_alloc&) { throw; }
        catch (...) {
            if (!impl_->journal.Healthy()) throw;
            impl_->Log(state,"validation",0,Error::Protocol); impl_->Retry(state,Error::Protocol);
        }
        return true;
    }
    return false;
}
void RoundDelivery::Stop(Error error) { for (auto& state : impl_->states) impl_->Finish(state,error); }
}
