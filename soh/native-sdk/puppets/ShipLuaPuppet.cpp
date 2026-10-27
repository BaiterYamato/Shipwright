#include "ShipLuaPuppet.h"
#include "bindings.h"

#include <cstdint>
#include <map>
#include <memory>

#include <cstdio>

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "z64.h"
#include "objects/gameplay_keep/gameplay_keep.h"
}

namespace ShipLuaHost {
namespace {

// Mesmo layout usado pelo pause menu (func_80091738): gameplay_keep em
// +0x3800 e o objeto do Link em +0x8800, com as joint tables no fim.
constexpr uintptr_t kKeepOffset = 0x3800;
constexpr uintptr_t kLinkObjectOffset = 0x8800;
constexpr float kChildLinkScale = 0.0064f;
// Escala default de ator no MM (Actor_Init), usada pela estátua da Elegia.
constexpr float kMmElegyStatueScale = 0.01f;

// Asset lido em runtime de dentro do mm.o2r da instalação do MM, via o
// namespace cross-world "mm/" montado pelo MM Base.
static const char sElegyShellHumanDL[] = "mm/objects/gameplay_keep/gElegyShellHumanDL";

struct PuppetState {
    std::unique_ptr<uint8_t[]> buffer;
    SkelAnime skelAnime{};
    bool ready = false;
    bool isStatue = false;
};

std::map<Actor*, std::unique_ptr<PuppetState>> sPuppets;

PuppetState* FindPuppet(Actor* actor) {
    const auto it = sPuppets.find(actor);
    return it == sPuppets.end() ? nullptr : it->second.get();
}

} // namespace
} // namespace ShipLuaHost

// O serviço de atores chama estes corpos através dos trampolins do provider.
extern "C" {

void ShipLuaPuppetUpdate(Actor* actor, PlayState* play) {
    ShipLuaHost::PuppetState* state = ShipLuaHost::FindPuppet(actor);
    if (state == nullptr || !state->ready) {
        return;
    }
    if (state->isStatue && !ShipLuaHost::StatueAvailable()) {
        ShipLuaHost::gHost.engine->kill_actor(actor);
        return;
    }
    if (!state->isStatue) {
        ShipLuaHost::gHost.LinkAnimation_Update(play, &state->skelAnime);
    }
    actor->focus.pos = actor->world.pos;
    actor->focus.pos.y += 40.0f;
}

void ShipLuaStatueDraw(Actor* actor, PlayState* play) {
    ShipLuaHost::PuppetState* state = ShipLuaHost::FindPuppet(actor);
    if (state == nullptr || !state->ready) {
        return;
    }
    if (ShipLuaHost::StatueAvailable()) {
        ShipLuaHost::gHost.actors->draw_display_list(play, ShipLuaHost::sElegyShellHumanDL, 0);
    }
}

void ShipLuaPuppetDraw(Actor* actor, PlayState* play) {
    ShipLuaHost::PuppetState* state = ShipLuaHost::FindPuppet(actor);
    if (state == nullptr || !state->ready) {
        return;
    }
    static u8 sSwordAndShield[2] = { PLAYER_SWORD_NONE, 0 };
    uint8_t* keep = state->buffer.get() + ShipLuaHost::kKeepOffset;
    uint8_t* linkObject = state->buffer.get() + ShipLuaHost::kLinkObjectOffset;

    ShipLuaHost::gHost.FrameInterpolation_RecordOpenChild(__FILE__, __LINE__);
    GraphicsContext* __gfxCtx = play->state.gfxCtx;
    ShipLuaHost::gHost.func_80093C80(play);
    ShipLuaHost::gHost.Gfx_SetupDL_25Opa(play->state.gfxCtx);
    ShipLuaHost::gHost.gSPSegment(POLY_OPA_DISP++, 0x04, reinterpret_cast<uintptr_t>(keep));
    ShipLuaHost::gHost.gSPSegment(POLY_OPA_DISP++, 0x06, reinterpret_cast<uintptr_t>(linkObject));
    ShipLuaHost::gHost.Player_DrawImpl(play, state->skelAnime.skeleton, state->skelAnime.jointTable,
                                       state->skelAnime.dListCount, 0, 0, 0, 0,
                                       ShipLuaHost::gHost.Player_OverrideLimbDrawPause, nullptr, sSwordAndShield);
    ShipLuaHost::gHost.FrameInterpolation_RecordCloseChild();
}

} // extern "C"

