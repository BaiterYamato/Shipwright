#include "ShipLuaPuppet.h"

#include <cstdint>
#include <map>
#include <memory>

#include <spdlog/spdlog.h>

#include "soh/frame_interpolation.h"
#include "soh/ResourceManagerHelpers.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "z64.h"
#include "objects/gameplay_keep/gameplay_keep.h"

extern PlayState* gPlayState;
extern FlexSkeletonHeader* gPlayerSkelHeaders[];
extern s16 gLinkObjectIds[];
s32 Player_OverrideLimbDrawPause(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* arg);
void Gfx_DrawDListOpa(PlayState* play, Gfx* dlist);
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
// namespace cross-world "mm/" montado pelo bootstrap.
static const ALIGN_ASSET(2) char sElegyShellHumanDL[] = "__OTR__mm/objects/gameplay_keep/gElegyShellHumanDL";

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

// Callbacks de ator com ABI C: além do ponteiro de função esperado pelo
// engine, o macro OPEN_DISPS declara FrameInterpolation_* em escopo de bloco e
// a linkage precisa casar com as definições extern "C".
extern "C" {

static void ShipLuaPuppetUpdate(Actor* actor, PlayState* play) {
    ShipLuaHost::PuppetState* state = ShipLuaHost::FindPuppet(actor);
    if (state == nullptr || !state->ready) {
        return;
    }
    if (!state->isStatue) {
        LinkAnimation_Update(play, &state->skelAnime);
    }
    actor->focus.pos = actor->world.pos;
    actor->focus.pos.y += 40.0f;
}

static void ShipLuaStatueDraw(Actor* actor, PlayState* play) {
    ShipLuaHost::PuppetState* state = ShipLuaHost::FindPuppet(actor);
    if (state == nullptr || !state->ready) {
        return;
    }
    Gfx_DrawDListOpa(play, (Gfx*)ShipLuaHost::sElegyShellHumanDL);
}

static void ShipLuaPuppetDraw(Actor* actor, PlayState* play) {
    ShipLuaHost::PuppetState* state = ShipLuaHost::FindPuppet(actor);
    if (state == nullptr || !state->ready) {
        return;
    }
    static u8 sSwordAndShield[2] = { PLAYER_SWORD_NONE, 0 };
    uint8_t* keep = state->buffer.get() + ShipLuaHost::kKeepOffset;
    uint8_t* linkObject = state->buffer.get() + ShipLuaHost::kLinkObjectOffset;

    OPEN_DISPS(play->state.gfxCtx);
    func_80093C80(play);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x04, reinterpret_cast<uintptr_t>(keep));
    gSPSegment(POLY_OPA_DISP++, 0x06, reinterpret_cast<uintptr_t>(linkObject));
    Player_DrawImpl(play, state->skelAnime.skeleton, state->skelAnime.jointTable, state->skelAnime.dListCount, 0, 0, 0,
                    0, Player_OverrideLimbDrawPause, nullptr, sSwordAndShield);
    CLOSE_DISPS(play->state.gfxCtx);
}

} // extern "C"

namespace ShipLuaHost {

bool ShipLuaPuppet_Attach(void* actorPtr, void* playPtr) {
    Actor* actor = static_cast<Actor*>(actorPtr);
    PlayState* play = static_cast<PlayState*>(playPtr);
    if (actor == nullptr || play == nullptr) {
        return false;
    }

    const s16 childObjectId = gLinkObjectIds[1];
    const size_t keepSize = gObjectTable[OBJECT_GAMEPLAY_KEEP].vromEnd - gObjectTable[OBJECT_GAMEPLAY_KEEP].vromStart;
    const size_t linkSize = gObjectTable[childObjectId].vromEnd - gObjectTable[childObjectId].vromStart;
    const size_t jointBytes = 2 * PLAYER_LIMB_MAX * sizeof(Vec3s) + 0x40;
    const size_t total = kLinkObjectOffset + ALIGN16(linkSize) + jointBytes;

    auto state = std::make_unique<PuppetState>();
    state->buffer = std::make_unique<uint8_t[]>(total);

    DmaMgr_SendRequest1(state->buffer.get() + kKeepOffset, gObjectTable[OBJECT_GAMEPLAY_KEEP].vromStart, keepSize,
                        __FILE__, __LINE__);
    DmaMgr_SendRequest1(state->buffer.get() + kLinkObjectOffset, gObjectTable[childObjectId].vromStart, linkSize,
                        __FILE__, __LINE__);

    Vec3s* jointTable =
        reinterpret_cast<Vec3s*>(ALIGN16(reinterpret_cast<uintptr_t>(state->buffer.get()) + kLinkObjectOffset +
                                         linkSize));

    // SkelAnime_InitLink resolve ponteiros via segmentos 4/6, como no pause.
    const uintptr_t previousKeep = gSegments[4];
    const uintptr_t previousObject = gSegments[6];
    gSegments[4] = VIRTUAL_TO_PHYSICAL(state->buffer.get() + kKeepOffset);
    gSegments[6] = VIRTUAL_TO_PHYSICAL(state->buffer.get() + kLinkObjectOffset);
    SkelAnime_InitLink(play, &state->skelAnime, gPlayerSkelHeaders[1],
                       (LinkAnimationHeader*)gPlayerAnim_link_normal_wait, 9, jointTable, jointTable,
                       PLAYER_LIMB_MAX);
    gSegments[4] = previousKeep;
    gSegments[6] = previousObject;

    state->ready = true;

    Actor_SetScale(actor, kChildLinkScale);
    actor->update = ShipLuaPuppetUpdate;
    actor->draw = ShipLuaPuppetDraw;

    sPuppets[actor] = std::move(state);
    SPDLOG_INFO("ShipLua puppet child-link anexado ao ator {}", static_cast<void*>(actor));
    return true;
}

bool ShipLuaPuppet_AttachStatue(void* actorPtr) {
    Actor* actor = static_cast<Actor*>(actorPtr);
    if (actor == nullptr) {
        return false;
    }
    // O draw desreferencia o resource da DL: sem o mm.o2r vizinho montado, o
    // caminho mm/ não resolve e o interpretador quebra. Confira ANTES.
    if (!ResourceMgr_FileExists(sElegyShellHumanDL)) {
        SPDLOG_WARN("ShipLua: assets da estátua indisponíveis — o mm.o2r do MM não está montado "
                    "(coloque as duas instalações lado a lado ou use SHIPLUA_MM_ROOT)");
        return false;
    }
    auto state = std::make_unique<PuppetState>();
    state->isStatue = true;
    state->ready = true;

    Actor_SetScale(actor, kMmElegyStatueScale);
    actor->update = ShipLuaPuppetUpdate;
    actor->draw = ShipLuaStatueDraw;

    sPuppets[actor] = std::move(state);
    SPDLOG_INFO("ShipLua estátua da Elegia (MM) anexada ao ator {}", static_cast<void*>(actor));
    return true;
}

void ShipLuaPuppet_HandleActorDestroy(void* actor) {
    sPuppets.erase(static_cast<Actor*>(actor));
}

void ShipLuaPuppet_Reset() {
    sPuppets.clear();
}

} // namespace ShipLuaHost
