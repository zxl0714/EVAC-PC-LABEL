#include "ketsui_arrange_audio.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <stdexcept>

namespace cave::ps4 {
namespace {
using Bytes = std::span<std::uint8_t>;
constexpr std::size_t kStubSize = 48;
constexpr std::size_t kRowSize = 12;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void Expect(Bytes bytes, std::size_t at, std::initializer_list<std::uint8_t> expected) {
    Require(at <= bytes.size() && expected.size() <= bytes.size() - at &&
        std::equal(expected.begin(), expected.end(), bytes.begin() + static_cast<std::ptrdiff_t>(at)),
        "Ketsui BGM instructions differ from the verified core");
}

std::uint32_t Read32(std::span<const std::uint8_t> bytes, std::size_t at) {
    Require(at <= bytes.size() && 4 <= bytes.size() - at, "Truncated Ketsui BGM table");
    std::uint32_t value{};
    for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(bytes[at + i]) << (8 * i);
    return value;
}

void Write32(Bytes bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

std::uint64_t Address(Bytes code, std::size_t operand, bool short_branch = false) {
    const auto offset = short_branch ? std::bit_cast<std::int8_t>(code[operand]) :
        std::bit_cast<std::int32_t>(Read32(code, operand));
    const auto address = static_cast<std::int64_t>(operand) + (short_branch ? 1 : 4) + offset;
    Require(address >= 0, "Ketsui BGM target is outside the image");
    return static_cast<std::uint64_t>(address);
}

std::uint64_t Target(Bytes code, std::size_t operand, bool short_branch) {
    const auto address = Address(code, operand, short_branch);
    Require(address < code.size(), "Ketsui BGM branch leaves the original text");
    return address;
}

std::size_t FindSite(Bytes code, std::initializer_list<int> signature, const char* error) {
    const auto equal=[](std::uint8_t byte, int expected) { return expected < 0 || byte == expected; };
    const auto site=std::search(code.begin(),code.end(),signature.begin(),signature.end(),equal);
    Require(site!=code.end() && std::search(site+1,code.end(),signature.begin(),signature.end(),equal)==code.end(),error);
    return static_cast<std::size_t>(site-code.begin());
}

std::size_t FindAttribute(Bytes text, Bytes attributes, std::uint64_t start,
                          std::span<const KetsuiAudioRelocation> relative, std::size_t setter) {
    const auto name=FindSite(text,{'x','-','b','g','m',0},"Ketsui x-bgm attribute name is missing or ambiguous");
    const auto entry=std::ranges::find(relative,name,&KetsuiAudioRelocation::target);
    Require(entry!=relative.end() && std::find_if(entry+1,relative.end(),[&](const auto& x){return x.target==name;})==relative.end() &&
        entry->address>=start && entry->address-start<=attributes.size() &&
        24<=attributes.size()-(entry->address-start), "Ketsui x-bgm attribute record is missing or ambiguous");
    const auto record=static_cast<std::size_t>(entry->address-start);
    Expect(attributes,record+4,{0,0,0,0,0,0,0,0,4,0,0,0});
    Require(Read32(attributes,record)==name, "Unexpected Ketsui x-bgm attribute name pointer");
    const auto handler=std::ranges::find(relative,entry->address+16,&KetsuiAudioRelocation::address);
    Require(handler!=relative.end() && handler->target<=text.size() && 5<=text.size()-handler->target &&
        handler->target==Read32(attributes,record+16) && Read32(attributes,record+20)==0,
        "Ketsui x-bgm handler does not match the attribute record");
    Expect(text,handler->target,{0xe9});
    Require(Target(text,handler->target+1,false)==setter,"Ketsui x-bgm handler does not dispatch to the located setter");
    return record+12;
}

void Relative(Bytes bytes, std::size_t at, std::uint64_t origin, std::uint64_t destination) {
    const auto offset = static_cast<std::int64_t>(destination) - static_cast<std::int64_t>(origin + at + 4);
    Require(offset >= INT32_MIN && offset <= INT32_MAX, "Ketsui BGM branch is out of range");
    Write32(bytes, at, static_cast<std::uint32_t>(offset));
}

void Jump(Bytes text, std::size_t at, std::size_t replaced, std::uint64_t stub) {
    text[at] = 0xe9;
    Relative(text, at + 1, 0, stub);
    std::ranges::fill(text.subspan(at + 5, replaced - 5), 0x90);
}

void PatchPlay(Bytes text, Bytes stub, std::uint64_t origin, std::uint64_t table, std::size_t at,
              std::size_t lookup, std::uint8_t mode_register, std::uint8_t cue_compare, bool event) {
    // All paths reach the original PCM row reader. Replay the original range
    // test unchanged for modes 0..4.
    const auto compare = static_cast<std::uint8_t>(mode_register | 0x30);
    Expect(text, at, {0xff, mode_register, 0x83, compare, 0x03,
        static_cast<std::uint8_t>(event ? 0x0f : 0x77)});
    if (event) Expect(text, at + 6, {0x87});
    const auto previous_fallback = Target(text, at + (event ? 7 : 6), !event);
    const auto previous_switch = at + (event ? 11 : 7);
    Expect(text, previous_switch, {0x48, 0x8d});
    Expect(text, lookup + 2, {0x48, 0x8d, 0x0c, 0x40, 0x48, 0x8d, 0x04, 0x8a});
    const std::array<std::uint8_t, 44> body{
        0xff, mode_register, 0x83, compare, 0x04,
        0x75, 0x15,                         // jne previous switch
        0x83, cue_compare, 0x0c,             // cue must be 0..12
        0x0f, 0x87, 0, 0, 0, 0,             // ja previous fallback
        0x48, 0x8d, 0x15, 0, 0, 0, 0,       // lea rdx, [table]
        0xe9, 0, 0, 0, 0,                   // jmp lookup
        0x83, compare, 0x03,
        0x0f, 0x87, 0, 0, 0, 0,             // ja previous fallback
        0xe9, 0, 0, 0, 0,                   // jmp previous switch
        0x90, 0x90,
    };
    std::ranges::copy(body, stub.begin());
    Relative(stub, 12, origin, previous_fallback);
    Relative(stub, 19, origin, table);
    Relative(stub, 24, origin, lookup);
    Relative(stub, 33, origin, previous_fallback);
    Relative(stub, 38, origin, previous_switch);
    Jump(text, at, previous_switch - at, origin);
}

void PatchFade(Bytes text, Bytes stub, std::uint64_t origin, std::uint64_t table, std::size_t at) {
    Expect(text, at, {0xff, 0xcb, 0x45, 0x31, 0xf6, 0x83, 0xfb, 0x03, 0x77, 0x19});
    const auto previous_fallback = Target(text, at + 9, true);
    Expect(text, previous_fallback - 5, {0x44, 0x8b, 0x74, 0x81, 0x08});
    const std::array<std::uint8_t, 43> body{
        0xff, 0xcb, 0x45, 0x31, 0xf6,       // dec ebx; xor r14d, r14d
        0x83, 0xfb, 0x04, 0x75, 0x13,      // original switch unless mode 5
        0x83, 0xf8, 0x0c, 0x77, 0x0e,      // cue must be 0..12
        0x48, 0x8d, 0x0d, 0, 0, 0, 0,      // lea rcx, [table]
        0xe9, 0, 0, 0, 0,                  // jmp original row calculation
        0x90, 0x90,
        0x83, 0xfb, 0x03, 0x0f, 0x87, 0, 0, 0, 0,
        0xe9, 0, 0, 0, 0,
    };
    std::ranges::copy(body, stub.begin());
    Relative(stub, 18, origin, table);
    Relative(stub, 23, origin, previous_fallback - 9);
    Relative(stub, 34, origin, previous_fallback);
    Relative(stub, 39, origin, at + 10);
    Jump(text, at, 10, origin);
}
}

std::vector<std::uint8_t> PrepareKetsuiArrangeAudio(
    Bytes text, Bytes attributes, std::uint64_t attributes_address,
    std::span<const std::uint8_t> data, std::uint64_t data_address,
    std::span<const KetsuiAudioRelocation> relative, std::uint64_t extension_address) {
    // Verify unique instruction sequences, then verify the relationships between
    // the attribute, switch cases and cue table before changing the prepared image.
    constexpr int u=-1;
    const auto at=FindSite(text,{0x48,0x85,0xff,0x74,0x17,0x83,0xfe,4,0x77,0x12,0x89,0xf0,
        0x48,0x8d,0x0d,u,u,u,u,0x8b,4,0x81,0x89,5,u,u,u,u,0xc3},"Ketsui BGM setter is missing or ambiguous");
    const auto limit=FindAttribute(text,attributes,attributes_address,relative,at);
    const auto play=FindSite(text,{0xff,0xca,0x83,0xfa,3,0x77,0x19,0x48,0x8d,5,u,u,u,u,0x48,0x63,0x14,0x90,
        0x48,1,0xc2,0xff,0xe2,0x48,0x8d,0x15,u,u,u,u,0xeb,0x1f},"Ketsui direct BGM playback is missing or ambiguous");
    const auto info=FindSite(text,{0xff,0xcf,0x83,0xff,3,0x77,0x19,0x48,0x8d,5,u,u,u,u,0x48,0x63,0x0c,0xb8,
        0x48,1,0xc1,0xff,0xe1,0x48,0x8d,0x15,u,u,u,u,0xeb,0x1e},"Ketsui direct BGM query is missing or ambiguous");
    const auto event=FindSite(text,{0xff,0xcb,0x83,0xfb,3,0x0f,0x87,u,u,u,u,0x48,0x8d,0x0d,u,u,u,u,0x48,
        0x63,0x14,0x99,0x48,1,0xca,0xff,0xe2,0x48,0x8d,0x15,u,u,u,u,0xe9,u,u,u,u},"Ketsui event BGM playback is missing or ambiguous");
    const auto fade=FindSite(text,{0xff,0xcb,0x45,0x31,0xf6,0x83,0xfb,3,0x77,0x19,0x48,0x63,0xcb,0x48,0x8d,
        0x15,u,u,u,u,0x89,0xc0,0x48,0x8b,0x0c,0xca,0x48,0x8d,4,0x40,0x44,0x8b,0x74,0x81,8},"Ketsui event BGM fade is missing or ambiguous");
    const auto stereo=Address(text,play+26);
    Require(Address(text,info+26)==stereo && Address(text,event+30)==stereo && stereo>=data_address,
        "Ketsui playback paths disagree about the stereo cue table");
    Require(Target(text,play+31,true)==play+0x3f && Target(text,info+31,true)==info+0x3e &&
        Target(text,event+35,false)==event+0x173,"Ketsui BGM playback paths disagree about the row reader");
    const auto fade_table=Address(text,fade+16);
    const auto fade_entry=std::ranges::find(relative,fade_table,&KetsuiAudioRelocation::address);
    Require(fade_entry!=relative.end() && fade_entry->target==stereo,"Ketsui fade path does not use the stereo cue table");
    const auto previous_modes = Target(text, at + 15, false);
    Require(previous_modes + 20 <= text.size(), "Ketsui BGM mode table is incomplete");
    for (unsigned i = 0; i < 5; ++i) {
        Require(Read32(text, previous_modes + 4 * i) == (i < 2 ? 1 - i : i), "Unexpected Ketsui BGM mode order");
    }

    constexpr auto pcm_offset = 4 * kStubSize;
    constexpr auto modes_offset = pcm_offset + kKetsuiArrangeCueCount * kRowSize;
    const auto stereo_offset = stereo-data_address;
    Require(stereo_offset<=data.size() && kKetsuiArrangeCueCount*kRowSize<=data.size()-stereo_offset,
        "Ketsui BGM table is incomplete");
    std::vector<std::uint8_t> extension(modes_offset + 24, 0x90);
    Bytes output = extension;
    for (unsigned cue = 0; cue < kKetsuiArrangeCueCount; ++cue) {
        const auto row = data.subspan(stereo_offset + cue * kRowSize, kRowSize);
        Require(Read32(row, 0) == 0x100 + cue, "Unexpected Ketsui stereo cue");
        std::ranges::copy(row, output.subspan(pcm_offset + cue * kRowSize).begin());
        Write32(output, pcm_offset + cue * kRowSize, kKetsuiArrangeFirstPcm + cue);
    }
    std::ranges::copy(text.subspan(previous_modes, 20), output.subspan(modes_offset).begin());
    Write32(output, modes_offset + 20, 5);
    attributes[limit]=5;
    text[at + 7] = 5;
    Relative(text, at + 15, 0, extension_address + modes_offset);
    const auto table = extension_address + pcm_offset;
    PatchPlay(text, output.first(kStubSize), extension_address, table, play, play+0x3f, 0xca, 0xf9, false);
    PatchPlay(text, output.subspan(kStubSize, kStubSize), extension_address + kStubSize, table,
        info, info+0x3e, 0xcf, 0xfe, false);
    PatchPlay(text, output.subspan(2 * kStubSize, kStubSize), extension_address + 2 * kStubSize, table,
        event, event+0x173, 0xcb, 0xf8, true);
    PatchFade(text, output.subspan(3 * kStubSize, kStubSize), extension_address + 3 * kStubSize, table, fade);
    return extension;
}
}
