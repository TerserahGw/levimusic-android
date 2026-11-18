#pragma once
// LeviMusic client-local 3D lyric experiment.
// World-render hook inspired by the open GPL-3.0 BedrockTools project:
// https://github.com/QYCottage/BedrockTools/tree/main/src/modules/visual
// This file reimplements a small renderer; no LHolo or WebScreen binary is reused.
// Requires exact matching game signatures and current Bedrock layout offsets.
#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>
#include <android/log.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

namespace lm_world3d {
constexpr char LOG_TAG[]="LeviMusicWorld3D";
struct Vec3 {float x=0,y=0,z=0;};
inline Vec3 operator+(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 operator-(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 operator*(Vec3 a,float v){return {a.x*v,a.y*v,a.z*v};}
inline Vec3 cross(Vec3 a,Vec3 b){
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
inline Vec3 normalized(Vec3 a){
    float m=std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);
    return m<.0001f?Vec3{1,0,0}:a*(1.f/m);
}
inline std::atomic<bool> enabled{false},attempted{false},installed{false};
inline std::atomic<void*> client{nullptr};
inline std::mutex textMutex;
inline std::string currentLine;
inline uint64_t lyricSerial=0,positionedSerial=0;
inline Vec3 worldAnchor{};
inline std::chrono::steady_clock::time_point lyricTime=std::chrono::steady_clock::now();

using RenderFn=void(*)(void*,void*,void*);
using UpdateFn=void*(*)(void*,bool);
using GetPlayerFn=void*(*)(void*);
using TessBegin=void(*)(void*,void*,int,int,int);
using TessColor=void(*)(void*,float,float,float,float);
using TessVertex=void(*)(void*,float,float,float);
using RenderMesh=void(*)(void*,void*,void*,char*);
inline RenderFn originalRender=nullptr;
inline UpdateFn originalUpdate=nullptr;
inline GetPlayerFn getLocalPlayer=nullptr;
inline TessBegin tessBegin=nullptr;
inline TessColor tessColor=nullptr;
inline TessVertex tessVertex=nullptr;
inline RenderMesh renderMesh=nullptr;
inline void* targetRender=nullptr;
inline void* targetUpdate=nullptr;

// Known BedrockTools Android offsets; THEY ARE VERSION-SPECIFIC.
constexpr size_t LEVEL_RENDERER_PLAYER=0x430;
constexpr size_t CAMERA_POSITION=0x614;
constexpr size_t SELECTION_MATERIAL=0x10A0;
constexpr size_t SCREEN_TESSELLATOR=0xB0;
constexpr size_t SCREEN_COLOR_HOLDER=0x30;
constexpr size_t ACTOR_ROTATION=0x238;

using Rows=std::array<std::string_view,7>;
// Original minimal 5x7 Latin glyphs; non-Latin characters currently use '?'.
inline Rows bitmap(char c){
    switch(c){
    case 'A':return {" ### ","#   #","#   #","#####","#   #","#   #","#   #"};
    case 'B':return {"#### ","#   #","#   #","#### ","#   #","#   #","#### "};
    case 'C':return {" ####","#    ","#    ","#    ","#    ","#    "," ####"};
    case 'D':return {"#### ","#   #","#   #","#   #","#   #","#   #","#### "};
    case 'E':return {"#####","#    ","#    ","#### ","#    ","#    ","#####"};
    case 'F':return {"#####","#    ","#    ","#### ","#    ","#    ","#    "};
    case 'G':return {" ####","#    ","#    ","# ###","#   #","#   #"," ####"};
    case 'H':return {"#   #","#   #","#   #","#####","#   #","#   #","#   #"};
    case 'I':return {"#####","  #  ","  #  ","  #  ","  #  ","  #  ","#####"};
    case 'J':return {"  ###","   # ","   # ","   # ","#  # ","#  # "," ##  "};
    case 'K':return {"#   #","#  # ","# #  ","##   ","# #  ","#  # ","#   #"};
    case 'L':return {"#    ","#    ","#    ","#    ","#    ","#    ","#####"};
    case 'M':return {"#   #","## ##","# # #","#   #","#   #","#   #","#   #"};
    case 'N':return {"#   #","##  #","# # #","#  ##","#   #","#   #","#   #"};
    case 'O':return {" ### ","#   #","#   #","#   #","#   #","#   #"," ### "};
    case 'P':return {"#### ","#   #","#   #","#### ","#    ","#    ","#    "};
    case 'Q':return {" ### ","#   #","#   #","#   #","# # #","#  ##"," ####"};
    case 'R':return {"#### ","#   #","#   #","#### ","# #  ","#  # ","#   #"};
    case 'S':return {" ####","#    ","#    "," ### ","    #","    #","#### "};
    case 'T':return {"#####","  #  ","  #  ","  #  ","  #  ","  #  ","  #  "};
    case 'U':return {"#   #","#   #","#   #","#   #","#   #","#   #"," ### "};
    case 'V':return {"#   #","#   #","#   #","#   #","#   #"," # # ","  #  "};
    case 'W':return {"#   #","#   #","#   #","# # #","# # #","## ##","#   #"};
    case 'X':return {"#   #","#   #"," # # ","  #  "," # # ","#   #","#   #"};
    case 'Y':return {"#   #","#   #"," # # ","  #  ","  #  ","  #  ","  #  "};
    case 'Z':return {"#####","    #","   # ","  #  "," #   ","#    ","#####"};
    case '0':return {" ### ","#  ##","# # #","# # #","##  #","#   #"," ### "};
    case '1':return {"  #  "," ##  ","  #  ","  #  ","  #  ","  #  ","#####"};
    case '2':return {" ### ","#   #","    #","   # ","  #  "," #   ","#####"};
    case '3':return {"#### ","    #","    #"," ### ","    #","    #","#### "};
    case '4':return {"   # ","  ## "," # # ","#  # ","#####","   # ","   # "};
    case '5':return {"#####","#    ","#    ","#### ","    #","    #","#### "};
    case '6':return {" ####","#    ","#    ","#### ","#   #","#   #"," ### "};
    case '7':return {"#####","    #","   # ","  #  "," #   "," #   "," #   "};
    case '8':return {" ### ","#   #","#   #"," ### ","#   #","#   #"," ### "};
    case '9':return {" ### ","#   #","#   #"," ####","    #","    #","#### "};
    case ' ':return {"     ","     ","     ","     ","     ","     ","     "};
    case '.':return {"     ","     ","     ","     ","     "," ### "," ### "};
    case ',':return {"     ","     ","     ","     "," ### ","  ## "," ##  "};
    case '?':return {" ### ","#   #","    #","   # ","  #  ","     ","  #  "};
    case '!':return {"  #  ","  #  ","  #  ","  #  ","  #  ","     ","  #  "};
    case '-':return {"     ","     ","     ","#####","     ","     ","     "};
    case ':':return {"     ","  #  ","  #  ","     ","  #  ","  #  ","     "};
    case '\'':return {"  #  ","  #  "," #   ","     ","     ","     ","     "};
    case '/':return {"    #","    #","   # ","  #  "," #   ","#    ","#    "};
    default:return {" ### ","#   #","    #","   # ","  #  ","     ","  #  "};
    }
}
inline std::string latinDisplay(std::string_view input){
    std::string result;
    for(unsigned char c:input){
        if(result.size()>=42)break;
        if(c>=128){if(result.empty()||result.back()!='?')result.push_back('?');continue;}
        if(c>='a'&&c<='z')result.push_back(static_cast<char>(c-32));
        else if(c>=32&&c<127)result.push_back(static_cast<char>(c));
    }
    return result;
}
inline uint32_t random32(uint32_t v){
    v^=v>>16;v*=0x7FEB352Du;v^=v>>15;v*=0x846CA68Bu;v^=v>>16;return v;
}
inline float random01(uint32_t v){return static_cast<float>(random32(v)%10000)/10000.f;}

inline void* clientUpdateHook(void* self,bool v){
    client.store(self,std::memory_order_relaxed);
    return originalUpdate?originalUpdate(self,v):nullptr;
}
inline void renderHook(void* renderer,void* ctx,void* arg){
    if(originalRender)originalRender(renderer,ctx,arg);
    if(!enabled.load(std::memory_order_relaxed)||!ctx||!renderer||
       !tessBegin||!tessColor||!tessVertex||!renderMesh||!getLocalPlayer)return;
    auto* gameClient=client.load(std::memory_order_relaxed);
    if(!gameClient)return;
    auto* player=getLocalPlayer(gameClient);
    if(!player)return;
    const uintptr_t base=reinterpret_cast<uintptr_t>(renderer);
    const uintptr_t p=reinterpret_cast<uintptr_t>(player);
    auto* lrp=*reinterpret_cast<void**>(base+LEVEL_RENDERER_PLAYER);
    if(!lrp)return;
    auto* rotation=*reinterpret_cast<void**>(p+ACTOR_ROTATION);
    if(!rotation)return;
    auto* tessel=*reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(ctx)+SCREEN_TESSELLATOR);
    auto* colors=*reinterpret_cast<unsigned char**>(reinterpret_cast<uintptr_t>(ctx)+SCREEN_COLOR_HOLDER);
    auto* material=reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(lrp)+SELECTION_MATERIAL);
    if(!tessel||!colors||!(*reinterpret_cast<void**>(material)))return;
    auto* camera=reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(lrp)+CAMERA_POSITION);
    Vec3 cam{camera[0],camera[1],camera[2]};
    const float pitch=reinterpret_cast<float*>(rotation)[0];
    const float yaw=reinterpret_cast<float*>(rotation)[1];
    if(!std::isfinite(pitch)||!std::isfinite(yaw) ||
       std::abs(pitch)>360||std::abs(yaw)>360 ||
       !std::isfinite(cam.x)||!std::isfinite(cam.y)||!std::isfinite(cam.z))return;
    constexpr float DEG=0.01745329251994f;
    Vec3 forward=normalized({-std::sin(yaw*DEG)*std::cos(pitch*DEG),
                             -std::sin(pitch*DEG),
                             std::cos(yaw*DEG)*std::cos(pitch*DEG)});
    Vec3 right=normalized(cross(forward,{0,1,0}));
    Vec3 up=normalized(cross(right,forward));
    std::string line;
    Vec3 anchor{};
    float alpha=1.f;
    {
        std::lock_guard lock(textMutex);
        if(currentLine.empty())return;
        if(lyricSerial!=positionedSerial){
            const uint32_t seed=random32(static_cast<uint32_t>(lyricSerial)*2654435761U);
            const float across=(random01(seed)-.5f)*3.6f;
            const float height=(random01(seed+31)-.5f)*2.f;
            anchor=cam+forward*4.6f+right*across+up*height;
            worldAnchor=anchor;
            positionedSerial=lyricSerial;
        }else anchor=worldAnchor;
        line=latinDisplay(currentLine);
        alpha=std::clamp(std::chrono::duration<float>(
               std::chrono::steady_clock::now()-lyricTime).count()/.23f,0.f,1.f);
    }
    if(line.empty())return;
    const Vec3 facing=normalized(cam-anchor);
    Vec3 glyphRight=normalized(cross(facing,{0,1,0}));
    Vec3 glyphUp=normalized(cross(glyphRight,facing));
    const float scale=.023f;
    const float center=static_cast<float>(line.size())*3.f*scale;
    int lit=0;
    for(char ch:line){
        Rows rows=bitmap(ch);
        for(int y=0;y<7;y++)for(int x=0;x<5;x++)
            if(rows[static_cast<size_t>(y)][static_cast<size_t>(x)]=='#')lit++;
    }
    if(lit==0)return;
    // Five layers, two vertices per on-pixel dash. World coordinates are
    // camera-relative, as in the native BedrockTools Tessellator world path.
    const int vertices=lit*10;
    if(vertices<=0 || vertices>20000)return;
    std::array<unsigned char,16> savedColor{};
    std::memcpy(savedColor.data(),colors,savedColor.size());
    const float neutral[4]{1.f,1.f,1.f,1.f};
    std::memcpy(colors,neutral,sizeof(neutral));
    colors[16]=1;
    tessBegin(tessel,nullptr,4,vertices,0);
    const float offset=scale*.25f;
    for(int pass=0;pass<5;pass++){
        bool outline=pass<4;
        tessColor(tessel,outline?0.f:1.f,outline?0.f:1.f,
                  outline?0.f:1.f,alpha*(outline?.95f:1.f));
        const float dx=(pass==0?-offset:pass==1?offset:0.f);
        const float dy=(pass==2?-offset:pass==3?offset:0.f);
        for(size_t n=0;n<line.size();n++){
            Rows rows=bitmap(line[n]);
            for(int y=0;y<7;y++)for(int x=0;x<5;x++){
                if(rows[static_cast<size_t>(y)][static_cast<size_t>(x)]!='#')continue;
                const float px=(static_cast<float>(n)*6.f+x)*scale-center+dx;
                const float py=(6.f-y)*scale+dy;
                Vec3 position=anchor+glyphRight*px+glyphUp*py-cam;
                tessVertex(tessel,position.x,position.y,position.z);
                Vec3 target=position+glyphRight*(scale*.7f);
                tessVertex(tessel,target.x,target.y,target.z);
            }
        }
    }
    char pad[0x58]{};
    renderMesh(ctx,tessel,material,pad);
    std::memcpy(colors,savedColor.data(),savedColor.size());
    colors[16]=1;
}

