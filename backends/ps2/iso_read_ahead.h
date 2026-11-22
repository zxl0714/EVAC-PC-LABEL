#pragma once

#include <array>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>

namespace cave::ps2 {

// Speculative I/O for an immutable disc image. A miss uses the caller's
// ordinary read path, even if the worker is delayed on a different seek.
class IsoReadAhead {
public:
    static std::unique_ptr<IsoReadAhead> Open(std::FILE* image, uint64_t size);
    ~IsoReadAhead();
    bool TryRead(uint64_t offset, void* destination, size_t bytes);

private:
    static constexpr size_t blockSize = 256 * 1024;
    static constexpr uint64_t none = UINT64_MAX;
    struct Block {
        uint64_t start = none;
        size_t length = 0;
        std::array<unsigned char, blockSize> data;
    };

    IsoReadAhead(intptr_t source, uint64_t size);
    void Run();
    void Request(uint64_t start);

    intptr_t source;
    uint64_t size;
    std::array<Block, 2> blocks;
    std::mutex mutex;
    std::condition_variable wake;
    uint64_t previousEnd = none;
    uint64_t requested = none;
    uint64_t reading = none;
    bool stopping = false;
    std::thread worker;
};
}
