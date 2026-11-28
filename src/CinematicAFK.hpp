#pragma once

// Minecraft 1.26.66.1-only cinematic camera. The supplied game binary shows
// SetupCamera building its live rotation twice through CameraTransformFn, then
// copying that result and the component position into the final renderer state.
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
using SetupCameraFn=void(*)(void*,void*,float);
using CameraTransformFn=void(*)(float*,const float*);
struct Pose {float x=0,y=0,z=0,pitch=0,yaw=0;uintptr_t player=0,world=0;int64_t sampled=0;};
struct Shot {float yaw=0,distance=0,vertical=0,pitch=0;};
struct Quaternion {float x=0,y=0,z=0,w=1;};
struct Position {float x=0,y=0,z=0;};

inline std::atomic<bool> enabled{false},supported{false},hooked{false},playing{false};
inline std::atomic<int> idleDelay{15};
inline std::atomic<int64_t> lastActivity{0},enteredAt{0},cameraAppliedAt{0},cameraHookAt{0},lastDiagnostic{0};
inline std::atomic<uint64_t> setupHookCalls{0},builderHookCalls{0},builderApplyCalls{0},
    finalStateWrites{0},cameraApplyCalls{0},cameraRejectCalls{0};
inline std::atomic<float> blend{0.f};
inline std::mutex poseMutex;inline Pose pose{};
inline SetupCameraFn originalSetup=nullptr;
inline CameraTransformFn originalCameraTransform=nullptr;
inline void* setupTarget=nullptr;inline void* transformTarget=nullptr;
inline uintptr_t imageBase=0;inline bool exactBuild=false;
inline thread_local int setupDepth=0;
inline thread_local bool frameDecisionMade=false,frameActive=false;
inline thread_local int builderCallsThisSetup=0;
inline thread_local float* patchedPosition=nullptr;
inline thread_local Position savedPosition{},cinematicPosition{};
inline thread_local Quaternion savedOrientation{},cinematicOrientation{};
inline thread_local bool componentHeld=false;

