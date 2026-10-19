#include "native_text.h"
#include "native_diagnostics.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
// This input's libc uses 16-bit wide units and a 16-byte conversion state.
// Only the default, table-free encoding is provided. setlocale is not bound.
struct ConversionState {
    std::uint64_t value;
    std::uint16_t reserved;
    std::int16_t remaining;
    std::uint32_t tail;
};
static_assert(sizeof(ConversionState) == 16 && offsetof(ConversionState, remaining) == 10);
thread_local ConversionState decode_state{}, encode_state{};
std::atomic<unsigned> conversion_logs{};

void ConversionLog(const char* direction, bool query, std::size_t result) {
    if (conversion_logs.fetch_add(1, std::memory_order_relaxed) >= 32) { return; }
    const auto host_error = errno;
    std::printf("{\"event\":\"native_text_conversion\",\"direction\":\"%s\",\"query\":%s,\"units\":%llu,\"encoding_error\":%s}\n",
        direction, query ? "true" : "false", static_cast<unsigned long long>(result), result == SIZE_MAX ? "true" : "false");
    errno = host_error;
}

// Deliberately matches the local 1/2/3-byte code-unit encoding: four-byte
// UTF-8 is rejected, while surrogate units and overlong sequences are accepted.
// Do not replace this with a host code-page conversion or UTF-32 wchar_t.
int DecodeUnit(std::uint16_t* output, const char* input, std::size_t extent, ConversionState& state) {
    auto remaining = state.remaining;
    if (remaining < 0 || remaining > 2) { UnsupportedApi("multibyte_conversion_state", static_cast<unsigned short>(remaining), nullptr); }
    auto value = static_cast<std::uint16_t>(state.value);
    for (std::size_t index = 0; index < extent; ++index) {
        const auto byte = static_cast<unsigned char>(input[index]);
        if (remaining) {
            if ((byte & 0xc0) != 0x80) { SetNativeErrno(86); return -1; }
            value = static_cast<std::uint16_t>((value << 6) | (byte & 63)); --remaining;
        } else if (byte < 128) { value = byte; }
        else if ((byte & 0xe0) == 0xc0) { value = byte & 31; remaining = 1; }
        else if ((byte & 0xf0) == 0xe0) { value = byte & 15; remaining = 2; }
        else { SetNativeErrno(86); return -1; }
        if (!remaining) {
            if (output) { *output = value; }
            state.remaining = 0;
            return value ? static_cast<int>(index + 1) : 0;
        }
    }
    state.value = value; state.remaining = remaining; return -2;
}

__declspec(noinline) std::size_t DecodeString(std::uint16_t* output, const char** source, std::size_t capacity, void* opaque, const void* caller) {
    if (!source || !*source) { UnsupportedApi("mbsrtowcs_source", 0, caller); }
    auto& state = opaque ? *static_cast<ConversionState*>(opaque) : decode_state;
    auto* cursor = *source; std::size_t count{};
    // The local routine initializes this unit even for a zero-capacity call.
    if (output) { output[0] = 0; }
    while (!output || count < capacity) {
        std::uint16_t unit{};
        const auto consumed = DecodeUnit(&unit, cursor, static_cast<std::size_t>(INT32_MAX), state);
        if (consumed < 0) {
            if (output) { *source = cursor; }
            ConversionLog("to_wide16", !output, SIZE_MAX); return SIZE_MAX;
        }
        if (output) { output[count] = unit; }
        if (!consumed && !unit) {
            if (output) { *source = nullptr; }
            ConversionLog("to_wide16", !output, count); return count;
        }
        ++count; cursor += consumed;
    }
    *source = cursor; ConversionLog("to_wide16", false, count); return count;
}
std::size_t KT_SYSV MultibyteToWide(std::uint16_t* output, const char** source, std::size_t capacity, void* state) {
    return DecodeString(output, source, capacity, state, __builtin_return_address(0));
}

