#pragma once
#include "common/catalogue.h"
#include "common/config_update.h"
#include "common/launch.h"
#include <array>

namespace cave::launcher {
inline constexpr auto kDonPachiProject=L"https://github.com/CapsLock4284/DonPachi-Recomp";
inline constexpr auto kDonPachiProjectName=L"DonPachi Recomp";
inline constexpr auto kDonPachiRelease=L"https://github.com/CapsLock4284/DonPachi-Recomp/releases/latest";
inline constexpr Game kDonPachi{
  .id="donpachi", .name="DonPachi", .backend="arcade", .data_dir="game-data/donpachi",
  .executable="external/DonPachi-Recomp/donpachi.exe", .integrated=false, .coming_soon=false,
  .files={}, .runtime_files={}, .community_supported=false, .name_japanese="首領蜂"
};

// Built-in runtimes and their data manifests are certified separately.
inline std::span<const Game> LauncherCatalogue() {
  static const auto games=[] {
    const auto native=Catalogue();
    std::vector<Game> all(native.begin(),native.end());
    all.push_back(kDonPachi);
    return all;
  }();
  return games;
}

inline const Game& FindLauncherGame(std::string_view id) {
  return id==kDonPachi.id?kDonPachi:FindGame(id);
}

inline Inspection InspectDonPachi(const fs::path& root,const fs::path& data) {
  Inspection result;
  result.game=kDonPachi.id;
  result.data_dir=Resolve(root,data);
  result.data_state=fs::is_regular_file(DataChild(result.data_dir,"donpachi.zip"))
      ?DataState::Unverified:DataState::Missing;
  if(!fs::is_regular_file(DataChild(root,kDonPachi.executable)))result.state="donpachi_program_missing";
  else if(result.data_state==DataState::Missing)result.state="donpachi_rom_missing";
  else result.state="needs_verification";
  return result;
}

inline LaunchSpec PrepareDonPachi(const fs::path& root,const Ini& ini,
                                  std::string_view profile,const fs::path& data_override={}) {
  const auto data=data_override.empty()?ini.DataDirectory(kDonPachi):data_override;
  const auto inspection=InspectDonPachi(root,data);
  if(inspection.state=="donpachi_program_missing")
    throw std::runtime_error("Install DonPachi Recomp in external/DonPachi-Recomp.");
  if(inspection.state=="donpachi_rom_missing")
    throw std::runtime_error("Place the US DonPachi ROM set, donpachi.zip, in "+Utf8(inspection.data_dir)+".");

  LaunchSpec launch;
  launch.game=kDonPachi.id;
  launch.paths=Paths(root,kDonPachi.id,inspection.data_dir,profile);
  ValidateOutputs(launch.paths);
  fs::create_directories(launch.paths.profile);
  launch.executable=DataChild(root,kDonPachi.executable);
  launch.configuration=DataChild(launch.paths.profile,"donpachi.ini");
  launch.arguments={L"--usa",L"--rom",DataChild(launch.paths.data,"donpachi.zip").native(),
                    L"--settings",launch.configuration.native(),
                    L"--eeprom",DataChild(launch.paths.profile,"donpachi.eeprom").native()};
  return launch;
}
inline void SetDonPachiFullscreen(const LaunchSpec& launch,bool fullscreen) {
  // DonPachi Recomp reads its display mode from the profile's settings file.
  const auto& settings=launch.configuration;
  std::optional<std::string> before;
  if(fs::exists(settings))before=Ini::ReadText(settings);
  const std::array edits{IniEdit{"window","fullscreen",fullscreen?"true":"false"}};
  UpdateIniFile(settings,before?std::optional<std::string_view>(*before):std::nullopt,edits);
}
}
