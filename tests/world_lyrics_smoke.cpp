#include "../src/WorldLyrics3D.hpp"
#include <cassert>
namespace {
int originals=0,meshes=0,vertices=0,declared=0,batchVertices=0;
float maxChroma=0.f;
float shader[4]{.2f,.3f,.4f,.5f};
void original(void*,void*,void*){++originals;}
void begin(void*,void*,int mode,int count,int){assert(mode==1);declared=count;batchVertices=0;}
void color(void*,float r,float,float b,float){assert(shader[0]==1.f);maxChroma=std::max(maxChroma,std::abs(r-b));}
void vertex(void*,float x,float y,float z){assert(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z));++vertices;++batchVertices;}
void mesh(void*,void*,void* material,char*){assert(material);assert(batchVertices==declared);++meshes;}
}
int main(){
    using namespace lm_world3d;
    auto pixels=glyphs("HELLO 123");
    assert(!pixels.empty() && pixels.size()*20 < 50000);
    assert(latinDisplay("lirik") == "lirik");
    assert(!glyphs("lirik").empty());
    assert(matchesPattern({reinterpret_cast<const uint8_t*>("\xEE\x0F\x16\xFC"),4}, "EE 0F ? FC"));
    assert(!matchesPattern({reinterpret_cast<const uint8_t*>("\xEE\x0F\x16\xFC"),4}, "EE 0F 17 FC"));
    auto id=profiles[0].buildId;
    std::array<uint8_t,36> note{4,0,0,0,20,0,0,0,3,0,0,0,'G','N','U',0};
    auto hex=[](char c){return c>='0'&&c<='9'?c-'0':c-'a'+10;};
    for(size_t i=0;i<20;++i)note[16+i]=uint8_t(hex(id[i*2])*16+hex(id[i*2+1]));
    assert(matchesBuildId(note,id));
    note[17]^=1;assert(!matchesBuildId(note,id));
    // The actual detour submits local world glyphs once, restores game color,
    // and leaves Minecraft's original world-render call intact.
    profile=&profiles[0];originalRender=original;
    tessBegin=begin;tessColor=color;tessVertex=vertex;renderMesh=mesh;
    material=std::make_shared<int>(1);
    alignas(8) std::array<char,0x900> player{},rendererPlayer{},renderer{},screen{};
    alignas(8) std::array<char,128> tess{};
    auto ptr=[](char* base,size_t off,auto value){std::memcpy(base+off,&value,sizeof(value));};
    auto rp=rendererPlayer.data();auto t=tess.data();auto holder=shader;
    ptr(renderer.data(),profile->rendererPlayer,rp);
    ptr(screen.data(),profile->tessellator,t);
    ptr(screen.data(),0x30,holder);
    Vec3 cam{100.f,67.f,200.f};ptr(rp,profile->camera,cam);
    pose={0.f,35.f,reinterpret_cast<uintptr_t>(player.data()),0x2345678,nowMs(),{100.f,65.f,200.f}};
    enabled=true;installed=true;publish("HELLO WORLD",0,4000,0,true);
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(originals==1&&meshes==1&&vertices==declared&&declared>0);
    assert(shader[0]==.2f&&shader[1]==.3f&&shader[2]==.4f&&shader[3]==.5f);
    assert(active());
    assert(scene.panels.size()==2);
    const auto anchor=scene.panels.front().anchor,right=scene.panels.front().right;
    lyricScatterFov=0;
    auto first=lyricAnchor(pose,cam,47,false);
    auto thirdNarrow=lyricAnchor(pose,{100.f,68.f,196.f},47,true);
    lyricScatterFov=140;
    auto firstWide=lyricAnchor(pose,cam,47,false);
    assert(magnitude2(first-firstWide)>.01f);
    auto third=lyricAnchor(pose,{100.f,68.f,196.f},47,true);
    assert(magnitude2(third-thirdNarrow)<.0001f);
    assert(magnitude2(third-pose.position)>magnitude2(firstWide-pose.position));
    lyricVerticalRangeTenths=0;
    auto levelAnchor=lyricAnchor(pose,cam,47,false);
    lyricVerticalRangeTenths=15;
    auto variedAnchor=lyricAnchor(pose,cam,47,false);
    assert(std::abs(variedAnchor.y-levelAnchor.y)<=1.5001f);
    assert(std::abs(variedAnchor.y-levelAnchor.y)>.001f);
    lyricVerticalRangeTenths=8;
    assert(splitLyric("One long lyric line that should appear across different parts").size()>=4);
    for(const auto& part:splitLyric("One long lyric line that should appear across different parts"))assert(part.size()<=10);
    auto longWord=splitLyric("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMN");
    assert(longWord.size()==4&&longWord[0]+longWord[1]+longWord[2]+longWord[3]=="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMN");
    auto firstPanel=layoutAnchor({0,0,0},{1,0,0},0,8,.9f);
    auto secondPanel=layoutAnchor({0,0,0},{1,0,0},1,8,.9f);
    auto fifthPanel=layoutAnchor({0,0,0},{1,0,0},4,8,.9f);
    assert(firstPanel.x<secondPanel.x&&firstPanel.y>fifthPanel.y);
    assert(cueFade(0,0,4000)>cueFade(3800,0,4000));
    assert(cueFade(4700,0,4000)==0.f);
    assert(riseForProgress(0.f)==0.f);
    assert(riseForProgress(.5f)>0.f&&riseForProgress(.5f)<riseForProgress(1.f));
    assert(riseForProgress(1.f)<.7f);
    const float corrected=advanceCueClock(700.f,620.f,16.f,true);
    assert(corrected>700.f&&corrected<716.f); // correction cannot jerk backward
    assert(advanceCueClock(700.f,2500.f,16.f,true)==2500.f); // deliberate seek
    assert(!shouldReveal(100,100,true,false));
    assert(shouldReveal(100,100,true,true));
    assert(shouldReveal(341,100,true,false));
    int white=0;
    for(int i=0;i<20;++i)if(lyricTint(i,0)==std::array<float,3>{1.f,1.f,1.f})++white;
    assert(white>=16);
    cam.x+=1.f;ptr(rp,profile->camera,cam);
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(meshes==2&&scene.panels.front().anchor.x==anchor.x&&scene.panels.front().right.x==right.x);
    pose.position.x+=1.f;
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(meshes==3&&std::abs(scene.panels.front().anchor.x-anchor.x)<.001f);
    pose.pitch=60.f;
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(meshes==4&&std::abs(scene.panels.front().anchor.y-anchor.y)<.001f);
    publish("NEXT LINE HAS MANY WORDS TO SPLIT INTO PARTS",0,5000,300,true);
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(meshes==5&&scene.panels.size()>1&&magnitude2(scene.panels.front().anchor-anchor)>.1f);
    assert(scene.panels[1].born-scene.panels[0].born>0);
    assert(magnitude2(scene.panels[0].anchor-scene.panels[1].anchor)>.1f);
    assert(!scene.previous.empty()&&scene.previousUntil>nowMs());
    assert(scene.previousFadeStart>nowMs());
    for(size_t i=1;i<scene.panels.size();++i)
        assert(magnitude2(scene.panels[i].anchor-scene.panels[i-1].anchor)>.1f);
    lyricTrailFollow=true;
    auto trail=scene.panels.front().anchor;
    scene.filteredPlayer=scene.panels.front().spawnPlayer;
    auto filtered=scene.filteredPlayer;
    pose.position.x=filtered.x+2.f;
    scene.lastFrame=nowMs()-100;
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(scene.panels.front().anchor.x==trail.x);
    assert(scene.filteredPlayer.x>filtered.x&&scene.filteredPlayer.x<pose.position.x);
    assert(scene.panels.front().visualAnchor.x>trail.x&&
           scene.panels.front().visualAnchor.x<trail.x+2.f);
    assert(scene.panels.front().visualLift>0.f&&scene.panels.front().visualLift<.7f);
    lyricTrailFollow=false;
    experimentalBeats=true;beatStrength=.8f;beatUpdated=nowMs();
    scene.panels[1].born=nowMs();scene.panels[1].partIndex=4;
    beatPulseSerial=1;beatPulseTime=nowMs();
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(meshes==7&&declared>0&&maxChroma>.1f);
    assert(scene.panels[1].appeared>0&&scene.panels[1].entryStrength>.4f);
    experimentalBeats=false;
    publish("");
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(meshes==8&&!scene.previous.empty());
    scene.previousUntil=nowMs()-1;
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(meshes==8);
    enabled=false;renderHook(renderer.data(),screen.data(),nullptr);
    assert(originals==10&&meshes==8);
    return 0;
}


