#pragma once
#include "launcher/donpachi_recomp.h"
#include "common/ini.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace cave::launcher {
struct GameLibrary {
  std::vector<std::size_t> order;
  std::vector<std::size_t> hidden;
  std::vector<std::size_t> visible;
  bool show_hidden{};

  GameLibrary()=default;
  explicit GameLibrary(const Ini& ini)
      : order(Read(ini,"game_order")),hidden(Read(ini,"hidden_games")) {
    const auto games=LauncherCatalogue();
    const auto saved_count=order.size();
    for(std::size_t index=0;index<games.size();++index)
      if(std::ranges::find(order,index)==order.end())order.push_back(index);
    constexpr std::array<std::string_view,4> platforms{"xbox360","ps4","ps2","arcade"};
    const auto rank=[&](std::size_t index) {
      if(games[index].id=="futari")return std::size_t{0};
      return 1+std::size_t(std::ranges::find(platforms,games[index].backend)-platforms.begin());
    };
    std::stable_sort(order.begin()+saved_count,order.end(),
                     [&](std::size_t a,std::size_t b){return rank(a)<rank(b);});
    Rebuild();
  }

  bool Hidden(std::size_t index) const {
    return std::ranges::find(hidden,index)!=hidden.end();
  }

  void SetHidden(std::size_t index,bool value) {
    if(value && !Hidden(index))hidden.push_back(index);
    if(!value)std::erase(hidden,index);
    Rebuild();
  }

  void Move(std::size_t from,std::size_t to) {
    const auto source=std::ranges::find(order,from),target=std::ranges::find(order,to);
    if(source==order.end() || target==order.end() || source==target)return;
    const auto position=target-order.begin();
    order.erase(source);
    order.insert(order.begin()+position,from);
    Rebuild();
  }

  void Rebuild() {
    visible.clear();
    for(const auto index:order)
      if(show_hidden || !Hidden(index))visible.push_back(index);
  }

  static std::string Encode(const std::vector<std::size_t>& indexes) {
    std::string text;
    for(const auto index:indexes) {
      if(!text.empty())text+=',';
      text+=LauncherCatalogue()[index].id;
    }
    return text;
  }

 private:
  static std::vector<std::size_t> Read(const Ini& ini,std::string_view key) {
    const auto value=ini.Get("collection",key);
    if(!value)return {};
    std::string_view list=value->text;
    std::vector<std::size_t> indexes;
    const auto games=LauncherCatalogue();
    while(!list.empty()) {
      const auto comma=list.find(',');
      auto id=list.substr(0,comma);
      const auto first=id.find_first_not_of(" \t");
      if(first!=id.npos) {
        id=id.substr(first,id.find_last_not_of(" \t")-first+1);
        const auto game=std::ranges::find(games,id,&Game::id);
        if(game!=games.end()) {
          const auto index=std::size_t(game-games.begin());
          if(std::ranges::find(indexes,index)==indexes.end())indexes.push_back(index);
        }
      }
      if(comma==list.npos)break;
      list.remove_prefix(comma+1);
    }
    return indexes;
  }
};
}
