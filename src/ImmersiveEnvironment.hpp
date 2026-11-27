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
inline std::atomic<int64_t> lastCaveEvidence{0};
inline std::atomic<int> loggedEnvironment{-1};
inline IsInWaterFn isInWater=nullptr;
inline IsSolidFn isSolid=nullptr;
inline IsSolidFn originalSolid=nullptr;
inline void* solidTarget=nullptr;
inline std::atomic<bool> solidHooked{false};
struct SourceCandidate {
    std::atomic<uintptr_t> source{0};
    std::atomic<int> x{0},y{0},z{0};
    std::atomic<int64_t> sampled{0};
};
inline std::array<SourceCandidate,8> sourceCandidates{};
inline std::atomic<uint32_t> sourceSerial{0};
inline std::atomic<int> probePlayerX{0},probePlayerY{0},probePlayerZ{0};
inline std::atomic<int64_t> probePlayerSample{0};
inline std::atomic<int> loggedSourceState{-1};

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
inline bool solidHook(void* source,const BlockPos& pos){
    const bool result=originalSolid&&originalSolid(source,pos);
    // Do not guess BlockSource from Actor/Dimension offsets. Capture receivers
    // that Minecraft itself passes to this exact virtual-dispatch wrapper.
    const int64_t now=nowMs();
    const int64_t playerAge=now-probePlayerSample.load(std::memory_order_acquire);
    const int dx=std::abs(pos.x-probePlayerX.load(std::memory_order_relaxed));
    const int dy=std::abs(pos.y-probePlayerY.load(std::memory_order_relaxed));
    const int dz=std::abs(pos.z-probePlayerZ.load(std::memory_order_relaxed));
    if(enabled.load(std::memory_order_relaxed)&&playerAge>=0&&playerAge<2200&&
       dx<=64&&dy<=64&&dz<=64&&plausible(reinterpret_cast<uintptr_t>(source))){
        auto& slot=sourceCandidates[sourceSerial.fetch_add(1,std::memory_order_relaxed)%sourceCandidates.size()];
        slot.source.store(reinterpret_cast<uintptr_t>(source),std::memory_order_relaxed);
        slot.x.store(pos.x,std::memory_order_relaxed);
        slot.y.store(pos.y,std::memory_order_relaxed);
        slot.z.store(pos.z,std::memory_order_relaxed);
        slot.sampled.store(now,std::memory_order_release);
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
inline bool solidAt(void* region,const BlockPos& pos){
    return isSolid&&isSolid(region,pos);
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
    if(!player||!supported||!isInWater||!isSolid){publish(Environment::Open);return;}
    // This exact-build wrapper asks the Actor ECS for its in-water flag. The
    // previous implementation guessed a Block name layout and therefore never
    // returned "water" on 1.26.66.1.
    if(isInWater(player)){publish(Environment::Underwater);return;}
    uintptr_t bestSource=0;int bestDistance=INT32_MAX;int64_t bestAge=INT64_MAX;
    for(auto& candidate:sourceCandidates){
        const int64_t sampled=candidate.sampled.load(std::memory_order_acquire);
        const int64_t age=now-sampled;
        if(sampled<=0||age<0||age>2200)continue;
        const int dx=std::abs(candidate.x.load(std::memory_order_relaxed)-head.x);
        const int dy=std::abs(candidate.y.load(std::memory_order_relaxed)-head.y);
        const int dz=std::abs(candidate.z.load(std::memory_order_relaxed)-head.z);
        const int distance=dx+dy+dz;
        if(dx<=96&&dy<=96&&dz<=96&&(distance<bestDistance||
           (distance==bestDistance&&age<bestAge))){
            bestSource=candidate.source.load(std::memory_order_relaxed);
            bestDistance=distance;bestAge=age;
        }
    }
    if(!plausible(bestSource)){
        if(loggedSourceState.exchange(0)!=0)
            __android_log_print(ANDROID_LOG_WARN,"LeviMusicImmersive",
                "no recent BlockSource query near player");
        publish(Environment::Open);
        return;
    }
    if(loggedSourceState.exchange(1)!=1)
        __android_log_print(ANDROID_LOG_INFO,"LeviMusicImmersive",
            "live BlockSource selected distance=%d age=%lld",bestDistance,
            static_cast<long long>(bestAge));
    auto* source=reinterpret_cast<void*>(bestSource);
    constexpr int roofOffsets[13][2]={{0,0},{1,0},{-1,0},{0,1},{0,-1},
        {2,0},{-2,0},{0,2},{0,-2},{3,0},{-3,0},{0,3},{0,-3}};
    int roofHits=0;
    for(const auto& d:roofOffsets){
        bool hit=false;
        for(int height=1;height<=20&&!hit;++height)
            hit=solidAt(source,{head.x+d[0],head.y+height,head.z+d[1]});
        if(hit)++roofHits;
    }
    int enclosedDirections=0;
    constexpr int directions[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    for(const auto& d:directions){
        bool blocked=false;
        for(int radius=1;radius<=14&&!blocked;++radius){
            for(int height:{0,1,2,3}){
                if(solidAt(source,{head.x+d[0]*radius,head.y+height,head.z+d[1]*radius})){
                    blocked=true;break;
                }
            }
        }
        if(blocked)++enclosedDirections;
    }
    // A low/wide ceiling is the strongest room signal. One ceiling ray plus a
    // nearby wall also covers irregular caves; many side walls cover tunnels
    // where the exact vertical ray briefly lands in an opening.
    const bool cave=roofHits>=3||(roofHits>=1&&enclosedDirections>=1)||
                    enclosedDirections>=5;
    if(cave){lastCaveEvidence=now;publish(Environment::Cave,roofHits,enclosedDirections);return;}
    // Block queries can briefly miss at chunk boundaries and while moving.
    // Holding the last enclosed result prevents the Bridge from destroying and
    // rebuilding the reverb chain several times per second.
    publish(now-lastCaveEvidence.load()<6000?Environment::Cave:Environment::Open,
            roofHits,enclosedDirections);
}
inline void request(bool value){
    enabled=value;
    if(!value){
        environment=0;lastCaveEvidence=0;loggedEnvironment=-1;
        probePlayerSample=0;loggedSourceState=-1;
        for(auto& candidate:sourceCandidates){candidate.sampled=0;candidate.source=0;}
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
