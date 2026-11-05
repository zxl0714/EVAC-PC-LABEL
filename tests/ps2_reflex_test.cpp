#include "reflex.h"
#include <cstdlib>
#include <vector>

namespace {
void Check(bool condition) {
    if (!condition) std::abort();
}
struct Driver {
    unsigned enabled{}, disabled{}, created{}, destroyed{}, sleeps{};
    uint64_t ready{}, submitted{}, lastWait{};
    VkResult sleepResult = VK_SUCCESS;
    std::vector<VkLatencyMarkerNV> markers;
} driver;
const auto device = reinterpret_cast<VkDevice>(1);
const auto chain = reinterpret_cast<VkSwapchainKHR>(2);
const auto timeline = reinterpret_cast<VkSemaphore>(3);

VKAPI_ATTR VkResult VKAPI_CALL Create(VkDevice, const VkSemaphoreCreateInfo* info,
                                      const VkAllocationCallbacks*, VkSemaphore* out) {
    auto* type = static_cast<const VkSemaphoreTypeCreateInfo*>(info->pNext);
    Check(type && type->semaphoreType == VK_SEMAPHORE_TYPE_TIMELINE);
    *out = timeline;
    ++driver.created;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL Destroy(VkDevice, VkSemaphore sem, const VkAllocationCallbacks*) {
    Check(sem == timeline && driver.ready == driver.submitted);
    ++driver.destroyed;
}
VKAPI_ATTR VkResult VKAPI_CALL Mode(VkDevice, VkSwapchainKHR, const VkLatencySleepModeInfoNV* info) {
    Check(info && !info->lowLatencyBoost && !info->minimumIntervalUs);
    if (info->lowLatencyMode) ++driver.enabled;
    else ++driver.disabled;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL Sleep(VkDevice, VkSwapchainKHR, const VkLatencySleepInfoNV* info) {
    Check(info->signalSemaphore == timeline && info->value > driver.submitted);
    ++driver.sleeps;
    if (driver.sleepResult == VK_SUCCESS) driver.submitted = info->value;
    return driver.sleepResult;
}
VKAPI_ATTR VkResult VKAPI_CALL Count(VkDevice, VkSemaphore, uint64_t* value) {
    *value = driver.ready;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL Wait(VkDevice, const VkSemaphoreWaitInfo* info, uint64_t timeout) {
    Check(info->semaphoreCount == 1 && *info->pSemaphores == timeline);
    driver.lastWait = timeout;
    return *info->pValues <= driver.ready ? VK_SUCCESS : VK_TIMEOUT;
}
VKAPI_ATTR void VKAPI_CALL Mark(VkDevice, VkSwapchainKHR, const VkSetLatencyMarkerInfoNV* info) {
    Check(info->presentID == 32);
    driver.markers.push_back(info->marker);
}
}

int main() {
    VolkDeviceTable api{};
    api.vkCreateSemaphore = Create;
    api.vkDestroySemaphore = Destroy;
    api.vkSetLatencySleepModeNV = Mode;
    api.vkLatencySleepNV = Sleep;
    api.vkGetSemaphoreCounterValue = Count;
    api.vkWaitSemaphores = Wait;
    api.vkSetLatencyMarkerNV = Mark;

    cave::ps2::Reflex off(false), reflex(true);
    off.Connect(device, api, chain, true);
    reflex.Connect(device, api, chain, false);
    auto incomplete = api;
    incomplete.vkLatencySleepNV = nullptr;
    reflex.Connect(device, incomplete, chain, true);
    Check(!driver.created);
    reflex.Connect(device, api, chain, true);
    reflex.AfterPresent(31, 90);
    reflex.AfterPresent(31, 90);
    driver.ready = driver.submitted;
    reflex.BeforeRead(90, 500);
    Check(driver.markers.empty());
    reflex.BeforeRead(91, 850);
    reflex.BeforeRead(91, 500);
    Check(driver.lastWait == 500000);
    reflex.SimulationReady(91);
    reflex.SimulationReady(91);
    Check(driver.markers == std::vector<VkLatencyMarkerNV>{
        VK_LATENCY_MARKER_SIMULATION_START_NV, VK_LATENCY_MARKER_INPUT_SAMPLE_NV,
        VK_LATENCY_MARKER_SIMULATION_END_NV});
    Check(driver.sleeps == 1);
    reflex.BeginOutput();
    reflex.AfterPresent(32, 91);
    reflex.Disconnect();
    reflex.Connect(device, api, chain, true);
    Check(!driver.destroyed && driver.enabled == 1 && driver.lastWait == 0);
    driver.ready = driver.submitted;
    reflex.Disconnect();
    reflex.Connect(device, api, chain, true);
    Check(driver.created == 1 && driver.enabled == 2);
    // A driver that repeatedly misses the read deadline yields to regular pacing.
    for (unsigned i = 1; i <= 8; ++i) {
        reflex.AfterPresent(i, i);
        reflex.BeforeRead(i + 1, 100);
        Check(driver.lastWait == 100000);
        reflex.BeginOutput();
        driver.ready = driver.submitted;
    }
    reflex.AfterPresent(9, 9);
    Check(driver.disabled == 2 && driver.sleeps == 10);
    reflex.Disconnect();
    reflex.Connect(device, api, chain, true);
    driver.sleepResult = VK_ERROR_UNKNOWN;
    reflex.AfterPresent(1, 1);
    reflex.AfterPresent(2, 2);
    Check(driver.sleeps == 11 && driver.disabled == 3);
    reflex.Disconnect();
    reflex.ReleaseDevice();
    Check(driver.created == 1 && driver.destroyed == 1);
}