unsigned EncodeUnit(std::uint16_t unit, std::array<char, 3>& bytes) {
    if (unit < 128) { bytes[0] = static_cast<char>(unit); return 1; }
    if (unit < 2048) {
        bytes[0] = static_cast<char>(0xc0 | (unit >> 6)); bytes[1] = static_cast<char>(0x80 | (unit & 63)); return 2;
    }
    bytes[0] = static_cast<char>(0xe0 | (unit >> 12)); bytes[1] = static_cast<char>(0x80 | ((unit >> 6) & 63)); bytes[2] = static_cast<char>(0x80 | (unit & 63)); return 3;
}
__declspec(noinline) std::size_t EncodeString(char* output, const std::uint16_t** source, std::size_t capacity, void* opaque, const void* caller) {
    if (!source || !*source) { UnsupportedApi("wcsrtombs_source", 0, caller); }
    // In the default encoding no shift state is used or modified.
    [[maybe_unused]] auto& state = opaque ? *static_cast<ConversionState*>(opaque) : encode_state;
    auto* cursor = *source; std::size_t count{};
    while (!output || count < capacity) {
        std::array<char, 3> bytes{}; const auto size = EncodeUnit(*cursor, bytes);
        if (output && size > capacity - count) { break; }
        if (output) { std::memcpy(output + count, bytes.data(), size); }
        if (!*cursor) {
            if (output) { *source = nullptr; }
            ConversionLog("from_wide16", !output, count); return count;
        }
        count += size; ++cursor;
    }
    *source = cursor; ConversionLog("from_wide16", false, count); return count;
}
std::size_t KT_SYSV WideToMultibyte(char* output, const std::uint16_t** source, std::size_t capacity, void* state) {
    return EncodeString(output, source, capacity, state, __builtin_return_address(0));
}
std::size_t KT_SYSV WideLength(const std::uint16_t* input) {
    std::size_t size{}; while (input[size]) { ++size; } return size;
}
std::uint16_t* KT_SYSV WideCopy(std::uint16_t* output, const std::uint16_t* input, std::size_t count) {
    if (count > SIZE_MAX / 2) { UnsupportedApi("wmemcpy_extent", count, __builtin_return_address(0)); }
    return count ? static_cast<std::uint16_t*>(std::memcpy(output, input, count * 2)) : output;
}
std::uint16_t* KT_SYSV WideMove(std::uint16_t* output, const std::uint16_t* input, std::size_t count) {
    if (count > SIZE_MAX / 2) { UnsupportedApi("wmemmove_extent", count, __builtin_return_address(0)); }
    return count ? static_cast<std::uint16_t*>(std::memmove(output, input, count * 2)) : output;
}
std::uint16_t* KT_SYSV WideFill(std::uint16_t* output,std::uint16_t value,std::size_t count) {
    if (count>SIZE_MAX/2) UnsupportedApi("wmemset_extent",count,__builtin_return_address(0));
    if (count) std::fill_n(output,count,value);
    return output;
}
int KT_SYSV WideCompare(const std::uint16_t* first, const std::uint16_t* second, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        if (first[index] != second[index]) { return first[index] < second[index] ? -1 : 1; }
    }
    return 0;
}
} // namespace

std::uint64_t ResolveTextImport(std::string_view identifier) {
    const auto hash = identifier.substr(0, identifier.find('#'));
    if (hash == "8hygs6D9KBY") { return reinterpret_cast<std::uint64_t>(&MultibyteToWide); }
    if (hash == "sOOMlZoy1pg") { return reinterpret_cast<std::uint64_t>(&WideToMultibyte); }
    if (hash == "WkkeywLJcgU") { return reinterpret_cast<std::uint64_t>(&WideLength); }
    if (hash == "fL3O02ypZFE") { return reinterpret_cast<std::uint64_t>(&WideCopy); }
    if (hash == "Noj9PsJrsa8") { return reinterpret_cast<std::uint64_t>(&WideMove); }
    if (hash == "QJ5xVfKkni0") { return reinterpret_cast<std::uint64_t>(&WideCompare); }
    if (hash == "Al8MZJh-4hM") { return reinterpret_cast<std::uint64_t>(&WideFill); }
    return 0;
}

