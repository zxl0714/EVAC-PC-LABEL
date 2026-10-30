#pragma once
#include "read_patches.h"
#include <nlohmann/json.hpp>

namespace cave::ps4 {
using ResourceBytes = std::vector<std::uint8_t>;
enum class ResourceFix { MusicAutosave, ReplayBatch, LocalRoundGate, CommunityReplayPeer, CommunityLocalReplay, EspradeLocalReplay, FeveronLocalReplay, GareggaLocalReplay };
// Decode a verified program's file-backed seed. Also used by ROM-free tests.
ResourceBytes ReadArchiveSeed(std::span<const std::uint8_t> source, std::uint64_t load, bool relative = false);
// Memory-only codec and checked transforms. The archive seed and script bodies
// come from local, version-verified data; neither is compiled into the program.
ResourceBytes ArchiveMask(std::span<const std::uint8_t> seed, std::string_view name);
ResourceBytes UnwrapResource(std::span<const std::uint8_t> source,
                            std::span<const std::uint8_t> seed, std::string_view name);
ResourceBytes WrapResource(std::span<const std::uint8_t> source,
                          std::span<const std::uint8_t> seed, std::string_view name, std::size_t extent);
nlohmann::json ParseResourceIndex(std::span<const std::uint8_t> source);
ResourceBytes FixResourceScript(std::span<const std::uint8_t> source, ResourceFix fix);
ResourceBytes EncodeReadFixes(const kt::content::ReadPatchSet& patches);
struct PreparedReadFixes { ResourceBytes bytes; nlohmann::json report; };
PreparedReadFixes PrepareReadFixes(const std::filesystem::path& data,
                                 bool community_replay, bool community_offline);
PreparedReadFixes PrepareDaioujouReadFixes(const std::filesystem::path& data);
PreparedReadFixes PrepareEspradeReadFixes(const std::filesystem::path& data);
PreparedReadFixes PrepareFeveronReadFixes(const std::filesystem::path& data);
PreparedReadFixes PrepareGareggaReadFixes(const std::filesystem::path& data);
int PrepareReadFixesCommand(int argc, wchar_t** argv);
}
