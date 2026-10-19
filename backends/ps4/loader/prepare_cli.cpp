#include "elf_prepare.h"
#include "preparation_io.h"
#include "backends/ps4/content/resource_prepare.h"
#include "common/ini.h"
#include "common/paths.h"
#include "backends/ps4/community/protocol.h"
#include <Windows.h>
#include <cstdio>
#include <stdexcept>

namespace cave::ps4 {
namespace {
std::string Hash(std::span<const std::uint8_t> bytes) {
    return PreparationHash(bytes);
}
void CheckOutput(const fs::path& root,const fs::path& literal,const fs::path& source,const fs::path& libc) {
    const std::array sources{source.parent_path(),libc.empty()?fs::path{}:libc.parent_path()};
    CheckPreparationPath(root,literal,sources,true);
}
}
int PrepareElfCommand(int argc,wchar_t** argv) {
    try {
        if (argc<7) throw std::runtime_error("Usage: --prepare-elf source --root collection --output new-file [--module] [--libc source]");
        fs::path root,output,libc; bool module{};
        for (int i=3;i<argc;++i) {
            const std::wstring_view option(argv[i]);
            if (option==L"--module" && !module) {module=true;continue;}
            if (i+1>=argc) throw std::runtime_error("Missing ELF preparation option value");
            if (!*argv[i+1]) throw std::runtime_error("Empty ELF preparation option value");
            if (option==L"--root" && root.empty()) root=argv[++i];
            else if (option==L"--output" && output.empty()) output=argv[++i];
            else if (option==L"--libc" && libc.empty()) libc=argv[++i];
            else throw std::runtime_error("Unknown or repeated ELF preparation option");
        }
        if (root.empty() || output.empty() || !root.is_absolute() || !output.is_absolute())
            throw std::runtime_error("ELF preparation requires absolute root and output paths");
        if (output.relative_path().native().find(L':')!=std::wstring::npos) throw std::runtime_error("Prepared output cannot name an alternate data stream");
        for (const auto& part:output) if (part==".." || part==".") throw std::runtime_error("Prepared output cannot contain relative components");
        root=fs::canonical(root);
        const auto source=fs::canonical(argv[2]);
        if (!libc.empty()) libc=fs::canonical(libc);
        CheckOutput(root,output,source,libc);
        const auto input=ReadPreparationInput(source),contract=libc.empty()?std::vector<std::uint8_t>{}:ReadPreparationInput(libc);
        if (!libc.empty() && contract.empty()) throw std::runtime_error("Empty libc contract input");
        const auto prepared=PrepareElf(input,module,contract);
        if (Sha256(source)!=Hash(input) || (!libc.empty() && Sha256(libc)!=Hash(contract)))
            throw std::runtime_error("ELF source changed during preparation");
        fs::create_directories(output.parent_path());
        CheckOutput(root,output,source,libc);
        WritePreparationFile(output,prepared.bytes);
        const kt::community::Json report{{"event","elf_prepared"},{"module",module},{"source",Utf8(source)},
            {"output",Utf8(output)},{"sourceSha256",Hash(input)},{"preparedSha256",Hash(prepared.bytes)},
            {"bytes",prepared.bytes.size()},{"imageBytes",prepared.image_size},{"segments",prepared.segments},
            {"imports",prepared.imports},{"exports",prepared.exports},{"relocations",prepared.relocations},
            {"libcRetContract",!contract.empty()},{"guestExecution",false}};
        std::puts(report.dump().c_str());
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"ELF preparation: %s\n",error.what()); return 2;
    }
}
int PrepareReadFixesCommand(int argc,wchar_t** argv) {
    try {
        if (argc<7) throw std::runtime_error("Usage: --prepare-fixes game-data --root collection --output new.ktfix [--game ketsui|daioujou] [--community-replay] [--community-offline]");
        fs::path root,output; bool replay{},offline{},game_given{};std::string game="ketsui";
        for (int i=3;i<argc;++i) {
            const std::wstring_view option(argv[i]);
            if (option==L"--community-replay" || option==L"--community-offline") {
                auto& flag=option==L"--community-replay" ? replay : offline;
                if (flag) throw std::runtime_error("Unknown or repeated resource preparation option");
                flag=true;continue;
            }
            if (i+1>=argc || !*argv[i+1]) throw std::runtime_error("Missing resource preparation option value");
            if (option==L"--root" && root.empty()) root=argv[++i];
            else if (option==L"--output" && output.empty()) output=argv[++i];
            else if (option==L"--game" && !game_given) {game=Utf8(fs::path(argv[++i]));game_given=true;}
            else throw std::runtime_error("Unknown or repeated resource preparation option");
        }
        if (root.empty() || output.empty() || !root.is_absolute() || !output.is_absolute() || output.extension()!=L".ktfix")
            throw std::runtime_error("Resource preparation requires absolute root and new .ktfix output paths");
        if (output.relative_path().native().find(L':')!=std::wstring::npos) throw std::runtime_error("Prepared output cannot name an alternate data stream");
        for (const auto& part:output) if (part==".." || part==".") throw std::runtime_error("Prepared output cannot contain relative components");
        root=fs::canonical(root); const auto data=fs::canonical(argv[2]);
        const auto source_sentinel=data/L"resource-preparation-source";
        CheckOutput(root,output,source_sentinel,{});
        if ((game!="ketsui" && game!="daioujou") || (game=="daioujou" && (replay || offline)))
            throw std::runtime_error("Unsupported game or incompatible resource preparation options");
        auto result=game=="ketsui"?PrepareReadFixes(data,replay,offline):PrepareDaioujouReadFixes(data);
        fs::create_directories(output.parent_path());
        CheckOutput(root,output,source_sentinel,{}); WritePreparationFile(output,result.bytes);
        result.report.update(kt::community::Json{{"event","read_fixes_prepared"},{"output",Utf8(output)},
            {"overlay_bytes",result.bytes.size()},{"guestExecution",false}});
        std::puts(result.report.dump().c_str()); return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"Resource preparation: %s\n",error.what()); return 2;
    }
}
}
