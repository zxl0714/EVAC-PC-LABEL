#include "preparation_io.h"
#include "backends/ps4/content/read_patches.h"
#include "common/ini.h"
#include "common/settings.h"
#include <Windows.h>
#include <fstream>
#include <stdexcept>

namespace cave::ps4 {
PreparationBytes ReadPreparationInput(const fs::path& path,std::uint64_t limit) {
    if (!fs::is_regular_file(path) || fs::file_size(path)>limit) throw std::runtime_error("Missing or oversized preparation input");
    std::ifstream file(path,std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read preparation input");
    PreparationBytes bytes(static_cast<std::size_t>(fs::file_size(path)));
    file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if (!file || file.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("Preparation input changed during reading");
    return bytes;
}
std::string PreparationHash(std::span<const std::uint8_t> bytes) {
    const auto hash=kt::content::ReadPatchHash(std::as_bytes(bytes));
    constexpr char digits[]="0123456789abcdef"; std::string result;
    for (const auto byte:hash) {const auto n=std::to_integer<unsigned>(byte);result+=digits[n>>4];result+=digits[n&15];}
    return result;
}
void CheckPreparationPath(const fs::path& root,const fs::path& literal,std::span<const fs::path> sources,
                          bool require_new,bool allow_container) {
    if (!literal.is_absolute()) throw std::runtime_error("Prepared output requires an absolute path");
    if (literal.relative_path().native().find(L':')!=std::wstring::npos) throw std::runtime_error("Prepared output cannot name an alternate data stream");
    for (const auto& part:literal) if (part==".." || part==".") throw std::runtime_error("Prepared output cannot contain relative components");
    for (auto current=literal;Within(current,root) && current!=root;current=current.parent_path()) {
        const auto attributes=GetFileAttributesW(current.c_str());
        if (attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Prepared output cannot traverse a file/directory link");
    }
    const auto output=Resolve(root,literal);
    bool allowed=Within(output,root/".local") && (allow_container || output!=root/".local");
    for (const auto& game:Catalogue()) {
        const auto container=root/"prepared"/FromUtf8(game.id);
        allowed |= Within(output,container) && (allow_container || output!=container);
    }
    if (!allowed) throw std::runtime_error("Prepared output must be under collection .local or a known prepared/<game> directory");
    for (const auto& source:sources) if (!source.empty() && (Within(output,source) || Within(source,output)))
        throw std::runtime_error("Prepared output overlaps an ELF source directory");
    const auto ini=Ini::Load(ConfigPath(root));
    for (const auto& game:Catalogue()) {
        fs::path data,content;
        try {data=Resolve(root,ini.DataDirectory(game));}
        catch (const std::exception&) {if (game.id=="ketsui" || Within(output,root/"prepared"/FromUtf8(game.id))) throw;}
        if (!data.empty() && (Within(output,data) || Within(data,output)))
            throw std::runtime_error("Prepared output overlaps configured game data");
        try {const auto path=ini.ContentDirectory(game,data);if (!path.empty()) content=Resolve(root,path);}
        catch (const std::exception&) {if (game.id=="ketsui" || Within(output,root/"prepared"/FromUtf8(game.id))) throw;}
        if (!content.empty() && (Within(output,content) || Within(content,output)))
            throw std::runtime_error("Prepared output overlaps configured DLC data");
    }
    if (require_new && fs::exists(output)) throw std::runtime_error("Prepared output already exists");
}
void WritePreparationFile(const fs::path& path,std::span<const std::uint8_t> bytes) {
    if (bytes.size()>512ull*1024*1024) throw std::runtime_error("Oversized prepared output");
    const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create new prepared output");
    DWORD written{};
    const bool okay=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) &&
        written==bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!okay) {DeleteFileW(path.c_str());throw std::runtime_error("Cannot write prepared output");}
}
}
