#pragma once
#include <cstdint>
namespace Ketsui::GpuCore {
void RecordDraw();
void RecordDispatch();
std::uint64_t DrawCount();
std::uint64_t DispatchCount();
}
