#include "iso_read_ahead.h"

#include <algorithm>
#include <cstring>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace cave::ps2 {
namespace {
intptr_t OpenSource(std::FILE* image) {
#ifdef _WIN32
    const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(image)));
    return reinterpret_cast<intptr_t>(ReOpenFile(handle, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_FLAG_SEQUENTIAL_SCAN));
#else
    return dup(fileno(image));
#endif
}
void CloseSource(intptr_t source) {
#ifdef _WIN32
    CloseHandle(reinterpret_cast<HANDLE>(source));
#else
    close(static_cast<int>(source));
#endif
}
size_t ReadBlock(intptr_t source, uint64_t offset, void* dest, size_t length) {
#ifdef _WIN32
    const auto handle = reinterpret_cast<HANDLE>(source);
    LARGE_INTEGER position;
    position.QuadPart = static_cast<LONGLONG>(offset);
    DWORD count = 0;
    if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN) ||
        !ReadFile(handle, dest, static_cast<DWORD>(length), &count, nullptr)) return 0;
    return count;
#else
    const auto count = pread(static_cast<int>(source), dest, length, static_cast<off_t>(offset));
    return count > 0 ? static_cast<size_t>(count) : 0;
#endif
}
}

std::unique_ptr<IsoReadAhead> IsoReadAhead::Open(std::FILE* image, uint64_t size) {
    if (!image || !size) return {};
    const intptr_t source = OpenSource(image);
    if (source == -1) return {};
    try {
        return std::unique_ptr<IsoReadAhead>(new IsoReadAhead(source, size));
    } catch (const std::system_error&) {
        CloseSource(source);
    } catch (const std::bad_alloc&) {
        CloseSource(source);
    }
    return {};
}

IsoReadAhead::IsoReadAhead(intptr_t source, uint64_t size)
    : source(source), size(size), worker(&IsoReadAhead::Run, this) {}

IsoReadAhead::~IsoReadAhead() {
    {
        std::lock_guard lock(mutex);
        stopping = true;
    }
    wake.notify_one();
    worker.join();
    CloseSource(source);
}

void IsoReadAhead::Request(uint64_t start) {
    if (start >= size || start == reading || start == requested) return;
    const auto& block = blocks[(start / blockSize) % blocks.size()];
    if (block.start == start && block.length == std::min<uint64_t>(blockSize, size - start)) return;
    requested = start;
    wake.notify_one();
}

bool IsoReadAhead::TryRead(uint64_t offset, void* destination, size_t bytes) {
    if (!bytes || offset >= size || bytes > size - offset) return false;
    const uint64_t start = offset - offset % blockSize;
    const uint64_t end = offset + bytes;
    const auto within = static_cast<size_t>(offset - start);
    std::lock_guard lock(mutex);
    const bool sequential = previousEnd == offset;
    previousEnd = end;
    auto& block = blocks[(start / blockSize) % blocks.size()];
    const bool hit = block.start == start && within <= block.length && bytes <= block.length - within;
    if (hit) std::memcpy(destination, block.data.data() + within, bytes);
    if (sequential || bytes >= blockSize) {
        // For bulk reads warm the first byte after the request. For small
        // streaming reads prime the current block, then stay one block ahead.
        const uint64_t next = end - end % blockSize;
        if (hit && next == start) Request(start + blockSize);
        else Request(next);
    }
    return hit;
}

void IsoReadAhead::Run() {
    std::unique_lock lock(mutex);
    for (;;) {
        wake.wait(lock, [this] { return stopping || requested != none; });
        if (stopping) return;
        reading = requested;
        requested = none;
        auto& block = blocks[(reading / blockSize) % blocks.size()];
        block.start = none;
        const uint64_t start = reading;
        const auto length = static_cast<size_t>(std::min<uint64_t>(blockSize, size - start));
        lock.unlock();
        const size_t count = ReadBlock(source, start, block.data.data(), length);
        lock.lock();
        block.start = start;
        block.length = count;
        reading = none;
    }
}
}
