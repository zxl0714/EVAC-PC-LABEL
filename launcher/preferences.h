#pragma once
#include "common/catalogue.h"
#include "common/config_update.h"
#include "common/settings.h"
#include "launcher/window_size.h"
#include <charconv>
#include <vector>
#include <stdexcept>

namespace cave::launcher {
inline bool CoverLayout(const Ini& ini) {
  const auto value=ini.Get("collection","launcher_layout");
  if(!value || value->text=="covers")return true;
  if(value->text=="banner")return false;
  throw std::runtime_error("[collection].launcher_layout must be banner or covers");
}
inline std::size_t InitialSelection(const Ini& ini,std::string_view explicit_choice={}) {
  const auto stored=ini.Get("collection","selected_game");
  const auto choice=explicit_choice.empty()?(stored?std::string_view(stored->text):std::string_view{}):explicit_choice;
  const auto games=Catalogue();
  for(std::size_t index=0;index<games.size();++index)if(games[index].id==choice)return index;
  if(!explicit_choice.empty())throw std::runtime_error("Unknown selected game");
  for(std::size_t index=0;index<games.size();++index)if(!games[index].coming_soon && games[index].integrated)return index;
  return 0;
}
inline std::string WindowSizeKey(bool covers,std::string_view dimension) {
  return std::string(covers?"launcher_covers_":"launcher_banner_")+std::string(dimension);
}
inline std::optional<WindowSize> ReadWindowSize(const Ini& ini,bool covers) {
  const auto read=[&](std::string_view dimension) {
    const auto value=ini.Get("collection",WindowSizeKey(covers,dimension));
    if(!value)return 0;
    int number{};const auto& text=value->text;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);
    return parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size() && number>=100 && number<=16384?number:0;
  };
  const WindowSize size{read("width"),read("height")};
  if(!size.width || !size.height)return std::nullopt;
  return size;
}
inline void SaveSelection(const fs::path& root,std::size_t selected,bool covers=false,
                          std::optional<WindowSize> size={}) {
  if(selected>=Catalogue().size())throw std::runtime_error("Invalid selected game index");
  // Read the latest disk text: closing must not overwrite an editor's changes
  // or implicitly save a dirty nickname/fullscreen field from the UI form.
  const auto before=Ini::ReadText(EnsureConfig(root));
  std::vector<IniEdit> edits{{"collection","selected_game",std::string(Catalogue()[selected].id)}};
  if(size) {
    edits.push_back({"collection",WindowSizeKey(covers,"width"),std::to_string(size->width)});
    edits.push_back({"collection",WindowSizeKey(covers,"height"),std::to_string(size->height)});
  }
  UpdateConfig(root,before,edits);
}
}