namespace ShipLuaHost {

bool ShipLuaPuppet_Attach(void* actorPtr, void* playPtr) {
    Actor* actor = static_cast<Actor*>(actorPtr);
    PlayState* play = static_cast<PlayState*>(playPtr);
    if (actor == nullptr || play == nullptr) {
        return false;
    }

    const s16 childObjectId = gHost.gLinkObjectIds[1];
    const size_t keepSize =
        gHost.gObjectTable[OBJECT_GAMEPLAY_KEEP].vromEnd - gHost.gObjectTable[OBJECT_GAMEPLAY_KEEP].vromStart;
    const size_t linkSize = gHost.gObjectTable[childObjectId].vromEnd - gHost.gObjectTable[childObjectId].vromStart;
    const size_t jointBytes = 2 * PLAYER_LIMB_MAX * sizeof(Vec3s) + 0x40;
    const size_t total = kLinkObjectOffset + ALIGN16(linkSize) + jointBytes;

    auto state = std::make_unique<PuppetState>();
    state->buffer = std::make_unique<uint8_t[]>(total);

    gHost.DmaMgr_SendRequest1(state->buffer.get() + kKeepOffset, gHost.gObjectTable[OBJECT_GAMEPLAY_KEEP].vromStart,
                              keepSize, __FILE__, __LINE__);
    gHost.DmaMgr_SendRequest1(state->buffer.get() + kLinkObjectOffset, gHost.gObjectTable[childObjectId].vromStart,
                              linkSize, __FILE__, __LINE__);

    Vec3s* jointTable = reinterpret_cast<Vec3s*>(
        ALIGN16(reinterpret_cast<uintptr_t>(state->buffer.get()) + kLinkObjectOffset + linkSize));

    // Restaura segmentos inclusive se a carga do recurso lançar uma exceção.
    {
        struct SegmentScope {
            uintptr_t keep = gHost.gSegments[4];
            uintptr_t object = gHost.gSegments[6];
            ~SegmentScope() {
                gHost.gSegments[4] = keep;
                gHost.gSegments[6] = object;
            }
        } scope;
        gHost.gSegments[4] = VIRTUAL_TO_PHYSICAL(state->buffer.get() + kKeepOffset);
        gHost.gSegments[6] = VIRTUAL_TO_PHYSICAL(state->buffer.get() + kLinkObjectOffset);
        gHost.SkelAnime_InitLink(play, &state->skelAnime, gHost.gPlayerSkelHeaders[1],
                                 (LinkAnimationHeader*)gPlayerAnim_link_normal_wait, 9, jointTable, jointTable,
                                 PLAYER_LIMB_MAX);
    }

    state->ready = true;

    gHost.Actor_SetScale(actor, kChildLinkScale);

    sPuppets[actor] = std::move(state);
    return true;
}

bool ShipLuaPuppet_AttachStatue(void* actorPtr) {
    Actor* actor = static_cast<Actor*>(actorPtr);
    if (actor == nullptr) {
        return false;
    }
    // MM Base precisa estar ativo e publicar o recurso; nunca desenha um caminho ausente.
    if (!StatueAvailable()) {
        std::fputs("Bonecos: estátua recusada; MM Base ou recurso ausente.\n", stderr);
        return false;
    }
    auto state = std::make_unique<PuppetState>();
    state->isStatue = true;
    state->ready = true;

    gHost.Actor_SetScale(actor, kMmElegyStatueScale);

    sPuppets[actor] = std::move(state);
    return true;
}

void ShipLuaPuppet_HandleActorDestroy(void* actor) {
    sPuppets.erase(static_cast<Actor*>(actor));
}

void ShipLuaPuppet_Reset() {
    sPuppets.clear();
}

} // namespace ShipLuaHost
