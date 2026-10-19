#include "prepared_cache.h"
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>

namespace cave::ps4 {
namespace {
using Json=nlohmann::json;
void Require(bool okay,const char* message) {if (!okay) throw std::runtime_error(message);}
PreparationBytes TextBytes(const std::string& text) {return {text.begin(),text.end()};}
std::string GenericName(const fs::path& path) {
    const auto text=path.generic_u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};
}
std::string Nonce() {
    std::array<unsigned char,16> bytes{};
    Require(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,
        "Cannot generate prepared cache identity");
    constexpr char digits[]="0123456789abcdef";std::string result;
    for (auto b:bytes) {result+=digits[b>>4];result+=digits[b&15];}return result;
}
Json ReadJson(const fs::path& path) {
    const auto bytes=ReadPreparationInput(path,256*1024);
    return Json::parse(bytes.begin(),bytes.end(),[](int depth,Json::parse_event_t,Json&) {
        Require(depth<=64,"Prepared cache JSON nesting exceeds limit");return true;
    });
}
fs::path Relative(const fs::path& parent,std::string_view name) {
    Require(!name.empty() && name.size()<=256 && name.find_first_of("\\:\r\n") == name.npos,
        "Invalid prepared cache relative path");
    const auto child=DataChild(parent,name);
    Require(child!=parent && Within(child,parent),"Prepared cache path escapes generation");return child;
}
void CheckTree(const fs::path& root,const fs::path& directory,std::span<const std::string> files,
               std::span<const fs::path> sources) {
    CheckPreparationPath(root,directory,sources,false,true);
    Require(fs::is_directory(directory),"Prepared cache generation is missing");
    std::set<std::string> expected{"receipt.json"},directories;
    for (const auto& name:files) {
        Require(expected.insert(name).second,"Duplicate prepared cache file");
        for (auto parent=FromUtf8(name).parent_path();!parent.empty();parent=parent.parent_path()) directories.insert(GenericName(parent));
    }
    std::size_t entries{};
    for (const auto& item:fs::recursive_directory_iterator(directory)) {
        Require(++entries<=files.size()+directories.size()+1,"Unexpected prepared cache entries");
        CheckPreparationPath(root,item.path(),sources);
        const auto name=GenericName(item.path().lexically_relative(directory));
        Require(item.is_directory() ? directories.contains(name) : item.is_regular_file() && expected.contains(name),
            "Unexpected prepared cache entry");
    }
    Require(entries==files.size()+directories.size()+1,"Missing prepared cache files");
}
Json Validate(const fs::path& root,const fs::path& directory,const Json& recipe,const std::string& key,
              std::span<const std::string> files,std::span<const fs::path> sources) {
    CheckTree(root,directory,files,sources);
    const auto receipt=ReadJson(directory/"receipt.json");
    Require(receipt.at("format")==1 && receipt.at("game")==recipe.at("game") && receipt.at("recipe")==recipe &&
        receipt.at("recipeSha256").get<std::string>()==key,"Prepared cache recipe differs");
    const auto& outputs=receipt.at("outputs");
    Require(outputs.is_array() && outputs.size()==files.size(),"Prepared cache output inventory differs");
    for (std::size_t i=0;i<files.size();++i) {
        const auto& item=outputs[i];const auto path=Relative(directory,files[i]);
        if (item.at("path").get<std::string>()!=files[i] || !item.at("bytes").is_number_unsigned() ||
            item.at("bytes").get<std::uint64_t>()>512ull*1024*1024 || !fs::is_regular_file(path) ||
            fs::file_size(path)!=item.at("bytes").get<std::uint64_t>())
            throw std::runtime_error("Prepared cache output metadata differs: "+files[i]);
        const auto hash=Sha256(path);
        if (hash!=item.at("sha256").get<std::string>()) throw std::runtime_error("Prepared cache output hash differs: "+files[i]);
    }
    return receipt;
}
struct Lock {
    HANDLE value{INVALID_HANDLE_VALUE};
    explicit Lock(const fs::path& file) {
        value=CreateFileW(file.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,
            FILE_ATTRIBUTE_HIDDEN|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
        Require(value!=INVALID_HANDLE_VALUE,"Another process is preparing this cache, or the cache cannot be locked");
    }
    ~Lock() {if (value!=INVALID_HANDLE_VALUE) CloseHandle(value);}
};
}
void WritePreparedRelative(const fs::path& stage,std::string_view name,std::span<const std::uint8_t> bytes) {
    const auto output=Relative(stage,name);
    for (auto path=stage/FromUtf8(name);Within(path,stage) && path!=stage;path=path.parent_path()) {
        const auto attributes=GetFileAttributesW(path.c_str());
        Require(attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"Prepared staging path is linked");
    }
    fs::create_directories(output.parent_path());WritePreparationFile(output,bytes);
}
PreparedCache EnsurePreparedCache(const fs::path& requested_root,const fs::path& requested_data,
    const Json& recipe,std::span<const std::string> files,const std::function<void(const fs::path&)>& build) {
    Require(recipe.is_object() && recipe.dump().size()<=128*1024 && !files.empty() && files.size()<=16,"Invalid prepared cache recipe");
    // The title is part of both the recipe hash and the writable namespace.
    // Never reuse another game's active pointer, even for identical outputs.
    const auto game=recipe.at("game").get<std::string>();
    (void)FindGame(game);
    const auto root=fs::canonical(requested_root),data=fs::canonical(requested_data);
    const auto container=root/"prepared"/FromUtf8(game);
    const std::array sources{data};
    CheckPreparationPath(root,container,sources,false,true);
    ValidateOutputs(Paths(root,game,data));
    std::set<std::string> unique;
    for (const auto& name:files) {
        (void)Relative(container,name);
        Require(name!="receipt.json" && unique.insert(name).second,"Invalid prepared cache output inventory");
    }
    CheckPreparationPath(root,container/".prepare.lock",sources);
    CheckPreparationPath(root,container/"active.json",sources);
    const auto key=PreparationHash(TextBytes(recipe.dump()));
    fs::create_directories(container);Lock lock(container/".prepare.lock");
    const auto active=container/"active.json";
    std::string miss="not_prepared";
    if (fs::exists(active)) {
        try {
            const auto pointer=ReadJson(active);
            Require(pointer.at("format")==1 && pointer.at("recipeSha256").get<std::string>()==key,"Prepared cache inputs changed");
            const auto name=pointer.at("directory").get<std::string>(),prefix="cache-"+key.substr(0,16)+"-";
            Require(name.size()==prefix.size()+32 && name.starts_with(prefix) &&
                name.find_first_not_of("0123456789abcdef",prefix.size())==name.npos,"Invalid prepared cache generation reference");
            const auto directory=Relative(container,name);
            return {directory,key,{},true,Validate(root,directory,recipe,key,files,sources)};
        } catch (const std::exception& error) {miss=std::string(error.what()).substr(0,512);}
    }
    // Keep paths short for the guest loader's ordinary Win32 file APIs. The
    // complete SHA-256 is still checked in both pointer and immutable receipt.
    const auto nonce=Nonce();const auto stage=container/(".pending-"+nonce),directory=container/("cache-"+key.substr(0,16)+"-"+nonce);
    CheckPreparationPath(root,stage,sources,true,true);CheckPreparationPath(root,directory,sources,true,true);
    Require(fs::create_directory(stage),"Cannot create independent prepared staging directory");
    build(stage);
    auto outputs=Json::array();
    for (const auto& name:files) {
        const auto file=Relative(stage,name);CheckPreparationPath(root,file,sources);
        Require(fs::is_regular_file(file) && fs::file_size(file)<=512ull*1024*1024,"Missing or oversized newly prepared output");
        outputs.push_back({{"path",name},{"bytes",fs::file_size(file)},{"sha256",Sha256(file)}});
    }
    const Json receipt{{"format",1},{"game",game},{"recipe",recipe},{"recipeSha256",key},{"outputs",std::move(outputs)}};
    WritePreparedRelative(stage,"receipt.json",TextBytes(receipt.dump(2)+'\n'));
    (void)Validate(root,stage,recipe,key,files,sources);
    CheckPreparationPath(root,directory,sources,true,true);
    Require(MoveFileExW(stage.c_str(),directory.c_str(),MOVEFILE_WRITE_THROUGH),"Cannot publish prepared generation");
    const auto pending=container/(".active-"+nonce+".json");
    CheckPreparationPath(root,pending,sources,true);
    WritePreparationFile(pending,TextBytes(Json{{"format",1},{"recipeSha256",key},{"directory",Utf8(directory.filename())}}.dump(2)+'\n'));
    CheckPreparationPath(root,active,sources);
    Require(MoveFileExW(pending.c_str(),active.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"Cannot activate prepared generation");
    return {directory,key,miss,false,receipt};
}
}
