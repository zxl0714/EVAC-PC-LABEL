#include "prepared_cache.h"
#include "elf_prepare.h"
#include "backends/ps4/content/resource_prepare.h"
#include "common/ini.h"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <stdexcept>

namespace cave::ps4 {
namespace {
using Json=nlohmann::json;
void CheckData(const fs::path& root,const fs::path& data) {
    const auto inspected=Inspect(FindGame("ketsui"),root,data,CheckLevel::Full);
    if (inspected.data_state!=DataState::Verified) throw std::runtime_error("Ketsui data is not the complete supported app/1.02 patch: "+
        (inspected.problems.empty()?inspected.reason:inspected.problems.front()));
}
}
PreparedGame PrepareKetsuiGame(const fs::path& requested_root,const fs::path& requested_data) {
    const auto root=fs::canonical(requested_root),data=fs::canonical(requested_data);
    ValidateOutputs(Paths(root,"ketsui",data));CheckData(root,data);
    std::array<wchar_t,32768> executable{};
    const auto length=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    if (!length || length>=executable.size()) throw std::runtime_error("Cannot determine preparation program identity");
    const fs::path producer(executable.data());const auto producer_hash=Sha256(producer);
    auto inputs=Json::array();
    for (const auto& file:FindGame("ketsui").files) inputs.push_back({{"path",file.path},{"bytes",file.bytes},{"sha256",file.sha256}});
    const Json recipe{{"format",1},{"game","ketsui"},{"preparationContract",1},{"producerSha256",producer_hash},
        {"inputs",std::move(inputs)},{"libcRetContract",true},{"musicAutosave",true},{"communityReplayBatch",true},{"communityLocalRounds",true}};
    const std::array<std::string,5> files{"eboot.ktprobe","modules/emu_integration_01400.prx.ktmodule",
        "modules/ketsui_01000.prx.ktmodule","game-fixes.ktfix","game-fixes.json"};
    auto cache=EnsurePreparedCache(root,data,recipe,files,[&](const fs::path& stage) {
        const auto libc=ReadPreparationInput(DataChild(data,"CUSA09062-app/sce_module/libc.prx"));
        const std::array<std::string,3> programs{"eboot.bin","emu_integration_01400.prx","ketsui_01000.prx"};
        for (std::size_t i=0;i<programs.size();++i) {
            const auto source=ReadPreparationInput(DataChild(data,"CUSA09062-patch/"+programs[i]));
            const auto prepared=PrepareElf(source,i!=0,libc);WritePreparedRelative(stage,files[i],prepared.bytes);
        }
        const auto fixes=PrepareReadFixes(data,true,true);WritePreparedRelative(stage,files[3],fixes.bytes);
        const auto metadata=fixes.report.dump(2)+'\n';
        WritePreparedRelative(stage,files[4],{reinterpret_cast<const std::uint8_t*>(metadata.data()),metadata.size()});
        CheckData(root,data);
        if (Sha256(producer)!=producer_hash) throw std::runtime_error("Preparation program changed while preparing data");
    });
    return {cache,cache.directory/"eboot.ktprobe",cache.directory/"modules",cache.directory/"game-fixes.ktfix"};
}
int PrepareGameCommand(int argc,wchar_t** argv) {
    try {
        fs::path root,data;
        for (int i=2;i<argc;++i) {
            if (i+1>=argc || !*argv[i+1]) throw std::runtime_error("Usage: --prepare-game --root collection [--data-dir supported-data]");
            const std::wstring_view option(argv[i]);
            if (option==L"--root" && root.empty()) root=argv[++i];
            else if (option==L"--data-dir" && data.empty()) data=argv[++i];
            else throw std::runtime_error("Unknown or repeated game preparation option");
        }
        if (root.empty() || !root.is_absolute()) throw std::runtime_error("Game preparation requires an absolute collection root");
        root=fs::canonical(root);
        if (data.empty()) data=Resolve(root,Ini::Load(root/"config.ini").DataDirectory(FindGame("ketsui")));
        const auto prepared=PrepareKetsuiGame(root,data);
        const Json result{{"event","game_data_prepared"},{"game","ketsui"},{"cacheHit",prepared.cache.reused},
            {"cacheMissReason",prepared.cache.miss_reason},{"recipeSha256",prepared.cache.key},
            {"directory",Utf8(prepared.cache.directory)},{"executable",Utf8(prepared.executable)},
            {"modules",Utf8(prepared.modules)},{"readFixes",Utf8(prepared.read_fixes)},
            {"sourceValidated",true},{"preparedOutputsVerified",true},{"guestExecution",false}};
        std::puts(result.dump().c_str());return 0;
    } catch (const std::exception& error) {std::fprintf(stderr,"Game preparation: %s\n",error.what());return 2;}
}
}
