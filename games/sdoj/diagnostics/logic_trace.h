#pragma once
#include <cstdint>

// Independent of the fine latency trace and its record budget. Records only
// native input consumption and a genuine completed/parked command buffer.
void SdojLogicTraceEvent(std::uint32_t module,std::uint32_t phase,std::uint32_t argument);
