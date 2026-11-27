#pragma once

// Experimental Minecraft 1.26.66.1-only cinematic camera. The audited camera
// ECS position/orientation passes are intercepted after they have populated the
// real CameraComponent used by the renderer.
#include <pl/memory/Hook.hpp>
#include <algorithm>
#include <android/log.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string_view>

namespace lm_cinematic {
using CameraSystemFn=void(*)(void*);
using CameraAdjustFn=void(*)(void*,const void*,const void*);
struct Pose {
    float x=0,y=0,z=0,pitch=0,yaw=0;
    uintptr_t player=0,world=0;
    int64_t sampled=0;
};
struct Shot { float yaw=0,distance=0,vertical=0,pitch=0; };
struct Quaternion { float x=0,y=0,z=0,w=1; };

inline std::atomic<bool> enabled{false},supported{false},hooked{false},playing{false};
inline std::atomic<int> idleDelay{15};
inline std::atomic<int64_t> lastActivity{0},enteredAt{0},cameraAppliedAt{0};
inline std::atomic<int64_t> cameraHookAt{0},lastDiagnostic{0};
inline std::atomic<uint64_t> cameraHookCalls{0},cameraApplyCalls{0},cameraRejectCalls{0};
inline std::atomic<float> blend{0.f};
inline std::mutex poseMutex;
inline Pose pose{};
inline CameraSystemFn originalPositionSystem=nullptr;
inline CameraSystemFn originalOrientationSystem=nullptr;
inline CameraAdjustFn originalAdjust=nullptr;
inline void* positionSystemTarget=nullptr;
inline void* orientationSystemTarget=nullptr;
inline void* adjustTarget=nullptr;
inline uintptr_t imageBase=0;
inline bool exactBuild=false;
inline thread_local int positionSystemDepth=0;
inline thread_local void* frameCameraComponent=nullptr;

inline int64_t nowMs(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline void noteActivity(){lastActivity=nowMs();enteredAt=0;}
inline void setPlaying(bool value){playing=value;}
inline void observePlayer(void* player,float x,float y,float z,float pitch,float yaw,uintptr_t world){
    std::lock_guard lock(poseMutex);
    pose={x,y,z,pitch,yaw,reinterpret_cast<uintptr_t>(player),world,nowMs()};
}
inline bool matches(const uint8_t* at,std::string_view pattern){
    auto hex=[](char c)->int{
        if(c>='0'&&c<='9')return c-'0';
        if(c>='A'&&c<='F')return c-'A'+10;
        if(c>='a'&&c<='f')return c-'a'+10;
        return -1;
    };
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
inline Shot shotAt(int64_t elapsed,float strength){
    const float seconds=std::max(0.f,float(elapsed)*.001f);
    Shot shot;
    shot.yaw=(seconds*.145f+std::sin(seconds*.071f)*.24f)*strength;
    shot.distance=(5.8f+std::sin(seconds*.113f)*1.15f+
                   std::sin(seconds*.031f)*.55f)*strength;
    shot.vertical=(1.75f+std::sin(seconds*.087f)*.72f+
                   std::sin(seconds*.029f)*.38f)*strength;
    shot.pitch=(-.105f+std::sin(seconds*.087f)*.065f)*strength;
    return shot;
}
inline bool shouldRun(Pose& out,float& strength){
    if(!enabled||!supported)return false;
    const int64_t now=nowMs();
    {std::lock_guard lock(poseMutex);out=pose;}
    if(!out.player||!out.world||now-out.sampled>1200)return false;
    const int64_t threshold=int64_t(std::clamp(idleDelay.load(),10,60))*1000;
    if(now-lastActivity.load()<threshold){
        strength=std::max(0.f,blend.load()-.08f);blend=strength;
        return strength>.001f;
    }
    int64_t entered=enteredAt.load();
    if(!entered){entered=now;enteredAt=entered;}
    strength=std::clamp(float(now-entered)/1500.f,0.f,1.f);blend=strength;
    return true;
}
inline bool sane(const Quaternion& q){
    const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return std::isfinite(n)&&n>.45f&&n<1.55f;
}
inline Quaternion normalized(Quaternion q){
    const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if(!std::isfinite(n)||n<.001f)return {};
    q.x/=n;q.y/=n;q.z/=n;q.w/=n;return q;
}
inline Quaternion lookQuaternion(float yaw,float pitch){
    const float cy=std::cos(yaw*.5f),sy=std::sin(yaw*.5f);
    const float cp=std::cos(pitch*.5f),sp=std::sin(pitch*.5f);
    // Minecraft yaw rotates +Z toward -X; positive pitch looks downward.
    return normalized({cy*sp,-sy*cp,sy*sp,cy*cp});
}
inline Quaternion blendQuaternion(Quaternion from,Quaternion to,float amount){
    from=normalized(from);to=normalized(to);
    float dot=from.x*to.x+from.y*to.y+from.z*to.z+from.w*to.w;
    if(dot<0.f){to.x=-to.x;to.y=-to.y;to.z=-to.z;to.w=-to.w;}
    amount=std::clamp(amount,0.f,1.f);
    return normalized({from.x+(to.x-from.x)*amount,
        from.y+(to.y-from.y)*amount,from.z+(to.z-from.z)*amount,
        from.w+(to.w-from.w)*amount});
}
inline bool buildCamera(const Pose& current,float strength,float& x,float& y,float& z,
                        Quaternion& orientation){
    const int64_t entered=enteredAt.load();if(!entered)return false;
    const auto shot=shotAt(nowMs()-entered,strength);
    constexpr float deg=.01745329251994329577f;
    const float orbit=current.yaw*deg+shot.yaw;
    x=current.x+std::sin(orbit)*shot.distance;
    y=current.y+1.62f+shot.vertical;
    z=current.z-std::cos(orbit)*shot.distance;
    const float dx=current.x-x,dy=current.y+1.2f-y,dz=current.z-z;
    const float horizontal=std::sqrt(dx*dx+dz*dz);
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||horizontal<.05f)return false;
    const float yaw=std::atan2(-dx,dz);
    const float pitch=-std::atan2(dy,horizontal)+shot.pitch;
    orientation=lookQuaternion(yaw,pitch);
    return sane(orientation);
}
inline bool applyCamera(void* camera){
    cameraHookAt=nowMs();cameraHookCalls.fetch_add(1,std::memory_order_relaxed);
    Pose current;float strength=0.f;
    if(!shouldRun(current,strength))return false;
    if(!plausible(camera,alignof(float))){cameraRejectCalls.fetch_add(1);return false;}
    // CameraComponent on the supported 1.26.6X layout stores orientation at
    // +0x28 and position immediately after it at +0x38.
    auto* bytes=static_cast<uint8_t*>(camera);
    Quaternion originalOrientation{};
    std::memcpy(&originalOrientation,bytes+0x28,sizeof(originalOrientation));
    if(!sane(originalOrientation)){cameraRejectCalls.fetch_add(1);return false;}
    float x=0,y=0,z=0;Quaternion desired{};
    if(!buildCamera(current,strength,x,y,z,desired)){
        cameraRejectCalls.fetch_add(1);return false;
    }
    const float eased=strength*strength*(3.f-2.f*strength);
    const Quaternion mixed=blendQuaternion(originalOrientation,desired,eased);
    const float position[3]{x,y,z};
    std::memcpy(bytes+0x28,&mixed,sizeof(mixed));
    std::memcpy(bytes+0x38,position,sizeof(position));
    cameraAppliedAt=nowMs();
    const auto count=cameraApplyCalls.fetch_add(1,std::memory_order_relaxed)+1;
    if(count==1)__android_log_print(ANDROID_LOG_INFO,"LeviMusicCinematic",
        "CameraComponent orbit active");
    return true;
}
inline void adjustHook(void* camera,const void* adjusted,const void* delta){
    if(originalAdjust)originalAdjust(camera,adjusted,delta);
    if(positionSystemDepth<=0)return;
    frameCameraComponent=camera;
    applyCamera(camera);
}
inline void positionSystemHook(void* system){
    frameCameraComponent=nullptr;
    ++positionSystemDepth;
    if(originalPositionSystem)originalPositionSystem(system);
    --positionSystemDepth;
}
inline void orientationSystemHook(void* system){
    if(originalOrientationSystem)originalOrientationSystem(system);
    // CameraSetPositionSystem's second view writes the orientation after the
    // adjusted-position pass. Reapply once more so it reaches the renderer.
    if(frameCameraComponent)applyCamera(frameCameraComponent);
    frameCameraComponent=nullptr;
}
inline void prepare(uintptr_t base,bool exact26661){
    if(hooked||!exact26661||!base){supported=hooked.load();return;}
    constexpr auto positionSystemRva=uintptr_t(0x11E65888);
    constexpr auto orientationSystemRva=uintptr_t(0x11E65C24);
    constexpr auto adjustRva=uintptr_t(0x11E9A1F8);
    constexpr std::string_view positionSignature=
        "FF C3 02 D1 E8 23 00 FD FD 7B 05 A9 FC 6F 06 A9 FA 67 07 A9 F8 5F 08 A9";
    constexpr std::string_view orientationSignature=
        "FF C3 02 D1 FD 7B 05 A9 FC 6F 06 A9 FA 67 07 A9 F8 5F 08 A9 F6 57 09 A9";
    constexpr std::string_view adjustSignature=
        "FF 43 01 D1 FD 7B 03 A9 F3 23 00 F9 FD C3 00 91 53 D0 3B D5 00 A0 00 91";
    auto* positionEntry=reinterpret_cast<uint8_t*>(base+positionSystemRva);
    auto* orientationEntry=reinterpret_cast<uint8_t*>(base+orientationSystemRva);
    auto* adjustEntry=reinterpret_cast<uint8_t*>(base+adjustRva);
    if(!matches(positionEntry,positionSignature)||
       !matches(orientationEntry,orientationSignature)||
       !matches(adjustEntry,adjustSignature)){
        supported=false;
        __android_log_print(ANDROID_LOG_WARN,"LeviMusicCinematic",
            "CameraSetPositionSystem signature mismatch");
        return;
    }
    positionSystemTarget=positionEntry;
    orientationSystemTarget=orientationEntry;
    adjustTarget=adjustEntry;
    if(pl::memory::hook(positionSystemTarget,reinterpret_cast<void*>(&positionSystemHook),
        reinterpret_cast<void**>(&originalPositionSystem))!=0||!originalPositionSystem){
        supported=false;return;
    }
    if(pl::memory::hook(adjustTarget,reinterpret_cast<void*>(&adjustHook),
        reinterpret_cast<void**>(&originalAdjust))!=0||!originalAdjust){
        pl::memory::unhook(positionSystemTarget,reinterpret_cast<void*>(&positionSystemHook));
        originalPositionSystem=nullptr;supported=false;return;
    }
    if(pl::memory::hook(orientationSystemTarget,reinterpret_cast<void*>(&orientationSystemHook),
        reinterpret_cast<void**>(&originalOrientationSystem))!=0||!originalOrientationSystem){
        pl::memory::unhook(adjustTarget,reinterpret_cast<void*>(&adjustHook));
        pl::memory::unhook(positionSystemTarget,reinterpret_cast<void*>(&positionSystemHook));
        originalAdjust=nullptr;originalPositionSystem=nullptr;supported=false;return;
    }
    hooked=true;supported=true;noteActivity();
    __android_log_print(ANDROID_LOG_INFO,"LeviMusicCinematic",
        "CameraSetPositionSystem orbit hooks ready");
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
inline void diagnose(){
    if(!enabled.load(std::memory_order_relaxed))return;
    const int64_t now=nowMs();
    int64_t previous=lastDiagnostic.load(std::memory_order_relaxed);
    if(now-previous<5000||!lastDiagnostic.compare_exchange_strong(
       previous,now,std::memory_order_relaxed))return;
    Pose current;{std::lock_guard lock(poseMutex);current=pose;}
    const auto hookAt=cameraHookAt.load(),appliedAt=cameraAppliedAt.load();
    __android_log_print(ANDROID_LOG_INFO,"LeviMusicCinematic",
        "state supported=%d poseAge=%lld idleAge=%lld hookCalls=%llu hookAge=%lld applyCalls=%llu rejects=%llu applyAge=%lld blend=%.2f",
        int(supported.load()),static_cast<long long>(current.sampled?now-current.sampled:-1),
        static_cast<long long>(now-lastActivity.load()),
        static_cast<unsigned long long>(cameraHookCalls.load()),
        static_cast<long long>(hookAt?now-hookAt:-1),
        static_cast<unsigned long long>(cameraApplyCalls.load()),
        static_cast<unsigned long long>(cameraRejectCalls.load()),
        static_cast<long long>(appliedAt?now-appliedAt:-1),double(blend.load()));
}
inline bool effectiveCamera(float& x,float& y,float& z){
    if(!active())return false;
    Pose current;{std::lock_guard lock(poseMutex);current=pose;}
    Quaternion unused{};return buildCamera(current,blend.load(),x,y,z,unused);
}
inline const char* status(){
    if(!enabled)return "OFF";
    if(!supported)return "UNAVAILABLE";
    return active()?"ACTIVE":"WAITING";
}
inline void shutdown(){
    enabled=false;playing=false;blend=0.f;enteredAt=0;cameraAppliedAt=0;
    cameraHookAt=0;cameraHookCalls=0;cameraApplyCalls=0;cameraRejectCalls=0;
    lastDiagnostic=0;
}
inline void unload(){
    shutdown();
    if(hooked&&orientationSystemTarget)
        pl::memory::unhook(orientationSystemTarget,reinterpret_cast<void*>(&orientationSystemHook));
    if(hooked&&adjustTarget)pl::memory::unhook(adjustTarget,reinterpret_cast<void*>(&adjustHook));
    if(hooked&&positionSystemTarget)
        pl::memory::unhook(positionSystemTarget,reinterpret_cast<void*>(&positionSystemHook));
    hooked=false;supported=false;
    originalPositionSystem=nullptr;originalOrientationSystem=nullptr;originalAdjust=nullptr;
    positionSystemTarget=nullptr;orientationSystemTarget=nullptr;adjustTarget=nullptr;
}
} // namespace lm_cinematic
