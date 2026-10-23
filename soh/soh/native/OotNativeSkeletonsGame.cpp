// Liga linkspan.oot.skeletons (OotNativeSkeletons.cpp) ao SkelAnime do jogo. As tabelas de
// juntas ficam no heap do host, fora da arena da cena, para o esqueleto sobreviver à troca
// de cena até o mod destruir.
#include "OotNativeSkeletons.h"
#include "OotNativePlayerBody.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "OotNativeView.h"

#include <map>
#include <new>
#include <string>
#include <vector>

#include "soh/ResourceManagerHelpers.h"
#include "soh/resource/type/Animation.h"

#include "z64.h"
#include "macros.h"

extern "C" {
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace {

struct SkeletonInstance {
    SkelAnime skelAnime{};
    std::vector<Vec3s> joints;
    std::vector<Vec3s> morph;
    bool flex = false;
    bool link = false;
};

constexpr u8 kSkeletonTypeFlex = 1;
constexpr u8 kSkeletonTypeCurve = 2;

ShipNativeStatus Create(const char* skeleton, const char* animation, void** out) {
    if (!ResourceMgr_FileExists(skeleton) || !ResourceMgr_FileExists(animation)) {
        return SHIP_NATIVE_FAILURE;
    }
    auto* header = ResourceMgr_LoadSkeletonByName(skeleton, nullptr);
    if (!header || header->skeletonType == kSkeletonTypeCurve || !header->limbCount) {
        return SHIP_NATIVE_FAILURE;
    }
    auto* instance = new (std::nothrow) SkeletonInstance;
    if (!instance) {
        return SHIP_NATIVE_FAILURE;
    }
    const s32 limbCount = header->limbCount + 1;
    instance->joints.assign(limbCount, Vec3s{});
    instance->morph.assign(limbCount, Vec3s{});
    instance->flex = header->skeletonType == kSkeletonTypeFlex;
    // O SkelAnime aceita os caminhos "__OTR__..." e registra o esqueleto para troca de assets.
    auto* skelPath = const_cast<char*>(skeleton);
    auto* animPath = reinterpret_cast<AnimationHeader*>(const_cast<char*>(animation));
    if (instance->flex) {
        SkelAnime_InitFlex(gPlayState, &instance->skelAnime, reinterpret_cast<FlexSkeletonHeader*>(skelPath), animPath,
                           instance->joints.data(), instance->morph.data(), limbCount);
    } else {
        SkelAnime_Init(gPlayState, &instance->skelAnime, reinterpret_cast<SkeletonHeader*>(skelPath), animPath,
                       instance->joints.data(), instance->morph.data(), limbCount);
    }
    *out = instance;
    return SHIP_NATIVE_OK;
}

void Destroy(void* state) {
    auto* instance = static_cast<SkeletonInstance*>(state);
    ResourceMgr_UnregisterSkeleton(&instance->skelAnime);
    delete instance;
}

// Normal e Link têm layouts diferentes (z_skelanime.c:900, :1267): o formato é conferido antes de qualquer Init/Change.
// ResourceManagerHelpers.cpp:275; o caminho vai sem o "__OTR__" do InternPath.
bool AnimationMatches(const char* animation, bool link) {
    if (!ResourceMgr_FileExists(animation)) {
        return false;
    }
    const auto resource =
        std::dynamic_pointer_cast<SOH::Animation>(ResourceMgr_GetResourceByNameHandlingMQ(animation + 7));
    return resource && resource->type == (link ? SOH::AnimationType::Link : SOH::AnimationType::Normal) &&
           resource->animationData.animationHeader.common.frameCount > 0;
}

ShipNativeStatus Play(void* state, const char* animation, float speed, uint8_t mode, float morphFrames) {
    auto* instance = static_cast<SkeletonInstance*>(state);
    if ((instance->link && !gPlayState) || !AnimationMatches(animation, instance->link)) {
        return SHIP_NATIVE_FAILURE;
    }
    const f32 last = static_cast<f32>(Animation_GetLastFrame(const_cast<char*>(animation)));
    if (instance->link) {
        // play_animation do prefixo V1 num esqueleto LINK: o Update dele chama LinkAnimation_Update.
        LinkAnimation_Change(gPlayState, &instance->skelAnime,
                             reinterpret_cast<LinkAnimationHeader*>(const_cast<char*>(animation)), speed, 0.0f, last,
                             mode, morphFrames);
    } else {
        Animation_Change(&instance->skelAnime, reinterpret_cast<AnimationHeader*>(const_cast<char*>(animation)), speed,
                         0.0f, last, mode, morphFrames);
    }
    return SHIP_NATIVE_OK;
}

uint8_t Update(void* state) {
    auto* instance = static_cast<SkeletonInstance*>(state);
    if (instance->link) {
        return LinkAnimation_Update(gPlayState, &instance->skelAnime) ? 1 : 0;
    }
    return SkelAnime_Update(&instance->skelAnime) ? 1 : 0;
}

void Frame(void* state, float* frame, float* lastFrame) {
    const auto& skelAnime = static_cast<SkeletonInstance*>(state)->skelAnime;
    *frame = skelAnime.curFrame;
    *lastFrame = skelAnime.endFrame;
}

void Draw(void* play, void* state) {
    auto* instance = static_cast<SkeletonInstance*>(state);
    auto* playState = static_cast<PlayState*>(play);
    Gfx_SetupDL_25Opa(playState->state.gfxCtx);
    if (instance->flex) {
        SkelAnime_DrawFlexOpa(playState, instance->skelAnime.skeleton, instance->skelAnime.jointTable,
                              instance->skelAnime.dListCount, nullptr, nullptr, nullptr);
    } else {
        SkelAnime_DrawOpa(playState, instance->skelAnime.skeleton, instance->skelAnime.jointTable, nullptr, nullptr,
                          nullptr);
    }
}

// HOST-SLIM-001 (RFC 0029): skeletons V2. LINK segue o Player (z_player.c, SkelAnime_InitLink com flags 9):
// juntas = membros + 1, mais 2 bytes, alinhadas em 16 pelo próprio InitLink; 3 Vec3s de folga cobrem os dois.
ShipNativeStatus CreateEx(uint32_t format, const char* skeleton, const char* animation, const int16_t* base, void** out,
                          float* assetLastFrame) {
    if (!gPlayState || !ResourceMgr_FileExists(skeleton) || !ResourceMgr_FileExists(animation)) {
        return SHIP_NATIVE_FAILURE;
    }
    auto* header = ResourceMgr_LoadSkeletonByName(skeleton, nullptr);
    if (!header || header->skeletonType == kSkeletonTypeCurve || !header->limbCount ||
        (format == SHIP_OOT_SKELETON_LINK && header->skeletonType != kSkeletonTypeFlex)) {
        return SHIP_NATIVE_FAILURE;
    }
    if (!AnimationMatches(animation, format == SHIP_OOT_SKELETON_LINK)) {
        return SHIP_NATIVE_FAILURE;
    }
    auto* instance = new (std::nothrow) SkeletonInstance;
    if (!instance) {
        return SHIP_NATIVE_FAILURE;
    }
    const s32 limbCount = header->limbCount + 1;
    instance->joints.assign(limbCount + 3, Vec3s{});
    instance->morph.assign(limbCount + 3, Vec3s{});
    instance->flex = header->skeletonType == kSkeletonTypeFlex;
    instance->link = format == SHIP_OOT_SKELETON_LINK;
    auto* skelPath = const_cast<char*>(skeleton);
    if (instance->link) {
        SkelAnime_InitLink(gPlayState, &instance->skelAnime, reinterpret_cast<FlexSkeletonHeader*>(skelPath),
                           reinterpret_cast<LinkAnimationHeader*>(const_cast<char*>(animation)), 9,
                           instance->joints.data(), instance->morph.data(), limbCount);
    } else if (instance->flex) {
        SkelAnime_InitFlex(gPlayState, &instance->skelAnime, reinterpret_cast<FlexSkeletonHeader*>(skelPath),
                           reinterpret_cast<AnimationHeader*>(const_cast<char*>(animation)), instance->joints.data(),
                           instance->morph.data(), limbCount);
    } else {
        SkelAnime_Init(gPlayState, &instance->skelAnime, reinterpret_cast<SkeletonHeader*>(skelPath),
                       reinterpret_cast<AnimationHeader*>(const_cast<char*>(animation)), instance->joints.data(),
                       instance->morph.data(), limbCount);
    }
    instance->skelAnime.baseTransl = { base[0], base[1], base[2] };
    instance->skelAnime.prevTransl = instance->skelAnime.baseTransl;
    *assetLastFrame = static_cast<float>(Animation_GetLastFrame(const_cast<char*>(animation)));
    *out = instance;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus PlayEx(void* state, const char* animation, float speed, uint8_t mode, float morphFrames, bool reverse,
                        float* assetLastFrame) {
    auto* instance = static_cast<SkeletonInstance*>(state);
    if (!gPlayState || !AnimationMatches(animation, instance->link)) {
        return SHIP_NATIVE_FAILURE;
    }
    const f32 last = static_cast<f32>(Animation_GetLastFrame(const_cast<char*>(animation)));
    const f32 start = reverse ? last : 0.0f;
    const f32 end = reverse ? 0.0f : last;
    const f32 signedSpeed = reverse ? -speed : speed;
    if (instance->link) {
        LinkAnimation_Change(gPlayState, &instance->skelAnime,
                             reinterpret_cast<LinkAnimationHeader*>(const_cast<char*>(animation)), signedSpeed, start,
                             end, mode, morphFrames);
    } else {
        Animation_Change(&instance->skelAnime, reinterpret_cast<AnimationHeader*>(const_cast<char*>(animation)),
                         signedSpeed, start, end, mode, morphFrames);
    }
    *assetLastFrame = last;
    return SHIP_NATIVE_OK;
}

// Não se chama AnimationInfo: o z64.h:1655 tem um typedef com esse nome.
ShipNativeStatus SkeletonAnimationInfo(void* state, const char* animation, ShipOotAnimationInfoV2* info) {
    info->present = ResourceMgr_FileExists(animation) ? 1 : 0;
    info->loaded = info->present && AnimationMatches(animation, static_cast<SkeletonInstance*>(state)->link) ? 1 : 0;
    if (info->loaded) {
        info->asset_last_frame = static_cast<float>(Animation_GetLastFrame(const_cast<char*>(animation)));
    }
    return SHIP_NATIVE_OK;
}

void AnimationState(void* state, float* frame, float* endFrame, float* speed) {
    const auto& skelAnime = static_cast<SkeletonInstance*>(state)->skelAnime;
    *frame = skelAnime.curFrame;
    *endFrame = skelAnime.endFrame;
    *speed = skelAnime.playSpeed;
}

struct LimbTrampoline {
    void (*limb)(void* context, int32_t limb);
    void* context;
};

// PostLimbDrawOpa (z64animation.h:117), o mesmo tipo no DrawOpa e no DrawFlexOpa. O callback do mod roda num escopo
// de render filho (aninhável: o Leave desfaz o que o mod empilhou), com a matriz do membro no topo.
void PostLimb(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* arg) {
    const auto* trampoline = static_cast<LimbTrampoline*>(arg);
    struct MatrixScope {
        MatrixScope() {
            Matrix_Push();
        }
        ~MatrixScope() {
            Matrix_Pop();
        }
    } matrixScope;
    struct RenderScope {
        explicit RenderScope(void* gfx) {
            ShipLuaHost::EnterOotRenderScope(gfx);
        }
        ~RenderScope() {
            ShipLuaHost::LeaveOotRenderScope();
        }
    } renderScope(play->state.gfxCtx);
    trampoline->limb(trampoline->context, limbIndex);
}

void DrawEx(void* play, void* actor, void* state, void (*limb)(void* context, int32_t limb), void* context) {
    auto* instance = static_cast<SkeletonInstance*>(state);
    auto* playState = static_cast<PlayState*>(play);
    LimbTrampoline trampoline{ limb, context };
    Gfx_SetupDL_25Opa(playState->state.gfxCtx);
    if (instance->flex) {
        SkelAnime_DrawFlexOpa(playState, instance->skelAnime.skeleton, instance->skelAnime.jointTable,
                              instance->skelAnime.dListCount, nullptr, PostLimb, &trampoline);
    } else {
        SkelAnime_DrawOpa(playState, instance->skelAnime.skeleton, instance->skelAnime.jointTable, nullptr, PostLimb,
                          &trampoline);
    }
}

uint64_t GameplayTick() {
    return gPlayState ? gPlayState->gameplayFrames : 0;
}
} // namespace

namespace ShipLuaHost {

void RegisterOotSkeletonsGameBridge() {
    OotSkeletonsBridge bridge;
    bridge.create = Create;
    bridge.destroy = Destroy;
    bridge.play = Play;
    bridge.update = Update;
    bridge.frame = Frame;
    bridge.draw = Draw;
    bridge.createEx = CreateEx;
    bridge.playEx = PlayEx;
    bridge.animationInfo = SkeletonAnimationInfo;
    bridge.state = AnimationState;
    bridge.drawEx = DrawEx;
    bridge.tick = GameplayTick;
    SetOotSkeletonsBridge(bridge);
}

} // namespace ShipLuaHost

// HOST-SLIM-001 (RFC 0029): ator do corpo nativo, no molde do corpo Lua (ShipLuaBootstrap.cpp:4486-4566).
namespace {
struct BodyGame {
    uint64_t skeleton = 0;
    Actor* actor = nullptr;
    float groundOffset = 0.0f;
    std::map<uint32_t, std::string> segments; // com "__OTR__"
};
std::map<Actor*, BodyGame*> gBodyActors;

SkelAnime* BodySkelAnime(const BodyGame& body) {
    auto* instance = static_cast<SkeletonInstance*>(ShipLuaHost::OotSkeletonInstance(body.skeleton));
    return instance ? &instance->skelAnime : nullptr;
}
} // namespace

extern "C" void LinkSpan_NativeBodyActorUpdate(Actor* actor, PlayState* play) {
    Player* player = GET_PLAYER(play);
    if (!player || !gBodyActors.count(actor)) {
        return;
    }
    player->stateFlags2 |= PLAYER_STATE2_DISABLE_DRAW;
    actor->world.pos = player->actor.world.pos;
    actor->shape.rot = player->actor.shape.rot;
    actor->scale = player->actor.scale;
    ShipLuaHost::OotPlayerBodyTick(play, player);
}

extern "C" void LinkSpan_NativeBodyActorDraw(Actor* actor, PlayState* play) {
    const auto found = gBodyActors.find(actor);
    Player* player = GET_PLAYER(play);
    if (found == gBodyActors.end() || !player) {
        return;
    }
    const BodyGame& body = *found->second;
    // O MOVEACTOR roda depois de Actor_UpdateAll: refaz a matriz pela posição atual do Player (como o corpo Lua).
    actor->world.pos = player->actor.world.pos;
    actor->shape.rot = player->actor.shape.rot;
    // groundOffset no lugar do shape.yOffset do corpo Lua (:3819, :4344): ajuste visual, o Player não sai do lugar.
    Matrix_SetTranslateRotateYXZ(actor->world.pos.x, actor->world.pos.y + body.groundOffset, actor->world.pos.z,
                                 &actor->shape.rot);
    Matrix_Scale(actor->scale.x, actor->scale.y, actor->scale.z, MTXMODE_APPLY);
    ShipLuaHost::EnterOotRenderScope(play->state.gfxCtx);
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    for (const auto& [segment, path] : body.segments) {
        char* data = ResourceMgr_GetResourceDataByNameHandlingMQ(path.c_str());
        if (data) {
            gSPSegment(POLY_OPA_DISP++, segment, reinterpret_cast<uintptr_t>(data));
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
    if (ShipLuaHost::OotPlayerBodyDraw(play, player, actor)) {
        ShipLuaHost::GetOotNativeSkeletonsServiceV2().draw_ex(play, actor, body.skeleton);
    }
    ShipLuaHost::LeaveOotRenderScope();
}

extern "C" u8 ShipLua_CustomBodyInUse(void); // ShipLuaBootstrap.cpp

namespace {
bool LuaBodyActive() {
    return ShipLua_CustomBodyInUse() != 0;
}

ShipNativeStatus BodyActivate(uint64_t skeleton, float groundOffset, void** out) {
    PlayState* play = gPlayState;
    Player* player = play ? GET_PLAYER(play) : nullptr;
    if (!player) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    auto* body = new (std::nothrow) BodyGame;
    if (!body) {
        return SHIP_NATIVE_FAILURE;
    }
    Actor* actor = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_ITEM00, player->actor.world.pos.x,
                               player->actor.world.pos.y, player->actor.world.pos.z, 0, 0, 0, 0);
    if (!actor) {
        delete body;
        return SHIP_NATIVE_FAILURE;
    }
    actor->update = LinkSpan_NativeBodyActorUpdate;
    actor->draw = LinkSpan_NativeBodyActorDraw;
    actor->scale = player->actor.scale;
    actor->shape.yOffset = 0.0f; // o Init do EN_ITEM00 ajusta para o coletável
    // Sala -1: o ator não morre na troca de sala da mesma cena (z_actor.c:3551) e o ponteiro em gBodyActors vale.
    actor->room = -1;
    // Sem UPDATE_DURING_OCARINA, a ocarina na mão para o update do ator (z_actor.c:2691, :2729) e o corpo congela; sem
    // as de culling, depois de um teleporte a posição antiga sai do volume e o ator deixa de seguir o Player.
    actor->flags |=
        ACTOR_FLAG_UPDATE_DURING_OCARINA | ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    body->skeleton = skeleton;
    body->actor = actor;
    body->groundOffset = groundOffset;
    try {
        gBodyActors.emplace(actor, body);
    } catch (...) {
        Actor_Kill(actor);
        delete body;
        return SHIP_NATIVE_FAILURE;
    }
    player->stateFlags2 |= PLAYER_STATE2_DISABLE_DRAW;
    *out = body;
    return SHIP_NATIVE_OK;
}

void BodyDeactivate(void* instance, bool inScene, bool showLink) {
    auto* body = static_cast<BodyGame*>(instance);
    gBodyActors.erase(body->actor);
    if (inScene) {
        Actor_Kill(body->actor);
        Player* player = gPlayState ? GET_PLAYER(gPlayState) : nullptr;
        if (showLink && player) {
            player->stateFlags2 &= ~PLAYER_STATE2_DISABLE_DRAW;
        }
    }
    delete body;
}

ShipNativeStatus BodySegments(void* instance, const std::map<uint32_t, std::string>& segments) {
    std::map<uint32_t, std::string> next;
    for (const auto& [segment, path] : segments) {
        std::string prefixed = "__OTR__" + path;
        if (!ResourceMgr_FileExists(prefixed.c_str())) {
            return SHIP_NATIVE_FAILURE;
        }
        next.emplace(segment, std::move(prefixed));
    }
    static_cast<BodyGame*>(instance)->segments.swap(next);
    return SHIP_NATIVE_OK;
}

// Igual ao corpo Lua (ShipLuaBootstrap.cpp:3140-3153): UPDATEY devolve a raiz Y à base e arg3 = 0 descarta o delta Y;
// sem `move`, NOMOVE só neutraliza a raiz.
void BodyRootMotion(void* instance, bool move, bool restart) {
    const auto& body = *static_cast<BodyGame*>(instance);
    SkelAnime* skelAnime = BodySkelAnime(body);
    PlayState* play = gPlayState;
    Player* player = play ? GET_PLAYER(play) : nullptr;
    if (!skelAnime || !player) {
        return;
    }
    skelAnime->movementFlags |= ANIM_FLAG_UPDATEY;
    if (!move || restart) {
        skelAnime->movementFlags |= ANIM_FLAG_NOMOVE;
    }
    AnimationContext_SetMoveActor(play, &player->actor, skelAnime, 0.0f);
}

void BodyRootStop(void* instance) {
    SkelAnime* skelAnime = BodySkelAnime(*static_cast<BodyGame*>(instance));
    if (skelAnime) {
        skelAnime->movementFlags |= ANIM_FLAG_NOMOVE;
    }
}
} // namespace

namespace ShipLuaHost {
void RegisterOotPlayerBodyGameBridge() {
    SetOotPlayerBodyBridge({ LuaBodyActive, BodyActivate, BodyDeactivate, BodySegments, BodyRootMotion, BodyRootStop });
    // OnActorDestroy roda antes da arena liberar o ator. A limpeza normal ja apagou sua entrada do mapa.
    static const auto actorDestroyHook =
        GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorDestroy>([](void* rawActor) {
            if (!gBodyActors.count(static_cast<Actor*>(rawActor)))
                return;
            // O ator saiu por outro mecanismo: devolve o Link inclusive se a reativacao posterior falhar.
            Player* player = gPlayState ? GET_PLAYER(gPlayState) : nullptr;
            if (player)
                player->stateFlags2 &= ~PLAYER_STATE2_DISABLE_DRAW;
            // Limpeza existente, sem tocar o ator morto, sem callback da DLL e sem avancar scene_epoch.
            // Guarda a definicao: PlayerUpdate reativa com outra geracao; o mod readquire o controle.
            OotPlayerBodySceneEnded();
        });
    (void)actorDestroyHook;
}
} // namespace ShipLuaHost
