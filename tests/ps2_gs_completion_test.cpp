#include "gs_completion_mailbox.h"
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>

namespace {
using namespace std::chrono_literals;
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void MaskAndReset() {
    cave::ps2::GsCompletionMailbox mailbox;
    unsigned wakes=0;mailbox.SetNotifier([&]{++wakes;});
    const auto first=mailbox.Generation();
    mailbox.Finish(first);mailbox.Finish(first);
    mailbox.Label(first,0x00ff00ff00120034ull);
    mailbox.Label(first,0x0000ffff0000abcdull);
    Require(wakes==1,"Completion notifications did not coalesce one unread batch");
    auto events=mailbox.Take();
    Require(events.finish && events.label && !events.signal && events.generation==first,"Completion flags/generation changed");
    Require(events.labelPayload==0x00ffffff0012abcdull,"Masked label composition changed");
    Require(!mailbox.Take(),"Completion was consumed twice");
    mailbox.Finish(first);const auto second=mailbox.Reset();
    mailbox.Finish(first);mailbox.Label(first,~uint64_t{});
    Require(second!=first && !mailbox.Take(),"Reset delivered stale GPU completion");
    mailbox.Finish(second);Require(mailbox.Take().finish,"New generation did not deliver completion");
    Require(wakes==3,"New generation did not wake the EE event consumer");
    mailbox.Finish(second);mailbox.AcknowledgeStatus(2);
    Require(!mailbox.Take(),"CSR clear restored an already acknowledged pending completion");
}
void SignalAcknowledgement() {
    cave::ps2::GsCompletionMailbox mailbox;const auto epoch=mailbox.Generation();
    Require(mailbox.Signal(epoch,1),"Initial signal rejected");
    auto first=mailbox.Take();Require(first.signal && first.signalPayload==1,"Initial signal changed");
    auto next=std::async(std::launch::async,[&]{return mailbox.Signal(epoch,2);});
    Require(next.wait_for(20ms)==std::future_status::timeout && !mailbox.Take(),"Second SIGNAL bypassed CSR acknowledgement");
    mailbox.AcknowledgeSignal();
    Require(next.wait_for(1s)==std::future_status::ready && next.get(),"Signal did not resume after acknowledgement");
    auto second=mailbox.Take();Require(second.signal && second.signalPayload==2,"Resumed signal changed");
    mailbox.AcknowledgeSignal();
}
void ResetAndCloseReleaseWaiter() {
    cave::ps2::GsCompletionMailbox mailbox;const auto epoch=mailbox.Generation();
    mailbox.Signal(epoch,1);mailbox.Take();
    auto stale=std::async(std::launch::async,[&]{return mailbox.Signal(epoch,2);});
    Require(stale.wait_for(20ms)==std::future_status::timeout,"Expected blocked signal");
    const auto newer=mailbox.Reset();
    Require(stale.wait_for(1s)==std::future_status::ready && !stale.get() && !mailbox.Take(),"Reset failed to cancel old signal");
    mailbox.Signal(newer,3);mailbox.Take();
    auto closing=std::async(std::launch::async,[&]{try{mailbox.Signal(newer,4);return false;}catch(const std::runtime_error&){return true;}});
    Require(closing.wait_for(20ms)==std::future_status::timeout,"Expected signal blocked before close");
    mailbox.Close();Require(closing.wait_for(1s)==std::future_status::ready && closing.get(),"Close stranded a signal waiter");
}
}
int main(){try{MaskAndReset();SignalAcknowledgement();ResetAndCloseReleaseWaiter();
    std::cout<<"GS completion mask composition, signal acknowledgement, reset epochs and shutdown: PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
