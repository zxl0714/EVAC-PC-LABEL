#include "replay.h"
#include <zlib.h>
#include <algorithm>
#include <bit>
#include <cstdio>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace kt::community {
namespace {
void Require(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}
struct RecordContract {std::size_t bytes,data_bytes;std::uint32_t version,data_version;std::string_view region;};
RecordContract ContractFor(std::string_view game) {
    (void)ProtocolFor(game);
    // The title's local save has 0x65600-byte records. RankingEntry instead
    // uses system/config/struct_replay.psb: a 0x80000-byte network structure
    // with a 0x78000-byte data field. Keep its own version/core contract;
    // equal network sizes do not make a Ketsui record valid for DaiOuJou.
    if (game=="daioujou") return {0x80000,0x78000,0x10030,0x10000,"ARCADE_01030"};
    return {kReplayRecordSize,kReplayDataSize,0x10001,0x10001,"ARCADE_01000"};
}
bool DaioujouRegion(std::string_view region) {
    // The 1.04 title can select any of its four shipped core revisions. The
    // container version is independent of that selection; an actual ARCADE
    // recording uses container 0x10030 with region ARCADE_01000.
    return region=="ARCADE_01000" || region=="ARCADE_01010" || region=="ARCADE_01020" || region=="ARCADE_01030";
}
std::uint32_t U32(std::span<const std::byte> data, std::size_t offset) {
    Require(offset <= data.size() && data.size() - offset >= 4, "field_bounds");
    std::uint32_t value{};
    for (unsigned index = 0; index < 4; ++index) {
        value |= std::to_integer<std::uint32_t>(data[offset + index]) << (index * 8);
    }
    return value;
}
std::uint16_t U16(std::span<const std::byte> data, std::size_t offset) {
    Require(offset <= data.size() && data.size() - offset >= 2, "field_bounds");
    return static_cast<std::uint16_t>(std::to_integer<unsigned>(data[offset]) |
        (std::to_integer<unsigned>(data[offset + 1]) << 8));
}
std::string Text(std::span<const std::byte> data, std::size_t offset, std::size_t capacity) {
    const auto field = data.subspan(offset, capacity);
    const auto end = std::find(field.begin(), field.end(), std::byte{});
    Require(end != field.end(), "unterminated_metadata");
    return {reinterpret_cast<const char*>(field.data()), static_cast<std::size_t>(end - field.begin())};
}
std::vector<std::byte> Inflate(std::span<const std::byte> data, std::size_t limit) {
    Require(!data.empty() && data.size() <= kReplayLimit && limit < (std::numeric_limits<uInt>::max)(), "compressed_bounds");
    std::vector<std::byte> plain(limit + 1);
    z_stream stream{};
    Require(inflateInit(&stream) == Z_OK, "inflate_init");
    struct End { z_stream* stream; ~End() { inflateEnd(stream); } } end{&stream};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = reinterpret_cast<Bytef*>(plain.data());
    stream.avail_out = static_cast<uInt>(plain.size());
    // One bounded call with all input and one extra output byte distinguishes
    // an exact limit from an over-expanding stream. No retry loop can spin.
    const auto result = inflate(&stream, Z_FINISH);
    Require(stream.total_out <= limit, "inflate_output_limit");
    Require(result == Z_STREAM_END, "invalid_or_truncated_zlib");
    Require(stream.avail_in == 0 && stream.total_in == data.size(), "trailing_zlib_data");
    plain.resize(stream.total_out);
    return plain;
}
unsigned Chunks(std::span<const std::byte> state) {
    std::size_t offset{};
    std::set<std::string> names;
    while (offset < state.size()) {
        Require(names.size() < 4096, "snapshot_chunk_limit");
        const auto length = std::to_integer<unsigned>(state[offset++]);
        Require(length && state.size() - offset >= length + 4, "snapshot_chunk_header");
        const std::string name(reinterpret_cast<const char*>(state.data() + offset), length);
        Require(std::all_of(name.begin(), name.end(), [](unsigned char c) { return c >= 0x20 && c <= 0x7e; }) &&
                names.insert(name).second, "snapshot_chunk_name");
        offset += length;
        const auto bytes = U32(state, offset); offset += 4;
        Require(bytes <= state.size() - offset, "snapshot_chunk_extent");
        offset += bytes;
    }
    Require(!names.empty(), "empty_snapshot");
    return static_cast<unsigned>(names.size());
}
void KeyDirectory(ReplayEnvelope& replay, unsigned players, unsigned mask,std::size_t data_bytes) {
    Require(replay.total_frames % 512 == 0, "key_frame_alignment");
    const std::uint64_t stride = replay.total_frames / 512;
    const std::uint64_t lines = static_cast<std::uint64_t>(players) * std::popcount(mask);
    const auto directory_bytes = stride * lines;
    const auto remaining = data_bytes - replay.snapshot_compressed_bytes;
    Require(directory_bytes < remaining, "key_directory_extent");
    const auto capacity = (remaining - directory_bytes) / 8;
    // The native reader stops before copying a word when its cursor reaches
    // capacity. The writer likewise leaves its final capacity slot unused.
    Require(capacity && replay.key_pages < capacity, "key_page_capacity");
    replay.key_directory_bytes = static_cast<unsigned>(directory_bytes);
    replay.key_page_capacity = static_cast<unsigned>(capacity);
    const std::span<const std::byte> record(replay.record);
    const auto directory = record.subspan(kReplayDataOffset + replay.snapshot_compressed_bytes,
                                          static_cast<std::size_t>(directory_bytes));
    std::size_t population{};
    for (const auto value : directory) { population += std::popcount(std::to_integer<unsigned>(value)); }
    Require(population == replay.key_pages, "key_page_population");
    const auto words = record.subspan(kReplayDataOffset + replay.snapshot_compressed_bytes +
                                      static_cast<std::size_t>(directory_bytes), replay.key_pages * 8ull);
    std::size_t cursor{};
    // Bitmap planes are per input-bit. Packed edge words are instead ordered
    // by 64-frame block, then player/bit, matching +0x8f00 in the 1.02 player.
    // The loop is bounded by eight times the checked directory byte count.
    for (std::uint64_t page = 0; page < replay.total_frames / 64; ++page) {
        for (std::uint64_t line = 0; line < lines; ++line) {
            const auto flags = std::to_integer<unsigned>(directory[static_cast<std::size_t>(line * stride + page / 8)]);
            if (!(flags & (1u << (page % 8)))) { continue; }
            Require(page * 64 < replay.consumed_frames, "future_key_page");
            const auto word = U32(words, cursor * 8) | (std::uint64_t{U32(words, cursor * 8 + 4)} << 32);
            const auto frames = (std::min)(std::uint64_t{64}, replay.consumed_frames - page * 64);
            Require(word && (frames == 64 || word >> frames == 0), "key_edge_extent");
            ++cursor;
        }
    }
    Require(cursor == replay.key_pages, "key_page_cursor");
}
}
ReplayEnvelope InspectReplayEnvelope(std::span<const std::byte> compressed,std::string_view game) {
    const auto contract=ContractFor(game);
    ReplayEnvelope result;
    result.game=game;
    result.record = Inflate(compressed, contract.bytes);
    Require(result.record.size() == contract.bytes, "record_size");
    const std::span<const std::byte> record(result.record);
    result.version = U32(record, 0); result.data_version = U32(record, 4);
    Require(result.version == contract.version && result.data_version == contract.data_version, "unsupported_record_version");
    Require(U32(record, 8) <= contract.data_bytes && U32(record, 12) == 0, "unsupported_record_extents");
    result.total_frames = U32(record, 16); result.consumed_frames = U32(record, 20);
    Require(result.total_frames && result.consumed_frames && result.consumed_frames <= result.total_frames, "frame_bounds");
    result.snapshot_compressed_bytes = U32(record, 24);
    const auto players = U32(record, 28), mask = U32(record, 32);
    result.key_pages = U32(record, 36);
    Require(result.snapshot_compressed_bytes && result.snapshot_compressed_bytes <= contract.data_bytes &&
            players && players <= 8 && mask, "replay_header_bounds");
    KeyDirectory(result, players, mask,contract.data_bytes);
    Require(!Text(record, 48, 16).empty(), "missing_record_title");
    result.region = Text(record, 80, 16);
    Require(!result.region.empty() && std::all_of(result.region.begin(), result.region.end(), [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }), "unsupported_region_syntax");
    (void)Text(record, 120, 17); (void)Text(record, 137, 17);
    result.kind = U16(record, 104); result.mode = U16(record, 106);
    result.score = U32(record, 108); result.character = U16(record, 112); result.stage = U16(record, 114);
    const auto snapshot = Inflate(record.subspan(kReplayDataOffset, result.snapshot_compressed_bytes), kReplaySnapshotLimit);
    result.snapshot_bytes = static_cast<std::uint32_t>(snapshot.size());
    result.snapshot_chunks = Chunks(snapshot);
    return result;
}
void MatchReplayMetadata(const ReplayEnvelope& replay, const ReplayExpectation& expected) {
    const auto group = expected.board / 10, ship = expected.board % 10;
    if (replay.game=="daioujou") {
        Require((group==0 || (group>=2 && group<=6)) && ship<=6,"unsupported_replay_board");
        const auto mode=group==0 ? 0u : group==2 ? 1u : group==3 ? 2u : group==5 ? 5u : 4u;
        // rankingModeConv maps eligible temporary CUSTOM (3) to ARCADE (0),
        // while ranking_ctrl writes the unconverted GameMode to the network
        // attachment. Native checkRegistRanking still excludes ALL-MIX/custom
        // submissions; board group 1 is not enabled here. Preserve raw bytes.
        const bool native_arcade_alias=group==0 && replay.mode==3;
        Require(DaioujouRegion(replay.region) && replay.kind==0 && (replay.mode==mode || native_arcade_alias),"replay_mode_mismatch");
        Require(expected.score==replay.score,"replay_score_mismatch");
        Require(replay.character<6 && (ship==6 || ship==replay.character),"replay_ship_mismatch");
        return;
    }
    Require(replay.game=="ketsui","unsupported_replay_title");
    // Native CUSTOM has its own combined/Type-A/Type-B boards (60/61/62).
    // It keeps mode 3 and its original settings inside the unchanged snapshot;
    // the native title does not partition these boards by a settings hash.
    Require(group <= 6 && ship <= 2, "unsupported_replay_board"); // Board 99 has no replay.
    const auto mode = group <= 2 ? 0u : group == 3 ? 1u : group == 4 ? 2u : group == 5 ? 4u : 3u;
    Require(replay.region == "ARCADE_01000" && replay.kind == 0 && replay.mode == mode, "replay_mode_mismatch");
    Require(expected.score == replay.score, "replay_score_mismatch");
    Require(replay.character < 8 && (!ship || ship == replay.character / 4 + 1), "replay_ship_mismatch");
}
std::vector<std::byte> EncodeReplayEnvelope(std::span<const std::byte> record,std::string_view game) {
    const auto contract=ContractFor(game);
    Require(record.size() == contract.bytes, "record_size");
    auto capacity = compressBound(static_cast<uLong>(record.size()));
    Require(capacity <= contract.bytes + 1024, "encoded_record_limit");
    std::vector<std::byte> encoded(capacity);
    Require(compress2(reinterpret_cast<Bytef*>(encoded.data()), &capacity,
                      reinterpret_cast<const Bytef*>(record.data()), static_cast<uLong>(record.size()), 6) == Z_OK,
            "record_encode_failed");
    encoded.resize(capacity);
    // Use exactly the existing upload validator, including the inner snapshot
    // and key extents. This does not claim native state compatibility.
    Require(InspectReplayEnvelope(encoded,game).record == std::vector<std::byte>(record.begin(), record.end()), "record_encode_roundtrip");
    return encoded;
}
bool ReplayEnvelopeCheck(const std::filesystem::path& path, std::optional<ReplayExpectation> expected,std::string_view game) {
    try {
        // Read at most the protocol limit plus one byte. File replacement or
        // growth during the read cannot allocate according to an untrusted size.
        std::ifstream file(path, std::ios::binary);
        Require(bool(file), "input_open");
        std::vector<std::byte> bytes(kReplayLimit + 1);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        Require(!file.bad(), "input_read");
        bytes.resize(static_cast<std::size_t>(file.gcount()));
        const auto replay = InspectReplayEnvelope(bytes,game);
        if (expected) { MatchReplayMetadata(replay, *expected); }
        const Json result{{"event", "community_replay_envelope_check"}, {"game",game}, {"valid_envelope", true},
            {"compressed_bytes", bytes.size()}, {"record_bytes", replay.record.size()}, {"record_sha256", Sha256(replay.record)},
            {"version", replay.version}, {"data_version", replay.data_version}, {"total_frames", replay.total_frames},
            {"consumed_frames", replay.consumed_frames}, {"snapshot_compressed_bytes", replay.snapshot_compressed_bytes},
            {"snapshot_bytes", replay.snapshot_bytes}, {"snapshot_chunks", replay.snapshot_chunks}, {"key_pages", replay.key_pages},
            {"key_directory_bytes", replay.key_directory_bytes}, {"key_page_capacity", replay.key_page_capacity},
            {"kind", replay.kind}, {"mode", replay.mode}, {"metadata_matched", bool(expected)},
            {"region", replay.region}, {"score", replay.score}, {"character", replay.character}, {"stage", replay.stage},
            {"native_playback_validated", false}, {"scope", "envelope_snapshot_framing_and_key_extents"}};
        std::puts(result.dump().c_str()); return true;
    } catch (const std::exception&) {
        std::puts("{\"event\":\"community_replay_envelope_check\",\"valid_envelope\":false,\"native_playback_validated\":false}");
        return false;
    }
}
}
