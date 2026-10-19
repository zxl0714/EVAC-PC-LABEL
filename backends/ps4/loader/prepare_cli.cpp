#include "elf_prepare.h"
#include "backends/ps4/content/resource_prepare.h"
#include "common/ini.h"
#include "common/paths.h"
#include "backends/ps4/community/protocol.h"
#include <Windows.h>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace cave::ps4 {
namespace {
std::vector<std::uint8_t> Read(const fs::path& path) {
    if (!fs::is_regular_file(path) || fs::file_size(path)>512ull*1024*1024) throw std::runtime_error("Missing or oversized ELF input");
    std::ifstream file(path,std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read ELF input");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(fs::file_size(path)));
    file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if (!file || file.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("ELF input changed during reading");
    return bytes;
}
std::string Hash(std::span<const std::uint8_t> bytes) {
    return kt::community::Sha256({reinterpret_cast<const std::byte*>(bytes.data()),bytes.size()});
}
void CheckOutput(const fs::path& root,const fs::path& literal,const fs::path& source,const fs::path& libc) {
    auto current=literal;
    while (Within(current,root) && current!=root) {
        const auto attributes=GetFileAttributesW(current.c_str());
        if (attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Prepared output cannot traverse a file/directory link");
        current=current.parent_path();
    }
    const auto output=Resolve(root,literal);
    if (!(Within(output,root/".local") || Within(output,root/"prepared"/"ketsui")) ||
        output==root/".local" || output==root/"prepared"/"ketsui")
        throw std::runtime_error("Prepared output must be under collection .local or prepared/ketsui");
    if (Within(output,source.parent_path()) || (!libc.empty() && Within(output,libc.parent_path())))
        throw std::runtime_error("Prepared output overlaps an ELF source directory");
    const auto ini=Ini::Load(root/"config.ini");
    for (const auto& game:Catalogue()) {
        fs::path data;
        try { data=Resolve(root,ini.DataDirectory(game)); }
        catch (const std::exception&) { if (game.id=="ketsui") throw; else continue; }
        if (Within(output,data) || Within(data,output)) throw std::runtime_error("Prepared output overlaps configured game data");
    }
    if (fs::exists(output)) throw std::runtime_error("Prepared output already exists");
}
void WriteNew(const fs::path& path,std::span<const std::uint8_t> bytes) {
    const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create new prepared output");
    DWORD written{};
    const bool okay=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) &&
        written==bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!okay) { DeleteFileW(path.c_str()); throw std::runtime_error("Cannot write prepared output"); }
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
        const auto input=Read(source),contract=libc.empty()?std::vector<std::uint8_t>{}:Read(libc);
        if (!libc.empty() && contract.empty()) throw std::runtime_error("Empty libc contract input");
        const auto prepared=PrepareElf(input,module,contract);
        if (Sha256(source)!=Hash(input) || (!libc.empty() && Sha256(libc)!=Hash(contract)))
            throw std::runtime_error("ELF source changed during preparation");
        fs::create_directories(output.parent_path());
        CheckOutput(root,output,source,libc);
        WriteNew(output,prepared.bytes);
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
        if (argc<7) throw std::runtime_error("Usage: --prepare-fixes game-data --root collection --output new.ktfix [--community-replay] [--community-offline]");
        fs::path root,output; bool replay{},offline{};
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
            else throw std::runtime_error("Unknown or repeated resource preparation option");
        }
        if (root.empty() || output.empty() || !root.is_absolute() || !output.is_absolute() || output.extension()!=L".ktfix")
            throw std::runtime_error("Resource preparation requires absolute root and new .ktfix output paths");
        if (output.relative_path().native().find(L':')!=std::wstring::npos) throw std::runtime_error("Prepared output cannot name an alternate data stream");
        for (const auto& part:output) if (part==".." || part==".") throw std::runtime_error("Prepared output cannot contain relative components");
        root=fs::canonical(root); const auto data=fs::canonical(argv[2]);
        const auto source_sentinel=data/L"resource-preparation-source";
        CheckOutput(root,output,source_sentinel,{});
        auto result=PrepareReadFixes(data,replay,offline);
        fs::create_directories(output.parent_path());
        CheckOutput(root,output,source_sentinel,{}); WriteNew(output,result.bytes);
        result.report.update(kt::community::Json{{"event","read_fixes_prepared"},{"output",Utf8(output)},
            {"overlay_bytes",result.bytes.size()},{"guestExecution",false}});
        std::puts(result.report.dump().c_str()); return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"Resource preparation: %s\n",error.what()); return 2;
    }
}
}
