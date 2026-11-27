#pragma once

// Experimental client-side environment probe for LeviMusic immersive audio.
// Exact-build ECS and BlockSource queries are kept fail-closed.
#include <pl/memory/Hook.hpp>
#include <android/log.h>
#include <array>
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
inline std::atomic<int64_t> lastWorldProbe{0};
inline std::atomic<int64_t> lastCaveEvidence{0};
inline std::atomic<int> loggedEnvironment{-1};
inline std::atomic<bool> probeUnderwater{false};
inline IsInWaterFn isInWater=nullptr;
inline IsSolidFn isSolid=nullptr;
inline IsSolidFn originalSolid=nullptr;
inline void* solidTarget=nullptr;
inline std::atomic<bool> solidHooked{false};
inline std::atomic<int> probePlayerX{0},probePlayerY{0},probePlayerZ{0};
inline std::atomic<int64_t> probePlayerSample{0};
inline std::atomic<int> loggedProbeState{-1};

inline int64_t nowMs(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline bool plausible(uintptr_t value,size_t alignment=alignof(void*)){
    return value>0x10000 && value<0x0001000000000000ULL && value%alignment==0;
}
inline void publish(Environment value,int roofHits=0,int wallHits=0){
    const int next=int(value);
    environment=next;
    const int previous=loggedEnvironment.exchange(next);
    if(previous!=next)
        __android_log_print(ANDROID_LOG_INFO,"LeviMusicImmersive",
            "environment=%s roof=%d walls=%d",
            value==Environment::Underwater?"underwater":value==Environment::Cave?"cave":"open",
            roofHits,wallHits);
}
inline bool solidAtLive(void* source,const BlockPos& pos){
    return originalSolid&&originalSolid(source,pos);
}
inline void sampleEnclosure(void* source,const BlockPos& head,int64_t now){
    constexpr int roofOffsets[13][2]={{0,0},{1,0},{-1,0},{0,1},{0,-1},
        {2,0},{-2,0},{0,2},{0,-2},{3,0},{-3,0},{0,3},{0,-3}};
    int roofHits=0;
    for(const auto& d:roofOffsets){
        bool hit=false;
        for(int height=1;height<=20&&!hit;++height)
            hit=solidAtLive(source,{head.x+d[0],head.y+height,head.z+d[1]});
        if(hit)++roofHits;
    }
    int enclosedDirections=0;
    constexpr int directions[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    for(const auto& d:directions){
        bool blocked=false;
        for(int radius=1;radius<=14&&!blocked;++radius){
            for(int height:{0,1,2,3}){
                if(solidAtLive(source,{head.x+d[0]*radius,head.y+height,head.z+d[1]*radius})){
                    blocked=true;break;
                }
            }
        }
        if(blocked)++enclosedDirections;
    }
    const bool cave=roofHits>=3||(roofHits>=1&&enclosedDirections>=1)||
                    enclosedDirections>=5;
    if(cave){
        lastCaveEvidence=now;
        publish(Environment::Cave,roofHits,enclosedDirections);
        return;
    }
    publish(now-lastCaveEvidence.load()<6000?Environment::Cave:Environment::Open,
            roofHits,enclosedDirections);
}
inline bool solidHook(void* source,const BlockPos& pos){
    const bool result=originalSolid&&originalSolid(source,pos);
    // A BlockSource receiver is only guaranteed to stay alive for the current
    // Minecraft callback. Probe while the callback owns it; never retain this
    // pointer for a later player tick.
    const int64_t now=nowMs();
    const int64_t playerAge=now-probePlayerSample.load(std::memory_order_acquire);
    const BlockPos head{probePlayerX.load(std::memory_order_relaxed),
        probePlayerY.load(std::memory_order_relaxed),
        probePlayerZ.load(std::memory_order_relaxed)};
    const int dx=std::abs(pos.x-head.x);
    const int dy=std::abs(pos.y-head.y);
    const int dz=std::abs(pos.z-head.z);
    if(enabled.load(std::memory_order_relaxed)&&playerAge>=0&&playerAge<2200&&
       !probeUnderwater.load(std::memory_order_relaxed)&&dx<=64&&dy<=64&&dz<=64&&
       plausible(reinterpret_cast<uintptr_t>(source))){
        int64_t previous=lastWorldProbe.load(std::memory_order_relaxed);
        if((now-previous>=600)&&lastWorldProbe.compare_exchange_strong(
            previous,now,std::memory_order_relaxed)){
            if(loggedProbeState.exchange(1)!=1)
                __android_log_print(ANDROID_LOG_INFO,"LeviMusicImmersive",
                    "sampling live BlockSource inside callback distance=%d",dx+dy+dz);
            sampleEnclosure(source,head,now);
        }
    }
    return result;
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
    solidTarget=reinterpret_cast<void*>(base+solidRva);
    if(!solidHooked.load()){
        if(pl::memory::hook(solidTarget,reinterpret_cast<void*>(&solidHook),
            reinterpret_cast<void**>(&originalSolid))!=0||!originalSolid){
            __android_log_print(ANDROID_LOG_WARN,"LeviMusicImmersive",
                "BlockSource capture hook failed");
            return;
        }
        solidHooked=true;
    }
    isSolid=originalSolid;
    supported=true;
    __android_log_print(ANDROID_LOG_INFO,"LeviMusicImmersive",
        "environment queries and BlockSource capture ready for Minecraft 1.26.66.1");
}
inline void observePlayer(void* player,float x,float y,float z){
    if(!enabled.load(std::memory_order_relaxed)){publish(Environment::Open);return;}
    const int64_t now=nowMs();
    const BlockPos head{int(std::floor(x)),int(std::floor(y+1.55f)),int(std::floor(z))};
    probePlayerX.store(head.x,std::memory_order_relaxed);
    probePlayerY.store(head.y,std::memory_order_relaxed);
    probePlayerZ.store(head.z,std::memory_order_relaxed);
    probePlayerSample.store(now,std::memory_order_release);
    int64_t previous=lastSample.load(std::memory_order_relaxed);
    if(now-previous<600||!lastSample.compare_exchange_strong(
        previous,now,std::memory_order_relaxed))return;
    if(!player||!supported||!isInWater||!originalSolid){publish(Environment::Open);return;}
    // This exact-build wrapper asks the Actor ECS for its in-water flag. The
    // previous implementation guessed a Block name layout and therefore never
    // returned "water" on 1.26.66.1.
    if(isInWater(player)){
        probeUnderwater=true;
        publish(Environment::Underwater);
        return;
    }
    probeUnderwater=false;
    // The block callback normally supplies a live receiver within the next
    // frame. Fail open only when no safe in-callback probe has run recently.
    if(now-lastWorldProbe.load()>2200){
        if(loggedProbeState.exchange(0)!=0)
            __android_log_print(ANDROID_LOG_WARN,"LeviMusicImmersive",
                "no live near-player BlockSource callback");
        publish(now-lastCaveEvidence.load()<6000?Environment::Cave:Environment::Open);
    }
}
inline void request(bool value){
    enabled=value;
    if(!value){
        environment=0;lastSample=0;lastWorldProbe=0;lastCaveEvidence=0;
        loggedEnvironment=-1;probeUnderwater=false;
        probePlayerSample=0;loggedProbeState=-1;
    }
}
inline Environment current(){return static_cast<Environment>(environment.load());}
inline const char* name(Environment value){
    return value==Environment::Underwater?"underwater":value==Environment::Cave?"cave":"open";
}
inline void unload(){
    request(false);
    if(solidHooked&&solidTarget)
        pl::memory::unhook(solidTarget,reinterpret_cast<void*>(&solidHook));
    solidHooked=false;solidTarget=nullptr;originalSolid=nullptr;isSolid=nullptr;supported=false;
}
} // namespace lm_immersive
