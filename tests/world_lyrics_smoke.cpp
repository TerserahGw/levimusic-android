#include "../src/WorldLyrics3D.hpp"
#include <cassert>
namespace {
int originals=0,meshes=0,vertices=0,declared=0;
float shader[4]{.2f,.3f,.4f,.5f};
void original(void*,void*,void*){++originals;}
void begin(void*,void*,int mode,int count,int){assert(mode==1);declared=count;}
void color(void*,float,float,float,float){assert(shader[0]==1.f);}
void vertex(void*,float x,float y,float z){assert(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z));++vertices;}
void mesh(void*,void*,void* material,char*){assert(material);++meshes;}
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
    enabled=true;installed=true;publish("HELLO WORLD");
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(originals==1&&meshes==1&&vertices==declared&&declared>0);
    assert(shader[0]==.2f&&shader[1]==.3f&&shader[2]==.4f&&shader[3]==.5f);
    assert(active());
    const auto anchor=scene.anchor;
    Pose lookingDown=pose;lookingDown.pitch=60.f;
    const auto down=lyricAnchor(lookingDown,lyricSerial);
    assert(down.y<anchor.y-2.2f);
    assert(std::abs(down.x-lookingDown.position.x)<5.f);
    float minSide=100.f,maxSide=-100.f;
    for(uint64_t n=1;n<128;++n){
        const auto point=lyricAnchor(pose,n);
        const float yaw=pose.yaw*.01745329251994f;
        const float offset=(point.x-pose.position.x)*(-std::cos(yaw))
                          +(point.z-pose.position.z)*(-std::sin(yaw));
        minSide=std::min(minSide,offset);maxSide=std::max(maxSide,offset);
    }
    assert(maxSide-minSide>2.8f);
    cam.x+=1.f;ptr(rp,profile->camera,cam);
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(meshes==2&&scene.anchor.x==anchor.x);
    pose.position.x+=1.f;
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(meshes==3&&std::abs(scene.anchor.x-anchor.x)<.001f);
    pose.pitch=60.f;
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(meshes==4&&std::abs(scene.anchor.y-anchor.y)<.001f);
    publish("NEXT LINE");
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(meshes==5&&magnitude2(scene.anchor-anchor)>.1f);
    enabled=false;renderHook(renderer.data(),screen.data(),nullptr);
    assert(originals==6&&meshes==5);
    return 0;
}

