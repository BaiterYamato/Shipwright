#include "DetachedLeaves.h"
#include "leaf_sim.h"
#include "../Weather/WindField.h"
#include "../WaterRipple/WaterRipple.h"
#include <libultraship/bridge/consolevariablebridge.h>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "objects/gameplay_keep/gameplay_keep.h"
}
#include "functions.h"
#include "soh/frame_interpolation.h"

namespace {
// Tick, eventos e construção das DLs ocorrem na thread de jogo; não há leitor externo.
leaves::Pool pool;
PlayState* session = nullptr;
int scene = -1;
int room = -1;
unsigned lastTick = 0;
bool initialized = false;
std::uint32_t randomState = 0x4c454146U;

bool Enabled() {
    // Padrão 0: muda o visual vanilla mesmo sem shaderpack (OOT-VANILLA-001, sem mods = upstream).
    return CVarGetInteger("gDeveloperTools.DetachedLeaves", 0) != 0;
}
bool Paused(const PlayState* play) {
    return play->pauseCtx.state != 0 || play->pauseCtx.debugState != 0;
}
bool Valid(const float* pos) {
    return pos && leaves::Finite({ pos[0], pos[1], pos[2] });
}
float Random() {
    // RNG visual privado: não altera drops, caules ou o RNG do gameplay.
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return static_cast<float>(randomState >> 8) / 16777216.0f;
}
bool Prepare(PlayState* play) {
    if (!play || !Enabled()) {
        DetachedLeaves_Reset();
        return false;
    }
    if (!initialized || session != play || scene != play->sceneNum || room != play->roomCtx.curRoom.num ||
        play->gameplayFrames < lastTick) {
        pool.Reset();
        session = play;
        scene = play->sceneNum;
        room = play->roomCtx.curRoom.num;
        lastTick = play->gameplayFrames;
        initialized = true;
        randomState = 0x4c454146U ^ static_cast<std::uint32_t>(scene);
        if (randomState == 0)
            randomState = 1;
    }
    return true;
}
leaves::Surface SampleWorld(void* context, leaves::Vec3 position, float ceiling) {
    auto* play = static_cast<PlayState*>(context);
    leaves::Surface surface;
    Vec3f probe{ position.x, ceiling, position.z };
    CollisionPoly* poly = nullptr;
    s32 bgId = BGCHECK_SCENE;
    const float y = BgCheck_EntityRaycastFloor5(play, &play->colCtx, &poly, &bgId, nullptr, &probe);
    // Sem retenção de CollisionPoly/ator. Assentamento persistente só em chão estático.
    if (poly && bgId == BGCHECK_SCENE && std::isfinite(y) && y > BGCHECK_Y_MIN &&
        !SurfaceType_IsIgnoredByEntities(&play->colCtx, poly, bgId) &&
        SurfaceType_GetFloorType(&play->colCtx, poly, bgId) != 6) {
        surface.ground = true;
        surface.groundY = y;
        surface.normal = { COLPOLY_GET_NORMAL(poly->normal.x), COLPOLY_GET_NORMAL(poly->normal.y),
                           COLPOLY_GET_NORMAL(poly->normal.z) };
    }
    WaterBox* water = nullptr;
    float waterY = position.y;
    if (WaterBox_GetSurface1(play, &play->colCtx, position.x, position.z, &waterY, &water)) {
        surface.water = true;
        surface.waterY = waterY;
    }
    return surface;
}
void Emit(const float* pos, leaves::Kind kind, float scale, float yaw, bool bush) {
    leaves::Spawn spawn;
    spawn.position = { pos[0] + (Random() - 0.5f) * 14, pos[1] + (bush ? 10 + Random() * 8 : 0),
                       pos[2] + (Random() - 0.5f) * 14 };
    spawn.velocity = { (Random() - 0.5f) * 65, bush ? 30 + Random() * 40 : -10, (Random() - 0.5f) * 65 };
    spawn.yaw = yaw;
    spawn.phase = Random() * 2 * leaves::Pi;
    spawn.spin = (Random() - 0.5f) * 1.8f;
    spawn.scale = scale;
    spawn.kind = kind;
    pool.Insert(spawn);
}
} // namespace

