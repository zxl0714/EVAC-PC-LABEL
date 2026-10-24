#include "pm4.h"
#include "ps4_init_350.h"
#include <algorithm>
#include <cstdio>
#include <limits>
#include <vector>

namespace kt::gfx::pm4 {
Decoded Decode(std::span<const std::uint32_t> words, std::uint32_t offset) {
    Decoded result{}; result.packet.offset = offset;
    if (words.size() > UINT32_MAX || offset >= words.size()) { result.error = Error::Truncated; return result; }
    auto& packet = result.packet;
    packet.header = words[offset];
    const auto type = packet.header >> 30;
    if (type == 2) {
        if (packet.header != 0x80000000u) { result.error = Error::ReservedHeader; return result; }
        packet.padding = true; result.next = offset + 1; return result;
    }
    if (type != 3) { result.error = Error::UnsupportedType; return result; }
    if (packet.header & 0xfcu) { result.error = Error::ReservedHeader; return result; }
    const auto size = ((packet.header >> 16) & 0x3fffu) + 1;
    if (size > words.size() - offset - 1) { result.error = Error::Truncated; return result; }
    packet.opcode = static_cast<std::uint8_t>(packet.header >> 8);
    packet.compute = (packet.header & 2) != 0; packet.predicated = (packet.header & 1) != 0;
    packet.body = words.subspan(static_cast<std::size_t>(offset) + 1, size);
    result.next = offset + 1 + size; return result;
}
namespace {
Error Registers(const Packet& packet, RegisterBank& bank, std::uint32_t& writes) {
    if (packet.body.size() < 2) { return Error::BadSize; }
    const auto first = packet.body.front();
    // Indexed/set-offset variants and their high control bits need their own
    // contract; do not mask them into an ordinary register write.
    if (first >= bank.values.size() || packet.body.size() - 1 > bank.values.size() - first) { return Error::RegisterRange; }
    for (std::size_t i = 1; i < packet.body.size(); ++i) {
        const auto index = first + i - 1; bank.values[index] = packet.body[i]; bank.known[index] = true;
    }
    writes += static_cast<std::uint32_t>(packet.body.size() - 1); return Error::None;
}
Error Apply(const Packet& packet, Analysis& result) {
    if (packet.padding) { ++result.padding_packets; return Error::None; }
    if (packet.predicated) { return Error::UnsupportedFlags; }
    if (packet.compute && packet.opcode != 0x76 && packet.opcode != 0x10) { return Error::UnsupportedFlags; }
    switch (packet.opcode) {
    case 0x10: return Error::None; // NOP body is deliberately opaque.
    case 0x12:
        if (packet.body.size() != 1) { return Error::BadSize; }
        if (packet.body[0] != 0) { return Error::BadValue; }
        result.context.known.reset(); result.shader.known.reset(); result.uconfig.known.reset(); result.instances.reset();
        result.last_draw_vertices.reset(); result.last_indexed_draw.reset(); ++result.clears; return Error::None;
    case 0x28:
        if (packet.body.size() != 2) { return Error::BadSize; }
        if (packet.body[0] != 0x80000000u || packet.body[1] != 0x80000000u) { return Error::BadValue; }
        ++result.context_controls; return Error::None;
    case 0x2f:
        if (packet.body.size() != 1) { return Error::BadSize; }
        result.instances = packet.body[0]; return Error::None;
    case 0x2a: // INDEX_TYPE: 16/32-bit indices, native byte order.
        if (packet.body.size() != 1) { return Error::BadSize; }
        if (packet.body[0] > 1) { return Error::BadValue; }
        result.context.values[0x29f] = packet.body[0]; result.context.known.set(0x29f);
        return Error::None;
    case 0x27: // DRAW_INDEX_2, with a 40-bit byte address.
        if (packet.body.size() != 5) { return Error::BadSize; }
        if (packet.body[2] > 0xff || (packet.body[1] & 1) ||
            !(packet.body[1] | packet.body[2]) || packet.body[3] > packet.body[0] || packet.body[4]) { return Error::BadValue; }
        result.last_indexed_draw = IndexedDraw{packet.body[1] | (std::uint64_t{packet.body[2]} << 32), packet.body[3], packet.body[0]};
        result.last_draw_vertices.reset(); ++result.draws; return Error::None;
    case 0x2d:
        if (packet.body.size() != 2) { return Error::BadSize; }
        if (packet.body[1] != 2) { return Error::BadValue; }
        result.last_indexed_draw.reset(); ++result.draws; result.last_draw_vertices = packet.body[0]; return Error::None;
    case 0x3c:
        if (packet.body.size() != 6) { return Error::BadSize; }
        if (packet.body[0] != 0x13 || (packet.body[1] & 3) || packet.body[2] > 0xffff) { return Error::BadValue; }
        result.last_wait = MemoryWait{packet.body[1] | (static_cast<std::uint64_t>(packet.body[2]) << 32), packet.body[3], packet.body[4], packet.body[5]};
        ++result.waits; return Error::None;
    case 0x50:
        if (packet.body.size() != 6) { return Error::BadSize; }
        if ((packet.body[0] & 0x10cf0ffeu) || (packet.body[5] & 0x80000000u) || ((packet.body[0] >> 20) & 3) == 2) { return Error::BadValue; }
        result.last_dma = DmaTransfer{packet.body[1] | (static_cast<std::uint64_t>(packet.body[2]) << 32),
            packet.body[3] | (static_cast<std::uint64_t>(packet.body[4]) << 32), packet.body[0], packet.body[5], packet.body[5] & 0x1fffffu};
        if (!result.first_dma) { result.first_dma = result.last_dma; }
        ++result.dma_transfers; return Error::None;
    case 0x58:
        if (packet.body.size() != 6) { return Error::BadSize; }
        // Cache-action/destination bits established by Init350 plus the PFP
        // engine selection. Main's +0x20cf90/+0x20f3a0 generators also emit
        // regional/one-unit forms with control 0x82c40040/0x88000000.
        // Keep the 40-bit address/size and poll=10 profile of those generators;
        // unknown controls/high fields still stop rather than being masked.
        if ((packet.body[0] & ~0xaec47fc0u) || packet.body[2] || packet.body[4] || packet.body[5] != 10 ||
            std::uint64_t{packet.body[3]} + packet.body[1] > 0x100000000ull) { return Error::BadValue; }
        result.last_acquire = MemoryAcquire{packet.body[0], std::uint64_t{packet.body[3]} << 8,
            std::uint64_t{packet.body[1]} << 8, packet.body[5]};
        ++result.acquires; return Error::None;
    case 0x69: return Registers(packet, result.context, result.register_writes);
    case 0x76: return Registers(packet, result.shader, result.register_writes);
    case 0x79: return Registers(packet, result.uconfig, result.register_writes);
    default: return Error::UnsupportedOpcode;
    }
}
}
Error Advance(const Packet& packet, Analysis& state) {
    const auto error = Apply(packet, state);
    if (error == Error::None) { ++state.packets; }
    else { state.error = error; state.error_offset = packet.offset; }
    return error;
}
Analysis Analyze(std::span<const std::uint32_t> words) {
    Analysis result;
    for (std::uint32_t offset = 0; offset < words.size();) {
        const auto decoded = Decode(words, offset);
        auto error = decoded.error;
        if (error == Error::None) { error = Apply(decoded.packet, result); }
        if (error != Error::None) { result.error = error; result.error_offset = offset; return result; }
        ++result.packets; offset = decoded.next;
    }
    return result;
}
std::uint32_t WriteHardware350(std::span<std::uint32_t> output) {
    if (output.size() < kHardware350Words) { return 0; }
    auto destination = output.first(kHardware350Words);
    std::fill(destination.begin(), destination.end(), 0u);
    // Context load controls, clear, and global cache acquire. These are real
    // PM4 commands, not a substitute for the Vulkan execution still to follow.
    constexpr std::uint32_t prefix[]{0xc0012800u, 0x80000000u, 0x80000000u, 0xc0001200u, 0u,
        0xc0055800u, 0x2ec47fc0u, 0xffffffffu, 0u, 0u, 0u, 10u};
    static_assert(std::size(prefix) + ps4::kInit350.size() + 2 <= kHardware350Words);
    std::copy(std::begin(prefix), std::end(prefix), destination.begin());
    std::copy(ps4::kInit350.begin(), ps4::kInit350.end(), destination.begin() + std::size(prefix));
    constexpr auto nop_offset = std::size(prefix) + ps4::kInit350.size();
    constexpr auto nop_body = kHardware350Words - nop_offset - 1;
    destination[nop_offset] = 0xc0001000u | (static_cast<std::uint32_t>(nop_body - 1) << 16);
    return kHardware350Words;
}
bool WriteWaitFlip(std::span<std::uint32_t> output, std::uint64_t address) {
    if (output.size() != 7 || !address || (address & 3) || address > 0xffffffffffffull) { return false; }
    const std::uint32_t words[]{0xc0053c00u, 0x13, static_cast<std::uint32_t>(address), static_cast<std::uint32_t>(address >> 32), 0, 0xffffffffu, 10};
    std::copy(std::begin(words), std::end(words), output.begin()); return true;
}
bool WriteVertexShader(std::span<std::uint32_t> output, std::span<const std::uint32_t> registers, std::uint32_t modifier, bool update) {
    constexpr std::uint32_t preserved = 0xfcfffc3fu;
    if (output.size() < 29 || registers.size() != 7 || registers[1] || (modifier & preserved)) { return false; }
    // Stage the complete packet before writing: caller storage may alias the
    // input register descriptor. PGM_LO addresses are in 256-byte units.
    const auto resource1 = modifier ? (registers[2] & preserved) | modifier : registers[2];
    std::array<std::uint32_t, 29> words{
        0xc0027600u, 0x48, registers[0], 0,
        0xc0027600u, 0x4a, resource1, registers[3],
        0xc0016900u, 0x207, registers[6],
        0xc0016900u, 0x1b1, registers[4],
        0xc0016900u, 0x1c3, registers[5],
        0xc00a1000u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    if (update) {
        for (const auto offset : {8u, 11u, 14u}) { words[offset] = 0xc0011000u; words[offset + 1] |= 0xc01e0000u; }
    }
    std::copy(words.begin(), words.end(), output.begin()); return true;
}
bool WritePixelShader350(std::span<std::uint32_t> output, std::span<const std::uint32_t> registers, bool update) {
    if (output.size() < 40 || (!registers.empty() && (registers.size() != 12 || registers[1]))) { return false; }
    std::array<std::uint32_t, 40> words{};
    if (registers.empty()) {
        const std::uint32_t disabled[]{0xc0027600u, 8, 0, 0, 0xc0016900u, 0x203, 0, 0xc0016900u, 0x8f, 0xf, 0xc01c1000u};
        std::copy(std::begin(disabled), std::end(disabled), words.begin());
    } else {
        words = {0xc0027600u, 8, registers[0], 0,
            0xc0027600u, 10, registers[2], registers[3],
            0xc0026900u, 0x1c4, registers[4], registers[5],
            0xc0026900u, 0x1b3, registers[6], registers[7],
            0xc0016900u, 0x1b6, registers[8],
            0xc0016900u, 0x1b8, registers[9],
            0xc0016900u, 0x203, registers[10],
            0xc0016900u, 0x8f, registers[11],
            0xc00a1000u};
    }
    if (update) {
        const auto marker = [&](unsigned offset) { words[offset] = (words[offset] & 0xffff00ffu) | 0x1000u; words[offset + 1] |= 0xc01e0000u; };
        if (registers.empty()) { marker(4); }
        else { for (const auto offset : {8u, 12u, 16u, 19u, 22u, 25u}) { marker(offset); } }
    }
    std::copy(words.begin(), words.end(), output.begin()); return true;
}
bool WriteDrawAuto(std::span<std::uint32_t> output, std::uint32_t vertices, std::uint32_t flags) {
    if (output.size() != 7 || (flags & 0x1ffffffeu)) { return false; }
    // Base-machine contract: high Neo scheduling hints have no effect and
    // this entry point does not enable predication, including for flags bit 0.
    const std::uint32_t words[]{0xc0012d00u, vertices, 2, 0xc0021000u, 0, 0, 0};
    std::copy(std::begin(words), std::end(words), output.begin()); return true;
}
bool WriteDrawIndex(std::span<std::uint32_t> output, std::uint32_t count, std::uint64_t address, std::uint32_t flags) {
    if (output.size() != 10 || !address || (address & 1) || address > 0xffffffffffull || (flags & 0x1ffffffeu)) { return false; }
    // Base-machine GNM emits DRAW_INDEX_2 and a three-word NOP payload.
    // The API's cache-policy argument does not select 16/32-bit index format;
    // that state comes from INDEX_TYPE (or the corresponding context register).
    const std::uint32_t words[]{0xc0042700u, count, static_cast<std::uint32_t>(address),
        static_cast<std::uint32_t>(address >> 32), count, 0, 0xc0021000u, 0, 0, 0};
    std::copy(std::begin(words), std::end(words), output.begin()); return true;
}
bool SelfTest() {
    bool passed = true;
    auto check = [&](bool condition, const char* name) {
        std::printf("{\"event\":\"pm4_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, condition ? "true" : "false"); passed &= condition;
    };
    std::array<std::uint32_t, kHardware350Words + 1> output; output.fill(0x73737373u);
    check(!WriteHardware350(std::span(output).first(kHardware350Words - 1)) && std::all_of(output.begin(), output.end(), [](auto n) { return n == 0x73737373u; }),
          "undersized_writer_preserves_entire_output");
    check(WriteHardware350(output) == 256 && output.back() == 0x73737373u, "writer_returns_dwords_and_preserves_tail");
    const auto analysis = Analyze(std::span(output).first(kHardware350Words));
    check(analysis.Complete() && analysis.packets == 45 && analysis.register_writes == 42 && analysis.clears == 1 && analysis.acquires == 1 && analysis.context_controls == 1 && analysis.instances == 1,
          "base_350_stream_has_exact_packet_and_register_counts");
    check(analysis.shader.Get(0x216) == 0xffffffffu && analysis.shader.Get(0x217) == 0xffffffffu && analysis.shader.Get(0x107) == 0x17 &&
          analysis.context.Get(0x282) == 8 && analysis.context.Get(0x293) == 0x6020000 && analysis.context.Get(0x295) == 256 &&
          analysis.context.Get(0x296) == 256 && analysis.context.Get(0x297) == 4 && analysis.uconfig.Get(0x200) == 0xe0000000u,
          "shader_context_uconfig_and_multi_register_values");
    check(!analysis.shader.Get(0x219) && !analysis.context.Get(0x1) && !analysis.uconfig.Get(1024), "unknown_register_values_are_not_fabricated_zero");
    const std::array clear_after_write{0xc0016900u, 1u, 7u, 0xc0001200u, 0u};
    const auto reset = Analyze(clear_after_write);
    check(reset.Complete() && !reset.context.Get(1) && reset.register_writes == 1 && reset.clears == 1, "clear_invalidates_known_state_without_zero_default_claim");
    check(Analyze(std::span<const std::uint32_t>{}).Complete() && Decode({}, 0).error == Error::Truncated, "empty_journal_and_no_header_decode");
    const std::array padding{0x80000000u, 0x80000000u};
    const auto pad = Analyze(padding);
    check(pad.Complete() && pad.packets == 2 && pad.padding_packets == 2, "type_two_header_only_padding");
    const std::array reserved_padding{0x80000001u};
    const std::array reserved_header{0xc0001004u, 0u};
    check(Analyze(reserved_padding).error == Error::ReservedHeader && Analyze(reserved_header).error == Error::ReservedHeader, "reserved_header_bits_are_not_silently_masked");
    const std::array type_zero{0u, 0u};
    const std::array type_one{0x40000000u};
    check(Analyze(type_zero).error == Error::UnsupportedType && Analyze(type_one).error == Error::UnsupportedType, "unimplemented_packet_types_stop");
    const std::array short_body{0xc0016900u, 1u};
    check(Analyze(short_body).error == Error::Truncated, "declared_packet_cannot_overread_span");
    std::vector<std::uint32_t> maximum(16385); maximum[0] = 0xffff1000u;
    const auto full = Decode(maximum, 0);
    check(full.error == Error::None && full.packet.body.size() == 16384 && full.next == 16385 && Decode(std::span(maximum).first(16384), 0).error == Error::Truncated,
          "maximum_fourteen_bit_count_does_not_wrap_to_zero");
    const std::array bad_start{0xc0016900u, 1024u, 1u};
    const std::array bad_end{0xc0027600u, 1023u, 1u, 2u};
    const std::array indexed{0xc0017900u, 0x10000200u, 1u};
    check(Analyze(bad_start).error == Error::RegisterRange && Analyze(bad_end).error == Error::RegisterRange && Analyze(indexed).error == Error::RegisterRange,
          "register_ranges_and_indexed_variants_not_truncated");
    const std::array unknown{0x80000000u, 0xc000ff00u, 0u}, predicated{0xc0016901u, 1u, 7u};
    const auto missing = Analyze(unknown);
    check(missing.error == Error::UnsupportedOpcode && missing.error_offset == 1 && missing.packets == 1 && Analyze(predicated).error == Error::UnsupportedFlags,
          "unknown_opcode_and_predication_report_exact_frontier");
    const std::array no_value{0xc0006900u, 1u}, bad_clear{0xc0001200u, 1u};
    const std::array bad_control{0xc0012800u, 0u, 0u};
    check(Analyze(no_value).error == Error::BadSize && Analyze(bad_clear).error == Error::BadValue && Analyze(bad_control).error == Error::BadValue,
          "known_packet_payload_shapes_and_values_validated");
    std::array<std::uint32_t, 8> wait{}; wait.back() = 73;
    check(WriteWaitFlip(std::span(wait).first(7), 0x1234567880ull) && wait.back() == 73, "wait_flip_writer_uses_exact_seven_word_extent");
    const auto waited = Analyze(std::span(wait).first(7));
    check(waited.Complete() && waited.waits == 1 && waited.last_wait && waited.last_wait->address == 0x1234567880ull &&
          waited.last_wait->reference == 0 && waited.last_wait->mask == 0xffffffffu && waited.last_wait->interval == 10,
          "wait_memory_journal_preserves_address_condition_and_interval_without_dereference");
    wait.fill(73);
    check(!WriteWaitFlip(wait, 0x1000) && !WriteWaitFlip(std::span(wait).first(7), 3) && !WriteWaitFlip(std::span(wait).first(7), 0x1000000000000ull) &&
          std::all_of(wait.begin(), wait.end(), [](auto n) { return n == 73; }), "invalid_wait_extent_alignment_or_address_never_writes");
    const std::array wrong_wait{0xc0053c00u, 0x13u, 3u, 0u, 0u, 0xffffffffu, 10u};
    check(Analyze(wrong_wait).error == Error::BadValue, "malformed_wait_address_not_rounded_into_valid_memory");
    std::array acquire{0xc0055800u, 0x82c40040u, 0x200u, 0u, 0x12345678u, 0u, 10u};
    const auto regional = Analyze(acquire);
    check(regional.Complete() && regional.acquires == 1 && regional.last_acquire &&
        regional.last_acquire->control == 0x82c40040u && regional.last_acquire->base == 0x1234567800ull &&
        regional.last_acquire->bytes == 0x20000 && regional.last_acquire->interval == 10,
        "acquire_regional_pfp_keeps_control_40bit_byte_range_and_poll");
    acquire[1] = 0x88000000u; acquire[2] = 1; acquire[4] = 0;
    const auto one_unit = Analyze(acquire);
    acquire[1] = 0; acquire[2] = 0;
    const auto empty = Analyze(acquire);
    check(one_unit.Complete() && one_unit.last_acquire->bytes == 256 && empty.Complete() && empty.last_acquire->bytes == 0,
        "acquire_one_unit_and_zero_range_generators_are_not_forced_to_global_init_values");
    acquire[1] = 0x82c40041u;
    const bool bad_acquire_control = Analyze(acquire).error == Error::BadValue;
    acquire[1] = 0x82c40040u; acquire[3] = 1;
    const bool bad_size_hi = Analyze(acquire).error == Error::BadValue;
    acquire[3] = 0; acquire[5] = 1;
    const bool bad_base_hi = Analyze(acquire).error == Error::BadValue;
    acquire[5] = 0; acquire[6] = 11;
    check(bad_acquire_control && bad_size_hi && bad_base_hi && Analyze(acquire).error == Error::BadValue,
        "acquire_unknown_controls_high_fields_and_poll_profile_remain_diagnostic");
    acquire[6] = 10; acquire[4] = 0xffffffffu; acquire[2] = 2;
    const bool overflow = Analyze(acquire).error == Error::BadValue;
    acquire[2] = 1;
    check(overflow && Analyze(acquire).Complete() && Analyze(acquire).last_acquire->base == 0xffffffff00ull,
        "acquire_range_endpoint_checked_without_wrapping_40bit_units");
    acquire[0] = 0xc0045800u;
    check(Analyze(std::span(acquire).first(6)).error == Error::BadSize &&
        Analyze(std::span(acquire).first(5)).error == Error::Truncated,
        "acquire_wrong_body_and_truncation_preserve_exact_packet_boundary");
    const std::array vertex{0x1080a000u, 0u, 0x12345678u, 7u, 5u, 6u, 9u};
    std::array<std::uint32_t, 41> shader_commands{}; shader_commands.fill(73);
    check(WriteVertexShader(shader_commands, vertex, 0) && shader_commands[29] == 73, "vertex_writer_preserves_storage_after_twenty_nine_words");
    const auto vs = Analyze(std::span(shader_commands).first(29));
    check(vs.Complete() && vs.packets == 6 && vs.register_writes == 7 && vs.shader.Get(0x48) == vertex[0] && vs.shader.Get(0x4a) == vertex[2] &&
          vs.shader.Get(0x4b) == 7 && vs.context.Get(0x207) == 9 && vs.context.Get(0x1b1) == 5 && vs.context.Get(0x1c3) == 6,
          "vertex_descriptor_register_mapping_and_resource_values");
    check(WriteVertexShader(shader_commands, vertex, 0x030003c0u) && Analyze(std::span(shader_commands).first(29)).shader.Get(0x4a) == ((vertex[2] & 0xfcfffc3fu) | 0x030003c0u),
          "vertex_modifier_changes_only_allowed_resource_fields");
    shader_commands.fill(73); auto invalid_vertex = vertex; invalid_vertex[1] = 1;
    check(!WriteVertexShader(shader_commands, invalid_vertex, 0) && !WriteVertexShader(shader_commands, vertex, 1) &&
          !WriteVertexShader(std::span(shader_commands).first(28), vertex, 0) && std::all_of(shader_commands.begin(), shader_commands.end(), [](auto n) { return n == 73; }),
          "invalid_vertex_address_modifier_and_capacity_do_not_write");
    const std::array pixel{0x1080b000u, 0u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u};
    check(WritePixelShader350(shader_commands, pixel) && shader_commands.back() == 73, "pixel_writer_preserves_storage_after_forty_words");
    const auto ps = Analyze(std::span(shader_commands).first(40));
    check(ps.Complete() && ps.packets == 9 && ps.register_writes == 12 && ps.shader.Get(8) == pixel[0] && ps.shader.Get(10) == 2 &&
          ps.context.Get(0x1c4) == 4 && ps.context.Get(0x1c5) == 5 && ps.context.Get(0x1b3) == 6 && ps.context.Get(0x1b4) == 7 &&
          ps.context.Get(0x1b6) == 8 && ps.context.Get(0x1b8) == 9 && ps.context.Get(0x203) == 10 && ps.context.Get(0x8f) == 11,
          "pixel_descriptor_register_mapping_and_packet_shapes");
    check(WritePixelShader350(shader_commands, {}) && Analyze(std::span(shader_commands).first(40)).Complete() &&
          Analyze(std::span(shader_commands).first(40)).shader.Get(8) == 0 && Analyze(std::span(shader_commands).first(40)).context.Get(0x8f) == 15,
          "null_pixel_shader_emits_explicit_disable_state_not_noop");
    shader_commands.fill(73); auto invalid_pixel = pixel; invalid_pixel[1] = 1;
    check(!WritePixelShader350(shader_commands, invalid_pixel) && !WritePixelShader350(std::span(shader_commands).first(39), pixel) &&
          std::all_of(shader_commands.begin(), shader_commands.end(), [](auto n) { return n == 73; }), "invalid_pixel_address_and_capacity_do_not_write");
    check(WriteVertexShader(shader_commands, vertex, 0, true) && shader_commands[8] == 0xc0011000u &&
          shader_commands[9] == 0xc01e0207u && shader_commands[12] == 0xc01e01b1u && shader_commands[15] == 0xc01e01c3u &&
          Analyze(std::span(shader_commands).first(29)).register_writes == 4 && !Analyze(std::span(shader_commands).first(29)).context.Get(0x207),
          "vertex_update_emits_opaque_context_markers_not_immediate_context_writes");
    const auto update_vs = shader_commands;
    std::copy(vertex.begin(), vertex.end(), shader_commands.begin());
    const bool alias_update = WriteVertexShader(shader_commands, std::span<const std::uint32_t>(shader_commands).first(7), 0, true) &&
        std::equal(shader_commands.begin(), shader_commands.begin() + 29, update_vs.begin());
    shader_commands.fill(73);
    check(alias_update && !WriteVertexShader(std::span(shader_commands).first(28), vertex, 0, true) &&
          !WritePixelShader350(shader_commands, invalid_pixel, true) &&
          std::all_of(shader_commands.begin(), shader_commands.end(), [](auto n) { return n == 73; }),
          "shader_update_stages_aliased_descriptors_and_preserves_failed_output");
    check(WritePixelShader350(shader_commands, pixel, true) && shader_commands[8] == 0xc0021000u && shader_commands[9] == 0xc01e01c4u &&
          shader_commands[13] == 0xc01e01b3u && shader_commands[17] == 0xc01e01b6u && shader_commands[20] == 0xc01e01b8u &&
          shader_commands[23] == 0xc01e0203u && shader_commands[26] == 0xc01e008fu && shader_commands.back() == 73 &&
          Analyze(std::span(shader_commands).first(40)).register_writes == 4 && !Analyze(std::span(shader_commands).first(40)).context.Get(0x8f),
          "pixel_update_preserves_sdk_marker_words_and_exact_forty_word_extent");
    check(WritePixelShader350(shader_commands, {}, true) && shader_commands[4] == 0xc0011000u && shader_commands[5] == 0xc01e0203u &&
          Analyze(std::span(shader_commands).first(40)).shader.Get(8) == 0 && !Analyze(std::span(shader_commands).first(40)).context.Get(0x203) &&
          Analyze(std::span(shader_commands).first(40)).context.Get(0x8f) == 15,
          "null_pixel_update_keeps_real_mask_write_and_opaque_depth_control_marker");
    std::array<std::uint32_t, 8> draw{}; draw.back() = 73;
    check(WriteDrawAuto(std::span(draw).first(7), 3, 0) && draw.back() == 73 && Analyze(std::span(draw).first(7)).Complete() &&
          Analyze(std::span(draw).first(7)).draws == 1 && Analyze(std::span(draw).first(7)).last_draw_vertices == 3,
          "auto_index_draw_journal_retains_vertex_count_without_executing");
    draw.fill(73);
    check(!WriteDrawAuto(draw, 3, 0) && !WriteDrawAuto(std::span(draw).first(7), 3, 2) &&
          std::all_of(draw.begin(), draw.end(), [](auto n) { return n == 73; }) && WriteDrawAuto(std::span(draw).first(7), 0, 0xe0000001u) && draw[0] == 0xc0012d00u,
          "auto_index_draw_extent_flags_zero_count_and_base_profile_hints");
    std::array<std::uint32_t, 11> indexed_draw; indexed_draw.fill(73);
    const auto indexed_words = std::span(indexed_draw).first(10);
    check(WriteDrawIndex(indexed_words, 480, 0x100106fffcull, 0) && indexed_draw.back() == 73 &&
          indexed_draw[0] == 0xc0042700 && indexed_draw[2] == 0x0106fffc && indexed_draw[3] == 0x10 &&
          Analyze(indexed_words).Complete() && Analyze(indexed_words).draws == 1 &&
          Analyze(indexed_words).last_indexed_draw->address == 0x100106fffcull &&
          Analyze(indexed_words).last_indexed_draw->count == 480 && !Analyze(indexed_words).last_draw_vertices,
          "room_indexed_draw_exact_ten_words_high_address_and_tail");
    indexed_draw.fill(73);
    check(!WriteDrawIndex(indexed_draw, 1, 2, 0) && !WriteDrawIndex(indexed_words, 1, 0, 0) &&
          !WriteDrawIndex(indexed_words, 1, 3, 0) && !WriteDrawIndex(indexed_words, 1, 0x10000000000ull, 0) &&
          !WriteDrawIndex(indexed_words, 1, 2, 2) && std::all_of(indexed_draw.begin(), indexed_draw.end(), [](auto n) { return n == 73; }),
          "indexed_draw_invalid_extent_address_flags_leave_output_unchanged");
    check(WriteDrawIndex(indexed_words, 0, 2, 0xe0000001) && Analyze(indexed_words).Complete(),
          "indexed_draw_zero_count_and_base_profile_hints");
    WriteDrawIndex(indexed_words, 480, 0x100106fffcull, 0);
    indexed_draw[4] = 481; const bool count_rejected = Analyze(indexed_words).error == Error::BadValue;
    indexed_draw[4] = 480; indexed_draw[3] = 0x100; const bool high_rejected = Analyze(indexed_words).error == Error::BadValue;
    indexed_draw[3] = 0x10; indexed_draw[5] = 2;
    check(count_rejected && high_rejected && Analyze(indexed_words).error == Error::BadValue &&
          Analyze(indexed_words.first(5)).error == Error::Truncated,
          "indexed_packet_rejects_invalid_capacity_high_address_initiator_and_truncation");
    WriteDrawIndex(indexed_words, 480, 0x100106fffcull, 0);
    const std::array<std::uint32_t, 2> type16{0xc0002a00, 0}, type32{0xc0002a00, 1}, bad_type{0xc0002a00, 4};
    check(Analyze(type16).context.Get(0x29f) == 0 && Analyze(type32).context.Get(0x29f) == 1 &&
          Analyze(bad_type).error == Error::BadValue, "index_type_distinguishes_16_and_32_bits_and_rejects_byte_swap");
    Analysis draw_state;
    Advance(Decode(type16, 0).packet, draw_state);
    Advance(Decode(indexed_words, 0).packet, draw_state);
    const bool indexed_state = draw_state.last_indexed_draw && draw_state.context.Get(0x29f) == 0;
    Advance(Decode(draw, 0).packet, draw_state);
    const bool auto_state = !draw_state.last_indexed_draw && draw_state.last_draw_vertices.has_value();
    const std::array<std::uint32_t, 2> clear{0xc0001200, 0};
    Advance(Decode(clear, 0).packet, draw_state);
    check(indexed_state && auto_state && !draw_state.last_indexed_draw && !draw_state.last_draw_vertices && !draw_state.context.Get(0x29f),
          "indexed_auto_and_clear_do_not_leak_prior_draw_state");
    std::array dma{0xc0055000u, 0x60000000u, 0x12345000u, 0x10u, 0x3022cu, 0u, 0x2820005cu};
    const auto transfer = Analyze(dma);
    check(transfer.Complete() && transfer.dma_transfers == 1 && transfer.last_dma && transfer.last_dma->source == 0x1012345000ull &&
          transfer.last_dma->destination == 0x3022c && transfer.last_dma->bytes == 92 && transfer.last_dma->command == 0x2820005c,
          "dma_journal_keeps_address_spaces_controls_and_byte_count_without_copying");
    dma[1] |= 2;
    check(Analyze(dma).error == Error::BadValue && Analyze(std::span(dma).first(6)).error == Error::Truncated,
          "dma_reserved_control_bits_and_truncated_packet_rejected");
    const std::array<std::uint32_t, 9> ordered{0xc0016900u, 0x200, 0x777, 0xc0012d00u, 3, 2, 0xc0016900u, 0x200, 0x770};
    Analysis stepped; unsigned depth_at_draw{};
    for (unsigned offset = 0; offset < ordered.size();) {
        const auto decoded = Decode(ordered, offset);
        if (decoded.error != Error::None || Advance(decoded.packet, stepped) != Error::None) { break; }
        if (decoded.packet.opcode == 0x2d) { depth_at_draw = stepped.context.Get(0x200).value_or(0); }
        offset = decoded.next;
    }
    check(stepped.Complete() && stepped.packets == 3 && depth_at_draw == 0x777 && stepped.context.Get(0x200) == 0x770,
          "ordered_execution_observes_draw_state_before_later_register_changes");
    std::printf("{\"event\":\"pm4_self_test\",\"passed\":%s}\n", passed ? "true" : "false"); return passed;
}
}
