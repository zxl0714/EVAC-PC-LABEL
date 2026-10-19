#pragma once
#include "client.h"
#include <atomic>
#include <functional>

namespace kt::community {
// Only the transport worker touches an admitted item. Stop may reject it only
// while it is still under the input queue lock.
struct RoundWork {
    RoundInput input;
    std::promise<RoundAcceptance> accepted;
    std::promise<RoundAttempt> first;
    RoundAttempt progress;
    bool accepted_done{}, first_done{};
    void Accept(RoundAcceptance value);
    void Finish(Error error, bool complete = false);
    void Reject(Error error);
};
class RoundDelivery {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    RoundDelivery(Config config, const std::filesystem::path& root, std::atomic<std::size_t>& count);
    ~RoundDelivery();
    void Stage(const std::shared_ptr<RoundWork>& work, bool online);
    // At most one HTTP operation per tick, on the existing transport worker.
    bool Tick(bool online, const std::function<Reply(std::string_view,const Json&)>& send);
    void Stop(Error error);
};
}
