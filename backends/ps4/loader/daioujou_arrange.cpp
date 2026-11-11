#include "daioujou_arrange.h"
#include "backends/ps4/content/resource_prepare.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace cave::ps4 {
namespace {
using Json = nlohmann::json;
constexpr std::string_view kOutput = "arrange/";
constexpr std::string_view kTitlePcm = "1100/config/title_pcm.psb.m";
constexpr std::uint64_t kIndexLimit = 4 * 1024 * 1024;
constexpr std::uint64_t kWaveLimit = 64 * 1024 * 1024;

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
        Require(index_.at("id") == "archive" && index_.at("file_info").is_object(),
            "Invalid DaiOuJou archive index");
    }

    ResourceBytes Read(std::string_view name) const {
        const auto& entry = index_.at("file_info").at(std::string(name));
        Require(entry.is_array() && entry.size() == 2, "Invalid DaiOuJou archive entry");
        const auto offset = Unsigned(entry[0]);
        const auto length = Unsigned(entry[1]);
        Require(offset <= body_size_ && length > 0 && length <= body_size_ - offset &&
            length <= kIndexLimit, "DaiOuJou metadata lies outside the archive");
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

void AddPcmDefinitions(ArchiveLayer& layer, const Json& definitions) {
    auto pcm = ParseResourceIndex(layer.Read(kTitlePcm));
    auto& waves = pcm.at("pcm_wav_definition");
    Require(waves.is_array() && waves.size() == 30, "Unexpected DaiOuJou PCM definitions");
    for (const auto& definition : definitions) {
        const auto collision = std::ranges::any_of(waves, [&](const Json& wave) {
            return wave.at("no") == definition.at("no") || wave.at("filename") == definition.at("filename");
        });
        Require(!collision, "X Arrange PCM definition collides with an original track");
        waves.push_back(definition);
    }
    layer.Add(kTitlePcm, WritePlainPsb(pcm, 0), true);
}
}

void AddDaioujouArrangeFiles(std::vector<std::string>& files, bool merged) {
    files.emplace_back(std::string(kOutput) + kBase.index);
    files.emplace_back(std::string(kOutput) + kBase.body);
    if (merged) {
        files.emplace_back(std::string(kOutput) + kUpdate.index);
        files.emplace_back(std::string(kOutput) + kUpdate.body);
    }
}

void PrepareDaioujouArrangeArchive(const fs::path& data, const fs::path& music,
    const fs::path& stage, const Json& tracks, std::span<const std::uint8_t> executable, bool merged) {
    const auto key = ReadArchiveSeed(executable, merged ? 0xdd : 0xde, true);
    auto definitions = Json::array();
    ArchiveLayer base(data, kBase, key);
    std::size_t reserve{};
    for (const auto& track : tracks) reserve += track.at("bytes").get<std::size_t>() + 16 * 1024;
    base.Reserve(reserve);
    for (unsigned i = 0; i < kDaioujouArrangeTrackNames.size(); ++i) {
        const auto filename = std::string(kDaioujouArrangeTrackNames[i]) + ".wav";
        const auto wave = ReadPreparationInput(DataChild(music, filename), kWaveLimit);
        const auto& expected = tracks.at(filename);
        Require(expected.at("bytes") == wave.size() && expected.at("sha256") == PreparationHash(wave),
            "X Arrange music changed during preparation");
        const auto resource = "1100/wav/evac_" + filename;
        base.Add(resource + ".m", wave, false);
        definitions.push_back({{"delay", 0}, {"filename", resource},
            {"no", kDaioujouArrangeFirstPcm + i}, {"stream", false}});
    }
    AddPcmDefinitions(base, definitions);
    base.Write(stage);
    if (merged) {
        ArchiveLayer update(data, kUpdate, key);
        AddPcmDefinitions(update, definitions);
        update.Write(stage);
    }
}
}
