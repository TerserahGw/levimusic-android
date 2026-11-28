#include "../src/WorldLyrics3D.hpp"
#include <cassert>
namespace {
int originals=0,meshes=0,vertices=0,declared=0,batchVertices=0;
float maxChroma=0.f;
float shader[4]{.2f,.3f,.4f,.5f};
int environmentFixture=0;
void* regionFixture=nullptr;
float cinematicComponentFixture[7]{0,0,0,1,0,65.62f,0};
void original(void*,void*,void*){++originals;}
void* fixtureBlockSource(void*){return regionFixture;}
void fixtureCameraTransform(float* output,const float*){
    std::fill(output,output+16,0.f);
    output[0]=output[5]=output[10]=output[15]=1.f;
}
void fixtureSetup(void*,void*,float){
    float output[16]{};
    lm_cinematic::cameraTransformHook(output,cinematicComponentFixture);
    lm_cinematic::cameraTransformHook(output,cinematicComponentFixture);
}
bool fixtureWater(void*){return environmentFixture==2;}
bool fixtureSolid(void*,const lm_immersive::BlockPos& position){
    return environmentFixture==1&&(position.y>=67||std::abs(position.x)>=2||std::abs(position.z)>=2);
}
void begin(void*,void*,int mode,int count,int){assert(mode==1);declared=count;batchVertices=0;}
void color(void*,float r,float,float b,float){assert(shader[0]==1.f);maxChroma=std::max(maxChroma,std::abs(r-b));}
void vertex(void*,float x,float y,float z){assert(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z));++vertices;++batchVertices;}
void mesh(void*,void*,void* material,char*){assert(material);assert(batchVertices==declared);++meshes;}
}
int main(){
    using namespace lm_world3d;
    assert(otherDistanceTenths==18&&otherHeightTenths==0&&
           otherVerticalRangeTenths==4&&otherFontPercent==140);
    assert(lyricHeightTenths==0&&lyricVerticalRangeTenths==2);
    assert(lyricScatterFov==60&&firstPlacement==0&&
           lyricTrailFollow&&lyricPalette==0);
    otherDistanceTenths=38;otherHeightTenths=13;lyricTrailFollow=false;
    const std::vector<lm_timing::WordCue> sample{{21070,21307,"I"},
        {21307,21753,"wanna"},{21753,22304,"take"},{22304,22487,"you"}};
    assert(lm_timing::revealedText("I wanna take you",sample,21069).empty());
    assert(lm_timing::revealedText("I wanna take you",sample,21070)=="I");
    assert(lm_timing::revealedText("I wanna take you",sample,21500)=="I wanna");
    assert(lm_timing::revealedText("I wanna take you",sample,22487)=="I wanna take you");
    assert(lm_timing::revealedText("ordinary line",{},10)=="ordinary line");
    const std::vector<lm_timing::WordCue> japanese{{100,250,"夜"},{250,400,"に"},
        {400,600,"駆"},{600,800,"ける"}};
    assert(lm_timing::revealedText("夜に駆ける",japanese,100)=="夜");
    assert(lm_timing::sourceWordIndex(2,4,4)==2);
    assert(lm_timing::sourceWordIndex(2,4,8)==4);
    auto pixels=glyphs("HELLO 123");
    assert(!pixels.empty() && pixels.size()*20 < 50000);
    assert(latinDisplay("lirik") == "lirik");
    assert(latinDisplay("yomi \xe6\x97\xa5\xe6\x9c\xac") == "yomi ");
    assert(latinDisplay("\xe6\x97\xa5\xe6\x9c\xac").find('?')==std::string::npos);
    assert(!glyphs("lirik").empty());
    assert(matchesPattern({reinterpret_cast<const uint8_t*>("\xEE\x0F\x16\xFC"),4}, "EE 0F ? FC"));
    assert(!matchesPattern({reinterpret_cast<const uint8_t*>("\xEE\x0F\x16\xFC"),4}, "EE 0F 17 FC"));
    auto id=profiles[0].buildId;
    std::array<uint8_t,36> note{4,0,0,0,20,0,0,0,3,0,0,0,'G','N','U',0};
    auto hex=[](char c){return c>='0'&&c<='9'?c-'0':c-'a'+10;};
    for(size_t i=0;i<20;++i)note[16+i]=uint8_t(hex(id[i*2])*16+hex(id[i*2+1]));
    assert(matchesBuildId(note,id));
    note[17]^=1;assert(!matchesBuildId(note,id));
    // SetupCamera calls the verified quaternion builder twice. The hook keeps
    // the cinematic pose alive for later frame consumers, then restores the
    // native component after the smooth exit finishes.
    lm_cinematic::originalCameraTransform=fixtureCameraTransform;
    lm_cinematic::originalSetup=fixtureSetup;
    lm_cinematic::enabled=true;lm_cinematic::supported=true;
    lm_cinematic::idleDelay=10;
    lm_cinematic::observePlayer(cinematicComponentFixture,0,64,0,0,0,1);
    lm_cinematic::lastActivity=lm_cinematic::nowMs()-11000;
    lm_cinematic::enteredAt=lm_cinematic::nowMs()-2000;
    const lm_cinematic::Position originalCameraPosition{cinematicComponentFixture[4],
        cinematicComponentFixture[5],cinematicComponentFixture[6]};
    lm_cinematic::setupHook(nullptr,nullptr,0.f);
    assert(lm_cinematic::cameraApplyCalls==1&&lm_cinematic::builderApplyCalls==2);
    assert(cinematicComponentFixture[4]!=originalCameraPosition.x||
           cinematicComponentFixture[5]!=originalCameraPosition.y||
           cinematicComponentFixture[6]!=originalCameraPosition.z);
    lm_cinematic::noteActivity();
    for(int i=0;i<20;++i)lm_cinematic::setupHook(nullptr,nullptr,0.f);
    assert(cinematicComponentFixture[4]==originalCameraPosition.x&&
           cinematicComponentFixture[5]==originalCameraPosition.y&&
           cinematicComponentFixture[6]==originalCameraPosition.z);
    lm_cinematic::shutdown();

    // The enclosure probe resolves the live BlockSource from the actor's
    // Dimension, synchronously while its callback is still valid.
    alignas(8) std::array<char,0x300> actorFixture{},sourceFixture{};
    std::array<void*,15> dimensionVtable{};
    struct FakeDimension {void** vtable;std::array<char,0xC8> padding{};void* source{};}
        dimensionFixture{dimensionVtable.data()};
    dimensionVtable[14]=reinterpret_cast<void*>(&fixtureBlockSource);
    dimensionFixture.source=sourceFixture.data();
    auto* dimensionPointer=&dimensionFixture;
    std::memcpy(actorFixture.data()+0x1E0,&dimensionPointer,sizeof(dimensionPointer));
    regionFixture=sourceFixture.data();
    lm_immersive::supported=true;lm_immersive::isInWater=fixtureWater;
    lm_immersive::isSolid=fixtureSolid;
    lm_immersive::request(true);
    environmentFixture=1;
    lm_immersive::observePlayer(actorFixture.data(),0,64,0);
    assert(lm_immersive::lastWorldProbe.load()>0);
    assert(lm_immersive::current()==lm_immersive::Environment::Cave);
    environmentFixture=2;lm_immersive::lastSample=0;
    lm_immersive::observePlayer(actorFixture.data(),0,64,0);
    assert(lm_immersive::current()==lm_immersive::Environment::Underwater);
    environmentFixture=0;lm_immersive::lastSample=0;
    lm_immersive::lastCaveEvidence=lm_immersive::nowMs()-7000;
    lm_immersive::observePlayer(actorFixture.data(),0,64,0);
    assert(lm_immersive::current()==lm_immersive::Environment::Open);
    lm_immersive::request(false);
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
    otherDistanceTenths=52;
    auto farther=lyricAnchor(pose,{100.f,68.f,196.f},47,true);
    assert(magnitude2(farther-third)>1.f);
    assert(magnitude2(lyricAnchor(pose,cam,47,false)-firstWide)<.0001f);
    otherDistanceTenths=38;
    otherHeightTenths=25;
    assert(std::abs(lyricAnchor(pose,{100.f,68.f,196.f},47,true).y-third.y-1.2f)<.001f);
    assert(magnitude2(lyricAnchor(pose,cam,47,false)-firstWide)<.0001f);
    otherHeightTenths=13;
    auto orbitBase=lyricAnchor(pose,{100.f,68.f,196.f},79,true);
    auto firstOrbit=orbitAnchor(pose,orbitBase,0,8);
    auto lastOrbit=orbitAnchor(pose,orbitBase,7,8);
    assert(magnitude2(lastOrbit-firstOrbit)>9.f);
    for(size_t i=1;i<8;++i){
        auto a=orbitAnchor(pose,orbitBase,i-1,8)-pose.position;
        auto b=orbitAnchor(pose,orbitBase,i,8)-pose.position;
        assert(a.x*b.z-a.z*b.x>0.f); // reading order travels around the player
        assert(magnitude2(a-b)>1.f);
        assert(std::abs(std::hypot(b.x,b.z)-3.8f)<.001f);
    }
    for(int mode=1;mode<=2;++mode){
        auto a=scatteredAnchor(pose,orbitBase,0,8,mode,79);
        auto b=scatteredAnchor(pose,orbitBase,4,8,mode,79);
        assert(magnitude2(a-b)>25.f); // opposite sectors, not a tight cluster
        assert(magnitude2(scatteredAnchor(pose,orbitBase,2,8,mode,79)-
                          scatteredAnchor(pose,orbitBase,2,8,mode,79))<.0001f);
    }
    auto fan=firstFanAnchor(pose,firstWide,0,6);
    assert(magnitude2(fan-firstWide)>.1f);
    lyricVerticalRangeTenths=0;
    auto levelAnchor=lyricAnchor(pose,cam,47,false);
    lyricVerticalRangeTenths=15;
    auto variedAnchor=lyricAnchor(pose,cam,47,false);
    assert(std::abs(variedAnchor.y-levelAnchor.y)<=1.5001f);
    assert(std::abs(variedAnchor.y-levelAnchor.y)>.001f);
    lyricVerticalRangeTenths=8;
    assert(splitLyric("One long lyric line that should appear across different parts").size()>=4);
    const auto scriptParts=splitUnicodeLyric("夜に駆ける 한국어 مرحبا Привет");
    assert(scriptParts.size()==4);
    for(const auto& part:scriptParts)assert(lm_timing::utf8Length(part)<=6);
    auto wrapped=splitUnicodeLyric("あいうえおかきくけこさしすせそ");
    assert(wrapped.size()==3&&wrapped[0]+wrapped[1]+wrapped[2]=="あいうえおかきくけこさしすせそ");
    assert(unicodeParts("夜に駆ける",japanese).size()==4);
    std::array<uint8_t,49> mask{};mask[24]=255;
    auto crisp=rasterMesh(mask,7,7,4,1);
    assert(crisp.pixels.size()==1&&crisp.outline.size()==24);
    for(const auto& edge:crisp.outline)
        assert(edge.x!=crisp.pixels[0].x||edge.y!=crisp.pixels[0].y);
    assert(panelClearance({0,0,2},.6f,{0,0,0},{.08f,0,2},.6f)<1.f);
    assert(panelClearance({0,.8f,2},.6f,{0,0,0},{0,0,2},.6f)>1.f);
    lyricPalette=3;
    assert(lyricTint(0,0)!=lyricTint(1,0));
    lyricPalette=1;
    for(const auto& part:splitLyric("One long lyric line that should appear across different parts"))assert(part.size()<=8);
    auto longWord=splitLyric("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMN");
    assert(longWord.size()==5&&longWord[0]+longWord[1]+longWord[2]+longWord[3]+longWord[4]=="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMN");
    auto firstPanel=layoutAnchor({0,0,0},{1,0,0},0,8,.9f);
    auto secondPanel=layoutAnchor({0,0,0},{1,0,0},1,8,.9f);
    auto fifthPanel=layoutAnchor({0,0,0},{1,0,0},4,8,.9f);
    assert(firstPanel.x<secondPanel.x&&firstPanel.y>fifthPanel.y);
    assert(cueFade(0,0,4000)==cueFade(3800,0,4000));
    assert(cueFade(4000,0,4000)==1.f);
    assert(cueFade(4700,0,4000)==0.f);
    assert(transitionFade(1000,1080,0,100)==1.f);
    assert(transitionFade(1300,1080,0,100)<transitionFade(1300,1080,3,100));
    assert(transitionFade(2000,1080,0,100)==0.f);
    assert(riseForProgress(0.f)==0.f);
    assert(riseForProgress(.5f)>0.f&&riseForProgress(.5f)<riseForProgress(1.f));
    assert(riseForProgress(1.f)<.7f);
    const float corrected=advanceCueClock(700.f,620.f,16.f,true);
    assert(corrected>700.f&&corrected<716.f); // correction cannot jerk backward
    assert(advanceCueClock(700.f,2500.f,16.f,true)==2500.f); // deliberate seek
    assert(advanceCueClock(700.f,1000.f,16.f,false)==1000.f); // paused seek
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
    assert(scene.previous[0].transitionAlpha>0.f);
    assert(scene.previous[0].transitionAlpha<=scene.previous[0].visibleAlpha);
    for(size_t i=1;i<scene.panels.size();++i)
        assert(magnitude2(scene.panels[i].anchor-scene.panels[i-1].anchor)>.1f);
    // A new line in another camera mode uses its own font and orbit layout.
    cam={100.f,68.f,196.f};ptr(rp,profile->camera,cam);
    otherFontPercent=140;
    publish("THIS LINE SPLITS INTO SEVERAL ORDERED PARTS AROUND THE PLAYER",0,5000,600,true);
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(scene.panels.size()>4&&scene.panels[0].fontPercent==140);
    for(size_t i=1;i<scene.panels.size();++i)
        assert(magnitude2(scene.panels[i].anchor-scene.panels[i-1].anchor)>1.f);
    otherFontPercent=115;
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
    assert(meshes==8&&declared>0&&maxChroma>.1f);
    assert(scene.panels[1].appeared>0&&scene.panels[1].entryStrength>.4f);
    experimentalBeats=false;
    publish("");
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(meshes==9&&!scene.previous.empty());
    scene.previousUntil=nowMs()-1;
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(meshes==9);
    enabled=false;renderHook(renderer.data(),screen.data(),nullptr);
    assert(originals==11&&meshes==9);
    enabled=true;
    publish("FADE TEST",0,1000,500,true);
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(!scene.panels.empty()&&scene.panels.front().visibleAlpha>0.f);
    publish("FADE TEST",0,1000,3000,true); // same lyric, now fully faded
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(scene.panels.front().visibleAlpha==0.f);
    publish("NEW CUE",3000,5000,3000,true);
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(!scene.previous.empty()&&scene.previous.front().transitionAlpha==0.f);
    scene=Scene{};
    pose.position={100.f,65.f,200.f};pose.yaw=0.f;pose.time=nowMs();
    cam={100.f,66.6f,200.f};ptr(rp,profile->camera,cam);
    const std::vector<lm_timing::WordCue> timed{{1000,1300,"I"},
        {1300,1800,"wanna"},{1800,2500,"care"}};
    publish("I wanna care",1000,2500,1000,false,timed);
    renderHook(renderer.data(),screen.data(),nullptr);
    const int firstWordVertices=declared;
    const auto firstWordAnchor=scene.panels.front().anchor;
    const auto prematureSecondAnchor=scene.panels[1].anchor;
    assert(firstWordVertices>0&&scene.words.size()==3);
    pose.yaw=90.f;pose.time=nowMs();
    publish("I wanna care",1000,2500,1400,false,timed);
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(declared>firstWordVertices);
    assert(scene.panels.front().anchor.x==firstWordAnchor.x);
    assert(scene.panels[1].anchor.x<pose.position.x-.7f);
    assert(magnitude2(scene.panels[1].anchor-prematureSecondAnchor)>.1f);
    const auto secondFacing=scene.panels[1].facing;
    pose.yaw=180.f;pose.time=nowMs();
    publish("I wanna care",1000,2500,2000,false,timed);
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(declared>firstWordVertices);
    assert(scene.panels[2].anchor.z<pose.position.z-.5f);
    assert(scene.panels[1].facing.x==secondFacing.x);
    const auto oldCue=scene.serial;
    publish("I wanna care",3000,4500,3000,false,timed);
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(scene.serial!=oldCue); // repeated line with a new timestamp
    scene=Scene{};
    cam={100.f,68.f,196.f};ptr(rp,profile->camera,cam);
    pose.time=nowMs();
    publish("I wanna care",1000,2500,1000,false,timed);
    renderHook(renderer.data(),screen.data(),nullptr);
    const auto thirdFirst=scene.panels.front().anchor;
    cam={104.f,68.f,200.f};ptr(rp,profile->camera,cam);
    pose.time=nowMs();
    publish("I wanna care",1000,2500,1400,false,timed);
    renderHook(renderer.data(),screen.data(),nullptr);
    const auto newRay=normalized(scene.panels[1].anchor-cam);
    const auto gaze=normalized(pose.position+Vec3{0,1.6f,0}-cam);
    assert(newRay.x*gaze.x+newRay.y*gaze.y+newRay.z*gaze.z>.4f);
    assert(scene.panels.front().anchor.x==thirdFirst.x);
    const std::vector<lm_timing::WordCue> cjkWords{{5000,5350,"夜"},
        {5350,5700,"に"},{5700,6300,"駆け"},{6300,6800,"る"}};
    const auto exactCjk=unicodeParts("夜に駆ける",cjkWords);
    assert(exactCjk.size()==4&&exactCjk[0]=="夜"&&exactCjk[2]=="駆け");
    const auto punctuatedCjk=unicodeParts("夜に、駆ける",cjkWords);
    assert(punctuatedCjk.size()==4);
    std::string rebuiltCjk;for(const auto& part:punctuatedCjk)rebuiltCjk+=part;
    assert(rebuiltCjk=="夜に、駆ける");
    scene=Scene{};
    std::vector<GlyphMesh> unicodeMesh;
    for(size_t i=0;i<splitUnicodeLyric("夜に駆ける 한국어").size();++i){
        GlyphMesh m;m.width=20;m.scale=.016f;
        m.pixels.push_back({0,0,0,180});
        unicodeMesh.push_back(m);
    }
    cam={100.f,66.6f,200.f};ptr(rp,profile->camera,cam);
    publish("夜に駆ける 한국어",5000,8000,5000,false,{},unicodeMesh,0);
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(scene.panels.size()==unicodeMesh.size());
    assert(scene.panels.front().pixels.front().coverage==180);
    assert(scene.panels.front().pixelScale==.016f);
    scene=Scene{};pose.time=nowMs();
    std::vector<GlyphMesh> cjkMeshes(exactCjk.size());
    for(auto& mesh:cjkMeshes){mesh.width=12;mesh.scale=.016f;mesh.pixels.push_back({0,0,0,255});}
    publish("夜に駆ける",5000,7000,5000,false,cjkWords,cjkMeshes,0);
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(scene.panels.size()==4&&scene.panels[0].sourceWord==0&&
           scene.panels[1].sourceWord==1&&scene.panels[1].appeared==0);
    publish("夜に駆ける",5000,7000,5900,false,cjkWords,cjkMeshes,0);
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(scene.panels[1].appeared>0&&scene.panels[2].appeared>0&&scene.panels[3].appeared==0);
    scene=Scene{};
    pose.time=nowMs();
    const std::vector<lm_timing::WordCue> fadeWords{{100,200,"one"},
        {200,300,"two"},{300,400,"three"},{400,500,"four"},{500,900,"five"}};
    std::vector<GlyphMesh> fadeMeshes(5);
    for(auto& part:fadeMeshes){part.width=12;part.pixels.push_back({0,0,0,255});}
    publish("one two three four five",100,2000,100,false,fadeWords,fadeMeshes);
    renderHook(renderer.data(),screen.data(),nullptr);
    publish("one two three four five",100,2000,1000,false,fadeWords,fadeMeshes);
    renderHook(renderer.data(),screen.data(),nullptr);
    publish("next",1200,3000,1200,false);
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(scene.previous.size()==5);
    assert(scene.previous.front().transitionWords[0]==0.f);
    assert(scene.previous.back().transitionWords[0]>0.f);
    lyricTrailFollow=true;
    const auto frozenExit=scene.previous.back().visualAnchor;
    pose.position.x+=1.5f;pose.time=nowMs();scene.lastFrame=nowMs()-100;
    renderHook(renderer.data(),screen.data(),nullptr);
    assert(scene.previous.back().visualAnchor.x==frozenExit.x&&
           scene.previous.back().visualAnchor.y==frozenExit.y&&
           scene.previous.back().visualAnchor.z==frozenExit.z);
    lyricTrailFollow=false;
    scene=Scene{};pose.time=nowMs();
    publish("ORIGINAL",0,4000,100,true,{}, {},0,"romanized reading");
    renderHook(renderer.data(),screen.data(),nullptr);
    bool foundReading=false;
    for(const auto& panel:scene.panels)if(panel.reading){
        foundReading=true;assert(panel.fontPercent<lyricFontPercent.load());
    }
    assert(foundReading);
    return 0;
}


