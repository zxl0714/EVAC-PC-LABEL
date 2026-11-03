#include "function_table.h"
#include <algorithm>
#include <stdexcept>

namespace cave::ps2 {
__declspec(noinline) void PopulateFunctionTable(Function* table, uint32_t slots,
                                                std::span<const FunctionTableRun> runs) {
    uint64_t end{};
    for (const auto& run : runs) {
        const uint64_t next = uint64_t(run.first) + run.count;
        if (!table || !run.function || !run.count || run.first < end || next > slots)
            throw std::invalid_argument("Invalid PS2 function-table run");
        end = next;
    }
    for (const auto& run : runs)
        std::fill_n(table + run.first, run.count, run.function);
}
}
