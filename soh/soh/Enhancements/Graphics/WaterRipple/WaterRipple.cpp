#include "WaterRipple.h"
#include "water_ripple_field.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <stdexcept>

#include <ship/Context.h>
#include <ship/config/ConsoleVariable.h>
#include "soh/cvar_prefixes.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
}

namespace {

constexpr int StepsPerTick = 3;     // 20 Hz de jogo, passo fixo de 1/60 s
constexpr int QuietTicks = 20 * 12; // sem eventos por 12 s: o campo já decaiu
constexpr float PlaneTolerance = 2.0f;
constexpr float Strength = 1.0f;

std::atomic<bool> wanted{ false };
std::mutex mutex;
std::unique_ptr<water::Field> field;
std::vector<uint16_t> packed;
WaterRipple::Frame current{};
uint64_t currentVersion = 0;
float planeY = 0.0f;
bool hasPlane = false;
int quiet = QuietTicks;
int lastScene = -1;
const void* lastPlay = nullptr;
water::Config simulationConfig{};
bool publishFlow = false;
bool publishFlowXZ = false;
bool meleeContact = false;
water::FlowComponent flowComponent = water::FlowComponent::X;

void Publish(bool active) {
    const auto uv = field->WorldToUV();
    current.worldToUV = { uv[0], uv[1], uv[2], uv[3] };
    current.info = { planeY, Strength, 1.0f / WaterRipple::Side, active ? (publishFlowXZ ? 2.0f : 1.0f) : 0.0f };
    if (active) {
        if (publishFlowXZ) {
            field->PackTextureRGBA16FlowXZ(packed);
        } else if (publishFlow) {
            field->PackTextureRGBA16Flow(packed, flowComponent);
        } else {
            field->PackTextureRGBA16(packed);
        }
    } else {
        std::fill(packed.begin(), packed.end(), uint16_t{ 0 });
    }
    ++currentVersion;
}

// Chamada com o mutex tomado.
bool Prepare(PlayState* play, float y) {
    if (!play) {
        return false;
    }
    if (!field) {
        field = std::make_unique<water::Field>(simulationConfig);
        packed.assign(size_t{ WaterRipple::Side } * WaterRipple::Side * 4, 0);
    }
    if (!hasPlane || std::fabs(y - planeY) > PlaneTolerance) {
        field->Reset();
        planeY = y;
        hasPlane = true;
    }
    if (simulationConfig.solver == water::Solver::ShallowWater) {
        if (const Player* player = GET_PLAYER(play))
            field->Follow(player->actor.world.pos.x, player->actor.world.pos.z);
    }
    quiet = 0;
    return true;
}

} // namespace

namespace WaterRipple {

bool Configure(const water::Config& config, bool flow, water::FlowComponent component) {
    if (config.side != Side || config.stepSeconds != 1.0f / 60.0f ||
        (component != water::FlowComponent::X && component != water::FlowComponent::Z))
        return false;
    std::unique_ptr<water::Field> next;
    try {
        next = std::make_unique<water::Field>(config);
    } catch (const std::invalid_argument&) { return false; }
    // Todas as alocações potencialmente falíveis precedem a troca do estado.
    std::vector<uint16_t> nextPacked(size_t{ Side } * Side * 4, 0);
    std::lock_guard lock(mutex);
    simulationConfig = config;
    publishFlow = flow;
    publishFlowXZ = false;
    meleeContact = false;
    flowComponent = component;
    if (wanted.load(std::memory_order_relaxed)) {
        field.swap(next);
        packed.swap(nextPacked);
    } else {
        field.reset();
        packed.clear();
    }
    current = {};
    hasPlane = false;
    quiet = QuietTicks;
    ++currentVersion;
    return true;
}

void SetWanted(bool value) {
    wanted.store(value);
    if (!value) {
        std::lock_guard lock(mutex);
        field.reset();
        packed.clear();
        current = {};
        hasPlane = false;
        quiet = QuietTicks;
        meleeContact = false;
        ++currentVersion;
    }
}

bool Read(uint64_t& version, std::vector<uint16_t>& texels, Frame& frame) {
    std::lock_guard lock(mutex);
    frame = current;
    if (version == currentVersion || packed.empty()) {
        return false;
    }
    texels = packed;
    version = currentVersion;
    return true;
}

} // namespace WaterRipple

extern "C" void WaterRipple_OnRipple(PlayState* play, const float* pos, short radius, short radiusMax) {
    if (!wanted.load(std::memory_order_relaxed) || !pos) {
        return;
    }
    std::lock_guard lock(mutex);
    if (!Prepare(play, pos[1])) {
        return;
    }
    const float size = static_cast<float>(std::max(radius, radiusMax));
    field->Inject(pos[0], pos[2], std::clamp(size * 0.06f, 20.0f, 64.0f),
                  -std::clamp(40.0f + size * 0.12f, 40.0f, 160.0f));
}

