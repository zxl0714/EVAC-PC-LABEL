#include "common/catalogue.h"
#include "common/ini.h"
#include "common/paths.h"
#include "common/settings.h"
#include "common/config_update.h"
#include "launcher/window_size.h"
#include "launcher/strings.h"
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace cave;
namespace {
int checks = 0;
void Require(bool okay, const char* message) {
  ++checks;
  if (!okay) throw std::runtime_error(message);
}
template<class F> void Reject(F action, const char* message) {
  bool rejected = false;
  try { action(); } catch (const std::exception&) { rejected = true; }
  Require(rejected, message);
}
void Write(const fs::path& path, std::string_view bytes = "abc") {
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  file << bytes;
  if (!file) throw std::runtime_error("Test file write failed");
}
struct Fixture {
  fs::path temporary, root, data;
  Fixture() {
    auto base = fs::weakly_canonical(fs::temp_directory_path());
    std::array<unsigned char, 16> nonce{};
    if (BCryptGenRandom(nullptr, nonce.data(), ULONG(nonce.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
      throw std::runtime_error("Test nonce failed");
    std::string name = "cave-common-test-";
    for (auto n : nonce) name += std::to_string(n) + '-';
    temporary = base / name;
    if (!fs::create_directory(temporary)) throw std::runtime_error("Test directory collision");
    root = temporary / FromUtf8("合集 空格");
    data = temporary / FromUtf8("外部 游戏数据");
    fs::create_directories(root); fs::create_directories(data);
  }
  ~Fixture() {
    // Only remove the exact random directory created by this fixture.
    auto base = fs::weakly_canonical(fs::temp_directory_path());
    if (temporary.parent_path() == base && temporary.filename().native().starts_with(L"cave-common-test-")) {
      std::error_code error;
      fs::remove_all(temporary, error);
    }
  }
};
void SettingsChecks(Fixture& fixture) {
    Require(ParseSettings("", "sdoj").sdoj_frame_delay == 3, "Missing Frame Delay defaults to the original path");
    Require(ParseSettings(DefaultConfig(), "sdoj").sdoj_frame_delay == 3,
            "Frame Delay template differs from the built-in default");
    for (unsigned mode : {1u, 2u, 3u})
        Require(ParseSettings("[game.sdoj]\nframe_delay="+std::to_string(mode), "sdoj").sdoj_frame_delay == mode,
                "SDOJ frame delay integer selection");
    for (const auto* value : {"0", "4", "-1", "2V", "balanced", "2.0", "2junk", ""})
        Reject([&] { ParseSettings(std::string("[game.sdoj]\nframe_delay=")+value, "sdoj"); },
               "Invalid SDOJ frame delay accepted");
    Require(ParseSettings("[game.sdoj]\nframe_delay=bad", "futari").sdoj_frame_delay == 3,
            "Another game interpreted SDOJ frame delay");
    auto parse=[](std::string_view text) { return ParseSettings(text,"espgaluda2"); };
    const auto defaults=parse("");
    const auto documented=parse(DefaultConfig());
    Require(!defaults.fullscreen && defaults.vsync && defaults.gamepad_enabled && defaults.community_enabled &&
            defaults.volume==1 && defaults.deadzone==0.20 && defaults.nickname=="Player" &&
            defaults.endpoint=="https://xiaolu.life/cave/api/v1", "Common built-in defaults");
    for (size_t n=0; n<kButtons.size(); ++n)
        Require(defaults.keyboard[n].keys==documented.keyboard[n].keys,"Template/button mapping differs");
    Require(parse("\xef\xbb\xbf[DISPLAY]\r\nfullscreen=TRUE\r\n").fullscreen,"Settings BOM/case/CRLF");
    auto multiple=parse("[keyboard]\nsouth=Z,Numpad0\nl3=none\n");
    Require(multiple.keyboard[4].keys.size()==2 && multiple.keyboard[14].keys.empty(),"Multiple keys/disabled binding");
    Require(parse("[community]\nnickname=大佐\n").nickname=="大佐","UTF-8 nickname");
    Require(parse("[audio]\nvolume=0\n[gamepad]\ndeadzone=0\nenabled=false\n").volume==0,"Numeric zero");
    auto overrides=parse("[display]\nfullscreen=true\nvsync=false\n[audio]\nvolume=.5\n"
                         "[game.espgaluda2.display]\nfullscreen=false\n"
                         "[game.espgaluda2.keyboard]\nsouth=Numpad0\n"
                         "[game.futari.display]\nfullscreen=not-a-boolean\n"
                         "[game.ketsui]\ndata_dir=\n");
    Require(!overrides.fullscreen && !overrides.vsync && overrides.volume==.5 && overrides.keyboard[4].keys[0]==96,
            "Public inheritance and selected-game override");
    Reject([&] { ParseSettings("[game.futari.display]\nfullscreen=bad\n","futari"); },"Selected-game error ignored");
    Reject([&] { parse("[display]\nfullscreen=bad\n[game.espgaluda2.display]\nfullscreen=true\n"); },
           "Override hid malformed public value");
    Require(KeyCode("Enter")==KeyCode("Return") && KeyCode("z")==90 && !KeyCode("Mouse5"),"Key aliases");
    for (const auto* text : {
        "[display]\nfullscreen=1", "[display]\nfullscreen=true ; inline", "[display]\noops=1",
        "[gamepad]\ndeadzone=nan", "[gamepad]\ndeadzone=inf", "[gamepad]\ndeadzone=1",
        "[gamepad]\ndeadzone=-.01", "[audio]\nvolume=1.1", "[audio]\nvolume=0garbage",
        "[keyboard]\nsouth=", "[keyboard]\nsouth=Z,", "[keyboard]\nsouth=Z,z",
        "[keyboard]\nstart=Enter,Return", "[keyboard]\nsouth=F8", "[keyboard]\nsouth=F11",
        "[keyboard]\nsouth=Backtick", "[keyboard]\nsouth=none,Z", "[keyboard]\nsouth=Unknown",
        "[keyboard]\nsouth=X", "[keyboard]\nwest=Z", "[keyboard]\nshot=Z", "[logging]\nlevel=quiet",
        "[community]\nnickname=", "[community]\nendpoint=ftp://example.org", "[community]\nendpoint=https://bad host"})
        Reject([&] { parse(text); },"Invalid setting accepted");
    Reject([&] { parse(std::string("[community]\nnickname=")+char(0xff)); },"Invalid setting UTF-8");
    Reject([&] { parse(std::string(1024*1024+1,' ')); },"Unbounded config accepted");
    Require(!ValidUtf8(std::string("x\0y",3)) && !ValidUtf8("\xC0\x80") && !ValidUtf8("\xED\xA0\x80") &&
            !ValidUtf8("\xF4\x90\x80\x80") && !ValidUtf8("\xE2\x82") && ValidUtf8("\xF0\x9F\x8E\xAE"),"UTF-8 bounds");
    const auto path=EnsureConfig(fixture.root);
    Require(path==fixture.root/"user-data/config.ini" && LoadSettings(path,"espgaluda2").nickname=="Player","First-launch configuration");
    Require(LoadSettings(path,"sdoj").sdoj_frame_delay==3,"New configuration did not select 3V");
    Write(path,"; preserve comments and future fields\n[future]\nextra=unchanged\n[community]\nnickname=友人\n");
    const auto hash=Sha256(path);
    EnsureConfig(fixture.root);
    Require(Sha256(path)==hash && LoadSettings(path,"espgaluda2").nickname=="友人","Existing configuration was overwritten");
}
void MigrationChecks() {
    Fixture fixture;
    const auto legacy=fixture.root/"config.ini",current=fixture.root/"user-data/config.ini";
    const std::string before="\xef\xbb\xbf; preserved bytes\r\n[collection]\r\nlanguage=ja\r\n"
        "[community]\r\nnickname=友人\r\n[game.futari]\r\ndata_dir=game-data/futari\r\n"
        "[game.futari.display]\r\nfullscreen=true\r\n[keyboard]\r\nsouth=Numpad0\r\n[future]\r\ncustom=keep\r\n";
    Write(legacy,before);
    Require(ConfigPath(fixture.root)==legacy && !fs::exists(current),"Read-only lookup migrated config");
    Require(EnsureConfig(fixture.root)==current && !fs::exists(legacy),"Legacy config was not moved");
    Require(Ini::ReadText(current)==before,"Migration changed original config bytes");
    const auto ini=Ini::Load(current);
    Require(ResolveSettings(ini,"sdoj").sdoj_frame_delay==3,"Legacy config without Frame Delay did not select 3V");
    Require(ResolveSettings(ini,"futari").nickname=="友人" && ResolveSettings(ini,"futari").fullscreen &&
            ResolveSettings(ini,"futari").keyboard[4].keys[0]==96 &&
            Paths(fixture.root,"futari",ini.DataDirectory(FindGame("futari"))).data==fixture.root/"game-data/futari",
            "Migration changed settings or relative data root");
    Write(legacy,"[community]\nnickname=Obsolete\n");
    Require(ConfigPath(fixture.root)==current && EnsureConfig(fixture.root)==current && Ini::ReadText(current)==before,
            "Legacy config overwrote new configuration");
    Write(current,"[collection]\nconfig_version=99\n");
    Reject([&]{Ini::Load(ConfigPath(fixture.root));},"Invalid current configuration fell back to legacy");
    for (unsigned mode : {1u,2u,3u}) {
        Fixture explicit_mode;
        const auto old=explicit_mode.root/"config.ini";
        const std::string text="; preserve explicit preference\r\n[game.sdoj]\r\nframe_delay = "+std::to_string(mode)+"\r\n";
        Write(old,text);
        const auto migrated=EnsureConfig(explicit_mode.root);
        Require(Ini::ReadText(migrated)==text && LoadSettings(migrated,"sdoj").sdoj_frame_delay==mode,
                "Migration replaced an explicit Frame Delay preference");
    }
}
void PreferenceChecks(Fixture& fixture) {
    const auto config=EnsureConfig(fixture.root);
    const std::string before="\xef\xbb\xbf; keep this comment\r\n[Collection]\r\nconfig_version = 1\r\n"
        "[DISPLAY]\r\n  FullScreen = false\r\nvsync = false\r\n"
        "[Community]\r\n nickname = Player   \r\nendpoint = https://example.org/api\r\n"
        "[game.futari.display]\r\nfullscreen=false\r\n[game.futari.community]\r\nnickname=Separate\r\n"
        "[game.daioujou.display]\r\nfullscreen=bad\r\n[future]\r\nkey=preserve\r\n";
    Write(config,before);
    SaveSharedPreferences(fixture.root,before,true,"友人-42");
    auto expected=before;
    expected.replace(expected.find("FullScreen = false"),18,"FullScreen = true");
    expected.replace(expected.find("Player"),6,"友人-42");
    Require(Ini::ReadText(config)==expected,"Quick settings changed other text/overrides");
    const auto ini=Ini::Load(config);
    const auto shared=ResolveSharedSettings(ini);
    Require(shared.fullscreen && shared.nickname=="友人-42" && !shared.vsync &&
            shared.endpoint=="https://example.org/api","Shared settings read a game's override");
    for(const auto id:{"espgaluda2","ketsui"}) {
        const auto settings=ResolveSettings(ini,id);
        Require(settings.fullscreen && settings.nickname=="友人-42","Shared preference not inherited");
    }
    Require(!ResolveSettings(ini,"futari").fullscreen && ResolveSettings(ini,"futari").nickname=="Separate",
            "Quick settings removed an explicit game override");
    Reject([&]{SaveSharedPreferences(fixture.root,before,false,"Stale");},"Stale form overwrote editor changes");
    Require(Ini::ReadText(config)==expected,"Stale save changed configuration");
    for(const auto& name:std::array<std::string,7>{"",std::string(65,'a')," leading","trailing ","a\tb","a\nb",std::string("x\0y",3)}) {
        Reject([&]{SaveSharedPreferences(fixture.root,expected,false,name);},"Invalid nickname saved");
        Require(Ini::ReadText(config)==expected,"Invalid nickname partially changed fullscreen");
    }
    for(const auto* source:{"","[display]","[community]\nnickname=Old\n; comment\n[future]\nx=1",
                            "[Community]\r\n; comment\r\n[Display]\r\nvsync=true\r\n"}) {
        Write(config,source);SaveSharedPreferences(fixture.root,source,true,"Player");
        const auto result=ResolveSharedSettings(Ini::Load(config));
        Require(result.fullscreen && result.nickname=="Player","Missing settings section/key insertion");
    }
    const std::array injection{IniEdit{"display","fullscreen","true\n[community]\nnickname=Injected"}};
    Reject([&]{EditIni("",injection);},"Multiline INI edit accepted");
    for(const auto& entry:fs::directory_iterator(config.parent_path()))
        Require(!entry.path().filename().native().starts_with(L"config.ini.pending-"),"Quick-settings transaction leaked");
}
void ContentChecks(Fixture& fixture) {
    constexpr auto hash="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const std::array first{DataFile{"packages/first/asset.bin",3,hash,false},DataFile{"headers/first.header",3,hash,false}};
    const std::array second{DataFile{"packages/second/asset.bin",3,hash,false},DataFile{"headers/second.header",3,hash,false}};
    const std::array packages{ContentPackage{"first","First","packages/first","FIRST",1,first},
                              ContentPackage{"second","Second","packages/second","SECOND",1,second}};
    Game game{"fixture","Fixture","xbox360","game-data/fixture","runtime/fake.exe",true,false,{}, {},packages};
    const auto source=fixture.data/"dlc";
    auto inspect=[&](CheckLevel level=CheckLevel::Full) {return InspectContent(game,source,level);};
    Require(inspect()[0].state==DataState::Missing && inspect()[1].state==DataState::Missing,"Missing DLC blocked base policy");
    Write(source/first[0].path);Write(source/first[1].path);
    Require(inspect()[0].state==DataState::Verified && inspect()[1].state==DataState::Missing,"Single package independence");
    Require(inspect(CheckLevel::Quick)[0].state==DataState::Unverified && !inspect(CheckLevel::Quick)[0].hashes_checked,
            "Quick DLC check claimed verified");
    Write(source/second[0].path,"xyz");Write(source/second[1].path);
    Require(inspect()[0].state==DataState::Verified && inspect()[1].state==DataState::Incomplete,"Corrupt DLC affected valid sibling");
    Write(source/second[0].path);
    Require(inspect()[1].state==DataState::Verified && inspect()[1].hashes_checked==2,"Full DLC hashes");
    fs::remove(source/second[1].path);
    Require(inspect()[1].state==DataState::Incomplete,"Missing DLC header accepted");
    Write(source/second[1].path);
    Write(source/"packages/second/unexpected.bin");
    Require(inspect()[1].state==DataState::Incomplete && inspect()[0].state==DataState::Verified,"Additional payload exposed");
    fs::remove(source/"packages/second/unexpected.bin");
    Write(source/"packages/second/asset.bin","short-or-truncated");
    Require(inspect()[1].state==DataState::Incomplete,"Wrong DLC size accepted");
    Write(source/second[0].path);
    Require(Ini::Parse("").ContentDirectory(game)=="game-data/fixture/dlc","Default DLC location");
    Require(Ini::Parse("").ContentDirectory(game,fixture.data)==source,"External data DLC default");
    Require(Ini::Parse("[game.fixture]\ndlc_dir=\n").ContentDirectory(game).empty(),"Explicit DLC disable ignored");
    Require(Ini::Parse("[game.fixture]\ndlc_dir=extra\n").ContentDirectory(game)=="extra","Explicit DLC location ignored");
    Require(FindGame("futari").content.size()==2 && FindGame("espgaluda2").content.empty(),"Content metadata crossed games");
}
}  // namespace
int main() {
  try {
    Fixture fixture;
    MigrationChecks();
    using launcher::Language;
    Fixture language_fixture;
    Require(launcher::ReadLanguage(Ini::Parse(""))==Language::Chinese,"Existing INI language default changed");
    for(const auto lang:{Language::Chinese,Language::Japanese,Language::English}) {
      Require(launcher::ParseLanguage(launcher::LanguageCode(lang))==lang,"Language code round trip");
      const std::array changes{IniEdit{"collection","language",launcher::LanguageCode(lang)}};
      const auto before=std::string("; retain comment\n[future]\nvalue=keep\n[community]\nnickname=Player\n");
      const auto language_config=EnsureConfig(language_fixture.root);
      Write(language_config,before);
      UpdateConfig(language_fixture.root,before,changes);
      const auto after=Ini::ReadText(language_config);
      Require(launcher::ReadLanguage(Ini::Parse(after))==lang && after.starts_with(before),"Language save/restart or text preservation");
      SaveSharedPreferences(language_fixture.root,after,true,"Friend");
      Require(launcher::ReadLanguage(Ini::Load(language_config))==lang,"Quick settings overwrote language");
      for(const auto& row:launcher::kTranslations)
        Require(std::wstring_view(launcher::Translate(lang,row.key)).size()>0,"Empty localized UI string");
    }
    for(const auto value:{"","fr","ZH","zh-CN","ja\nen"})
      Reject([&]{launcher::ParseLanguage(value);},"Invalid UI language accepted");
    Reject([]{launcher::Translate(Language::English,L"missing-key");},"Missing translation silently accepted");
    for(const auto& game:Catalogue())Require(!game.name_japanese.empty() && game.DisplayName()==game.name_japanese,"Missing Japanese display name");
    constexpr std::string_view hash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const std::array files{DataFile{"main.bin",3,hash,true}, DataFile{"res/asset.bin",3,hash,false}};
    const std::array<std::string_view,1> runtime{"runtime/fake.exe"};
    Game game{"fixture","Fixture","xbox360","game-data/fixture","runtime/fake.exe",true,false,files,runtime};
    Require(Catalogue().size() == 6 && Catalogue()[0].id == "futari" && Catalogue()[1].id == "espgaluda2" &&
            Catalogue()[2].id == "ketsui" && Catalogue()[3].id == "sdoj" &&
            Catalogue()[4].id == "daioujou" && Catalogue()[5].id == "esprade", "Catalogue order");
    Require(FindGame("sdoj").community_supported && FindGame("futari").community_supported &&
            FindGame("espgaluda2").community_supported && FindGame("ketsui").community_supported,
            "Integrated titles must advertise their verified community adapters");
    const auto normal=launcher::InitialWindowSize(1104,1920,1080,96,40,6);
    Require(normal.width==1104 && normal.height==1018,"Launcher fits all six cards at 100% DPI");
    const auto scaled=launcher::InitialWindowSize(1104,2560,2160,144,56,6);
    Require(scaled.width==1104 && scaled.height==1523,"Launcher height follows 150% DPI");
    const auto small=launcher::InitialWindowSize(1104,1024,728,144,56,6);
    Require(small.width==1024 && small.height==728,"Launcher stays inside a small monitor work area");
    Require(launcher::InitialWindowSize(1104,2560,2160,144,56,7).height>scaled.height,
            "Launcher grows with the game catalogue");
    Reject([] { FindGame("unknown"); }, "Unknown game accepted");
    Require(FindGame("espgaluda2").files.size() == 155, "G2 complete inventory");
    auto inspect = [&](CheckLevel level = CheckLevel::Full) { return Inspect(game, fixture.root, fixture.data, level); };
    Require(inspect().state == "missing_data", "Empty data state");
    Write(fixture.data / "main.bin");
    Require(Sha256(fixture.data / "main.bin") == hash, "SHA-256 known vector");
    Require(inspect().state == "incomplete_data", "Missing resource state");
    Write(fixture.data / "res/asset.bin");
    Require(inspect().state == "missing_runtime", "Missing runtime state");
    Write(fixture.root / "runtime/fake.exe");
    Require(inspect(CheckLevel::Quick).state == "needs_verification" && inspect(CheckLevel::Quick).hashes_checked == 0,
            "Quick scan must not claim verified");
    Require(inspect(CheckLevel::Programs).data_state == DataState::Unverified && inspect(CheckLevel::Programs).hashes_checked == 1,
            "Program verification must not claim complete resource hashes");
    auto verified = inspect();
    Require(verified.state == "ready" && verified.hashes_checked == 2, "Full verification");
    const std::array<std::string_view,1> forbidden{"main.binp"};
    game.forbidden_files = forbidden;
    Require(inspect().state == "ready", "Absent companion rejected base data");
    Write(fixture.data / "main.binp", "authored patch fixture");
    for (const auto level : {CheckLevel::Quick, CheckLevel::Programs, CheckLevel::Full}) {
      const auto patched = inspect(level);
      Require(patched.data_state == DataState::Unsupported && patched.state == "unsupported_version" &&
              patched.problems[0] == "unsupported companion: main.binp", "Undeclared companion accepted");
    }
    // Other titles without this manifest restriction retain their own policy.
    game.forbidden_files = {};
    Require(inspect().state == "ready", "Title-local companion restriction leaked");
    Write(fixture.data / "main.bin", "xyz");
    Require(inspect().state == "unsupported_version", "Same-size wrong program accepted");
    Write(fixture.data / "main.bin"); Write(fixture.data / "res/asset.bin", "xyz");
    Require(inspect().state == "incomplete_data", "Corrupt resource accepted");
    Write(fixture.data / "res/asset.bin");
    game.integrated = false;
    Require(inspect().state == "not_integrated" && inspect().data_state == DataState::Verified, "Unintegrated title marked ready");
    game.coming_soon = true;
    Require(inspect().state == "coming_soon", "Future title marked available");
    game.coming_soon = false; game.integrated = true;
    auto paths = Paths(fixture.root, "fixture", fixture.data);
    ValidateOutputs(paths);
    Require(paths.data == fixture.data && Within(paths.logs, fixture.root), "External/Unicode paths");
    Reject([&] { ValidateOutputs(Paths(fixture.root,"fixture",fixture.root)); }, "Data/output overlap accepted");
    Reject([&] { Paths(fixture.root,"fixture",fixture.data,"../unsafe"); }, "Profile traversal accepted");
    Reject([&] { Resolve(fixture.root,L"C:relative"); }, "Drive-relative path accepted");
    Reject([&] { DataChild(fixture.data,"../escape.bin"); }, "Manifest traversal accepted");
    auto alias = paths; alias.logs = paths.profile;
    Reject([&] { ValidateOutputs(alias); }, "Overlapping output roots accepted");
    const auto ini = Ini::Parse("\xef\xbb\xbf[Collection]\nconfig_version=1\n[Game.Fixture]\nDATA_DIR = 外部 游戏数据\n[game.absent]\ndata_dir=\n");
    Require(ini.DataDirectory(game) == FromUtf8("外部 游戏数据"), "Case/BOM/Unicode INI");
    Require(Ini::Parse("").DataDirectory(game) == "game-data/fixture", "Missing config default");
    Reject([] { Ini::Parse("[x]\na=1\nA=2\n"); }, "Duplicate INI key");
    Reject([] { Ini::Parse("[collection]\nconfig_version=99\n"); }, "Future config version accepted");
    Reject([] { Ini::Parse("[bad\n"); }, "Bad INI section");
    Reject([&] { Ini::Parse("[game.fixture]\ndata_dir=\n").DataDirectory(game); }, "Empty selected path accepted");
    Reject([] { Ini::Parse(std::string("[x]\na=") + char(0xff)); }, "Invalid UTF-8 accepted");
    Require(inspect().state == "ready", "Other-game settings affected current title");
    SettingsChecks(fixture);
    PreferenceChecks(fixture);
    ContentChecks(fixture);
    std::cout << checks << " native checks passed; fixture programs were not executed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
