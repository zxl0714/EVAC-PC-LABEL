#pragma once
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cave::ps4 {
struct PreparationPolicy {
    std::vector<std::string> programs;
    bool merged_app{}, read_fixes{true}, community_offline{};
    bool replay_peer{}, local_replay{};
};

inline PreparationPolicy PlanPs4Data(std::string_view game, std::string_view version,
                                    std::string_view layout) {
    PreparationPolicy result;
    if (game == "ketsui" && ((version == "01.00" && layout == "base") ||
                             (version == "01.02" && layout == "merged-patch"))) {
        result.programs = {"eboot.bin", "emu_integration_01400.prx", "ketsui_01000.prx"};
        result.merged_app = layout == "merged-patch";
    } else if (game == "daioujou" && version == "01.00" && layout == "base") {
        result.programs = {"eboot.bin", "emu_integration_01700.prx", "daioujou_01000.prx"};
        result.replay_peer = true;
        result.local_replay = true;
    } else if ((game == "daioujou" || game == "esprade") && version == "01.04" && layout == "merged-patch") {
        result.programs = {"eboot.bin", game == "daioujou" ? "emu_integration_01700.prx" : "emu_integration_01500.prx"};
        for (const auto* suffix : {"01000.prx", "01010.prx", "01020.prx", "01030.prx"})
            result.programs.push_back(std::string(game) + '_' + suffix);
        result.merged_app = true;
        result.replay_peer = game == "daioujou";
        result.local_replay = true;
    } else {
        throw std::runtime_error("Unsupported PS4 data preparation version/layout");
    }
    return result;
}

inline void AppendVersionArguments(std::vector<std::wstring>& arguments, bool merged, bool offline) {
    if (merged) arguments.insert(arguments.end(), {L"--merged-app", L"on"});
    if (offline) arguments.insert(arguments.end(), {L"--community-offline", L"on"});
}

inline bool CommunityEnabledForData(bool requested, bool forced_offline) {
    return requested && !forced_offline;
}
}
