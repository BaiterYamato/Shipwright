#include "PersistentDebris.h"
#include "rigid_debris.h"
#include <cstring>
#include <libultraship/bridge/consolevariablebridge.h>
#include <spdlog/spdlog.h>
extern "C" {
#include "z64.h"
#include "macros.h"
}
#include "functions.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/frame_interpolation.h"
#include "objects/gameplay_dangeon_keep/gameplay_dangeon_keep.h"
#include "objects/object_tsubo/object_tsubo.h"
#include "objects/object_bombiwa/object_bombiwa.h"

namespace {
using namespace rigid_debris;
Pool pool;
struct Visual {
    Gfx* displayList = nullptr;
    void* segment = nullptr;
    int objectId = 0, slot = -1, epoch = 0, color = -1;
    float scale = 1;
    bool translucent = false;
};
std::array<Visual, Pool::capacity> visuals{};
PlayState* session = nullptr;
int scene = -1, room = -1, nextEpoch = 0;
unsigned lastTick = 0;
bool hasTick = false, hasPlayer = false;
Vec3 previousPlayer{};
std::array<Vec3, 16> explosions{};
unsigned explosionTick = 0, explosionCount = 0;
bool hasExplosionTick = false;

Vec3 Vector(const float* p) {
    return { p[0], p[1], p[2] };
}
Vec3 Vector(const Vec3f& p) {
    return { p.x, p.y, p.z };
}
Vec3f GameVector(Vec3 p) {
    return { p.x, p.y, p.z };
}
bool ValidVector(const float* p) {
    return p && Finite(Vector(p));
}
bool Paused(const PlayState* play) {
    return play->pauseCtx.state != 0 || play->pauseCtx.debugState != 0;
}
// Diagnóstico por CVar (padrão desligado): aceite de caco e estado do pool a cada segundo de jogo.
bool Log() {
    return CVarGetInteger("gDeveloperTools.PersistentDebrisLog", 0) != 0;
}
void Clear() {
    pool.Reset();
    visuals = {};
    hasTick = hasPlayer = hasExplosionTick = false;
    explosionCount = 0;
}
void Cull(PlayState* play) {
    for (std::size_t i = 0; i < Pool::capacity; ++i)
        if (pool.Get(i).active) {
            const auto& v = visuals[i];
            const int slot = Object_GetIndex(&play->objectCtx, v.objectId);
            // Revalida também no Draw: nenhum segmento de objeto descarregado pode ser usado.
            if (slot < 0 || slot != v.slot || !Object_IsLoaded(&play->objectCtx, slot) ||
                play->objectCtx.status[slot].segment != v.segment)
                pool.Remove(i);
        }
}
bool Prepare(PlayState* play) {
    // Padrão 0: muda o visual vanilla mesmo sem shaderpack (OOT-VANILLA-001, sem mods = upstream).
    if (!play || !CVarGetInteger("gDeveloperTools.PersistentDebris", 0)) {
        Clear();
        session = nullptr;
        return false;
    }
    if (session != play || scene != play->sceneNum || room != play->roomCtx.curRoom.num ||
        (hasTick && play->gameplayFrames < lastTick)) {
        Clear();
        session = play;
        scene = play->sceneNum;
        room = play->roomCtx.curRoom.num;
    }
    Cull(play);
    return true;
}
// Nomes OTR são comparados por conteúdo: os arrays dos headers são static em cada TU.
// Ponteiros de DL binária/segmentada não são tratados como string nem capturados.
bool Asset(void* dl, const char* name) {
    return ResourceMgr_OTRSigCheck(static_cast<char*>(dl)) && std::strcmp(static_cast<const char*>(dl), name) == 0;
}
enum class Material { None, Pot, Wood, Stone };
Material Identify(int object, void* dl) {
    if (!dl)
        return Material::None;
    // Lista positiva exata. gCuttableShrubStalkDL/gCuttableShrubTipDL nunca entram.
    if (object == OBJECT_GAMEPLAY_DANGEON_KEEP) {
        if (Asset(dl, dgPotFragmentDL))
            return Material::Pot;
        if (Asset(dl, dgSmallWoodenBoxFragmentDL))
            return Material::Wood;
    } else if (object == OBJECT_TSUBO && Asset(dl, dobject_tsubo_DL_001960))
        return Material::Pot;
    else if (object == OBJECT_BOMBIWA && Asset(dl, dobject_bombiwa_DL_0009E0))
        return Material::Stone;
    return Material::None;
}
bool Floor(void* ctx, Vec3 point, Ground& out) {
    auto* play = static_cast<PlayState*>(ctx);
    // Sonda curta acima do canto: corrige penetração sem escolher andares muito acima.
    Vec3f probe{ point.x, point.y + 12, point.z };
    CollisionPoly* poly = nullptr;
    s32 bgId = BGCHECK_SCENE;
    float height = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
    if (!poly || bgId != BGCHECK_SCENE || !std::isfinite(height) || height <= BGCHECK_Y_MIN ||
        SurfaceType_IsIgnoredByEntities(&play->colCtx, poly, bgId))
        return false;
    out.height = height;
    out.normal = Unit(
        { COLPOLY_GET_NORMAL(poly->normal.x), COLPOLY_GET_NORMAL(poly->normal.y), COLPOLY_GET_NORMAL(poly->normal.z) });
    return out.normal.y > .05f;
}
bool WallTest(void* ctx, Vec3 previous, Vec3 next, float radius, Wall& out) {
    auto* play = static_cast<PlayState*>(ctx);
    Vec3f prev = GameVector(previous), pos = GameVector(next), result = pos;
    CollisionPoly* poly = nullptr;
    s32 bgId = BGCHECK_SCENE;
    if (!BgCheck_EntitySphVsWall3(&play->colCtx, &result, &pos, &prev, radius, &poly, &bgId, nullptr, 0))
        return false;
    out.push = Vector(result) - next;
    out.normal = Unit(out.push);
    return Finite(out.push);
}
bool Water(void* ctx, Vec3 point, float& surface) {
    auto* play = static_cast<PlayState*>(ctx);
    WaterBox* box = nullptr;
    surface = point.y;
    return WaterBox_GetSurface1(play, &play->colCtx, point.x, point.z, &surface, &box) && std::isfinite(surface);
}
} // namespace

