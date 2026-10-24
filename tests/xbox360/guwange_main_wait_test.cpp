#include "games/guwange/main_wait.h"

#include <iostream>
#include <stdexcept>

namespace {
struct TitleStopped {};
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    try {
        for (auto flag : {1u, 0x80000000u, 0xFFFFFFFFu}) {
            int checks = 0, waits = 0;
            cave::guwange::KeepMainThreadAlive(flag, [&] { ++checks; }, [&] { ++waits; });
            Require(checks == 0 && waits == 0, "Nonzero original path must not wait");
        }
        for (int stop_after : {1, 4}) {
            int checks = 0, waits = 0;
            bool stopped = false;
            try {
                cave::guwange::KeepMainThreadAlive(0,
                    [&] { if (++checks == stop_after) throw TitleStopped{}; },
                    [&] { ++waits; });
            } catch (const TitleStopped&) { stopped = true; }
            Require(stopped, "Zero sample must not return normally");
            Require(checks == stop_after && waits == stop_after - 1,
                    "Check termination before each cooperative wait");
        }
        std::cout << "Guwange main wait: nonzero fallthrough, zero keepalive and termination pass\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
