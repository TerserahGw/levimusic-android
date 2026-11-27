#pragma once

// Experimental client-side environment probe for LeviMusic immersive audio.
// The local BlockSource is obtained from ClientInstance on Minecraft's update
// thread, so all enclosure rays address the same dimension as the player.
#include <pl/memory/Hook.hpp>
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
using ClientUpdateFn=void(*)(void*,uint32_t,uint32_t);
using GetRegionFn=void*(*)(void*);

inline std::atomic<bool> enabled{false},supported{false},clientHooked{false};
inline std::atomic<int> environment{0};
inline std::atomic<int64_t> lastSample{0},lastWorldProbe{0},lastCaveEvidence{0};
inline std::atomic<int> loggedEnvironment{-1},loggedProbeState{-1};
inline std::atomic<bool> probeUnderwater{false};
inline std::atomic<int> probePlayerX{0},probePlayerY{0},probePlayerZ{0};
inline std::atomic<int64_t> probePlayerSample{0};
inline IsInWaterFn isInWater=nullptr;
inline IsSolidFn isSolid=nullptr;
inline ClientUpdateFn originalClientUpdate=nullptr;
inline GetRegionFn getRegion=nullptr;
inline void* clientTarget=nullptr;
inline uintptr_t imageBase=0;

inline int64_t nowMs(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline bool plausible(uintptr_t value,size_t alignment=alignof(void*)){
    return value>0x10000 && value<0x0001000000000000ULL && value%alignment==0;
}
inline void publish(Environment value,int roofHits=0,int wallHits=0){
    const int next=int(value);environment=next;
    const int previous=loggedEnvironment.exchange(next);
    if(previous!=next)__android_log_print(ANDROID_LOG_INFO,"LeviMusicImmersive",
        "environment=%s roof=%d walls=%d",
        value==Environment::Underwater?"underwater":value==Environment::Cave?"cave":"open",
        roofHits,wallHits);
}
inline bool solidAt(void* source,const BlockPos& pos){
    return isSolid&&isSolid(source,pos);
}
inline void sampleEnclosure(void* source,const BlockPos& head,int64_t now){
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
    constexpr int directions[8][2]={{1,0},{-1,0},{0,1},{0,-1},
        {1,1},{1,-1},{-1,1},{-1,-1}};
    for(const auto& d:directions){
        bool blocked=false;
        for(int radius=1;radius<=20&&!blocked;++radius){
            for(int height:{-1,0,1,2}){
                if(solidAt(source,{head.x+d[0]*radius,head.y+height,
                                  head.z+d[1]*radius})){
                    blocked=true;break;
                }
            }
        }
        if(blocked)++enclosedDirections;
    }
    // A roof plus two obstructed directions is already a room/cave. A dense
    // horizontal enclosure is also accepted even when the ceiling is tall.
    const bool cave=(roofHits>=1&&enclosedDirections>=2)||
                    roofHits>=4||enclosedDirections>=5;
    if(cave){lastCaveEvidence=now;publish(Environment::Cave,roofHits,enclosedDirections);}
    else publish(now-lastCaveEvidence.load()<3500?Environment::Cave:Environment::Open,
                 roofHits,enclosedDirections);
}
inline void sampleClient(void* client){
    if(!enabled||probeUnderwater||!client||!isSolid)return;
    const int64_t now=nowMs();
    const int64_t age=now-probePlayerSample.load(std::memory_order_acquire);
    if(age<0||age>1500)return;
    int64_t previous=lastWorldProbe.load(std::memory_order_relaxed);
    if(now-previous<650||!lastWorldProbe.compare_exchange_strong(
        previous,now,std::memory_order_relaxed))return;
    if(!getRegion){loggedProbeState=0;return;}
    void* source=getRegion(client);
    if(!plausible(reinterpret_cast<uintptr_t>(source))){loggedProbeState=0;return;}
    const BlockPos head{probePlayerX.load(),probePlayerY.load(),probePlayerZ.load()};
    if(loggedProbeState.exchange(1)!=1)__android_log_print(ANDROID_LOG_INFO,
        "LeviMusicImmersive","local BlockSource acquired from ClientInstance");
    sampleEnclosure(source,head,now);
}
inline void clientUpdateHook(void* client,uint32_t renderTick,uint32_t simTick){
    if(originalClientUpdate)originalClientUpdate(client,renderTick,simTick);
    sampleClient(client);
}
inline void prepare(uintptr_t base,bool exact26661){
    supported=false;isInWater=nullptr;isSolid=nullptr;getRegion=nullptr;imageBase=base;
    if(!base||!exact26661)return;
    constexpr uintptr_t waterRva=0xFD8DB60;
    constexpr uintptr_t solidRva=0x10374160;
    constexpr uintptr_t clientUpdateRva=0x9B7B418;
    constexpr uintptr_t getRegionRva=0x9B7F71C;
    constexpr uint8_t waterSignature[]{0x00,0x20,0x00,0x91,0x0c,0xce,0x10,0x14};
    constexpr uint8_t solidSignature[]{0x09,0x00,0x40,0xf9,0x23,0x08,0x40,0xb9,
        0x28,0x08,0x40,0x29,0x24,0xdd,0x40,0xf9,0xe1,0x03,0x08,0x2a,0x80,0x00,0x1f,0xd6};
    constexpr uint8_t clientSignature[]{0xe8,0x0f,0x19,0xfc,0xfd,0x7b,0x01,0xa9,
        0xfc,0x6f,0x02,0xa9,0xfa,0x67,0x03,0xa9,0xf8,0x5f,0x04,0xa9,0xf6,0x57,0x05,0xa9,
        0xf4,0x4f,0x06,0xa9,0xfd,0x43,0x00,0x91,0xff,0x03,0x13,0xd1,0x59,0xd0,0x3b,0xd5};
    constexpr uint8_t regionSignature[]{0xff,0x43,0x01,0xd1,0xfd,0x7b,0x03,0xa9,
        0xf3,0x23,0x00,0xf9,0xfd,0xc3,0x00,0x91,0x53,0xd0,0x3b,0xd5,0xe8,0x03,0x00,0xaa};
    const auto* water=reinterpret_cast<const uint8_t*>(base+waterRva);
    const auto* solid=reinterpret_cast<const uint8_t*>(base+solidRva);
    const auto* client=reinterpret_cast<const uint8_t*>(base+clientUpdateRva);
    const auto* region=reinterpret_cast<const uint8_t*>(base+getRegionRva);
    if(std::memcmp(water,waterSignature,sizeof(waterSignature))||
       std::memcmp(solid,solidSignature,sizeof(solidSignature))||
       std::memcmp(client,clientSignature,sizeof(clientSignature))||
       std::memcmp(region,regionSignature,sizeof(regionSignature))){
        __android_log_print(ANDROID_LOG_WARN,"LeviMusicImmersive",
            "environment access signature mismatch");
        return;
    }
    isInWater=reinterpret_cast<IsInWaterFn>(base+waterRva);
    isSolid=reinterpret_cast<IsSolidFn>(base+solidRva);
    getRegion=reinterpret_cast<GetRegionFn>(base+getRegionRva);
    clientTarget=reinterpret_cast<void*>(base+clientUpdateRva);
    if(!clientHooked.load()){
        if(pl::memory::hook(clientTarget,reinterpret_cast<void*>(&clientUpdateHook),
            reinterpret_cast<void**>(&originalClientUpdate))!=0||!originalClientUpdate){
            __android_log_print(ANDROID_LOG_WARN,"LeviMusicImmersive",
                "ClientInstance update hook failed");
            return;
        }
        clientHooked=true;
    }
    supported=true;
    __android_log_print(ANDROID_LOG_INFO,"LeviMusicImmersive",
        "ClientInstance region probe ready");
}
inline void observePlayer(void* player,float x,float y,float z){
    if(!enabled){publish(Environment::Open);return;}
    const int64_t now=nowMs();
    const BlockPos head{int(std::floor(x)),int(std::floor(y+1.55f)),int(std::floor(z))};
    probePlayerX=head.x;probePlayerY=head.y;probePlayerZ=head.z;
    probePlayerSample.store(now,std::memory_order_release);
    int64_t previous=lastSample.load(std::memory_order_relaxed);
    if(now-previous<600||!lastSample.compare_exchange_strong(
       previous,now,std::memory_order_relaxed))return;
    if(!player||!supported||!isInWater||!isSolid){publish(Environment::Open);return;}
    if(isInWater(player)){
        probeUnderwater=true;publish(Environment::Underwater);return;
    }
    probeUnderwater=false;
    if(now-lastWorldProbe.load()>2200){
        if(loggedProbeState.exchange(0)!=0)__android_log_print(ANDROID_LOG_WARN,
            "LeviMusicImmersive","waiting for local ClientInstance region");
        publish(now-lastCaveEvidence.load()<3500?Environment::Cave:Environment::Open);
    }
}
inline void request(bool value){
    enabled=value;
    if(!value){
        environment=0;lastSample=0;lastWorldProbe=0;lastCaveEvidence=0;
        loggedEnvironment=-1;probeUnderwater=false;probePlayerSample=0;
        loggedProbeState=-1;
    }
}
inline Environment current(){return static_cast<Environment>(environment.load());}
inline const char* name(Environment value){
    return value==Environment::Underwater?"underwater":value==Environment::Cave?"cave":"open";
}
inline void unload(){
    request(false);
    if(clientHooked&&clientTarget)
        pl::memory::unhook(clientTarget,reinterpret_cast<void*>(&clientUpdateHook));
    clientHooked=false;clientTarget=nullptr;originalClientUpdate=nullptr;
    isSolid=nullptr;isInWater=nullptr;getRegion=nullptr;supported=false;
}
} // namespace lm_immersive
