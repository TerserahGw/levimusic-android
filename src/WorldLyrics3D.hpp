#pragma once
// World renderer adapted from World Analysis (Copyright 2026 Ekowat),
// itself derived from BedrockTools by RadiantByte. GPL-3.0-only.
#include "WorldLyricsBindings.hpp"
#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>
#include <android/log.h>
#include <link.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
namespace lm_world3d {
using Clock=std::chrono::steady_clock;
inline int64_t nowMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();}
struct Vec3 {float x=0,y=0,z=0;};
inline Vec3 operator+(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 operator-(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 operator*(Vec3 a,float s){return {a.x*s,a.y*s,a.z*s};}
inline float magnitude2(Vec3 a){return a.x*a.x+a.y*a.y+a.z*a.z;}
inline Vec3 normalized(Vec3 a){float d=std::sqrt(magnitude2(a));return d>.00001f?a*(1.f/d):Vec3{1,0,0};}
inline bool valid(void* ptr){auto n=reinterpret_cast<uintptr_t>(ptr);return n>0x10000 && n%8==0;}
template<class T> inline T read(void* ptr,size_t offset){T value{};std::memcpy(&value,static_cast<char*>(ptr)+offset,sizeof(T));return value;}
enum class State {Idle,Preparing,Ready,Unsupported,BindingMismatch,HookFailed};
inline std::atomic<State> state{State::Idle};
inline std::atomic<bool> enabled{false},installed{false};
inline std::atomic<int64_t> lastDraw{0};
inline std::atomic<int> renderStage{0}; // 0 no callback, 1 no player, 2 no LRC, 3 no material, 4 drawing
inline std::atomic<int> matchingBindings{0}; // largest complete prefix of audited signatures
inline std::atomic<uint64_t> generation{0};
inline std::atomic<int> lyricDistanceTenths{22},lyricScatterFov{100};
inline std::atomic<int> lyricHeightTenths{13};
inline std::mutex lifecycle,lyricsMutex,poseMutex;
inline std::thread installer;
inline const Profile* profile=nullptr;
struct Pose{float pitch=0,yaw=0;uintptr_t player=0,world=0;int64_t time=0;Vec3 position{};};
inline Pose pose;
inline std::string currentLine;
inline uint64_t lyricSerial=0;
using TickFn=void(*)(void*);
using RenderFn=void(*)(void*,void*,void*);
using TessBegin=void(*)(void*,void*,int,int,int);
using TessColor=void(*)(void*,float,float,float,float);
using TessVertex=void(*)(void*,float,float,float);
using RenderMesh=void(*)(void*,void*,void*,char*);
inline TickFn originalTick=nullptr;
inline RenderFn originalRender=nullptr;
inline TessBegin tessBegin=nullptr;
inline TessColor tessColor=nullptr;
inline TessVertex tessVertex=nullptr;
inline RenderMesh renderMesh=nullptr;
inline void* tickTarget=nullptr;
inline void* renderTarget=nullptr;
inline void* materialGroup=nullptr;
inline bool tickHooked=false,renderHooked=false;
struct HashedString {
    uint64_t hash=0;std::string value;mutable const HashedString* lastMatch=nullptr;
    explicit HashedString(std::string_view s):value(s){
        if(s.empty())return;
        hash=0xCBF29CE484222325ULL;
        for(unsigned char c:s)hash=static_cast<uint64_t>(c)^(hash*0x100000001B3ULL);
    }
};
using MaterialPtr=std::shared_ptr<void>;
inline MaterialPtr material;
inline int64_t materialRetry=0;
inline void resolveMaterial(int64_t now){
    if(material||now<materialRetry||!valid(materialGroup))return;
    materialRetry=now+1000;
    auto** table=read<void**>(materialGroup,0);
    if(!valid(table)||!table[2])return;
    using Fn=MaterialPtr(*)(void*,const HashedString*);
    auto fn=reinterpret_cast<Fn>(table[2]);
    for(const char* name:{"name_tag_depth_tested","name_tag_depth_tested_with_backface","selection_box"}){
        HashedString key(name);
        material=fn(materialGroup,&key);
        if(material)break;
    }
}
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
    case 'a':return {"     ","     "," ### ","    #"," ####","#   #"," ####"};
    case 'b':return {"#    ","#    ","#### ","#   #","#   #","#   #","#### "};
    case 'c':return {"     ","     "," ####","#    ","#    ","#    "," ####"};
    case 'd':return {"    #","    #"," ####","#   #","#   #","#   #"," ####"};
    case 'e':return {"     ","     "," ### ","#   #","#####","#    "," ####"};
    case 'f':return {"  ###"," #   "," #   ","###  "," #   "," #   "," #   "};
    case 'g':return {"     "," ####","#   #","#   #"," ####","    #"," ### "};
    case 'h':return {"#    ","#    ","#### ","#   #","#   #","#   #","#   #"};
    case 'i':return {"  #  ","     "," ##  ","  #  ","  #  ","  #  "," ### "};
    case 'j':return {"   # ","     ","  ## ","   # ","   # ","#  # "," ##  "};
    case 'k':return {"#    ","#    ","#  # ","# #  ","##   ","# #  ","#  # "};
    case 'l':return {" ##  ","  #  ","  #  ","  #  ","  #  ","  #  "," ### "};
    case 'm':return {"     ","     ","## # ","# # #","# # #","# # #","# # #"};
    case 'n':return {"     ","     ","#### ","#   #","#   #","#   #","#   #"};
    case 'o':return {"     ","     "," ### ","#   #","#   #","#   #"," ### "};
    case 'p':return {"     ","#### ","#   #","#   #","#### ","#    ","#    "};
    case 'q':return {"     "," ####","#   #","#   #"," ####","    #","    #"};
    case 'r':return {"     ","     ","# ###","##   ","#    ","#    ","#    "};
    case 's':return {"     ","     "," ####","#    "," ### ","    #","#### "};
    case 't':return {" #   "," #   ","#### "," #   "," #   "," #   ","  ###"};
    case 'u':return {"     ","     ","#   #","#   #","#   #","#   #"," ####"};
    case 'v':return {"     ","     ","#   #","#   #","#   #"," # # ","  #  "};
    case 'w':return {"     ","     ","#   #","# # #","# # #","# # #"," # # "};
    case 'x':return {"     ","     ","#   #"," # # ","  #  "," # # ","#   #"};
    case 'y':return {"     ","#   #","#   #","#   #"," ####","    #"," ### "};
    case 'z':return {"     ","     ","#####","   # ","  #  "," #   ","#####"};
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
inline std::string latinDisplay(std::string_view s){
    std::string out;
    for(unsigned char c:s){
        if(out.size()>=96)break;
        if(c>=128){if((c&0xC0)!=0x80)out+='?';continue;}
        if(c=='\n'||c=='\r'||c=='\t')out+=' ';
        else if(c>=32&&c<127)out+=static_cast<char>(c);
    }
    return out;
}
struct Pixel{float x,y;};
inline std::vector<Pixel> glyphs(std::string_view text){
    std::vector<Pixel> result;result.reserve(2200);
    // Two bounded rows, up to 32 Latin glyphs in each.
    for(int row=0;row<2&&!text.empty();++row){
        size_t n=std::min<size_t>(32,text.size());
        if(text.size()>n){auto space=text.rfind(' ',n);if(space!=text.npos&&space>12)n=space;}
        const auto slice=text.substr(0,n);
        const float left=-float(slice.size()*6-1)*.5f;
        for(size_t k=0;k<slice.size();++k){
            auto map=bitmap(slice[k]);
            for(int y=0;y<7;++y)for(int x=0;x<5;++x)
                if(map[y][x]=='#')result.push_back({left+float(k*6+x),float(row?-8:2)+float(6-y)});
        }
        text.remove_prefix(n);
        while(!text.empty()&&text.front()==' ')text.remove_prefix(1);
    }
    return result;
}
inline void publish(std::string_view value){
    std::lock_guard lock(lyricsMutex);
    auto bounded=value.substr(0,512);
    if(currentLine==bounded)return;
    currentLine=bounded;++lyricSerial;lastDraw=0;
}
inline void tickHook(void* player){
    if(originalTick)originalTick(player);
    if(!enabled.load(std::memory_order_relaxed)||!profile||!valid(player))return;
    auto* rotation=read<void*>(player,profile->rotation);
    if(!valid(rotation))return;
    const float pitch=read<float>(rotation,0),yaw=read<float>(rotation,4);
    if(!std::isfinite(pitch)||!std::isfinite(yaw)||std::abs(pitch)>180.f)return;
    // The state-vector component precedes the rotation component by 0x10
    // in the audited Android player layouts (World Analysis Actor offsets).
    auto* stateVector=read<void*>(player,profile->rotation-0x10);
    if(!valid(stateVector))return;
    Vec3 position=read<Vec3>(stateVector,0);
    if(!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z)
       ||magnitude2(position)>1.e14f)return;
    Pose next{pitch,std::remainder(yaw,360.f),reinterpret_cast<uintptr_t>(player),
              read<uintptr_t>(player,profile->level),nowMs(),position};
    std::unique_lock lock(poseMutex,std::try_to_lock);
    if(lock)pose=next;
}
struct Panel{
    Vec3 anchor{},right{},facing{};
    std::vector<Pixel> pixels;
    int64_t born=0;
};
struct Scene{
    uint64_t serial=0,generation=0;
    uintptr_t world=0,player=0,renderer=0;
    Vec3 origin{};
    int64_t lastFrame=0,previousFadeStart=0,previousUntil=0;
    std::vector<Panel> panels,previous;
};
inline Scene scene; // render thread only
inline std::vector<std::string> splitLyric(std::string_view text){
    std::vector<std::string> result;
    std::string part;
    size_t at=0;
    while(at<text.size()&&result.size()<8){
        while(at<text.size()&&text[at]==' ')++at;
        size_t end=text.find(' ',at);
        if(end==text.npos)end=text.size();
        if(end==at)break;
        std::string word(text.substr(at,std::min<size_t>(end-at,22)));
        size_t words=part.empty()?0:1;
        for(char c:part)if(c==' ')++words;
        if(!part.empty()&&(part.size()+word.size()+1>22||words>=4)){
            result.push_back(std::move(part));part.clear();
        }
        if(!part.empty())part+=' ';
        part+=word;at+=word.size();
    }
    if(!part.empty())result.push_back(std::move(part));
    return result;
}
inline Vec3 lyricAnchor(Pose view,Vec3 cam,uint64_t seed,bool thirdPerson){
    constexpr float deg=.01745329251994f;
    const uint64_t h=(seed*0x9e3779b97f4a7c15ULL)^(seed>>13);
    const float angle=thirdPerson?float(h%3601)/10.f:
        view.yaw+(float(h%1001)/1000.f-.5f)*float(lyricScatterFov.load());
    float yaw=angle*deg;
    Vec3 forward{-std::sin(yaw),0,std::cos(yaw)};
    if(thirdPerson){
        Vec3 toCamera=normalized({cam.x-view.position.x,0,cam.z-view.position.z});
        float dot=forward.x*toCamera.x+forward.z*toCamera.z;
        if(dot<-.85f){yaw+=(h&1?35.f:-35.f)*deg;forward={-std::sin(yaw),0,std::cos(yaw)};}
    }
    float distance=float(lyricDistanceTenths.load())*.1f+(thirdPerson?1.1f:0.f);
    return view.position+Vec3{0,float(lyricHeightTenths.load())*.1f,0}+forward*distance;
}
struct ColorGuard{
    float* holder;std::array<float,4> saved;
    explicit ColorGuard(float* p):holder(p){std::memcpy(saved.data(),p,16);for(int i=0;i<4;++i)p[i]=1.f;}
    ~ColorGuard(){std::memcpy(holder,saved.data(),16);}
};
inline void renderHook(void* renderer,void* ctx,void* arg){
    if(originalRender)originalRender(renderer,ctx,arg);
    if(!enabled.load(std::memory_order_relaxed)||!installed.load(std::memory_order_acquire)
       ||!profile||!valid(renderer)||!valid(ctx))return;
    renderStage=1;
    const int64_t now=nowMs();
    Pose view;
    {std::unique_lock lock(poseMutex,std::try_to_lock);if(!lock)return;view=pose;}
    if(!view.world||!view.time||now-view.time>1500)return;
    std::string text;uint64_t serial;
    {std::unique_lock lock(lyricsMutex,std::try_to_lock);if(!lock)return;text=currentLine;serial=lyricSerial;}
    if(text.empty())renderStage=2;
    auto* rp=read<void*>(renderer,profile->rendererPlayer);
    auto* tess=read<void*>(ctx,profile->tessellator);
    auto* colors=read<float*>(ctx,0x30);
    if(!valid(rp)||!valid(tess)||!valid(colors))return;
    const Vec3 cam=read<Vec3>(rp,profile->camera);
    if(!std::isfinite(cam.x)||!std::isfinite(cam.y)||!std::isfinite(cam.z)
       ||magnitude2(cam-view.position)>1024.f)return;
    resolveMaterial(now);
    if(!material){renderStage=3;return;}
    const uint64_t gen=generation.load();
    const bool reset=scene.generation!=gen||scene.world!=view.world||scene.player!=view.player
        ||scene.renderer!=reinterpret_cast<uintptr_t>(rp)||now-scene.lastFrame>1500
        ||(!scene.panels.empty()&&magnitude2(cam-scene.panels.front().anchor)>1024.f);
    if(scene.serial!=serial||reset){
        if(reset){scene.previous.clear();scene.previousUntil=0;}
        else if(scene.serial!=serial)scene.previous=std::move(scene.panels);
        scene.panels.clear();
        const bool thirdPerson=magnitude2(cam-(view.position+Vec3{0,1.6f,0}))>6.25f;
        const auto parts=splitLyric(latinDisplay(text));
        for(size_t i=0;i<parts.size();++i){
            Panel panel;
            panel.anchor=lyricAnchor(view,cam,serial*17+i*89,thirdPerson);
            panel.facing=normalized(cam-panel.anchor);
            panel.right=normalized({panel.facing.z,0,-panel.facing.x});
            panel.pixels=glyphs(parts[i]);
            panel.born=now+int64_t(i)*260;
            scene.panels.push_back(std::move(panel));
        }
        scene.origin=view.position;
        scene.serial=serial;scene.generation=gen;scene.world=view.world;
        scene.player=view.player;scene.renderer=reinterpret_cast<uintptr_t>(rp);
        if(!scene.previous.empty()){
            scene.previousFadeStart=now+(text.empty()?0:
                std::max<int64_t>(150,int64_t(scene.panels.size())*130));
            scene.previousUntil=scene.previousFadeStart+320;
        }
    }
    scene.lastFrame=now;
    const bool fading=now<scene.previousUntil&&!scene.previous.empty();
    if(scene.panels.empty()&&!fading)return;
    constexpr float scale=.011f;
    ColorGuard guard(colors);
    size_t count=0;
    for(const auto& panel:scene.panels)if(now>=panel.born)count+=panel.pixels.size();
    if(fading)for(const auto& panel:scene.previous)if(now>=panel.born)count+=panel.pixels.size();
    if(!count)return;
    tessBegin(tess,nullptr,1,int(count*20),0);
    auto draw=[&](const Panel& panel,float alpha,float lift){
        if(alpha<=0.f)return;
        const Vec3 right=panel.right,up{0,1,0};
        auto quad=[&](Vec3 tl){
            auto tr=tl+right*(scale*.88f),bl=tl-up*(scale*.88f),br=tr-up*(scale*.88f);
            for(auto p:{bl,br,tr,tl})tessVertex(tess,p.x,p.y,p.z);
        };
        for(int pass=0;pass<5;++pass){
            const bool outline=pass<4;
            tessColor(tess,outline?0.f:1.f,outline?0.f:1.f,outline?0.f:1.f,alpha*(outline?.45f:1.f));
            const float dx=pass==0?-.12f:pass==1?.12f:0.f;
            const float dy=pass==2?-.12f:pass==3?.12f:0.f;
            auto origin=panel.anchor-cam+up*lift+panel.facing*(outline?0.f:.001f);
            for(const auto& p:panel.pixels)quad(origin+right*((p.x+dx)*scale)+up*((p.y+dy)*scale));
        }
    };
    if(fading){
        float fade=std::clamp(float(scene.previousUntil-now)/320.f,0.f,1.f);
        for(const auto& panel:scene.previous)if(now>=panel.born)
            draw(panel,fade,(1.f-fade)*.12f);
    }
    for(const auto& panel:scene.panels)if(now>=panel.born){
        float appear=std::clamp(float(now-panel.born)/220.f,.05f,1.f);
        draw(panel,appear,(1.f-appear)*(-.12f));
    }
    alignas(8) char texture[0x58]{};
    renderMesh(ctx,tess,&material,texture);
    if(enabled.load()&&gen==generation.load()){renderStage=4;lastDraw=now;}
}
inline bool active(){int64_t n=lastDraw.load();return enabled.load()&&n>0&&nowMs()-n<500;}
inline const char* status(){
    if(!enabled.load())return "3D off";
    switch(state.load()){
        case State::Idle:case State::Preparing:return "Preparing 3D renderer";
        case State::Unsupported:return "3D: game renderer signatures unavailable";
        case State::BindingMismatch:return "3D: renderer bindings changed";
        case State::HookFailed:return "3D: renderer hook failed";
        case State::Ready:
            if(active())return "3D lyrics active";
            switch(renderStage.load()){
                case 1:return "3D: waiting for local player";
                case 2:return "3D: waiting for synced lyrics";
                case 3:return "3D: game material unavailable";
                case 4:return "3D: waiting for next world frame";
                default:return "3D: waiting for world renderer";
            }
    }
    return "";
}
struct Image{
    uintptr_t base=0;const ElfW(Phdr)* headers=nullptr;size_t count=0;const Profile* selected=nullptr;
    bool contains(uintptr_t at,size_t size,unsigned flags)const{
        for(size_t i=0;i<count;++i){const auto& h=headers[i];
            if(h.p_type!=PT_LOAD||(h.p_flags&flags)!=flags)continue;
            auto start=base+h.p_vaddr;
            if(at>=start&&at-start<=h.p_memsz&&size<=h.p_memsz-(at-start))return true;
        }
        return false;
    }
};
inline Image findImage(){
    Image result;
    dl_iterate_phdr([](dl_phdr_info* info,size_t,void* arg){
        if(!info->dlpi_name)return 0;
        std::string_view name(info->dlpi_name);
        if(name.substr(name.find_last_of('/')+1)!="libminecraftpe.so")return 0;
        auto& image=*static_cast<Image*>(arg);
        image={info->dlpi_addr,info->dlpi_phdr,info->dlpi_phnum,nullptr};
        for(size_t i=0;i<image.count;++i){const auto& h=image.headers[i];
            auto start=image.base+h.p_vaddr;
            if(h.p_type!=PT_NOTE||!image.contains(start,h.p_memsz,PF_R))continue;
            for(const auto& prof:profiles)
                if(matchesBuildId({reinterpret_cast<const uint8_t*>(start),size_t(h.p_memsz)},prof.buildId))
                    image.selected=&prof;
        }
        return 1;
    },&result);
    return result;
}
inline void prepare(){
    auto image=findImage();
    if(!image.headers){
        __android_log_print(ANDROID_LOG_WARN,"LeviMusicWorld3D","libminecraftpe.so not loaded; renderer cannot bind");
        state=State::Unsupported;return;
    }
    std::array<uintptr_t,BindingCount> addresses{};
    auto exact=[&](const Profile& candidate){
        for(size_t i=0;i<addresses.size();++i){
            const auto& binding=candidate.bindings[i];auto at=image.base+binding.rva;
            if(!image.contains(at,binding.pattern.size(),PF_R|PF_X)){
                __android_log_print(ANDROID_LOG_WARN,"LeviMusicWorld3D","exact binding %zu address outside executable image",i);
                return false;
            }
            if(!matchesPattern({reinterpret_cast<const uint8_t*>(at),patternBytes(binding.pattern)},binding.pattern)){
                // A second mod may already have replaced the entry trampoline.
                // With the same exact ELF Build ID, accept only when the untouched
                // body after the 16-byte hook stub still matches our audited code.
                if(i==MaterialGroup)return false; // ADRP/ADD is needed to locate the material table.
                std::string_view tail=binding.pattern;
                for(int skip=0;skip<16;++skip){
                    auto space=tail.find(' ');
                    if(space==std::string_view::npos)return false;
                    tail.remove_prefix(space+1);
                }
                if(!image.contains(at+16,patternBytes(tail),PF_R|PF_X)
                   ||!matchesPattern({reinterpret_cast<const uint8_t*>(at+16),patternBytes(tail)},tail)){
                    __android_log_print(ANDROID_LOG_WARN,"LeviMusicWorld3D","exact binding %zu differs beyond the hook stub",i);
                    return false;
                }
                __android_log_print(ANDROID_LOG_INFO,"LeviMusicWorld3D",
                    "existing hook at binding %zu on exact build %.*s; preserving hook chain",
                    i,int(candidate.name.size()),candidate.name.data());
            }
            addresses[i]=at;
        }
        return true;
    };
    const Profile* selected=nullptr;
    if(image.selected&&exact(*image.selected))selected=image.selected;
    // The Android LHolo package also resolves native signatures at runtime.
    // Accept a later hotfix only when EVERY function from one audited layout
    // still has the same unique instruction signature. Keep this off the music
    // worker and decline partial matches rather than mixing incompatible ABIs.
    if(!selected){
        for(const auto& candidate:profiles){
            std::array<uintptr_t,BindingCount> found{};
            bool complete=true;int matched=0;
            for(size_t i=0;i<found.size();++i){
                const auto& binding=candidate.bindings[i];
                auto at=pl::memory::resolveSignature(binding.pattern.data(),"libminecraftpe.so");
                if(!at||!image.contains(at,binding.pattern.size(),PF_R|PF_X)
                   ||!matchesPattern({reinterpret_cast<const uint8_t*>(at),patternBytes(binding.pattern)},binding.pattern)){
                    complete=false;break;
                }
                found[i]=at;++matched;
            }
            matchingBindings=std::max(matchingBindings.load(),matched);
            __android_log_print(ANDROID_LOG_INFO,"LeviMusicWorld3D",
                "profile %.*s: %d/%zu signatures verified, exact ELF ID=%d",
                int(candidate.name.size()),candidate.name.data(),matched,found.size(),int(image.selected==&candidate));
            if(complete){selected=&candidate;addresses=found;break;}
        }
        if(!selected){
            __android_log_print(ANDROID_LOG_WARN,"LeviMusicWorld3D",
                "no complete render profile: %d/%zu matching signatures, exact ID=%d",
                matchingBindings.load(),addresses.size(),int(image.selected!=nullptr));
            state=image.selected?State::BindingMismatch:State::Unsupported;return;
        }
    }
    // World Analysis resolves the material group from ADRP x0 / ADD x0
    // at the verified common-material function, then skips 32 bytes.
    auto at=addresses[MaterialGroup];uint32_t a=0,b=0;
    std::memcpy(&a,reinterpret_cast<void*>(at),4);std::memcpy(&b,reinterpret_cast<void*>(at+4),4);
    if((a&0x9F00001F)!=0x90000000||(b&0xFF8003FF)!=0x91000000){state=State::BindingMismatch;return;}
    int64_t imm=((a>>5)&0x7FFFF)*4+((a>>29)&3);
    if(imm&(1<<20))imm-=(1<<21);
    auto group=uintptr_t(int64_t(at&~uintptr_t(0xfff))+imm*4096)
        +(((b>>10)&0xfff)<<((b&0x400000)?12:0))+32;
    if(!image.contains(group,sizeof(void*),PF_R)){state=State::BindingMismatch;return;}
    profile=selected;materialGroup=reinterpret_cast<void*>(group);
    tessBegin=reinterpret_cast<TessBegin>(addresses[Begin]);
    tessColor=reinterpret_cast<TessColor>(addresses[Color]);
    tessVertex=reinterpret_cast<TessVertex>(addresses[Vertex]);
    renderMesh=reinterpret_cast<RenderMesh>(addresses[Mesh]);
    tickTarget=reinterpret_cast<void*>(addresses[Tick]);
    renderTarget=reinterpret_cast<void*>(addresses[Render]);
    if(pl::memory::hook(tickTarget,reinterpret_cast<void*>(&tickHook),
                        reinterpret_cast<void**>(&originalTick))!=0){state=State::HookFailed;return;}
    tickHooked=true;
    if(pl::memory::hook(renderTarget,reinterpret_cast<void*>(&renderHook),
                        reinterpret_cast<void**>(&originalRender))!=0){state=State::HookFailed;return;}
    renderHooked=true;installed.store(true,std::memory_order_release);state=State::Ready;
    __android_log_print(ANDROID_LOG_INFO,"LeviMusicWorld3D","world renderer ready: %s (%s)",profile->name.data(),image.selected?"exact build":"matching hotfix signatures");
}
inline void request(bool value){
    if(enabled.exchange(value)!=value){++generation;lastDraw=0;renderStage=0;}
    if(!value)return;
    std::lock_guard lock(lifecycle);
    if(state.load()!=State::Idle)return;
    state=State::Preparing;
    installer=std::thread([]{
        try{prepare();}catch(...){state=State::HookFailed;}
        if(state.load()!=State::Ready)
            __android_log_print(ANDROID_LOG_WARN,"LeviMusicWorld3D","renderer unavailable (%d)",int(state.load()));
    });
}
inline void shutdown(){
    request(false);publish("");
    std::lock_guard lock(lifecycle);
    if(installer.joinable())installer.join();
}
inline void unload(){
    shutdown();
    if(renderHooked)pl::memory::unhook(renderTarget,reinterpret_cast<void*>(&renderHook));
    if(tickHooked)pl::memory::unhook(tickTarget,reinterpret_cast<void*>(&tickHook));
    installed=false;
}
} // namespace lm_world3d


