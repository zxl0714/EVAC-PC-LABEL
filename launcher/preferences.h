#pragma once
#include "common/catalogue.h"
#include "common/config_update.h"
#include "common/settings.h"
#include <stdexcept>

namespace cave::launcher {
inline std::size_t InitialSelection(const Ini& ini,std::string_view explicit_choice={}) {
  const auto stored=ini.Get("collection","selected_game");
  const auto choice=explicit_choice.empty()?(stored?std::string_view(stored->text):std::string_view{}):explicit_choice;
  const auto games=Catalogue();
  for(std::size_t index=0;index<games.size();++index)if(games[index].id==choice)return index;
  if(!explicit_choice.empty())throw std::runtime_error("Unknown selected game");
  for(std::size_t index=0;index<games.size();++index)if(!games[index].coming_soon && games[index].integrated)return index;
  return 0;
}
inline void SaveSelection(const fs::path& root,std::size_t selected) {
  if(selected>=Catalogue().size())throw std::runtime_error("Invalid selected game index");
  // Read the latest disk text: closing must not overwrite an editor's changes
  // or implicitly save a dirty nickname/fullscreen field from the UI form.
  const auto before=Ini::ReadText(EnsureConfig(root));
  const std::array edits{IniEdit{"collection","selected_game",std::string(Catalogue()[selected].id)}};
  UpdateConfig(root,before,edits);
}
}
