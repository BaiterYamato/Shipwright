#include "backend_glue.h"
#include "oot_actors.h"
#include "oot_hooks.h"
#include "oot_randomizer.h"
#include "text.h"
#include <array>
#include <cstring>
#include <vector>

extern "C" {
extern short gFourSwordCloneId;
extern size_t gFourSwordCloneStructSize;
void FourSwordClone_Init(void*, void*);
void FourSwordClone_Destroy(void*, void*);
void FourSwordClone_Update(void*, void*);
void FourSwordClone_Draw(void*, void*);
void NeiWorld_OnActorInit(void*, void*);
void NeiWorld_Draw(void*);
void NeiWorld_Reset(void);
void NeiBackends_ClearRuntime(void);
unsigned NeiSensor_GetWish(unsigned);
void NeiSensor_SetWish(unsigned, unsigned);
}
namespace {
const ShipNativeRuntime* gRuntime;
const ShipOotActorsV1* gActors;
const ShipOotRandomizerV2* gQueries;
std::vector<uint64_t> gHooks;
std::vector<int16_t> gActorTypes;
struct ActorCallbacks { void (*init)(void*,void*); void (*destroy)(void*,void*);
    void (*update)(void*,void*); void (*draw)(void*,void*); };
std::array<ActorCallbacks, 4> gCallbacks;
unsigned gTypeCount;
void SHIP_NATIVE_CALL Init(void* u, void* a, void* p) { auto& f=*(ActorCallbacks*)u; if(f.init) f.init(a,p); }
void SHIP_NATIVE_CALL Destroy(void* u, void* a, void* p) { auto& f=*(ActorCallbacks*)u; if(f.destroy) f.destroy(a,p); }
void SHIP_NATIVE_CALL Update(void* u, void* a, void* p) { auto& f=*(ActorCallbacks*)u; if(f.update) f.update(a,p); }
void SHIP_NATIVE_CALL Draw(void* u, void* a, void* p) { auto& f=*(ActorCallbacks*)u; if(f.draw) f.draw(a,p); }
ShipNativeStatus SHIP_NATIVE_CALL ActorInit(void*, const ShipNativeHookCall* call) {
    if (!call || call->payload_size < sizeof(ShipOotActorHookV1)) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& p=*(ShipOotActorHookV1*)call->payload;
    NeiWorld_OnActorInit(p.actor,p.play_state);
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL DrawEnd(void*, const ShipNativeHookCall* call) {
    if (!call || call->payload_size < sizeof(ShipOotRenderPlayHookV1)) return SHIP_NATIVE_INVALID_ARGUMENT;
    NeiWorld_Draw(((ShipOotRenderPlayHookV1*)call->payload)->play_state);
    return SHIP_NATIVE_OK;
}
bool Hook(const char* name, uint32_t size, ShipNativeHookFn fn) {
    ShipNativeHookSpec spec{sizeof(spec),name,1,size,SHIP_NATIVE_HOOK_OBSERVE,SHIP_NATIVE_HOOK_AFTER,0,fn,nullptr};
    uint64_t handle=0;
    if (gRuntime->register_hook(gRuntime->context,&spec,&handle)!=SHIP_NATIVE_OK) return false;
    gHooks.push_back(handle); return true;
}
}
extern "C" short NeiBackend_RegisterActor(const char* name, unsigned category, unsigned flags,
    unsigned size, void (*init)(void*,void*), void (*destroy)(void*,void*),
    void (*update)(void*,void*), void (*draw)(void*,void*)) {
    if (!gActors || gTypeCount >= gCallbacks.size()) return -1;
    auto& callbacks=gCallbacks[gTypeCount]; callbacks={init,destroy,update,draw};
    ShipOotActorTypeSpecV1 spec{sizeof(spec),name,(uint8_t)category,flags,1,size,
        Init,Destroy,Update,draw?Draw:nullptr,&callbacks};
    int16_t id=-1;
    if(gActors->register_actor_type(&spec,&id)!=SHIP_NATIVE_OK) return -1;
    ++gTypeCount; gActorTypes.push_back(id); return id;
}
extern "C" void FourSwordClone_EnsureRegistered(void) {
    if(gFourSwordCloneId!=-1) return;
    gFourSwordCloneId=NeiBackend_RegisterActor("skijer.nei.four_sword_clone",8,0x30,
        (unsigned)gFourSwordCloneStructSize,FourSwordClone_Init,FourSwordClone_Destroy,
        FourSwordClone_Update,FourSwordClone_Draw);
}
extern "C" uint8_t Randomizer_SensorBuildHint(void) {
    LinkSpanNei::SetSensorHint("");
    if(!gQueries) return 0;
    for(unsigned i=0;i<5;++i) {
        ShipOotRandomizerHintV2 hint{sizeof(hint)};
        if(gQueries->find_uncollected(NeiSensor_GetWish(i),&hint)!=SHIP_NATIVE_OK) continue;
        LinkSpanNei::SetSensorHint(std::string("The slate senses %g")+hint.item_name+
            "%w&in %y"+hint.area_name+"%w,&"+hint.description+"...");
        return 1;
    }
    return 0;
}
namespace LinkSpanNei {
bool StartBackends(const ShipNativeRuntime* runtime) {
    gRuntime=runtime;
    gActors=(const ShipOotActorsV1*)runtime->get_service(runtime->context,
        LINKSPAN_OOT_ACTORS_SERVICE,1,sizeof(ShipOotActorsV1));
    gQueries=(const ShipOotRandomizerV2*)runtime->get_service(runtime->context,
        LINKSPAN_OOT_RANDOMIZER_SERVICE,2,sizeof(ShipOotRandomizerV2));
    if(!gActors || !Hook(LINKSPAN_OOT_HOOK_ACTOR_INIT,sizeof(ShipOotActorHookV1),ActorInit) ||
       !Hook(LINKSPAN_OOT_HOOK_PLAY_DRAW_END,sizeof(ShipOotRenderPlayHookV1),DrawEnd)) {
        StopBackends(); return false;
    }
    FourSwordClone_EnsureRegistered();
    if(gFourSwordCloneId<0) { StopBackends(); return false; }
    return true;
}
void StopBackends() {
    if(gRuntime) for(auto handle:gHooks) gRuntime->unregister_hook(gRuntime->context,handle);
    gHooks.clear();
    if(gRuntime) { NeiBackends_ClearRuntime(); NeiWorld_Reset(); }
    if(gActors) for(auto id:gActorTypes) gActors->unregister_actor_type(id);
    gActorTypes.clear(); gTypeCount=0; gFourSwordCloneId=-1;
    gActors=nullptr; gQueries=nullptr; gRuntime=nullptr; SetSensorHint("");
}
std::string SensorCatalog() {
    std::string out;
    if(!gQueries) return out;
    for(unsigned i=1;i<gQueries->item_count();++i) {
        ShipOotRandomizerItemInfoV2 info{sizeof(info)};
        if(gQueries->item_info(i,&info)==SHIP_NATIVE_OK)
            out+=std::to_string(i)+"\t"+info.name+"\n";
    }
    return out;
}
std::string SensorWishes() {
    std::string out;
    for(unsigned i=0;i<5;++i) out+=std::to_string(NeiSensor_GetWish(i))+"\n";
    return out;
}
bool SetSensorWish(unsigned slot, unsigned item) {
    if(slot>=5 || !gQueries) return false;
    ShipOotRandomizerItemInfoV2 info{sizeof(info)};
    if(item && gQueries->item_info(item,&info)!=SHIP_NATIVE_OK) return false;
    NeiSensor_SetWish(slot,item); return true;
}
}
