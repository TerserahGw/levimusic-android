#pragma once

// Experimental Minecraft 1.26.66.1-only cinematic camera. This hooks the
// audited final SetupCamera matrix path instead of guessing component fields.
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
using MatrixTopDirtyFn=float*(*)(void*);
using CameraDepsFn=void(*)(void*);
struct Pose {float x=0,y=0,z=0,pitch=0,yaw=0;uintptr_t player=0,world=0;int64_t sampled=0;};
struct Shot {float yaw=0,distance=0,vertical=0,pitch=0;};
inline std::atomic<bool> enabled{false},supported{false},hooked{false},playing{false};
inline std::atomic<int> idleDelay{15};
inline std::atomic<int64_t> lastActivity{0},enteredAt{0},cameraAppliedAt{0},cameraHookAt{0},lastDiagnostic{0};
inline std::atomic<uint64_t> cameraHookCalls{0},cameraApplyCalls{0},cameraRejectCalls{0};
inline std::atomic<float> blend{0.f};
inline std::mutex poseMutex;inline Pose pose{};
inline SetupCameraFn original=nullptr;inline MatrixTopDirtyFn matrixTopDirty=nullptr;
inline CameraDepsFn cameraDeps=nullptr;inline void* target=nullptr;
inline uintptr_t imageBase=0;inline bool exactBuild=false;

