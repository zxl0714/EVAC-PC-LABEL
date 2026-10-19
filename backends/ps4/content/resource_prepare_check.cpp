// ROM-free, memory-only differential-test driver. No guest execution or writes.
#include "resource_prepare.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace {
using namespace cave::ps4;
using Json = nlohmann::json;
ResourceBytes Unhex(const std::string& text) {
    if (text.size()%2 || text.size()>8*1024*1024) throw std::runtime_error("Invalid test hex extent");
    auto digit=[](char c) -> unsigned {
        if (c>='0' && c<='9') return static_cast<unsigned>(c-'0');
        if (c>='a' && c<='f') return static_cast<unsigned>(c-'a'+10);
        throw std::runtime_error("Invalid test hex character");
    };
    ResourceBytes result;
    for (std::size_t i=0;i<text.size();i+=2) result.push_back(static_cast<std::uint8_t>((digit(text[i])<<4)|digit(text[i+1])));
    return result;
}
std::string Hex(std::span<const std::uint8_t> bytes) {
    constexpr char digits[]="0123456789abcdef"; std::string result;
    for (auto b:bytes) {result+=digits[b>>4];result+=digits[b&15];} return result;
}
Json Run(const Json& item) {
    const auto op=item.at("op").get<std::string>();
    if (op=="encode") {
        kt::content::ReadPatchSet patches{item.at("title"),item.at("version"),{}};
        for (const auto& entry:item.at("patches")) {
            kt::content::ReadPatch patch{entry.at("path"),entry.at("file_size"),entry.at("offset"),{}, {}};
            if ((entry.at("file_size").is_number_integer() && entry.at("file_size").get<std::int64_t>()<0) ||
                (entry.at("offset").is_number_integer() && entry.at("offset").get<std::int64_t>()<0) ||
                entry.at("file_size").is_boolean() || entry.at("offset").is_boolean()) throw std::runtime_error("Invalid signed test range");
            const auto hash=Unhex(entry.at("expected_sha256")); const auto replacement=Unhex(entry.at("replacement"));
            if (hash.size()!=32) throw std::runtime_error("Invalid test hash length");
            std::copy(std::as_bytes(std::span(hash)).begin(),std::as_bytes(std::span(hash)).end(),patch.original_sha256.begin());
            const auto bytes=std::as_bytes(std::span(replacement)); patch.replacement.assign(bytes.begin(),bytes.end());
            patches.entries.push_back(std::move(patch));
        }
        return Hex(EncodeReadFixes(patches));
    }
    const auto source=Unhex(item.value("source",std::string{}));
    if (op=="psb") return ParseResourceIndex(source);
    if (op=="fix") {
        const auto kind=item.at("kind").get<std::string>();
        const auto fix=kind=="music" ? ResourceFix::MusicAutosave : kind=="batch" ? ResourceFix::ReplayBatch : ResourceFix::LocalRoundGate;
        if (kind!="music" && kind!="batch" && kind!="offline") throw std::runtime_error("Unknown script test kind");
        return Hex(FixResourceScript(source,fix));
    }
    const auto seed=Unhex(item.at("seed")); const auto name=item.at("resource").get<std::string>();
    if (op=="mask") return Hex(ArchiveMask(seed,name));
    if (op=="unwrap") return Hex(UnwrapResource(source,seed,name));
    if (op=="wrap") return Hex(WrapResource(source,seed,name,item.at("extent")));
    throw std::runtime_error("Unknown resource check operation");
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if (argc!=2 || std::filesystem::file_size(argv[1])>16*1024*1024) throw std::runtime_error("Expected bounded test request JSON");
        std::ifstream source(std::filesystem::path(argv[1]),std::ios::binary); const auto request=Json::parse(source);
        if (!request.is_array() || request.size()>200) throw std::runtime_error("Invalid test request count");
        auto results=Json::array();
        for (const auto& item:request) {
            Json result{{"name",item.at("name")}};
            try {result["value"]=Run(item);result["valid"]=true;}
            catch (const std::exception& error) {result["valid"]=false;result["error"]=error.what();}
            results.push_back(std::move(result));
        }
        std::puts(Json{{"results",std::move(results)},{"guestExecution",false}}.dump().c_str()); return 0;
    } catch (const std::exception& error) {std::fprintf(stderr,"Resource check: %s\n",error.what());return 2;}
}
