#pragma once
#include "common/controller_bindings.h"
#include "common/launch.h"
#include "common/settings.h"
#include "common/verification_cache.h"
#include <Windows.h>
#include <array>
#include <stdexcept>

namespace cave::launcher {
inline constexpr auto kSdojRecompDirectory="external/SDOJ-Recomp";
inline constexpr auto kSdojRecompProject=L"https://github.com/eandis/SDOJ-Recomp";
inline constexpr auto kSdojRecompRelease=L"https://github.com/eandis/SDOJ-Recomp/releases/latest";

inline bool ReadSdojRecomp(const Ini& ini) {
  const auto choice=ini.Get("game.sdoj","eandis");
  return choice && choice->text=="true";
}

inline fs::path SdojRecompExecutable(const fs::path& root) {
  return DataChild(root,std::string(kSdojRecompDirectory)+"/DoDonPachi SaiDaiOuJou.exe");
}

inline bool SupportsSdojCommunity(const fs::path& executable) {
  const auto image=LoadLibraryExW(executable.c_str(),nullptr,
                                  LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
  if(!image)return false;
  const bool supported=FindResourceW(image,L"SDOJ_COMMUNITY_V1",RT_RCDATA)!=nullptr;
  FreeLibrary(image);
  return supported;
}

inline std::string RecompKey(const Binding& binding) {
  if(binding.keys.empty())return {};
  switch(binding.keys.front()) {
    case 0x0D:return "Return";
    case 0x1B:return "Escape";
    case 0xBA:return "Semicolon";
    case 0xBB:return "Plus";
    case 0xDB:return "LBracket";
    case 0xDD:return "RBracket";
    case 0xDE:return "Quote";
    default:return KeyName(binding.keys.front());
  }
}

inline LaunchSpec PrepareSdojRecomp(const fs::path& root,const Ini& ini,const fs::path& data_override,
                                    unsigned display_dpi) {
  const auto& game=FindGame("sdoj");
  LaunchSpec launch;
  launch.game=game.id;
  launch.executable=SdojRecompExecutable(root);
  if(!fs::is_regular_file(launch.executable))throw std::runtime_error("SDOJ-Recomp executable is missing");
  launch.paths=Paths(root,game.id,data_override.empty()?ini.DataDirectory(game):data_override);
  launch.controller_mapping_hint=ControllerMappingHint(ini,game.id);

  const auto settings=ResolveSettings(ini,game.id);
  // The upstream SDL window accepts logical units while EVAC stores pixels.
  const auto dpi=display_dpi?display_dpi:96u;
  const auto logical=[dpi](unsigned pixels) {return (pixels*96u+dpi/2)/dpi;};
  // The Windows release defaults to XInput. SDL also supports HID arcade sticks.
  launch.arguments={L"--input_backend=sdl",
                    settings.fullscreen?L"--fullscreen=true":L"--fullscreen=false",
                    L"--window_width="+std::to_wstring(logical(settings.window_width)),
                    L"--window_height="+std::to_wstring(logical(settings.window_height))};
  constexpr std::array keys{L"up",L"down",L"left",L"right",L"shot",L"bomb",L"rapid_shot",L"extra"};
  for(size_t index=0;index<keys.size();++index) {
    const auto key=RecompKey(settings.keyboard[index]);
    launch.arguments.push_back(std::wstring(L"--keyboard_p1_")+keys[index]+L"="+FromUtf8(key).native());
  }
  if(SupportsSdojCommunity(launch.executable)) {
    launch.arguments.push_back(L"--player_name="+FromUtf8(settings.nickname).native());
    const auto endpoint=settings.community_enabled?settings.endpoint:std::string{};
    launch.arguments.push_back(L"--community_endpoint="+FromUtf8(endpoint).native());
  }

  // Explicitly sharing data is safe for the optical TU that both players support.
  // Otherwise upstream uses its own prepared data and first-run setup, including GoD.
  if(fs::is_regular_file(launch.paths.data/"default.xex")) {
    VerificationCache cache(root);
    const auto data=InspectLaunchData(game,root,launch.paths.data,CheckLevel::Programs,cache);
    if(data.variant==game.title_update->id &&
       (data.data_state==DataState::Verified || data.data_state==DataState::Unverified))
      launch.arguments.push_back(L"--game_data_root="+data.data_dir.native());
  }
  return launch;
}
}  // namespace cave::launcher
