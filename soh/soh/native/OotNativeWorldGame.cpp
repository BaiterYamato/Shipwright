// Liga linkspan.oot.world/colliders (OotNativeWorld.cpp) ao jogo: BgCheck, WaterBox, cilindros
// do CollisionCheck e knockback do Player.
#include "OotNativeWorld.h"

#include <new>

#include "soh/Enhancements/game-interactor/GameInteractor.h"

#include "z64.h"
#include "macros.h"

extern "C" {
#include "functions.h"
#include "variables.h"
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
    if (poly && floorY > BGCHECK_Y_MIN) {
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
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(
        []() { ShipLuaHost::FlushOotColliders(); });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayDestroy>(
        []() { ShipLuaHost::ReleaseOotSceneColliders(); });
}

} // namespace ShipLuaHost
