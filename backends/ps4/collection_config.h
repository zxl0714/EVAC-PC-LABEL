#pragma once
#include "common/settings.h"
#include "input/keyboard_config.h"
#include "community/protocol.h"
#include "native_video.h"

namespace cave::ps4 {
struct CollectionConfiguration {
    std::string game{"ketsui"};
    bool community_supported{};
    bool garegga_english_translation{true};
    Settings shared;
    kt::input::KeyboardBindings keyboard{};
    kt::input::KeyboardBindings keyboard_p2{};
    kt::community::Config community;
    NativeDisplaySettings display;
};
CollectionConfiguration ResolveCollectionConfiguration(const Ini& ini,std::string_view game="ketsui");
CollectionConfiguration LoadCollectionConfiguration(const fs::path& path,std::string_view game="ketsui");
void ApplyCollectionConfiguration(const CollectionConfiguration& config);
void ReportCollectionConfiguration(const CollectionConfiguration& config);
bool CollectionConfigurationSelfTest();
}
