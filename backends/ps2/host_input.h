#pragma once
#include <array>
#include <cstdint>
#include <memory>

namespace cave::ps2 {
struct HostPadState {
    uint16_t buttons=0; // Active-high PS2 button bits.
    std::array<uint8_t,4> axes{128,128,128,128}; // RX, RY, LX, LY.
};
class HostPadSource {
public:
    virtual ~HostPadSource()=default;
    virtual HostPadState Read(unsigned port)=0;
};
// One physical host per game process. Shared ownership keeps an in-flight EE
// read alive while the main thread disconnects its input source at shutdown.
void SetHostPadSource(std::shared_ptr<HostPadSource> source);
uint32_t DiagnosticTapMilliseconds();
}
