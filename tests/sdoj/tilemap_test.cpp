// Authored memory-layout check: no game data, game process or device input.
#include "compat/tilemap.h"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks{};
void Check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
}
int main() {
    try {
        constexpr auto bytes=sdoj::compat::TilemapBytes;
        std::array<std::uint8_t,bytes+32> storage;
        storage.fill(0xa5);
        auto map=std::span(storage).subspan(16,bytes);
        Check(!sdoj::compat::InitializeTilemap(map.first(bytes-1)),"reject a partial allocation");
        Check(storage[16]==0xa5 && storage[bytes+15]==0xa5,"rejected extent is untouched");
        Check(sdoj::compat::InitializeTilemap(map),"initialize exactly one complete map");
        for(unsigned i=0;i<storage.size();++i)
            Check(storage[i]==(i>=16 && i<16+bytes?0:0xa5),"allocation bounds and every byte");
        // Native 88121444..AC updates 12 columns x 10 rows of a 64x64 ring.
        // Its preceding column is also visited by the 11x9 emitter window.
        // Model the writes at each possible origin: the border remains empty,
        // while the current rectangle retains its actual contents unchanged.
        for(unsigned origin=0;origin<64;++origin) {
            Check(sdoj::compat::InitializeTilemap(map),"fresh allocation");
            for(unsigned y=0;y<10;++y)for(unsigned x=0;x<12;++x)
                map[(((origin+y)&63)*64+((origin+x)&63))*8]=0x12;
            for(unsigned y=0;y<9;++y) {
                const auto row=(origin+y)&63;
                Check(map[(row*64+((origin+63)&63))*8]==0,"untouched preceding column is empty");
                for(unsigned x=0;x<10;++x)
                    Check(map[(row*64+((origin+x)&63))*8]==0x12,"authored map cells are not filtered");
            }
        }
        std::cout<<checks<<" tilemap initialization checks passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
