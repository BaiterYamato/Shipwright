// Liga linkspan.oot.world/colliders (OotNativeWorld.cpp) ao jogo: BgCheck, WaterBox, cilindros
// do CollisionCheck e knockback do Player.
#include "OotNativeWorld.h"
#include "OotNativePlayerQuads.h"
#include "OotNativeQuadLists.h"

#include <new>
#include <cstring>

#include "soh/Enhancements/game-interactor/GameInteractor.h"

#include "z64.h"
#include "macros.h"

extern "C" {
#include "functions.h"
#include "variables.h"
#include "../../src/code/linkspan_vanilla.h"
extern PlayState* gPlayState;
}

namespace {

void* Gameplay() {
    return gPlayState;
}

void FillPoly(PlayState* play, ShipOotWorldHitV1* hit, CollisionPoly* poly, s32 bgId, const Vec3f& pos) {
    hit->hit = 1;
    hit->pos[0] = pos.x;
    hit->pos[1] = pos.y;
    hit->pos[2] = pos.z;
    if (poly) {
        hit->normal[0] = COLPOLY_GET_NORMAL(poly->normal.x);
        hit->normal[1] = COLPOLY_GET_NORMAL(poly->normal.y);
        hit->normal[2] = COLPOLY_GET_NORMAL(poly->normal.z);
        hit->floor_type = SurfaceType_GetFloorType(&play->colCtx, poly, bgId);
    }
    hit->bg_id = bgId;
}

ShipNativeStatus RaycastFloor(void* state, float x, float y, float z, ShipOotWorldHitV1* hit) {
    auto* play = static_cast<PlayState*>(state);
    CollisionPoly* poly = nullptr;
    s32 bgId = BGCHECK_SCENE;
    Vec3f pos{ x, y, z };
    const f32 floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &pos);
    if (poly && floorY > LINKSPAN_BGCHECK_Y_MIN) {
        FillPoly(play, hit, poly, bgId, Vec3f{ x, floorY, z });
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus LineTest(void* state, const float* from, const float* to, uint32_t surfaces, ShipOotWorldHitV1* hit) {
    auto* play = static_cast<PlayState*>(state);
    Vec3f a{ from[0], from[1], from[2] };
    Vec3f b{ to[0], to[1], to[2] };
    Vec3f result{};
    CollisionPoly* poly = nullptr;
    s32 bgId = BGCHECK_SCENE;
    if (BgCheck_EntityLineTest1(&play->colCtx, &a, &b, &result, &poly, (surfaces & LINKSPAN_OOT_WORLD_LINE_WALL) != 0,
                                (surfaces & LINKSPAN_OOT_WORLD_LINE_FLOOR) != 0,
                                (surfaces & LINKSPAN_OOT_WORLD_LINE_CEILING) != 0, true, &bgId)) {
        FillPoly(play, hit, poly, bgId, result);
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus WallCheck(void* state, const float* from, const float* to, float radius, float height,
                           ShipOotWorldHitV1* hit) {
    auto* play = static_cast<PlayState*>(state);
    Vec3f prev{ from[0], from[1], from[2] };
    Vec3f next{ to[0], to[1], to[2] };
    Vec3f result = next;
    CollisionPoly* poly = nullptr;
    s32 bgId = BGCHECK_SCENE;
    if (BgCheck_EntitySphVsWall2(&play->colCtx, &result, &next, &prev, radius, &poly, &bgId, height)) {
        FillPoly(play, hit, poly, bgId, result);
    } else {
        hit->pos[0] = next.x;
        hit->pos[1] = next.y;
        hit->pos[2] = next.z;
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus WaterSurface(void* state, float x, float z, float* y) {
    auto* play = static_cast<PlayState*>(state);
    WaterBox* box = nullptr;
    f32 surface = 0.0f;
    if (!WaterBox_GetSurface1(play, &play->colCtx, x, z, &surface, &box)) {
        return SHIP_NATIVE_FAILURE;
    }
    *y = surface;
    return SHIP_NATIVE_OK;
}

void* ColliderCreate(void* state, const ShipOotCylinderSpecV1& spec) {
    auto* play = static_cast<PlayState*>(state);
    auto* collider = new (std::nothrow) ColliderCylinder{};
    if (!collider) {
        return nullptr;
    }
    ColliderCylinderInit init{};
    init.base = { spec.col_type, spec.at_flags, spec.ac_flags, spec.oc1_flags, spec.oc2_flags, COLSHAPE_CYLINDER };
    init.info.elemType = spec.elem_type;
    init.info.toucher = { spec.touch_dmg_flags, spec.touch_effect, spec.touch_damage };
    init.info.bumper = { spec.bump_dmg_flags, spec.bump_effect, spec.bump_defense };
    init.info.toucherFlags = spec.touch_flags;
    init.info.bumperFlags = spec.bump_flags;
    init.info.ocElemFlags = spec.oc_elem_flags;
    init.dim.radius = spec.radius;
    init.dim.height = spec.height;
    init.dim.yShift = spec.y_shift;
    Collider_InitCylinder(play, collider);
    Collider_SetCylinder(play, collider, static_cast<Actor*>(spec.actor), &init);
    return collider;
}

void ColliderFree(void* state, void* collider) {
    auto* cylinder = static_cast<ColliderCylinder*>(collider);
    if (state) {
        Collider_DestroyCylinder(static_cast<PlayState*>(state), cylinder);
    }
    delete cylinder;
}

void ColliderSubmit(void* state, void* collider) {
    auto* play = static_cast<PlayState*>(state);
    auto* cylinder = static_cast<ColliderCylinder*>(collider);
    if (!cylinder->base.actor) {
        return;
    }
    Collider_UpdateCylinder(cylinder->base.actor, cylinder);
    if (cylinder->base.atFlags & AT_ON) {
        CollisionCheck_SetAT(play, &play->colChkCtx, &cylinder->base);
    }
    if (cylinder->base.acFlags & AC_ON) {
        CollisionCheck_SetAC(play, &play->colChkCtx, &cylinder->base);
    }
    if (cylinder->base.ocFlags1 & OC1_ON) {
        CollisionCheck_SetOC(play, &play->colChkCtx, &cylinder->base);
    }
}

void ColliderRead(void* collider, ShipOotColliderHitsV1* hits) {
    auto* cylinder = static_cast<ColliderCylinder*>(collider);
    auto& base = cylinder->base;
    if (base.atFlags & AT_HIT) {
        hits->at_hit = 1;
        hits->at_actor = base.at;
    }
    if (base.acFlags & AC_HIT) {
        hits->ac_hit = 1;
        hits->ac_actor = base.ac;
        hits->ac_dmg_flags = cylinder->info.acHitInfo ? cylinder->info.acHitInfo->toucher.dmgFlags : 0;
    }
    if (base.ocFlags1 & OC1_HIT) {
        hits->oc_hit = 1;
        hits->oc_actor = base.oc;
    }
    base.atFlags &= ~AT_HIT;
    base.acFlags &= ~AC_HIT;
    base.ocFlags1 &= ~OC1_HIT;
    base.ocFlags2 &= ~OC2_HIT_PLAYER;
    cylinder->info.toucherFlags &= ~TOUCH_HIT;
    cylinder->info.bumperFlags &= ~BUMP_HIT;
}

void Knockback(void* state, void* source, float speed, int16_t yaw, float yVelocity, uint32_t damage, uint8_t large) {
    auto* play = static_cast<PlayState*>(state);
    if (!GET_PLAYER(play)) {
        return;
    }
    if (large) {
        Actor_SetPlayerKnockbackLarge(play, static_cast<Actor*>(source), speed, yaw, yVelocity, damage);
    } else {
        Actor_SetPlayerKnockbackSmall(play, static_cast<Actor*>(source), speed, yaw, yVelocity, damage);
    }
}

} // namespace

// HOST-SLIM-001 (RFC 0029): quads reais do Link para linkspan.oot.colliders V2 (OotNativePlayerQuads.cpp).
extern "C" u8 ShipLua_CustomBodyInUse(void); // ShipLuaBootstrap.cpp (F2e)

namespace {
ColliderQuad gSavedQuads[ShipLuaHost::kOotPlayerQuadSlots];
MtxF gSavedShieldMf;
bool gHostShielding = false; // PLAYER_STATE1_SHIELDING ligado pelo DEFENSE com defending
static_assert(sizeof(MtxF) == sizeof(ShipOotPlayerQuadSpecV2::defense_matrix), "MtxF = 16 floats");

ColliderQuad* QuadSlot(Player* player, uint32_t slot) {
    return slot == SHIP_OOT_QUAD_DEFENSE ? &player->shieldQuad : &player->meleeWeaponQuads[slot];
}

// CollisionCheck_SetAT/AC não deduplicam: tirar antes de inscrever de novo, e ao desligar.
void Unlist(PlayState* play, Collider* collider) {
    auto& context = play->colChkCtx;
    const bool stableIndices = (context.sacFlags & 1) != 0;
    ShipLuaHost::Detail::EraseQuadFromList(context.colAT, context.colATCount, collider, stableIndices);
    ShipLuaHost::Detail::EraseQuadFromList(context.colAC, context.colACCount, collider, stableIndices);
}

void StopShielding(Player* player) {
    if (gHostShielding) {
        player->stateFlags1 &= ~PLAYER_STATE1_SHIELDING;
        gHostShielding = false;
    }
}

void* QuadPlayer() {
    return gPlayState ? GET_PLAYER(gPlayState) : nullptr;
}

bool QuadLuaBody() {
    return ShipLua_CustomBodyInUse() != 0;
}

ShipNativeStatus QuadReady() {
    return !gPlayState || (gPlayState->colChkCtx.sacFlags & 1) ? SHIP_NATIVE_UNSUPPORTED : SHIP_NATIVE_OK;
}

void QuadReserve(void* raw, uint32_t slot) {
    auto* player = static_cast<Player*>(raw);
    gSavedQuads[slot] = *QuadSlot(player, slot);
    if (slot == SHIP_OOT_QUAD_DEFENSE) {
        gSavedShieldMf = player->shieldMf;
        gHostShielding = false;
    }
    // Do acquire em diante só o dono inscreve: sai a inscrição do jogo já feita neste frame.
    if (gPlayState) {
        Unlist(gPlayState, &QuadSlot(player, slot)->base);
    }
}

void QuadWithdraw(void* raw, uint32_t slot) {
    auto* player = static_cast<Player*>(raw);
    if (gPlayState) {
        Unlist(gPlayState, &QuadSlot(player, slot)->base);
    }
    if (slot == SHIP_OOT_QUAD_DEFENSE) {
        StopShielding(player);
    }
}

ShipNativeStatus QuadApply(void* raw, uint32_t slot, const ShipOotPlayerQuadSpecV2& spec) {
    const ShipNativeStatus ready = QuadReady();
    if (ready != SHIP_NATIVE_OK) {
        return ready;
    }
    auto* player = static_cast<Player*>(raw);
    PlayState* play = gPlayState;
    ColliderQuad* quad = QuadSlot(player, slot);
    auto& context = play->colChkCtx;
    const bool wantsAT = (spec.at_flags & AT_ON) != 0;
    const bool wantsAC = (spec.ac_flags & AC_ON) != 0;
    if ((wantsAT && !ShipLuaHost::Detail::QuadListHasCapacity(context.colAT, context.colATCount, COLLISION_CHECK_AT_MAX,
                                                              &quad->base)) ||
        (wantsAC && !ShipLuaHost::Detail::QuadListHasCapacity(context.colAC, context.colACCount, COLLISION_CHECK_AC_MAX,
                                                              &quad->base))) {
        return SHIP_NATIVE_LIMIT; // Nao retirar a inscricao anterior nem tocar material/defesa.
    }
    const ColliderQuad previousQuad = *quad;
    const CollisionCheckContext previousContext = context;
    const MtxF previousShield = player->shieldMf;
    const auto previousFlags = player->stateFlags1;
    const bool previousHostShielding = gHostShielding;
    return ShipLuaHost::Detail::RegisterQuadTransaction(
        wantsAT, wantsAC,
        [&] {
            Unlist(play, &quad->base);
            quad->base.colType = spec.col_type;
            quad->base.atFlags = spec.at_flags;
            quad->base.acFlags = spec.ac_flags;
            quad->info.elemType = spec.elem_type;
            quad->info.toucher.dmgFlags = spec.touch_dmg_flags;
            quad->info.toucher.effect = spec.touch_effect;
            quad->info.toucher.damage = spec.touch_damage;
            quad->info.bumper.dmgFlags = spec.bump_dmg_flags;
            quad->info.bumper.effect = spec.bump_effect;
            quad->info.bumper.defense = spec.bump_defense;
            quad->info.toucherFlags = spec.touch_flags;
            quad->info.bumperFlags = spec.bump_flags;
            Vec3f vertices[4];
            for (int i = 0; i < 4; ++i) {
                vertices[i] = { spec.vertices[i][0], spec.vertices[i][1], spec.vertices[i][2] };
            }
            Collider_SetQuadVertices(quad, &vertices[0], &vertices[1], &vertices[2], &vertices[3]);
            if (slot == SHIP_OOT_QUAD_DEFENSE) {
                std::memcpy(&player->shieldMf, spec.defense_matrix, sizeof(player->shieldMf));
                if (spec.defending) {
                    if (!(player->stateFlags1 & PLAYER_STATE1_SHIELDING)) {
                        player->stateFlags1 |= PLAYER_STATE1_SHIELDING;
                        gHostShielding = true;
                    }
                } else {
                    StopShielding(player);
                }
            }
        },
        [&] {
            // Inclui flags/alvos que SetAT/AC limpam antes de poder retornar -1.
            *quad = previousQuad;
            context = previousContext;
            player->shieldMf = previousShield;
            player->stateFlags1 = previousFlags;
            gHostShielding = previousHostShielding;
        },
        [&] { return CollisionCheck_SetAT(play, &context, &quad->base); },
        [&] { return CollisionCheck_SetAC(play, &context, &quad->base); });
}

// Só o material: vértices, alvos e acertos são do frame, e o Player os refaz.
void QuadRestore(void* raw, uint32_t slot) {
    auto* player = static_cast<Player*>(raw);
    QuadWithdraw(raw, slot);
    ColliderQuad* quad = QuadSlot(player, slot);
    const ColliderQuad& saved = gSavedQuads[slot];
    quad->base.colType = saved.base.colType;
    quad->base.atFlags = saved.base.atFlags & ~(AT_HIT | AT_BOUNCED);
    quad->base.acFlags = saved.base.acFlags & ~(AC_HIT | AC_BOUNCED);
    quad->info.elemType = saved.info.elemType;
    quad->info.toucher = saved.info.toucher;
    quad->info.bumper = saved.info.bumper;
    quad->info.toucherFlags = saved.info.toucherFlags & ~(TOUCH_HIT | TOUCH_DREW_HITMARK);
    quad->info.bumperFlags = saved.info.bumperFlags & ~(BUMP_HIT | BUMP_DRAW_HITMARK);
    if (slot == SHIP_OOT_QUAD_DEFENSE) {
        player->shieldMf = gSavedShieldMf;
    }
}

void QuadRead(void* raw, uint32_t slot, ShipOotColliderHitsV1* hits) {
    const ColliderQuad* quad = QuadSlot(static_cast<Player*>(raw), slot);
    if (quad->base.atFlags & AT_HIT) {
        hits->at_hit = 1;
        hits->at_actor = quad->base.at;
    }
    if (quad->base.acFlags & AC_HIT) {
        hits->ac_hit = 1;
        hits->ac_actor = quad->base.ac;
        hits->ac_dmg_flags = quad->info.acHitInfo ? quad->info.acHitInfo->toucher.dmgFlags : 0;
    }
}

uint64_t QuadTick() {
    return gPlayState ? gPlayState->gameplayFrames : 0;
}
} // namespace

// Guards de z_player.c e z_player_lib.c: o collider é um slot arrendado do Link? Compara com o Player atual, então o
// Dark Link (EnTorch2, outro Player) nunca bate.
extern "C" u8 LinkSpan_PlayerQuadOwned(void* collider) {
    Player* player = gPlayState ? GET_PLAYER(gPlayState) : nullptr;
    if (!player || !collider) {
        return 0;
    }
    for (uint32_t slot = 0; slot < ShipLuaHost::kOotPlayerQuadSlots; ++slot) {
        if (collider == QuadSlot(player, slot)) {
            return ShipLuaHost::OotPlayerQuadSlotOwned(player, slot) ? 1 : 0;
        }
    }
    return 0;
}

extern "C" void LinkSpan_PlayerQuadsResolved(PlayState* play) {
    Player* player = play ? GET_PLAYER(play) : nullptr;
    if (player) {
        ShipLuaHost::OotPlayerQuadsResolved(player);
    }
}

namespace ShipLuaHost {

void RegisterOotWorldGameHooks() {
    static bool registered = false;
    if (registered || !GameInteractor::Instance) {
        return;
    }
    registered = true;
    OotWorldBridge bridge;
    bridge.gameplay = Gameplay;
    bridge.raycastFloor = RaycastFloor;
    bridge.lineTest = LineTest;
    bridge.wallCheck = WallCheck;
    bridge.waterSurface = WaterSurface;
    bridge.colliderCreate = ColliderCreate;
    bridge.colliderFree = ColliderFree;
    bridge.colliderSubmit = ColliderSubmit;
    bridge.colliderRead = ColliderRead;
    bridge.knockback = Knockback;
    SetOotWorldBridge(bridge);
    SetOotPlayerQuadBridge(
        { QuadPlayer, QuadLuaBody, QuadReserve, QuadApply, QuadWithdraw, QuadRestore, QuadRead, QuadTick, QuadReady });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(
        []() { ShipLuaHost::FlushOotColliders(); });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayDestroy>(
        []() { ShipLuaHost::ReleaseOotSceneColliders(); });
}

} // namespace ShipLuaHost
