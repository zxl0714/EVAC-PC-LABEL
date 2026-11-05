#pragma once
#include "volk.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <shared_mutex>

namespace cave::ps2 {
// Granite owns the swapchain and its present IDs. Post one driver sleep after
// a successful Present so its timeline can signal during the game's idle time.
// Only the matching P1 read consumes it. GPU work never waits on the pad.
class Reflex {
    const bool requested;
    std::shared_mutex lifetime;
    VkDevice device{};
    const VolkDeviceTable* api{};
    VkSwapchainKHR chain{};
    VkSemaphore semaphore{};
    uint64_t signal{},pending{},presentId{},field{};
    bool active{};
    std::atomic<bool> open{},sampled{},ended{};
    uint64_t reads{},timeouts{},busy{},issued{};
    unsigned consecutiveMisses{},consecutiveBusy{};
    VkResult waitError=VK_SUCCESS;

    void Disable(VkResult reason) {
        open=false;
        active=false;
        const VkLatencySleepModeInfoNV off{VK_STRUCTURE_TYPE_LATENCY_SLEEP_MODE_INFO_NV};
        api->vkSetLatencySleepModeNV(device,chain,&off);
        std::fprintf(stderr,"[ps2-reflex] active=false reason=driver result=%d\n",int(reason));
    }
    void Marker(VkLatencyMarkerNV marker) const {
        VkSetLatencyMarkerInfoNV m{VK_STRUCTURE_TYPE_SET_LATENCY_MARKER_INFO_NV};
        m.presentID=presentId;m.marker=marker;
        api->vkSetLatencyMarkerNV(device,chain,&m);
    }
    VkResult Retire(uint64_t value,uint64_t timeout) const {
        VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        wait.semaphoreCount=1;wait.pSemaphores=&semaphore;wait.pValues=&value;
        return api->vkWaitSemaphores(device,&wait,timeout);
    }
public:
    explicit Reflex(bool enable):requested(enable) {}
    void Connect(VkDevice handle,const VolkDeviceTable& dispatch,VkSwapchainKHR swapchain,bool supported) {
        std::unique_lock lock(lifetime);
        if(!requested || !swapchain || !supported) {
            std::fprintf(stderr,"[ps2-reflex] active=false reason=%s\n",requested?"capability":"configuration");
            return;
        }
        device=handle;api=&dispatch;
        if(!api->vkLatencySleepNV || !api->vkSetLatencySleepModeNV || !api->vkSetLatencyMarkerNV ||
           !api->vkWaitSemaphores || !api->vkGetSemaphoreCounterValue) {
            std::fprintf(stderr,"[ps2-reflex] active=false reason=entry_points\n");return;
        }
        // A slow signal may outlive a window resize. Keep its device semaphore
        // alive and use regular pacing on the new chain while it is in flight.
        if(pending && Retire(pending,0)!=VK_SUCCESS) {
            std::fprintf(stderr,"[ps2-reflex] active=false reason=retiring_sleep\n");return;
        }
        pending=0;
        chain=swapchain;
        VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        type.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo create{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,&type};
        auto result=semaphore?VK_SUCCESS:api->vkCreateSemaphore(device,&create,nullptr,&semaphore);
        if(result==VK_SUCCESS) {
            VkLatencySleepModeInfoNV mode{VK_STRUCTURE_TYPE_LATENCY_SLEEP_MODE_INFO_NV};
            mode.lowLatencyMode=VK_TRUE;
            result=api->vkSetLatencySleepModeNV(device,chain,&mode);
        }
        active=result==VK_SUCCESS;
        std::fprintf(stderr,"[ps2-reflex] active=%s result=%d\n",active?"true":"false",int(result));
    }
    void Disconnect() {
        std::unique_lock lock(lifetime);
        open=false;
        if(!chain)return;
        if(active) {
            const VkLatencySleepModeInfoNV off{VK_STRUCTURE_TYPE_LATENCY_SLEEP_MODE_INFO_NV};
            api->vkSetLatencySleepModeNV(device,chain,&off);
        }
        if(pending && Retire(pending,0)==VK_SUCCESS)pending=0;
        std::fprintf(stderr,"[ps2-reflex] issued=%llu sampled=%llu deadline_misses=%llu busy=%llu\n",
            (unsigned long long)issued,(unsigned long long)reads,(unsigned long long)timeouts,(unsigned long long)busy);
        chain=VK_NULL_HANDLE;active=false;
        presentId=field=reads=timeouts=busy=issued=consecutiveMisses=consecutiveBusy=0;
        waitError=VK_SUCCESS;
    }
    void ReleaseDevice() {
        std::unique_lock lock(lifetime);
        if(!semaphore)return;
        // Called only after WSI teardown and after the EE thread has joined.
        // An outstanding driver signal must finish before destroying its target.
        if(pending) {
            const auto result=Retire(pending,UINT64_MAX);
            if(result!=VK_SUCCESS)std::fprintf(stderr,"[ps2-reflex] retire result=%d\n",int(result));
        }
        api->vkDestroySemaphore(device,semaphore,nullptr);
        semaphore=VK_NULL_HANDLE;pending=signal=0;
    }
    void BeginOutput() {open.store(false,std::memory_order_release);}
    void AfterPresent(uint64_t submittedId,uint64_t guestField) {
        std::unique_lock lock(lifetime);
        if(!active || !submittedId || submittedId+1==presentId)return;
        if(waitError!=VK_SUCCESS || consecutiveMisses>=8) {
            Disable(waitError==VK_SUCCESS?VK_TIMEOUT:waitError);return;
        }
        if(pending) {
            uint64_t value{};
            const auto r=api->vkGetSemaphoreCounterValue(device,semaphore,&value);
            if(r!=VK_SUCCESS){Disable(r);return;}
            if(value<pending) {
                ++busy;
                if(++consecutiveBusy>=3)Disable(VK_TIMEOUT);
                return;
            }
            pending=0;
        }
        consecutiveBusy=0;
        VkLatencySleepInfoNV sleep{VK_STRUCTURE_TYPE_LATENCY_SLEEP_INFO_NV};
        sleep.signalSemaphore=semaphore;sleep.value=++signal;
        const auto result=api->vkLatencySleepNV(device,chain,&sleep);
        if(result!=VK_SUCCESS){Disable(result);return;}
        ++issued;pending=signal;field=guestField+1;presentId=submittedId+1;
        sampled=false;ended=false;open=true;
    }
    void BeforeRead(uint64_t responseField,unsigned budgetUs) {
        if(!open.load(std::memory_order_acquire))return;
        std::shared_lock lock(lifetime);
        if(!open || field!=responseField || sampled.exchange(true))return;
        const auto result=Retire(pending,uint64_t(std::min(budgetUs,500u))*1000);
        if(result!=VK_SUCCESS) {
            if(result==VK_TIMEOUT){++timeouts;++consecutiveMisses;}else waitError=result;
            ended=true;return;
        }
        if(!open){ended=true;return;}
        consecutiveMisses=0;
        ++reads;
        Marker(VK_LATENCY_MARKER_SIMULATION_START_NV);
        Marker(VK_LATENCY_MARKER_INPUT_SAMPLE_NV);
    }
    void SimulationReady(uint64_t responseField) {
        if(!open.load(std::memory_order_acquire))return;
        std::shared_lock lock(lifetime);
        if(open && field==responseField && sampled && !ended.exchange(true))
            Marker(VK_LATENCY_MARKER_SIMULATION_END_NV);
    }
};
}
