#include "tracked_texture.h"
#include "direct_fixture.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace kt::memory {
bool TrackedTextureSelfTest() {
    bool passed = true;
    const auto check = [&](const char* name, bool value) {
        passed &= value;
        std::printf("{\"event\":\"tracked_texture_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false");
    };
    DirectFixture direct;
    kt::gfx::TextureLayout layout;
    layout.address = reinterpret_cast<std::uint64_t>(direct.first); layout.bytes = 128;
    layout.width = 2; layout.height = 2; layout.pitch = 8; layout.padded_height = 4; layout.tiling = 8;
    layout.components = {4, 5, 6, 7};
    auto* first = static_cast<unsigned char*>(direct.first);
    auto* alias = static_cast<unsigned char*>(direct.alias);
    std::fill_n(first, 128, 73);
    const std::array<unsigned char, 8> row{1, 2, 3, 4, 5, 6, 7, 8};
    std::memcpy(first, row.data(), 8); std::memcpy(first + 32, row.data(), 8);
    TrackedTextureCache cache;
    const auto original = cache.Decode(layout);
    check("initial_decode_owns_current_cropped_pixels_and_publishes_bounded_weak_witness", original->size() == 16 && (*original)[0] == 1 &&
        (*original)[8] == 1 && cache.Bytes() == 144 && cache.Witnesses() == 1 && cache.Stats().content_misses == 1);
    const auto compared = cache.Stats().compared_bytes;
    const auto repeated = cache.Decode(layout);
    check("valid_generation_reuses_immutable_pixels_without_full_content_comparison", repeated == original && cache.Stats().generation_hits == 1 && cache.Stats().compared_bytes == compared);
    alias[0] = 19;
    const auto changed = cache.Decode(layout);
    check("physical_alias_write_invalidates_witness_and_preserves_old_pixels", changed != original && (*changed)[0] == 19 && (*original)[0] == 1 && cache.Stats().content_misses == 2);
    alias[127] ^= 0xff;
    const auto padding = cache.Decode(layout);
    check("backing_tail_padding_write_forces_complete_revalidation_even_if_visible_pixels_match", padding != changed && *padding == *changed && cache.Stats().compared_bytes == compared + 256);
    alias[4095] ^= 0xff;
    const auto page_padding = cache.Decode(layout);
    check("same_page_neighbor_write_cannot_keep_an_unprotected_generation_alive", page_padding == padding && cache.Stats().content_hits == 1);
    alias[8192] ^= 0xff;
    const auto before_unrelated = cache.Stats();
    check("nonoverlapping_pages_do_not_invalidate_texture_witness", cache.Decode(layout) == padding && cache.Stats().generation_hits == before_unrelated.generation_hits + 1);
    auto other_layout = layout; other_layout.width = 1;
    const auto narrow = cache.Decode(other_layout);
    check("layout_change_is_not_a_generation_hit_for_same_memory", narrow->size() == 8 && cache.Stats().content_misses == 4 && cache.Witnesses() == 2);
    auto writing = BeginNativeHostWrite(alias, 128);
    const auto busy_before = cache.Stats();
    const auto busy_pixels = cache.Decode(layout);
    check("inflight_host_write_uses_full_comparison_and_does_not_publish_a_witness", writing && busy_pixels == padding &&
        cache.Stats().generation_hits == busy_before.generation_hits && cache.Stats().compared_bytes == busy_before.compared_bytes + 128 && cache.Witnesses() == 1);
    const unsigned char host_value = 27; SIZE_T written{};
    const bool kernel_write = WriteProcessMemory(GetCurrentProcess(), alias, &host_value, 1, &written) && written == 1;
    const auto during = cache.Decode(layout);
    check("kernel_write_during_host_lease_cannot_reuse_stale_pixels", kernel_write && (*during)[0] == 27 && (*padding)[0] == 19 && cache.Witnesses() == 1);
    writing = {};
    const auto after = cache.Decode(layout); const auto hits_before = cache.Stats().generation_hits;
    check("completed_host_write_rearms_only_after_capturing_current_content", after == during && cache.Decode(layout) == after && cache.Stats().generation_hits == hits_before + 1);
    {
        TrackedTextureCache fallback(false);
        const auto a = fallback.Decode(layout); const auto b = fallback.Decode(layout);
        check("full_validation_mode_never_uses_or_publishes_generation_witnesses", a == b && fallback.Stats().generation_hits == 0 && fallback.Stats().compared_bytes == 256 && fallback.Witnesses() == 0);
    }
    {
        TrackedTextureCache bounded(true, 144, 1);
        auto a = bounded.Decode(layout);
        std::weak_ptr<const std::vector<std::uint8_t>> weak = a;
        a.reset();
        auto shifted = layout; shifted.address += 8192;
        const auto b = bounded.Decode(shifted);
        check("witnesses_do_not_keep_evicted_pixel_storage_alive_or_exceed_budget", weak.expired() && b && bounded.Bytes() == 144 && bounded.Witnesses() == 1);
        const auto again = bounded.Decode(layout);
        check("expired_or_evicted_witness_requires_full_current_content_validation", (*again)[0] == 27 && bounded.Stats().generation_hits == 0 && bounded.Stats().content_misses == 3);
    }
    bool rejected{};
    auto invalid = layout; invalid.pitch = 1;
    try { (void)cache.Decode(invalid); } catch (const std::exception&) { rejected = true; }
    check("invalid_layout_is_rejected_instead_of_bypassing_decode_validation", rejected);
    check("guest_readonly_alias_keeps_original_permissions_during_snapshot_capture", direct.ProtectAlias(1) && cache.Decode(layout) && !NativeMemoryWritable(alias, 1));
    check("guest_noaccess_mapping_is_rejected_even_if_old_pixels_survive", direct.ProtectAlias(0) && [&] {
        auto inaccessible = layout; inaccessible.address = reinterpret_cast<std::uint64_t>(alias);
        try { (void)cache.Decode(inaccessible); return false; } catch (const std::exception&) { return true; }
    }() && direct.ProtectAlias(3));
    direct.Reset();
    rejected = false;
    try { (void)cache.Decode(layout); } catch (const std::exception&) { rejected = true; }
    check("unmapped_memory_is_rejected_before_reusing_a_surviving_pixel_snapshot", rejected && (*original)[0] == 1);
    {
        DirectFixture replacement;
        auto reused = layout; reused.address = reinterpret_cast<std::uint64_t>(replacement.first);
        const auto fresh = cache.Decode(reused);
        check("new_allocation_cannot_reuse_proof_from_released_backing_at_same_virtual_or_physical_address", fresh != after &&
            std::all_of(fresh->begin(), fresh->end(), [](auto value) { return value == 0; }) && (*after)[0] == 27);
    }
    std::printf("{\"event\":\"tracked_texture_self_test\",\"passed\":%s}\n", passed ? "true" : "false");
    return passed;
}
}
