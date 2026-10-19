#include "native_mspace.h"
#include "native_memory.h"
#include "native_diagnostics.h"
#include "native_host_timing.h"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <stdexcept>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
struct Block { std::uint64_t size; bool free; };
struct Arena {
    std::uintptr_t base;
    std::uint64_t size, physical;
    std::map<std::uintptr_t, Block> blocks;
    Arena(std::uintptr_t start, std::uint64_t length, std::uint64_t offset)
        : base(start), size(length), physical(offset) { blocks.emplace(start, Block{length, true}); }
};
struct Arenas {
    std::mutex mutex;
    std::map<std::uintptr_t, Arena> values;
    std::uintptr_t next = 0x4d53000000000001ull;
};
Arenas& State() { static Arenas arenas; return arenas; }

std::uintptr_t Align(std::uintptr_t value, std::uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

void* AllocateBlock(Arena& arena, std::uint64_t alignment, std::uint64_t size) {
    // This is Sony mspace memalign, not POSIX posix_memalign. The local libc
    // accepts small alignments (the game asks for 4) and rounds larger non-
    // powers of two upward. Reject signed-negative/overflow requests first.
    if (alignment > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) { SetNativeErrno(12); return nullptr; }
    alignment = alignment <= 32 ? 32 : std::bit_ceil(alignment);
    if (size > (std::numeric_limits<std::uint64_t>::max)() - 15) { SetNativeErrno(12); return nullptr; }
    size = Align((std::max)(size, 1ull), 16);
    for (auto iterator = arena.blocks.begin(); iterator != arena.blocks.end(); ++iterator) {
        if (!iterator->second.free || iterator->first > (std::numeric_limits<std::uintptr_t>::max)() - (alignment - 1)) { continue; }
        const auto address = Align(iterator->first, alignment);
        const auto prefix = address - iterator->first;
        if (prefix > iterator->second.size || size > iterator->second.size - prefix) { continue; }
        const auto suffix = iterator->second.size - prefix - size;
        auto allocated = arena.blocks.end();
        // Publish the split only after every potentially throwing node allocation.
        try {
            if (prefix) { allocated = arena.blocks.emplace(address, Block{size, false}).first; }
            if (suffix) { arena.blocks.emplace(address + size, Block{suffix, true}); }
        } catch (...) {
            if (allocated != arena.blocks.end()) { arena.blocks.erase(allocated); }
            throw;
        }
        if (prefix) { iterator->second.size = prefix; }
        else { iterator->second = Block{size, false}; }
        return reinterpret_cast<void*>(address);
    }
    SetNativeErrno(12);
    return nullptr;
}

bool FreeBlock(Arena& arena, void* pointer) {
    if (!pointer) { return true; }
    auto iterator = arena.blocks.find(reinterpret_cast<std::uintptr_t>(pointer));
    if (iterator == arena.blocks.end() || iterator->second.free) { return false; }
    iterator->second.free = true;
    auto next = std::next(iterator);
    if (next != arena.blocks.end() && next->second.free) { iterator->second.size += next->second.size; arena.blocks.erase(next); }
    if (iterator != arena.blocks.begin()) {
        auto previous = std::prev(iterator);
        if (previous->second.free) { previous->second.size += iterator->second.size; arena.blocks.erase(iterator); }
    }
    return true;
}

template <typename Function>
auto Api(const void* caller, Function function) -> decltype(function()) {
    try { return function(); }
    catch (const std::bad_alloc&) { SetNativeErrno(12); return {}; }
    catch (const std::exception& error) {
        std::fprintf(stderr, "Native mspace error: %s\n", error.what());
        UnsupportedApi("native_mspace_host", 0, caller);
    }
}
template <typename Function>
void VoidApi(const void* caller, Function function) {
    try { function(); }
    catch (const std::exception& error) {
        std::fprintf(stderr, "Native mspace lifecycle error: %s\n", error.what());
        UnsupportedApi("native_mspace_host", 0, caller);
    }
}
Arena& Find(Arenas& state, void* handle, const void* caller) {
    auto iterator = state.values.find(reinterpret_cast<std::uintptr_t>(handle));
    if (iterator == state.values.end()) { UnsupportedApi("invalid_mspace_handle", reinterpret_cast<std::uintptr_t>(handle), caller); }
    return iterator->second;
}

void* KT_SYSV Create(const char* name, void* base, std::uint64_t size, int flags) {
    const auto caller = __builtin_return_address(0);
    NativeHostOperationScope timing("sceLibcMspaceCreate", "allocation", size, caller, true, NativeHostAllocationKind::MspaceCreate);
    return Api(caller, [&]() -> void* {
        if (flags) { UnsupportedApi("sceLibcMspaceCreate_flags", static_cast<unsigned>(flags), caller); }
        if (!name || !base || reinterpret_cast<std::uintptr_t>(base) % 16 || size < 16 || size % 16) { SetNativeErrno(22); return nullptr; }
        auto& state = State();
        std::lock_guard lock(state.mutex);
        std::uint64_t physical{};
        if (!RetainNativeMemoryRange(base, size, &physical)) { UnsupportedApi("sceLibcMspaceCreate_backing", reinterpret_cast<std::uintptr_t>(base), caller); }
        for (const auto& [handle, arena] : state.values) {
            (void)handle;
            if (physical < arena.physical + arena.size && arena.physical < physical + size) {
                ReleaseNativeMemoryRange(base); SetNativeErrno(22); return nullptr;
            }
        }
        const auto handle = state.next++;
        try { state.values.try_emplace(handle, reinterpret_cast<std::uintptr_t>(base), size, physical); }
        catch (...) { ReleaseNativeMemoryRange(base); throw; }
        std::printf("{\"event\":\"native_mspace_create\",\"handle\":\"0x%llx\",\"base\":\"0x%llx\",\"size\":%llu}\n", handle, reinterpret_cast<std::uintptr_t>(base), size);
        TraceNativeApi("sceLibcMspaceCreate", size, caller);
        return reinterpret_cast<void*>(handle);
    });
}
int KT_SYSV Destroy(void* handle) {
    const auto caller = __builtin_return_address(0);
    NativeHostOperationScope timing("sceLibcMspaceDestroy", "allocation", 0, caller, true, NativeHostAllocationKind::MspaceDestroy);
    VoidApi(caller, [&] {
        auto& state = State();
        std::lock_guard lock(state.mutex);
        auto& arena = Find(state, handle, caller);
        ReleaseNativeMemoryRange(reinterpret_cast<void*>(arena.base));
        state.values.erase(reinterpret_cast<std::uintptr_t>(handle));
    });
    return 0;
}
void* KT_SYSV Memalign(void* handle, std::uint64_t alignment, std::uint64_t size) {
    const auto caller = __builtin_return_address(0);
    NativeHostOperationScope timing("sceLibcMspaceMemalign", "allocation", size, caller, true, NativeHostAllocationKind::MspaceMemalign);
    return Api(caller, [&] {
        auto& state = State();
        std::lock_guard lock(state.mutex);
        auto* pointer = AllocateBlock(Find(state, handle, caller), alignment, size);
        static std::uint64_t calls{};
        ++calls;
        if (calls <= 32 || !pointer || calls % 1024 == 0) {
            std::printf("{\"event\":\"native_mspace_allocate\",\"size\":%llu,\"alignment\":%llu,\"pointer\":\"0x%llx\",\"call\":%llu}\n", size, alignment, reinterpret_cast<std::uintptr_t>(pointer), calls);
        }
        return pointer;
    });
}
void KT_SYSV Free(void* handle, void* pointer) {
    const auto caller = __builtin_return_address(0);
    NativeHostOperationScope timing("sceLibcMspaceFree", "allocation", 0, caller, true, NativeHostAllocationKind::MspaceFree);
    VoidApi(caller, [&] {
        auto& state = State();
        std::lock_guard lock(state.mutex);
        if (!FreeBlock(Find(state, handle, caller), pointer)) { UnsupportedApi("invalid_mspace_free", reinterpret_cast<std::uintptr_t>(pointer), caller); }
    });
}
} // namespace

