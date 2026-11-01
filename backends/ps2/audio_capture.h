#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace cave::ps2::audio_capture {
struct Capture {
    static constexpr size_t MaxSamples=48000u*2u*90u;
    std::filesystem::path directory;
    std::array<std::vector<int16_t>,3> samples;
    std::array<uint64_t,3> written{};
    explicit Capture(std::filesystem::path path):directory(std::move(path)) {
        if(!std::filesystem::create_directory(directory))throw std::runtime_error("Audio capture directory already exists");
        for(auto& values:samples)values.resize(MaxSamples);
    }
};
inline std::unique_ptr<Capture> capture;
inline void Configure(const std::filesystem::path& path) {
    capture=path.empty()?nullptr:std::make_unique<Capture>(path);
}
inline void Append(unsigned core,std::span<const int16_t> values) {
    if(!capture || core>=3)return;
    auto& output=capture->samples[core];
    auto& written=capture->written[core];
    const auto count=std::min(values.size(),Capture::MaxSamples);
    values=values.last(count);
    const auto offset=size_t(written%Capture::MaxSamples);
    const auto first=std::min(count,Capture::MaxSamples-offset);
    std::copy_n(values.begin(),first,output.begin()+offset);
    if(first<count)std::copy(values.begin()+first,values.end(),output.begin());
    written+=count;
}
inline void Save() {
    if(!capture)return;
    const char* names[]{"core0.wav","core1.wav","mixed.wav"};
    for(unsigned core=0;core<3;++core) {
        std::ofstream out(capture->directory/names[core],std::ios::binary);
        const auto count=size_t(std::min<uint64_t>(capture->written[core],Capture::MaxSamples));
        const auto bytes=uint32_t(count*sizeof(int16_t));
        const auto word=[&](uint32_t value){out.write(reinterpret_cast<const char*>(&value),4);};
        const auto half=[&](uint16_t value){out.write(reinterpret_cast<const char*>(&value),2);};
        out.write("RIFF",4);word(36+bytes);out.write("WAVEfmt ",8);word(16);
        half(1);half(2);word(48000);word(48000*4);half(4);half(16);
        out.write("data",4);word(bytes);
        const auto start=capture->written[core]>=Capture::MaxSamples?size_t(capture->written[core]%Capture::MaxSamples):0;
        const auto first=std::min(count,Capture::MaxSamples-start);
        out.write(reinterpret_cast<const char*>(capture->samples[core].data()+start),first*sizeof(int16_t));
        if(first<count)out.write(reinterpret_cast<const char*>(capture->samples[core].data()),(count-first)*sizeof(int16_t));
        if(!out)throw std::runtime_error("Audio capture output failed");
    }
    capture.reset();
}
}
