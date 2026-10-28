#pragma once

// Diagnostic builds only. Vulkan must be included before this header. Feedback
// is keyed by the exact swapchain/present ID, never by a nearest ETW timestamp.
// Sources: VK_EXT_present_timing rev3 and VK_KHR_calibrated_timestamps.
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace evac_latency {
inline std::uint64_t FeedbackQpc() {
    LARGE_INTEGER value{}; QueryPerformanceCounter(&value);
    return static_cast<std::uint64_t>(value.QuadPart);
}
inline std::uint64_t FeedbackFrequency() {
    LARGE_INTEGER value{}; QueryPerformanceFrequency(&value);
    return static_cast<std::uint64_t>(value.QuadPart);
}

class VulkanPresentFeedback {
    vk::Device device;
    vk::PhysicalDevice physical;
    std::FILE* output{};
    bool enabled;
    vk::SwapchainKHR swapchain{};
    std::uint64_t generation{}, domain_id{}, present_id{};
    vk::PresentStageFlagsEXT stages{};
    vk::PresentTimingInfoEXT timing{};
    vk::PresentTimingsInfoEXT timings{};
    vk::PresentId2KHR ids{};
    std::uint64_t requests{}, completed{};
    std::map<std::pair<std::uint64_t,unsigned>,std::uint64_t> calibrated;

    void Calibrate(vk::TimeDomainKHR domain, std::uint64_t id,
                   vk::PresentStageFlagsEXT stage, bool force) {
        if(domain!=vk::TimeDomainKHR::ePresentStageLocalEXT &&
           domain!=vk::TimeDomainKHR::eSwapchainLocalEXT)
            throw std::runtime_error("Feedback changed to an unrequested time domain");
        const auto key=std::pair{id,static_cast<unsigned>(stage)};
        const auto now=FeedbackQpc();
        if(!force && now-calibrated[key]<FeedbackFrequency()) return;
        const vk::SwapchainCalibratedTimestampInfoEXT local{
            .swapchain=swapchain,.presentStage=stage,.timeDomainId=id};
        const std::array info{
            vk::CalibratedTimestampInfoKHR{.timeDomain=vk::TimeDomainKHR::eQueryPerformanceCounter},
            vk::CalibratedTimestampInfoKHR{.pNext=&local,.timeDomain=domain}};
        std::array<std::uint64_t,2> values{};
        std::uint64_t deviation{};
        const auto status=device.getCalibratedTimestampsKHR(
            2,info.data(),values.data(),&deviation);
        if(status!=vk::Result::eSuccess)
            throw std::runtime_error("Cannot calibrate present feedback");
        std::fprintf(output,
            "{\"kind\":\"calibration\",\"generation\":%llu,\"domain\":%u,\"domain_id\":%llu,"
            "\"stage\":%u,\"host_qpc\":%llu,\"local_ns\":%llu,\"max_deviation_ns\":%llu}\n",
            generation,static_cast<unsigned>(domain),id,static_cast<unsigned>(stage),
            values[0],values[1],deviation);
        calibrated[key]=now;
    }

public:
    VulkanPresentFeedback(vk::Device device_,vk::PhysicalDevice physical_,
                          const std::filesystem::path& path,bool enabled_=true)
        :device(device_),physical(physical_),enabled(enabled_) {
        if(std::filesystem::exists(path)) throw std::runtime_error("Fresh feedback output required");
        if(_wfopen_s(&output,path.c_str(),L"wx") || !output)
            throw std::runtime_error("Cannot open feedback output");
        std::fprintf(output,"{\"kind\":\"header\",\"schema\":1,\"pid\":%lu,\"frequency\":%llu,\"timing_feedback\":%s}\n",
                     GetCurrentProcessId(),FeedbackFrequency(),enabled?"true":"false");
    }
    ~VulkanPresentFeedback() {
        if(output) {
            std::fprintf(output,"{\"kind\":\"end\",\"generation\":%llu,\"requests\":%llu,\"completed\":%llu}\n",
                         generation,requests,completed);
            std::fclose(output);
        }
    }
    VulkanPresentFeedback(const VulkanPresentFeedback&)=delete;
    VulkanPresentFeedback& operator=(const VulkanPresentFeedback&)=delete;

    static vk::SwapchainCreateFlagsKHR Flags() {
        return vk::SwapchainCreateFlagBitsKHR::ePresentTimingEXT |
               vk::SwapchainCreateFlagBitsKHR::ePresentId2;
    }

    void Attach(vk::SwapchainKHR chain,vk::SurfaceKHR surface) {
        swapchain=chain; ++generation; present_id=0; calibrated.clear();
        if(!enabled) {
            std::fprintf(output,"{\"kind\":\"chain\",\"generation\":%llu,\"domain_id\":0,\"stages\":0}\n",generation);
            return;
        }
        const auto caps=physical.getSurfaceCapabilities2KHR<
            vk::SurfaceCapabilities2KHR,vk::PresentTimingSurfaceCapabilitiesEXT,
            vk::SurfaceCapabilitiesPresentId2KHR>({.surface=surface});
        const auto& timing_caps=caps.get<vk::PresentTimingSurfaceCapabilitiesEXT>();
        if(!timing_caps.presentTimingSupported ||
           !caps.get<vk::SurfaceCapabilitiesPresentId2KHR>().presentId2Supported)
            throw std::runtime_error("Surface does not support identified present feedback");
        const auto endpoint=(timing_caps.presentStageQueries & vk::PresentStageFlagBitsEXT::eImageFirstPixelVisible)
            ?vk::PresentStageFlagBitsEXT::eImageFirstPixelVisible
            :vk::PresentStageFlagBitsEXT::eImageFirstPixelOut;
        stages=timing_caps.presentStageQueries &
            (vk::PresentStageFlagBitsEXT::eQueueOperationsEnd | endpoint);
        if(!(stages & endpoint))
            throw std::runtime_error("Surface cannot report first-pixel output or visibility");
        std::uint64_t counter{};
        vk::SwapchainTimeDomainPropertiesEXT domains{};
        auto status=device.getSwapchainTimeDomainPropertiesEXT(chain,&domains,&counter);
        if(status!=vk::Result::eSuccess || !domains.timeDomainCount)
            throw std::runtime_error("No presentation time domains");
        std::vector<vk::TimeDomainKHR> names(domains.timeDomainCount);
        std::vector<std::uint64_t> identifiers(domains.timeDomainCount);
        domains.pTimeDomains=names.data(); domains.pTimeDomainIds=identifiers.data();
        status=device.getSwapchainTimeDomainPropertiesEXT(chain,&domains,&counter);
        if(status!=vk::Result::eSuccess) throw std::runtime_error("Cannot query time domains");
        bool found=false;
        for(unsigned i=0;i<domains.timeDomainCount;++i) {
            if(names[i]!=vk::TimeDomainKHR::ePresentStageLocalEXT) continue;
            domain_id=identifiers[i]; found=true; break;
        }
        if(!found) throw std::runtime_error("No stage-local presentation clock");
        if(device.setSwapchainPresentTimingQueueSizeEXT(chain,512)!=vk::Result::eSuccess)
            throw std::runtime_error("Cannot allocate feedback queue");
        std::fprintf(output,"{\"kind\":\"chain\",\"generation\":%llu,\"domain_id\":%llu,\"stages\":%u}\n",
                     generation,domain_id,static_cast<unsigned>(stages));
        for(auto stage:{vk::PresentStageFlagBitsEXT::eQueueOperationsEnd,
                        vk::PresentStageFlagBitsEXT::eImageFirstPixelOut,
                        vk::PresentStageFlagBitsEXT::eImageFirstPixelVisible})
            if(stages & stage) Calibrate(vk::TimeDomainKHR::ePresentStageLocalEXT,domain_id,stage,true);
    }

    const void* Before(const void* prior=nullptr) {
        Drain(); ++present_id;
        if(!enabled) return prior;
        timing={.timeDomainId=domain_id,.presentStageQueries=stages}; // no scheduled target
        timings={.pNext=prior,.swapchainCount=1,.pTimingInfos=&timing};
        ids={.pNext=&timings,.swapchainCount=1,.pPresentIds=&present_id};
        return &ids;
    }
    void After(std::uint64_t source,std::uint64_t begin,std::uint64_t end,
               vk::PresentModeKHR mode,vk::Result result) {
        ++requests;
        std::fprintf(output,
            "{\"kind\":\"present\",\"generation\":%llu,\"id\":%llu,\"source\":%llu,"
            "\"begin_qpc\":%llu,\"end_qpc\":%llu,\"mode\":%u,\"result\":%d}\n",
            generation,present_id,source,begin,end,static_cast<unsigned>(mode),static_cast<int>(result));
    }
    void Drain() {
        if(!swapchain || !enabled) return;
        std::array<vk::PastPresentationTimingEXT,64> results{};
        std::array<std::array<vk::PresentStageTimeEXT,2>,64> stage_times{};
        for(unsigned i=0;i<results.size();++i)
            results[i]={.presentStageCount=2,.pPresentStages=stage_times[i].data()};
        const vk::PastPresentationTimingInfoEXT info{.swapchain=swapchain};
        vk::PastPresentationTimingPropertiesEXT properties{
            .presentationTimingCount=static_cast<unsigned>(results.size()),.pPresentationTimings=results.data()};
        const auto status=device.getPastPresentationTimingEXT(&info,&properties);
        if(status!=vk::Result::eSuccess && status!=vk::Result::eIncomplete)
            throw std::runtime_error("Present feedback query failed");
        for(unsigned i=0;i<properties.presentationTimingCount;++i) {
            const auto& result=results[i];
            if(result.reportComplete) ++completed;
            for(unsigned s=0;s<result.presentStageCount;++s) {
                const auto& stamp=result.pPresentStages[s];
                if(stamp.time) Calibrate(result.timeDomain,result.timeDomainId,stamp.stage,false);
                std::fprintf(output,
                    "{\"kind\":\"stage\",\"generation\":%llu,\"id\":%llu,\"stage\":%u,"
                    "\"time\":%llu,\"domain\":%u,\"domain_id\":%llu,\"complete\":%s}\n",
                    generation,result.presentId,static_cast<unsigned>(stamp.stage),stamp.time,
                    static_cast<unsigned>(result.timeDomain),result.timeDomainId,result.reportComplete?"true":"false");
            }
        }
    }
    void Sample(std::uint64_t frame,std::uint64_t begin,std::uint64_t acquired,
                std::uint64_t submitted,std::uint64_t retired,double gpu_us) {
        std::fprintf(output,
            "{\"kind\":\"work\",\"generation\":%llu,\"id\":%llu,\"frame\":%llu,"
            "\"begin_qpc\":%llu,\"acquired_qpc\":%llu,\"submitted_qpc\":%llu,"
            "\"retired_qpc\":%llu,\"gpu_us\":%.6f}\n",
            generation,present_id,frame,begin,acquired,submitted,retired,gpu_us);
    }
};
} // namespace evac_latency
