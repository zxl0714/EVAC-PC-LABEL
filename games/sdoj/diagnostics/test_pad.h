#pragma once
#include <filesystem>
#include <functional>
namespace rex { struct RuntimeConfig; }
namespace sdoj::diagnostics {
// Explicit development-only pad source. No desktop input or guest-memory writes.
void AddTestPad(rex::RuntimeConfig&, const std::filesystem::path&, std::function<void()> quit);
}
