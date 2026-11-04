#include "pcm_queue.h"
#include "sdl_pcm_feed.h"
#include <SDL3/SDL_init.h>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

struct Source {
    cave::ps2::PcmQueue queue;
    size_t produced = 0, missing = 0, dropped = 0;
    void Generate(size_t frames) {
        std::array<int16_t, 1600> stereo;
        for (size_t i = 0; i < frames; ++i, ++produced) {
            const auto sample = int16_t(std::lround(12000 * std::sin(produced * (6.283185307179586 / 48))));
            stereo[2*i] = sample;
            stereo[2*i+1] = -sample;
        }
        dropped += frames - queue.Write({stereo.data(), frames * 2});
    }
    static void Read(void* state, int16_t* pcm, size_t frames) {
        auto& s = *static_cast<Source*>(state);
        size_t late=0;
        auto valid = s.queue.Read({pcm, frames * 2}, &late);
        s.dropped += late;
        s.missing += frames - valid;
        std::fill(pcm + valid * 2, pcm + frames * 2, 0);
    }
};

size_t RunSegment(SDL_AudioStream* stream, Source& source, cave::ps2::SdlPcmFeed& feed,
                  int rate, size_t sourceFrame) {
    const SDL_AudioSpec output{SDL_AUDIO_S16, 2, rate};
    Require(SDL_SetAudioStreamFormat(stream, nullptr, &output), SDL_GetError());
    // The game supplies 800 samples per 60 Hz field. A device can ask for a
    // non-integral number of fields and switch callback sizes while playing.
    constexpr std::array<size_t, 6> periods{256, 441, 512, 1323, 2048, 4096};
    size_t elapsed = 0, crossings = 0;
    int previous = 0, peak = 0;
    for (size_t i = 0; i < 180; ++i) {
        const size_t count = periods[i % periods.size()];
        elapsed += count;
        const auto due = ((sourceFrame + elapsed * 48000 / rate) / 800 + 1) * 800;
        while (source.produced < due) source.Generate(800);
        std::vector<int16_t> pcm(count * 2);
        const int received = SDL_GetAudioStreamData(stream, pcm.data(), int(count * 4));
        if (received != int(count * 4)) {
            std::cerr << "rate=" << rate << " period=" << i << " requested_bytes=" << count*4
                      << " received=" << received << " callbacks=" << feed.callbacks
                      << " largest_source_request=" << feed.largestRequest << " error=" << SDL_GetError() << '\n';
            throw std::runtime_error("Device request was not filled");
        }
        for (size_t n = 0; n < count; ++n) {
            Require(std::abs(int(pcm[2*n]) + pcm[2*n+1]) <= 2, "Stereo channels changed");
            if (elapsed - count + n < 2048) continue;
            if (previous <= 0 && pcm[2*n] > 0) ++crossings;
            previous = pcm[2*n];
            peak = std::max(peak, std::abs(previous));
        }
    }
    const auto frequency = double(crossings) * rate / (elapsed - 2048);
    std::cout << "SDL device=" << rate << " Hz; peak=" << peak << " pitch_hz=" << frequency
              << " largest_source_request=" << feed.largestRequest << " missing=" << source.missing
              << " dropped=" << source.dropped << '\n';
    Require(std::abs(frequency - 1000) < 1 && std::abs(peak - 6000) < 250, "Pitch or volume changed");
    Require(feed.callbacks && !feed.putFailures && !source.dropped && !source.missing,
            "Steady playback lost PCM");
    return sourceFrame + elapsed * 48000 / rate;
}
void Run() {
    // After a stalled guest catches up, the next pull should play recent
    // samples without shortening the device request, including large periods.
    for(const size_t period:{480u,1440u,4459u}) {
        cave::ps2::PcmQueue queue;
        std::vector<int16_t> burst(queue.CapacityFrames*2),output(period*2);
        for(size_t i=0;i<burst.size();++i)burst[i]=int16_t(i/2);
        Require(queue.Write(burst)==queue.CapacityFrames,"Catch-up fixture did not fill");
        size_t late=0;
        Require(queue.Read(output,&late)==period && late==queue.CapacityFrames-period-800 &&
                output.front()==int16_t(late) && output.back()==int16_t(late+period-1),
                "Catch-up left old audio queued or truncated the device period");
    }
    Source source;
    cave::ps2::SdlPcmFeed feed{&source, Source::Read};
    const SDL_AudioSpec input{SDL_AUDIO_S16, 2, 48000}, output{SDL_AUDIO_S16, 2, 44100};
    std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> stream(
        SDL_CreateAudioStream(&input, &output), SDL_DestroyAudioStream);
    Require(stream && SDL_SetAudioStreamGetCallback(stream.get(), feed.Pull, &feed), SDL_GetError());
    Require(SDL_SetAudioStreamGain(stream.get(), 0.5f), SDL_GetError());
    Require(feed.Prime(stream.get()), SDL_GetError());
    size_t position = 0;
    for (const int rate : {44100, 48000, 44100})
        position = RunSegment(stream.get(), source, feed, rate, position);
}
}
int main() {
    if (!SDL_Init(0)) return 1;
    try { Run(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; SDL_Quit(); return 1; }
    SDL_Quit();
}
