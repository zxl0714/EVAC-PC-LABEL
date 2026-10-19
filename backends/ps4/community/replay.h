#pragma once
#include "protocol.h"
#include <optional>

namespace kt::community {
inline constexpr std::size_t kReplayRecordSize = 0x80000;
inline constexpr std::size_t kReplayDataOffset = 0x500, kReplayDataSize = 0x78000;
// The 1.02 native player uses one half of its 4 MiB scratch buffer.
inline constexpr std::size_t kReplaySnapshotLimit = 2 * 1024 * 1024;
struct ReplayEnvelope {
    std::string game{"ketsui"};
    std::vector<std::byte> record;
    std::uint32_t version{}, data_version{}, total_frames{}, consumed_frames{};
    std::uint32_t snapshot_compressed_bytes{}, snapshot_bytes{}, snapshot_chunks{}, key_pages{};
    std::uint32_t key_directory_bytes{}, key_page_capacity{};
    std::uint32_t score{};
    std::uint16_t kind{}, mode{}, character{}, stage{};
    std::string region;
};
// Structural inspection only: never executes state or writes a player profile.
// Snapshot framing and key directories/edge extents are checked, but core state
// values and metadata must also be validated before native player integration.
ReplayEnvelope InspectReplayEnvelope(std::span<const std::byte> compressed,std::string_view game="ketsui");
// Validates an owned raw record, then produces one bounded zlib envelope using
// the pinned codec. Compressed bytes need not equal the original game's encoder.
std::vector<std::byte> EncodeReplayEnvelope(std::span<const std::byte> record,std::string_view game="ketsui");
struct ReplayExpectation { std::uint32_t board{}; std::uint64_t score{}; };
// Checks core version, normal-play kind, game-mode family, score and ship
// category. It is not full state compatibility or proof against forged scores.
void MatchReplayMetadata(const ReplayEnvelope& replay, const ReplayExpectation& expected);
bool ReplayEnvelopeCheck(const std::filesystem::path& path, std::optional<ReplayExpectation> expected = {},std::string_view game="ketsui");
}
