#pragma once

// Experimental client-side environment probe for LeviMusic immersive audio.
// Block layout/signatures are kept fail-closed and are only resolved once.
#include <pl/memory/Signature.hpp>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace lm_immersive {
enum class Environment : int { Open=0, Cave=1, Underwater=2 };
struct BlockPos { int x=0,y=0,z=0; };
using GetBlockFn=void*(*)(void*,const BlockPos&);
using BrightnessFn=float(*)(void*,const BlockPos&);

inline std::atomic<bool> enabled{false};
inline std::atomic<int> environment{0};
inline std::atomic<int64_t> lastSample{0};
inline GetBlockFn getBlock=nullptr;
inline BrightnessFn getBrightness=nullptr;

inline int64_t nowMs(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline bool plausible(uintptr_t value,size_t alignment=alignof(void*)){
    return value>0x10000 && value<0x0001000000000000ULL && value%alignment==0;
}
template<class T> inline T read(uintptr_t base,size_t offset){
    return *reinterpret_cast<const T*>(base+offset);
}
inline void prepare(){
    if(getBlock)return;
    getBlock=reinterpret_cast<GetBlockFn>(pl::memory::resolveSignature(
        "FF 03 01 D1 FD 7B 01 A9 F6 57 02 A9 F4 4F 03 A9 FD 43 00 91 56 D0 3B D5 C8 16 40 F9 E8 07 00 F9 28 04 40 B9 09 84 C0 79 1F 01 09 6B ? ? ? ? 09 80 C0 79 F3 03 00 AA 1F 01 09 6B ? ? ? ?",
        "libminecraftpe.so"));
    getBrightness=reinterpret_cast<BrightnessFn>(pl::memory::resolveSignature(
        "FD 7B BD A9 F5 0B 00 F9 F4 4F 02 A9 FD 03 00 91 F3 03 00 AA 00 1C 40 F9 F4 03 01 AA 08 00 40 F9 08 A9 40 F9 00 01 3F D6 68 1E 40 F9 F5 03 00 AA E0 03 08 AA ? ? ? ? 02 1C 40 92 E1 03 14 AA",
        "libminecraftpe.so"));
}
inline std::string_view blockName(void* block){
    if(!block)return {};
    const auto raw=reinterpret_cast<uintptr_t>(block);
    const auto type=read<uintptr_t>(raw,0x68);
    if(!plausible(type))return {};
    const auto text=type+0x88+0x40+0x8;
    if(!plausible(text))return {};
    const auto* name=reinterpret_cast<const std::string*>(text);
    if(name->size()>128)return {};
    return *name;
}
inline bool air(std::string_view value){
    return value.empty()||value=="minecraft:air"||value=="minecraft:cave_air"||
           value=="minecraft:void_air";
}
inline void observePlayer(void* player,float x,float y,float z){
    if(!enabled.load(std::memory_order_relaxed)){environment=0;return;}
    const int64_t now=nowMs();
    int64_t previous=lastSample.load(std::memory_order_relaxed);
    if(now-previous<600||!lastSample.compare_exchange_strong(
        previous,now,std::memory_order_relaxed))return;
    if(!player||!getBlock){environment=0;return;}
    const auto actor=reinterpret_cast<uintptr_t>(player);
    const auto dimension=read<uintptr_t>(actor,0x1C0);
    if(!plausible(dimension)){environment=0;return;}
    const auto region=read<uintptr_t>(dimension,0xD0);
    if(!plausible(region)){environment=0;return;}
    const BlockPos head{int(std::floor(x)),int(std::floor(y+1.55f)),int(std::floor(z))};
    const auto headName=blockName(getBlock(reinterpret_cast<void*>(region),head));
    if(headName.find("water")!=std::string_view::npos){environment=int(Environment::Underwater);return;}
    int solid=0;
    constexpr int offsets[6][3]={{1,0,0},{-1,0,0},{0,0,1},{0,0,-1},{0,1,0},{0,-1,0}};
    for(const auto& d:offsets){
        BlockPos at{head.x+d[0],head.y+d[1],head.z+d[2]};
        if(!air(blockName(getBlock(reinterpret_cast<void*>(region),at))))++solid;
    }
    float light=15.f;
    if(getBrightness){
        light=getBrightness(reinterpret_cast<void*>(region),head);
        if(!std::isfinite(light)||light<0||light>100)light=15.f;
    }
    environment=(solid>=3&&light<8.f)?int(Environment::Cave):int(Environment::Open);
}
inline void request(bool value){enabled=value;if(!value)environment=0;}
inline Environment current(){return static_cast<Environment>(environment.load());}
inline const char* name(Environment value){
    return value==Environment::Underwater?"underwater":value==Environment::Cave?"cave":"open";
}
} // namespace lm_immersive
