#include "common/catalogue.h"
#include "common/ini.h"
#include "common/paths.h"
#include "common/settings.h"
#include "common/config_update.h"
#include "launcher/window_size.h"
#include "launcher/strings.h"
#include "launcher/nickname.h"
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
void NicknameChecks() {
  Require(NormalizeDisplayNickname("  Ｐｌａｙｅｒ　") == "Player","Nickname normalization drifted from community rules");
  Require(NormalizeDisplayNickname("\xff").empty(),"Malformed nickname accepted for preview");
  Require(LegacyDisplayNickname("Player") == "Player","Printable native name changed");
  Require(LegacyDisplayNickname("abcdefghijklmnop") == "abcdefghijklmno","Legacy byte limit changed");
  Require(LegacyDisplayNickname("友人") == "Player-797d0fd6","Native Unicode alias preview differs");
  Require(Ps4DisplayNickname("abcdefghijklmn小") == "abcdefghijklmn","PS4 split a UTF-8 codepoint");
  Require(Ps4DisplayNickname("abcdefghijklmnop") == "abcdefghijklmnop","PS4 exact-limit name shortened");
  const auto ini=Ini::Parse("");
  Require(launcher::NicknameIssues(ini,"Player").empty(),"Universal ASCII name warned");
  Require(launcher::NicknameIssues(ini,"Ｐｌａｙｅｒ").empty(),"NFKC-compatible fullwidth Latin falsely warned");
  const auto aliases=launcher::NicknameIssues(ini,"ルル");
  Require(aliases.size()==4,"Supported PS4 kana incorrectly warned / Xbox limitations missed");
  for(const auto& issue:aliases)Require(issue.alias && issue.missing==U"ル" && issue.native_name==LegacyDisplayNickname("ルル"),"Duplicate glyphs/alias mismatch");
  Require(std::any_of(aliases.begin(),aliases.end(),[](const auto& issue){return issue.game->id=="guwange";}),
          "Guwange native nickname alias warning missing");
  const auto symbols=launcher::NicknameIssues(ini,"A@A");
  Require(symbols.size()==5,"Missing PS4 ASCII glyph was not detected");
  for(const auto& issue:symbols)Require(!issue.alias && issue.missing==U"@","ASCII rule incorrectly applied to PS4 font");
  const auto astral=launcher::NicknameIssues(ini,"😀😀");
  for(const auto& issue:astral)Require(issue.game->integrated && issue.game->community_supported,"Unavailable title leaked into shared nickname warnings");
  Require(astral.size()==9,"Emoji missed in a title/font variant");
  for(const auto& issue:astral)Require(issue.missing==U"😀","Surrogate pair split or duplicate warning");
  const auto independent=Ini::Parse("[game.futari.community]\nnickname=Separate\n");
  const auto overridden=launcher::NicknameIssues(independent,"ルル");
  Require(overridden.size()==3 && std::none_of(overridden.begin(),overridden.end(),[](const auto& item){return item.game->id=="futari";}),"Independent game nickname warning leaked from shared form");
  Require(!launcher::HasNicknameGlyph("ketsui",U'路',"01.00") && launcher::HasNicknameGlyph("ketsui",U'路',"01.02"),"Ketsui base/update glyph differences ignored");
  Require(launcher::HasNicknameGlyph("feveron",U'魚') && !launcher::HasNicknameGlyph("feveron",U'黒') &&
          launcher::HasNicknameGlyph("esprade",U'黒'),"Feveron coverage must not inherit unavailable common glyphs");
  const auto feveron_missing=launcher::NicknameIssues(ini,"黒");
  Require(std::any_of(feveron_missing.begin(),feveron_missing.end(),[](const auto& issue){return issue.game->id=="feveron" && issue.missing==U"黒";}),
          "Feveron-specific missing glyph warning omitted");
  const auto long_name=launcher::NicknameIssues(ini,"abcdefghijklmnopq");
  Require(long_name.size()==9,"Native length loss not warned");
  for(const auto& issue:long_name)Require(issue.shortened && issue.missing.empty() && !issue.alias,"Length warning mislabeled as missing glyph");
  Require(launcher::NicknameCodepoints("Aル😀")==U"Aル😀","UTF-8 warning character decoding");
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
void NestedRootChecks(Fixture& fixture) {
  constexpr std::string_view hash="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  for(const auto entry:{"default.xex","eboot.bin"}) {
    const auto source=fixture.temporary/FromUtf8(std::string("层级检查 ")+entry);
    const auto inner=source/FromUtf8("压缩包外层")/FromUtf8("实际游戏根目录");
    const std::array files{DataFile{entry,3,hash,true},DataFile{"res/asset.bin",3,hash,false}};
    Game game{"fixture","Fixture",entry==std::string_view("default.xex")?"xbox360":"ps4",
              "game-data/fixture","runtime/fake.exe",true,false,files,{}};
    Write(source/"README.txt","authored placement instructions");
    auto inspect=[&](const auto& data){return Inspect(game,fixture.root,data,CheckLevel::Full);};
    Require(inspect(source).state=="missing_data","placement-only directory is not an installed game");
    Write(inner/entry);Write(inner/"res/asset.bin");
    const auto nested=inspect(source);
    Require(nested.state=="nested_data" && nested.data_state==DataState::Nested &&
            nested.candidate_roots==std::vector<std::string>{Utf8(inner)} && nested.hashes_checked==0,
            "nested directory hint identifies a root but never claims data validation");
    Require(nested.data_dir==source && nested.reason.find(entry)!=std::string::npos &&
            nested.problems.front().find(Utf8(inner))!=std::string::npos,
            "nested hint retains original selection and names the actual directory");
    Require(inspect(inner).state=="ready","explicitly selected inner directory still undergoes complete validation");
    const auto alternative=source/FromUtf8("另一个候选");Write(alternative/entry);
    Require(inspect(source).candidate_roots.size()==2,"multiple candidates are reported, not chosen automatically");
    Write(source/entry,"xyz");
    Require(inspect(source).state=="unsupported_version" && inspect(source).candidate_roots.empty(),
            "a present but wrong root program cannot be hidden by a nested candidate");
    const auto too_deep=fixture.temporary/FromUtf8(std::string("更深 ")+entry);
    Write(too_deep/"a/b/c"/entry);
    Require(inspect(too_deep).state=="missing_data" && inspect(too_deep).candidate_roots.empty(),
            "hint search is restricted to two wrapper levels");
  }
}
void DataVariantChecks(Fixture& fixture) {
  const auto source=fixture.temporary/"version-fixture";
  Write(source/"eboot.bin","abc");const auto base_hash=Sha256(source/"eboot.bin");
  Write(source/"eboot.bin","def");const auto update_hash=Sha256(source/"eboot.bin");
  const std::array base_files{DataFile{"eboot.bin",3,base_hash,true},DataFile{"core.prx",3,base_hash,true},DataFile{"asset.bin",3,base_hash,false}};
  const std::array update_files{DataFile{"eboot.bin",3,update_hash,true},DataFile{"core.prx",3,update_hash,true},DataFile{"asset.bin",3,base_hash,false},DataFile{"patch.bin",3,update_hash,false}};
  Write(source/"wrapper.bin","wrapped");const auto wrapper_hash=Sha256(source/"wrapper.bin");
  const std::array wrapped_files{DataFile{"eboot.bin",7,wrapper_hash,true},DataFile{"core.prx",3,base_hash,true},DataFile{"asset.bin",3,base_hash,false}};
  const std::array<std::string_view,2> legacy{"CUSA09062-app","CUSA09062-patch"};
  const std::array<std::string_view,3> base_forbidden{"CUSA09062-app","CUSA09062-patch","patch.bin"};
  const std::array variants{DataVariant{"update-102","01.02","merged-patch","eboot.bin",update_files,legacy},
                           DataVariant{"base","01.00","base","eboot.bin",base_files,base_forbidden},
                           DataVariant{"base-self","01.00","base","eboot.bin",wrapped_files,base_forbidden}};
  Game game{"fixture","Fixture","ps4","game-data/fixture","runtime/fake.exe",true,false,update_files,{}};
  game.forbidden_files=legacy;game.variants=variants;
  auto inspect=[&](CheckLevel level=CheckLevel::Full){return Inspect(game,fixture.root,source,level);};
  Write(source/"eboot.bin");Write(source/"core.prx");Write(source/"asset.bin");
  const auto base=inspect();
  Require(base.state=="ready" && base.variant=="base" && base.version=="01.00" && base.layout=="base" &&
          base.files_checked==3 && base.hashes_checked==4,"base data selects its own complete hash manifest");
  Require(IdentifyDataVariant(game,source)==&variants[1] && DataFiles(game,"base").size()==3,"preparation can bind the identified base manifest");
  Write(source/"eboot.bin","wrapped");
  Require(inspect().state=="ready"&&inspect().variant=="base-self"&&inspect().version=="01.00","wrapped base selects its own full manifest");
  Require(inspect(CheckLevel::Quick).variant=="base-self","quick check recognizes wrapped base by raw identity");
  Write(source/"core.prx","def");
  Require(inspect().state=="unsupported_version","wrapped base rejects mixed program hashes");
  Write(source/"core.prx");Write(source/"patch.bin","def");
  Require(inspect().state=="unsupported_version","wrapped base rejects update remnants");
  fs::remove(source/"patch.bin");Write(source/"eboot.bin");
  Write(source/"patch.bin","def");
  Require(inspect().state=="unsupported_version","base executable with leftover patch resources is rejected");
  Write(source/"eboot.bin","def");
  Require(inspect().state=="unsupported_version" && inspect().version=="01.02","mixed base module and updated entry cannot validate as either version");
  Write(source/"core.prx","def");
  Require(inspect().state=="ready" && inspect().variant=="update-102" && inspect().files_checked==4,"complete overlaid data selects the updated manifest");
  fs::remove(source/"patch.bin");
  Require(inspect().state=="incomplete_data" && inspect().variant=="update-102","an updated program cannot fall back to a base manifest when a patch file is missing");
  Write(source/"eboot.bin","xyz");
  Require(inspect(CheckLevel::Quick).state=="unsupported_version" && inspect(CheckLevel::Quick).variant.empty(),"unknown same-sized program is rejected even by the quick version selector");
  fs::remove_all(source);
  Write(source/"CUSA09062-app/eboot.bin");Write(source/"CUSA09062-patch/eboot.bin","def");
  Require(inspect().state=="unsupported_version" && inspect().candidate_roots.empty(),"legacy dual-root data is rejected instead of silently selected or nested-imported");
  Reject([&] {DataFiles(game,"unknown");},"unknown variant used as primary manifest");
}
void SettingsChecks(Fixture& fixture) {
    const auto guwange=ParseSettings("","guwange");
    const auto guwange_template=ParseSettings(DefaultConfig(),"guwange");
    for (std::size_t n=0;n<kRightStickButtons.size();++n) {
        Require(guwange.keyboard_right_stick[n].keys==guwange_template.keyboard_right_stick[n].keys,
                "Guwange right-stick template/default mismatch");
        Require(ParseSettings(DefaultConfig(),"futari").keyboard_right_stick[n].keys.empty(),
                "Guwange axes leaked into another title");
    }
    Require(ParseSettings("[game.guwange.keyboard]\nright_stick_up=Numpad8,O\nright_stick_down=none\n","guwange")
                .keyboard_right_stick[0].keys.size()==2,"Guwange right-stick override");
    for (const auto* text:{"[game.guwange.keyboard]\nright_stick_up=Z\n",
            "[game.guwange.keyboard]\nright_stick_up=L\n",
            "[game.guwange.keyboard]\nright_stick_up=F8\n",
            "[keyboard]\nright_stick_up=I\n"})
        Reject([&] {ParseSettings(text,"guwange");},"Invalid/conflicting right-stick binding accepted");
    Require(ResolveSharedSettings(Ini::Parse(DefaultConfig())).keyboard_right_stick[0].keys.empty(),
            "Shared preferences unexpectedly enable title axes");
    Require(ParseSettings("", "sdoj").sdoj_frame_delay == 1, "Missing Frame Delay defaults to the low-latency path");
    Require(ParseSettings(DefaultConfig(), "sdoj").sdoj_frame_delay == 1,
            "Frame Delay template differs from the built-in default");
    for (unsigned mode : {1u, 2u, 3u})
        Require(ParseSettings("[game.sdoj]\nframe_delay="+std::to_string(mode), "sdoj").sdoj_frame_delay == mode,
                "SDOJ frame delay integer selection");
    for (const auto* value : {"0", "4", "-1", "2V", "balanced", "2.0", "2junk", ""})
        Reject([&] { ParseSettings(std::string("[game.sdoj]\nframe_delay=")+value, "sdoj"); },
               "Invalid SDOJ frame delay accepted");
    Require(ParseSettings("[game.sdoj]\nframe_delay=bad", "futari").sdoj_frame_delay == 1,
            "Another game interpreted SDOJ frame delay");
    Require(ParseSettings("", "espgaluda2").g2_frame_delay == 2, "G2 missing Frame Delay retains original path");
    Require(ParseSettings(DefaultConfig(), "espgaluda2").g2_frame_delay == 2, "G2 default template");
    for (unsigned mode : {1u, 2u})
        Require(ParseSettings("[game.espgaluda2]\nframe_delay="+std::to_string(mode), "espgaluda2").g2_frame_delay == mode,
                "G2 frame delay integer selection");
    for (const auto* value : {"0", "3", "-1", "1V", "low", "1.0", "1junk", ""})
        Reject([&] {ParseSettings(std::string("[game.espgaluda2]\nframe_delay=")+value,"espgaluda2");},
               "Invalid G2 frame delay accepted");
    Require(ParseSettings("[game.espgaluda2]\nframe_delay=bad", "futari").g2_frame_delay == 2,
            "Another game interpreted G2 frame delay");
    Require(ResolveSharedSettings(Ini::Parse("[game.espgaluda2]\nframe_delay=bad")).g2_frame_delay == 2,
            "Shared preferences interpreted G2 frame delay");
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
    Require(LoadSettings(path,"sdoj").sdoj_frame_delay==1,"New configuration did not select 1V");
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
    Require(ResolveSettings(ini,"sdoj").sdoj_frame_delay==1,"Legacy config without Frame Delay did not select 1V");
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
    const std::array first{DataFile{"packages/first/asset.bin",3,hash,false},DataFile{"packages/first/Rom/extra.bin",3,hash,false}};
    const std::array second{DataFile{"packages/second/key.bin",3,hash,false}};
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
    Write(source/second[0].path,"xyz");
    Require(inspect()[0].state==DataState::Verified && inspect()[1].state==DataState::Incomplete,"Corrupt DLC affected valid sibling");
    Write(source/second[0].path);
    Require(inspect()[1].state==DataState::Verified && inspect()[1].hashes_checked==1,"Full DLC hashes");
    fs::remove(source/first[1].path);
    Require(inspect()[0].state==DataState::Incomplete,"Missing DLC payload accepted");
    Write(source/first[1].path);
    Write(source/"Headers/old.header","old metadata, ignored");
    Require(inspect()[0].state==DataState::Verified && inspect()[1].state==DataState::Verified,"Old sidecars affected payload checks");
    Write(source/"packages/second/unexpected.bin");
    Require(inspect()[1].state==DataState::Incomplete && inspect()[0].state==DataState::Verified,"Additional payload exposed");
    fs::remove(source/"packages/second/unexpected.bin");
    Write(source/second[0].path,"short-or-truncated");
    Require(inspect()[1].state==DataState::Incomplete,"Wrong DLC size accepted");
    Write(source/second[0].path);
    Require(Ini::Parse("").ContentDirectory(game)=="game-data/fixture/dlc","Default DLC location");
    Require(Ini::Parse("").ContentDirectory(game,fixture.data)==source,"External data DLC default");
    Require(Ini::Parse("[game.fixture]\ndlc_dir=auto\n").ContentDirectory(game,fixture.data)==source,"Automatic DLC default ignored external data");
    Require(Ini::Parse("[game.fixture]\ndlc_dir=\n").ContentDirectory(game).empty(),"Explicit DLC disable ignored");
    Require(Ini::Parse("[game.fixture]\ndlc_dir=extra\n").ContentDirectory(game)=="extra","Explicit DLC location ignored");
    Require(FindGame("futari").content.size()==2 && FindGame("espgaluda2").content.empty(),"Content metadata crossed games");
    for(const auto& package:FindGame("futari").content)
      for(const auto& file:package.files)Require(ContentRelativePath(package,file).extension()!=L".header","Generated metadata still required from players");
    const auto original_hash=Sha256(source/first[0].path);
    const std::array layouts{"00000002/FIRST","FIRST","extracted-container",""};
    for(size_t index=0;index<layouts.size();++index) {
      const auto separate=fixture.temporary/("layout-"+std::to_string(index));
      for(const auto& file:first)Write(separate/layouts[index]/ContentRelativePath(packages[0],file));
      const auto results=InspectContent(game,separate,CheckLevel::Full);
      Require(results[0].state==DataState::Verified && results[1].state==DataState::Missing,"Extracted DLC layout resolution");
      Require(results[0].directory==fs::canonical(separate/layouts[index]),"Resolved payload directory not reported");
      fs::remove(separate/layouts[index]/ContentRelativePath(packages[0],first[1]));
      Require(InspectContent(game,separate,CheckLevel::Full)[0].state==DataState::Incomplete,"Partial extracted package missed");
    }
    Require(Sha256(source/first[0].path)==original_hash && !fs::exists(source/"prepared"),"Inspection modified inputs");
    const auto nested=fixture.temporary/"nested-dlc";
    for(const auto& file:first)Write(nested/"archive/contents/package"/ContentRelativePath(packages[0],file));
    const auto hint=InspectContent(game,nested,CheckLevel::Full);
    Require(hint[0].state==DataState::Nested && hint[1].state==DataState::Missing,"Nested DLC reported as absent or affected sibling");
    Require(hint[0].directory==nested/"archive/contents/package" && !hint[0].hashes_checked &&
            hint[0].problems.front().find("dlc_dir")!=std::string::npos,"Nested DLC hint accepted unverified input or missed correction");
    Require(InspectContent(game,hint[0].directory,CheckLevel::Full)[0].state==DataState::Verified,"Corrected nested DLC cannot be verified");
}
}  // namespace
int main() {
  try {
    NicknameChecks();
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
    Require(Catalogue().size() == 8 && Catalogue()[0].id == "futari" && Catalogue()[1].id == "espgaluda2" &&
            Catalogue()[2].id == "ketsui" && Catalogue()[3].id == "sdoj" &&
            Catalogue()[4].id == "daioujou" && Catalogue()[5].id == "esprade" &&
            Catalogue()[6].id == "guwange" && Catalogue()[7].id == "feveron", "Catalogue order");
    Require(!FindGame("feveron").coming_soon && FindGame("feveron").integrated &&
            FindGame("feveron").community_supported && FindGame("feveron").runtime_files.size()==3,
            "Feveron offline runtime is integrated; community acceptance remains separate");
    Require(FindGame("sdoj").community_supported && FindGame("futari").community_supported &&
            FindGame("espgaluda2").community_supported && FindGame("ketsui").community_supported,
            "Integrated titles must advertise their verified community adapters");
    const auto normal=launcher::InitialWindowSize(1104,1920,1080,96,40,6);
    Require(normal.width==1104 && normal.height==972,"Launcher leaves ten percent of the work area free");
    const auto scaled=launcher::InitialWindowSize(1104,2560,2160,144,56,6);
    Require(scaled.width==1104 && scaled.height==1463,"Launcher height follows 150% DPI");
    const auto small=launcher::InitialWindowSize(1104,1024,728,144,56,6);
    Require(small.width==1024 && small.height==655,"Small monitor height cap rounds down");
    Require(launcher::InitialWindowSize(1104,2560,2160,144,56,7).height>scaled.height,
            "Launcher grows with the game catalogue");
    for(const auto dpi:{96u,120u,144u}) {
      const int work_height=1080-int(40*dpi/96),frame_height=int(32*dpi/96);
      const auto eight=launcher::InitialWindowSize(1104,1920,work_height,dpi,frame_height,8);
      Require(eight.height==work_height*9/10,"Eight-card 1080p launcher uses at most 90 percent at 100/125/150 percent DPI");
      Require(launcher::InitialWindowSize(1104,1920,work_height,dpi,frame_height,30).height==eight.height,
              "More games do not grow the initial window beyond the cap");
    }
    Require(launcher::InitialWindowSize(1104,1920,1080,96,32,1).height==370,
            "Short catalogues do not expand to fill the height limit");
    Require(launcher::InitialWindowSize(1104,0,0,0,0,8).height==1,
            "Empty work area still yields a valid minimum size");
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
    NestedRootChecks(fixture);
    DataVariantChecks(fixture);
    std::cout << checks << " native checks passed; fixture programs were not executed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
