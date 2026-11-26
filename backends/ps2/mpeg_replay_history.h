#pragma once
#include "state_archive.h"
#include <algorithm>
#include <deque>
#include <memory>
#include <optional>

namespace cave::ps2 {
// An MPEG-2 decoder owns opaque prediction surfaces and a partially filled
// parser. Replaying the elementary stream rebuilds both. Keep two complete
// groups of pictures before the current group so open GOPs have references.
class MpegReplayHistory {
public:
    void Feed(std::span<const uint8_t> data, int64_t pts, int64_t dts) {
        chunks_.push_back({end_, pts, dts, {data.begin(), data.end()}});
        for (uint8_t byte : data) {
            code_ = (code_ << 8) | byte;
            ++end_;
            if (code_ == 0x000001b3) sequenceStart_ = end_ - 4;
            if (code_ != 0x000001b8 && code_ != 0x00000100) continue;
            const auto start = end_ - 4;
            if (sequenceStart_) {
                sequence_ = CopyRange(*sequenceStart_, start);
                sequenceStart_.reset();
            }
            if (code_ != 0x000001b8 || !sequence_) continue;
            groups_.push_back({start, sequence_});
            if (groups_.size() <= 3) continue;
            groups_.pop_front();
            const auto& first = groups_.front();
            prefix_ = first.sequence;
            while (chunks_.size() > 1 && chunks_[1].start <= first.start) chunks_.pop_front();
            auto& chunk = chunks_.front();
            if (first.start != chunk.start) {
                const auto count = static_cast<size_t>(first.start - chunk.start);
                chunk.bytes.erase(chunk.bytes.begin(), chunk.bytes.begin() + count);
                chunk.start = first.start;
                chunk.pts = chunk.dts = -1;
            }
        }
    }

    void Write(StateWriter& out) const {
        out.Field(static_cast<uint32_t>(chunks_.size() + (prefix_ ? 1 : 0)));
        auto write = [&](int64_t pts, int64_t dts, const std::vector<uint8_t>& bytes) {
            out.Fields(pts, dts);
            out.Blob(bytes);
        };
        if (prefix_) write(-1, -1, *prefix_);
        for (const auto& chunk : chunks_) write(chunk.pts, chunk.dts, chunk.bytes);
    }

private:
    using Sequence = std::shared_ptr<const std::vector<uint8_t>>;
    struct Chunk { uint64_t start; int64_t pts, dts; std::vector<uint8_t> bytes; };
    struct Group { uint64_t start; Sequence sequence; };
    Sequence CopyRange(uint64_t start, uint64_t end) const {
        auto result = std::make_shared<std::vector<uint8_t>>();
        for (const auto& chunk : chunks_) {
            const auto from = std::max(start, chunk.start);
            const auto to = std::min(end, chunk.start + chunk.bytes.size());
            if (to > from) result->insert(result->end(), chunk.bytes.begin() + (from - chunk.start),
                                         chunk.bytes.begin() + (to - chunk.start));
        }
        return result;
    }
    uint32_t code_ = UINT32_MAX;
    uint64_t end_{};
    std::optional<uint64_t> sequenceStart_;
    Sequence sequence_, prefix_;
    std::deque<Group> groups_;
    std::deque<Chunk> chunks_;
};
}
