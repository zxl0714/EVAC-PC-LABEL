#pragma once
#include "static_modules.h"

namespace cave::ps2 {
struct FunctionTableRun {
    uint32_t first, count;
    Function function;
};

// Populate an initially zeroed dense table before it is used. Holes remain
// null; normal dispatch still indexes the original pointer array directly.
void PopulateFunctionTable(Function* table, uint32_t slots,
                           std::span<const FunctionTableRun> runs);
}
