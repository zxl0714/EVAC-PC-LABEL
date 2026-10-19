#pragma once
#include "../content/content_view.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace kt::save {
enum class Error { Invalid, NotFound, Exists, Busy, NotMounted, NoSpace, Corrupt, Io, ReadOnly, BadFile, Directory, NotDirectory, Permission };
struct Failure : std::runtime_error {
    Error error;
    std::uint32_t native_error{};
    Failure(Error code, const char* message, std::uint32_t host_error = 0)
        : std::runtime_error(message), error(code), native_error(host_error) {}
};
struct Params { std::string title, subtitle, detail; std::uint32_t user{}; std::int64_t modified{}; };
struct Entry { std::string name; Params params; std::uint64_t blocks{}, free_blocks{}; };
enum class SortKey { Name, User, Blocks, Modified, Free = 5 };
struct OpenOptions { bool read{}, write{}, create{}, truncate{}, exclusive{}, append{}, sync{}, directory{}; };
class Store;
class Mount {
    struct Impl; std::unique_ptr<Impl> impl_;
    explicit Mount(std::unique_ptr<Impl> impl);
    friend class Store; friend class File;
public:
    ~Mount();
    bool ReadOnly() const;
    const std::string& Name() const;
};
class File {
    struct Impl; std::unique_ptr<Impl> impl_;
    explicit File(std::unique_ptr<Impl> impl);
    friend class Store;
public:
    ~File();
    kt::content::NodeInfo Info() const;
    std::size_t ReadAt(std::uint64_t offset, std::span<std::byte> output);
    std::size_t WriteAt(std::uint64_t offset, std::span<const std::byte> input, std::uint64_t& end);
    void Flush();
    bool ReadOnlyMount() const;
};
// Logical save files and standard PSF metadata, not an encrypted console image.
// The caller selects its dedicated user/title root, outside original content.
class Store {
    struct Impl; std::shared_ptr<Impl> impl_;
    friend class Mount; friend class File;
public:
    // Checkpoints are for authored interruption fixtures, never configured by
    // guest data or ordinary game options. The production provider omits it.
    explicit Store(const std::filesystem::path& root,
                   std::function<void(std::string_view)> checkpoint = {});
    static void CreateRoot(const std::filesystem::path& root);
    bool HasMounts() const;
    std::shared_ptr<Mount> MountSlot(std::string_view name, bool writable, bool create, bool exclusive,
                                   std::uint64_t blocks, bool& created);
    void Unmount(const std::shared_ptr<Mount>& mount);
    std::vector<Entry> Search(std::string_view pattern, SortKey key, bool descending) const;
    Entry Describe(const std::shared_ptr<Mount>& mount) const;
    void SetParams(const std::shared_ptr<Mount>& mount, const Params& params);
    void SaveIcon(const std::shared_ptr<Mount>& mount, std::span<const std::byte> bytes);
    std::unique_ptr<File> Open(const std::shared_ptr<Mount>& mount, std::string_view relative, OpenOptions options);
    // A removed slot is renamed to .trash; no recursive deletion is performed.
    void Delete(std::string_view name);
};
} // namespace kt::save
