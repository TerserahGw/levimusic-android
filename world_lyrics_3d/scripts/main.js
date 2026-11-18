// LeviMusic World Lyrics 3D — v1.2.1 experimental addon
// Uses Minecraft's own 3D TextPrimitive. NOT a 2D HUD effect.
// Requires Beta APIs in the world. Receives lines through:
// /scriptevent levimusic:lyric Hello there
// or JSON: /scriptevent levimusic:lyric {"text":"Hello","durationMs":2700}
// The native LeviMusic mod does not automatically send script events yet.

import { world, system, TextPrimitive } from "@minecraft/server";

const active=new Map();
const MAX_LINE=120;
const DEFAULT_MS=3800;
const TICK_MS=50;
const FADE_TICKS=6;

function clamp(v,lo,hi){return Math.min(hi,Math.max(lo,v));}
function normalize(v) {
    const length=Math.hypot(v.x,v.y,v.z)||1;
    return {x:v.x/length,y:v.y/length,z:v.z/length};
}
function cross(a,b) {
    return {x:a.y*b.z-a.z*b.y,y:a.z*b.x-a.x*b.z,z:a.x*b.y-a.y*b.x};
}
function add(a,b){return {x:a.x+b.x,y:a.y+b.y,z:a.z+b.z};}
function scale(v,n){return {x:v.x*n,y:v.y*n,z:v.z*n};}
function rng(){return Math.random()*2-1;}

function newVisibleLocation(player){
    const eye=player.getHeadLocation();
    const forward=normalize(player.getViewDirection());
    const upWorld={x:0,y:1,z:0};
    let right=normalize(cross(forward,upWorld));
    let up=normalize(cross(right,forward));
    if(Math.abs(forward.y)>.95){
        right={x:1,y:0,z:0};
        up={x:0,y:0,z:1};
    }
    for(let attempt=0;attempt<18;attempt++){
        const distance=3.2+Math.random()*3.4;
        const dx=rng()*.36*distance;
        const dy=rng()*.22*distance;
        const result=add(add(add(eye,scale(forward,distance)),
                            scale(right,dx)),scale(up,dy));
        // Avoid placing labels inside blocks if the local world is loaded.
        try {
            const block=player.dimension.getBlock({
                x:Math.floor(result.x),y:Math.floor(result.y),z:Math.floor(result.z)
            });
            if(block && !block.isAir)continue;
        }catch{}
        return result;
    }
    return add(eye,scale(forward,4));
}
function erase(id){
    const entry=active.get(id);
    if(!entry)return;
    try{entry.shape.remove();}catch{}
    active.delete(id);
}
function show(player,text,durationMs){
    if(!player||player.typeId!=="minecraft:player")return;
    erase(player.id);
    text=String(text||"").trim().slice(0,MAX_LINE);
    if(!text)return;
    if(!world.primitiveShapesManager){
        // Feature is not installed/enabled in this Minecraft version/world.
        return;
    }
    const position=newVisibleLocation(player);
    try {
        const shape=new TextPrimitive(
            {x:position.x,y:position.y,z:position.z,dimension:player.dimension},
            text
        );
        shape.visibleTo=[player]; // No other player can see this shape.
        shape.color={red:1,green:1,blue:1,alpha:0};
        shape.backgroundColorOverride={red:0,green:0,blue:0,alpha:0};
        shape.useRotation=false; // Billboard always faces the player's camera.
        shape.depthTest=true;
        shape.scale=.7;
        shape.maximumRenderDistance=20;
        world.primitiveShapesManager.addText(shape,player.dimension);
        const life=clamp(Math.round(durationMs/TICK_MS),14,600);
        active.set(player.id,{
            shape,playerId:player.id,
            born:system.currentTick,
            end:system.currentTick+life
        });
    }catch(error){
        console.warn("[LeviMusic WorldLyrics] 3D text failed: "+error);
    }
}
world.afterEvents.scriptEventReceive.subscribe(event=>{
    if(event.id!=="levimusic:lyric"&&event.id!=="levimusic:clear")return;
    const p=event.sourceEntity;
    if(!p||p.typeId!=="minecraft:player")return;
    if(event.id==="levimusic:clear"){
        erase(p.id);return;
    }
    let text=event.message, durationMs=DEFAULT_MS;
    if(text.startsWith("{")){
        try {
            const data=JSON.parse(text);
            text=String(data.text??"");
            durationMs=Number(data.durationMs??DEFAULT_MS);
        }catch{return;}
    }
    if(!Number.isFinite(durationMs))durationMs=DEFAULT_MS;
    show(p,text,clamp(durationMs,700,30000));
});
system.runInterval(()=>{
    for(const [id,item] of active){
        const elapsed=system.currentTick-item.born;
        const remaining=item.end-system.currentTick;
        if(remaining<=0){erase(id);continue;}
        const opacity=Math.min(1,elapsed/FADE_TICKS,remaining/FADE_TICKS);
        try {
            item.shape.color={red:1,green:1,blue:1,alpha:clamp(opacity,0,1)};
        }catch{erase(id);}
    }
},1);
