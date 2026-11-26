#pragma once

// Experimental client-side environment probe for LeviMusic immersive audio.
// Exact-build ECS and BlockSource queries are kept fail-closed.
#include <android/log.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace lm_immersive {
enum class Environment : int { Open=0, Cave=1, Underwater=2 };
struct BlockPos { int x=0,y=0,z=0; };
using IsInWaterFn=bool(*)(void*);
using IsSolidFn=bool(*)(void*,const BlockPos&);

inline std::atomic<bool> enabled{false};
inline std::atomic<bool> supported{false};
inline std::atomic<int> environment{0};
inline std::atomic<int64_t> lastSample{0};
inline IsInWaterFn isInWater=nullptr;
inline IsSolidFn isSolid=nullptr;

inline int64_t nowMs(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline bool plausible(uintptr_t value,size_t alignment=alignof(void*)){
    return value>0x10000 && value<0x0001000000000000ULL && value%alignment==0;
}
template<class T> inline T read(uintptr_t base,size_t offset){
    T value{};std::memcpy(&value,reinterpret_cast<const void*>(base+offset),sizeof(value));return value;
}
inline void prepare(uintptr_t base,bool exact26661){
    supported=false;isInWater=nullptr;isSolid=nullptr;
    if(!base||!exact26661)return;
    constexpr uintptr_t waterRva=0xFD8DB60;
    constexpr uintptr_t solidRva=0x10374160;
    constexpr uint8_t waterSignature[]{0x00,0x20,0x00,0x91,0x0c,0xce,0x10,0x14};
    constexpr uint8_t solidSignature[]{0x09,0x00,0x40,0xf9,0x23,0x08,0x40,0xb9,
        0x28,0x08,0x40,0x29,0x24,0xdd,0x40,0xf9,0xe1,0x03,0x08,0x2a,0x80,0x00,0x1f,0xd6};
    const auto* water=reinterpret_cast<const uint8_t*>(base+waterRva);
    const auto* solid=reinterpret_cast<const uint8_t*>(base+solidRva);
    if(std::memcmp(water,waterSignature,sizeof(waterSignature))||
       std::memcmp(solid,solidSignature,sizeof(solidSignature))){
        __android_log_print(ANDROID_LOG_WARN,"LeviMusicImmersive","environment query signature mismatch");
        return;
    }
    isInWater=reinterpret_cast<IsInWaterFn>(base+waterRva);
    isSolid=reinterpret_cast<IsSolidFn>(base+solidRva);
    supported=true;
    __android_log_print(ANDROID_LOG_INFO,"LeviMusicImmersive","environment queries ready for Minecraft 1.26.66.1");
}
inline bool solidAt(void* region,const BlockPos& pos){
    return isSolid&&isSolid(region,pos);
}
inline void observePlayer(void* player,float x,float y,float z){
    if(!enabled.load(std::memory_order_relaxed)){environment=0;return;}
    const int64_t now=nowMs();
    int64_t previous=lastSample.load(std::memory_order_relaxed);
    if(now-previous<600||!lastSample.compare_exchange_strong(
        previous,now,std::memory_order_relaxed))return;
    if(!player||!supported||!isInWater||!isSolid){environment=0;return;}
    // This exact-build wrapper asks the Actor ECS for its in-water flag. The
    // previous implementation guessed a Block name layout and therefore never
    // returned "water" on 1.26.66.1.
    if(isInWater(player)){environment=int(Environment::Underwater);return;}
    const auto actor=reinterpret_cast<uintptr_t>(player);
    const auto dimension=read<uintptr_t>(actor,0x1C0);
    if(!plausible(dimension)){environment=0;return;}
    const auto region=read<uintptr_t>(dimension,0xD0);
    if(!plausible(region)){environment=0;return;}
    const BlockPos head{int(std::floor(x)),int(std::floor(y+1.55f)),int(std::floor(z))};
    auto* source=reinterpret_cast<void*>(region);
    bool roof=false;
    constexpr int roofOffsets[5][2]={{0,0},{1,0},{-1,0},{0,1},{0,-1}};
    for(int height=1;height<=5&&!roof;++height)
        for(const auto& d:roofOffsets)
            if(solidAt(source,{head.x+d[0],head.y+height,head.z+d[1]})){roof=true;break;}
    int enclosedDirections=0;
    constexpr int directions[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    for(const auto& d:directions){
        bool blocked=false;
        for(int radius=1;radius<=4&&!blocked;++radius){
            for(int height:{0,2}){
                if(solidAt(source,{head.x+d[0]*radius,head.y+height,head.z+d[1]*radius})){
                    blocked=true;break;
                }
            }
        }
        if(blocked)++enclosedDirections;
    }
    // Also covers enclosed buildings, matching the intended environmental echo.
    environment=((roof&&enclosedDirections>=3)||enclosedDirections>=6)?
        int(Environment::Cave):int(Environment::Open);
}
inline void request(bool value){enabled=value;if(!value)environment=0;}
inline Environment current(){return static_cast<Environment>(environment.load());}
inline const char* name(Environment value){
    return value==Environment::Underwater?"underwater":value==Environment::Cave?"cave":"open";
}
} // namespace lm_immersive
