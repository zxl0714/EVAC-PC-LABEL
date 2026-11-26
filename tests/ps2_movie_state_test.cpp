// Exercise the exact demux/decoder implementation linked into R-Type Final.
// The fixture is 100 frames of FFmpeg testsrc2 at 64x48, encoded with
// mpeg2video -g 8 -bf 2 -q:v 7. Only the first sequence header is kept,
// matching the long interval between headers in the game's opening movie.
#include "rtype_movie.cpp"
#include <fstream>
#include <iterator>

namespace ps2_stubs {
void testMovieState(std::span<const uint8_t> stream) {
    constexpr uint32_t id = 0x1234;
    const size_t split = stream.size() * 2 / 3 + 1;
    auto feed = [&](size_t pos, size_t end, std::vector<MpegDecodedFrame>* output) {
        auto& p = getPlaybackState(id);
        while (pos < end) {
            const size_t amount = std::min<size_t>(127, end - pos);
            feedElementaryStream(p, stream.data() + pos, amount, int64_t(pos * 7), -1);
            pos += amount;
            while (p.decodedFrames.size() > 2) {
                if (output) output->push_back(std::move(p.decodedFrames.front()));
                p.decodedFrames.pop_front();
            }
        }
    };
    auto finish = [&]() {
        std::vector<MpegDecodedFrame> result;
        feed(split, stream.size(), &result);
        auto& p = getPlaybackState(id);
        p.streamEnded = true;
        flushDecoderIfEnded(p);
        for (auto& f : p.decodedFrames) result.push_back(std::move(f));
        return result;
    };
    feed(0, split, nullptr);
    auto& p = getPlaybackState(id);
    if (!p.decoder || p.decodedFrames.size() != 2) throw std::runtime_error("Movie did not decode");
    std::vector<uint8_t> history;
    cave::ps2::StateWriter writer(history);
    p.decoder->writeState(writer);
    if (history.size() >= split) throw std::runtime_error("Decoder kept the whole movie");
    const auto snapshot = saveMpegState();
    const auto expected = finish();
    resetMpegStubState();
    loadMpegState(snapshot);
    const auto actual = finish();
    if (actual.size() < 10 || actual.size() != expected.size())
        throw std::runtime_error("Lost frames after MPEG restore");
    for (size_t i = 0; i < actual.size(); ++i) {
        const auto& a = actual[i];
        const auto& b = expected[i];
        if (a.width != b.width || a.height != b.height || a.pts90k != b.pts90k ||
            a.repeatPict != b.repeatPict || a.rgba != b.rgba)
            throw std::runtime_error("MPEG frame differs after restore at " + std::to_string(i));
    }
    std::cout << actual.size() << " identical resumed frames, decoder history "
              << history.size() << " / " << split << " bytes\n";
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::invalid_argument("Expected MPEG-2 elementary stream");
        std::ifstream input(argv[1], std::ios::binary);
        std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(input), {});
        if (bytes.empty()) throw std::runtime_error("Empty test movie");
        ps2_stubs::testMovieState(bytes);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
