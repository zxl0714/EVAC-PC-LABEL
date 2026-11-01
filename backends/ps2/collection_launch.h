#pragma once
#include "native_host.h"
#include <filesystem>
#include <memory>

namespace cave::ps2 {
class CollectionLaunch {
public:
    static std::unique_ptr<CollectionLaunch> Parse(int argc,char** argv);
    ~CollectionLaunch();
    const std::filesystem::path& Elf() const;
    const std::filesystem::path& Disc() const;
    const std::filesystem::path& Data() const;
    const std::filesystem::path& Profile() const;
    const std::filesystem::path& FrameTimes() const;
    NativeHostOptions HostOptions() const;
    void Complete(int code);
private:
    class Impl;
    explicit CollectionLaunch(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl;
};
}
