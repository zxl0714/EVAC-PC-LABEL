#include "daioujou_arrange.h"
#include "garegga_english.h"
#include "ketsui_arrange_audio.h"
#include "ketsui_arrange_music.h"
#include "common/daioujou_music.h"
#include "common/death_label_gadgets.h"
#include "backends/ps4/content/resource_prepare.h"
#include <algorithm>
#include <cstdio>
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
        Require(index_.at("id") == "archive" && index_.at("file_info").is_object(), "Invalid PS4 archive index");
    }

    bool Has(std::string_view name) const { return index_.at("file_info").contains(std::string(name)); }

    ResourceBytes Read(std::string_view name, std::uint64_t packed_limit = kIndexLimit) const {
        const auto& entry = index_.at("file_info").at(std::string(name));
        Require(entry.is_array() && entry.size() == 2, "Invalid DaiOuJou archive entry");
        const auto offset = Unsigned(entry[0]);
        const auto length = Unsigned(entry[1]);
        Require(offset <= body_size_ && length > 0 && length <= body_size_ - offset,
            "DaiOuJou resource lies outside the archive");
        Require(length <= packed_limit, "DaiOuJou resource exceeds its size limit");
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

void WriteDeathLabelGadgets(const fs::path& stage,const ArchiveLayer& layer,bool update) {
    ResourceBytes sprites;
    try {
        sprites = ExportDeathLabelGadgetSprites(
            layer.Read("1100/motion/title_gadget.psb.m", kGadgetAtlasResourceLimit), update);
    } catch (const std::exception& error) {
        // A replacement Gadget atlas must not prevent the original game from launching.
        std::fprintf(stderr,"Death Label Gadget artwork unavailable: %s\n",error.what());
    }
    WritePreparedRelative(stage,death_label::GadgetCachePath,sprites);
}

void ReplaceExpected(std::string& script, std::string_view before, std::string_view after, unsigned count) {
    std::size_t start{};
    for (unsigned i = 0; i < count; ++i) {
        const auto at = script.find(before, start);
        Require(at != script.npos, "DaiOuJou menu script differs from the verified layout");
        script.replace(at, before.size(), after);
        start = at + after.size();
    }
    Require(script.find(before, start) == script.npos, "Unexpected duplicate DaiOuJou menu anchor");
}

void AddDeathLabelMenu(std::string& direct) {
    ReplaceExpected(direct, "  m_motion_custom = null;", R"nut(  m_motion_custom = null;
  m_death_art = null;
  m_death_label = null;)nut", 1);
    ReplaceExpected(direct, R"nut(    ["_exec_GameMode"   ,"_exec_GameMode"   ,"_exec_StageSelect"],)nut",
        R"nut(    ["_exec_GameMode"   ,"_exec_GameMode"   ,"_exec_DeathLabel"],)nut", 1);
    ReplaceExpected(direct, "    m_count = 0;", R"nut(    m_count = 0;

    if ("evac_run_death_label" in getroottable()) {
      c_cursor_width = [3,2,3,3,1];
      local artPath = conv_path("motion/title_evac_death_label.psb");
      local fontPath = conv_path("font/title_m2shot_font.psb");
      local resources = Resource();
      resources.load(artPath, fontPath);
      while (resources.loading) wait(0);
      local motion = resources.find(artPath);
      if (motion != null) {
        m_layerBase.registerMotionResource(motion);
        m_death_art = Motion(m_layerBase);
        m_death_art.chara = "evac_death_label";
        m_death_art.motion = "stand";
        m_death_art.setCoord(309, 202);
      }
      m_death_label = Indicator(m_layerBase, resources.find(fontPath));
      if (m_death_art != null) {
        m_death_label.setFontScale(1.15);
        m_death_label.setFontScaleX(0.96);
        m_death_label.setFontColor(0xdccfbfff);
      }
      m_death_label.print("DEATH LABEL");
      if (m_death_art != null) {
        m_death_label.setCoord(180, 168);
      } else {
        m_death_label.setCoord(309 - m_death_label.width / 2, 202 - m_death_label.height / 2);
      }
      _showDeathLabel(true);
    }
    if (m_x >= c_cursor_width[m_y]) m_x = c_cursor_width[m_y] - 1;
)nut", 1);
    ReplaceExpected(direct, "        this[c_exec_func[m_y][m_x]]();", R"nut(        _showDeathLabel(false);
        this[c_exec_func[m_y][m_x]]();
        _showDeathLabel(true);)nut", 1);
    ReplaceExpected(direct, "          _exec_bgmset(_gamemode);", R"nut(          _showDeathLabel(false);
          _exec_bgmset(_gamemode);
          _showDeathLabel(true);)nut", 1);
    ReplaceExpected(direct, "  function _exec_Manual() {", R"nut(  function _showDeathLabel(visible) {
    if (m_death_label != null) m_death_label.visible = visible;
    if (m_death_art != null) m_death_art.visible = visible;
  }

  function _exec_DeathLabel() {
    ::g_menu_sound.on_decide();
    local assignments = array(PlatFormButtonId.NUM, 0);
    ::g_systemdata.get_value(SystemDataValueIndex.SETTING_PAD).get_emu_key_unit(0, assignments);
    local shot = 0, bomb = 0, rapid = 0;
    // Translate the original P1 assignments to the pad bits sent to PS2.
    foreach (button in [
        [PlatFormButtonId.BUTTON_CIRCLE, 0x2000], [PlatFormButtonId.BUTTON_CROSS, 0x4000],
        [PlatFormButtonId.BUTTON_TRIANGLE, 0x1000], [PlatFormButtonId.BUTTON_SQUARE, 0x8000],
        [PlatFormButtonId.BUTTON_R1, 0x0800], [PlatFormButtonId.BUTTON_R2, 0x0200],
        [PlatFormButtonId.BUTTON_R3, 0x0004], [PlatFormButtonId.BUTTON_L1, 0x0400],
        [PlatFormButtonId.BUTTON_L2, 0x0100], [PlatFormButtonId.BUTTON_L3, 0x0002],
        [PlatFormButtonId.BUTTON_START, 0x0008], [PlatFormButtonId.BUTTON_SELECT, 0x0001]]) {
      local mask = assignments[button[0]];
      if (mask & M2EpiPadFlag.BUTTON_A) shot = shot | button[1];
      if (mask & M2EpiPadFlag.BUTTON_B) bomb = bomb | button[1];
      if (mask & M2EpiPadFlag.BUTTON_C) rapid = rapid | button[1];
    }
    // Use the effective playfield, including the size preset and manual offsets.
    local emu = ::g_emu_task;
    local screen = ::g_systemdata.get_value(SystemDataValueIndex.SETTING_SCREEN);
    local width = emu.getWidth() * emu.scaleX;
    local height = emu.getHeight() * emu.scaleY;
    if (abs(emu.getRotZdegree()) == 90) {
      local swapped = width;
      width = height;
      height = swapped;
    }
    local rotation = screen.get_rot_mode();
    local turns = rotation == RotateMode.ROT_L ? 3 : (rotation == RotateMode.ROT_R ? 1 : 0);
    local gadgets = ::g_systemdata.get_value(SystemDataValueIndex.SETTING_GADGET).get_trigger();
    local sound = ::g_systemdata.get_value(SystemDataValueIndex.SETTING_GAME);
    ::evac_run_death_label(shot, bomb, rapid, (width + 0.5).tointeger(),
      (height + 0.5).tointeger(), screen.get_pos_x(), screen.get_pos_y(), turns, gadgets ? 1 : 0,
      sound.get_index("VolumeBGM"), sound.get_index("VolumeSE"));
  }

  function _exec_Manual() {)nut", 1);
}

void AddDaioujouDirectMenu(ArchiveLayer& layer, bool arrange, bool death_label) {
    constexpr std::string_view direct_path = "1100/script/title_mode_direct_menu.nut.m";
    const auto original = layer.Read(direct_path);
    std::string direct(original.begin(), original.end());
    if (arrange) {
        ReplaceExpected(direct, "[0,1,3,3]", "[0,1,3,2]", 2);
        constexpr std::string_view selected = "m_bgm_mode[_gamemode] = _bgmMode;";
        constexpr std::string_view save =
            "m_bgm_mode[_gamemode] = _bgmMode;\n"
            "        ::g_systemdata.get_value(SystemDataValueIndex.SETTING_SOUND).set_bgm_set_mode(_gamemode, _bgmMode);\n"
            "        ::g_systemdata.CheckValueChangeAutosave(SystemDataValueIndex.SETTING_SOUND);\n"
            "        this._check_autosave();";
        ReplaceExpected(direct, selected, save, 2);
    }
    if (death_label) AddDeathLabelMenu(direct);
    layer.Add(direct_path, {reinterpret_cast<const std::uint8_t*>(direct.data()), direct.size()}, true);
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

    constexpr std::string_view gadget_path = "1100/script/title_gadget_score.nut.m";
    const auto gadget_source = layer.Read(gadget_path);
    std::string gadget(gadget_source.begin(), gadget_source.end());
    ReplaceExpected(gadget, "c_bgm_num = 3;", "c_bgm_num = 4;", 1);
    ReplaceExpected(gadget, "local _table = [0,1,3];", "local _table = [0,1,3,2];", 1);
    layer.Add(gadget_path, {reinterpret_cast<const std::uint8_t*>(gadget.data()), gadget.size()}, true);

    constexpr std::string_view gadget_motion_path = "1100/motion/title_gadget.psb.m";
    auto gadget_motion = layer.Read(gadget_motion_path, kGadgetAtlasResourceLimit);
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

void AddPcmDefinitions(ArchiveLayer& layer, std::string_view path, const Json& definitions) {
    auto pcm = ParseResourceIndex(layer.Read(path));
    auto& waves = pcm.at("pcm_wav_definition");
    Require(waves.is_array(), "Unexpected PCM definitions");
    for (const auto& definition : definitions) {
        const auto collision = std::ranges::any_of(waves, [&](const Json& wave) {
            return wave.at("no") == definition.at("no") || wave.at("filename") == definition.at("filename");
        });
        Require(!collision, "X Arrange PCM definition collides with an original track");
        waves.push_back(definition);
    }
    layer.Add(path, WritePlainPsb(pcm, 0), true);
}

void AddKetsuiArrangeMenu(ArchiveLayer& layer) {
    constexpr std::string_view profile_path = "5000/config/title_prof.psb.m";
    if (layer.Has(profile_path)) {
        auto profile = ParseResourceIndex(layer.Read(profile_path));
        auto& modes = profile.at("root").at("bgmmode_num");
        Require(modes == 5, "Unexpected Ketsui BGM mode count");
        modes = 6;
        layer.Add(profile_path, WritePlainPsb(profile, 0), true);
    }
    constexpr std::array<std::string_view, 5> settings{
        "5000/config/title_setting_game.psb.m",
        "5000/config/title_setting_game_debug.psb.m",
        "5000/config/title_setting_game_menu.psb.m",
        "5000/config/title_setting_game_setting.psb.m",
        "5000/config/title_setting_system_setting.psb.m",
    };
    for (const auto path : settings) {
        if (!layer.Has(path)) continue;
        auto config = ParseResourceIndex(layer.Read(path));
        auto& choices = config.at("param").at("bgmMode").at("subitem").at("form");
        Require(choices == Json::array({"a", "b", "c", "d", "e"}), "Unexpected Ketsui gameplay BGM settings");
        choices.push_back("x-arrange");
        layer.Add(path, WritePlainPsb(config, 0), true);
    }
    constexpr std::string_view ui_path = "5000/motion/title_ui_common.psb.m";
    if (layer.Has(ui_path)) {
        auto ui = layer.Read(ui_path);
        ApplyKetsuiArrangeArtwork(ui);
        layer.Add(ui_path, ui, true);
    }
    constexpr std::string_view cards_path = "5000/motion/title_ui_kizuna_ac.psb.m";
    if (layer.Has(cards_path)) {
        auto cards = layer.Read(cards_path, 16 * 1024 * 1024);
        ApplyKetsuiArrangeCardArtwork(cards);
        layer.Add(cards_path, cards, true);
    }
    constexpr std::string_view direct_path = "5000/script/title_mode_direct_menu.nut.m";
    if (layer.Has(direct_path)) {
        // This is the same save transform used for the regular direct menu.
        const auto menu = FixResourceScript(layer.Read(direct_path), ResourceFix::MusicAutosave);
        layer.Add(direct_path, menu, true);
    }
    constexpr std::string_view gadget_path = "5000/script/title_gadget_music_info.nut.m";
    if (layer.Has(gadget_path)) {
        const auto data = layer.Read(gadget_path);
        std::string gadget(data.begin(), data.end());
        ReplaceExpected(gadget, "c_bgm_num = 5;", "c_bgm_num = 6;", 1);
        ReplaceExpected(gadget, "[0,0,3,1,2,3]", "[0,0,3,1,2,0]", 1);
        layer.Add(gadget_path, {reinterpret_cast<const std::uint8_t*>(gadget.data()), gadget.size()}, true);
    }
    constexpr std::string_view gadget_motion_path = "5000/motion/title_gadget.psb.m";
    if (layer.Has(gadget_motion_path)) {
        auto motion = layer.Read(gadget_motion_path, 24 * 1024 * 1024);
        ApplyKetsuiArrangeGadgetArtwork(motion);
        layer.Add(gadget_motion_path, motion, true);
    }
    constexpr std::string_view gallery_path = "5000/script/title_mode_sound_gallery.nut.m";
    if (layer.Has(gallery_path)) {
        const auto data = layer.Read(gallery_path);
        std::string gallery(data.begin(), data.end());
        ReplaceExpected(gallery, "\"matsumoto\",\"virt\"]", "\"matsumoto\",\"virt\",\"x-arrange\"]", 1);
        ReplaceExpected(gallery, "[1,20,60,80,40,100]", "[1,20,60,80,40,20]", 1);
        constexpr std::string_view duration = "local _limit_time = _table[_bgmset][c_bgm_table[m_bgmIndex]];";
        ReplaceExpected(gallery, duration,
            "local _cue = c_bgm_table[m_bgmIndex];\n"
            "    local _limit_time = _bgmset == 5 ? (_cue == 11 ? 315 + 30 : 0) : _table[_bgmset][_cue];", 1);
        layer.Add(gallery_path, {reinterpret_cast<const std::uint8_t*>(gallery.data()), gallery.size()}, true);
    }
}
}

void PrepareGareggaArchive(const fs::path& data, const fs::path& stage,
    std::span<const std::uint8_t> executable) {
    const auto key = ReadArchiveSeed(executable, 0xf3);
    ArchiveLayer layer(data, kBase, key);
    auto atlases = GareggaEnglishAtlases();
    while (!atlases.empty()) {
        const auto& first = atlases.front();
        auto motion = layer.Read(first.path, 8 * 1024 * 1024);
        Require(PreparationHash(motion) == first.sha256, "Unexpected Garegga Korean motion atlas");
        do {
            ApplyGareggaEnglish(motion, atlases.front());
            atlases = atlases.subspan(1);
        } while (!atlases.empty() && atlases.front().path == first.path);
        layer.Add(first.path, motion, true);
    }
    layer.Write(stage);
}

void AddArrangeFiles(std::vector<std::string>& files, bool merged) {
    files.emplace_back(std::string(kOutput) + kBase.index);
    files.emplace_back(std::string(kOutput) + kBase.body);
    if (merged) {
        files.emplace_back(std::string(kOutput) + kUpdate.index);
        files.emplace_back(std::string(kOutput) + kUpdate.body);
    }
}

void PrepareDaioujouArchive(const fs::path& data, const fs::path& stage,
    std::span<const std::uint8_t> executable, bool merged, bool arrange, bool death_label,
    VerificationCache* verification) {
    const auto key = ReadArchiveSeed(executable, merged ? 0xdd : 0xde, true);
    auto definitions = Json::array();
    ArchiveLayer base(data, kBase, key);
    if (arrange) {
        std::size_t reserve = 28 * 1024 * 1024;
        for (const auto& cue : daioujou::ArrangeCues()) reserve += 4ull*cue.samples + 16 * 1024;
        base.Reserve(reserve);
        daioujou::ReadArrangeWaves(data,[&](std::string_view name,std::span<const std::uint8_t> wave) {
            const auto track=std::ranges::find(kDaioujouArrangeTrackNames,name);
            Require(track!=kDaioujouArrangeTrackNames.end(),"Unknown X Arrange PCM track");
            const auto i=static_cast<unsigned>(track-kDaioujouArrangeTrackNames.begin());
            const auto resource = "1100/wav/evac_" + std::string(name) + ".wav";
            base.Add(resource + ".m", wave, false);
            definitions.push_back({{"delay", 0}, {"filename", resource},
                {"no", kDaioujouArrangeFirstPcm + i}, {"stream", false}});
        },verification);
        std::sort(definitions.begin(),definitions.end(),[](const Json& a,const Json& b){return a.at("no")<b.at("no");});
        AddPcmDefinitions(base, kTitlePcm, definitions);
        AddArrangeMenu(base, false);
    }
    if (death_label) {
        base.Add("1100/motion/title_evac_death_label.psb.m", BuildDaioujouDeathLabelArtwork(), false);
        if (!merged) WriteDeathLabelGadgets(stage,base,false);
    }
    AddDaioujouDirectMenu(base, arrange, death_label);
    base.Write(stage);
    if (merged) {
        ArchiveLayer update(data, kUpdate, key);
        if (death_label) WriteDeathLabelGadgets(stage,update,true);
        if (arrange) {
            AddPcmDefinitions(update, kTitlePcm, definitions);
            AddArrangeMenu(update, true);
        }
        AddDaioujouDirectMenu(update, arrange, death_label);
        update.Write(stage);
    }
}

void PrepareKetsuiArrangeArchive(const fs::path& data, const fs::path& stage,
    std::span<const std::uint8_t> executable, bool merged, const Json& tracks) {
    auto definitions = Json::array();
    const auto key = ReadArchiveSeed(executable, merged ? 0xd3 : 0xd2);
    ArchiveLayer base(data, kBase, key);
    base.Reserve(300 * 1024 * 1024);
    for (unsigned cue = 0; cue < kKetsuiArrangeCueCount; ++cue) {
        const auto name = "5000/wav/evac_ketsui_arr_" + std::to_string(cue) + ".wav";
        const auto wave = DecodeKetsuiArrangeTrack(data, cue, tracks.at(cue).get_ref<const std::string&>());
        base.Add(name + ".m", wave, false);
        definitions.push_back({{"delay", 0}, {"filename", name},
            {"no", kKetsuiArrangeFirstPcm + cue}, {"stream", false}});
    }
    constexpr std::string_view pcm = "5000/config/title_pcm.psb.m";
    AddPcmDefinitions(base, pcm, definitions);
    AddKetsuiArrangeMenu(base);
    base.Write(stage);
    if (merged) {
        ArchiveLayer update(data, kUpdate, key);
        if (update.Has(pcm)) AddPcmDefinitions(update, pcm, definitions);
        AddKetsuiArrangeMenu(update);
        update.Write(stage);
    }
}
}
