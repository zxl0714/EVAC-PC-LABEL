#include "daioujou_arrange.h"
#include "common/daioujou_music.h"
#include "backends/ps4/content/resource_prepare.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace cave::ps4 {
namespace {
using Json = nlohmann::json;
constexpr std::string_view kOutput = "arrange/";
constexpr std::string_view kTitlePcm = "1100/config/title_pcm.psb.m";
constexpr std::string_view kCommonUi = "1100/motion/title_ui_common.psb.m";
constexpr std::uint64_t kIndexLimit = 4 * 1024 * 1024;

struct ArchiveNames {
    const char* index;
    const char* body;
};
constexpr ArchiveNames kBase{"ps4data_info.psb.m", "ps4data_body.bin"};
constexpr ArchiveNames kUpdate{"ps4data_patch_info.psb.m", "ps4data_patch_body.bin"};

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::string_view Basename(std::string_view path) {
    return path.substr(path.rfind('/') + 1);
}
std::uint64_t Unsigned(const Json& value) {
    Require(value.is_number_unsigned() ||
        (value.is_number_integer() && value.get<std::int64_t>() >= 0),
        "Invalid DaiOuJou archive extent");
    return value.get<std::uint64_t>();
}

class ArchiveLayer {
public:
    ArchiveLayer(const fs::path& data, ArchiveNames names, std::span<const std::uint8_t> key)
        : names_(names), key_(key), body_(DataChild(data, names.body)),
          body_size_(fs::file_size(body_)), index_(ParseResourceIndex(UnwrapResource(
              ReadPreparationInput(DataChild(data, names.index), kIndexLimit), key, names.index))) {
        Require(index_.at("id") == "archive" && index_.at("file_info").is_object(),
            "Invalid DaiOuJou archive index");
    }

    ResourceBytes Read(std::string_view name, std::uint64_t packed_limit = kIndexLimit) const {
        const auto& entry = index_.at("file_info").at(std::string(name));
        Require(entry.is_array() && entry.size() == 2, "Invalid DaiOuJou archive entry");
        const auto offset = Unsigned(entry[0]);
        const auto length = Unsigned(entry[1]);
        Require(offset <= body_size_ && length > 0 && length <= body_size_ - offset &&
            length <= packed_limit, "DaiOuJou resource lies outside the archive");
        ResourceBytes packed(static_cast<std::size_t>(length));
        std::ifstream input(body_, std::ios::binary);
        input.seekg(static_cast<std::streamoff>(offset));
        Require(bool(input.read(reinterpret_cast<char*>(packed.data()), static_cast<std::streamsize>(length))),
            "Cannot read DaiOuJou archive metadata");
        return UnwrapResource(packed, key_, Basename(name));
    }

    void Add(std::string_view name, std::span<const std::uint8_t> bytes, bool metadata) {
        auto packed = PackArchiveResource(bytes, key_, Basename(name), metadata);
        auto& files = index_.at("file_info");
        const std::string path(name);
        Require(metadata ? files.contains(path) : !files.contains(path), "Unexpected DaiOuJou resource name");
        files[path] = Json::array({body_size_ + tail_.size(), packed.size()});
        tail_.insert(tail_.end(), packed.begin(), packed.end());
    }

    void Reserve(std::size_t bytes) { tail_.reserve(bytes); }

