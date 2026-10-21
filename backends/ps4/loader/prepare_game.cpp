#include "prepared_cache.h"
#include "elf_prepare.h"
#include "backends/ps4/content/resource_prepare.h"
#include "common/ini.h"
#include "common/settings.h"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace cave::ps4 {
namespace {
using Json=nlohmann::json;
Inspection CheckData(const fs::path& root,const fs::path& data,std::string_view id) {
    auto game=FindGame(id);
    // Preparation verifies inputs independently of the player-facing capability
    // gate. A prepared image is not proof that an unfinished title is playable.
    game.coming_soon=false;
    const auto inspected=Inspect(game,root,data,CheckLevel::Full);
    if (inspected.data_state!=DataState::Verified) throw std::runtime_error(std::string(id)+" data is not the complete supported version: "+
        (inspected.problems.empty()?inspected.reason:inspected.problems.front()));
    return inspected;
}
}
PreparedGame PrepareKetsuiGame(const fs::path& requested_root,const fs::path& requested_data) {
    return PreparePs4Game(requested_root,requested_data,"ketsui");
}
PreparedGame PreparePs4Game(const fs::path& requested_root,const fs::path& requested_data,std::string_view game) {
    if (game!="ketsui" && game!="daioujou") throw std::runtime_error("Unsupported PS4 preparation game");
    const bool ketsui=game=="ketsui";
    const auto root=fs::canonical(requested_root),data=fs::canonical(requested_data);
    ValidateOutputs(Paths(root,game,data));const auto inspected=CheckData(root,data,game);
    std::array<wchar_t,32768> executable{};
    const auto length=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    if (!length || length>=executable.size()) throw std::runtime_error("Cannot determine preparation program identity");
    const fs::path producer(executable.data());const auto producer_hash=Sha256(producer);
    auto inputs=Json::array();
    for (const auto& file:DataFiles(FindGame(game),inspected.variant)) inputs.push_back({{"path",file.path},{"bytes",file.bytes},{"sha256",file.sha256}});
    const Json recipe{{"format",1},{"game",game},{"preparationContract",1},{"producerSha256",producer_hash},
        {"inputs",std::move(inputs)},{"libcRetContract",true},{"mergedApp",!ketsui || inspected.layout=="merged-patch"},
        {"dataVariant",inspected.variant},{"dataVersion",inspected.version},
        {"musicAutosave",ketsui},{"communityReplayBatch",ketsui},{"communityLocalRounds",ketsui},
        {"communityReplayPeerFallback",!ketsui},{"communityLocalReplay",!ketsui}};
    const std::vector<std::string> programs=ketsui ?
        std::vector<std::string>{"eboot.bin","emu_integration_01400.prx","ketsui_01000.prx"} :
        std::vector<std::string>{"eboot.bin","emu_integration_01700.prx","daioujou_01000.prx","daioujou_01010.prx",
            "daioujou_01020.prx","daioujou_01030.prx"};
    std::vector<std::string> files{"eboot.ktprobe"};
    for (std::size_t i=1;i<programs.size();++i) files.push_back("modules/"+programs[i]+".ktmodule");
    files.push_back("game-fixes.ktfix");files.push_back("game-fixes.json");
    auto cache=EnsurePreparedCache(root,data,recipe,files,[&](const fs::path& stage) {
        const auto libc=ReadPreparationInput(DataChild(data,"sce_module/libc.prx"));
        for (std::size_t i=0;i<programs.size();++i) {
            const auto source=ReadPreparationInput(DataChild(data,programs[i]));
            const auto prepared=PrepareElf(source,i!=0,libc);WritePreparedRelative(stage,files[i],prepared.bytes);
        }
        const auto fixes=ketsui?PrepareReadFixes(data,true,true):PrepareDaioujouReadFixes(data);
        WritePreparedRelative(stage,files[programs.size()],fixes.bytes);
        const auto metadata=fixes.report.dump(2)+'\n';
        WritePreparedRelative(stage,files[programs.size()+1],{reinterpret_cast<const std::uint8_t*>(metadata.data()),metadata.size()});
        if(CheckData(root,data,game).variant!=inspected.variant)throw std::runtime_error("Game-data version changed during preparation");
        if (Sha256(producer)!=producer_hash) throw std::runtime_error("Preparation program changed while preparing data");
    });
    return {cache,cache.directory/"eboot.ktprobe",cache.directory/"modules",cache.directory/"game-fixes.ktfix"};
}
int PrepareGameCommand(int argc,wchar_t** argv) {
    try {
        fs::path root,data;std::string game="ketsui";bool game_given{};
        for (int i=2;i<argc;++i) {
            if (i+1>=argc || !*argv[i+1]) throw std::runtime_error("Usage: --prepare-game --root collection [--game ketsui|daioujou] [--data-dir supported-data]");
            const std::wstring_view option(argv[i]);
            if (option==L"--root" && root.empty()) root=argv[++i];
            else if (option==L"--data-dir" && data.empty()) data=argv[++i];
            else if (option==L"--game" && !game_given) {game=Utf8(fs::path(argv[++i]));game_given=true;}
            else throw std::runtime_error("Unknown or repeated game preparation option");
        }
        if (root.empty() || !root.is_absolute()) throw std::runtime_error("Game preparation requires an absolute collection root");
        root=fs::canonical(root);
        if (data.empty()) data=Resolve(root,Ini::Load(ConfigPath(root)).DataDirectory(FindGame(game)));
        const auto prepared=PreparePs4Game(root,data,game);
        const Json result{{"event","game_data_prepared"},{"game",game},{"cacheHit",prepared.cache.reused},
            {"cacheMissReason",prepared.cache.miss_reason},{"recipeSha256",prepared.cache.key},
            {"directory",Utf8(prepared.cache.directory)},{"executable",Utf8(prepared.executable)},
            {"modules",Utf8(prepared.modules)},{"readFixes",Utf8(prepared.read_fixes)},
            {"sourceValidated",true},{"preparedOutputsVerified",true},{"guestExecution",false}};
        std::puts(result.dump().c_str());return 0;
    } catch (const std::exception& error) {std::fprintf(stderr,"Game preparation: %s\n",error.what());return 2;}
}
}