inline int64_t nowMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();}
inline void noteActivity(){lastActivity=nowMs();enteredAt=0;}
inline void setPlaying(bool value){playing=value;}
inline void observePlayer(void* player,float x,float y,float z,float pitch,float yaw,uintptr_t world){
    std::lock_guard lock(poseMutex);pose={x,y,z,pitch,yaw,reinterpret_cast<uintptr_t>(player),world,nowMs()};
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
inline bool saneMatrix(const float* matrix){
    if(!plausible(matrix,alignof(float)))return false;
    constexpr int used[]{0,1,2,4,5,6,8,9,10,12,13,14};
    for(int i:used)if(!std::isfinite(matrix[i])||std::abs(matrix[i])>1.e7f)return false;
    const float basis=std::abs(matrix[0])+std::abs(matrix[1])+std::abs(matrix[2])+
        std::abs(matrix[4])+std::abs(matrix[5])+std::abs(matrix[6])+
        std::abs(matrix[8])+std::abs(matrix[9])+std::abs(matrix[10]);
    return basis>.05f;
}
inline Shot shotAt(int64_t elapsed,float strength){
    const float s=std::max(0.f,float(elapsed)*.001f);
    return {(s*.145f+std::sin(s*.071f)*.24f)*strength,
        (5.8f+std::sin(s*.113f)*1.15f+std::sin(s*.031f)*.55f)*strength,
        (1.75f+std::sin(s*.087f)*.72f+std::sin(s*.029f)*.38f)*strength,
        (-.105f+std::sin(s*.087f)*.065f)*strength};
}
inline void applyOrbit(float* m,float yaw,float distance,float vertical,float pitch){
    const float sy=std::sin(yaw),cy=std::cos(yaw);
    for(int row=0;row<3;++row){const int i=row*4;const float x=m[i],z=m[i+2];m[i]=cy*x+sy*z;m[i+2]=cy*z-sy*x;}
    const float sp=std::sin(pitch),cp=std::cos(pitch);
    for(int row=0;row<3;++row){const int i=row*4;const float y=m[i+1],z=m[i+2];m[i+1]=cp*y-sp*z;m[i+2]=sp*y+cp*z;}
    const float tx=m[12],ty=m[13],tz=m[14];m[12]=cy*tx+sy*tz;m[13]=ty-vertical;m[14]=cy*tz-sy*tx-distance;
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
inline void setupHook(void* owner,void* stack,float partial){
    if(original)original(owner,stack,partial);
    cameraHookAt=nowMs();cameraHookCalls.fetch_add(1);
    Pose current;float strength=0;if(!shouldRun(current,strength))return;
    float* matrix=matrixTopDirty&&plausible(stack)?matrixTopDirty(stack):nullptr;
    if(!saneMatrix(matrix)){cameraRejectCalls.fetch_add(1);return;}
    float backup[16];std::memcpy(backup,matrix,sizeof(backup));const auto shot=shotAt(nowMs()-enteredAt.load(),strength);
    applyOrbit(matrix,shot.yaw,shot.distance,shot.vertical,shot.pitch);
    if(!saneMatrix(matrix)){std::memcpy(matrix,backup,sizeof(backup));cameraRejectCalls.fetch_add(1);return;}
    if(cameraDeps&&plausible(stack))cameraDeps(stack);
    cameraAppliedAt=nowMs();
    if(cameraApplyCalls.fetch_add(1)==0)__android_log_print(ANDROID_LOG_INFO,"LeviMusicCinematic",
        "orbit applied to final SetupCamera matrix");
}
inline void prepare(uintptr_t base,bool exact26661){
    if(hooked||!exact26661||!base){supported=hooked.load();return;}
    constexpr uintptr_t setupRva=0xB7CFDC8,topRva=0x11A1A218,depsRva=0x11A1CBCC;
    constexpr std::string_view signature="FF 03 04 D1 EE 3B 00 FD ED 33 08 6D EB 2B 09 6D E9 23 0A 6D FD 7B 0B A9 FA 67 0C A9 F8 5F 0D A9 F6 57 0E A9 F4 4F 0F A9 FD C3 02 91 59 D0 3B D5 F3 03 00 AA E0 03 01 AA 28 17 40 F9 F7 03 01 AA";
    auto* entry=reinterpret_cast<uint8_t*>(base+setupRva);
    if(!matches(entry,signature)){__android_log_print(ANDROID_LOG_WARN,"LeviMusicCinematic",
        "1.26.66.1 SetupCamera signature mismatch");return;}
    matrixTopDirty=reinterpret_cast<MatrixTopDirtyFn>(base+topRva);cameraDeps=reinterpret_cast<CameraDepsFn>(base+depsRva);target=entry;
    if(pl::memory::hook(target,reinterpret_cast<void*>(&setupHook),reinterpret_cast<void**>(&original))!=0||!original)return;
    hooked=true;supported=true;noteActivity();__android_log_print(ANDROID_LOG_INFO,"LeviMusicCinematic","final SetupCamera orbit hook ready");
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
    const int64_t now=nowMs();int64_t previous=lastDiagnostic.load();
    if(now-previous<5000||!lastDiagnostic.compare_exchange_strong(previous,now))return;
    Pose current;{std::lock_guard lock(poseMutex);current=pose;}const auto hookAt=cameraHookAt.load(),appliedAt=cameraAppliedAt.load();
    __android_log_print(ANDROID_LOG_INFO,"LeviMusicCinematic",
        "state supported=%d poseAge=%lld idleAge=%lld hookCalls=%llu hookAge=%lld applyCalls=%llu rejects=%llu applyAge=%lld blend=%.2f",
        int(supported.load()),static_cast<long long>(current.sampled?now-current.sampled:-1),static_cast<long long>(now-lastActivity.load()),
        static_cast<unsigned long long>(cameraHookCalls.load()),static_cast<long long>(hookAt?now-hookAt:-1),
        static_cast<unsigned long long>(cameraApplyCalls.load()),static_cast<unsigned long long>(cameraRejectCalls.load()),
        static_cast<long long>(appliedAt?now-appliedAt:-1),double(blend.load()));
}
inline bool effectiveCamera(float& x,float& y,float& z){
    if(!active())return false;
    Pose current;{std::lock_guard lock(poseMutex);current=pose;}
    const auto shot=shotAt(nowMs()-enteredAt.load(),blend.load());constexpr float deg=.01745329251994329577f;
    const float yaw=current.yaw*deg+shot.yaw;x=current.x+std::sin(yaw)*shot.distance;
    y=current.y+1.62f+shot.vertical;z=current.z-std::cos(yaw)*shot.distance;return true;
}
inline const char* status(){if(!enabled)return "OFF";if(!supported)return "UNAVAILABLE";return active()?"ACTIVE":"WAITING";}
inline void shutdown(){enabled=false;playing=false;blend=0.f;enteredAt=0;cameraAppliedAt=0;cameraHookAt=0;
    cameraHookCalls=0;cameraApplyCalls=0;cameraRejectCalls=0;lastDiagnostic=0;}
inline void unload(){shutdown();if(hooked&&target)pl::memory::unhook(target,reinterpret_cast<void*>(&setupHook));
    hooked=false;supported=false;original=nullptr;target=nullptr;}
} // namespace lm_cinematic
