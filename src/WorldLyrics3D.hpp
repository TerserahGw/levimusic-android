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
inline std::atomic<int> lyricVerticalRangeTenths{8},lyricFontPercent{115};
inline std::atomic<bool> lyricTrailFollow{false},experimentalBeats{false};
inline std::atomic<float> beatStrength{0.f};
inline std::atomic<int64_t> beatUpdated{0};
inline std::atomic<int> beatState{0}; // 0 off, 1 needs permission, 2 waiting for audio, 3 receiving audio
inline std::atomic<int> cueStartMs{0},cueEndMs{0},cuePositionMs{0};
inline std::atomic<bool> cuePlaying{false};
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
struct Pixel{float x,y;uint8_t word=0;};
inline std::vector<Pixel> glyphs(std::string_view text){
    std::vector<Pixel> result;result.reserve(2200);
    // Two bounded rows, up to 32 Latin glyphs in each.
    for(int row=0;row<2&&!text.empty();++row){
        size_t n=std::min<size_t>(32,text.size());
        if(text.size()>n){auto space=text.rfind(' ',n);if(space!=text.npos&&space>12)n=space;}
        const auto slice=text.substr(0,n);
        const float left=-float(slice.size()*6-1)*.5f;
        uint8_t word=0;
        for(size_t k=0;k<slice.size();++k){
            if(slice[k]==' '){++word;continue;}
            auto map=bitmap(slice[k]);
            for(int y=0;y<7;++y)for(int x=0;x<5;++x)
                if(map[y][x]=='#')result.push_back({left+float(k*6+x),float(row?-8:2)+float(6-y),word});
        }
        text.remove_prefix(n);
        while(!text.empty()&&text.front()==' ')text.remove_prefix(1);
    }
    return result;
}
inline void publish(std::string_view value,int start=0,int end=0,int position=0,bool playing=false){
    cueStartMs=std::max(0,start);cueEndMs=std::max(start+1,end);
    cuePositionMs=std::max(0,position);cuePlaying=playing;
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
    Vec3 spawnPlayer{};
    std::vector<Pixel> pixels;
    int64_t born=0;
    int partIndex=0,partCount=1;
    int cueStart=0,cueEnd=1;
    size_t letters=0;
    float width=0;
    std::array<uint8_t,8> wordLengths{};
};
struct Scene{
    uint64_t serial=0,generation=0;
    uintptr_t world=0,player=0,renderer=0;
    Vec3 origin{};
    Vec3 lastPlayer{};
    Vec3 filteredPlayer{};
    int64_t lastFrame=0,previousFadeStart=0,previousUntil=0;
    std::vector<Panel> panels,previous;
};
inline Scene scene; // render thread only
inline std::vector<std::string> splitLyric(std::string_view text){
    std::vector<std::string> result;
    std::string part;
    size_t at=0;
    while(at<text.size()&&result.size()<16){
        while(at<text.size()&&text[at]==' ')++at;
        size_t end=text.find(' ',at);
        if(end==text.npos)end=text.size();
        if(end==at)break;
        const size_t room=part.empty()?10:(part.size()>=9?0:9-part.size());
        if(!part.empty() && (end-at>room || room==0)){
            result.push_back(std::move(part));part.clear();continue;
        }
        const size_t taken=std::min<size_t>(end-at,10);
        std::string word(text.substr(at,taken));
        if(!part.empty())part+=' ';
        part+=word;at+=taken;
    }
    if(!part.empty())result.push_back(std::move(part));
    return result;
}
inline float smoothstep(float t){t=std::clamp(t,0.f,1.f);return t*t*(3.f-2.f*t);}
inline float cueProgress(int position,int start,int end){
    return std::clamp(float(position-start)/float(std::max(end-start,1)),0.f,1.f);
}
inline float cueFade(int position,int start,int end){
    const int window=std::clamp((end-start)/5,450,1100);
    return 1.f-smoothstep(float(position-(end-window))/float(window+500));
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
    const int range=std::clamp(lyricVerticalRangeTenths.load(),0,15);
    const uint64_t vertical=((seed^0xd1b54a32d192ed03ULL)*0x94d049bb133111ebULL)>>24;
    const float offset=range?float(int(vertical%uint64_t(2*range+1))-range)*.1f:0.f;
    return view.position+Vec3{0,float(lyricHeightTenths.load())*.1f+offset,0}+forward*distance;
}
// Compare directions on the camera image as well as physical separation: panels
// behind one another are unreadable even when their anchors are far apart.
inline float panelClearance(Vec3 candidate,float width,Vec3 cam,Vec3 otherAnchor,float otherWidth){
    Vec3 a=candidate-cam,b=otherAnchor-cam;
    float da=std::sqrt(magnitude2(a)),db=std::sqrt(magnitude2(b));
    if(da<.01f||db<.01f)return -1.f;
    float horizontal=std::abs(a.x/da-b.x/db)+std::abs(a.z/da-b.z/db);
    float vertical=std::abs(a.y/da-b.y/db);
    const float minH=(width+otherWidth)*.5f/std::min(da,db)+.07f;
    const float minV=.18f/std::min(da,db)+.035f;
    return std::max(horizontal/minH,vertical/minV);
}
inline float panelClearance(Vec3 candidate,float width,Vec3 cam,const Panel& other){
    return panelClearance(candidate,width,cam,other.anchor,other.width);
}
inline Vec3 layoutAnchor(Vec3 base,Vec3 right,size_t index,size_t count,float step,size_t maxCols=4){
    const size_t cols=std::min(maxCols,count);
    const size_t rows=(count+cols-1)/cols;
    const size_t row=index/cols,col=index%cols;
    const size_t inRow=std::min(cols,count-row*cols);
    return base+right*((float(col)-float(inRow-1)*.5f)*step)
        +Vec3{0,(float(rows-1)*.5f-float(row))*.31f,0};
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
    const int start=cueStartMs.load(),end=cueEndMs.load(),position=cuePositionMs.load();
    if(scene.serial!=serial||reset){
        if(reset){scene.previous.clear();scene.previousUntil=0;}
        else if(scene.serial!=serial)scene.previous=std::move(scene.panels);
        scene.panels.clear();
        const bool thirdPerson=magnitude2(cam-(view.position+Vec3{0,1.6f,0}))>6.25f;
        const auto parts=splitLyric(latinDisplay(text));
        const float step=.2f+float(std::clamp(lyricFontPercent.load(),60,200))*.01f*.66f;
        const size_t columns=thirdPerson?4:
            lyricScatterFov.load()<50?2:lyricFontPercent.load()>145?3:4;
        Vec3 base{};float best=-1.f;
        for(size_t attempt=0;attempt<40&&!parts.empty();++attempt){
            Vec3 candidate=lyricAnchor(view,cam,serial*733+attempt*137,thirdPerson);
            Vec3 facing=normalized(cam-candidate);
            Vec3 right=normalized({facing.z,0,-facing.x});
            float score=100.f;
            if(!scene.previous.empty())for(size_t i=0;i<parts.size();++i){
                Vec3 point=layoutAnchor(candidate,right,i,parts.size(),step,columns);
                float width=float(parts[i].size())*6.f*.011f*float(lyricFontPercent.load())*.01f;
                for(const auto& old:scene.previous){
                    Vec3 occupied=old.anchor;
                    if(lyricTrailFollow.load()){
                        const float progress=cueProgress(position,old.cueStart,old.cueEnd);
                        occupied=occupied+
                            (scene.filteredPlayer-old.spawnPlayer)*(.83f-.62f*progress)
                            +Vec3{0,.09f*progress+.85f*progress*progress,0};
                    }
                    score=std::min(score,panelClearance(point,width,cam,occupied,old.width));
                }
            }
            if(score>best){best=score;base=candidate;}
            if(score>=1.15f)break;
        }
        Vec3 facing=normalized(cam-base);
        Vec3 right=normalized({facing.z,0,-facing.x});
        int duration=std::clamp(end-start,600,12000);
        int spacing=std::clamp(duration/int(std::max<size_t>(1,parts.size()+1)),55,230);
        for(size_t i=0;i<parts.size();++i){
            Panel panel;
            panel.partIndex=int(i);panel.partCount=int(parts.size());
            panel.cueStart=start;panel.cueEnd=end;
            panel.letters=parts[i].size();
            size_t word=0;
            for(char c:parts[i]){
                if(c==' '){word=std::min<size_t>(word+1,7);continue;}
                if(panel.wordLengths[word]<255)++panel.wordLengths[word];
            }
            panel.width=float(panel.letters)*6.f*.011f*float(lyricFontPercent.load())*.01f;
            panel.anchor=layoutAnchor(base,right,i,parts.size(),step,columns);
            panel.spawnPlayer=view.position;
            panel.facing=normalized(cam-panel.anchor);
            panel.right=normalized({panel.facing.z,0,-panel.facing.x});
            panel.pixels=glyphs(parts[i]);
            panel.born=now+int64_t(i)*spacing;
            scene.panels.push_back(std::move(panel));
        }
        scene.origin=view.position;
        scene.lastPlayer=view.position;
        if(reset)scene.filteredPlayer=view.position;
        scene.serial=serial;scene.generation=gen;scene.world=view.world;
        scene.player=view.player;scene.renderer=reinterpret_cast<uintptr_t>(rp);
        if(!scene.previous.empty()){
            scene.previousFadeStart=now+(text.empty()?0:100);
            scene.previousUntil=scene.previousFadeStart+620;
        }
    }
    const float dt=std::clamp(float(now-scene.lastFrame),0.f,200.f);
    if(magnitude2(view.position-scene.filteredPlayer)<64.f){
        const float weight=1.f-std::exp(-dt/175.f);
        scene.filteredPlayer=scene.filteredPlayer+(view.position-scene.filteredPlayer)*weight;
    }else scene.filteredPlayer=view.position;
    scene.lastFrame=now;
    scene.lastPlayer=view.position;
    const bool fading=now<scene.previousUntil&&!scene.previous.empty();
    if(scene.panels.empty()&&!fading)return;
    const float baseScale=.011f*float(std::clamp(lyricFontPercent.load(),60,200))*.01f;
    const bool beats=experimentalBeats.load()&&now-beatUpdated.load()<1200;
    const float beat=beats?std::clamp(beatStrength.load(),0.f,1.f):0.f;
    ColorGuard guard(colors);
    size_t count=0;
    int currentPart=-1;
    if(cueFade(position,start,end)>0.f)
        for(const auto& panel:scene.panels)if(now>=panel.born){
            count+=panel.pixels.size();currentPart=panel.partIndex;
        }
    if(fading)for(const auto& panel:scene.previous)if(now>=panel.born)count+=panel.pixels.size();
    if(!count)return;
    tessBegin(tess,nullptr,1,int(count*20),0);
    auto draw=[&](const Panel& panel,float alpha,float lift){
        if(alpha<=0.f)return;
        const float progress=cueProgress(position,panel.cueStart,panel.cueEnd);
        const float scale=baseScale*(1.f+(beats?beat*.34f:0.f))*(beats?
            std::clamp(1.12f-float(panel.letters)*.008f,.82f,1.1f):1.f);
        // Every panel has a tiny, smooth world-space sway. Audio adds a
        // stronger transient tilt only when a real beat sample is available.
        const float t=float(now-panel.born)*.001f;
        const float sway=.028f*std::sin(t*1.8f+panel.partIndex*.45f)
                        +.012f*std::sin(t*1.13f+panel.partIndex);
        const float tilt=sway+(beats?std::sin(t*11.f+float(panel.partIndex))*.26f*beat:0.f);
        const Vec3 right=panel.right*std::cos(tilt)+Vec3{0,1,0}*std::sin(tilt);
        const Vec3 up=Vec3{0,1,0}*std::cos(tilt)-panel.right*std::sin(tilt);
        Vec3 anchor=panel.anchor;
        if(lyricTrailFollow.load()){
            float lag=std::clamp(.83f-.62f*progress,.12f,.83f);
            anchor=anchor+(scene.filteredPlayer-panel.spawnPlayer)*lag;
            // Quadratic lift accelerates through the line instead of pausing
            // after its appearance and racing at a fixed speed near its end.
            lift+=.09f*progress+.85f*progress*progress;
        }
        auto quad=[&](Vec3 tl,float glyphScale){
            auto tr=tl+right*(glyphScale*.88f),bl=tl-up*(glyphScale*.88f),br=tr-up*(glyphScale*.88f);
            for(auto p:{bl,br,tr,tl})tessVertex(tess,p.x,p.y,p.z);
        };
        for(int pass=0;pass<5;++pass){
            const bool outline=pass<4;
            const float dx=pass==0?-.12f:pass==1?.12f:0.f;
            const float dy=pass==2?-.12f:pass==3?.12f:0.f;
            auto origin=anchor-cam+up*lift+panel.facing*(outline?0.f:.001f);
            for(const auto& p:panel.pixels){
                const float accent=beats&&((p.word+panel.letters)%3!=0)?beat:0.f;
                const float wordScale=beats?std::clamp(1.14f-
                    float(panel.wordLengths[std::min<size_t>(p.word,7)])*.025f+.10f*beat,
                    .80f,1.19f):1.f;
                tessColor(tess,outline?0.f:1.f,outline?0.f:1.f-.38f*accent,
                          outline?0.f:1.f-.65f*accent,alpha*(outline?.45f:1.f));
                quad(origin+right*((p.x+dx)*scale)+up*((p.y+dy)*scale),scale*wordScale);
            }
        }
    };
    if(fading){
        float fade=1.f-smoothstep(float(now-scene.previousFadeStart)/620.f);
        for(const auto& panel:scene.previous)if(now>=panel.born)
            draw(panel,fade,(1.f-fade)*.16f);
    }
    for(const auto& panel:scene.panels)if(now>=panel.born){
        float appear=std::clamp(float(now-panel.born)/220.f,.05f,1.f);
        float focus=panel.partIndex==currentPart?1.f:.68f;
        draw(panel,appear*focus*cueFade(position,start,end),(1.f-appear)*(-.12f));
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
            if(active()){
                if(experimentalBeats.load()){
                    switch(beatState.load()){
                        case 1:return "3D active | Beats: allow audio in app Settings";
                        case 2:return "3D active | Beats: waiting for LeviMusic audio";
                        case 3:return "3D active | Beats: audio connected";
                        default:return "3D active | Beats: Bridge unavailable";
                    }
                }
                return "3D lyrics active";
            }
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