extern "C" void DetachedLeaves_Reset(void) {
    pool.Reset();
    session = nullptr;
    scene = room = -1;
    initialized = false;
    lastTick = 0;
}
extern "C" int DetachedLeaves_OnBushCut(PlayState* play, const float* pos, float scale) {
    if (!Valid(pos) || !std::isfinite(scale) || scale <= 0 || !Prepare(play) || Paused(play))
        return 0;
    const float size = std::clamp(scale / 0.4f, 0.3f, 1.5f);
    // Doze folhas substituem as quatro pontas; caules continuam no chamador.
    for (int i = 0; i < 12; ++i) {
        // Faixa inferior das pontas vanilla (sFragmentScales/256 = 0,15..0,42): folha solta, menor que a ponta.
        const float leafScale = (0.14f + Random() * 0.10f) * size;
        const float yaw = Random() * 2 * leaves::Pi;
        Emit(pos, leaves::Kind::Bush, leafScale, yaw, true);
    }
    return 1;
}
extern "C" int DetachedLeaves_OnTreeLeaf(PlayState* play, const float* pos, float yaw, int yellow) {
    if (!Valid(pos) || !std::isfinite(yaw) || !Prepare(play) || Paused(play))
        return 0;
    Emit(pos, yellow ? leaves::Kind::Yellow : leaves::Kind::Green, 0.12f, yaw, false);
    return 1;
}
extern "C" void DetachedLeaves_Tick(PlayState* play) {
    if (!Prepare(play))
        return;
    // Idempotente por gameplayFrames; garante vento atual mesmo neste recorte de z_play.
    WindField_Tick(play);
    const unsigned tick = play->gameplayFrames;
    if (tick == lastTick)
        return;
    lastTick = tick;
    if (Paused(play))
        return;
    leaves::Winds winds{};
    for (std::size_t i = 0; i < leaves::Capacity; ++i) {
        const auto& leaf = pool.Items()[i];
        if (leaf.state == leaves::State::Free)
            continue;
        const float position[3]{ leaf.position.x, leaf.position.y, leaf.position.z };
        float velocity[3]{};
        WindField_Sample(position, velocity);
        winds[i] = { velocity[0], velocity[1], velocity[2] };
    }
    const auto events = pool.Step({ play, SampleWorld }, winds);
    for (std::size_t i = 0; i < events.count; ++i) {
        const auto& point = events.water[i].position;
        const float position[3]{ point.x, point.y, point.z };
        WaterRipple_OnRipple(play, position, 20, 80);
    }
}
extern "C" void DetachedLeaves_Draw(PlayState* play) {
    if (!play || !Enabled() || session != play || scene != play->sceneNum || room != play->roomCtx.curRoom.num ||
        pool.Count() == 0)
        return;
    const s32 keep = Object_GetIndex(&play->objectCtx, OBJECT_GAMEPLAY_KEEP);
    if (keep < 0 || !Object_IsLoaded(&play->objectCtx, keep))
        return;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    Matrix_Push();
    Gfx_SetupDL_25Opa(gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x06, reinterpret_cast<uintptr_t>(play->objectCtx.status[keep].segment));
    for (const auto& leaf : pool.Items()) {
        if (leaf.state == leaves::State::Free || leaf.size <= 0)
            continue;
        FrameInterpolation_RecordOpenChild(&leaf, static_cast<int>(leaf.generation));
        Matrix_Translate(leaf.position.x, leaf.position.y, leaf.position.z, MTXMODE_NEW);
        // Leva o eixo Y da placa à normal do solo antes do yaw e do pêndulo local.
        const auto n = leaves::Unit(leaf.normal);
        const float axisLength = std::hypot(n.z, n.x);
        if (axisLength > 0.0001f) {
            Vec3f axis{ n.z / axisLength, 0, -n.x / axisLength };
            Matrix_RotateAxis(std::acos(std::clamp(n.y, -1.0f, 1.0f)), &axis, MTXMODE_APPLY);
        }
        Matrix_RotateY(leaf.yaw, MTXMODE_APPLY);
        Matrix_RotateX(leaf.tilt, MTXMODE_APPLY);
        const float scale = leaf.scale * leaf.size;
        // A ponta do gameplay_keep é uma lâmina em pé (eixo +Y); deitá-la por rotação mantém forma e normais.
        // Achatar Y com escala não uniforme deixava as normais na horizontal e a folha cinza sob o sol.
        // Não depende de OBJECT_WOOD02 vivo.
        Matrix_RotateX(-0.5f * leaves::Pi, MTXMODE_APPLY);
        Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
        const bool yellow = leaf.kind == leaves::Kind::Yellow;
        const bool bush = leaf.kind == leaves::Kind::Bush;
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, bush ? 255 : (yellow ? 180 : 50), bush ? 255 : (yellow ? 155 : 170),
                        bush ? 255 : (yellow ? 0 : 70), 255);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gCuttableShrubTipDL);
        FrameInterpolation_RecordCloseChild();
    }
    Matrix_Pop();
    CLOSE_DISPS(gfxCtx);
}
