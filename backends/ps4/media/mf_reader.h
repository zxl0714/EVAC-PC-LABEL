#pragma once
#include "../content/content_view.h"
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace kt::media {
class Failure : public std::runtime_error {
public:
    Failure(long code, const char* operation) : std::runtime_error(operation), hresult(code) {}
    long hresult;
};
enum class Kind { Video, Audio };
struct Stream {
    Kind kind{};
    unsigned index{}, width{}, height{}, stride{}, rate_numerator{}, rate_denominator{};
    unsigned channels{}, sample_rate{};
};
struct Frame {
    Stream stream{};
    std::int64_t timestamp{}, duration{}; // Media Foundation 100-nanosecond units.
    bool end{}, tick{}, format_changed{};
    std::vector<std::byte> bytes;
};
// All methods and destruction run on the caller's initialized MF MTA thread.
// The source is an already-opened read-only content lease, never a URL.
class Reader {
public:
    explicit Reader(std::shared_ptr<content::ReadOnlyFile> file);
    ~Reader();
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    const std::vector<Stream>& Streams() const;
    std::uint64_t Duration() const;
    Frame Read(unsigned index);
    void Rewind();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
