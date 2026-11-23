#pragma once
#include "common/catalogue.h"
#include "common/config_update.h"
#include "common/launch.h"
#include "dodonpachi_data.h"
#include <array>

namespace cave::launcher {
struct ArcadeRecomp {
  Game game;
  std::wstring_view project, project_name;
  std::string_view rom, settings, eeprom;
  const wchar_t* setup_title;
  const wchar_t* rom_hint;
  const wchar_t* edition;
};

inline constexpr std::array kArcadeRecomps{
  ArcadeRecomp{
    {.id="donpachi", .name="DonPachi", .backend="arcade", .data_dir="game-data/donpachi",
     .executable="external/DonPachi-Recomp/donpachi.exe", .integrated=false, .coming_soon=false,
     .files={}, .runtime_files={}, .community_supported=false, .name_japanese="首領蜂"},
    L"https://github.com/CapsLock4284/DonPachi-Recomp", L"DonPachi Recomp",
    "donpachi.zip","donpachi.ini","donpachi.eeprom",
    L"安装首領蜂",L"请放置 donpachi.zip（美版）",L"街机美版 · 1995"},
  ArcadeRecomp{
    {.id="dodonpachi", .name="DoDonPachi", .backend="arcade", .data_dir="game-data/dodonpachi",
     .executable="external/DoDonPachi-Recomp/ddonpach.exe", .integrated=false, .coming_soon=false,
     .files={}, .runtime_files={}, .community_supported=false, .name_japanese="怒首領蜂"},
    L"https://github.com/CapsLock4284/DoDonPachi-Recomp", L"DoDonPachi Recomp",
    "ddonpach.zip","ddonpach.ini","ddonpach.eeprom",
    L"安装怒首領蜂",L"请选择国际版或日版 ROM（ZIP）",L"街机国际版／日版 · 1997"}
};

inline const ArcadeRecomp* FindArcadeRecomp(std::string_view id) {
  for(const auto& arcade:kArcadeRecomps)if(arcade.game.id==id)return &arcade;
  return nullptr;
}

// Built-in runtimes and their data manifests are certified separately.
inline std::span<const Game> LauncherCatalogue() {
  static const auto games=[] {
    const auto native=Catalogue();
    std::vector<Game> all(native.begin(),native.end());
    for(const auto& arcade:kArcadeRecomps)all.push_back(arcade.game);
    return all;
  }();
  return games;
}

inline const Game& FindLauncherGame(std::string_view id) {
  if(const auto* arcade=FindArcadeRecomp(id))return arcade->game;
  return FindGame(id);
}

inline Inspection InspectArcadeRecomp(const ArcadeRecomp& arcade,const fs::path& root,const fs::path& data) {
  Inspection result;
  result.game=arcade.game.id;
  result.data_dir=Resolve(root,data);
  const auto program=DataChild(root,arcade.game.executable);
  bool ready;
  if (arcade.game.id=="dodonpachi") {
    ready=!FindDoDonPachiZip(result.data_dir).empty() ||
          (!fs::is_regular_file(result.data_dir) &&
           (fs::is_regular_file(program.parent_path()/"ddonpach.dat") ||
            fs::is_regular_file(program.parent_path()/"ddonpach_jpn.dat")));
  } else {
    ready=fs::is_regular_file(DataChild(result.data_dir,arcade.rom));
  }
  result.data_state=ready?DataState::Unverified:DataState::Missing;
  if(!fs::is_regular_file(program))result.state="arcade_program_missing";
  else if(result.data_state==DataState::Missing)result.state="arcade_rom_missing";
  else result.state="needs_verification";
  return result;
}

inline LaunchSpec PrepareArcadeRecomp(const ArcadeRecomp& arcade,const fs::path& root,const Ini& ini,
                                      std::string_view profile,const fs::path& data_override={}) {
  const auto data=data_override.empty()?ini.DataDirectory(arcade.game):data_override;
  const auto inspection=InspectArcadeRecomp(arcade,root,data);
  const auto program=DataChild(root,arcade.game.executable);
  if(inspection.state=="arcade_program_missing")
    throw std::runtime_error("Install "+std::string(arcade.game.name)+" Recomp in "+Utf8(program.parent_path())+".");
  if(inspection.state=="arcade_rom_missing")
    throw std::runtime_error("Place "+std::string(arcade.rom)+" in "+Utf8(inspection.data_dir)+".");

  LaunchSpec launch;
  launch.game=arcade.game.id;
  launch.paths=Paths(root,arcade.game.id,inspection.data_dir,profile);
  ValidateOutputs(launch.paths);
  fs::create_directories(launch.paths.profile);
  launch.executable=program;
  launch.configuration=DataChild(launch.paths.profile,arcade.settings);
  if (arcade.game.id=="dodonpachi") {
    ConfigureDoDonPachi(launch);
  } else {
    const auto rom=DataChild(launch.paths.data,arcade.rom);
    launch.arguments={L"--usa",L"--rom",rom.native()};
  }
  launch.arguments.insert(launch.arguments.end(),{L"--settings",launch.configuration.native(),
                           L"--eeprom",DataChild(launch.paths.profile,arcade.eeprom).native()});
  return launch;
}
inline void SetArcadeFullscreen(const LaunchSpec& launch,bool fullscreen) {
  const auto& settings=launch.configuration;
  std::optional<std::string> before;
  if(fs::exists(settings))before=Ini::ReadText(settings);
  const std::array edits{IniEdit{"window","fullscreen",fullscreen?"true":"false"}};
  UpdateIniFile(settings,before?std::optional<std::string_view>(*before):std::nullopt,edits);
}
}