inline int64_t nowMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();}
inline void noteActivity(){lastActivity=nowMs();enteredAt=0;}
inline void setPlaying(bool value){playing=value;}
inline void observePlayer(void* player,float x,float y,float z,float pitch,float yaw,uintptr_t world){
    const int64_t now=nowMs();
    bool moved=false;
    {
        std::lock_guard lock(poseMutex);
        const bool same=pose.player==reinterpret_cast<uintptr_t>(player)&&pose.world==world&&pose.sampled;
        if(same){
            const float dx=x-pose.x,dy=y-pose.y,dz=z-pose.z;
            const float yawDelta=std::remainder(yaw-pose.yaw,360.f);
            moved=dx*dx+dy*dy+dz*dz>.0025f||std::abs(pitch-pose.pitch)>.45f||std::abs(yawDelta)>.45f;
        }else moved=true;
        pose={x,y,z,pitch,yaw,reinterpret_cast<uintptr_t>(player),world,now};
    }
    // Player pose is the authoritative AFK signal. LeviLauncher can emit
    // synthetic MOVE/key-up callbacks while no person is touching the game;
    // those callbacks used to postpone cinematic mode forever.
    if(moved){lastActivity=now;enteredAt=0;}
}
inline bool matches(const uint8_t* at,std::string_view pattern){
    auto hex=[](char c)->int{
        if(c>='0'&&c<='9')return c-'0';
        if(c>='A'&&c<='F')return c-'A'+10;
        if(c>='a'&&c<='f')return c-'a'+10;
        return -1;
    };
    size_t index=0;while(!pattern.empty()){
        const auto end=pattern.find(' ');const auto token=pattern.substr(0,end);
        if(token!="?"&&token!="??"&&(token.size()!=2||hex(token[0])<0||hex(token[1])<0||
           at[index]!=(hex(token[0])<<4|hex(token[1]))))return false;
        ++index;if(end==std::string_view::npos)return true;pattern.remove_prefix(end+1);
    }return false;
}
inline bool plausible(const void* value,size_t alignment=alignof(void*)){
    const auto raw=reinterpret_cast<uintptr_t>(value);
    return raw>0x10000&&raw<0x0001000000000000ULL&&raw%alignment==0;
}
inline bool sanePosition(const Position& p){
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&
           std::abs(p.x)<1.e7f&&std::abs(p.y)<1.e7f&&std::abs(p.z)<1.e7f;
}
inline bool saneQuaternion(const Quaternion& q){
    const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return std::isfinite(n)&&n>.35f&&n<1.65f;
}
inline bool nearPosition(const Position& a,const Position& b,float epsilon=.18f){
    return std::abs(a.x-b.x)<epsilon&&std::abs(a.y-b.y)<epsilon&&
           std::abs(a.z-b.z)<epsilon;
}
inline bool nearQuaternion(const Quaternion& a,const Quaternion& b,float epsilon=.08f){
    const float direct=std::abs(a.x-b.x)+std::abs(a.y-b.y)+
        std::abs(a.z-b.z)+std::abs(a.w-b.w);
    const float negated=std::abs(a.x+b.x)+std::abs(a.y+b.y)+
        std::abs(a.z+b.z)+std::abs(a.w+b.w);
    return std::min(direct,negated)<epsilon;
}
inline void writeComponent(float* component,const Quaternion& rotation,const Position& position){
    component[0]=rotation.x;component[1]=rotation.y;
    component[2]=rotation.z;component[3]=rotation.w;
    component[4]=position.x;component[5]=position.y;component[6]=position.z;
}
inline Quaternion normalized(Quaternion q){
    const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if(!std::isfinite(n)||n<.001f)return {};
    q.x/=n;q.y/=n;q.z/=n;q.w/=n;return q;
}
inline Quaternion blended(Quaternion from,Quaternion to,float amount){
    from=normalized(from);to=normalized(to);
    if(from.x*to.x+from.y*to.y+from.z*to.z+from.w*to.w<0.f)
        to={-to.x,-to.y,-to.z,-to.w};
    amount=std::clamp(amount,0.f,1.f);
    return normalized({from.x+(to.x-from.x)*amount,from.y+(to.y-from.y)*amount,
        from.z+(to.z-from.z)*amount,from.w+(to.w-from.w)*amount});
}
inline Quaternion lookQuaternion(float yaw,float pitch){
    const float cy=std::cos(yaw*.5f),sy=std::sin(yaw*.5f);
    const float cp=std::cos(pitch*.5f),sp=std::sin(pitch*.5f);
    return normalized({cy*sp,-sy*cp,sy*sp,cy*cp});
}
inline Shot shotAt(int64_t elapsed,float strength){
    const float s=std::max(0.f,float(elapsed)*.001f);
    return {(s*.145f+std::sin(s*.071f)*.24f)*strength,
        (5.8f+std::sin(s*.113f)*1.15f+std::sin(s*.031f)*.55f)*strength,
        (1.75f+std::sin(s*.087f)*.72f+std::sin(s*.029f)*.38f)*strength,
        (-.105f+std::sin(s*.087f)*.065f)*strength};
}
inline bool shouldRun(Pose& out,float& strength){
    if(!enabled||!supported)return false;
    const int64_t now=nowMs();
    {std::lock_guard lock(poseMutex);out=pose;}
    if(!out.player||!out.world||now-out.sampled>1200)return false;
    const int64_t threshold=int64_t(std::clamp(idleDelay.load(),10,60))*1000;
    if(now-lastActivity.load()<threshold){strength=std::max(0.f,blend.load()-.08f);blend=strength;return strength>.001f;}
    int64_t entered=enteredAt.load();if(!entered){entered=now;enteredAt=entered;}
    strength=std::clamp(float(now-entered)/1500.f,0.f,1.f);blend=strength;return true;
}
inline bool prepareFrame(const float* componentRotation){
    if(!plausible(componentRotation,alignof(float)))return false;
    Pose current;float strength=0;if(!shouldRun(current,strength))return false;
    Quaternion original{componentRotation[0],componentRotation[1],
        componentRotation[2],componentRotation[3]};
    Position originalPosition{componentRotation[4],componentRotation[5],componentRotation[6]};
    if(!saneQuaternion(original)||!sanePosition(originalPosition))return false;
    // SetupCamera is not the last consumer of this camera component. Beta2
    // restored the component as soon as SetupCamera returned, so a later
    // renderer read still saw the normal pose. Preserve the native pose only
    // when the game has refreshed it, then keep our pose alive for the frame.
    const bool stillOurs=componentHeld&&patchedPosition==componentRotation+4&&
        nearPosition(originalPosition,cinematicPosition)&&
        nearQuaternion(original,cinematicOrientation);
    if(!stillOurs){
        savedPosition=originalPosition;
        savedOrientation=original;
        componentHeld=true;
    }
    const auto shot=shotAt(nowMs()-enteredAt.load(),strength);
    constexpr float deg=.01745329251994329577f;
    const float orbit=current.yaw*deg+shot.yaw;
    cinematicPosition={current.x+std::sin(orbit)*shot.distance,
        current.y+1.62f+shot.vertical,current.z-std::cos(orbit)*shot.distance};
    const float dx=current.x-cinematicPosition.x;
    const float dy=current.y+1.2f-cinematicPosition.y;
    const float dz=current.z-cinematicPosition.z;
    const float horizontal=std::sqrt(dx*dx+dz*dz);
    if(!sanePosition(cinematicPosition)||horizontal<.05f)return false;
    const float yaw=std::atan2(-dx,dz);
    const float pitch=-std::atan2(dy,horizontal)+shot.pitch;
    const float eased=strength*strength*(3.f-2.f*strength);
    cinematicOrientation=blended(savedOrientation,lookQuaternion(yaw,pitch),eased);
    if(!saneQuaternion(cinematicOrientation))return false;
    patchedPosition=const_cast<float*>(componentRotation+4);
    writeComponent(patchedPosition-4,cinematicOrientation,cinematicPosition);
    return true;
}
inline void restoreComponent(const float* componentRotation){
    if(!componentHeld)return;
    auto* component=const_cast<float*>(componentRotation);
    const Quaternion current{component[0],component[1],component[2],component[3]};
    const Position currentPosition{component[4],component[5],component[6]};
    if(component+4==patchedPosition&&nearPosition(currentPosition,cinematicPosition)&&
       nearQuaternion(current,cinematicOrientation))
        writeComponent(component,savedOrientation,savedPosition);
    componentHeld=false;patchedPosition=nullptr;
}
inline void cameraTransformHook(float* output,const float* componentRotation){
    builderHookCalls.fetch_add(1,std::memory_order_relaxed);
    if(setupDepth<=0){
        if(originalCameraTransform)originalCameraTransform(output,componentRotation);
        return;
    }
    ++builderCallsThisSetup;
    if(!frameDecisionMade){
        frameDecisionMade=true;frameActive=prepareFrame(componentRotation);
        if(!frameActive)restoreComponent(componentRotation);
        if(!frameActive&&blend.load()>.001f)cameraRejectCalls.fetch_add(1,std::memory_order_relaxed);
    }
    if(frameActive&&patchedPosition){
        writeComponent(patchedPosition-4,cinematicOrientation,cinematicPosition);
        if(originalCameraTransform)
            originalCameraTransform(output,reinterpret_cast<const float*>(&cinematicOrientation));
        builderApplyCalls.fetch_add(1,std::memory_order_relaxed);
    }else if(originalCameraTransform){
        originalCameraTransform(output,componentRotation);
    }
}
inline void setupHook(void* owner,void* stack,float partial){
    cameraHookAt=nowMs();setupHookCalls.fetch_add(1,std::memory_order_relaxed);
    const bool outer=setupDepth==0;
    if(outer){frameDecisionMade=false;frameActive=false;builderCallsThisSetup=0;}
    ++setupDepth;if(originalSetup)originalSetup(owner,stack,partial);--setupDepth;
    if(!outer)return;
    if(frameActive&&builderCallsThisSetup>0){
        // Verified 1.26.66.1 final camera positions. Re-assert both copies
        // after native SetupCamera so post-SetupCamera consumers cannot see a
        // stale location even if another internal path rewrote one of them.
        if(plausible(owner)){
            auto* bytes=static_cast<uint8_t*>(owner);
            auto* primary=reinterpret_cast<float*>(bytes+0x614);
            auto* render=reinterpret_cast<float*>(bytes+0x620);
            primary[0]=render[0]=cinematicPosition.x;
            primary[1]=render[1]=cinematicPosition.y;
            primary[2]=render[2]=cinematicPosition.z;
            finalStateWrites.fetch_add(1,std::memory_order_relaxed);
        }
        cameraAppliedAt=nowMs();
        if(cameraApplyCalls.fetch_add(1,std::memory_order_relaxed)==0)
            __android_log_print(ANDROID_LOG_INFO,"LeviMusicCinematic",
                "native camera component orbit reached final SetupCamera state");
    }
}
inline void prepare(uintptr_t base,bool exact26661){
    if(hooked||!exact26661||!base){supported=hooked.load();return;}
    constexpr uintptr_t setupRva=0xB7CFDC8,transformRva=0x9C75AA8;
    constexpr std::string_view setupSignature="FF 03 04 D1 EE 3B 00 FD ED 33 08 6D EB 2B 09 6D E9 23 0A 6D FD 7B 0B A9 FA 67 0C A9 F8 5F 0D A9 F6 57 0E A9 F4 4F 0F A9";
    constexpr std::string_view transformSignature=
        "28 00 80 52 E0 03 22 1E E1 03 22 1E 02 01 22 1E E3 03 22 1E 49 00 80 52";
    auto* setupEntry=reinterpret_cast<uint8_t*>(base+setupRva);
    auto* transformEntry=reinterpret_cast<uint8_t*>(base+transformRva);
    if(!matches(setupEntry,setupSignature)||!matches(transformEntry,transformSignature)){
        __android_log_print(ANDROID_LOG_WARN,"LeviMusicCinematic",
            "supplied 1.26.66.1 camera transform signature mismatch");return;
    }
    setupTarget=setupEntry;transformTarget=transformEntry;
    if(pl::memory::hook(transformTarget,reinterpret_cast<void*>(&cameraTransformHook),
       reinterpret_cast<void**>(&originalCameraTransform))!=0||!originalCameraTransform)return;
    if(pl::memory::hook(setupTarget,reinterpret_cast<void*>(&setupHook),
       reinterpret_cast<void**>(&originalSetup))!=0||!originalSetup){
        pl::memory::unhook(transformTarget,reinterpret_cast<void*>(&cameraTransformHook));
        originalCameraTransform=nullptr;return;
    }
    hooked=true;supported=true;noteActivity();
    __android_log_print(ANDROID_LOG_INFO,"LeviMusicCinematic",
        "binary-verified SetupCamera + camera transform hooks ready");
}
inline void discover(uintptr_t base,bool exact26661){imageBase=base;exactBuild=exact26661;if(enabled&&!hooked)prepare(imageBase,exactBuild);}
inline void request(bool value,int delay=15){
    idleDelay=std::clamp(delay,10,60);enabled=value;noteActivity();
    if(value&&!hooked&&imageBase)prepare(imageBase,exactBuild);
    if(!value)blend=0.f;
}
inline bool active(){return enabled&&supported&&blend.load()>.01f&&nowMs()-cameraAppliedAt.load()<350;}
inline void diagnose(){
    if(!enabled)return;
    const int64_t now=nowMs();
    int64_t previous=lastDiagnostic.load();
    if(now-previous<5000||!lastDiagnostic.compare_exchange_strong(previous,now))return;
    Pose current;{std::lock_guard lock(poseMutex);current=pose;}const auto hookAt=cameraHookAt.load(),appliedAt=cameraAppliedAt.load();
    __android_log_print(ANDROID_LOG_INFO,"LeviMusicCinematic",
        "state supported=%d poseAge=%lld idleAge=%lld setup=%llu builders=%llu builderApply=%llu finalWrite=%llu apply=%llu reject=%llu hookAge=%lld applyAge=%lld blend=%.2f",
        int(supported.load()),static_cast<long long>(current.sampled?now-current.sampled:-1),static_cast<long long>(now-lastActivity.load()),
        static_cast<unsigned long long>(setupHookCalls.load()),static_cast<unsigned long long>(builderHookCalls.load()),
        static_cast<unsigned long long>(builderApplyCalls.load()),static_cast<unsigned long long>(finalStateWrites.load()),
        static_cast<unsigned long long>(cameraApplyCalls.load()),
        static_cast<unsigned long long>(cameraRejectCalls.load()),static_cast<long long>(hookAt?now-hookAt:-1),
        static_cast<long long>(appliedAt?now-appliedAt:-1),double(blend.load()));
}
inline bool effectiveCamera(float& x,float& y,float& z){
    if(!active())return false;
    Pose current;
    {std::lock_guard lock(poseMutex);current=pose;}
    const auto shot=shotAt(nowMs()-enteredAt.load(),blend.load());constexpr float deg=.01745329251994329577f;
    const float yaw=current.yaw*deg+shot.yaw;x=current.x+std::sin(yaw)*shot.distance;
    y=current.y+1.62f+shot.vertical;z=current.z-std::cos(yaw)*shot.distance;return true;
}
inline const char* status(){if(!enabled)return "OFF";if(!supported)return "UNAVAILABLE";return active()?"ACTIVE":"WAITING";}
inline void shutdown(){enabled=false;playing=false;blend=0.f;enteredAt=0;cameraAppliedAt=0;cameraHookAt=0;
    setupHookCalls=0;builderHookCalls=0;builderApplyCalls=0;finalStateWrites=0;
    cameraApplyCalls=0;cameraRejectCalls=0;lastDiagnostic=0;}
inline void unload(){shutdown();if(hooked&&setupTarget)pl::memory::unhook(setupTarget,reinterpret_cast<void*>(&setupHook));
    if(hooked&&transformTarget)pl::memory::unhook(transformTarget,reinterpret_cast<void*>(&cameraTransformHook));
    hooked=false;supported=false;originalSetup=nullptr;originalCameraTransform=nullptr;setupTarget=nullptr;transformTarget=nullptr;}
} // namespace lm_cinematic
