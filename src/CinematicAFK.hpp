#pragma once

// Experimental Minecraft 1.26.66.1-only cinematic camera. It is deliberately
// opt-in and verifies the exact audited SetupCamera prologue before hooking.
#include <pl/memory/Hook.hpp>
#include <algorithm>
#include <array>
#include <android/log.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string_view>

namespace lm_cinematic {
using SetupCameraFn=void(*)(void*,void*,float);
using MatrixTopDirtyFn=float*(*)(void*);
using CameraDepsFn=void(*)(void*);
struct Pose { float x=0,y=0,z=0,pitch=0,yaw=0;uintptr_t world=0;int64_t sampled=0; };

inline std::atomic<bool> enabled{false},supported{false},hooked{false},playing{false};
inline std::atomic<int> idleDelay{15};
inline std::atomic<int64_t> lastActivity{0},enteredAt{0},cameraAppliedAt{0};
inline std::atomic<float> blend{0.f};
inline std::mutex poseMutex;
inline Pose pose{};
inline SetupCameraFn original=nullptr;
inline MatrixTopDirtyFn matrixTopDirty=nullptr;
inline CameraDepsFn cameraDeps=nullptr;
inline void* target=nullptr;
inline uintptr_t imageBase=0;
inline bool exactBuild=false;

inline int64_t nowMs(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline void noteActivity(){lastActivity=nowMs();enteredAt=0;}
inline void setPlaying(bool value){playing=value;if(!value){enteredAt=0;blend=0.f;}}
inline void observePlayer(float x,float y,float z,float pitch,float yaw,uintptr_t world){
    std::lock_guard lock(poseMutex);pose={x,y,z,pitch,yaw,world,nowMs()};
}
inline bool matches(const uint8_t* at,std::string_view pattern){
    auto hex=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;return -1;};
    size_t index=0;
    while(!pattern.empty()){
        const auto end=pattern.find(' ');auto token=pattern.substr(0,end);
        if(token!="?"&&token!="??"){
            if(token.size()!=2||hex(token[0])<0||hex(token[1])<0||
               at[index]!=(hex(token[0])<<4|hex(token[1])))return false;
        }
        ++index;if(end==std::string_view::npos)return true;pattern.remove_prefix(end+1);
    }
    return false;
}
inline bool plausible(const void* value,size_t alignment=alignof(void*)){
    const auto raw=reinterpret_cast<uintptr_t>(value);
    return raw>0x10000 && raw<0x0001000000000000ULL && raw%alignment==0;
}
inline bool saneMatrix(const float* matrix){
    if(!plausible(matrix,alignof(float)))return false;
    for(size_t i=0;i<16;++i)
        if(!std::isfinite(matrix[i])||std::abs(matrix[i])>1.e7f)return false;
    return true;
}
inline void applyOrbit(float* m,float yaw,float pitch,float distance,float amount){
    if(!m||amount<=0.f)return;
    const float sy=std::sin(yaw*amount),cy=std::cos(yaw*amount);
    for(int row=0;row<3;++row){
        const float a=m[row*4],c=m[row*4+2];
        m[row*4]=cy*a+sy*c;m[row*4+2]=cy*c-sy*a;
    }
    const float sp=std::sin(pitch*amount),cp=std::cos(pitch*amount);
    for(int row=0;row<3;++row){
        const float b=m[row*4+1],c=m[row*4+2];
        m[row*4+1]=cp*b-sp*c;m[row*4+2]=sp*b+cp*c;
    }
    m[14]-=distance*amount;
}
inline bool shouldRun(Pose& out,float& strength,float& angle){
    if(!enabled||!supported||!playing)return false;
    const int64_t now=nowMs();
    {std::lock_guard lock(poseMutex);out=pose;}
    if(!out.world||now-out.sampled>1200)return false;
    const int64_t threshold=int64_t(std::clamp(idleDelay.load(),10,60))*1000;
    if(now-lastActivity.load()<threshold){
        const float previous=blend.load();strength=std::max(0.f,previous-.08f);blend=strength;
        return strength>.001f;
    }
    int64_t entered=enteredAt.load();
    if(!entered){entered=now;enteredAt=entered;}
    strength=std::clamp(float(now-entered)/1500.f,0.f,1.f);blend=strength;
    angle=float(now-entered)*.00018f;
    return true;
}
inline void setupHook(void* owner,void* stack,float partial){
    if(original)original(owner,stack,partial);
    Pose current;float strength=0,angle=0;
    if(!shouldRun(current,strength,angle)||!matrixTopDirty||!cameraDeps||!plausible(stack))return;
    float* stackMatrix=matrixTopDirty(stack);
    auto* cameraMatrix=plausible(owner)?
        reinterpret_cast<float*>(static_cast<uint8_t*>(owner)+0xEB8):nullptr;
    // SetupCamera keeps a second final camera transform in its owner. Updating
    // only the stack matrix makes world lyrics orbit while the visible camera
    // remains unchanged.
    if(!saneMatrix(stackMatrix)||!saneMatrix(cameraMatrix))return;
    std::array<float,16> originalStack{},originalCamera{};
    std::memcpy(originalStack.data(),stackMatrix,sizeof(originalStack));
    std::memcpy(originalCamera.data(),cameraMatrix,sizeof(originalCamera));
    const float shot=std::sin(angle*.43f);
    const float pitch=-.12f+shot*.08f;
    const float distance=5.4f+shot*.9f;
    applyOrbit(stackMatrix,angle,pitch,distance,strength);
    applyOrbit(cameraMatrix,angle,pitch,distance,strength);
    if(!saneMatrix(stackMatrix)||!saneMatrix(cameraMatrix)){
        std::memcpy(stackMatrix,originalStack.data(),sizeof(originalStack));
        std::memcpy(cameraMatrix,originalCamera.data(),sizeof(originalCamera));
        cameraAppliedAt=0;
        return;
    }
    cameraDeps(stack);
    cameraAppliedAt=nowMs();
}
inline void prepare(uintptr_t base,bool exact26661){
    if(hooked||!exact26661||!base){supported=false;return;}
    constexpr auto setupRva=uintptr_t(0xB7CFDC8);
    constexpr auto topRva=uintptr_t(0x11A1A218);
    constexpr auto depsRva=uintptr_t(0x11A1CBCC);
    constexpr std::string_view signature=
        "FF 03 04 D1 EE 3B 00 FD ED 33 08 6D EB 2B 09 6D E9 23 0A 6D FD 7B 0B A9 FA 67 0C A9 F8 5F 0D A9 F6 57 0E A9 F4 4F 0F A9 FD C3 02 91 59 D0 3B D5 F3 03 00 AA E0 03 01 AA 28 17 40 F9 F7 03 01 AA";
    auto* entry=reinterpret_cast<uint8_t*>(base+setupRva);
    if(!matches(entry,signature)){
        supported=false;
        __android_log_print(ANDROID_LOG_WARN,"LeviMusicCinematic","1.26.66.1 camera signature mismatch");
        return;
    }
    matrixTopDirty=reinterpret_cast<MatrixTopDirtyFn>(base+topRva);
    cameraDeps=reinterpret_cast<CameraDepsFn>(base+depsRva);
    target=entry;
    if(pl::memory::hook(target,reinterpret_cast<void*>(&setupHook),
        reinterpret_cast<void**>(&original))!=0){supported=false;return;}
    hooked=true;supported=true;noteActivity();
    __android_log_print(ANDROID_LOG_INFO,"LeviMusicCinematic","camera hook ready for Minecraft 1.26.66.1");
}
inline void discover(uintptr_t base,bool exact26661){
    imageBase=base;exactBuild=exact26661;
    if(enabled&&!hooked)prepare(imageBase,exactBuild);
}
inline void request(bool value,int delay=15){
    idleDelay=std::clamp(delay,10,60);enabled=value;noteActivity();
    if(value&&!hooked&&imageBase)prepare(imageBase,exactBuild);
    if(!value)blend=0.f;
}
inline bool active(){
    return enabled&&supported&&blend.load()>.01f&&nowMs()-cameraAppliedAt.load()<350;
}
inline bool effectiveCamera(float& x,float& y,float& z){
    if(!active())return false;
    Pose current;{std::lock_guard lock(poseMutex);current=pose;}
    const int64_t entered=enteredAt.load();if(!entered)return false;
    const float a=float(nowMs()-entered)*.00018f;
    const float d=5.4f+std::sin(a*.43f)*.9f;
    x=current.x+std::sin(a)*d;y=current.y+2.25f+std::sin(a*.61f)*.55f;z=current.z+std::cos(a)*d;
    return true;
}
inline const char* status(){
    if(!enabled)return "OFF";
    if(!supported)return "UNAVAILABLE";
    return active()?"ACTIVE":"WAITING";
}
inline void shutdown(){enabled=false;playing=false;blend=0.f;enteredAt=0;cameraAppliedAt=0;}
inline void unload(){shutdown();if(hooked&&target)pl::memory::unhook(target,reinterpret_cast<void*>(&setupHook));hooked=false;supported=false;}
} // namespace lm_cinematic
