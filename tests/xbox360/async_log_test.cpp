#include "async_file_sink.h"
#include <spdlog/sinks/base_sink.h>
#include <iostream>
#include <vector>
class BlockingSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    std::mutex gate;
    std::condition_variable changed;
    bool entered{},released{};
    std::vector<std::string> records;
private:
    void sink_it_(const spdlog::details::log_msg& message) override {
        std::unique_lock lock(gate);entered=true;changed.notify_all();
        changed.wait(lock,[&]{return released;});
        records.emplace_back(message.payload.data(),message.payload.size());
    }
    void flush_() override {}
};
int main() {
    auto output=std::make_shared<BlockingSink>();
    rex::logging::AsyncFileSink sink(output,8);
    sink.log(spdlog::details::log_msg("test",spdlog::level::info,"first"));
    {
        std::unique_lock lock(output->gate);
        if (!output->changed.wait_for(lock,std::chrono::seconds(2),[&]{return output->entered;})) return 2;
    }
    for (int n=0;n<1000;++n) {
        const auto text=std::to_string(n);
        sink.log(spdlog::details::log_msg("test",spdlog::level::info,text));
    }
    const bool bounded=sink.pending()<=8 && sink.dropped()>=992;
    {std::lock_guard lock(output->gate);output->released=true;output->changed.notify_all();}
    sink.flush();
    if (!bounded || output->records.size()>10 ||
        std::find(output->records.begin(),output->records.end(),"999")==output->records.end() ||
        output->records.back().find("LOG_QUEUE dropped_oldest=")==std::string::npos) return 3;
    sink.log(spdlog::details::log_msg("test",spdlog::level::info,std::string(20000,'x')));
    sink.flush();
    if (output->records.back().size()!=16384) return 4;
    std::cout<<"Bounded async logging regression passed; explicit flush drained the worker\n";
    return 0;
}
