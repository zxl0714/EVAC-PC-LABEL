#pragma once
#include "common/ini.h"
#include <map>
#include <stdexcept>

namespace cave {
inline bool ControllerGuid(std::string_view guid) {
    return guid.size()==32 && guid.find_first_not_of("0123456789abcdef")==guid.npos;
}

inline std::map<std::string,std::string> ControllerMappings(const Ini& ini,std::string_view game={}) {
    std::map<std::string,std::string> mappings;
    const auto apply=[&](std::string_view section) {
        for(const auto& [address,entry]:ini.Values()) {
            if(address.first!=section)continue;
            if(!ControllerGuid(address.second) || entry.text.size()>2048 || entry.text.empty() ||
               entry.text.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ,.:_+-~")!=entry.text.npos)
                throw std::runtime_error("Invalid controller mapping in config.ini line "+std::to_string(entry.line));
            if(entry.text=="default")mappings.erase(address.second);
            else mappings[address.second]=entry.text;
        }
    };
    apply("controller");
    if(!game.empty())apply("game."+std::string(game)+".controller");
    return mappings;
}

inline std::string ControllerMappingHint(const Ini& ini,std::string_view game) {
    std::string hint;
    for(const auto& [guid,mapping]:ControllerMappings(ini,game)) {
        if(!hint.empty())hint+='\n';
        hint+=guid+",EVAC Controller,"+mapping;
    }
    return hint;
}
}
