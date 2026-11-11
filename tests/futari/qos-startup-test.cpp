#include "games/futari/compat/qos_startup.h"
#include <windows.h>
#include <array>
#include <cstdio>
#include <stdexcept>

namespace {
void Check(bool condition, const char* description) {
    if (!condition) throw std::runtime_error(description);
}
struct Fixture {
    std::array<uint32_t, 256> memory{};
    uint32_t observed{};
    uint32_t Read(uint32_t at) const {
        Check(at && at % 4 == 0 && at / 4 < memory.size(), "invalid guest read");
        return memory[at / 4];
    }
    void Write(uint32_t at, uint32_t value) { memory[at / 4] = value; }
};
}
int main() {
    try {
        Fixture fixture;
        constexpr uint32_t Owner = 128, System = 256, Network = 384, Handle = 17;
        auto read = [&](uint32_t address) { return fixture.Read(address); };
        Check(futari::compat::PublishedQosThread(0, read) == 0, "no owner");
        Check(futari::compat::PublishedQosThread(Owner, read) == 0, "unpublished system");
        fixture.Write(Owner + 76, System);
        Check(futari::compat::PublishedQosThread(Owner, read) == 0, "no network object");
        fixture.Write(System + 72, Network);
        Check(futari::compat::PublishedQosThread(Owner, read) == 0, "thread creation failed");
        fixture.Write(Network + 28, Handle);
        fixture.Write(Owner + 76, 0);
        auto worker = CreateThread(nullptr, 0, [](void* arg) -> DWORD {
            auto& f = *static_cast<Fixture*>(arg);
            f.observed = f.memory[Owner / 4 + 19];
            return 0;
        }, &fixture, futari::compat::SuspendQosCreate(0), nullptr);
        Check(worker != nullptr, "create suspended worker");
        const auto before = WaitForSingleObject(worker, 20);
        // The native outer constructor publishes this pointer only after its
        // nested constructor has created the thread and all profile objects.
        fixture.Write(Owner + 76, System);
        const auto handle = futari::compat::PublishedQosThread(Owner, read);
        Check(handle == Handle, "resume the published object's own worker");
        const auto previous = ResumeThread(worker);
        const auto after = WaitForSingleObject(worker, 1000);
        CloseHandle(worker);
        Check(before == WAIT_TIMEOUT, "worker ran before system publication");
        Check(previous == 1 && after == WAIT_OBJECT_0, "worker resumed exactly once");
        Check(fixture.observed == System, "worker observed the completed system");
        Check(futari::compat::SuspendQosCreate(0x10000) == 0x10004, "preserve creation options");
        std::puts("Futari QoS publication and suspended-thread checks passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what()); return 1;
    }
}
