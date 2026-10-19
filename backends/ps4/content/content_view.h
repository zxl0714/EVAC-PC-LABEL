#pragma once
#include "sfo.h"
#include "read_patches.h"
#include <array>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace kt::content {
class InvalidPath : public std::runtime_error { public: using std::runtime_error::runtime_error; };
class NotDirectory : public std::runtime_error { public: using std::runtime_error::runtime_error; };
struct NodeInfo {
    bool directory;
    std::uint32_t device, inode, links;
    std::uint64_t size, allocation, access_time, modification_time, change_time, creation_time;
};
class ReadOnlyFile {
public:
    ReadOnlyFile(void* handle, const NodeInfo& info) : handle_(handle), info_(info) {}
    ~ReadOnlyFile();
    ReadOnlyFile(const ReadOnlyFile&) = delete;
    ReadOnlyFile& operator=(const ReadOnlyFile&) = delete;
    std::uint64_t Size() const { return info_.size; }
    const NodeInfo& Info() const { return info_; }
    std::size_t ReadAt(std::uint64_t offset, std::span<std::byte> output);
    void InstallReadPatches(std::shared_ptr<const ReadPatchSet> patches, std::string_view relative);
    void Hold(std::shared_ptr<void> lease) { lease_ = std::move(lease); }
private:
    void* handle_;
    NodeInfo info_;
    std::mutex mutex_;
    std::shared_ptr<void> lease_;
    std::vector<ReadPatch> read_patches_;
    std::uint32_t reported_patches_{};
};
enum class AddonStatus : std::uint32_t { NoExtraData = 0, Installed = 4 };
struct Addon {
    std::string label;
    std::filesystem::path root;
    AddonStatus status = AddonStatus::Installed;
    std::array<std::byte, 16> capability{};
};
class ContentView {
public:
    ContentView(const std::filesystem::path& app, const std::filesystem::path& patch = {}, const std::filesystem::path& addons = {},
                const std::filesystem::path& read_patches = {});
    const Sfo& Metadata() const { return metadata_; }
    const std::string& TitleId() const { return title_; }
    const std::string& ContentId() const { return content_; }
    const std::vector<Addon>& Addons() const { return addons_; }
    std::size_t ReadPatchCount() const { return read_patches_ ? read_patches_->entries.size() : 0; }
    std::unique_ptr<ReadOnlyFile> OpenApp0(std::string_view relative, bool allow_directory = false) const;
    std::unique_ptr<ReadOnlyFile> OpenAddon(std::size_t index, std::string_view relative, bool allow_directory = false) const;
    bool EntitlementKey(std::size_t index, std::array<std::byte, 16>& output) const;
private:
    std::filesystem::path app_, patch_;
    Sfo metadata_;
    std::string title_, content_;
    std::vector<Addon> addons_;
    std::shared_ptr<const ReadPatchSet> read_patches_;
};
}
