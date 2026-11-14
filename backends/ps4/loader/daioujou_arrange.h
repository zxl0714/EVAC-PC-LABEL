#pragma once
#include "prepared_cache.h"
#include <array>

namespace cave::ps4 {
inline constexpr unsigned kDaioujouArrangeFirstPcm = 0x187;
inline constexpr std::array<std::string_view, 14> kDaioujouArrangeTrackNames{
    "ddo_arr_psel", "ddo_arr_govr", "ddo_arr_sclr", "ddo_arr_name",
    "ddo_arr_stg1", "ddo_arr_boss", "ddo_arr_stg2", "ddo_arr_stg3",
    "ddo_arr_stg4", "ddo_arr_stg5", "ddo_arr_lbs1", "ddo_arr_lbs3",
    "ddo_arr_aendg_full", "ddo_arr_aendg_short",
};
std::vector<std::uint8_t> PrepareDaioujouArrangeAudio(std::string_view module,
    std::span<std::uint8_t> text, std::span<const std::uint8_t> data,
    std::uint64_t original_image_size, std::uint64_t extension_address);
enum class DaioujouArrangeMotion { Direct, Pause, Options };
void ApplyDaioujouArrangeMotion(std::vector<std::uint8_t>& motion, DaioujouArrangeMotion kind, bool update);
void ApplyDaioujouArrangeArtwork(std::span<std::uint8_t> motion, bool update);
void ApplyDaioujouArrangeGadgetArtwork(std::span<std::uint8_t> motion, bool update);
void ApplyKetsuiArrangeArtwork(std::span<std::uint8_t> motion);
void ApplyKetsuiArrangeCardArtwork(std::span<std::uint8_t> motion);
void ApplyKetsuiArrangeGadgetArtwork(std::span<std::uint8_t> motion);
void AddArrangeFiles(std::vector<std::string>& files, bool merged);
void PrepareDaioujouArrangeArchive(const fs::path& data, const fs::path& stage,
    std::span<const std::uint8_t> executable, bool merged,VerificationCache* verification=nullptr);
void PrepareKetsuiArrangeArchive(const fs::path& data, const fs::path& stage,
    std::span<const std::uint8_t> executable, bool merged, const nlohmann::json& tracks);
}
