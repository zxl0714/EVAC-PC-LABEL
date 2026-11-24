#pragma once
#include "launcher/arcade_recomp.h"
#include "common/config_update.h"
#include "common/settings.h"
#include "launcher/window_size.h"
#include <charconv>
#include <vector>
#include <stdexcept>

namespace cave::launcher {
inline bool ReadBigPicture(const Ini& ini) {
  const auto value=ini.Get("collection","launcher_big_picture");
  return value && value->text=="true";
}
inline std::size_t InitialSelection(const Ini& ini,std::string_view explicit_choice={}) {
  const auto stored=ini.Get("collection","selected_game");
  const auto choice=explicit_choice.empty()?(stored?std::string_view(stored->text):std::string_view{}):explicit_choice;
  const auto games=LauncherCatalogue();
  for(std::size_t index=0;index<games.size();++index)if(games[index].id==choice)return index;
  if(!explicit_choice.empty())throw std::runtime_error("Unknown selected game");
  for(std::size_t index=0;index<games.size();++index)if(!games[index].coming_soon && games[index].integrated)return index;
  return 0;
}
inline std::optional<WindowSize> ReadWindowSize(const Ini& ini) {
  const auto read=[&](std::string_view key) {
    const auto value=ini.Get("collection",key);
    if(!value)return 0;
    int number{};const auto& text=value->text;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);
    return parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size() && number>=100 && number<=16384?number:0;
  };
  const WindowSize size{read("launcher_covers_width"),read("launcher_covers_height")};
  if(!size.width || !size.height)return std::nullopt;
  return size;
}
inline void SaveSelection(const fs::path& root,std::size_t selected,
                          std::optional<WindowSize> size={},bool big_picture=false) {
  if(selected>=LauncherCatalogue().size())throw std::runtime_error("Invalid selected game index");
  // Read the latest disk text: closing must not overwrite an editor's changes
  // or implicitly save a dirty nickname/fullscreen field from the UI form.
  const auto before=Ini::ReadText(EnsureConfig(root));
  std::vector<IniEdit> edits{{"collection","selected_game",std::string(LauncherCatalogue()[selected].id)},
                            {"collection","launcher_big_picture",big_picture?"true":"false"}};
  if(size) {
    edits.push_back({"collection","launcher_covers_width",std::to_string(size->width)});
    edits.push_back({"collection","launcher_covers_height",std::to_string(size->height)});
  }
  UpdateConfig(root,before,edits);
}
}
