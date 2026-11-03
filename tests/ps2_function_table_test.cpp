#include "function_table.h"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
int calls{};
void A(uint8_t*, R5900Context*, PS2Runtime*) { calls += 1; }
void B(uint8_t*, R5900Context*, PS2Runtime*) { calls += 10; }
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    try {
        using namespace cave::ps2;
        std::array<Function, 32> first{}, second{};
        const std::array<FunctionTableRun, 3> runs{{{0, 3, A}, {7, 1, B}, {30, 2, A}}};
        PopulateFunctionTable(first.data(), uint32_t(first.size()), runs);
        for (unsigned i = 0; i < first.size(); ++i) {
            const auto expected = i < 3 || i >= 30 ? A : i == 7 ? B : nullptr;
            Require(first[i] == expected, "Dense function-table slot changed");
            if (first[i]) first[i](nullptr, nullptr, nullptr);
        }
        Require(calls == 15, "Indirect dispatch changed after table initialization");
        const std::array other{FunctionTableRun{0, 3, B}};
        PopulateFunctionTable(second.data(), uint32_t(second.size()), other);
        Require(first[0] == A && second[0] == B, "Same-address module tables were merged");
        for (const auto& bad : {FunctionTableRun{31, 2, A}, {UINT32_MAX, 2, A},
                               {0, 0, A}, {0, 1, nullptr}}) {
            auto empty = std::array<Function, 32>{};
            const std::array invalid{FunctionTableRun{0, 1, A}, bad};
            bool rejected{};
            try { PopulateFunctionTable(empty.data(), uint32_t(empty.size()), invalid); }
            catch (const std::invalid_argument&) { rejected = true; }
            Require(rejected, "Invalid function-table range was accepted");
            Require(empty == std::array<Function, 32>{}, "Invalid table was partially installed");
        }
        PopulateFunctionTable(nullptr, 0, {});
        std::cout << "PS2 compact table: slots, holes, indirect calls, bank separation and invalid ranges passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
