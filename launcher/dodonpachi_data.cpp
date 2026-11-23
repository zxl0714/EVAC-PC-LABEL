#include "dodonpachi_data.h"
#include "common/config_update.h"

namespace cave::launcher {
fs::path FindDoDonPachiZip(const fs::path& data) {
    if (!fs::is_directory(data)) {
        if (_wcsicmp(data.extension().c_str(), L".zip") == 0 && fs::is_regular_file(data))
            return data;
        return {};
    }
    for (const auto name : {"ddonpach.zip", "ddonpachj.zip"}) {
        const auto zip = DataChild(data, name);
        if (fs::is_regular_file(zip)) return zip;
    }
    return {};
}

void ConfigureDoDonPachi(LaunchSpec& launch) {
    const auto zip = FindDoDonPachiZip(launch.paths.data);
    const auto program = launch.executable.parent_path();
    std::optional<std::string> before;
    if (fs::exists(launch.configuration)) before = Ini::ReadText(launch.configuration);
    const auto ini = Ini::Parse(before.value_or(""));
    std::vector<IniEdit> edits;
    if (!zip.empty()) {
        const auto path = Utf8(zip);
        const auto* previous = ini.Get("files", "rom");
        if (previous && previous->text == path) return;
        edits.push_back({"files", "rom", path});
        // Upstream's --rom imports the selected ZIP even when a DAT already exists.
        // Later launches use the saved path and the game's normal DAT cache.
        launch.arguments = {L"--rom", zip.native()};
        const bool japanese = _wcsicmp(zip.filename().c_str(), L"ddonpachj.zip") == 0;
        edits.push_back({"game", "version", japanese ? "japan" : "world"});
    } else {
        const bool world = fs::is_regular_file(program / "ddonpach.dat");
        const bool japan = fs::is_regular_file(program / "ddonpach_jpn.dat");
        if (world == japan) return;
        const auto* version = ini.Get("game", "version");
        const bool selected_japan = version && _stricmp(version->text.c_str(), "japan") == 0;
        if (selected_japan == japan) return;
        edits.push_back({"game", "version", japan ? "japan" : "world"});
    }
    UpdateIniFile(launch.configuration, before, edits);
}
}
