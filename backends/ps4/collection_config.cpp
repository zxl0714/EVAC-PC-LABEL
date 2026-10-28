#include "collection_config.h"
#include "native_audio.h"
#include "native_pad.h"
#include "audio/wasapi_engine.h"
#include "graphics/present_mode.h"
#include <array>
#include <cstdio>
#include <stdexcept>

namespace cave::ps4 {
CollectionConfiguration ResolveCollectionConfiguration(const Ini& ini,std::string_view game) {
    CollectionConfiguration result;
    const auto& title=FindGame(game);
    if (title.backend!="ps4") throw std::runtime_error("Collection game does not use the PS4 backend");
    result.game=game;
    result.community_supported=title.community_supported;
    result.shared = ResolveSettings(ini, game);
    // Same positions as cave::kButtons. Select is PS4 Touchpad, also used by
    // SDL Back/Touchpad in this title; Options is the Start position.
    constexpr std::array<std::uint32_t,16> masks{
        0x10,0x40,0x80,0x20,0x4000,0x2000,0x8000,0x1000,8,0x100000,0x400,0x800,0x100,0x200,2,4};
    static_assert(masks.size() == kButtons.size());
    for (std::size_t button=0; button<masks.size(); ++button) {
        for (auto key : result.shared.keyboard[button].keys) {
            if (key >= result.keyboard.size() || result.keyboard[key]) throw std::runtime_error("Invalid PS4 keyboard mapping");
            result.keyboard[key] = masks[button];
        }
    }
    auto endpoint = result.shared.endpoint;
    while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();
    if (endpoint.ends_with("/cave/api/v1")) endpoint += "/games/"+std::string(game);
    (void)kt::community::ParseEndpoint(endpoint,game);
    result.community = {result.shared.community_enabled && result.community_supported, result.shared.nickname, std::move(endpoint),std::string(game)};
    if (!kt::community::ValidNicknameBytes(result.community.nickname)) throw std::runtime_error("Invalid community nickname");
    result.display = {FromUtf8("EVAC PC LABEL — " + std::string(title.DisplayName())).wstring(),
        result.shared.fullscreen, result.shared.vsync, result.shared.window_width, result.shared.window_height};
    return result;
}
CollectionConfiguration LoadCollectionConfiguration(const fs::path& path,std::string_view game) {
    if (!fs::is_regular_file(path)) throw std::runtime_error("Collection config.ini is missing");
    return ResolveCollectionConfiguration(Ini::Load(path),game);
}
void ApplyCollectionConfiguration(const CollectionConfiguration& config) {
    ConfigureNativePadSettings(config.keyboard, config.shared.gamepad_enabled, config.shared.deadzone);
    ConfigureNativeAudioVolume(config.shared.volume);
    ConfigureNativeDisplay(config.display);
    ReportCollectionConfiguration(config);
}
void ReportCollectionConfiguration(const CollectionConfiguration& config) {
    using kt::community::Json;
    Json keyboard=Json::object();
    for (std::size_t n=0; n<kButtons.size(); ++n) keyboard[std::string(kButtons[n])] = config.shared.keyboard[n].keys;
    const auto title=fs::path(config.display.title).u8string();
    const auto report=Json{{"event","collection_configuration"},{"game",config.game},
        {"fullscreen",config.display.fullscreen},{"window_width",config.display.window_width},
        {"window_height",config.display.window_height},{"vsync",config.display.vsync},{"volume",config.shared.volume},
        {"gamepad",{{"enabled",config.shared.gamepad_enabled},{"deadzone",config.shared.deadzone}}},
        {"community",{{"requested",config.shared.community_enabled},{"supported",config.community_supported},
            {"enabled",config.community.enabled},{"endpoint",config.community.endpoint},{"nickname",config.community.nickname}}},
        {"title",std::string(title.begin(),title.end())},{"keyboard",std::move(keyboard)}};
    std::puts(report.dump().c_str());
}
bool CollectionConfigurationSelfTest() {
    unsigned passed{}; bool okay=true;
    const auto check=[&](bool result,const char* name) {
        std::printf("{\"event\":\"collection_config_case\",\"name\":\"%s\",\"passed\":%s}\n",name,result?"true":"false");
        okay &= result; passed += result;
    };
    const auto defaults=ResolveCollectionConfiguration(Ini::Parse(DefaultConfig()));
    const auto daioujou=ResolveCollectionConfiguration(Ini::Parse(
        "[display]\nfullscreen=true\n[game.daioujou.display]\nfullscreen=false\n"
        "[game.ketsui.keyboard]\nunknown=invalid\n"),"daioujou");
    check(daioujou.game=="daioujou" && !daioujou.shared.fullscreen &&
        daioujou.display.title==L"EVAC PC LABEL — 怒首領蜂大往生 臨廻転生", "ps4_title_configuration_isolated");
    check(daioujou.shared.community_enabled && daioujou.community_supported && daioujou.community.enabled &&
        daioujou.community.endpoint=="https://xiaolu.life/cave/api/v1/games/daioujou","daioujou_uses_own_configured_community_service");
    const auto daioujou_offline=ResolveCollectionConfiguration(Ini::Parse("[community]\nenabled=false\n"),"daioujou");
    check(daioujou_offline.community_supported && !daioujou_offline.community.enabled,
        "daioujou_offline_preference_does_not_create_provider");
    const auto feveron=ResolveCollectionConfiguration(Ini::Parse(
        "[display]\nfullscreen=true\n[game.feveron.display]\nfullscreen=false\n"
        "[game.feveron.community]\nnickname=Feveron Player\n"
        "[game.esprade.keyboard]\nunknown=invalid\n"),"feveron");
    check(feveron.game=="feveron" && !feveron.shared.fullscreen &&
        feveron.display.title==L"EVAC PC LABEL — 弾銃フィーバロン" &&
        feveron.community.nickname=="Feveron Player" &&
        feveron.community.endpoint=="https://xiaolu.life/cave/api/v1/games/feveron",
        "feveron_title_settings_and_service_identity_isolated");
    check(feveron.shared.community_enabled && feveron.community_supported && feveron.community.enabled,
        "feveron_accepted_service_uses_shared_online_preference");
    check(defaults.keyboard['Z']==0x4000 && defaults.keyboard['X']==0x2000 && defaults.keyboard['C']==0x8000 &&
        defaults.keyboard['V']==0x1000 && defaults.keyboard[13]==8 && defaults.keyboard[8]==0x100000,
        "common_positions_map_to_ps4_face_options_touchpad");
    check(defaults.community.enabled && defaults.community.nickname=="Player" &&
        defaults.community.endpoint=="https://xiaolu.life/cave/api/v1/games/ketsui", "collection_community_defaults");
    check(defaults.keyboard[38]==0x10 && defaults.keyboard[40]==0x40 && defaults.keyboard[37]==0x80 &&
        defaults.keyboard[39]==0x20 && defaults.keyboard['A']==0x400 && defaults.keyboard['S']==0x800 &&
        defaults.keyboard['Q']==0x100 && defaults.keyboard['W']==0x200,
        "directions_shoulders_and_triggers_preserved");
    const auto custom=ResolveCollectionConfiguration(Ini::Parse(
        // Per-game preferences replace shared values without reading peers.
        "[display]\nfullscreen=true\n[audio]\nvolume=0.8\n"
        "[game.ketsui.display]\nfullscreen=false\nvsync=false\n[game.ketsui.audio]\nvolume=0.25\n"
        "[game.ketsui.keyboard]\nsouth=Space,B\nselect=Tab\nl3=F1\nr3=F2\n"
        "[game.ketsui.gamepad]\nenabled=false\ndeadzone=0.35\n"
        "[game.ketsui.community]\nenabled=false\nnickname=Test Player\n"
        "[game.espgaluda2.keyboard]\nunknown=invalid\n"));
    check(!custom.shared.fullscreen && !custom.shared.vsync && custom.shared.volume==0.25 &&
        !custom.shared.gamepad_enabled && custom.shared.deadzone==0.35 && !custom.community.enabled,
        "game_overrides_and_other_game_isolation");
    check(!custom.keyboard['Z'] && custom.keyboard[32]==0x4000 && custom.keyboard['B']==0x4000 &&
        custom.keyboard[9]==0x100000 && custom.keyboard[112]==2 && custom.keyboard[113]==4,
        "aliases_replace_old_mapping_and_preserve_stick_clicks");
    for (const auto text : {"[keyboard]\nsouth=F11\n", "[keyboard]\nsouth=X\n",
                           "[game.ketsui.gamepad]\ndeadzone=1\n", "[game.ketsui.keyboard]\ncross=Z\n",
                           "[community]\nendpoint=https://xiaolu.life/cave/api/v1/games/futari\n"}) {
        bool rejected{}; try { (void)ResolveCollectionConfiguration(Ini::Parse(text)); }
        catch (const std::exception&) { rejected=true; }
        check(rejected,"invalid_configuration_rejected_without_applying");
    }
    constexpr std::array<int,3> all{0,1,2};
    constexpr std::array<int,2> mailbox{1,2};
    constexpr std::array<int,1> fifo{2};
    check(SelectPresentMode(true,std::span<const int>(all),2,0,1)==2 &&
        SelectPresentMode(false,std::span<const int>(all),2,0,1)==0 &&
        SelectPresentMode(false,std::span<const int>(mailbox),2,0,1)==1 &&
        SelectPresentMode(false,std::span<const int>(fifo),2,0,1)==2, "vsync_mode_selection_and_fifo_fallback");
    check(kt::audio::AudioConversionSelfTest(),"master_volume_multiplies_guest_gain_and_mutes_without_touching_input");
    ApplyCollectionConfiguration(custom);
    const auto applied=GetNativeDisplaySettings();
    check(applied.title==defaults.display.title && !applied.fullscreen && !applied.vsync &&
        applied.title==L"EVAC PC LABEL — ケツイ Deathtiny ～絆地獄たち～", "native_display_uses_catalogue_title_and_preferences");
    std::printf("{\"event\":\"collection_config_self_test\",\"passed\":%u,\"success\":%s}\n",passed,okay?"true":"false");
    return okay;
}
}
