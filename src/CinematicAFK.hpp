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
struct Shot {
    float yaw=0.f;
    float distance=0.f;
    float vertical=0.f;
    float pitch=0.f;
};
inline Shot shotAt(int64_t elapsed,float strength){
    const float seconds=std::max(0.f,float(elapsed)*.001f);
    // A continuous orbit with two slower crane/dolly waves. Keeping every
    // component continuous avoids visible cuts when the AFK director changes
    // composition and gives the lyric renderer the exact same camera pose.
    Shot shot;
    shot.yaw=(seconds*.145f+std::sin(seconds*.071f)*.24f)*strength;
    shot.distance=(5.8f+std::sin(seconds*.113f)*1.15f+
                   std::sin(seconds*.031f)*.55f)*strength;
    shot.vertical=(1.75f+std::sin(seconds*.087f)*.72f+
                   std::sin(seconds*.029f)*.38f)*strength;
    shot.pitch=(-.105f+std::sin(seconds*.087f)*.065f)*strength;
    return shot;
}
// Port of the audited reference transform. The old approximation rotated the
// basis but only subtracted m[14]; that moved the lyric camera calculation
// while leaving Bedrock's final view effectively on its original pivot.
inline void applyOrbit(float* m,float yaw,float lateral,float vertical,
                       float pitch,float pivot){
    if(!m)return;
    const float sy=std::sin(yaw),cy=std::cos(yaw);
    for(int row=0;row<3;++row){
        const int i=row*4;
        const float x=m[i],z=m[i+2];
        m[i]=cy*x+sy*z;
        m[i+2]=cy*z-sy*x;
    }
    if(std::abs(pitch)>1.e-5f){
        const float sp=std::sin(pitch),cp=std::cos(pitch);
        for(int row=0;row<3;++row){
            const int i=row*4;
            const float y=m[i+1],z=m[i+2];
            m[i+1]=cp*y-sp*z;
            m[i+2]=sp*y+cp*z;
        }
    }
    const float tx=m[12],ty=m[13],tz=m[14];
    m[12]=cy*tx+sy*tz+sy*pivot;
    m[13]=ty-vertical;
    m[14]=cy*tz-sy*tx+(cy-1.f)*pivot-lateral;
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
    // Target 1.26.66.1 writes its final camera matrix from owner+0xEB0 through
    // owner+0xF2C. The reference mod used +0xEB8 because its older class layout
    // was eight bytes larger. Using that reference offset on this target kept
    // the hook alive but transformed the wrong 16-float window.
    auto* cameraMatrix=plausible(owner)?
        reinterpret_cast<float*>(static_cast<uint8_t*>(owner)+0xEB0):nullptr;
    if(!saneMatrix(stackMatrix))return;
    const bool hasCameraMatrix=saneMatrix(cameraMatrix);
    std::array<float,16> originalStack{},originalCamera{};
    std::memcpy(originalStack.data(),stackMatrix,sizeof(originalStack));
    if(hasCameraMatrix)
        std::memcpy(originalCamera.data(),cameraMatrix,sizeof(originalCamera));
    const auto shot=shotAt(nowMs()-enteredAt.load(),strength);
    // Pull back along local Z while rotating the camera basis. The previous
    // code passed distance as the pivot with zero pull-back, so it could rotate
    // lyric placement without ever moving the visible camera away from the
    // player's eye.
    applyOrbit(stackMatrix,shot.yaw,shot.distance,shot.vertical,shot.pitch,0.f);
    if(hasCameraMatrix)
        applyOrbit(cameraMatrix,shot.yaw,shot.distance,shot.vertical,shot.pitch,0.f);
    if(!saneMatrix(stackMatrix)||(hasCameraMatrix&&!saneMatrix(cameraMatrix))){
        std::memcpy(stackMatrix,originalStack.data(),sizeof(originalStack));
        if(hasCameraMatrix)
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
    const auto shot=shotAt(nowMs()-entered,blend.load());
    constexpr float deg=.01745329251994329577f;
    const float worldYaw=current.yaw*deg+shot.yaw;
    x=current.x+std::sin(worldYaw)*shot.distance;
    y=current.y+1.62f+shot.vertical;
    z=current.z-std::cos(worldYaw)*shot.distance;
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