    void Write(const fs::path& stage) const {
        const auto index = PackArchiveResource(WritePlainPsb(index_, 40), key_, names_.index, true);
        WritePreparedRelative(stage, std::string(kOutput) + names_.index, index);
        WritePreparedRelative(stage, std::string(kOutput) + names_.body, tail_);
    }

private:
    ArchiveNames names_;
    std::span<const std::uint8_t> key_;
    fs::path body_;
    std::uint64_t body_size_;
    Json index_;
    ResourceBytes tail_;
};

void ReplaceExpected(std::string& script, std::string_view before, std::string_view after, unsigned count) {
    std::size_t start{};
    for (unsigned i = 0; i < count; ++i) {
        const auto at = script.find(before, start);
        Require(at != script.npos, "DaiOuJou BGM selection script differs from the verified layout");
        script.replace(at, before.size(), after);
        start = at + after.size();
    }
    Require(script.find(before, start) == script.npos, "Unexpected duplicate DaiOuJou BGM selection");
}

void AddArrangeMenu(ArchiveLayer& layer, bool update) {
    constexpr std::string_view profile_path = "1100/config/title_prof.psb.m";
    auto profile = ParseResourceIndex(layer.Read(profile_path));
    auto& settings = profile.at("root");
    auto& choices = settings.at("bgmmode_num");
    auto& pause_icons = settings.at("pause_menu_bgm_set_variable_list_bgmset");
    Require(choices == 3 && pause_icons == Json::array({0, 1, 3}), "Unexpected DaiOuJou BGM menu profile");
    choices = 4;
    pause_icons.push_back(2);
    layer.Add(profile_path, WritePlainPsb(profile, 0), true);

    constexpr std::string_view direct_path = "1100/script/title_mode_direct_menu.nut.m";
    const auto original = layer.Read(direct_path);
    std::string direct(original.begin(), original.end());
    ReplaceExpected(direct, "[0,1,3,3]", "[0,1,3,2]", 2);
    constexpr std::string_view selected = "m_bgm_mode[_gamemode] = _bgmMode;";
    constexpr std::string_view save =
        "m_bgm_mode[_gamemode] = _bgmMode;\n"
        "        ::g_systemdata.get_value(SystemDataValueIndex.SETTING_SOUND).set_bgm_set_mode(_gamemode, _bgmMode);\n"
        "        ::g_systemdata.CheckValueChangeAutosave(SystemDataValueIndex.SETTING_SOUND);\n"
        "        this._check_autosave();";
    ReplaceExpected(direct, selected, save, 2);
    layer.Add(direct_path, {reinterpret_cast<const std::uint8_t*>(direct.data()), direct.size()}, true);

    constexpr std::string_view gadget_path = "1100/script/title_gadget_score.nut.m";
    const auto gadget_source = layer.Read(gadget_path);
    std::string gadget(gadget_source.begin(), gadget_source.end());
    ReplaceExpected(gadget, "c_bgm_num = 3;", "c_bgm_num = 4;", 1);
    ReplaceExpected(gadget, "local _table = [0,1,3];", "local _table = [0,1,3,2];", 1);
    layer.Add(gadget_path, {reinterpret_cast<const std::uint8_t*>(gadget.data()), gadget.size()}, true);

    constexpr std::string_view gadget_motion_path = "1100/motion/title_gadget.psb.m";
    auto gadget_motion = layer.Read(gadget_motion_path, 16 * 1024 * 1024);
    ApplyDaioujouArrangeGadgetArtwork(gadget_motion, update);
    layer.Add(gadget_motion_path, gadget_motion, true);

    const auto add_motion = [&](std::string_view path, DaioujouArrangeMotion kind) {
        auto motion = layer.Read(path);
        ApplyDaioujouArrangeMotion(motion, kind, update);
        layer.Add(path, motion, true);
    };
    if (!update) add_motion("1100/motion/title_ui_direct_ac.psb.m", DaioujouArrangeMotion::Direct);
    add_motion("1100/motion/title_pause_menu.psb.m", DaioujouArrangeMotion::Pause);
    add_motion("1100/motion/title_ui_option.psb.m", DaioujouArrangeMotion::Options);

    auto common = layer.Read(kCommonUi, 8 * 1024 * 1024);
    ApplyDaioujouArrangeArtwork(common, update);
    layer.Add(kCommonUi, common, true);
}

void AddPcmDefinitions(ArchiveLayer& layer, const Json& definitions) {
    auto pcm = ParseResourceIndex(layer.Read(kTitlePcm));
    auto& waves = pcm.at("pcm_wav_definition");
    Require(waves.is_array() && waves.size() == 30, "Unexpected DaiOuJou PCM definitions");
    for (const auto& definition : definitions) {
        const auto collision = std::ranges::any_of(waves, [&](const Json& wave) {
            return wave.at("no") == definition.at("no") || wave.at("filename") == definition.at("filename");
        });
        Require(!collision, "X Arrange PCM definition collides with an original track");
        waves.push_back(definition);
    }
    layer.Add(kTitlePcm, WritePlainPsb(pcm, 0), true);
}
}

void AddDaioujouArrangeFiles(std::vector<std::string>& files, bool merged) {
    files.emplace_back(std::string(kOutput) + kBase.index);
    files.emplace_back(std::string(kOutput) + kBase.body);
    if (merged) {
        files.emplace_back(std::string(kOutput) + kUpdate.index);
        files.emplace_back(std::string(kOutput) + kUpdate.body);
    }
}

void PrepareDaioujouArrangeArchive(const fs::path& data, const fs::path& stage,
    std::span<const std::uint8_t> executable, bool merged,VerificationCache* verification) {
    const auto key = ReadArchiveSeed(executable, merged ? 0xdd : 0xde, true);
    auto definitions = Json::array();
    ArchiveLayer base(data, kBase, key);
    std::size_t reserve = 28 * 1024 * 1024;
    for (const auto& cue : daioujou::ArrangeCues()) reserve += 4ull*cue.samples + 16 * 1024;
    base.Reserve(reserve);
    daioujou::ReadArrangeWaves(data,[&](std::string_view name,std::span<const std::uint8_t> wave) {
        const auto track=std::ranges::find(kDaioujouArrangeTrackNames,name);
        Require(track!=kDaioujouArrangeTrackNames.end(),"Unknown X Arrange PCM track");
        const auto i=static_cast<unsigned>(track-kDaioujouArrangeTrackNames.begin());
        const auto filename = std::string(name) + ".wav";
        const auto resource = "1100/wav/evac_" + filename;
        base.Add(resource + ".m", wave, false);
        definitions.push_back({{"delay", 0}, {"filename", resource},
            {"no", kDaioujouArrangeFirstPcm + i}, {"stream", false}});
    },verification);
    std::sort(definitions.begin(),definitions.end(),[](const Json& a,const Json& b){return a.at("no")<b.at("no");});
    AddPcmDefinitions(base, definitions);
    AddArrangeMenu(base, false);
    base.Write(stage);
    if (merged) {
        ArchiveLayer update(data, kUpdate, key);
        AddPcmDefinitions(update, definitions);
        AddArrangeMenu(update, true);
        update.Write(stage);
    }
}
}