bool TextSelfTest() {
    bool passed = true; unsigned cases{};
    const auto check = [&](const char* name, bool result) {
        ++cases; passed &= result; std::printf("{\"event\":\"text_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, result ? "true" : "false");
    };
    const char input[] = "A\xc3\xa9\xe7\xb5\x86";
    const char* source = input; ConversionState state{}; state.reserved = 73; state.tail = 91;
    const auto initial = state;
    check("length_query_ignores_capacity_and_preserves_source", MultibyteToWide(nullptr, &source, 0, &state) == 3 && source == input && std::memcmp(&state, &initial, 16) == 0);
    std::array<std::uint16_t, 5> wide; wide.fill(0xa5a5);
    check("decode_writes_sixteen_bit_units_and_terminator_only", MultibyteToWide(wide.data(), &source, 4, &state) == 3 && !source && wide[0] == 'A' && wide[1] == 0xe9 && wide[2] == 0x7d46 && !wide[3] && wide[4] == 0xa5a5);
    wide.fill(0xa5a5); source = input;
    check("bounded_decode_advances_to_next_character_without_terminator", MultibyteToWide(wide.data(), &source, 2, &state) == 2 && source == input + 3 && wide[1] == 0xe9 && wide[2] == 0xa5a5);
    check("decode_resume_completes_and_retires_source", MultibyteToWide(wide.data() + 2, &source, 2, &state) == 1 && !source && wide[2] == 0x7d46 && !wide[3]);
    source = input; wide.fill(0xa5a5);
    check("zero_capacity_matches_local_first_unit_initialization", MultibyteToWide(wide.data(), &source, 0, &state) == 0 && source == input && !wide[0] && wide[1] == 0xa5a5);
    const char invalid[] = "A\xff"; source = invalid; wide.fill(0xa5a5);
    check("encoding_error_keeps_pointer_at_failing_sequence_and_tail", MultibyteToWide(wide.data(), &source, 4, &state) == SIZE_MAX && source == invalid + 1 && wide[0] == 'A' && wide[1] == 0xa5a5);
    source = invalid;
    check("query_error_never_advances_source", MultibyteToWide(nullptr, &source, 0, &state) == SIZE_MAX && source == invalid);
    const char incomplete[] = "\xe7\xb5"; source = incomplete;
    check("nul_cannot_complete_a_partial_multibyte_character", MultibyteToWide(wide.data(), &source, 4, &state) == SIZE_MAX && source == incomplete);
    const char four_byte[] = "\xf0\x9f\x98\x80"; source = four_byte;
    check("local_encoding_rejects_four_byte_utf8", MultibyteToWide(wide.data(), &source, 4, &state) == SIZE_MAX && source == four_byte);
    const char overlong[] = "\xc1\x81"; source = overlong;
    check("local_overlong_code_unit_behavior_is_preserved", MultibyteToWide(wide.data(), &source, 4, &state) == 1 && !source && wide[0] == 'A');
    std::uint16_t unit = 0xa5a5; state = {};
    check("partial_decoder_records_only_target_state_fields", DecodeUnit(&unit, "\xe7", 1, state) == -2 && unit == 0xa5a5 && state.value == 7 && state.remaining == 2);
    const char continuation[] = "\xb5\x86"; source = continuation;
    check("string_conversion_can_resume_an_existing_partial_state", MultibyteToWide(wide.data(), &source, 4, &state) == 1 && !source && wide[0] == 0x7d46 && state.remaining == 0);
    const std::array<std::uint16_t, 4> original{'A', 0xe9, 0x7d46, 0}; const auto* wide_source = original.data();
    std::array<char, 12> bytes; bytes.fill(73); state = initial;
    check("encode_query_counts_bytes_and_preserves_pointer_and_state", WideToMultibyte(nullptr, &wide_source, 0, &state) == 6 && wide_source == original.data() && std::memcmp(&state, &initial, 16) == 0);
    check("encode_writes_real_bytes_terminator_and_preserves_tail", WideToMultibyte(bytes.data(), &wide_source, 7, &state) == 6 && !wide_source && std::memcmp(bytes.data(), input, 7) == 0 && bytes[7] == 73);
    wide_source = original.data(); bytes.fill(73);
    check("bounded_encode_never_emits_partial_character", WideToMultibyte(bytes.data(), &wide_source, 2, &state) == 1 && wide_source == original.data() + 1 && bytes[0] == 'A' && bytes[1] == 73);
    check("bounded_encode_resume_retains_nul_until_it_fits", WideToMultibyte(bytes.data() + 1, &wide_source, 5, &state) == 5 && wide_source == original.data() + 3 && bytes[6] == 73);
    check("encode_resume_can_write_only_the_terminator", WideToMultibyte(bytes.data() + 6, &wide_source, 1, &state) == 0 && !wide_source && !bytes[6] && bytes[7] == 73);
    const std::array<std::uint16_t, 3> surrogates{0xd83d, 0xde00, 0}; wide_source = surrogates.data(); bytes.fill(73);
    check("surrogates_remain_separate_three_byte_code_units", WideToMultibyte(bytes.data(), &wide_source, bytes.size(), &state) == 6 && std::memcmp(bytes.data(), "\xed\xa0\xbd\xed\xb8\x80\0", 7) == 0);
    source = bytes.data(); wide.fill(0xa5a5);
    check("surrogate_code_units_round_trip_without_utf32_expansion", MultibyteToWide(wide.data(), &source, 4, &state) == 2 && wide[0] == 0xd83d && wide[1] == 0xde00 && !wide[2] && wide[3] == 0xa5a5);
    wide_source = original.data(); bytes.fill(73);
    check("zero_capacity_encode_does_not_write_or_advance", WideToMultibyte(bytes.data(), &wide_source, 0, nullptr) == 0 && wide_source == original.data() && bytes[0] == 73);
    check("wide_length_counts_code_units_not_bytes_or_scalars", WideLength(original.data()) == 3 && WideLength(surrogates.data()) == 2);
    source = input; wide.fill(0xa5a5);
    check("null_state_uses_separate_thread_local_decode_state", MultibyteToWide(wide.data(), &source, 4, nullptr) == 3 && !source && wide[2] == 0x7d46);
    wide.fill(0xa5a5);
    check("wide_copy_uses_code_unit_count_and_returns_destination", WideCopy(wide.data(), original.data(), 3) == wide.data() && wide[0] == 'A' && wide[1] == 0xe9 && wide[2] == 0x7d46 && wide[3] == 0xa5a5);
    check("wide_move_handles_forward_overlap", WideMove(wide.data() + 1, wide.data(), 3) == wide.data() + 1 && wide[0] == 'A' && wide[1] == 'A' && wide[2] == 0xe9 && wide[3] == 0x7d46 && wide[4] == 0xa5a5);
    check("wide_move_handles_backward_overlap", WideMove(wide.data(), wide.data() + 1, 3) == wide.data() && wide[0] == 'A' && wide[1] == 0xe9 && wide[2] == 0x7d46 && wide[3] == 0x7d46);
    const std::array<std::uint16_t, 3> less{0x100, 0, 1}, greater{0x100, 0, 0xff01};
    check("wide_compare_uses_unsigned_units_and_continues_past_nul", WideCompare(less.data(), greater.data(), 3) == -1 && WideCompare(greater.data(), less.data(), 3) == 1 && WideCompare(less.data(), greater.data(), 2) == 0);
    const std::array<std::uint16_t, 1> low{0xff}, high{0x100};
    check("wide_compare_is_not_little_endian_byte_comparison", WideCompare(low.data(), high.data(), 1) == -1 && WideCompare(high.data(), low.data(), 1) == 1);
    check("zero_wide_extent_does_not_dereference_null", !WideCopy(nullptr, nullptr, 0) && !WideMove(nullptr, nullptr, 0) &&
        !WideFill(nullptr,0,0) && WideCompare(nullptr, nullptr, 0) == 0);
    wide.fill(73);
    check("wide_fill_writes_16_bit_units_and_preserves_neighbors",WideFill(wide.data()+1,0x1234,2)==wide.data()+1 &&
        wide[0]==73 && wide[1]==0x1234 && wide[2]==0x1234 && wide[3]==73);
    std::printf("{\"event\":\"text_self_test\",\"cases\":%u,\"passed\":%s}\n", cases, passed ? "true" : "false"); return passed;
}
