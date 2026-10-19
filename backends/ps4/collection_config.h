#pragma once
#include "common/settings.h"
#include "input/keyboard_config.h"
#include "community/protocol.h"
#include "native_video.h"

namespace cave::ps4 {
struct CollectionConfiguration {
    std::string game{"ketsui"};
    bool community_supported{};
    Settings shared;
    kt::input::KeyboardBindings keyboard{};
    kt::community::Config community;
    NativeDisplaySettings display;
};
CollectionConfiguration ResolveCollectionConfiguration(const Ini& ini,std::string_view game="ketsui");
CollectionConfiguration LoadCollectionConfiguration(const fs::path& path,std::string_view game="ketsui");
void ApplyCollectionConfiguration(const CollectionConfiguration& config);
void ReportCollectionConfiguration(const CollectionConfiguration& config);
bool CollectionConfigurationSelfTest();
}
