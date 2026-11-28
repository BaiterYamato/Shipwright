// Liga linkspan.oot.camera/render (OotNativeView.cpp) ao jogo: subcâmera da PlayState, pilha de
// matrizes, display lists por caminho e o hook oot.player.limb_draw.
#include "OotNativeView.h"

#include <fast/Fast3dWindow.h>
#include <fast/fog_air_profile.h>
#include <fast/fog_detail.h>
#include <fast/interpreter.h>
#include <ship/Context.h>
#include <fast/toon_shading.h>
#include <ship/config/ConsoleVariable.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>

#include "OotNativeHooks.h"
#include "oot_hooks.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/frame_interpolation.h"
#include "soh/OotFogBridge.h"
#include "soh/cvar_prefixes.h"

#include "z64.h"
#include "macros.h"

extern "C" {
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
// OTRGlobals.h só a declara para C.
float OTRGetAspectRatio(void);
}

namespace {

constexpr size_t kOtrPrefixLength = 7; // "__OTR__"

ShipNativeStatus CameraCreate(int16_t* camera, void** play) {
    PlayState* state = gPlayState;
    if (!state || !GET_PLAYER(state) || state->activeCamera != CAM_ID_MAIN || !state->cameraPtrs[CAM_ID_MAIN]) {
        return SHIP_NATIVE_LIMIT;
    }
    const s16 sub = Play_CreateSubCamera(state);
    if (sub == SUBCAM_NONE) {
        return SHIP_NATIVE_LIMIT;
    }
    Camera* main = Play_GetCamera(state, CAM_ID_MAIN);
    Vec3f at = main->at;
    Vec3f eye = main->eye;
    const f32 fov = main->fov;
    Play_ChangeCameraStatus(state, CAM_ID_MAIN, CAM_STAT_WAIT);
    Play_ChangeCameraStatus(state, sub, CAM_STAT_ACTIVE);
    Play_CameraSetAtEye(state, sub, &at, &eye);
    Play_CameraSetFov(state, sub, fov);
    *camera = sub;
    *play = state;
    return SHIP_NATIVE_OK;
}

bool CameraValid(int16_t camera, void* play) {
    const PlayState* state = gPlayState;
    return state && state == play && camera > CAM_ID_MAIN && camera < NUM_CAMS && state->cameraPtrs[camera] &&
           state->activeCamera == camera;
}

// Na mesma cena: remove a subcâmera; só devolve a principal se a nossa ainda era a ativa.
void CameraDestroy(int16_t camera, void* play) {
    PlayState* state = gPlayState;
    if (!state || state != play || camera <= CAM_ID_MAIN || camera >= NUM_CAMS || !state->cameraPtrs[camera]) {
        return;
    }
    const bool wasActive = state->activeCamera == camera;
    Play_ClearCamera(state, camera);
    if (wasActive && state->cameraPtrs[CAM_ID_MAIN]) {
        Play_ChangeCameraStatus(state, CAM_ID_MAIN, CAM_STAT_ACTIVE);
    }
}

void CameraApply(int16_t camera, void* play, const ShipOotCameraViewV2& view, bool roll) {
    auto* state = static_cast<PlayState*>(play);
    Vec3f eye{ view.eye[0], view.eye[1], view.eye[2] };
    Vec3f at{ view.at[0], view.at[1], view.at[2] };
    Play_CameraSetAtEye(state, camera, &at, &eye);
    Play_CameraSetFov(state, camera, view.fov);
    if (roll) {
        Play_GetCamera(state, camera)->roll = view.roll;
    }
}

ShipNativeStatus CameraRead(ShipOotCameraViewV2* view) {
    PlayState* state = gPlayState;
    if (!state) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    Camera* camera = Play_GetCamera(state, state->activeCamera);
    if (!camera) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    view->size = sizeof(ShipOotCameraViewV2);
    view->eye[0] = camera->eye.x;
    view->eye[1] = camera->eye.y;
    view->eye[2] = camera->eye.z;
    view->at[0] = camera->at.x;
    view->at[1] = camera->at.y;
    view->at[2] = camera->at.z;
    view->fov = camera->fov;
    view->roll = camera->roll;
    view->reserved = 0;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus DrawDisplayList(void* play, const char* path, uint8_t layer) {
    if (!ResourceMgr_FileExists(path + kOtrPrefixLength)) {
        return SHIP_NATIVE_FAILURE;
    }
    auto* state = static_cast<PlayState*>(play);
    auto* dlist = reinterpret_cast<Gfx*>(const_cast<char*>(path));
    if (layer == LINKSPAN_OOT_RENDER_TRANSLUCENT) {
        Gfx_DrawDListXlu(state, dlist);
    } else {
        Gfx_DrawDListOpa(state, dlist);
    }
    return SHIP_NATIVE_OK;
}

void MatrixTranslateApply(float x, float y, float z) {
    Matrix_Translate(x, y, z, MTXMODE_APPLY);
}

void MatrixScaleApply(float x, float y, float z) {
    Matrix_Scale(x, y, z, MTXMODE_APPLY);
}

void MatrixRotateApply(int16_t x, int16_t y, int16_t z) {
    Matrix_RotateZYX(x, y, z, MTXMODE_APPLY);
}

// Cabeça da display list da camada. Sem OPEN_DISPS: a macro declara FrameInterpolation_* no escopo do bloco, o que
// dentro deste namespace anônimo vira outro símbolo, e estes opcodes não levam matriz para interpolar.
Gfx** RenderLayerHead(uint8_t layer) {
    GraphicsContext* gfxCtx = gPlayState->state.gfxCtx;
    return layer == LINKSPAN_OOT_RENDER_TRANSLUCENT ? &gfxCtx->polyXlu.p : &gfxCtx->polyOpa.p;
}

ShipNativeStatus EmitToonKey(uint8_t layer, int8_t dx, int8_t dy, int8_t dz, uint8_t r, uint8_t g, uint8_t b) {
    if (!gPlayState)
        return SHIP_NATIVE_UNSUPPORTED;
    Gfx** head = RenderLayerHead(layer);
    gSPToonKey((*head)++, dx, dy, dz, r, g, b);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus EmitStencil(uint8_t layer, uint8_t mode) {
    if (!gPlayState)
        return SHIP_NATIVE_UNSUPPORTED;
    Gfx** head = RenderLayerHead(layer);
    gSPStencil((*head)++, mode);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus EmitToonShadow(uint8_t layer, int16_t feetClampY, float size) {
    if (!gPlayState)
        return SHIP_NATIVE_UNSUPPORTED;
    Gfx** head = RenderLayerHead(layer);
    // Tamanho <= 0 desarma a captura (normal nula), como o gSPToonShadow(0, 0, 0, 0) do fork: marca a fronteira
    // do objeto sem deixar o ator anterior acumular esta geometria. O braço (nz = 0x7F) armaria com tamanho 0.
    if (!(size > 0.0f)) {
        gSPToonShadow((*head)++, 0, 0, 0, 0.0f);
    } else {
        gSPToonShadowArm((*head)++, feetClampY, size);
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus FlushToonShadows(uint8_t layer) {
    if (!gPlayState)
        return SHIP_NATIVE_UNSUPPORTED;
    Gfx** head = RenderLayerHead(layer);
    gSPToonShadowFlush((*head)++);
    return SHIP_NATIVE_OK;
}

std::shared_ptr<Fast::Interpreter> CurrentInterpreter() {
    auto wnd = Ship::Context::GetRawInstance()
                   ? std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow())
                   : nullptr;
    return wnd ? wnd->GetInterpreterWeak().lock() : nullptr;
}

struct FogFrame {
    void* play = nullptr;
    bool submitted = false;
    bool hasFlow = false;
    void* sourcePlay = nullptr;
    uint32_t generation = 0;
    uint64_t stream = 0;
    bool airProfile = false;
    FastFogVolume volume{};
    FastFogFlow flow{};
};

thread_local FogFrame gFogFrame;
static_assert(LINKSPAN_OOT_FOG_MAX_SIDE == FAST_FOG_GRID_SIDE);
static_assert(LINKSPAN_OOT_FOG_FLOW_PARTICLES == FAST_FOG_FLOW_PARTICLES);
static_assert(LINKSPAN_OOT_FOG_FLOW_VORTICES == FAST_FOG_FLOW_VORTICES);
static_assert(LINKSPAN_OOT_FOG_FLOW_LIFT == FAST_FOG_FLOW_LIFT);
static_assert(LINKSPAN_OOT_FOG_FLOW_VERTICAL_COMPUTE == FAST_FOG_FLOW_VERTICAL_COMPUTE);
static_assert(LINKSPAN_OOT_FOG_FLOW_CPU_FIELD == FAST_FOG_FLOW_CPU_FIELD);
static_assert(LINKSPAN_OOT_FOG_FLOW_DETAIL_GRID == FAST_FOG_FLOW_DETAIL_GRID);
static_assert(LINKSPAN_OOT_FOG_DETAIL_MAX_SIDE == FAST_FOG_DETAIL_SIDE);
static_assert(LINKSPAN_OOT_FOG_VORTEX_REST_DENSITY == FAST_FOG_VORTEX_REST_DENSITY);

bool ValidFogGridShape(const ShipOotFogGridV1* grid) {
    return grid && grid->size >= sizeof(*grid) && grid->cells && grid->width >= 2 && grid->depth >= 2 &&
           grid->width <= LINKSPAN_OOT_FOG_MAX_SIDE && grid->depth <= LINKSPAN_OOT_FOG_MAX_SIDE;
}

bool FogValueInRange(float value, float minimum, float maximum) {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

// RFC 0034 (draft): densities up to 1.5 (the shader's own clamp) are accepted only on a flow that carries the
// nested detail grid, so the slash rim inside a full bank keeps headroom. Every other caller keeps [0, 1].
constexpr float kFogDensityMax = 1.0f;
constexpr float kFogDetailDensityMax = 1.5f;

ShipNativeStatus SubmitFogGridLimited(void* play, const ShipOotFogGridV1* grid, float densityMax);

ShipNativeStatus SHIP_NATIVE_CALL SubmitFogGrid(void* play, const ShipOotFogGridV1* grid) {
    return SubmitFogGridLimited(play, grid, kFogDensityMax);
}

ShipNativeStatus SubmitFogGridLimited(void* play, const ShipOotFogGridV1* grid, float densityMax) {
    if (!play || play != gFogFrame.play) return SHIP_NATIVE_UNSUPPORTED;
    if (gFogFrame.submitted) return SHIP_NATIVE_LIMIT;
    if (!ValidFogGridShape(grid)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto interpreter = CurrentInterpreter();
    auto* renderer = interpreter ? interpreter->GetCurrentRenderingAPI() : nullptr;
    if (!renderer || !renderer->SupportsFogVolume() ||
        Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 0) != 0) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    const auto bounded = FogValueInRange;
    if (!bounded(grid->x_min, -1000000.0f, 1000000.0f) || !bounded(grid->x_max, -1000000.0f, 1000000.0f) ||
        !bounded(grid->z_min, -1000000.0f, 1000000.0f) || !bounded(grid->z_max, -1000000.0f, 1000000.0f) ||
        grid->x_max <= grid->x_min || grid->z_max <= grid->z_min ||
        !bounded(grid->layer_height, 1.0f, 512.0f) || !bounded(grid->falloff, 1.0f, 512.0f) ||
        !bounded(grid->extinction, 0.0f, 1.0f) || !bounded(grid->edge_feather, 1.0f, 512.0f)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    for (float channel : grid->color) {
        if (!bounded(channel, 0.0f, 1.0f)) return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    float bottom = 1000000.0f;
    float top = -1000000.0f;
    auto& volume = gFogFrame.volume;
    volume.lightCount = 0;
    volume.upperCount = 0;
    volume.atmosphere[3] = 0.0f;
    volume.detailGrid[0] = 0.0f;
    for (uint32_t i = 0; i < grid->width * grid->depth; ++i) {
        const auto& cell = grid->cells[i];
        if (!bounded(cell.density, 0.0f, densityMax) || !bounded(cell.floor_y, -1000000.0f, 1000000.0f)) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        volume.cells[i][0] = cell.density;
        volume.cells[i][1] = cell.floor_y * cell.density;
        if (cell.density > 0.0f) {
            bottom = std::min(bottom, cell.floor_y);
            top = std::max(top, cell.floor_y);
        }
    }
    gFogFrame.submitted = true;
    volume.grid[0] = 0.0f;
    if (top < bottom) return SHIP_NATIVE_OK;
    volume.minimum[0] = grid->x_min;
    volume.minimum[1] = bottom;
    volume.minimum[2] = grid->z_min;
    volume.minimum[3] = grid->falloff;
    volume.maximum[0] = grid->x_max;
    volume.maximum[1] = top + grid->layer_height;
    volume.maximum[2] = grid->z_max;
    volume.maximum[3] = grid->edge_feather;
    volume.forward[3] = grid->extinction;
    std::copy_n(grid->color, 3, volume.color);
    volume.grid[0] = static_cast<float>(grid->width);
    volume.grid[1] = static_cast<float>(grid->depth);
    volume.grid[2] = grid->layer_height;
    return SHIP_NATIVE_OK;
}

bool CopyFogImpulse(const ShipOotFogImpulseV2& input, FastFogImpulse& output) {
    constexpr float limit = 180.0f;
    const auto bounded = FogValueInRange;
    if (!bounded(input.density_scale, 0.0f, 1.0f) ||
        !bounded(input.velocity_x, -1000000.0f, 1000000.0f) ||
        !bounded(input.velocity_z, -1000000.0f, 1000000.0f) ||
        !bounded(input.min_x, -limit, input.max_x) || !bounded(input.max_x, input.min_x, limit) ||
        !bounded(input.min_z, -limit, input.max_z) || !bounded(input.max_z, input.min_z, limit)) {
        return false;
    }
    output = {input.density_scale, input.velocity_x, input.velocity_z, input.min_x, input.max_x,
              input.min_z, input.max_z};
    return true;
}

void CopyFogLights(const PlayState& play, FastFogVolume& volume) {
    std::array<float, FAST_FOG_LIGHT_COUNT> distance{};
    const auto* node = play.lightCtx.listHead;
    for (int visited = 0; node && visited < 64; ++visited, node = node->next) {
        if (!node->info || node->info->type != LIGHT_POINT_GLOW) continue;
        const auto& light = node->info->params.point;
        if (light.radius <= 0 || !(light.color[0] || light.color[1] || light.color[2]) ||
            !std::isfinite(light.x) || !std::isfinite(light.y) || !std::isfinite(light.z)) {
            continue;
        }
        const float position[]{light.x, light.y, light.z};
        const float radius = std::min<float>(light.radius, 512.0f);
        float distanceToBox = 0.0f, distanceToCenter = 0.0f;
        for (int axis = 0; axis < 3; ++axis) {
            const float min = volume.minimum[axis], max = volume.maximum[axis];
            const float outside = position[axis] - std::clamp(position[axis], min, max);
            const float center = position[axis] - 0.5f * (min + max);
            distanceToBox += outside * outside;
            distanceToCenter += center * center;
        }
        if (distanceToBox >= radius * radius) continue;
        const auto last = distance.begin() + volume.lightCount;
        const auto next = std::upper_bound(distance.begin(), last, distanceToCenter);
        const auto index = static_cast<uint32_t>(next - distance.begin());
        if (index >= FAST_FOG_LIGHT_COUNT) continue;
        volume.lightCount = std::min<uint32_t>(volume.lightCount + 1, FAST_FOG_LIGHT_COUNT);
        for (auto i = volume.lightCount - 1; i > index; --i) {
            volume.lights[i] = volume.lights[i - 1];
            distance[i] = distance[i - 1];
        }
        distance[index] = distanceToCenter;
        volume.lights[index] = {{light.x, light.y, light.z, radius},
                               {light.color[0] / 255.0f, light.color[1] / 255.0f, light.color[2] / 255.0f, 0.0f}};
    }
}

ShipNativeStatus SHIP_NATIVE_CALL SubmitFogFlow(void* play, const ShipOotFogGridV1* grid, const ShipOotFogFlowV2* flow) {
    if (!play || play != gFogFrame.play) return SHIP_NATIVE_UNSUPPORTED;
    if (gFogFrame.submitted) return SHIP_NATIVE_LIMIT;
    constexpr auto flagsOffset = offsetof(ShipOotFogFlowV2, flags);
    constexpr auto edgesOffset = offsetof(ShipOotFogFlowV2, blocked_edges);
    constexpr auto raisedOffset = offsetof(ShipOotFogFlowV2, raised_fraction);
    constexpr auto upperOffset = offsetof(ShipOotFogFlowV2, upper_cells);
    constexpr auto upperRaisedOffset = offsetof(ShipOotFogFlowV2, upper_raised_fraction);
    constexpr auto verticalOffset = offsetof(ShipOotFogFlowV2, vertical_cells);
    constexpr auto detailOffset = offsetof(ShipOotFogFlowV2, detail);
    if (!ValidFogGridShape(grid) || !flow || flow->size < flagsOffset || !flow->cells ||
        !FogValueInRange(flow->step_seconds, 0.001f, 0.1f)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto interpreter = CurrentInterpreter();
    auto* renderer = interpreter ? interpreter->GetCurrentRenderingAPI() : nullptr;
    if (!renderer || !renderer->SupportsFogFlow()) return SHIP_NATIVE_UNSUPPORTED;
    const auto* edges = flow->size >= edgesOffset + sizeof(flow->blocked_edges) ? flow->blocked_edges : nullptr;
    const auto flags = flow->size >= flagsOffset + sizeof(flow->flags) ? flow->flags : 0u;
    const float* raised = nullptr;
    if (flags & LINKSPAN_OOT_FOG_FLOW_LIFT) {
        if (flow->size < raisedOffset + sizeof(flow->raised_fraction) || !flow->raised_fraction) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        raised = flow->raised_fraction;
    }
    const ShipOotFogCellV1* upper = nullptr;
    if (flags & LINKSPAN_OOT_FOG_FLOW_STACKED_SUPPORTS) {
        if (flow->size < upperOffset + sizeof(flow->upper_cells) || !flow->upper_cells) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        upper = flow->upper_cells;
    }
    const auto* upperRaised = upper && raised && flow->size >= upperRaisedOffset + sizeof(flow->upper_raised_fraction)
                                  ? flow->upper_raised_fraction : nullptr;
    const ShipOotFogVerticalCell* vertical = nullptr;
    if (flags & LINKSPAN_OOT_FOG_FLOW_VERTICAL_COMPUTE) {
        if (!raised || !(flags & LINKSPAN_OOT_FOG_FLOW_VORTICES) || (flags & LINKSPAN_OOT_FOG_FLOW_CPU_FIELD) ||
            flow->size < verticalOffset + sizeof(flow->vertical_cells) || !flow->vertical_cells) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        vertical = flow->vertical_cells;
    }
    // RFC 0034 (draft): validate the nested grid before anything is copied.
    const ShipOotFogDetailV1* detail = nullptr;
    if (flags & LINKSPAN_OOT_FOG_FLOW_DETAIL_GRID) {
        if (!(flags & LINKSPAN_OOT_FOG_FLOW_CPU_FIELD) || flow->size < detailOffset + sizeof(flow->detail) ||
            !flow->detail) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        detail = flow->detail;
        if (detail->size < sizeof(*detail) || !detail->cells || detail->width < 2 || detail->depth < 2 ||
            detail->width > LINKSPAN_OOT_FOG_DETAIL_MAX_SIDE || detail->depth > LINKSPAN_OOT_FOG_DETAIL_MAX_SIDE ||
            !FogValueInRange(detail->x_min, grid->x_min, grid->x_max) ||
            !FogValueInRange(detail->x_max, grid->x_min, grid->x_max) ||
            !FogValueInRange(detail->z_min, grid->z_min, grid->z_max) ||
            !FogValueInRange(detail->z_max, grid->z_min, grid->z_max) || detail->x_max <= detail->x_min ||
            detail->z_max <= detail->z_min ||
            !FogValueInRange(detail->blend, 0.001f,
                             0.5f * std::min(detail->x_max - detail->x_min, detail->z_max - detail->z_min))) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        // O piso e a espessura vêm da grade principal: sem nenhuma célula principal com densidade, o volume não
        // é desenhado, então um detalhe nesse caso é recusado em vez de aceito e omitido.
        bool mainHasDensity = false;
        for (uint32_t i = 0; i < grid->width * grid->depth && !mainHasDensity; ++i) {
            mainHasDensity = grid->cells[i].density > 0.0f;
        }
        if (!mainHasDensity) return SHIP_NATIVE_INVALID_ARGUMENT;
        for (uint32_t i = 0; i < detail->width * detail->depth; ++i) {
            const auto& cell = detail->cells[i];
            if (!FogValueInRange(cell.density, 0.0f, kFogDetailDensityMax) ||
                !FogValueInRange(cell.velocity_x, -180.0f, 180.0f) ||
                !FogValueInRange(cell.velocity_z, -180.0f, 180.0f)) {
                return SHIP_NATIVE_INVALID_ARGUMENT;
            }
        }
    }
    auto& snapshot = gFogFrame.flow;
    snapshot.maxRaisedFraction = 0.0f;
    for (uint32_t i = 0; i < grid->width * grid->depth; ++i) {
        const auto& cell = flow->cells[i];
        const uint32_t blocked = edges ? edges[i] : 0u;
        auto& dest = snapshot.cells[i];
        if (blocked > 15u || cell.open > 1 || (!cell.open && grid->cells[i].density != 0.0f) ||
            !FogValueInRange(cell.velocity_x, -180.0f, 180.0f) ||
            !FogValueInRange(cell.velocity_z, -180.0f, 180.0f) ||
            !CopyFogImpulse(cell.before_step, dest.before) || !CopyFogImpulse(cell.after_step, dest.after)) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        dest.initial[0] = grid->cells[i].density;
        dest.initial[1] = grid->cells[i].floor_y;
        dest.initial[2] = cell.velocity_x;
        dest.initial[3] = cell.velocity_z;
        dest.open = cell.open | (blocked << 1u);
        const auto motion = vertical ? vertical[i] : ShipOotFogVerticalCell{};
        if (vertical && (!FogValueInRange(motion.raised_density, 0.0f,
                                          grid->cells[i].density * LINKSPAN_OOT_FOG_MAX_RAISED_FRACTION + 0.00001f) ||
                         !FogValueInRange(motion.vertical_speed, -84.0f, 84.0f) ||
                         !FogValueInRange(motion.before_step_lift, -32.0f, 32.0f) ||
                         (!cell.open && (motion.raised_density != 0.0f || motion.vertical_speed != 0.0f ||
                                         motion.before_step_lift != 0.0f)))) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        dest.vertical[0] = motion.raised_density;
        dest.vertical[1] = motion.vertical_speed;
        dest.vertical[2] = motion.before_step_lift;
        if (raised) {
            const float fraction = raised[i];
            if (!FogValueInRange(fraction, 0.0f, LINKSPAN_OOT_FOG_MAX_RAISED_FRACTION) ||
                (!cell.open && fraction != 0.0f)) {
                return SHIP_NATIVE_INVALID_ARGUMENT;
            }
            snapshot.raisedFraction[i] = fraction;
            snapshot.maxRaisedFraction = std::max(snapshot.maxRaisedFraction, fraction);
        }
        if (upper) {
            const auto& deck = upper[i];
            const float minDeck = grid->cells[i].floor_y + 0.5f * grid->layer_height;
            if (!FogValueInRange(deck.density, 0.0f, 1.0f) ||
                !FogValueInRange(deck.floor_y, -1000000.0f, 1000000.0f) ||
                (deck.density > 0.0f && (!cell.open || deck.floor_y < minDeck))) {
                return SHIP_NATIVE_INVALID_ARGUMENT;
            }
        }
        const float fraction = upperRaised ? upperRaised[i] : 0.0f;
        if (upperRaised && (!FogValueInRange(fraction, 0.0f, LINKSPAN_OOT_FOG_MAX_RAISED_FRACTION) ||
                            (upper[i].density <= 0.0f && fraction != 0.0f))) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        snapshot.upperRaisedFraction[i] = fraction;
        snapshot.maxRaisedFraction = std::max(snapshot.maxRaisedFraction, fraction);
    }
    const auto result = SubmitFogGridLimited(play, grid, detail ? kFogDetailDensityMax : kFogDensityMax);
    if (result != SHIP_NATIVE_OK || gFogFrame.volume.grid[0] == 0.0f) return result;
    const auto* state = static_cast<PlayState*>(play);
    const auto cvars = Ship::Context::GetRawInstance()->GetConsoleVariables();
    const bool canopy = upper && state->sceneNum == SCENE_KOKIRI_FOREST && state->roomCtx.curRoom.num == 0 &&
                        cvars->GetInteger(CVAR_DEVELOPER_TOOLS("FogreKokiriCanopy"), 0) > 0 &&
                        cvars->GetInteger(CVAR_DEVELOPER_TOOLS("FogreKokiriAirProfile"), 1) != 0;
    if (upper) {
        auto& volume = gFogFrame.volume;
        for (uint32_t i = 0; i < grid->width * grid->depth; ++i) {
            volume.upperCells[i][0] = upper[i].density;
            volume.upperCells[i][1] = upper[i].floor_y * upper[i].density;
            if (upper[i].density <= 0.0f) continue;
            ++volume.upperCount;
            if (canopy) {
                const float center = upper[i].floor_y + 0.5f * grid->layer_height;
                volume.minimum[1] = std::min(volume.minimum[1], center - Fast::KokiriAirHalfHeight);
                volume.maximum[1] = std::max(volume.maximum[1], center + Fast::KokiriAirHalfHeight);
            } else {
                volume.maximum[1] = std::max(volume.maximum[1], upper[i].floor_y + grid->layer_height);
            }
        }
    }
    if (detail) {
        auto& volume = gFogFrame.volume;
        volume.detailBounds[0] = detail->x_min;
        volume.detailBounds[1] = detail->z_min;
        volume.detailBounds[2] = detail->x_max;
        volume.detailBounds[3] = detail->z_max;
        volume.detailGrid[0] = static_cast<float>(detail->width);
        volume.detailGrid[1] = static_cast<float>(detail->depth);
        volume.detailGrid[2] = detail->blend;
        volume.detailGrid[3] = 0.0f;
        // As células vão pelo canal de fog_detail.h (até 128 por lado); volume.detailCells (32x32) fica sem uso.
        auto& fine = gFastFogDetailCells;
        fine.width = detail->width;
        fine.height = detail->depth;
        for (uint32_t i = 0; i < detail->width * detail->depth; ++i) {
            const auto& cell = detail->cells[i];
            fine.cells[i][0] = cell.density;
            fine.cells[i][1] = cell.velocity_x;
            fine.cells[i][2] = cell.velocity_z;
            fine.cells[i][3] = 0.0f;
        }
    }
    if (flags & LINKSPAN_OOT_FOG_FLOW_LOCAL_LIGHTS) CopyFogLights(*static_cast<PlayState*>(play), gFogFrame.volume);
    if (flags & LINKSPAN_OOT_FOG_FLOW_ATMOSPHERE) {
        const auto& lights = static_cast<PlayState*>(play)->lightCtx;
        for (int channel = 0; channel < 3; ++channel) {
            gFogFrame.volume.atmosphere[channel] = lights.fogColor[channel] / 255.0f;
        }
        gFogFrame.volume.atmosphere[3] = 0.70f;
    }
    const bool airProfile = canopy && gFogFrame.volume.upperCount != 0;
    if (gFogFrame.sourcePlay != play || gFogFrame.generation != flow->generation || gFogFrame.airProfile != airProfile) {
        ++gFogFrame.stream;
        gFogFrame.sourcePlay = play;
        gFogFrame.generation = flow->generation;
        gFogFrame.airProfile = airProfile;
    }
    snapshot.stream = gFogFrame.stream;
    snapshot.tick = flow->tick;
    snapshot.dt = flow->step_seconds;
    snapshot.flags = flags & (FAST_FOG_FLOW_PARTICLES | FAST_FOG_FLOW_VORTICES | FAST_FOG_FLOW_LIFT |
                              FAST_FOG_FLOW_VERTICAL_COMPUTE | FAST_FOG_FLOW_CPU_FIELD);
    if (airProfile)
        snapshot.flags |= Fast::KokiriAirFlowFlag;
    gFogFrame.hasFlow = true;
    return SHIP_NATIVE_OK;
}

void SetToonRamp(float center, float softness, float highlight, float shadow, bool debugBands) {
    if (auto interpreter = CurrentInterpreter()) {
        if (auto* rapi = interpreter->GetCurrentRenderingAPI()) {
            rapi->SetToonRamp(center, softness, highlight, shadow, debugBands ? 1.0f : 0.0f);
        }
    }
}

void SetToonShadowParams(float opacity, float minElevation, float slabDepth, float slabRise, int32_t edgeSoftness,
                         bool showVolume) {
    if (auto interpreter = CurrentInterpreter()) {
        interpreter->SetToonShadowParams(opacity, minElevation, slabDepth, slabRise, edgeSoftness, showVolume);
    }
}

// Valores iniciais do renderer (toon_shading.h e os membros de Fast::Interpreter).
void ResetToonLook() {
    SetToonRamp(TOON_SHADING_DEFAULT_RAMP_CENTER, TOON_SHADING_DEFAULT_RAMP_SOFTNESS, TOON_SHADING_DEFAULT_HIGHLIGHT,
                TOON_SHADING_DEFAULT_SHADOW, false);
    SetToonShadowParams(0.5f, 0.6f, 40.0f, 10.0f, 1, false);
}

void MatrixTranslateNew(float x, float y, float z) {
    Matrix_Translate(x, y, z, MTXMODE_NEW);
}

void MatrixRotateAxisApply(float radians, float x, float y, float z) {
    Vec3f axis{ x, y, z };
    Matrix_RotateAxis(radians, &axis, MTXMODE_APPLY);
}

const void* ExportMatrix(void* gfx) {
    static char kFile[] = "linkspan.oot.render";
    return Matrix_NewMtx(static_cast<GraphicsContext*>(gfx), kFile, 0);
}

// Sem gSPDisplayList: a função do jogo procura um caminho OTR no ponteiro, e a lista do mod é memória crua.
void DrawNative(void* gfx, const void* displayList, uint8_t layer) {
    auto* gfxCtx = static_cast<GraphicsContext*>(gfx);
    Gfx** head = layer == LINKSPAN_OOT_RENDER_TRANSLUCENT ? &gfxCtx->polyXlu.p : &gfxCtx->polyOpa.p;
    __gSPDisplayList((*head)++, static_cast<Gfx*>(const_cast<void*>(displayList)));
}

void SetToon(void* gfx, bool enabled) {
    auto* gfxCtx = static_cast<GraphicsContext*>(gfx);
    gSPToon(gfxCtx->polyOpa.p++, enabled);
    gSPToon(gfxCtx->polyXlu.p++, enabled);
}

ShipNativeStatus FrameInfo(ShipOotRenderFrameInfoV1* info) {
    if (!gGameState)
        return SHIP_NATIVE_UNSUPPORTED;
    info->frame = gGameState->frames;
    info->camera_epoch = static_cast<uint32_t>(FrameInterpolation_GetCameraEpoch());
    info->delta_seconds = (R_UPDATE_RATE > 0 ? R_UPDATE_RATE : 3) / 60.0f;
    info->aspect_ratio = OTRGetAspectRatio();
    return SHIP_NATIVE_OK;
}

void InterpolationOpen(const void* key, int32_t child) {
    FrameInterpolation_RecordOpenChild(key, child);
}

void InterpolationClose() {
    FrameInterpolation_RecordCloseChild();
}

} // namespace

namespace ShipLuaHost {

void RegisterOotViewGameHooks() {
    static bool registered = false;
    if (registered || !GameInteractor::Instance) {
        return;
    }
    registered = true;
    OotViewBridge bridge;
    bridge.cameraCreate = CameraCreate;
    bridge.cameraValid = CameraValid;
    bridge.cameraDestroy = CameraDestroy;
    bridge.cameraApply = CameraApply;
    bridge.cameraRead = CameraRead;
    bridge.drawDisplayList = DrawDisplayList;
    bridge.matrixPush = Matrix_Push;
    bridge.matrixPop = Matrix_Pop;
    bridge.matrixTranslate = MatrixTranslateApply;
    bridge.matrixScale = MatrixScaleApply;
    bridge.matrixRotateZYX = MatrixRotateApply;
    bridge.emitToonKey = EmitToonKey;
    bridge.emitStencil = EmitStencil;
    bridge.emitToonShadow = EmitToonShadow;
    bridge.flushToonShadows = FlushToonShadows;
    bridge.setToonRamp = SetToonRamp;
    bridge.setToonShadowParams = SetToonShadowParams;
    bridge.resetToonLook = ResetToonLook;
    bridge.matrixTranslateNew = MatrixTranslateNew;
    bridge.matrixRotateAxis = MatrixRotateAxisApply;
    bridge.exportMatrix = ExportMatrix;
    bridge.drawNative = DrawNative;
    bridge.setToon = SetToon;
    bridge.frameInfo = FrameInfo;
    bridge.interpolationOpen = InterpolationOpen;
    bridge.interpolationClose = InterpolationClose;
    SetOotViewBridge(bridge);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnCameraState>(
        [](PlayState*) { ShipLuaHost::UpdateOotCamera(); });
}

} // namespace ShipLuaHost

// Chamada no início de Player_PostLimbDrawGameplay (z_player_lib.c), na thread do jogo.
extern "C" void LinkSpan_PlayerLimbDraw(PlayState* play, s32 limbIndex, Actor* actor) {
    const auto* registry = ShipLuaHost::GetOotHookRegistry();
    const auto point = ShipLuaHost::GetOotHookPoints().playerLimbDraw;
    if (!registry || !registry->HasHooks(point)) {
        return;
    }
    ShipOotPlayerLimbHookV1 payload{ sizeof(ShipOotPlayerLimbHookV1), play, actor, limbIndex };
    Matrix_Push();
    ShipLuaHost::EnterOotRenderScope(play->state.gfxCtx);
    ShipLuaHost::GetOotHookRegistry()->Dispatch(point, &payload, sizeof(payload), nullptr, nullptr);
    ShipLuaHost::LeaveOotRenderScope();
    Matrix_Pop();
}

// HOST-SLIM-001 (RFC 0029): Play_ClearCamera avisa antes de liberar o slot.
extern "C" void LinkSpan_CameraCleared(PlayState* play, s16 camId) {
    ShipLuaHost::NotifyOotCameraCleared(play, camId);
}

const ShipOotFogV1& ShipLuaHost::GetOotNativeFogService() {
    static const ShipOotFogV1 service{ sizeof(ShipOotFogV1), SubmitFogGrid };
    return service;
}

const ShipOotFogV2& ShipLuaHost::GetOotNativeFogFlowService() {
    static const ShipOotFogV2 service{ sizeof(ShipOotFogV2), SubmitFogFlow,
                                      LINKSPAN_OOT_FOG_FLOW_BLOCKED_EDGES | LINKSPAN_OOT_FOG_FLOW_VORTICES |
                                          LINKSPAN_OOT_FOG_FLOW_LIFT | LINKSPAN_OOT_FOG_FLOW_DOMAIN_SHIFT |
                                          LINKSPAN_OOT_FOG_FLOW_LOCAL_LIGHTS | LINKSPAN_OOT_FOG_FLOW_STACKED_SUPPORTS |
                                          LINKSPAN_OOT_FOG_FLOW_ATMOSPHERE | LINKSPAN_OOT_FOG_FLOW_VERTICAL_COMPUTE |
                                          LINKSPAN_OOT_FOG_FLOW_CPU_FIELD | LINKSPAN_OOT_FOG_FLOW_DETAIL_GRID |
                                          LINKSPAN_OOT_FOG_FLOW_DETAIL_WIDE };
    return service;
}

extern "C" void LinkSpan_BeginFogFrame(void* play) {
    gFogFrame.play = play;
    gFogFrame.submitted = false;
    gFogFrame.hasFlow = false;
}

extern "C" void LinkSpan_EndFogFrame(void) {
    gFogFrame.play = nullptr;
    gFogFrame.submitted = false;
}

extern "C" void LinkSpan_ResetFogScene(void) {
    gFogFrame.sourcePlay = nullptr;
    LinkSpan_EndFogFrame();
}

extern "C" int LinkSpan_TakeFogGrid(void* play, FastFogVolume* volume, const FastFogFlow** flow) {
    if (!volume || !play || play != gFogFrame.play || !gFogFrame.submitted) return 0;
    const bool drawable = gFogFrame.volume.grid[0] != 0.0f;
    if (drawable) {
        *volume = gFogFrame.volume;
        *flow = gFogFrame.hasFlow ? &gFogFrame.flow : nullptr;
    }
    LinkSpan_EndFogFrame();
    return drawable;
}

// O roll do lease V2 entra depois da função de câmera do jogo e antes do vetor up
// (z_camera.c, Camera_Update), que é onde o jogo o lê.
extern "C" void LinkSpan_CameraView(Camera* camera) {
    PlayState* play = gPlayState;
    if (!play || !camera || play->activeCamera <= CAM_ID_MAIN || play->activeCamera >= NUM_CAMS ||
        play->cameraPtrs[play->activeCamera] != camera) {
        return;
    }
    int16_t roll = 0;
    if (ShipLuaHost::GetOotCameraRoll(play->activeCamera, play, &roll)) {
        camera->roll = roll;
    }
}
