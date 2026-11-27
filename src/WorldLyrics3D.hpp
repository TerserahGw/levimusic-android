#pragma once
// World renderer adapted from World Analysis (Copyright 2026 Ekowat),
// itself derived from BedrockTools by RadiantByte. GPL-3.0-only.
#include "WorldLyricsBindings.hpp"
#include "WordTiming.hpp"
#include "CinematicAFK.hpp"
#include "ImmersiveEnvironment.hpp"
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
#include <span>
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
inline std::atomic<int> lyricDistanceTenths{22},lyricScatterFov{60};
inline std::atomic<int> lyricHeightTenths{0};
inline std::atomic<int> lyricVerticalRangeTenths{2},lyricFontPercent{115};
inline std::atomic<int> otherDistanceTenths{18},otherHeightTenths{0};
inline std::atomic<int> otherVerticalRangeTenths{4},otherFontPercent{140};
inline std::atomic<int> otherArcSpread{220};
inline std::atomic<int> firstPlacement{0},otherPlacement{2};
inline std::atomic<bool> lyricTrailFollow{true},experimentalBeats{false};
inline std::atomic<int> lyricPalette{2}; // 0 pearl, 1 pastel, 2 rainbow, 3 neon
inline std::atomic<float> beatStrength{0.f};
inline std::atomic<int64_t> beatUpdated{0};
inline std::atomic<uint64_t> beatPulseSerial{0};
inline std::atomic<int64_t> beatPulseTime{0};
inline std::atomic<int> beatState{0}; // 0 off, 1 needs permission, 2 waiting for audio, 3 receiving audio
inline std::atomic<int> cueStartMs{0},cueEndMs{0},cuePositionMs{0};
inline std::atomic<int64_t> cueSampleAt{0};
inline std::atomic<bool> cuePlaying{false};
inline std::mutex lifecycle,lyricsMutex,poseMutex;
inline std::thread installer;
inline const Profile* profile=nullptr;
struct Pose{float pitch=0,yaw=0;uintptr_t player=0,world=0;int64_t time=0;Vec3 position{};};
inline Pose pose;
inline std::atomic<int64_t> lastPlayerTick{0};
inline std::atomic<uintptr_t> lastPlayerWorld{0};
inline std::string currentLine;
struct Pixel{float x,y;uint8_t word=0,coverage=255;};
struct GlyphMesh{std::vector<Pixel> pixels,outline;float width=0,scale=.011f;};
inline std::shared_ptr<const std::vector<GlyphMesh>> currentMeshes;
inline uint32_t currentStyle=0;
inline std::vector<lm_timing::WordCue> currentWords;
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
        // A missing glyph must not become a row of question marks in world space.
        // Romanized text is folded to ASCII before it reaches this bitmap renderer.
        if(c>=128)continue;
        if(c=='\n'||c=='\r'||c=='\t')out+=' ';
        else if(c>=32&&c<127)out+=static_cast<char>(c);
    }
    return out;
}
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
// Build two separate silhouettes: colored glyph coverage and an outer contour.
// The dark contour only occupies empty cells beside a glyph, never each pixel.
inline GlyphMesh rasterMesh(std::span<const uint8_t> alpha,int width,int height,
                           int baseline,int step=2){
    GlyphMesh mesh;
    if(width<=0||height<=0||step<=0||alpha.size()!=size_t(width)*size_t(height))return mesh;
    const int cols=(width+step-1)/step,rows=(height+step-1)/step;
    mesh.width=float(cols);mesh.scale=.00375f*float(step);
    std::vector<uint8_t> cells(size_t(cols)*size_t(rows));
    for(int cy=0;cy<rows;++cy)for(int cx=0;cx<cols;++cx){
        int sum=0,n=0;
        for(int dy=0;dy<step&&cy*step+dy<height;++dy)
            for(int dx=0;dx<step&&cx*step+dx<width;++dx){
                sum+=alpha[size_t(cy*step+dy)*size_t(width)+size_t(cx*step+dx)];++n;
            }
        cells[size_t(cy)*size_t(cols)+size_t(cx)]=uint8_t(sum/std::max(n,1));
    }
    auto pixel=[&](int x,int y,uint8_t coverage){
        return Pixel{float(x)-float(cols)*.5f,float(baseline)/float(step)-float(y),0,coverage};
    };
    for(int y=0;y<rows;++y)for(int x=0;x<cols;++x){
        const auto at=size_t(y)*size_t(cols)+size_t(x);
        const uint8_t coverage=cells[at];
        if(coverage>=12)mesh.pixels.push_back(pixel(x,y,coverage));
        if(coverage>=32)continue;
        int near=0,outer=0;
        for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx){
            int nx=x+dx,ny=y+dy;
            if(nx<0||nx>=cols||ny<0||ny>=rows)continue;
            const int neighbor=cells[size_t(ny)*size_t(cols)+size_t(nx)];
            if(std::abs(dx)<=1&&std::abs(dy)<=1)near=std::max(near,neighbor);
            else outer=std::max(outer,neighbor);
        }
        if(near>=55)mesh.outline.push_back(pixel(x,y,235));
        else if(outer>=70)mesh.outline.push_back(pixel(x,y,165));
    }
    return mesh;
}
inline void publish(std::string_view value,int start=0,int end=0,int position=0,
                    bool playing=false,std::span<const lm_timing::WordCue> words={},
                    std::span<const GlyphMesh> meshes={},uint32_t style=0){
    const int previousStart=cueStartMs.load();
    cueStartMs=std::max(0,start);cueEndMs=std::max(start+1,end);
    cuePositionMs=std::max(0,position);cuePlaying=playing;
    cueSampleAt=nowMs();
    std::lock_guard lock(lyricsMutex);
    auto bounded=value.substr(0,512);
    if(currentLine==bounded&&previousStart==std::max(0,start)&&
       currentWords.size()==words.size()&&currentStyle==style)return;
    currentLine=bounded;
    currentWords.assign(words.begin(),words.end());
    currentMeshes=meshes.empty()?nullptr:
        std::make_shared<const std::vector<GlyphMesh>>(meshes.begin(),meshes.end());
    currentStyle=style;
    ++lyricSerial;lastDraw=0;
}
inline void tickHook(void* player){
    if(originalTick)originalTick(player);
    if(!profile||!valid(player))return;
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
    const float normalizedYaw=std::remainder(yaw,360.f);
    const auto world=read<uintptr_t>(player,profile->level);
    lastPlayerTick=nowMs();lastPlayerWorld=world;
    lm_cinematic::observePlayer(player,position.x,position.y,position.z,pitch,normalizedYaw,world);
    lm_immersive::observePlayer(player,position.x,position.y,position.z);
    if(!enabled.load(std::memory_order_relaxed))return;
    Pose next{pitch,normalizedYaw,reinterpret_cast<uintptr_t>(player),
              world,nowMs(),position};
    std::unique_lock lock(poseMutex,std::try_to_lock);
    if(lock)pose=next;
}
struct Panel{
    Vec3 anchor{},right{},facing{};
    Vec3 visualAnchor{};
    Vec3 spawnPlayer{};
    std::vector<Pixel> pixels;
    std::vector<Pixel> outline;
    std::vector<float> transitionWords;
    int64_t born=0;
    int64_t appeared=0;
    float entryStrength=0.f;
    int partIndex=0,partCount=1;
    int cueStart=0,cueEnd=1;
    float visualLift=0.f;
    int fontPercent=115;
    float pixelScale=.011f;
    int wordCount=1;
    int wordBase=0;
    int sourceWord=-1;
    float visibleAlpha=0.f,transitionAlpha=0.f;
    size_t letters=0;
    float width=0;
};
struct Scene{
    uint64_t serial=0,generation=0;
    uintptr_t world=0,player=0,renderer=0;
    Vec3 origin{};
    Vec3 lastPlayer{};
    Vec3 filteredPlayer{};
    uint64_t lastBeatPulse=0;
    float cueClock=0.f;
    int64_t lastFrame=0,previousFadeStart=0,previousUntil=0;
    int previousStagger=0;
    std::vector<Panel> panels,previous;
    std::vector<lm_timing::WordCue> words;
    int displayWords=0;
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
inline std::vector<std::string> splitTimedLyric(std::string_view text){
    std::vector<std::string> parts;
    for(size_t at=0;at<text.size()&&parts.size()<32;){
        while(at<text.size()&&text[at]==' ')++at;
        if(at==text.size())break;
        size_t end=text.find(' ',at);
        if(end==text.npos)end=text.size();
        while(at<end&&parts.size()<32){
            const size_t length=std::min<size_t>(10,end-at);
            parts.emplace_back(text.substr(at,length));
            at+=length;
        }
    }
    return parts;
}
inline std::vector<std::string> splitUnicodeLyric(std::string_view text){
    std::vector<std::string> parts;
    for(size_t at=0;at<text.size()&&parts.size()<32;){
        while(at<text.size()&&text[at]==' ')++at;
        if(at==text.size())break;
        const size_t end=text.find(' ',at);
        const size_t limit=end==text.npos?text.size():end;
        while(at<limit&&parts.size()<32){
            const size_t begin=at;
            for(size_t count=0;count<10&&at<limit;++count){
                ++at;
                while(at<limit&&(static_cast<unsigned char>(text[at])&0xC0)==0x80)++at;
            }
            parts.emplace_back(text.substr(begin,at-begin));
        }
    }
    return parts;
}
inline std::vector<std::string> unicodeParts(std::string_view text,
                                             std::span<const lm_timing::WordCue> words){
    if(text.find(' ')==text.npos&&words.size()>1&&words.size()<=32){
        std::string joined;
        std::vector<std::string> parts;
        for(const auto& word:words){
            joined+=word.text;
            if(lm_timing::utf8Length(word.text)>10)return splitUnicodeLyric(text);
            parts.push_back(word.text);
        }
        if(joined==text)return parts;
    }
    return splitUnicodeLyric(text);
}
inline float smoothstep(float t){t=std::clamp(t,0.f,1.f);return t*t*(3.f-2.f*t);}
inline float timedWordAlpha(const Panel& panel,std::span<const lm_timing::WordCue> words,
                            int displayWords,float position,int newestWord,uint8_t localWord){
    if(words.empty())return 1.f;
    const int index=displayWords==1?panel.sourceWord:
        lm_timing::sourceWordIndex(panel.wordBase+int(localWord),displayWords,int(words.size()));
    if(index<0)return 1.f;
    const auto& word=words[size_t(index)];
    if(position<float(word.startMs))return 0.f;
    const float entry=std::max(.03f,smoothstep((position-float(word.startMs))/160.f));
    float trail=1.f;
    if(index+4<int(words.size())&&newestWord-index>=4)
        trail=1.f-smoothstep((position-float(words[size_t(index+4)].startMs))/380.f);
    return entry*(position<float(word.endMs)?1.f:.84f)*trail;
}
inline float cueProgress(float position,int start,int end){
    return std::clamp(float(position-start)/float(std::max(end-start,1)),0.f,1.f);
}
inline float cueFade(float position,int start,int end){
    (void)start;
    return 1.f-smoothstep((position-float(end))/650.f);
}
inline float transitionFade(int64_t now,int64_t start,int index,int stagger){
    return 1.f-smoothstep(float(now-start-int64_t(index)*stagger)/620.f);
}
inline float riseForProgress(float progress){
    progress=std::clamp(progress,0.f,1.f);
    return .08f*progress+.54f*progress*progress;
}
inline float advanceCueClock(float current,float observed,float dt,bool playing){
    if(!playing)return observed;
    if(std::abs(observed-current)>1200.f)return observed; // intentional seek
    const float next=current+(playing?dt:0.f);
    return next+std::clamp(observed-next,-dt*.18f,dt*.18f);
}
inline bool shouldReveal(int64_t now,int64_t due,bool beats,bool freshPulse){
    return now>=due&&(!beats||freshPulse||now-due>=240);
}
inline std::array<float,3> lyricTint(int part,int word){
    const int palette=lyricPalette.load();
    static constexpr std::array<std::array<float,3>,6> pastel{{
        {1.f,.83f,.89f},{.78f,.91f,1.f},{.85f,1.f,.86f},
        {1.f,.92f,.72f},{.90f,.83f,1.f},{.80f,1.f,.96f}}};
    static constexpr std::array<std::array<float,3>,6> rainbow{{
        {1.f,.39f,.48f},{1.f,.69f,.35f},{1.f,.94f,.39f},
        {.44f,1.f,.65f},{.44f,.78f,1.f},{.83f,.57f,1.f}}};
    static constexpr std::array<std::array<float,3>,6> neon{{
        {.45f,1.f,.96f},{1.f,.56f,.87f},{.68f,.64f,1.f},
        {1.f,.91f,.42f},{.55f,1.f,.54f},{1.f,.55f,.57f}}};
    const auto at=static_cast<size_t>((part*3+word)%6);
    if(palette==1)return pastel[at];
    if(palette==2)return rainbow[at];
    if(palette==3)return neon[at];
    if((part*3+word)%5!=2)return {1.f,1.f,1.f};
    return pastel[at];
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
    float distance=float(thirdPerson?otherDistanceTenths.load():lyricDistanceTenths.load())*.1f;
    const int range=std::clamp(thirdPerson?otherVerticalRangeTenths.load():
        lyricVerticalRangeTenths.load(),0,15);
    const uint64_t vertical=((seed^0xd1b54a32d192ed03ULL)*0x94d049bb133111ebULL)>>24;
    const float offset=range?float(int(vertical%uint64_t(2*range+1))-range)*.1f:0.f;
    const int height=thirdPerson?otherHeightTenths.load():lyricHeightTenths.load();
    return view.position+Vec3{0,float(height)*.1f+offset,0}+forward*distance;
}
// Compare directions on the camera image as well as physical separation: panels
// behind one another are unreadable even when their anchors are far apart.
inline float panelClearance(Vec3 candidate,float width,Vec3 cam,Vec3 otherAnchor,float otherWidth){
    Vec3 a=candidate-cam,b=otherAnchor-cam;
    float da=std::sqrt(magnitude2(a)),db=std::sqrt(magnitude2(b));
    if(da<.01f||db<.01f)return -1.f;
    float horizontal=std::hypot(a.x/da-b.x/db,a.z/da-b.z/db);
    float vertical=std::abs(a.y/da-b.y/db);
    // Compare projected extents, including each word's actual raster width.
    // The vertical allowance covers a full glyph, not only its baseline.
    const float minH=width*.5f/da+otherWidth*.5f/db+.07f;
    const float minV=.22f/da+.22f/db+.035f;
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
// Non-first-person lyrics travel clockwise around the player. Each line may
// start anywhere in 360 degrees, while its own pieces keep an obvious order.
inline Vec3 orbitAnchor(Pose view,Vec3 base,size_t index,size_t count){
    if(count<=1)return base;
    const float deg=.01745329251994f;
    const float center=std::atan2(-(base.x-view.position.x),base.z-view.position.z);
    const float arc=float(std::min<int>(otherArcSpread.load(),
        std::max<int>(40,int(count-1)*26)))*deg;
    const float fraction=float(index)/float(count-1);
    const float yaw=center+(fraction-.5f)*arc;
    const float distance=float(otherDistanceTenths.load())*.1f;
    const float baseY=view.position.y+float(otherHeightTenths.load())*.1f;
    const float range=float(std::clamp(otherVerticalRangeTenths.load(),0,15))*.1f;
    const float offset=std::clamp((base.y-baseY)*.35f+(.5f-fraction)*
        std::min(range*1.2f,.9f),-range,range);
    return {view.position.x-std::sin(yaw)*distance,baseY+offset,
            view.position.z+std::cos(yaw)*distance};
}
inline Vec3 scatteredAnchor(Pose view,Vec3 base,size_t index,size_t count,int mode,uint64_t seed){
    if(count<=1)return base;
    constexpr float deg=.01745329251994f;
    const float center=std::atan2(-(base.x-view.position.x),base.z-view.position.z);
    const float fraction=float(index)/float(count);
    // Ordered clockwise positions, but each lyric starts in a new world sector.
    const float jitter=mode==2?std::sin(float(seed%997+index*163)*.71f)*.24f:0.f;
    const float yaw=center+(fraction+(jitter/float(count)))*360.f*deg;
    const float radial=mode==2?std::sin(float(seed%701+index*97)*.83f)*.38f:0.f;
    const float height=view.position.y+float(otherHeightTenths.load())*.1f;
    const float range=float(otherVerticalRangeTenths.load())*.1f;
    const float lift=mode==2?std::sin(float(index)*1.17f+float(seed%37))*
        std::min(range,.55f):(.5f-fraction)*std::min(range*1.3f,.9f);
    const float distance=float(otherDistanceTenths.load())*.1f+radial;
    return {view.position.x-std::sin(yaw)*distance,height+lift,
            view.position.z+std::cos(yaw)*distance};
}
inline Vec3 firstFanAnchor(Pose view,Vec3 base,size_t index,size_t count){
    if(count<=1)return base;
    constexpr float deg=.01745329251994f;
    const float center=std::atan2(-(base.x-view.position.x),base.z-view.position.z);
    const float arc=float(std::min(lyricScatterFov.load(),int((count-1)*15)))*deg;
    const float fraction=float(index)/float(count-1);
    const float yaw=center+(fraction-.5f)*arc;
    const float distance=float(lyricDistanceTenths.load())*.1f;
    return {view.position.x-std::sin(yaw)*distance,
            base.y+(.5f-fraction)*std::min(.7f,float(lyricVerticalRangeTenths.load())*.1f),
            view.position.z+std::cos(yaw)*distance};
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
    std::string text;std::vector<lm_timing::WordCue> words;
    std::shared_ptr<const std::vector<GlyphMesh>> meshes;uint64_t serial;
    {std::unique_lock lock(lyricsMutex,std::try_to_lock);if(!lock)return;
     text=currentLine;words=currentWords;meshes=currentMeshes;serial=lyricSerial;}
    if(text.empty())renderStage=2;
    auto* rp=read<void*>(renderer,profile->rendererPlayer);
    auto* tess=read<void*>(ctx,profile->tessellator);
    auto* colors=read<float*>(ctx,0x30);
    if(!valid(rp)||!valid(tess)||!valid(colors))return;
    Vec3 cam=read<Vec3>(rp,profile->camera);
    lm_cinematic::effectiveCamera(cam.x,cam.y,cam.z);
    if(!std::isfinite(cam.x)||!std::isfinite(cam.y)||!std::isfinite(cam.z)
       ||magnitude2(cam-view.position)>1024.f)return;
    resolveMaterial(now);
    if(!material){renderStage=3;return;}
    const uint64_t gen=generation.load();
    const bool reset=scene.generation!=gen||scene.world!=view.world||scene.player!=view.player
        ||scene.renderer!=reinterpret_cast<uintptr_t>(rp)||now-scene.lastFrame>1500
        ||(!scene.panels.empty()&&magnitude2(cam-scene.panels.front().anchor)>1024.f);
    const int start=cueStartMs.load(),end=cueEndMs.load();
    const bool playing=cuePlaying.load();
    const int64_t sampleAge=std::clamp<int64_t>(now-cueSampleAt.load(),0,1200);
    const float observed=float(cuePositionMs.load())+(playing?float(sampleAge):0.f);
    float position=observed;
    const bool newCue=scene.serial!=serial||reset;
    if(newCue){
        if(reset){scene.previous.clear();scene.previousUntil=0;}
        else if(scene.serial!=serial){
            int latest=-1;
            for(size_t i=0;i<scene.words.size();++i)
                if(scene.cueClock>=float(scene.words[i].startMs))latest=int(i);
            for(auto& old:scene.panels){
                old.transitionWords.resize(size_t(std::max(old.wordCount,1)));
                for(size_t i=0;i<old.transitionWords.size();++i)
                    old.transitionWords[i]=timedWordAlpha(old,scene.words,
                        scene.displayWords,scene.cueClock,latest,uint8_t(i));
            }
            scene.previous=std::move(scene.panels);
        }
        scene.panels.clear();
        const bool thirdPerson=magnitude2(cam-(view.position+Vec3{0,1.6f,0}))>6.25f;
        const std::string display=meshes?text:latinDisplay(text);
        scene.words=std::move(words);
        const auto parts=meshes?unicodeParts(display,scene.words):
            scene.words.empty()?splitLyric(display):splitTimedLyric(display);
        std::vector<int> wordAt(display.size());
        int wordIndex=-1;bool inside=false;
        for(size_t k=0;k<display.size();++k){
            if(display[k]==' '){inside=false;continue;}
            if(!inside){++wordIndex;inside=true;}
            wordAt[k]=wordIndex;
        }
        scene.displayWords=wordIndex+1;
        const int fontPercent=std::clamp(thirdPerson?otherFontPercent.load():
            lyricFontPercent.load(),60,200);
        const float step=.2f+float(fontPercent)*.01f*.66f;
        const size_t columns=lyricScatterFov.load()<50?2:fontPercent>145?3:4;
        const int mode=thirdPerson?otherPlacement.load():firstPlacement.load();
        const uint64_t layoutSeed=serial*733;
        auto place=[&](Vec3 candidate,Vec3 right,size_t index){
            if(thirdPerson)return mode==0?orbitAnchor(view,candidate,index,parts.size()):
                scatteredAnchor(view,candidate,index,parts.size(),mode,layoutSeed);
            if(mode==1)return firstFanAnchor(view,candidate,index,parts.size());
            if(mode==2){
                const size_t row=index/columns;
                auto center=layoutAnchor(candidate,right,index,parts.size(),step,columns);
                return center+right*(row%2==0?.17f:-.17f)*step+
                    Vec3{0,std::sin(float(layoutSeed%101+row*31)*.47f)*.09f,0};
            }
            return layoutAnchor(candidate,right,index,parts.size(),step,columns);
        };
        Vec3 base{};float best=-1.f;
        for(size_t attempt=0;attempt<40&&!parts.empty();++attempt){
            Vec3 candidate=lyricAnchor(view,cam,serial*733+attempt*137,thirdPerson);
            Vec3 facing=normalized(cam-candidate);
            Vec3 right=normalized({facing.z,0,-facing.x});
            float score=100.f;
            if(!scene.previous.empty())for(size_t i=0;i<parts.size();++i){
                Vec3 point=place(candidate,right,i);
                float width=(meshes&&i<meshes->size()?(*meshes)[i].width*(*meshes)[i].scale:
                    float(parts[i].size())*6.f*.011f)*float(fontPercent)*.01f;
                for(const auto& old:scene.previous){
                    Vec3 occupied=old.visualAnchor;
                    if(lyricTrailFollow.load()){
                        occupied=occupied+Vec3{0,old.visualLift,0};
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
        size_t partCursor=0;
        for(size_t i=0;i<parts.size();++i){
            Panel panel;
            panel.partIndex=int(i);panel.partCount=int(parts.size());
            panel.cueStart=start;panel.cueEnd=end;
            panel.fontPercent=fontPercent;
            panel.letters=lm_timing::utf8Length(parts[i]);
            panel.wordCount=int(std::count(parts[i].begin(),parts[i].end(),' '))+1;
            const size_t partAt=display.find(parts[i],partCursor);
            if(partAt!=display.npos){
                panel.wordBase=wordAt[partAt];
                partCursor=partAt+parts[i].size();
            }
            if(!scene.words.empty())
                panel.sourceWord=lm_timing::sourceWordIndex(panel.wordBase,
                    scene.displayWords,int(scene.words.size()));
            if(scene.displayWords==1&&scene.words.size()>1&&parts.size()>1)
                panel.sourceWord=std::min(int(scene.words.size())-1,
                    int(i*scene.words.size()/parts.size()));
            const bool raster=meshes&&i<meshes->size()&&!(*meshes)[i].pixels.empty();
            panel.pixelScale=raster?(*meshes)[i].scale:.011f;
            panel.width=(raster?(*meshes)[i].width:
                float(panel.letters)*6.f)*panel.pixelScale*float(fontPercent)*.01f;
            panel.anchor=place(base,right,i);
            panel.visualAnchor=panel.anchor;
            panel.spawnPlayer=view.position;
            panel.facing=normalized(cam-panel.anchor);
            panel.right=normalized({panel.facing.z,0,-panel.facing.x});
            panel.pixels=raster?(*meshes)[i].pixels:glyphs(parts[i]);
            if(raster)panel.outline=(*meshes)[i].outline;
            panel.born=scene.words.empty()?now+int64_t(i)*spacing:now;
            scene.panels.push_back(std::move(panel));
        }
        scene.origin=view.position;
        scene.lastPlayer=view.position;
        if(reset)scene.filteredPlayer=view.position;
        scene.cueClock=observed;
        scene.serial=serial;scene.generation=gen;scene.world=view.world;
        scene.player=view.player;scene.renderer=reinterpret_cast<uintptr_t>(rp);
        if(!scene.previous.empty()){
            scene.previousFadeStart=now+(text.empty()?0:80);
            scene.previousStagger=std::clamp(850/int(scene.previous.size()),55,150);
            int64_t lastWord=0;
            for(auto& old:scene.previous){
                old.transitionAlpha=old.visibleAlpha;
                lastWord=std::max(lastWord,int64_t(old.partIndex)*scene.previousStagger+
                    int64_t(old.wordCount-1)*65);
            }
            scene.previousUntil=scene.previousFadeStart+lastWord+620;
        }
    }
    const float dt=std::clamp(float(now-scene.lastFrame),0.f,200.f);
    if(!newCue)scene.cueClock=advanceCueClock(scene.cueClock,observed,dt,playing);
    position=scene.cueClock;
    if(magnitude2(view.position-scene.filteredPlayer)<64.f){
        const float weight=1.f-std::exp(-dt/175.f);
        scene.filteredPlayer=scene.filteredPlayer+(view.position-scene.filteredPlayer)*weight;
    }else scene.filteredPlayer=view.position;
    scene.lastFrame=now;
    scene.lastPlayer=view.position;
    const bool beats=experimentalBeats.load()&&now-beatUpdated.load()<1200;
    const float beat=beats?std::clamp(beatStrength.load(),0.f,1.f):0.f;
    const uint64_t pulse=beatPulseSerial.load();
    bool freshPulse=beats&&pulse!=scene.lastBeatPulse&&now-beatPulseTime.load()<240;
    if(pulse!=scene.lastBeatPulse&&!freshPulse)scene.lastBeatPulse=pulse;
    const float followDt=1.f-std::exp(-dt*.001f*7.f);
    const float liftDt=1.f-std::exp(-dt/135.f);
    int newestWord=-1;
    for(size_t i=0;i<scene.words.size();++i)
        if(position>=float(scene.words[i].startMs))newestWord=int(i);
    auto follow=[&](std::vector<Panel>& panels){for(auto& panel:panels){
        if(lyricTrailFollow.load()){
            const float age=cueProgress(position,panel.cueStart,panel.cueEnd);
            // The target follows the player. Older lyrics have a weaker spring
            // and a lower target gain, so they visibly trail without jitter.
            const float gain=1.f-.32f*age;
            const Vec3 target=panel.anchor+(scene.filteredPlayer-panel.spawnPlayer)*gain;
            panel.visualAnchor=panel.visualAnchor+(target-panel.visualAnchor)*
                (followDt*(1.f-.48f*age));
            const float targetLift=riseForProgress(age);
            panel.visualLift+=(targetLift-panel.visualLift)*liftDt;
        }else{panel.visualAnchor=panel.anchor;panel.visualLift=0.f;}
    }};
    follow(scene.panels);follow(scene.previous);
    for(auto& panel:scene.panels)if(!panel.appeared&&
        (panel.sourceWord<0||position>=float(scene.words[static_cast<size_t>(panel.sourceWord)].startMs))&&
        shouldReveal(now,panel.born,beats&&scene.words.empty(),freshPulse)){
        if(panel.sourceWord>=0){
            // A timed word chooses its own world anchor at its actual timestamp.
            // Already revealed words retain their anchor and orientation.
            const bool thirdPerson=magnitude2(cam-(view.position+Vec3{0,1.6f,0}))>6.25f;
            const Vec3 playerRay=view.position+Vec3{0,1.6f,0}-cam;
            const Vec3 gaze=normalized(playerRay);
            Vec3 best=panel.anchor;float bestScore=-100.f;
            for(size_t attempt=0;attempt<48;++attempt){
                const uint64_t seed=serial*733+uint64_t(panel.partIndex+1)*977+
                    uint64_t(attempt)*137;
                const Vec3 candidate=lyricAnchor(view,cam,seed,thirdPerson);
                const Vec3 ray=candidate-cam;
                float score=100.f;
                if(thirdPerson){
                    const float facing=ray.x*gaze.x+ray.y*gaze.y+ray.z*gaze.z;
                    const float distance=std::sqrt(magnitude2(ray));
                    const float alignment=facing/std::max(distance,.01f);
                    if(alignment<.4f)score=-5.f;
                    if(alignment>.94f&&distance>std::sqrt(magnitude2(playerRay))+.35f)
                        score=-4.f; // player would hide the lyric
                }
                for(const auto& old:scene.panels)if(old.appeared&&
                    (old.sourceWord<0||newestWord-old.sourceWord<4))
                    score=std::min(score,panelClearance(candidate,panel.width,cam,
                        old.visualAnchor+Vec3{0,old.visualLift,0},old.width));
                for(const auto& old:scene.previous)if(old.appeared&&old.visibleAlpha>.05f)
                    score=std::min(score,panelClearance(candidate,panel.width,cam,
                        old.visualAnchor+Vec3{0,old.visualLift,0},old.width));
                if(score>bestScore){bestScore=score;best=candidate;}
                if(score>=1.3f)break;
            }
            panel.anchor=best;
            panel.visualAnchor=best;
            panel.spawnPlayer=view.position;
            panel.visualLift=0.f;
            panel.facing=normalized(cam-best);
            panel.right=normalized({panel.facing.z,0,-panel.facing.x});
        }
        panel.appeared=now;
        panel.entryStrength=freshPulse?std::clamp(beat,.48f,1.f):0.f;
        if(freshPulse){scene.lastBeatPulse=pulse;freshPulse=false;}
    }
    const bool fading=now<scene.previousUntil&&!scene.previous.empty();
    if(scene.panels.empty()&&!fading)return;
    const float activeFade=cueFade(position,start,end);
    if(activeFade<=0.f)for(auto& panel:scene.panels)panel.visibleAlpha=0.f;
    auto timedAlpha=[&](const Panel& panel,uint8_t localWord){
        return timedWordAlpha(panel,scene.words,scene.displayWords,position,newestWord,localWord);
    };
    auto exitAlpha=[](const Panel& panel,uint8_t word){
        return panel.transitionWords.empty()?1.f:
            panel.transitionWords[std::min<size_t>(word,panel.transitionWords.size()-1)];
    };
    ColorGuard guard(colors);
    size_t count=0;
    int currentPart=-1;
    if(activeFade>0.f)
        for(const auto& panel:scene.panels)if(panel.appeared){
            size_t visible=0;
            for(const auto& pixel:panel.pixels)if(timedAlpha(panel,pixel.word)>0.f)++visible;
            for(const auto& pixel:panel.outline)if(timedAlpha(panel,pixel.word)>0.f)++visible;
            count+=visible;
            if(visible)currentPart=panel.partIndex;
        }
    if(fading)for(const auto& panel:scene.previous)if(panel.appeared&&
        panel.transitionAlpha*transitionFade(now,scene.previousFadeStart,
            panel.partIndex*scene.previousStagger+(panel.wordCount-1)*65,1)>0.f){
        for(const auto& pixel:panel.pixels)if(exitAlpha(panel,pixel.word)>0.f)++count;
        for(const auto& pixel:panel.outline)if(exitAlpha(panel,pixel.word)>0.f)++count;
    }
    if(!count)return;
    tessBegin(tess,nullptr,1,int(count*4),0);
    auto draw=[&](const Panel& panel,float alpha,float lift,bool leaving=false){
        if(alpha<=0.f)return;
        const float baseScale=panel.pixelScale*float(panel.fontPercent)*.01f;
        const float entryAge=std::max(0.f,float(now-panel.appeared));
        const float pulseFade=1.f-smoothstep(entryAge/340.f);
        const float enter=smoothstep(entryAge/270.f);
        const float settle=smoothstep((entryAge-250.f)/180.f);
        const float scale=baseScale*std::clamp(1.06f-float(panel.letters)*.006f,.94f,1.04f)
            *(.78f+.28f*enter-.06f*settle+panel.entryStrength*.13f*pulseFade);
        // One slow, tiny floating motion for the entire lyric segment.
        // Beats never change its tilt or animate individual glyph geometry.
        const float t=entryAge*.001f;
        const float tilt=.005f*std::sin(t*.95f+panel.partIndex*.27f);
        const Vec3 right=panel.right*std::cos(tilt)+Vec3{0,1,0}*std::sin(tilt);
        const Vec3 up=Vec3{0,1,0}*std::cos(tilt)-panel.right*std::sin(tilt);
        Vec3 anchor=panel.visualAnchor;
        lift+=.022f*std::sin(t*1.15f+panel.partIndex*.32f)
              +panel.entryStrength*.055f*pulseFade;
        if(lyricTrailFollow.load()){
            lift+=panel.visualLift;
        }
        auto quad=[&](Vec3 tl){
            auto tr=tl+right*(scale*.98f),bl=tl-up*(scale*.98f),br=tr-up*(scale*.98f);
            for(auto p:{bl,br,tr,tl})tessVertex(tess,p.x,p.y,p.z);
        };
        for(int pass=0;pass<2;++pass){
            const bool outline=pass==0;
            const auto origin=anchor-cam+up*lift+panel.facing*(outline?0.f:.001f);
            const auto& pixels=outline?panel.outline:panel.pixels;
            for(const auto& p:pixels){
                const float reveal=leaving?exitAlpha(panel,p.word):timedAlpha(panel,p.word);
                if(reveal<=0.f)continue;
                const auto tint=lyricTint(panel.partIndex,p.word);
                const float wordAlpha=leaving?transitionFade(now,scene.previousFadeStart,
                    panel.partIndex*scene.previousStagger+int(p.word)*65,1):1.f;
                tessColor(tess,outline?0.f:tint[0],outline?0.f:tint[1],outline?0.f:tint[2],
                          alpha*wordAlpha*reveal*float(p.coverage)/255.f);
                quad(origin+right*(p.x*scale)+up*(p.y*scale));
            }
        }
    };
    if(fading){
        for(const auto& panel:scene.previous)if(panel.appeared)
            draw(panel,panel.transitionAlpha,.1f,true);
    }
    for(auto& panel:scene.panels)if(panel.appeared){
        const float appear=std::max(.03f,smoothstep(float(now-panel.appeared)/350.f));
        float focus=scene.words.empty()?(panel.partIndex==currentPart?1.f:.68f):1.f;
        panel.visibleAlpha=appear*focus*activeFade;
        draw(panel,panel.visibleAlpha,(1.f-appear)*(-.09f));
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
    const bool exact26661=image.selected&&image.selected->name=="1.26.66.1";
#if !defined(LEVI_STANDALONE)
    lm_immersive::prepare(image.base,exact26661);
#endif
    lm_cinematic::discover(image.base,exact26661);
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
inline void request(bool value,bool monitor=false){
    if(enabled.exchange(value)!=value){++generation;lastDraw=0;renderStage=0;}
    if(!value&&!monitor)return;
    std::lock_guard lock(lifecycle);
    if(state.load()!=State::Idle)return;
    state=State::Preparing;
    installer=std::thread([]{
        try{prepare();}catch(...){state=State::HookFailed;}
        if(state.load()!=State::Ready)
            __android_log_print(ANDROID_LOG_WARN,"LeviMusicWorld3D","renderer unavailable (%d)",int(state.load()));
    });
}
inline bool inWorld(){
    return lastPlayerWorld.load()!=0&&nowMs()-lastPlayerTick.load()<1800;
}
inline void shutdown(){
    request(false,false);publish("");lastPlayerTick=0;lastPlayerWorld=0;
    lm_cinematic::shutdown();
    lm_immersive::request(false);
    std::lock_guard lock(lifecycle);
    if(installer.joinable())installer.join();
}
inline void unload(){
    shutdown();
    lm_cinematic::unload();
    lm_immersive::unload();
    if(renderHooked)pl::memory::unhook(renderTarget,reinterpret_cast<void*>(&renderHook));
    if(tickHooked)pl::memory::unhook(tickTarget,reinterpret_cast<void*>(&tickHook));
    installed=false;
}
} // namespace lm_world3d


