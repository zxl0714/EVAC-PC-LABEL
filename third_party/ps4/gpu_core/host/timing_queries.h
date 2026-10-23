#pragma once
#include "host_services.h"
#include "diagnostics/timing_trace.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include <array>

namespace Ketsui::GpuCore {
// Diagnostic timestamps bracket the original drawing batch. They measure a
// device timeline span, including dependencies/gaps, NOT exclusive GPU busy
// time. Never add submissions, WAIT_BIT, Finish or fence waits to read them.
class TimingQueries {
    const Vulkan::Instance& instance;
    Vulkan::Scheduler& scheduler;
    vk::UniqueQueryPool pool;
    struct Slot { bool pending{}; std::uint64_t sequence{}, tick{}, began{}, submitted{}; };
    std::array<Slot,64> slots{};
    unsigned next{}, active=64, bits{};
    double period{};
    std::uint64_t sequence{}, skipped{};
public:
    TimingQueries(const Vulkan::Instance& i,Vulkan::Scheduler& s):instance(i),scheduler(s) {
        if (!kt::diagnostics::TimingTraceEnabled()) return;
        const auto families=i.GetPhysicalDevice().getQueueFamilyProperties();
        bits=families.at(i.GetGraphicsQueueFamilyIndex()).timestampValidBits;
        period=i.GetPhysicalDevice().getProperties().limits.timestampPeriod;
        if (bits && period>0) {
            auto result=i.GetDevice().createQueryPoolUnique({.queryType=vk::QueryType::eTimestamp,.queryCount=128});
            if (result.result==vk::Result::eSuccess) pool=std::move(result.value);
        }
        TimingLog("{\"event\":\"native_gpu_timestamp_support\",\"enabled\":%s,\"valid_bits\":%u,\"period_ns\":%.9f,\"slots\":64,\"extra_gpu_waits\":false}\n",pool?"true":"false",bits,period);
    }
    void Begin() {
        if (!pool || active<64) return;
        if (slots[next].pending) { ++skipped; return; }
        active=next; next=(next+1)%64;
        slots[active]={false,++sequence,scheduler.CurrentTick(),kt::diagnostics::MonotonicMicroseconds(),0};
        scheduler.EndRendering();
        auto command=scheduler.CommandBuffer();
        command.resetQueryPool(*pool,active*2,2);
        command.writeTimestamp2(vk::PipelineStageFlagBits2::eTopOfPipe,*pool,active*2);
    }
    void End() {
        if (active>=64) return;
        scheduler.EndRendering();
        scheduler.CommandBuffer().writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands,*pool,active*2+1);
        auto& slot=slots[active]; slot.tick=scheduler.CurrentTick();
        slot.submitted=kt::diagnostics::MonotonicMicroseconds(); slot.pending=true; active=64;
    }
    void Collect() {
        if (!pool) return;
        for (unsigned index=0;index<64;++index) {
            auto& slot=slots[index]; if (!slot.pending) continue;
            std::array<std::uint64_t,4> data{};
            const auto result=instance.GetDevice().getQueryPoolResults(*pool,index*2,2,sizeof(data),data.data(),16,
                vk::QueryResultFlagBits::e64|vk::QueryResultFlagBits::eWithAvailability);
            if (result==vk::Result::eNotReady) continue;
            if (result!=vk::Result::eSuccess) {
                TimingLog("{\"event\":\"native_gpu_timestamp_error\",\"sequence\":%llu,\"result\":%d}\n",slot.sequence,static_cast<int>(result));
                slot.pending=false; continue;
            }
            if (!data[1] || !data[3]) continue;
            const auto mask=bits==64?UINT64_MAX:((std::uint64_t{1}<<bits)-1);
            const auto elapsed=(data[2]-data[0])&mask;
            TimingLog("{\"event\":\"native_gpu_batch_timestamp\",\"sequence\":%llu,\"tick\":%llu,\"began_clock_us\":%llu,\"end_recorded_clock_us\":%llu,\"clock_us\":%llu,\"gpu_begin\":%llu,\"gpu_end\":%llu,\"gpu_span_us\":%.3f,\"skipped_slots\":%llu,\"query_result\":%d}\n",
                slot.sequence,slot.tick,slot.began,slot.submitted,kt::diagnostics::MonotonicMicroseconds(),data[0],data[2],double(elapsed)*period/1000.0,skipped,static_cast<int>(result));
            slot.pending=false;
        }
    }
};
}