inline void publish(std::string_view value){
    std::lock_guard lock(textMutex);
    if(value==currentLine)return;
    currentLine=std::string(value.substr(0,250));
    ++lyricSerial;
    lyricTime=std::chrono::steady_clock::now();
}

inline bool tryInstall(){
    if(installed)return true;
    if(attempted.exchange(true))return false;
    // BedrockTools public GPL signatures. Resolve only when the user
    // explicitly enables this experimental local client renderer.
    const auto sig=[](const char* pattern)->uintptr_t{
        return pl::memory::resolveSignature(pattern,"libminecraftpe.so");
    };
    const uintptr_t render=sig("? ? ? D1 ? ? ? FD ? ? ? 6D ? ? ? 6D ? ? ? 6D ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 57 D0 3B D5");
    const uintptr_t update=sig("? ? ? FC ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? D1 59 D0 3B D5 ? ? ? 52 F5 03 02 2A");
    const uintptr_t getPlayer=sig("? ? ? D1 ? ? ? A9 ? ? ? F9 ? ? ? 91 53 D0 3B D5 E8 03 00 AA ? ? ? 91 ? ? ? F9 ? ? ? 91 ? ? ? F8 ? ? ? ? ? ? ? 91 ? ? ? ? ? ? ? 36 ? ? ? 91 ? ? ? 52 ? ? ? ? ? ? ? ? E0 03 1F AA ? ? ? F9 ? ? ? F8 1F 01 09 EB ? ? ? 54 ? ? ? A9 ? ? ? F9 ? ? ? 91 C0 03 5F D6 ? ? ? ? C0 03 5F D6");
    const uintptr_t begin=sig("? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 03 00 91 ? ? ? 39 ? ? ? 39 08 01 09 2A");
    const uintptr_t color=sig("? ? ? 52 ? ? ? 39 04 01 27 1E 00 08 24 1E 21 08 24 1E 42 08 24 1E 63 08 24 1E 08 00 38 1E");
    const uintptr_t vertex=sig("? ? ? D1 ? ? ? FD ? ? ? 6D ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 5B D0 3B D5 ? ? ? F9 ? ? ? F9");
    const uintptr_t mesh=sig("? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 03 00 91 ? ? ? D1 57 D0 3B D5 F6 03 00 AA E0 03 01 AA ? ? ? F9 F4 03 03 AA");
    if(!render||!update||!getPlayer||!begin||!color||!vertex||!mesh){
        __android_log_print(ANDROID_LOG_WARN,LOG_TAG,
             "World 3D renderer incompatible: one or more Minecraft signatures unavailable");
        return false;
    }
    getLocalPlayer=reinterpret_cast<GetPlayerFn>(getPlayer);
    tessBegin=reinterpret_cast<TessBegin>(begin);
    tessColor=reinterpret_cast<TessColor>(color);
    tessVertex=reinterpret_cast<TessVertex>(vertex);
    renderMesh=reinterpret_cast<RenderMesh>(mesh);
    targetRender=reinterpret_cast<void*>(render);
    targetUpdate=reinterpret_cast<void*>(update);
    if(pl::memory::hook(targetUpdate,reinterpret_cast<void*>(&clientUpdateHook),
                         reinterpret_cast<void**>(&originalUpdate))!=0){
        __android_log_print(ANDROID_LOG_WARN,LOG_TAG,"Cannot hook Minecraft client update");
        return false;
    }
    if(pl::memory::hook(targetRender,reinterpret_cast<void*>(&renderHook),
                         reinterpret_cast<void**>(&originalRender))!=0){
        pl::memory::unhook(targetUpdate,reinterpret_cast<void*>(&clientUpdateHook));
        originalUpdate=nullptr;
        __android_log_print(ANDROID_LOG_WARN,LOG_TAG,"Cannot hook Minecraft level renderer");
        return false;
    }
    installed=true;
    __android_log_print(ANDROID_LOG_INFO,LOG_TAG,
        "Experimental client-only world-space 3D lyrics rendering activated");
    return true;
}
inline void shutdown(){
    enabled=false;
    if(installed.exchange(false)){
        pl::memory::unhook(targetRender,reinterpret_cast<void*>(&renderHook));
        pl::memory::unhook(targetUpdate,reinterpret_cast<void*>(&clientUpdateHook));
    }
    client=nullptr;
    originalRender=nullptr;
    originalUpdate=nullptr;
    publish("");
    attempted=false;
}
} // namespace lm_world3d
