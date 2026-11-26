// Included by both the SDK MPEG stub and the R-Type Final FFmpeg implementation.
namespace ps2_stubs {
namespace {
using cave::ps2::StateReader;
using cave::ps2::StateWriter;

template<class A>
void ArchivePlayback(A& a, MpegPlaybackState& p) {
    a.Fields(p.picturesServed, p.width, p.height, p.decodeMode, p.imageBufferAddr,
             p.sawInput, p.sawSequenceEnd, p.streamEnded, p.decoderFailed, p.cdStreamGeneration,
             p.waitingForVideoSequenceHeader, p.frameRateCode, p.frameRateExtensionN,
             p.frameRateExtensionD, p.hasFrameRateExtension, p.pictureIntervalQ32,
             p.nextPictureTickQ32, p.presentationEndTickQ32, p.firstPresentedPts90k,
             p.ptsPresentationBaseTickQ32);
#if PS2X_HAS_FFMPEG
    a.Field(p.sawProgramEnd);
#endif
    a.Blob(p.videoSequenceSyncBuffer);
    a.Blob(p.videoTimingScanBuffer);
    a.Blob(p.pssBuffer);
    a.Items(p.pssGuestAddrs, 32u << 20, 4, [&](auto& address) { a.Field(address); });
    if (p.pssGuestAddrs.size() != p.pssBuffer.size())
        throw std::invalid_argument("Inconsistent MPEG demux addresses");
    a.Items(p.decodedFrames, 1024, 24, [&](auto& f) {
        a.Fields(f.width, f.height, f.repeatPict, f.pts90k);
        a.Blob(f.rgba);
        if (f.width <= 0 || f.height <= 0 || f.width > 4096 || f.height > 4096 ||
            uint64_t(f.width) * f.height * 4 != f.rgba.size())
            throw std::invalid_argument("Invalid MPEG frame size");
    });
#if PS2X_HAS_FFMPEG
    bool hasDecoder = bool(p.decoder);
    a.Field(hasDecoder);
    if (hasDecoder) {
        if constexpr (std::is_same_v<A, StateWriter>) {
            p.decoder->writeState(a);
        } else {
            p.decoder = std::make_unique<MpegFfmpegDecoder>();
            p.decoder->readState(a);
        }
    }
#endif
}

template<class A, class Map, class Visit>
void ArchiveMovieObjects(A& a, Map& objects, Visit visit) {
    if constexpr (std::is_same_v<A, StateWriter>) {
        a.Field(static_cast<uint32_t>(objects.size()));
        for (auto& [key, value] : objects) { a.Field(key); visit(value); }
    } else {
        const auto count = a.Count(65536, 8);
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t key{};
            a.Field(key);
            auto [it, fresh] = objects.try_emplace(key);
            if (!fresh) throw std::invalid_argument("Duplicate MPEG object");
            visit(it->second);
        }
    }
}

template<class A>
void ArchiveMovie(A& a, MpegStubState& s) {
    a.Fields(s.initialized, s.nextCallbackHandle, s.cdStreamGeneration,
             s.cdStreamBytesProduced, s.cdStreamBytesDemuxed,
             s.cdStreamEofPending, s.currentCdStreamEofSeen);
    ArchiveMovieObjects(a, s.callbacksByMpeg, [&](auto& callbacks) {
        a.Items(callbacks, 65536, 21, [&](auto& c) {
            a.Fields(c.type, c.streamId, c.func, c.data, c.handle, c.stream);
        });
    });
    ArchiveMovieObjects(a, s.playbackByMpeg, [&](auto& playback) { ArchivePlayback(a, playback); });
}
}

std::vector<uint8_t> saveMpegState() {
    std::lock_guard lock(g_mpeg_stub_mutex);
    std::vector<uint8_t> bytes;
    StateWriter writer(bytes);
    writer.Field(uint32_t{1});
    ArchiveMovie(writer, g_mpeg_stub_state);
    return bytes;
}

void loadMpegState(std::span<const uint8_t> bytes) {
    MpegStubState restored;
    StateReader reader(bytes);
    uint32_t version{};
    reader.Field(version);
    if (version != 1) throw std::invalid_argument("Unsupported MPEG state");
    ArchiveMovie(reader, restored);
    reader.Finish();
    std::lock_guard lock(g_mpeg_stub_mutex);
    g_mpeg_stub_state = std::move(restored);
}
}