extern "C" void WaterRipple_OnSplash(PlayState* play, const float* pos, short scale) {
    if (!wanted.load(std::memory_order_relaxed) || !pos) {
        return;
    }
    std::lock_guard lock(mutex);
    if (!Prepare(play, pos[1])) {
        return;
    }
    const float size = static_cast<float>(std::max<short>(scale, 0));
    field->Inject(pos[0], pos[2], std::clamp(24.0f + size * 0.05f, 24.0f, 80.0f),
                  -std::clamp(120.0f + size * 0.1f, 120.0f, 220.0f));
}

namespace {
// gDeveloperTools.WaterSolver: 0 = onda amortecida (R034), 1 = águas rasas (R035, padrão desde 12/11: sem ele o
// rio não corre).
int appliedSolver = 0;

// Uma janela/plano local: a base vem do poligono do leito mais proximo do Link.
// z_bgcheck.c:4551 (waterbox), 4481/4494/4502 (corrente);
// z_player.c:11361-11369,11880,12048-12052; z_actor.c:1352-1357.
// A cena nao armazena vetores em WaterBox. Nao inferir direcao de caminhos de som.
bool NearbyWater(PlayState* play, const Player* player, Vec3f& focus, water::Point& base) {
    base = {};
    const auto* header = play->colCtx.colHeader;
    if (!header || !header->waterBoxes ||
        header->waterBoxes == PHYSICAL_TO_VIRTUAL(gSegments[0])) return false;
    float best = 384.0f * 384.0f;
    bool found = false;
    int bestIndex = -1;
    for (int i = 0; i < header->numWaterBoxes; ++i) {
        const auto& box = header->waterBoxes[i];
        if (box.notSwimmable || (box.room != -1 && box.room != play->roomCtx.curRoom.num) ||
            box.xLength <= 2.0f || box.zLength <= 2.0f ||
            std::fabs(player->actor.world.pos.y - box.ySurface) > 384.0f) continue;
        const float x = std::clamp(player->actor.world.pos.x, float(box.xMin) + 1.0f,
                                   float(box.xMin + box.xLength) - 1.0f);
        const float z = std::clamp(player->actor.world.pos.z, float(box.zMin) + 1.0f,
                                   float(box.zMin + box.zLength) - 1.0f);
        const float dx = x - player->actor.world.pos.x, dz = z - player->actor.world.pos.z;
        const float distance = dx * dx + dz * dz;
        if (distance >= best) continue;
        best = distance;
        focus = { x, float(box.ySurface), z };
        found = true;
        bestIndex = i;
    }
    if (!found) return false;
    {
        // Da margem, o ponto preso na borda da caixa costuma cair no barranco (leito acima da superfície) e a
        // corrente zerava. Grade 5x5 de sondas (+-192) dentro da caixa; média das correntes válidas.
        // Vale em qualquer cena: sem leito de correnteza (conveyor) as sondas não acertam e a base fica zero
        // (GFX-UNIFY-001, 12/11; antes só no Zora's River).
        const auto& box = header->waterBoxes[bestIndex];
        const float minX = float(box.xMin) + 1.0f, maxX = float(box.xMin + box.xLength) - 1.0f;
        const float minZ = float(box.zMin) + 1.0f, maxZ = float(box.zMin + box.zLength) - 1.0f;
        constexpr float ProbeSpacing = 96.0f;
        constexpr float speeds[]{ 2.0f, 4.0f, 7.0f }; // z_player.c:11880
        // Velocidade do ator -> mundo/s do solver: 20 ticks/s * speedRate.
        const float rate = (R_UPDATE_RATE * 0.5f) * 20.0f;
        float sumX = 0.0f, sumZ = 0.0f;
        int hits = 0;
        for (int gz = -2; gz <= 2; ++gz) {
            for (int gx = -2; gx <= 2; ++gx) {
                Vec3f probe{ std::clamp(focus.x + gx * ProbeSpacing, minX, maxX), focus.y + 1.0f, // abaixo de pontes
                             std::clamp(focus.z + gz * ProbeSpacing, minZ, maxZ) };
                CollisionPoly* poly = nullptr;
                s32 bgId = BGCHECK_SCENE;
                const float bed = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
                if (!poly || bed > focus.y || SurfaceType_IsConveyor(&play->colCtx, poly, bgId)) continue;
                const auto speed = SurfaceType_GetConveyorSpeed(&play->colCtx, poly, bgId);
                if (speed < 1 || speed > 3) continue;
                const s16 yaw = static_cast<s16>(SurfaceType_GetConveyorDirection(&play->colCtx, poly, bgId) << 10);
                sumX += Math_SinS(yaw) * speeds[speed - 1] * rate;
                sumZ += Math_CosS(yaw) * speeds[speed - 1] * rate;
                ++hits;
            }
        }
        if (hits > 0) base = { sumX / hits, sumZ / hits };
    }
    return true;
}

// WeaponInfo e atualizado por z_player_lib.c:1530-1552; z64player.h:641-645.
// Um impulso por golpe que realmente intercepta o plano dentro de uma waterbox.
// Nao depende de um splash visual e nao acrescenta eventos ao caminho legado.
void InjectMeleeWater(PlayState* play, const Player* player) {
    const auto& weapon = player->meleeWeaponInfo[0];
    if (player->meleeWeaponState <= 0 || !weapon.active) {
        meleeContact = false;
        return;
    }
    if (meleeContact) return;
    const float dy = weapon.tip.y - weapon.base.y;
    float t = 0.5f;
    if (std::fabs(dy) > 0.0001f) {
        t = (planeY - weapon.base.y) / dy;
        if (t < 0.0f || t > 1.0f) return;
    } else if (std::fabs(weapon.base.y - planeY) > PlaneTolerance) {
        return;
    }
    const float x = weapon.base.x + t * (weapon.tip.x - weapon.base.x);
    const float z = weapon.base.z + t * (weapon.tip.z - weapon.base.z);
    WaterBox* box = nullptr;
    float surface = planeY;
    if (!WaterBox_GetSurface1(play, &play->colCtx, x, z, &surface, &box) ||
        std::fabs(surface - planeY) > PlaneTolerance) return;
    if (field->Inject(x, z, 24.0f, -120.0f)) { // mesmo minimo de OnSplash
        quiet = 0;
        meleeContact = true;
    }
}

// Chamado com mutex; modo 0 continua no caminho original abaixo.
void TickCurrent(PlayState* play) {
    if (play != lastPlay || play->sceneNum != lastScene) {
        lastPlay = play;
        lastScene = play->sceneNum;
        if (field) {
            field->Reset();
            field->SetBackgroundCurrent({});
            Publish(false);
        }
        hasPlane = false;
        quiet = QuietTicks;
        meleeContact = false;
    }
    if (play->pauseCtx.state != 0) return;
    const Player* player = GET_PLAYER(play);
    Vec3f focus{};
    water::Point base{};
    if (!player || !NearbyWater(play, player, focus, base)) {
        // Suspende sem apagar o campo; nao repete upload de zeros a cada tick.
        if (field && current.info[3] != 0.0f) Publish(false);
        meleeContact = false;
        return;
    }
    if (!field || !hasPlane || std::fabs(focus.y - planeY) > PlaneTolerance) {
        if (!Prepare(play, focus.y)) return;
        quiet = QuietTicks; // Prepare nao representa um evento neste caminho.
    }
    publishFlowXZ = true;
    if (!field->SetBackgroundCurrent(base)) {
        // Rejeicao explicita do CFL: nao publicar corrente anterior em outro trecho.
        field->SetBackgroundCurrent({});
        Publish(false);
        return;
    }
    const bool flowing = base.x != 0.0f || base.z != 0.0f;
    field->Follow(player->actor.world.pos.x, player->actor.world.pos.z);
    InjectMeleeWater(play, player);
    if (!flowing && quiet >= QuietTicks) {
        if (current.info[3] != 0.0f) Publish(false);
        return;
    }
    for (int i = 0; i < StepsPerTick; ++i) field->Step();
    if (flowing) {
        // Nao envelhecer ate QuietTicks: continua ativo mesmo sem eventos.
        quiet = std::min(quiet + 1, QuietTicks);
        Publish(true);
    } else if (++quiet >= QuietTicks) {
        field->Reset();
        Publish(false);
    } else {
        Publish(true);
    }
}

void ApplySolverCVar() {
    const int solver = std::clamp(
        Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_DEVELOPER_TOOLS("WaterSolver"), 1), 0,
        1);
    if (solver == appliedSolver) {
        return;
    }
    water::Config config;
    config.side = WaterRipple::Side;
    config.solver = solver == 1 ? water::Solver::ShallowWater : water::Solver::DampedWave;
    if (WaterRipple::Configure(config)) {
        appliedSolver = solver;
    }
}
} // namespace

extern "C" void WaterRipple_Tick(PlayState* play) {
    if (!wanted.load(std::memory_order_relaxed) || !play) {
        return;
    }
    ApplySolverCVar();
    std::lock_guard lock(mutex);
    if (appliedSolver == 1) {
        TickCurrent(play);
        return;
    }
    if (!field) {
        return;
    }
    if (play != lastPlay || play->sceneNum != lastScene) {
        lastPlay = play;
        lastScene = play->sceneNum;
        field->Reset();
        hasPlane = false;
        quiet = QuietTicks;
        Publish(false);
        return;
    }
    if (quiet >= QuietTicks || play->pauseCtx.state != 0) {
        return;
    }
    if (const Player* player = GET_PLAYER(play)) {
        field->Follow(player->actor.world.pos.x, player->actor.world.pos.z);
    }
    for (int i = 0; i < StepsPerTick; ++i) {
        field->Step();
    }
    if (++quiet >= QuietTicks) {
        field->Reset();
        Publish(false);
    } else {
        Publish(true);
    }
}
