// Authored PCM workload; reuse the existing sound contracts without a game.
#define main RunOriginalSpuContracts
#include "../../tests/ps2_spu_test.cpp"
#undef main
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace {
uint64_t HashWord(uint64_t hash,uint16_t value) {
    hash=(hash^uint8_t(value))*1099511628211ull;
    return (hash^uint8_t(value>>8))*1099511628211ull;
}
uint64_t RegisterHash(SpuDevice& spu) {
    uint64_t hash=14695981039346656037ull;
    for(unsigned core=0;core<2;++core)for(unsigned voice=0;voice<24;++voice) {
        for(unsigned offset=0;offset<16;offset+=2)
            hash=HashWord(hash,spu.Read16(0x1f900000u+core*0x400u+voice*16u+offset));
        for(unsigned offset=0;offset<12;offset+=2)
            hash=HashWord(hash,spu.Read16(0x1f900000u+core*0x400u+0x1c0u+voice*12u+offset));
    }
    return HashWord(hash,spu.Read16(0x1f9007c2));
}
}
int wmain(int argc,wchar_t** argv) {
  try {
    if(argc<2 || argc>3)throw std::invalid_argument("Usage: ps2_spu_bench new-output-directory [blocks]");
    const std::filesystem::path output(argv[1]);
    if(std::filesystem::exists(output))throw std::runtime_error("Choose a fresh PCM output directory");
    std::filesystem::create_directories(output);
    if(RunOriginalSpuContracts(1,nullptr))throw std::runtime_error("Existing SPU contracts failed");
    const unsigned blocks=argc==3?unsigned(std::stoul(std::wstring(argv[2]))):2000u;
    if(blocks<128 || blocks>48000)throw std::invalid_argument("Use 128..48000 audio blocks");
    constexpr unsigned blockFrames=64;
    for(unsigned scenario=0;scenario<8;++scenario) {
        SpuDevice spu;
        const unsigned voices=scenario<5?std::array<unsigned,5>{0,1,8,24,48}[scenario]:1;
        std::vector<uint8_t> sample(16,0x37);sample[0]=4;sample[1]=3;
        for(unsigned core=0;core<2;++core) {
            Write(spu,core,0x19a,0x8020);
            SetAddress(spu,core,0x1a8,0x5000);TransferAll(spu,core,sample,true);
        }
        for(unsigned index=0;index<voices;++index) {
            const auto core=index/24,voice=index%24;
            Write(spu,core,voice*16,0x180);Write(spu,core,voice*16+2,0x180);
            Write(spu,core,voice*16+4,scenario==6?0u:0x1000u);
            Write(spu,core,voice*16+6,0x000f);Write(spu,core,voice*16+8,0x1fc0);
            SetAddress(spu,core,0x1c0+voice*12,0x5000);SetAddress(spu,core,0x1c4+voice*12,0x5000);
            if(scenario!=5)Write(spu,core,0x1a0+(voice/16)*2,uint16_t(1u<<(voice%16)));
        }
        if(scenario==5)for(unsigned core=0;core<2;++core)for(unsigned voice=0;voice<24;++voice)
            Write(spu,core,voice*16+4,0x1000); // stopped, moving decoder: must not skip
        if(scenario==7) {
            Write(spu,0,0x1a4,1);Write(spu,0,0,0x8000);Write(spu,0,2,0x8000);
        }
        spu.Render(std::span<int16_t>{}); // zero-length render must not advance channel state
        std::vector<int16_t> pcm(blocks*blockFrames*2);
        uint64_t wallNs=0,irqHash=14695981039346656037ull;
        for(unsigned block=0;block<blocks;++block) {
            if(scenario==6 && block%127==0)Write(spu,0,0x1a4,1);
            if(scenario==6 && block%127==30)Write(spu,0,0x1a0,1);
            if(scenario==7 && block%97==0)Write(spu,0,4,(block/97)&1u?0x1000:0);
            auto samples=std::span(pcm).subspan(block*blockFrames*2,blockFrames*2);
            const auto begin=std::chrono::steady_clock::now();spu.Render(samples);
            wallNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
            irqHash=HashWord(irqHash,uint16_t(spu.IrqPending()));
        }
        uint64_t hash=14695981039346656037ull;for(auto value:pcm)hash=HashWord(hash,uint16_t(value));
        std::ofstream file(output/("scenario-"+std::to_string(scenario)+".pcm"),std::ios::binary);
        file.write(reinterpret_cast<const char*>(pcm.data()),std::streamsize(pcm.size()*sizeof(int16_t)));
        file.close();if(!file)throw std::runtime_error("Cannot write PCM evidence");
        std::cout<<std::fixed<<std::setprecision(6)<<"{\"scenario\":"<<scenario<<",\"voices\":"<<voices
          <<",\"audio_frames\":"<<blocks*blockFrames<<",\"render_ns\":"<<wallNs
          <<",\"ms_per_audio_second\":"<<double(wallNs)/1e6/(double(blocks*blockFrames)/48000.0)
          <<",\"pcm_hash\":"<<hash<<",\"register_hash\":"<<RegisterHash(spu)<<",\"irq_hash\":"<<irqHash<<"}\n";
    }
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
