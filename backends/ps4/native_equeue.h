#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

struct NativeKernelEvent {
    std::uint64_t ident;
    std::int16_t filter;
    std::uint16_t flags;
    std::uint32_t fflags;
    std::uint64_t data;
    void* user_data;
};
static_assert(sizeof(NativeKernelEvent) == 32);
static_assert(offsetof(NativeKernelEvent, filter) == 8 && offsetof(NativeKernelEvent, data) == 16 && offsetof(NativeKernelEvent, user_data) == 24);

std::uint64_t ResolveEqueueImport(std::string_view identifier);
bool EqueueSelfTest();
int AddNativeVideoEvent(std::uint64_t queue, std::uint64_t ident, std::uint64_t owner, void* user_data);
int RemoveNativeVideoEvent(std::uint64_t queue, std::uint64_t ident, std::uint64_t owner);
bool EmitNativeVideoEvent(std::uint64_t queue, std::uint64_t ident, std::uint64_t owner,
                          std::uint64_t value, std::uint64_t tsc, std::uint64_t occurrences = 1);