// Fora do namespace anônimo: a declaração local do OPEN_DISPS dentro dele sai com ligação C++ no MSVC.
static void DrawBody(PlayState* play, const Body& b, const Visual& v) {
    auto* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    // Endereço fixo do slot + epoch novo por spawn: não interpola entre cacos reciclados.
    FrameInterpolation_RecordOpenChild(&v, v.epoch);
    Matrix_Push();
    Matrix_Translate(b.position.x, b.position.y, b.position.z, MTXMODE_NEW);
    MtxF matrix;
    Matrix_Get(&matrix);
    const Vec3 x = b.orientation.Rotate({ 1, 0, 0 }), y = b.orientation.Rotate({ 0, 1, 0 }),
               z = b.orientation.Rotate({ 0, 0, 1 });
    matrix.xx = x.x;
    matrix.yx = x.y;
    matrix.zx = x.z;
    matrix.xy = y.x;
    matrix.yy = y.y;
    matrix.zy = y.z;
    matrix.xz = z.x;
    matrix.yz = z.y;
    matrix.zz = z.z;
    Matrix_Put(&matrix);
    Matrix_Scale(v.scale, v.scale, v.scale, MTXMODE_APPLY);
    // Mesmos setup, cor, DL e passe do EffectSsKakera_Draw; segmento original revalidado.
    static constexpr unsigned char colors[2][3] = { { 255, 255, 255 }, { 235, 170, 130 } };
    if (v.translucent) {
        gSPSegment(POLY_XLU_DISP++, 0x06, reinterpret_cast<uintptr_t>(v.segment));
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        Gfx_SetupDL_25Xlu(gfxCtx);
        if (v.color >= 0 && v.color < 2)
            gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, colors[v.color][0], colors[v.color][1], colors[v.color][2], 255);
        gSPDisplayList(POLY_XLU_DISP++, v.displayList);
    } else {
        gSPSegment(POLY_OPA_DISP++, 0x06, reinterpret_cast<uintptr_t>(v.segment));
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        Gfx_SetupDL_25Opa(gfxCtx);
        if (v.color >= 0 && v.color < 2)
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, colors[v.color][0], colors[v.color][1], colors[v.color][2], 255);
        gSPDisplayList(POLY_OPA_DISP++, v.displayList);
    }
    Matrix_Pop();
    FrameInterpolation_RecordCloseChild();
    CLOSE_DISPS(gfxCtx);
}
extern "C" bool PersistentDebris_TrySpawnShard(PlayState* play, const float* position, const float* velocity,
                                               int16_t flags, int16_t scale, int16_t colorIdx, int16_t objectId,
                                               void* displayList) {
    if (!ValidVector(position) || !ValidVector(velocity) || scale <= 0 || !Prepare(play) || Paused(play)) {
        if (Log())
            SPDLOG_INFO("PersistentDebris: recusado na entrada (objeto {} escala {})", objectId, scale);
        return false;
    }
    const Material material = Identify(objectId, displayList);
    if (material == Material::None) {
        if (Log()) {
            const bool otr = displayList && ResourceMgr_OTRSigCheck(static_cast<char*>(displayList));
            SPDLOG_INFO("PersistentDebris: recusado objeto {} dl {}", objectId,
                        otr ? static_cast<const char*>(displayList) : "(binária)");
        }
        return false;
    }
    // Sem DMA no SoH: o segmento pode ser nulo (a DL é recurso OTR) e o Kakera vanilla o usa assim mesmo.
    // Validade = objeto carregado no slot; o Cull revalida slot, carga e o mesmo ponteiro de segmento.
    const int slot = Object_GetIndex(&play->objectCtx, objectId);
    if (slot < 0 || !Object_IsLoaded(&play->objectCtx, slot)) {
        if (Log())
            SPDLOG_INFO("PersistentDebris: recusado objeto {} slot {} não carregado", objectId, slot);
        return false;
    }
    Spawn s;
    s.position = Vector(position);
    s.velocity = Vector(velocity) * 20;
    // Aproximações de caixa por material, dimensionadas na mesma escala visual do vanilla.
    float size = std::clamp(scale * (material == Material::Stone ? .17f : .055f), .35f, 7.0f);
    s.halfExtents = { size, size * (material == Material::Stone ? .8f : .3f), size * .8f };
    const float density = material == Material::Wood ? .45f : (material == Material::Stone ? 2.7f : 1.7f);
    s.mass = 8 * s.halfExtents.x * s.halfExtents.y * s.halfExtents.z * density * pool.config.waterDensity;
    s.restitution = material == Material::Stone ? .22f : .3f;
    s.friction = material == Material::Wood ? .7f : .6f;
    // Derivação determinística: não consome o RNG global e não altera drops/gameplay.
    const float seed = static_cast<float>((nextEpoch % 127) + 1);
    s.orientation = Quat::AxisAngle({ 1, .7f, .3f }, seed * .713f);
    const unsigned spin = (static_cast<unsigned>(flags) >> 5) & 3;
    s.angularVelocity = Vec3{ 1, .27f, .43f } * static_cast<float>(spin * 3);
    const auto id = pool.Insert(s);
    if (id == Pool::invalid) {
        if (Log())
            SPDLOG_INFO("PersistentDebris: recusado pelo pool (meia {:.2f} x {:.2f}, massa {:.5f})", s.halfExtents.x,
                        s.halfExtents.y, s.mass);
        return false;
    }
    if (nextEpoch == 0x7fffffff)
        nextEpoch = 0;
    visuals[id] = { static_cast<Gfx*>(displayList),
                    play->objectCtx.status[slot].segment,
                    objectId,
                    slot,
                    ++nextEpoch,
                    colorIdx,
                    scale / 256.0f,
                    (flags & 0x80) != 0 };
    if (Log())
        SPDLOG_INFO("PersistentDebris: caco {} material {} objeto {} pos ({:.1f}, {:.1f}, {:.1f}) vel ({:.1f}, {:.1f}, "
                    "{:.1f}) meia {:.2f} escala {:.3f}",
                    id, static_cast<int>(material), objectId, s.position.x, s.position.y, s.position.z, s.velocity.x,
                    s.velocity.y, s.velocity.z, s.halfExtents.x, scale / 256.0f);
    return true;
}
extern "C" void PersistentDebris_OnExplosion(PlayState* play, const float* position) {
    if (!ValidVector(position) || !Prepare(play) || Paused(play))
        return;
    if (!hasExplosionTick || explosionTick != play->gameplayFrames) {
        explosionTick = play->gameplayFrames;
        explosionCount = 0;
        hasExplosionTick = true;
    }
    const Vec3 p = Vector(position);
    for (unsigned i = 0; i < explosionCount; ++i)
        if (Length(explosions[i] - p) < 1)
            return;
    if (explosionCount < explosions.size())
        explosions[explosionCount++] = p;
    pool.Explosion(p);
}
extern "C" void PersistentDebris_Tick(PlayState* play) {
    if (!Prepare(play))
        return;
    if (hasTick && lastTick == play->gameplayFrames)
        return;
    lastTick = play->gameplayFrames;
    hasTick = true;
    Player* player = GET_PLAYER(play);
    if (Paused(play)) {
        hasPlayer = false;
        return;
    }
    if (player) {
        const Vec3 position = Vector(player->actor.world.pos);
        Vec3 velocity = hasPlayer ? (position - previousPlayer) * 20 : Vec3{};
        // Transições/teleporte não viram um chute de velocidade arbitrária.
        if (Length(velocity) < 2000)
            pool.PushSphere(position + Vec3{ 0, 3, 0 }, velocity, 10);
        previousPlayer = position;
        hasPlayer = true;
    } else
        hasPlayer = false;
    pool.Step({ play, Floor, WallTest, Water });
    if (Log() && lastTick % 20 == 0 && pool.Count() > 0) {
        for (std::size_t i = 0; i < Pool::capacity; ++i) {
            const auto& b = pool.Get(i);
            if (!b.active)
                continue;
            SPDLOG_INFO(
                "PersistentDebris: tick {} ativos {} corpo {} estado {} pos ({:.1f}, {:.1f}, {:.1f}) vel {:.1f}",
                lastTick, pool.Count(), i, static_cast<int>(b.state), b.position.x, b.position.y, b.position.z,
                Length(b.velocity));
            break;
        }
    }
}
extern "C" void PersistentDebris_Draw(PlayState* play) {
    if (!Prepare(play))
        return;
    std::size_t drawn = 0;
    for (std::size_t i = 0; i < Pool::capacity; ++i)
        if (pool.Get(i).active) {
            DrawBody(play, pool.Get(i), visuals[i]);
            ++drawn;
        }
    static unsigned loggedDraw = ~0u;
    if (Log() && drawn > 0 && lastTick % 20 == 0 && loggedDraw != lastTick) {
        loggedDraw = lastTick;
        SPDLOG_INFO("PersistentDebris: desenhados {} no tick {}", drawn, lastTick);
    }
}
extern "C" void PersistentDebris_Reset(void) {
    Clear();
    session = nullptr;
    scene = room = -1;
}
