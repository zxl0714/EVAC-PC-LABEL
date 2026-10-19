#include "atrac9.h"
#include <libatrac9.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>

namespace kt::audio {
namespace {
constexpr std::size_t kInputLimit = 16u * 1024 * 1024, kPcmLimit = 64u * 1024 * 1024;
// LibAtrac9 initialization writes shared transform/Huffman tables. Serialize
// initialization AND decoding so no active decoder reads tables being rebuilt.
std::mutex decoder_mutex;
unsigned U8(std::byte value) { return std::to_integer<unsigned>(value); }
unsigned LE16(std::span<const std::byte> input, unsigned at) { return U8(input[at]) | U8(input[at+1]) << 8; }
unsigned LE32(std::span<const std::byte> input, unsigned at) { return LE16(input,at) | LE16(input,at+2) << 16; }
bool Id(std::span<const std::byte> input, unsigned at, const char* id) { return !std::memcmp(input.data()+at,id,4); }
unsigned DecodedFrames(std::size_t bytes, const Atrac9Config& codec) {
    if (!bytes || bytes > kInputLimit || bytes % codec.superframe_bytes) { throw Atrac9Failure{"ATRAC9 superframe extent"}; }
    const auto frames = bytes / codec.superframe_bytes * codec.SuperframeSamples();
    if (frames > kPcmLimit / (codec.channels * 2)) { throw Atrac9Failure{"ATRAC9 decoded budget"}; }
    return static_cast<unsigned>(frames);
}
}
Atrac9Config InspectAtrac9Config(std::uint32_t config) {
    const unsigned a=config&255, b=(config>>8)&255, c=(config>>16)&255, d=config>>24;
    const auto bits = a<<24 | b<<16 | c<<8 | d;
    const unsigned rate_index=(bits>>20)&15, channel_index=(bits>>17)&7;
    // Validate before entering the library: its ChannelConfigs table has six
    // entries, and this runtime currently mixes only mono/stereo sources.
    if (a!=0xfe || bits&0x10000 || bits&7 || channel_index>2) { throw Atrac9Failure{"ATRAC9 config profile",true}; }
    constexpr unsigned rates[]{11025,12000,16000,22050,24000,32000,44100,48000,44100,48000,64000,88200,96000,128000,176400,192000};
    constexpr unsigned powers[]{6,6,7,7,7,8,8,8,6,6,7,7,7,8,8,8};
    const auto frames=1u<<((bits>>3)&3), frame_bytes=((bits>>5)&2047)+1;
    return {channel_index?2u:1u,rates[rate_index],frame_bytes*frames,1u<<powers[rate_index],frames};
}
Atrac9Wave ParseAtrac9Wave(std::span<const std::byte> file) {
    if (file.size()<12 || file.size()>kInputLimit) { throw Atrac9Failure{"ATRAC9 RIFF size"}; }
    if (!Id(file,0,"RIFF") || !Id(file,8,"WAVE")) { throw Atrac9Failure{"ATRAC9 RIFF signature",true}; }
    if (std::uint64_t{LE32(file,4)}+8!=file.size()) { throw Atrac9Failure{"ATRAC9 RIFF extent"}; }
    std::span<const std::byte> fmt,fact,smpl,data;
    unsigned data_offset{};
    for (std::size_t at=12; at<file.size();) {
        if (file.size()-at<8) { throw Atrac9Failure{"ATRAC9 chunk header"}; }
        const auto size=LE32(file,static_cast<unsigned>(at+4));
        const auto end=at+8+std::uint64_t{size}, padded=end+(size&1);
        if (padded>file.size()) { throw Atrac9Failure{"ATRAC9 chunk extent"}; }
        auto assign=[&](std::span<const std::byte>& slot) {
            if (slot.data()) { throw Atrac9Failure{"ATRAC9 duplicate chunk"}; }
            slot=file.subspan(at+8,size);
        };
        if (Id(file,static_cast<unsigned>(at),"fmt ")) { assign(fmt); }
        else if (Id(file,static_cast<unsigned>(at),"fact")) { assign(fact); }
        else if (Id(file,static_cast<unsigned>(at),"smpl")) { assign(smpl); }
        else if (Id(file,static_cast<unsigned>(at),"data")) { assign(data); data_offset=static_cast<unsigned>(at+8); }
        at=static_cast<std::size_t>(padded);
    }
    constexpr unsigned char guid[]{0xd2,0x42,0xe1,0x47,0xba,0x36,0x8d,0x4d,0x88,0xfc,0x61,0x65,0x4f,0x8c,0x83,0x6c};
    if (fmt.size()<52 || LE16(fmt,0)!=0xfffe || std::memcmp(fmt.data()+24,guid,16)) {
        throw Atrac9Failure{"RIFF codec is not ATRAC9",true};
    }
    if (LE16(fmt,16)!=fmt.size()-18 || LE32(fmt,40)!=1 || LE32(fmt,48)) { throw Atrac9Failure{"ATRAC9 extension profile",true}; }
    Atrac9Wave wave;
    wave.config=LE32(fmt,44); wave.codec=InspectAtrac9Config(wave.config);
    if (LE16(fmt,2)!=wave.codec.channels || LE32(fmt,4)!=wave.codec.rate || LE16(fmt,12)!=wave.codec.superframe_bytes) {
        throw Atrac9Failure{"ATRAC9 format/config disagreement"};
    }
    if (fact.size()!=12 || !data.data()) { throw Atrac9Failure{"ATRAC9 fact/data chunk"}; }
    wave.samples=LE32(fact,0); wave.delay=LE32(fact,8);
    wave.data_offset=data_offset; wave.data_bytes=static_cast<unsigned>(data.size());
    const auto total=DecodedFrames(data.size(),wave.codec);
    const auto end=std::uint64_t{wave.delay}+wave.samples;
    if (!wave.samples || end>total || total-end>=wave.codec.SuperframeSamples()) { throw Atrac9Failure{"ATRAC9 sample extent"}; }
    if (smpl.data()) {
        if (smpl.size()<36) { throw Atrac9Failure{"ATRAC9 smpl extent"}; }
        const auto loops=LE32(smpl,28);
        if (loops>1) { throw Atrac9Failure{"ATRAC9 multiple loops",true}; }
        if (loops) {
            if (smpl.size()<60 || LE32(smpl,40)!=0 || LE32(smpl,52)!=0) { throw Atrac9Failure{"ATRAC9 loop profile",true}; }
            wave.loop_start=LE32(smpl,44);
            const auto inclusive_end=LE32(smpl,48);
            if (inclusive_end==UINT32_MAX || wave.loop_start<wave.delay || wave.loop_start>inclusive_end || std::uint64_t{inclusive_end}+1>end) {
                throw Atrac9Failure{"ATRAC9 loop extent"};
            }
            wave.loop_end=inclusive_end+1; wave.loop_count=LE32(smpl,56); wave.looping=true;
        }
    }
    return wave;
}
std::vector<std::byte> DecodeAtrac9(std::span<const std::byte> data, std::uint32_t config) {
    const auto codec=InspectAtrac9Config(config);
    const auto frames=DecodedFrames(data.size(),codec);
    std::vector<std::byte> output(std::size_t{frames}*codec.channels*2);
    std::lock_guard lock(decoder_mutex);
    std::unique_ptr<void,decltype(&Atrac9ReleaseHandle)> handle(Atrac9GetHandle(),Atrac9ReleaseHandle);
    if (!handle) { throw std::bad_alloc{}; }
    std::array<unsigned char,4> bytes{};
    for (unsigned n=0;n<4;++n) { bytes[n]=static_cast<unsigned char>(config>>(n*8)); }
    if (Atrac9InitDecoder(handle.get(),bytes.data())!=0) { throw Atrac9Failure{"ATRAC9 decoder configuration"}; }
    Atrac9CodecInfo info{};
    if (Atrac9GetCodecInfo(handle.get(),&info)!=0 || info.channels!=static_cast<int>(codec.channels) || info.samplingRate!=static_cast<int>(codec.rate) ||
        info.frameSamples!=static_cast<int>(codec.frame_samples) || info.superframeSize!=static_cast<int>(codec.superframe_bytes) ||
        info.framesInSuperframe!=static_cast<int>(codec.frames_per_superframe)) { throw Atrac9Failure{"ATRAC9 decoder geometry"}; }
    std::size_t written{};
    for (std::size_t sf=0;sf<data.size();sf+=codec.superframe_bytes) {
        unsigned consumed{};
        for (unsigned frame=0;frame<codec.frames_per_superframe;++frame) {
            if (consumed>=codec.superframe_bytes) { throw Atrac9Failure{"ATRAC9 truncated superframe"}; }
            int used{};
            const auto available=codec.superframe_bytes-consumed;
            const auto status=Atrac9Decode(handle.get(),reinterpret_cast<const unsigned char*>(data.data()+sf+consumed),static_cast<int>(available),
                reinterpret_cast<short*>(output.data()+written),&used,0);
            if (status!=0 || used<=0 || static_cast<unsigned>(used)>available) { throw Atrac9Failure{"ATRAC9 invalid frame"}; }
            consumed+=static_cast<unsigned>(used); written+=codec.frame_samples*codec.channels*2;
        }
    }
    return output;
}
bool Atrac9SelfTest() {
    bool passed=true;
    const auto check=[&](const char* name,bool value) {
        passed&=value;
        std::printf("{\"event\":\"atrac9_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n",name,value?"true":"false");
    };
    const auto rejects=[&](auto&& action) { try { action(); return false; } catch (const Atrac9Failure&) { return true; } };
    const auto config=InspectAtrac9Config(0xf00770fe);
    check("mono_superframe_config",config.channels==1 && config.rate==48000 && config.superframe_bytes==256 && config.SuperframeSamples()==1024);
    check("reject_reserved_channel_table_index",rejects([&] { InspectAtrac9Config(0xf0077cfe); }));
    check("reject_bad_config_header",rejects([&] { InspectAtrac9Config(0xf00770ff); }));
    check("reject_config_reserved_bits",rejects([&] { InspectAtrac9Config(0xf10770fe); }));
    check("reject_partial_superframe",rejects([&] { DecodeAtrac9(std::vector<std::byte>(255),0xf00770fe); }));
    check("reject_decoded_memory_budget",rejects([&] { DecodeAtrac9(std::vector<std::byte>(200000),0x000070fe); }));
    try {
        std::vector<std::byte> encoded(64); encoded[4]=std::byte{4}; // First-frame CLC scale factors, all zero spectra.
        const auto pcm=DecodeAtrac9(encoded,0xe00770fe);
        check("authored_zero_spectrum_frame",pcm.size()==512 && std::all_of(pcm.begin(),pcm.end(),[](auto x) { return x==std::byte{}; }));
    } catch (const Atrac9Failure&) { check("authored_zero_spectrum_frame",false); }
    std::vector<std::byte> invalid(64); invalid[0]=std::byte{0x40};
    check("reject_first_frame_reuse",rejects([&] { DecodeAtrac9(invalid,0xe00770fe); }));
    std::vector<std::byte> file(232);
    const auto put=[&](unsigned at,unsigned value) { for (unsigned b=0;b<4;++b) { file[at+b]=static_cast<std::byte>(value>>(b*8)); } };
    std::memcpy(file.data(),"RIFF",4); put(4,224); std::memcpy(file.data()+8,"WAVEfmt ",8); put(16,52);
    file[20]=std::byte{0xfe}; file[21]=std::byte{0xff}; file[22]=std::byte{1}; put(24,48000);
    file[32]=std::byte{64}; file[36]=std::byte{34};
    constexpr unsigned char guid[]{0xd2,0x42,0xe1,0x47,0xba,0x36,0x8d,0x4d,0x88,0xfc,0x61,0x65,0x4f,0x8c,0x83,0x6c};
    std::memcpy(file.data()+44,guid,16); put(60,1); put(64,0xe00770fe);
    std::memcpy(file.data()+72,"fact",4); put(76,12); put(80,200); put(84,16); put(88,16);
    std::memcpy(file.data()+92,"smpl",4); put(96,60); put(128,1); put(144,48); put(148,191);
    std::memcpy(file.data()+160,"data",4); put(164,64);
    file[172]=std::byte{4};
    const auto wave=ParseAtrac9Wave(file);
    check("riff_delay_and_inclusive_loop_end",wave.samples==200 && wave.delay==16 && wave.data_offset==168 && wave.data_bytes==64 && wave.loop_start==48 && wave.loop_end==192 && wave.looping);
    auto broken=file; broken.resize(231);
    check("reject_truncated_riff",rejects([&] { ParseAtrac9Wave(broken); }));
    put(148,300);
    check("reject_loop_beyond_audible_samples",rejects([&] { ParseAtrac9Wave(file); }));
    put(148,191); put(16,0xffffffff);
    check("reject_chunk_overflow",rejects([&] { ParseAtrac9Wave(file); }));
    return passed;
}
}