std::uint64_t ResolveMspaceImport(std::string_view identifier) {
    const auto hash = identifier.substr(0, identifier.find('#'));
    if (hash == "-hn1tcVHq5Q") { return reinterpret_cast<std::uint64_t>(&Create); }
    if (hash == "W6SiVSiCDtI") { return reinterpret_cast<std::uint64_t>(&Destroy); }
    if (hash == "iF1iQHzxBJU") { return reinterpret_cast<std::uint64_t>(&Memalign); }
    if (hash == "Vla-Z+eXlxo") { return reinterpret_cast<std::uint64_t>(&Free); }
    return 0;
}

bool MspaceSelfTest() {
    // Exercise the allocator separately from Windows backing, with a local
    // authored arena. The SysV fixture also covers shared-section integration.
    alignas(4096) unsigned char storage[16384]{};
    Arena arena(reinterpret_cast<std::uintptr_t>(storage), sizeof(storage), 0);
    bool passed = true;
    auto check = [&](const char* name, bool result) {
        passed &= result;
        std::printf("{\"event\":\"mspace_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, result ? "true" : "false");
    };
    auto* first = AllocateBlock(arena, 16, 31);
    auto* second = AllocateBlock(arena, 4096, 8000);
    auto* third = AllocateBlock(arena, 16, 0);
    check("alignment_and_partition", first == storage && second == storage + 4096 && third == storage + 32 && arena.blocks.size() == 5);
    if (first) { std::memset(first, 0x5a, 31); }
    if (second) { std::memset(second, 0xa7, 8000); }
    check("disjoint_writable_storage", storage[30] == 0x5a && storage[4096] == 0xa7 && storage[12095] == 0xa7);
    check("invalid_and_overflow_allocations", !AllocateBlock(arena, 1ull << 63, 16) && !AllocateBlock(arena, 16, (std::numeric_limits<std::uint64_t>::max)()));
    check("out_of_memory_is_not_an_overrun", !AllocateBlock(arena, 16, sizeof(storage)) && storage[30] == 0x5a);
    check("invalid_free_does_not_mutate", !FreeBlock(arena, storage + 1) && !FreeBlock(arena, storage + 12000) && storage[4096] == 0xa7);
    check("null_free_and_coalescing", FreeBlock(arena, nullptr) && FreeBlock(arena, first) && !FreeBlock(arena, first) &&
        FreeBlock(arena, third) && FreeBlock(arena, second) && arena.blocks.size() == 1 && arena.blocks.begin()->second.size == sizeof(storage));
    check("full_range_reuse", AllocateBlock(arena, 16, sizeof(storage)) == storage && !AllocateBlock(arena, 16, 1));
    check("release_full_range", FreeBlock(arena, storage) && arena.blocks.size() == 1 && arena.blocks.begin()->second.free);
    const auto small = AllocateBlock(arena, 4, 7);
    const auto rounded = AllocateBlock(arena, 65, 17);
    check("small_and_non_power_alignment_normalization", small && rounded && reinterpret_cast<std::uintptr_t>(small) % 32 == 0 &&
        reinterpret_cast<std::uintptr_t>(rounded) % 128 == 0 && FreeBlock(arena, small) && FreeBlock(arena, rounded) && arena.blocks.size() == 1);
    std::printf("{\"event\":\"mspace_self_test\",\"passed\":%s}\n", passed ? "true" : "false");
    return passed;
}
