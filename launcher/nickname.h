#pragma once
#include "common/catalogue.h"
#include "common/ini.h"
#include "common/nickname.h"
#include "common/paths.h"
#include "common/settings.h"
#include "launcher/nickname_fonts.h"
#include <vector>

namespace cave::launcher {
struct NicknameIssue {
    const Game* game{};
    std::string native_name;
    std::u32string missing;
    std::string version;
    bool alias{},shortened{};
};
inline std::u32string NicknameCodepoints(std::string_view utf8) {
    // Callers only pass validated UTF-8 / normalized UTF-8.
    std::u32string output;
    for(size_t i=0;i<utf8.size();) {
        const auto lead=static_cast<unsigned char>(utf8[i++]);
        unsigned following=lead<0x80?0:lead<0xe0?1:lead<0xf0?2:3;
        char32_t cp=lead&(following?((1u<<(6-following))-1):0x7f);
        while(following--)cp=(cp<<6)|(static_cast<unsigned char>(utf8[i++])&0x3f);
        output.push_back(cp);
    }
    return output;
}
inline std::vector<NicknameIssue> NicknameIssues(const Ini& ini,std::string_view shared_name,const fs::path& root={}) {
    const auto canonical=NormalizeDisplayNickname(shared_name);
    if(canonical.empty())return {};
    std::vector<NicknameIssue> issues;
    for(const auto& game:Catalogue()) {
        if(game.backend!="xbox360" && game.backend!="ps4")continue;
        // A per-game name is edited separately, not overwritten by this form.
        if(ini.Get("game."+std::string(game.id)+".community","nickname"))continue;
        std::vector<std::string> versions{""};
        if(game.id=="ketsui") {
            versions={"01.00","01.02"};
            if(!root.empty())try {
                if(const auto variant=IdentifyDataVariant(game,Resolve(root,ini.DataDirectory(game))))
                    versions={std::string(variant->version)};
            }catch(const std::exception&) {/* Uninstalled/invalid data: show the applicable version explicitly. */}
        }
        for(const auto& version:versions) {
        NicknameIssue item{};item.game=&game;item.version=version;
        const bool legacy=game.backend=="xbox360";
        item.native_name=legacy?LegacyDisplayNickname(canonical):Ps4DisplayNickname(canonical);
        const auto characters=NicknameCodepoints(legacy?canonical:item.native_name);
        for(const auto cp:characters) {
            const bool supported=legacy?(cp>=32 && cp<=126):HasNicknameGlyph(game.id,cp,version);
            if(!supported && item.missing.find(cp)==item.missing.npos)item.missing.push_back(cp);
        }
        item.alias=legacy && !item.missing.empty();
        item.shortened=!item.alias && item.native_name!=canonical;
        if(!item.missing.empty() || item.shortened)issues.push_back(std::move(item));
        }
    }
    return issues;
}
}
