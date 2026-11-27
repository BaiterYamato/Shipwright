#include "WorldPass.h"
#include "PassSource.h"
#include "SunShadow.h"

#ifdef _WIN32
// libultraship always builds its DX11 backend on Windows. Its definition is private to that target.
#ifndef ENABLE_DX11
#define ENABLE_DX11
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "GeometryShadow.h"
#include "FairyLightCap.h"
#include "soh/Enhancements/Graphics/WaterRipple/WaterRipple.h"
#include "soh/Enhancements/Graphics/Decals/ImpactDecals.h"
#include "soh/Enhancements/Graphics/Weather/WetSurface.h"
#include "soh/Enhancements/Graphics/Weather/WindField.h"
#include <wrl/client.h>
#include <fast/Fast3dWindow.h>
#include <fast/backends/gfx_direct3d_common.h>
#include "PackGpuTimingsDX11.h"
#include <fast/labpbr_kokiri.h>
#include <fast/world_postprocess.h>
#include <ship/Context.h>
#include <ship/config/ConsoleVariable.h>
#include <spdlog/spdlog.h>
#include <d3d11shader.h>
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <numeric>
#include <system_error>
#include <thread>
#include <vector>

extern "C" {
#include "z64.h"
#include "overlays/actors/ovl_En_Elf/z_en_elf.h"
extern PlayState* gPlayState;
extern SaveContext gSaveContext;
void Matrix_MtxToMtxF(Mtx* source, MtxF* destination);
}

namespace {

using Microsoft::WRL::ComPtr;

constexpr size_t FogLayerModeCount = 6;
// Calibrado no A/B M02b de 11/11/2026: com a luz do pack, a camada cheia apaga o
// contraste do chao; tau x 0,25 (modo 3) e o padrao de "fog": "over".
constexpr int DefaultFogLayerMode = 3;
using ShaderPacks::MaxPassInputs;
using ShaderPacks::SceneColorInput;
using ShaderPacks::SceneDepthInput;
using ShaderPacks::SceneMaterialNormalInput;
using ShaderPacks::SceneMaterialSpecularInput;
using ShaderPacks::SceneFogShadowInput;
using ShaderPacks::SceneGeometryShadowInput;
using ShaderPacks::ScenePointShadowInput;
using ShaderPacks::SceneSunShadowInput;
using ShaderPacks::SunShadowMap;
using ShaderPacks::SunShadowMesh;
using ShaderPacks::UnboundInput;
using ShaderPacks::SceneNoiseInput;
using ShaderPacks::ZeroInput;
using ShaderPacks::OneInput;
using ShaderPacks::LegacyPassInputs;
using ShaderPacks::MaxPassOutputs;
// Fast3D e o host usam t0..t11 (entradas, ondulação e temporal); o Draw salva e restaura todos.
constexpr UINT SavedSourceSlots = 12;

struct CameraConstants {
    float projection[4][4];
    float view[4][4];
    float info[4]; // Near, far, interpolation fraction, replaced-matrix flags.
};
static_assert(sizeof(CameraConstants) == 9 * 16);

// RFC-M11: estado privado do host; nenhuma mudanca de ABI da libultraship.
struct TemporalConstants {
    float projection[4][4];
    float view[4][4];
    float info[4];
    float state[4];
};
static_assert(sizeof(TemporalConstants) == 10 * 16);

// RFC 0035: b10 LinkSpanIris. Projeções já com o ajuste de aspecto que o Fast3D aplica no x.
struct IrisConstants {
    float previousProjection[4][4];
    float previousView[4][4];
    float fog[4];  // Cor da névoa (rgb) e início em unidades do jogo.
    float info[4]; // Quadro anterior válido, duração do quadro, ajuste de aspecto atual, fim da névoa.
    // Relógio de render (não o de gameplay, 20 Hz): quadros desenhados (mod 720720) e segundos (mod 3600), como
    // frameCounter e frameTimeCounter do Iris. Com a câmera interpolada há vários quadros por quadro de jogo.
    // zw: rampa da névoa nativa em u (PolicromaFeatureFogRamp), início e fim já suavizados; {0, 0} = ausente.
    float clock[4];
    // Parâmetros do provider (setParameters da ponte V3), por exemplo a altura das nuvens de um pack Iris.
    float parameters[4];
    // Vetor 12 (PolicromaFeatureSkyExposure): x = quanto céu o jogador vê (0..1, suavizado), y = envCtx.indoors,
    // z = envCtx.skyboxDisabled, w = skyboxId; y, z e w crus, sem suavizar.
    float sky[4];
};
static_assert(sizeof(IrisConstants) == 13 * 16);
// Os shaders podem declarar só um prefixo do bloco (10 a 13 vetores; os de providers anteriores têm menos).
constexpr UINT MinIrisVectors = 10;

// Rampa da névoa nativa na escala "u" do gSPFogPosition (início, fim). {0, 0} = sem rampa; uma rampa válida
// sempre tem fim > 0 (vanilla termina em 1000, worldFog em u0 + 0,0001 no mínimo).
using FogRamp = std::array<float, 2>;
constexpr float FogRampVanillaEnd = 1000.0f;
constexpr float FogRampMinSpan = 0.0001f;    // mesmo piso do Play_SetFog (z_play.c)
constexpr double FogRampTimeConstant = 0.5;  // segundos; o jogo interpola hora e clima em degraus de 20 Hz

bool HasFogRamp(const FogRamp& ramp) {
    return ramp[1] > 0.0f;
}

// Mesma conta do Play_FogU (z_play.c): u = 1000 * f/(f-n) * (1 - n/d), com d travado em n.
float FogRampU(float distance, float zNear, float zFar) {
    return FogRampVanillaEnd * (zFar / (zFar - zNear)) * (1.0f - zNear / std::max(distance, zNear));
}

// Os mesmos números que Play_SetFog entrega ao gSPFogPosition / gSPFogFactorF neste quadro.
FogRamp NativeFogRamp(const LightContext& light) {
    float start, end;
    if (light.worldFog) {
        start = FogRampU(light.fogStart, light.zNear, light.zFar);
        end = std::max(FogRampU(light.fogEnd, light.zNear, light.zFar), start + FogRampMinSpan);
    } else if (light.fogNear < 0) {
        // Gfx_SetFog (z_rcp.c) com near < 0 usa gSPFogFactor(0, 255): névoa total em toda parte. Rampa degenerada
        // {0, piso} para o provider ver "tudo coberto" em vez de uma rampa normal 0..1000.
        start = 0.0f;
        end = FogRampMinSpan;
    } else {
        start = std::min(static_cast<float>(light.fogNear), FogRampVanillaEnd);
        end = FogRampVanillaEnd;
    }
    if (!std::isfinite(start) || !std::isfinite(end))
        return {};
    return { start, end };
}

// Filtro exponencial em u (k do provider é muito não linear em u0). dt <= 0 (1º quadro, troca de cena, pausa)
// ou rampa ausente em um dos lados: salta direto para o alvo.
FogRamp SmoothFogRamp(const FogRamp& previous, const FogRamp& target, double dt) {
    if (dt <= 0.0 || !HasFogRamp(previous) || !HasFogRamp(target))
        return target;
    const float blend = static_cast<float>(1.0 - std::exp(-dt / FogRampTimeConstant));
    return { previous[0] + (target[0] - previous[0]) * blend, previous[1] + (target[1] - previous[1]) * blend };
}

// Quanto céu o jogador vê, 0..1, sem lista de cenas (a política de luz fica no provider). A cena diz se é interior
// (indoors) e a sala se o céu está desligado (skyboxDisabled): indoors com skyboxId de céu normal visível conta como
// céu; indoors sem céu = 0; ao ar livre com o céu desligado pela sala (copa, túnel) = SkyOpennessCovered.
constexpr float SkyOpennessOpen = 1.0f;
constexpr float SkyOpennessCovered = 0.5f;
constexpr float SkyOpennessClosed = 0.0f;
constexpr float SkyOpennessUnset = -1.0f;     // ainda sem valor suavizado: o 1º quadro salta para o alvo
constexpr double SkyOpennessTimeConstant = 1.0; // segundos

// x = openness alvo, y = indoors, z = skyboxDisabled, w = skyboxId (o que vai cru no vetor 12 do b10).
using SkyFacts = std::array<float, 4>;

float SkyOpenness(int skyboxId, bool indoors, bool skyboxDisabled) {
    if (skyboxId == SKYBOX_NORMAL_SKY && !skyboxDisabled)
        return SkyOpennessOpen;
    // O Market se declara indoors (cena pré-renderizada), mas é ao ar livre: o skybox dele é o céu pintado da praça.
    // Medido em 22/11: entrance B1 dava indoors=1 skyboxId=9 e escurecia a praça como se fosse casa.
    if (skyboxId == SKYBOX_MARKET_ADULT || skyboxId == SKYBOX_MARKET_CHILD_DAY || skyboxId == SKYBOX_MARKET_CHILD_NIGHT)
        return SkyOpennessOpen;
    if (indoors)
        return SkyOpennessClosed;
    return skyboxDisabled ? SkyOpennessCovered : SkyOpennessOpen;
}

SkyFacts NativeSkyFacts(const PlayState& play) {
    const bool indoors = play.envCtx.indoors != 0;
    const bool skyboxDisabled = play.envCtx.skyboxDisabled != 0;
    return { SkyOpenness(play.skyboxId, indoors, skyboxDisabled), indoors ? 1.0f : 0.0f,
             skyboxDisabled ? 1.0f : 0.0f, static_cast<float>(play.skyboxId) };
}

// Mesmo molde do SmoothFogRamp: dt <= 0 (1º quadro, troca de cena, pausa) ou sem valor anterior salta para o alvo.
float SmoothSkyOpenness(float previous, float target, double dt) {
    if (dt <= 0.0 || previous < 0.0f)
        return target;
    const float blend = static_cast<float>(1.0 - std::exp(-dt / SkyOpennessTimeConstant));
    return previous + (target - previous) * blend;
}
// setParameters da ponte V3: escrito pela thread do mod, lido no desenho.
std::array<std::atomic<float>, 4> providerParameters{};

bool TemporalEye(const CameraConstants& camera, std::array<float, 3>& eye) {
    for (const auto& row : camera.projection)
        for (float value : row)
            if (!std::isfinite(value)) return false;
    for (const auto& row : camera.view)
        for (float value : row)
            if (!std::isfinite(value)) return false;
    for (float value : camera.info)
        if (!std::isfinite(value)) return false;
    const auto& v = camera.view;
    const float cof[3][3] = {
        { v[1][1]*v[2][2]-v[1][2]*v[2][1], v[1][2]*v[2][0]-v[1][0]*v[2][2], v[1][0]*v[2][1]-v[1][1]*v[2][0] },
        { v[2][1]*v[0][2]-v[2][2]*v[0][1], v[2][2]*v[0][0]-v[2][0]*v[0][2], v[2][0]*v[0][1]-v[2][1]*v[0][0] },
        { v[0][1]*v[1][2]-v[0][2]*v[1][1], v[0][2]*v[1][0]-v[0][0]*v[1][2], v[0][0]*v[1][1]-v[0][1]*v[1][0] }
    };
    const float det = v[0][0]*cof[0][0] + v[0][1]*cof[0][1] + v[0][2]*cof[0][2];
    if (!std::isfinite(det) || std::abs(det) < 0.00001f || camera.info[1] <= camera.info[0] ||
        std::abs(camera.projection[0][0]) < 0.00001f || std::abs(camera.projection[1][1]) < 0.00001f) return false;
    for (size_t i = 0; i < 3; ++i) {
        eye[i] = -(v[3][0]*cof[i][0] + v[3][1]*cof[i][1] + v[3][2]*cof[i][2]) / det;
        if (!std::isfinite(eye[i])) return false;
    }
    return true;
}

bool TemporalCameraCut(const CameraConstants& current, const CameraConstants& previous) {
    std::array<float, 3> a{}, b{};
    if (!TemporalEye(current, a) || !TemporalEye(previous, b)) return true;
    float displacement = 0.0f;
    for (size_t i = 0; i < 3; ++i) displacement += (a[i]-b[i])*(a[i]-b[i]);
    const float limit = std::max(20.0f * current.info[0], 0.02f * current.info[1]);
    // info[3] (flags de matriz substituida) alterna 0/3 entre quadro real e interpolado (jogo 20 Hz, tela 60 Hz):
    // as duas matrizes sao as desenhadas, entao a troca da flag nao e corte (prova de 12/11).
    if (displacement > limit * limit) return true;
    // Giro abrupto em qualquer eixo; bases interpoladas podem nao ser ortogonais.
    for (size_t axis = 0; axis < 3; ++axis) {
        float dot = 0.0f, aa = 0.0f, bb = 0.0f;
        for (size_t row = 0; row < 3; ++row) {
            const float x = current.view[row][axis], y = previous.view[row][axis];
            dot += x*y; aa += x*x; bb += y*y;
        }
        if (dot < 0.9063078f * std::sqrt(aa*bb)) return true; // 25 graus
    }
    for (size_t row = 0; row < 4; ++row)
        for (size_t col = 0; col < 4; ++col)
            if (std::abs(current.projection[row][col] - previous.projection[row][col]) > 0.0001f) return true;
    return false;
}

// b6: molhado (2 vetores, packs antigos) seguido do campo de vento (mais 2, R054).
struct WeatherConstants {
    WetSurface::Frame wet;
    WindField::Frame wind;
};
static_assert(sizeof(WeatherConstants) == 64);

using SceneVector = std::array<float, 4>;
// A direcao solar publicada ao pack continua intacta. So o mapa usa a luz oposta a noite.
bool UseMoonShadow(const SceneVector& sun, uint16_t dayTime) {
    const float length = std::sqrt(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
    if (!std::isfinite(length) || length <= 1.0f || !(sun[1] < -0.035f * length))
        return false;
    const auto debug = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
        CVAR_DEVELOPER_TOOLS("MoonShadowDebug"), 0);
    return !debug || dayTime % 4u != 1u;
}

constexpr size_t TransmissionTriangleOffset = 1 + 2 * ShaderPacks::MaxTransmissionMaterials;
using TransmissionConstants =
    std::array<SceneVector, TransmissionTriangleOffset + 3 * Fast::MaxWorldTransmissionTriangles>;
struct PointShadowConstants {
    SceneVector source;
    SceneVector light;
};
static_assert(sizeof(PointShadowConstants) == 2 * 16);
constexpr size_t MaxPointLights = ShaderPacks::GeometryShadow::MaxPointLights;
constexpr size_t MaxSceneLights = 8;
constexpr size_t MaxWaterBoxes = 8;
// Mapa solar em toda cena com céu aberto de dia e noite (skybox normal visível), sem lista de cenas
// (GFX-UNIFY-001, 12/11: antes eram só Kokiri, Lost Woods, Graveyard e Hyrule Field; Zora's River ficava sem
// a sombra do Link). Fora das áreas Unbound, o pack aplica só a sombra quando o mapa é válido.
bool UsesSolarGeometry(const PlayState& play, bool lightsFog = false) {
    // Kokiri's exterior uses the fogged horizon without the normal skybox.
    // A pack that requests solar scattering can capture its outdoor occluders.
    // Só com o feixe ligado (GFX-PBR-001, 20/11): no padrão a sala 0 de Kokiri fica como antes da integração.
    if (play.skyboxId == SKYBOX_NORMAL_SKY && !play.envCtx.skyboxDisabled)
        return true;
    if (!(lightsFog && play.sceneNum == SCENE_KOKIRI_FOREST && play.roomCtx.curRoom.num == 0 &&
          play.skyboxId == SKYBOX_UNSET_1D))
        return false;
    const auto cvars = Ship::Context::GetRawInstance()->GetConsoleVariables();
    return cvars->GetInteger(CVAR_DEVELOPER_TOOLS("FogreKokiriShaftStrength"), 0) > 0 ||
           cvars->GetInteger(CVAR_DEVELOPER_TOOLS("FogreKokiriCanopy"), 0) > 0;
}

struct SceneConstants {
    SceneVector info; // Scene, room, render time in seconds, day fraction.
    SceneVector sun;
    SceneVector counts; // Lights, water boxes, lights selected for point shadows.
    SceneVector sunColor;
    std::array<SceneVector, MaxSceneLights> lightPositions;
    std::array<SceneVector, MaxSceneLights> lightColors;
    std::array<SceneVector, MaxWaterBoxes> waterBounds;
    std::array<SceneVector, MaxWaterBoxes> waterPlanes;
};
static_assert(sizeof(SceneConstants) == 36 * 16);

// Identidade obtida do ator vivo, sem cache de ponteiros que sobreviva a destruição, reload ou troca de cena.
ShaderPacks::FairyLightCap::Owner ClassifyFairyLightOwner(const PlayState& play, const LightInfo* info) {
    using Owner = ShaderPacks::FairyLightCap::Owner;
    for (const auto* actor = play.actorCtx.actorLists[ACTORCAT_ITEMACTION].head; actor; actor = actor->next) {
        if (actor->id != ACTOR_EN_ELF)
            continue;
        const auto* fairy = reinterpret_cast<const EnElf*>(actor);
        if (info != &fairy->lightInfoGlow && info != &fairy->lightInfoNoGlow)
            continue;
        return actor->params == FAIRY_NAVI ? Owner::Navi : Owner::Fairy;
    }
    return Owner::Other;
}

void CaptureScene(const PlayState& play, SceneConstants& scene,
                  const ShaderPacks::TransmissionMaterials* shadowMaterials, size_t pointLightCapacity) {
    scene.info = { static_cast<float>(play.sceneNum), static_cast<float>(play.roomCtx.curRoom.num),
                   static_cast<float>(play.gameplayFrames), gSaveContext.dayTime / 65536.0f };
    scene.sun = { play.envCtx.sunPos.x, play.envCtx.sunPos.y, play.envCtx.sunPos.z, 0.0f };
    const auto& direct = play.envCtx.dirLight1.params.dir;
    scene.sunColor = { direct.color[0] / 255.0f, direct.color[1] / 255.0f, direct.color[2] / 255.0f, 0.0f };

    struct PointLight {
        SceneVector position;
        SceneVector color;
        float score;
        float shadowScore = 0.8f * 0.8f;
        ShaderPacks::FairyLightCap::Owner owner = ShaderPacks::FairyLightCap::Owner::Other;
    };
    const int fairyLightCap = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
        ShaderPacks::FairyLightCap::CVar, ShaderPacks::FairyLightCap::NoLimit);
    std::array<PointLight, ShaderPacks::FairyLightCap::MaxCandidates> candidates{};
    size_t count = 0;
    const auto* node = play.lightCtx.listHead;
    for (size_t visited = 0; node && visited < candidates.size(); ++visited, node = node->next) {
        if (!node->info || (node->info->type != LIGHT_POINT_GLOW && node->info->type != LIGHT_POINT_NOGLOW))
            continue;
        const auto& light = node->info->params.point;
        if (light.radius <= 0 || !(light.color[0] || light.color[1] || light.color[2]) || !std::isfinite(light.x) ||
            !std::isfinite(light.y) || !std::isfinite(light.z))
            continue;
        const float x = light.x - play.view.eye.x;
        const float y = light.y - play.view.eye.y;
        const float z = light.z - play.view.eye.z;
        const float radius = std::min<float>(light.radius, 1024.0f);
        candidates[count] = { { light.x, light.y, light.z, radius },
                                { light.color[0] / 255.0f, light.color[1] / 255.0f, light.color[2] / 255.0f, 0.0f },
                                 (x * x + y * y + z * z) / (radius * radius) };
        if (fairyLightCap > ShaderPacks::FairyLightCap::NoLimit)
            candidates[count].owner = ClassifyFairyLightOwner(play, node->info);
        ++count;
    }
    if (fairyLightCap > ShaderPacks::FairyLightCap::NoLimit) {
        std::array<ShaderPacks::FairyLightCap::Candidate, ShaderPacks::FairyLightCap::MaxCandidates> owners{};
        for (size_t i = 0; i < count; ++i)
            owners[i] = { candidates[i].owner, candidates[i].score };
        const auto admission = ShaderPacks::FairyLightCap::Admit(owners, count, MaxSceneLights, fairyLightCap);
        size_t retained = 0;
        for (size_t i = 0; i < count; ++i) {
            if (admission.accepted[i])
                candidates[retained++] = candidates[i];
        }
        count = retained;
    }
    const size_t visible = std::min(count, MaxSceneLights);
    size_t reserved = 0;
    if (shadowMaterials) {
        constexpr float cutoff = 0.8f * 0.8f;
        for (size_t i = 0; i < count; ++i) {
            auto& candidate = candidates[i];
            const auto& light = candidate.position;
            if (light[3] <= ShaderPacks::GeometryShadow::PointNear)
                continue;
            for (const auto& material : *shadowMaterials) {
                if (material.scene != play.sceneNum || material.tint[3] <= 0.0f)
                    continue;
                const auto& anchor = material.light;
                const float x = light[0] - anchor[0], y = light[1] - anchor[1], z = light[2] - anchor[2];
                const float score = (x * x + y * y + z * z) / (anchor[3] * anchor[3]);
                candidate.shadowScore = std::min(candidate.shadowScore, score);
            }
        }
        const auto selectedEnd = candidates.begin() + std::min(visible, pointLightCapacity);
        std::partial_sort(candidates.begin(), selectedEnd, candidates.begin() + count,
                          [](const auto& a, const auto& b) { return a.shadowScore < b.shadowScore; });
        reserved = std::find_if(candidates.begin(), selectedEnd,
                                [cutoff](const auto& light) { return light.shadowScore >= cutoff; }) -
                   candidates.begin();
    }
    std::partial_sort(candidates.begin() + reserved, candidates.begin() + visible, candidates.begin() + count,
                      [](const auto& a, const auto& b) { return a.score < b.score; });
    if (fairyLightCap > ShaderPacks::FairyLightCap::NoLimit) {
        // A reserva de sombras e a distância continuam escolhendo as outras luzes. Reponha só a Navi omitida.
        for (size_t i = visible; i < count; ++i) {
            if (candidates[i].owner != ShaderPacks::FairyLightCap::Owner::Navi)
                continue;
            // Nunca recua para dentro do prefixo reservado às sombras.
            size_t replacement = visible;
            while (replacement > reserved &&
                   candidates[replacement - 1].owner == ShaderPacks::FairyLightCap::Owner::Navi)
                --replacement;
            if (replacement <= reserved)
                break;
            --replacement;
            std::swap(candidates[replacement], candidates[i]);
        }
    }
    for (size_t i = 0; i < visible; ++i) {
        scene.lightPositions[i] = candidates[i].position;
        scene.lightColors[i] = candidates[i].color;
    }
    scene.counts[0] = static_cast<float>(visible);
    scene.counts[2] = static_cast<float>(reserved);

    const auto* collision = play.colCtx.colHeader;
    if (!collision || !collision->waterBoxes)
        return;
    // Cenas com mais de MaxWaterBoxes caixas (Zora's River tem 22) publicam as mais próximas do olho, não as
    // primeiras do índice: senão a água sob a câmera pode ficar sem sombreamento do pack.
    struct WaterCandidate {
        float score;
        uint16_t index;
    };
    std::array<WaterCandidate, 128> waterCandidates{};
    size_t waterCandidateCount = 0;
    for (size_t i = 0; i < collision->numWaterBoxes && waterCandidateCount < waterCandidates.size(); ++i) {
        const auto& box = collision->waterBoxes[i];
        if ((box.room != -1 && box.room != play.roomCtx.curRoom.num) || box.notSwimmable || box.xLength <= 0 ||
            box.zLength <= 0)
            continue;
        const float dx = std::max({ float(box.xMin) - play.view.eye.x, 0.0f,
                                    play.view.eye.x - float(box.xMin + box.xLength) });
        const float dz = std::max({ float(box.zMin) - play.view.eye.z, 0.0f,
                                    play.view.eye.z - float(box.zMin + box.zLength) });
        const float dy = play.view.eye.y - float(box.ySurface);
        waterCandidates[waterCandidateCount++] = { dx * dx + dy * dy + dz * dz, static_cast<uint16_t>(i) };
    }
    const size_t waterCount = std::min(waterCandidateCount, MaxWaterBoxes);
    std::partial_sort(waterCandidates.begin(), waterCandidates.begin() + waterCount,
                      waterCandidates.begin() + waterCandidateCount, [](const auto& a, const auto& b) {
                          return a.score < b.score || (a.score == b.score && a.index < b.index);
                      });
    for (size_t i = 0; i < waterCount; ++i) {
        const auto& box = collision->waterBoxes[waterCandidates[i].index];
        const float x = static_cast<float>(box.xMin), z = static_cast<float>(box.zMin);
        scene.waterBounds[i] = { x, z, x + box.xLength, z + box.zLength };
        scene.waterPlanes[i] = { static_cast<float>(box.ySurface), 0.0f, 0.0f, 0.0f };
    }
    scene.counts[1] = static_cast<float>(waterCount);
}

struct WorldPass;

struct WorldSnapshot {
    // A referência fraca rejeita trocas do pack sem reter 32 packs antigos na GPU.
    std::weak_ptr<WorldPass> pass;
    uint16_t sceneId = 0;
    bool waterDisplace = false;
    float waterTime = -1.0f;
    uint32_t ticket = 0;
    const void* projectionKey = nullptr;
    const void* viewKey = nullptr;
    CameraConstants camera{};
    SceneConstants scene{};
    std::shared_ptr<const SunShadowMesh> shadowMesh;
    std::array<float, 3> geometryFocus{};
    bool hasGeometryFocus = false;
    int fogSunTest = 0;
    unsigned sunCascades = 3;
    float sunShadowDistance = 4000.0f;
    unsigned sunShadowTile = ShaderPacks::SolarCascades::TileSize;
    SceneVector irisFog{};
    float irisFogEnd = 0.0f;
    FogRamp irisFogRamp{};
    SkyFacts irisSky{ SkyOpennessOpen, 0.0f, 0.0f, 0.0f };
};

// Display lists carry a ticket. Matrix addresses are only lookup keys during replay.
std::mutex worldSnapshotsMutex;
std::array<WorldSnapshot, 32> worldSnapshots{};
uint32_t worldTicket = 0;

uint32_t QueueWorldSnapshot(GraphicsContext* gfxCtx, const std::shared_ptr<WorldPass>& pass,
                            bool captureCamera, bool captureScene, bool captureShadow,
                            bool captureGeometryShadow, bool lightsFog,
                            const ShaderPacks::TransmissionMaterials* shadowMaterials, size_t pointLightCapacity) {
    auto* play = gPlayState;
    if (!play || play->state.gfxCtx != gfxCtx)
        return 0;
    const auto& view = play->view;
    WorldSnapshot snapshot{};
    snapshot.pass = pass;
    snapshot.sceneId = play->sceneNum;
    if (captureCamera) {
        if (!view.projectionPtr || !view.viewingPtr)
            return 0;
        snapshot.projectionKey = view.projectionPtr;
        snapshot.viewKey = view.viewingPtr;
        MtxF projection, viewing;
        Matrix_MtxToMtxF(view.projectionPtr, &projection);
        Matrix_MtxToMtxF(view.viewingPtr, &viewing);
        std::memcpy(snapshot.camera.projection, projection.mf, sizeof(projection.mf));
        std::memcpy(snapshot.camera.view, viewing.mf, sizeof(viewing.mf));
        snapshot.camera.info[0] = view.zNear;
        snapshot.camera.info[1] = view.zFar;
    }
    if (captureScene || captureGeometryShadow || shadowMaterials) {
        CaptureScene(*play, snapshot.scene, shadowMaterials, pointLightCapacity);
    }
    {
        const auto& light = play->lightCtx;
        const float fogFar = static_cast<float>(light.fogFar);
        snapshot.irisFog = { light.fogColor[0] / 255.0f, light.fogColor[1] / 255.0f, light.fogColor[2] / 255.0f,
                             light.worldFog ? light.fogStart : Environment_LegacyFogStart(light.fogNear, fogFar) };
        snapshot.irisFogEnd = light.worldFog ? light.fogEnd : fogFar;
        snapshot.irisFogRamp = NativeFogRamp(light);
        snapshot.irisSky = NativeSkyFacts(*play);
    }
    const auto waterCVars = Ship::Context::GetRawInstance()->GetConsoleVariables();
    const int waterScene = waterCVars->GetInteger("gDeveloperTools.WaterDisplaceScene", -1);
    snapshot.waterDisplace = waterCVars->GetInteger("gDeveloperTools.WaterDisplace", 0) != 0 &&
                            (waterScene < 0 || waterScene == play->sceneNum);
    snapshot.waterTime = waterCVars->GetInteger("gDeveloperTools.WaterDisplaceFreezeTime", 0) != 0 ?
                        waterCVars->GetFloat("gDeveloperTools.WaterDisplaceTime", 0.0f) : -1.0f;
    if (!std::isfinite(snapshot.waterTime)) snapshot.waterTime = -1.0f;
    if (snapshot.waterDisplace) {
        // Fallback se este chamador nao pediu SceneData. Sem lista de cenas/materiais.
        if (!captureScene && !captureGeometryShadow && !shadowMaterials)
            CaptureScene(*play, snapshot.scene, nullptr, 0);
        const float x = std::floor(play->view.eye.x / Fast::WaterDisplace::Cell) * Fast::WaterDisplace::Cell;
        const float z = std::floor(play->view.eye.z / Fast::WaterDisplace::Cell) * Fast::WaterDisplace::Cell;
        for (auto& plane : snapshot.scene.waterPlanes) {
            // y=capacidade M12, zw=centro da janela; tamanho do cbuffer preservado.
            plane[1] = 1.0f; plane[2] = x; plane[3] = z;
        }
    }
    if (lightsFog) {
        snapshot.fogSunTest =
            Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_DEVELOPER_TOOLS("FogSunTest"), 0);
    }
    if (captureShadow)
        snapshot.shadowMesh = ShaderPacks::CaptureSunShadowMesh(*play);
    if (captureGeometryShadow && UsesSolarGeometry(*play, lightsFog)) {
        snapshot.sunCascades = static_cast<unsigned>(std::clamp(
            Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_DEVELOPER_TOOLS("SunCascades"), 3),
            1, 3));
        // Alcance da sombra solar (GFX-UNIFY-001, 11/11): com o far de Hyrule Field (12800 u) a 1ª cascata cobria
        // ~3500 u e o Link ocupava 3 texels. Com 4000 u ela cobre ~1100 u (~1,1 u por texel).
        snapshot.sunShadowDistance = static_cast<float>(std::clamp(
            Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_DEVELOPER_TOOLS("SunShadowDistance"),
                                                                               4000),
            500, 20000));
        // Settings → Graphics → Shadow Map Resolution: lado de cada cascata (512, 1024, 2048 ou 4096).
        snapshot.sunShadowTile = ShaderPacks::SolarCascades::ClampTileSize(
            Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_SETTING("ShadowMapResolution"),
                                                                               ShaderPacks::SolarCascades::TileSize));
        if (const auto* player = play->actorCtx.actorLists[ACTORCAT_PLAYER].head) {
            const auto& position = player->world.pos;
            snapshot.geometryFocus = { position.x, position.y, position.z };
            snapshot.hasGeometryFocus = true;
        }
    }

    const std::lock_guard lock(worldSnapshotsMutex);
    if (++worldTicket == 0)
        ++worldTicket;
    snapshot.ticket = worldTicket;
    worldSnapshots[worldTicket % worldSnapshots.size()] = snapshot;
    return worldTicket;
}

bool ReadWorldSnapshot(const Fast::Interpreter& interpreter, uint32_t ticket, WorldSnapshot& snapshot) {
    if (!ticket)
        return false;
    {
        const std::lock_guard lock(worldSnapshotsMutex);
        snapshot = worldSnapshots[ticket % worldSnapshots.size()];
    }
    if (snapshot.ticket != ticket)
        return false;

    if (snapshot.projectionKey) {
        auto& camera = snapshot.camera;
        const bool projected = Fast::TryReadInterpolatedMatrix(interpreter, snapshot.projectionKey, camera.projection);
        const bool viewed = Fast::TryReadInterpolatedMatrix(interpreter, snapshot.viewKey, camera.view);
        camera.info[2] = interpreter.mInterpolationT;
        camera.info[3] = (viewed ? 1.0f : 0.0f) + (projected ? 2.0f : 0.0f);
    }
    snapshot.scene.info[2] = std::max(0.0f, (snapshot.scene.info[2] - 1.0f + interpreter.mInterpolationT) / 20.0f);
    // Override de prova A/B: congela TODO o tempo do pack, sem tocar o jogo.
    if (snapshot.waterTime >= 0.0f)
        snapshot.scene.info[2] = snapshot.waterTime;
    return true;
}

struct PixelPass {
    ComPtr<ID3D11PixelShader> shader;
    unsigned divisor;
    std::array<int, MaxPassInputs> inputs;
    unsigned width = 0, height = 0;
    bool persistent = false;
    bool fogOver = false;
    std::string name; // file no manifesto; nao usar nome gerado pelo compilador.
    unsigned outputs = 1;
    UINT inputSlots = LegacyPassInputs; // 16 quando o passe usa t4..t15 (manifesto v3).
    ComPtr<ID3D11VertexShader> vertexShader; // v3: vertex shader do pack; nulo usa o de tela cheia.
};

struct PassTarget {
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11ShaderResourceView> view;
    UINT width;
    UINT height;
};

bool CreatePassTarget(ID3D11Device& device, DXGI_FORMAT format, PassTarget& target) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = target.width;
    desc.Height = target.height;
    desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Format = format;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    return SUCCEEDED(device.CreateTexture2D(&desc, nullptr, texture.GetAddressOf())) &&
           SUCCEEDED(device.CreateRenderTargetView(texture.Get(), nullptr, target.target.GetAddressOf())) &&
           SUCCEEDED(device.CreateShaderResourceView(texture.Get(), nullptr, target.view.GetAddressOf()));
}

struct WorldPass {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11SamplerState> wrapSampler; // s1
    ComPtr<ID3D11Buffer> cameraBuffer;
    ComPtr<ID3D11Buffer> sceneBuffer;
    ComPtr<ID3D11Buffer> sunShadowBuffer;
    ComPtr<ID3D11Buffer> transmissionBuffer;
    ComPtr<ID3D11Buffer> pointShadowBuffer;
    ComPtr<ID3D11Buffer> weatherBuffer;
    ComPtr<ID3D11Buffer> decalsBuffer;
    std::shared_ptr<const ShaderPacks::TransmissionMaterials> transmissionMaterials;
    TransmissionConstants transmissionConstants{};
    std::array<PointShadowConstants, MaxPointLights> pointShadowConstants{};
    ComPtr<ID3D11ShaderResourceView> sunShadowView;
    std::unique_ptr<ShaderPacks::GeometryShadow> geometryShadow;
    std::unique_ptr<ShaderPacks::GeometryShadow> pointShadow;
    std::shared_ptr<const SunShadowMesh> shadowMesh;
    ShaderPacks::SolarCascades::Constants shadowConstants{};
    std::vector<PixelPass> passes;
    using GpuTimer = ShaderPacks::PackGpu::DX11Timer;
    std::unique_ptr<GpuTimer> gpuTimings; // nullptr com CVar 0: nenhum recurso/query M03.

    void SyncGpuTimings(Fast::GfxRenderingAPIDX11& api) {
        const bool enabled = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
                                 "gDeveloperTools.PackGpuTimings", 0) != 0;
        if (!enabled) {
            gpuTimings.reset();
        } else if (!gpuTimings) {
            std::vector<std::string> names;
            for (const auto& pass : passes)
                names.push_back(pass.name);
            gpuTimings = std::make_unique<GpuTimer>(api, name, names);
        }
    }
    void GpuMark(size_t section, bool end) {
        if (gpuTimings)
            gpuTimings->Mark(section, end);
    }
    void FinishGpuTimings(bool complete) {
        if (gpuTimings)
            gpuTimings->Finish(complete);
    }
    std::vector<PassTarget> targets;
    std::vector<PassTarget> historyTargets;
    // Saídas 1..n-1 de cada passe (RFC 0035); a saída 0 fica em targets/historyTargets.
    std::vector<std::vector<PassTarget>> extraTargets, extraHistoryTargets;
    ComPtr<ID3D11Buffer> irisBuffer;
    CameraConstants previousIrisCamera{};
    float irisAspect = 1.0f, previousIrisAspect = 1.0f;
    bool irisInitialized = false;
    uint16_t irisScene = 0;
    double irisTime = 0.0;
    uint32_t irisFrame = 0;
    double irisClock = 0.0;
    SceneVector irisFog{};
    float irisFogEnd = 0.0f;
    FogRamp irisFogRamp{};
    FogRamp smoothedFogRamp{};
    SkyFacts irisSky{ SkyOpennessOpen, 0.0f, 0.0f, 0.0f };
    float smoothedSkyOpenness = SkyOpennessUnset;
    ComPtr<ID3D11ShaderResourceView> noiseView;
    ComPtr<ID3D11ShaderResourceView> oneView; // "one": 1x1 (1, 1, 1, 1).
    std::array<ComPtr<ID3D11ShaderResourceView>, ShaderPacks::MaxPackTextures> packTextureViews; // "texture:nome"
    ShaderPacks::PackHistory history;
    // Guarda somente identidade/estado da transação; o snapshot continua no ticket.
    // Cada replay do interpretador pode preparar o mesmo ticket novamente.
    const Fast::Interpreter* frameInterpreter = nullptr;
    uint64_t frameReplay = 0;
    uint32_t frameTicket = 0;
    bool compositionPending = false;
    bool hasFog = false;
    bool fogLayerPrepared = false;
    PassTarget fogLayer{};
    std::array<ComPtr<ID3D11PixelShader>, FogLayerModeCount> fogComposite;
    int lastFogLayerMode = -1;
    ComPtr<ID3D11BlendState> fogBlend;
    ComPtr<ID3D11DepthStencilState> fogDepthState;
    ID3D11RenderTargetView* frameTarget = nullptr; // Só identidade; não reter o backbuffer entre quadros.
    ID3D11DepthStencilView* frameDepth = nullptr;
    bool hasHistory = false;
    ComPtr<ID3D11Buffer> historyBuffer;
    bool usesTemporal = false, temporalInitialized = false;
    bool reportedTemporalFailure = false;
    float temporalAspect = 1.0f, previousTemporalAspect = 1.0f;
    float previousSceneTime = 0.0f, previousRoom = 0.0f;
    uint32_t temporalSerial = 0;
    int previousTemporalReset = 0;
    CameraConstants previousTemporalCamera{};
    ComPtr<ID3D11Buffer> temporalBuffer;
    ComPtr<ID3D11Texture2D> temporalColor, temporalDepth;
    ComPtr<ID3D11ShaderResourceView> temporalColorView, temporalDepthView;

    bool PrepareTemporal(ID3D11ShaderResourceView* currentColor, ID3D11ShaderResourceView* currentDepth) {
        if (!currentColor || !currentDepth) { temporalInitialized = false; return false; }
        ComPtr<ID3D11Resource> colorResource, depthResource;
        ComPtr<ID3D11Texture2D> colorTexture, depthTexture;
        currentColor->GetResource(colorResource.GetAddressOf());
        currentDepth->GetResource(depthResource.GetAddressOf());
        if (FAILED(colorResource.As(&colorTexture)) || FAILED(depthResource.As(&depthTexture))) {
            temporalInitialized = false; return false;
        }
        D3D11_TEXTURE2D_DESC colorDesc{}, depthDesc{}, oldColor{}, oldDepth{};
        colorTexture->GetDesc(&colorDesc); depthTexture->GetDesc(&depthDesc);
        if (colorDesc.SampleDesc.Count != 1 || depthDesc.SampleDesc.Count != 1 ||
            colorDesc.Width != depthDesc.Width || colorDesc.Height != depthDesc.Height ||
            colorDesc.ArraySize != 1 || depthDesc.ArraySize != 1 || colorDesc.MipLevels != 1 || depthDesc.MipLevels != 1) {
            temporalInitialized = false; return false;
        }
        if (temporalColor) temporalColor->GetDesc(&oldColor);
        if (temporalDepth) temporalDepth->GetDesc(&oldDepth);
        if (temporalColor && temporalDepth && oldColor.Width == colorDesc.Width && oldColor.Height == colorDesc.Height &&
            oldColor.Format == colorDesc.Format && oldDepth.Format == depthDesc.Format) return true;
        temporalInitialized = false;
        colorDesc.BindFlags = depthDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        colorDesc.Usage = depthDesc.Usage = D3D11_USAGE_DEFAULT;
        colorDesc.CPUAccessFlags = depthDesc.CPUAccessFlags = 0;
        colorDesc.MiscFlags = depthDesc.MiscFlags = 0;
        D3D11_SHADER_RESOURCE_VIEW_DESC colorView{}, depthView{};
        currentColor->GetDesc(&colorView); currentDepth->GetDesc(&depthView);
        ComPtr<ID3D11Texture2D> nextColor, nextDepth;
        ComPtr<ID3D11ShaderResourceView> nextColorView, nextDepthView;
        if (FAILED(device->CreateTexture2D(&colorDesc, nullptr, nextColor.GetAddressOf())) ||
            FAILED(device->CreateTexture2D(&depthDesc, nullptr, nextDepth.GetAddressOf())) ||
            FAILED(device->CreateShaderResourceView(nextColor.Get(), &colorView, nextColorView.GetAddressOf())) ||
            FAILED(device->CreateShaderResourceView(nextDepth.Get(), &depthView, nextDepthView.GetAddressOf()))) return false;
        temporalColor = std::move(nextColor); temporalDepth = std::move(nextDepth);
        temporalColorView = std::move(nextColorView); temporalDepthView = std::move(nextDepthView);
        return true;
    }

    PassTarget& Output(size_t i, unsigned k = 0) {
        const bool swapped = passes[i].persistent && history.WriteIndex() == 1;
        if (k)
            return swapped ? extraHistoryTargets[i][k - 1] : extraTargets[i][k - 1];
        return swapped ? historyTargets[i] : targets[i];
    }
    PassTarget& Previous(size_t i, unsigned k = 0) {
        const bool swapped = history.ReadIndex() == 1;
        if (k)
            return swapped ? extraHistoryTargets[i][k - 1] : extraTargets[i][k - 1];
        return swapped ? historyTargets[i] : targets[i];
    }
    ComPtr<ID3D11Texture2D> scene;
    ComPtr<ID3D11ShaderResourceView> sceneView;
    D3D11_TEXTURE2D_DESC sceneDesc{};
    bool usesDepth = false;
    bool usesMaterial = false;
    ComPtr<ID3D11PixelShader> resolveMaterial;
    std::array<PassTarget, 2> resolvedMaterial{};
    bool reportedMaterialTargetFailure = false;
    bool usesSunShadow = false;
    bool lightsFog = false;
    // Escurecimento de interior (céu automático): multiplica a saída final do pack por um ganho via blend.
    ComPtr<ID3D11PixelShader> sceneGainShader;
    ComPtr<ID3D11BlendState> sceneGainBlend;
    ComPtr<ID3D11PixelShader> resolveDepth;
    ComPtr<ID3D11DepthStencilView> depthAttachment;
    ComPtr<ID3D11ShaderResourceView> depthSource;
    PassTarget resolvedDepth{};
    std::string name;
    std::chrono::steady_clock::time_point activationStart, activatedAt;
    std::atomic<bool> firstDrawSubmitted{ false };
    std::atomic<bool> firstFrameLogged{ false };
    std::atomic<bool> firstPackFrameLogged{ false };
    bool usesWaterRipple = false;
    ComPtr<ID3D11Texture2D> rippleTexture;
    ComPtr<ID3D11ShaderResourceView> rippleView;
    ComPtr<ID3D11Buffer> rippleBuffer;
    std::vector<uint16_t> rippleTexels;
    uint64_t rippleVersion = 0;
    bool drawn = false;
    bool reportedInterpolatedCamera = false;
    bool reportedTargetFailure = false;
    uint64_t requiredTargetBytes = 0;
    bool reportedDepthFailure = false;
    int32_t reportedGeometryShadowScene = -1;
    int32_t reportedGeometryOverflowScene = -1;
    int32_t reportedGeometryFailureScene = -1;
    unsigned reportedShadowTileRequest = 0; // Último tamanho recusado pela GPU; não tenta de novo a cada quadro.
    bool reportedTransmission = false;
    size_t pointLightCapacity = 0;
    size_t sunCascadeCapacity = 0;
    size_t reportedPointCount = 0;

    void PrepareTransmission(uint16_t sceneId) {
        transmissionConstants = {};
        size_t materialCount = 0, triangleCount = 0;
        const auto geometry = Fast::ReadWorldTransmission();
        for (const auto& material : *transmissionMaterials) {
            if (material.scene != sceneId)
                continue;
            for (const auto& triangle : geometry) {
                if (triangle.texture != material.texture)
                    continue;
                const size_t offset = TransmissionTriangleOffset + 3 * triangleCount++;
                for (size_t i = 0; i < 3; ++i) {
                    const auto& vertex = triangle.vertices[i];
                    transmissionConstants[offset + i] = { vertex[0], vertex[1], vertex[2],
                                                          static_cast<float>(materialCount) };
                }
            }
            transmissionConstants[1 + 2 * materialCount] = material.tint;
            transmissionConstants[2 + 2 * materialCount] = material.light;
            ++materialCount;
        }
        transmissionConstants[0] = { static_cast<float>(materialCount), static_cast<float>(triangleCount), 0, 0 };
        if (triangleCount && !reportedTransmission) {
            const auto& vertex = transmissionConstants[TransmissionTriangleOffset];
            SPDLOG_INFO(
                "Shader pack DX11 transmissão: {} (cena {}, {} triângulos, primeiro vértice {:.1f}, {:.1f}, {:.1f})",
                name, sceneId, triangleCount, vertex[0], vertex[1], vertex[2]);
            reportedTransmission = true;
        }
    }

    void PreparePointShadow(ID3D11DeviceContext& ctx, const SceneConstants& world) {
        pointShadowConstants = {};
        const auto count = std::min(pointLightCapacity, static_cast<size_t>(world.counts[2]));
        if (transmissionConstants[0][1] < 1.0f || !count) {
            reportedPointCount = 0;
            return;
        }
        const std::span sources(world.lightPositions.data(), count);
        const auto geometry = Fast::ReadWorldGeometry();
        if (!pointShadow->DrawPoint(ctx, geometry, Fast::ReadWorldCutouts(), sources))
            return;
        using Shadow = ShaderPacks::GeometryShadow;
        for (size_t i = 0; i < count; ++i) {
            pointShadowConstants[i] = {
                sources[i], { static_cast<float>(i + 1), Shadow::PointNear, Shadow::PointFaceSize, Shadow::PointScale }
            };
        }
        if (reportedPointCount != count) {
            SPDLOG_INFO("Shader pack DX11 sombras pontuais: {} ({} fontes, {} faces, {} triângulos opacos)", name,
                        count, 6 * count, geometry.size() / 3);
            reportedPointCount = count;
        }
    }

    void PrepareSunShadow(const std::shared_ptr<const SunShadowMesh>& mesh) {
        if (shadowMesh == mesh)
            return;
        shadowMesh = mesh;
        sunShadowView.Reset();
        shadowConstants = {};
        if (!mesh)
            return;

        auto map = ShaderPacks::BakeSunShadow(*mesh);
        if (map.depth.empty()) {
            SPDLOG_WARN("Shader pack DX11 sem mapa de sombras estáticas dentro do orçamento: {}", name);
            return;
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = SunShadowMap::Size;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32_FLOAT;
        desc.SampleDesc = { 1, 0 };
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA pixels{ map.depth.data(), SunShadowMap::Size * sizeof(float), 0 };
        ComPtr<ID3D11Texture2D> texture;
        if (FAILED(device->CreateTexture2D(&desc, &pixels, texture.GetAddressOf())) ||
            FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, sunShadowView.GetAddressOf()))) {
            SPDLOG_WARN("Shader pack DX11 não criou o mapa de sombras estáticas: {}", name);
            return;
        }
        std::copy(map.constants.begin(), map.constants.end(), shadowConstants.begin());
        shadowConstants[14] = { 1, 1, 0, 0 };
        SPDLOG_INFO("Shader pack DX11 mapa de sombras estáticas: {} ({} triângulos, {}x{})", name,
                    mesh->triangles.size(), SunShadowMap::Size, SunShadowMap::Size);
    }

    bool PrepareTargets(D3D11_TEXTURE2D_DESC desc) {
        if (scene && sceneDesc.Width == desc.Width && sceneDesc.Height == desc.Height &&
            sceneDesc.Format == desc.Format)
            return true;

        std::vector<PassTarget> nextTargets, nextHistoryTargets;
        std::vector<std::vector<PassTarget>> nextExtra, nextExtraHistory;
        // Also reserve room for resolved depth if MSAA is enabled at this resolution later.
        uint64_t bytes = static_cast<uint64_t>(desc.Width) * desc.Height * (usesDepth ? 8 : 4);
        if (usesTemporal)
            bytes += static_cast<uint64_t>(desc.Width) * desc.Height * 16; // Cor FP16 + depth ate 64 bits.
        if (hasFog)
            bytes += static_cast<uint64_t>(desc.Width) * desc.Height * 8;
        // O mapa em cascata tem alocação e recusa próprias (ResizeSolar mantém o tamanho anterior sem memória).
        // Somado aqui, 3 x 4096² em float (201 MB) estourava os 256 MB e desligava o pack inteiro ao recriar os
        // alvos, por exemplo ao redimensionar a janela (playtest 12/11, Shadow Map Resolution 4096 em 1793x1009).
        if (usesSunShadow && !geometryShadow)
            bytes += static_cast<uint64_t>(SunShadowMap::Size) * SunShadowMap::Size * sizeof(float);
        // Histórico pequeno (exposição, médias) fica em FP32: em FP16 o ulp de um log2 entre 4 e 8
        // é 0,0039, e a adaptação lenta (passo < meio ulp por quadro) trava antes do alvo.
        // Histórico de tela cheia continua FP16 por custo de banda.
        std::vector<DXGI_FORMAT> formats;
        for (size_t i = 0; i + 1 < passes.size(); ++i) {
            const auto divisor = passes[i].divisor;
            const auto width = passes[i].width ? passes[i].width : (desc.Width + divisor - 1) / divisor;
            const auto height = passes[i].height ? passes[i].height : (desc.Height + divisor - 1) / divisor;
            const bool precise = passes[i].persistent && static_cast<uint64_t>(width) * height <= 64u * 64u;
            formats.push_back(precise ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT);
            bytes += static_cast<uint64_t>(width) * height * (precise ? 16 : 8) * (passes[i].persistent ? 2 : 1) *
                     passes[i].outputs;
            nextTargets.push_back({ {}, {}, width, height });
            nextHistoryTargets.push_back(passes[i].persistent ? PassTarget{ {}, {}, width, height } : PassTarget{});
            nextExtra.emplace_back(passes[i].outputs - 1, PassTarget{ {}, {}, width, height });
            nextExtraHistory.emplace_back(passes[i].persistent ? passes[i].outputs - 1 : 0,
                                          PassTarget{ {}, {}, width, height });
        }
        // Teto de memória dos alvos do pack (MB). Um grafo Iris grande com histórico (Complementary: 22 alvos FP16
        // em 1920x1009, ~360 MB) passa dos 256 MB de antes; o teto é a proteção contra falta de VRAM.
        requiredTargetBytes = bytes;
        const auto budget = static_cast<uint64_t>(std::clamp(
            Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_DEVELOPER_TOOLS("PackTargetBudget"),
                                                                              512),
            64, 4096));
        if (bytes > budget * 1024u * 1024u)
            return false;

        // A camada independe do formato/MSAA da cena. Recriar so ao mudar tamanho.
        PassTarget nextFog = fogLayer;
        if (hasFog && (!nextFog.target || nextFog.width != desc.Width || nextFog.height != desc.Height)) {
            nextFog = { {}, {}, desc.Width, desc.Height };
            if (!CreatePassTarget(*device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, nextFog))
                return false;
        }

        desc.SampleDesc = { 1, 0 };
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> view;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf())) ||
            FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf())))
            return false;

        for (size_t i = 0; i < nextTargets.size(); ++i) {
            if (!CreatePassTarget(*device.Get(), formats[i], nextTargets[i]))
                return false;
        }

        for (size_t i = 0; i < nextHistoryTargets.size(); ++i) {
            if (nextHistoryTargets[i].width && !CreatePassTarget(*device.Get(), formats[i], nextHistoryTargets[i])) {
                return false;
            }
            for (auto& extra : nextExtra[i])
                if (!CreatePassTarget(*device.Get(), formats[i], extra))
                    return false;
            for (auto& extra : nextExtraHistory[i])
                if (!CreatePassTarget(*device.Get(), formats[i], extra))
                    return false;
        }
        scene = std::move(texture);
        sceneView = std::move(view);
        sceneDesc = desc;
        targets = std::move(nextTargets);
        historyTargets = std::move(nextHistoryTargets);
        extraTargets = std::move(nextExtra);
        extraHistoryTargets = std::move(nextExtraHistory);
        fogLayer = std::move(nextFog);
        history.Reset();
        temporalInitialized = false;
        reportedTargetFailure = false;
        return true;
    }

    bool PrepareDepth(ID3D11DepthStencilView* attachment, const D3D11_TEXTURE2D_DESC& color) {
        if (!attachment)
            return false;
        if (depthAttachment.Get() == attachment)
            return true;

        ComPtr<ID3D11Resource> resource;
        ComPtr<ID3D11Texture2D> texture;
        attachment->GetResource(resource.GetAddressOf());
        if (FAILED(resource.As(&texture)))
            return false;

        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        if (desc.Width != color.Width || desc.Height != color.Height || desc.ArraySize != 1 || desc.MipLevels != 1 ||
            desc.SampleDesc.Count != color.SampleDesc.Count || desc.SampleDesc.Quality != color.SampleDesc.Quality ||
            !(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)) {
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
        switch (desc.Format) {
            case DXGI_FORMAT_R32G8X24_TYPELESS:
                viewDesc.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
                break;
            case DXGI_FORMAT_R24G8_TYPELESS:
                viewDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
                break;
            default:
                return false;
        }
        const bool multisampled = desc.SampleDesc.Count > 1;
        viewDesc.ViewDimension = multisampled ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
        if (!multisampled)
            viewDesc.Texture2D.MipLevels = 1;

        ComPtr<ID3D11ShaderResourceView> view;
        if (FAILED(device->CreateShaderResourceView(texture.Get(), &viewDesc, view.GetAddressOf())))
            return false;
        PassTarget resolved{ {}, {}, desc.Width, desc.Height };
        if (multisampled && usesDepth &&
            (!resolveDepth || !CreatePassTarget(*device.Get(), DXGI_FORMAT_R32_FLOAT, resolved))) {
            return false;
        }

        depthAttachment = attachment;
        depthSource = std::move(view);
        resolvedDepth = std::move(resolved);
        reportedDepthFailure = false;
        SPDLOG_INFO("Shader pack DX11 profundidade: {} ({}x{}, {} amostras, modo={})", name, desc.Width, desc.Height,
                    desc.SampleDesc.Count, multisampled ? "nearest" : "direct");
        return true;
    }

    // Textura do campo de onda em t4 e constantes em b5, fora dos quatro slots de entrada do manifesto.
    bool PrepareWaterRipple(ID3D11DeviceContext& ctx) {
        if (!rippleView) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = desc.Height = WaterRipple::Side;
            desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
            desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_BUFFER_DESC buffer{};
            buffer.ByteWidth = sizeof(WaterRipple::Frame);
            buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            const std::vector<uint16_t> zero(size_t{ WaterRipple::Side } * WaterRipple::Side * 4, 0);
            const D3D11_SUBRESOURCE_DATA initial{ zero.data(), WaterRipple::Side * 8, 0 };
            if (FAILED(device->CreateTexture2D(&desc, &initial, rippleTexture.GetAddressOf())) ||
                FAILED(device->CreateShaderResourceView(rippleTexture.Get(), nullptr, rippleView.GetAddressOf())) ||
                FAILED(device->CreateBuffer(&buffer, nullptr, rippleBuffer.GetAddressOf()))) {
                rippleView.Reset();
                usesWaterRipple = false;
                return false;
            }
        }
        WaterRipple::Frame frame{};
        if (WaterRipple::Read(rippleVersion, rippleTexels, frame)) {
            ctx.UpdateSubresource(rippleTexture.Get(), 0, nullptr, rippleTexels.data(), WaterRipple::Side * 8, 0);
        }
        ctx.UpdateSubresource(rippleBuffer.Get(), 0, nullptr, &frame, 0, 0);
        return true;
    }

    bool PrepareResources(ID3D11DeviceContext& ctx) {
        ComPtr<ID3D11RenderTargetView> target;
        ComPtr<ID3D11DepthStencilView> depth;
        ctx.OMGetRenderTargets(1, target.GetAddressOf(), depth.GetAddressOf());
        if (!target)
            return false;

        ComPtr<ID3D11Resource> resource;
        ComPtr<ID3D11Texture2D> color;
        target->GetResource(resource.GetAddressOf());
        if (FAILED(resource.As(&color)))
            return false;

        D3D11_TEXTURE2D_DESC desc{};
        color->GetDesc(&desc);
        if (desc.ArraySize != 1 || desc.MipLevels != 1)
            return false;
        if (gpuTimings)
            gpuTimings->Begin(desc);
        if (!PrepareTargets(desc)) {
            if (!reportedTargetFailure) {
                SPDLOG_WARN("Shader pack DX11 sem alvos intermediários: {} ({}x{}, {} MB pedidos; teto em "
                            "gDeveloperTools.PackTargetBudget)",
                            name, desc.Width, desc.Height, requiredTargetBytes / (1024 * 1024));
                reportedTargetFailure = true;
            }
            return false;
        }
        if (usesDepth && !PrepareDepth(depth.Get(), desc)) {
            if (!reportedDepthFailure) {
                SPDLOG_WARN("Shader pack DX11 sem profundidade acessível: {}", name);
                reportedDepthFailure = true;
            }
            return false;
        }

        if (!usesDepth && usesMaterial && resolveMaterial && desc.SampleDesc.Count == 4)
            PrepareDepth(depth.Get(), desc); // Optional input. The shaderpack can still draw without it.

        if (usesWaterRipple)
            PrepareWaterRipple(ctx);
        frameTarget = target.Get();
        frameDepth = depth.Get();
        return true;
    }

    void PrepareIris(Fast::Interpreter& interpreter, const WorldSnapshot& snapshot) {
        if (!irisBuffer)
            return;
        irisAspect = interpreter.AdjXForAspectRatio(1.0f);
        irisFog = snapshot.irisFog;
        irisFogEnd = snapshot.irisFogEnd;
        irisFogRamp = snapshot.irisFogRamp;
        if (snapshot.irisSky != irisSky)
            // Diagnóstico da luz do céu automática: uma linha por mudança dos fatos da cena (não por quadro).
            SPDLOG_INFO("Policroma céu: abertura={:.2f} indoors={:.0f} skyboxDisabled={:.0f} skyboxId={:.0f}",
                        snapshot.irisSky[0], snapshot.irisSky[1], snapshot.irisSky[2], snapshot.irisSky[3]);
        irisSky = snapshot.irisSky;
    }

    void PrepareFogLayer(ID3D11DeviceContext& ctx) {
        const float zero[4]{};
        ctx.ClearRenderTargetView(fogLayer.target.Get(), zero);
        if (resolvedDepth.target) {
            // Resolver ANTES da nevoa; Draw usa exatamente esta mesma SRV.
            ComPtr<ID3D11RenderTargetView> oldTarget;
            ComPtr<ID3D11DepthStencilView> oldAttachment;
            ComPtr<ID3D11VertexShader> oldVertex;
            ComPtr<ID3D11PixelShader> oldPixel;
            ComPtr<ID3D11InputLayout> oldLayout;
            ComPtr<ID3D11RasterizerState> oldRaster;
            ComPtr<ID3D11BlendState> oldBlend;
            ComPtr<ID3D11DepthStencilState> oldDepth;
            ComPtr<ID3D11ShaderResourceView> oldSource;
            D3D11_PRIMITIVE_TOPOLOGY topology{};
            D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
            UINT count = ARRAYSIZE(viewports), mask = 0, stencil = 0;
            float factors[4]{};
            ctx.OMGetRenderTargets(1, oldTarget.GetAddressOf(), oldAttachment.GetAddressOf());
            ctx.VSGetShader(oldVertex.GetAddressOf(), nullptr, nullptr);
            ctx.PSGetShader(oldPixel.GetAddressOf(), nullptr, nullptr);
            ctx.IAGetInputLayout(oldLayout.GetAddressOf());
            ctx.IAGetPrimitiveTopology(&topology);
            ctx.RSGetState(oldRaster.GetAddressOf());
            ctx.RSGetViewports(&count, viewports);
            ctx.OMGetBlendState(oldBlend.GetAddressOf(), factors, &mask);
            ctx.OMGetDepthStencilState(oldDepth.GetAddressOf(), &stencil);
            ctx.PSGetShaderResources(0, 1, oldSource.GetAddressOf());

            const D3D11_VIEWPORT viewport{ 0, 0, static_cast<float>(sceneDesc.Width),
                                           static_cast<float>(sceneDesc.Height), 0, 1 };
            ctx.OMSetRenderTargets(1, resolvedDepth.target.GetAddressOf(), nullptr);
            ctx.OMSetDepthStencilState(fogDepthState.Get(), 0);
            ctx.OMSetBlendState(nullptr, nullptr, ~0u);
            ctx.RSSetState(raster.Get());
            ctx.RSSetViewports(1, &viewport);
            ctx.IASetInputLayout(nullptr);
            ctx.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ctx.VSSetShader(vertex.Get(), nullptr, 0);
            ctx.PSSetShader(resolveDepth.Get(), nullptr, 0);
            ctx.PSSetShaderResources(0, 1, depthSource.GetAddressOf());
            ctx.Draw(3, 0);
            ID3D11ShaderResourceView* unbound = nullptr;
            ctx.PSSetShaderResources(0, 1, &unbound);
            ctx.OMSetRenderTargets(1, oldTarget.GetAddressOf(), oldAttachment.Get());
            ctx.PSSetShaderResources(0, 1, oldSource.GetAddressOf());
            ctx.PSSetShader(oldPixel.Get(), nullptr, 0);
            ctx.VSSetShader(oldVertex.Get(), nullptr, 0);
            ctx.IASetInputLayout(oldLayout.Get());
            ctx.IASetPrimitiveTopology(topology);
            ctx.RSSetState(oldRaster.Get());
            ctx.RSSetViewports(count, viewports);
            ctx.OMSetBlendState(oldBlend.Get(), factors, mask);
            ctx.OMSetDepthStencilState(oldDepth.Get(), stencil);
        }
        fogLayerPrepared = true;
    }

    Fast::KokiriLabPbrMaterialViews ResolveMaterial(ID3D11DeviceContext& ctx,
                                                   Fast::KokiriLabPbrMaterialViews material,
                                                   ID3D11DepthStencilView* attachment,
                                                   const D3D11_TEXTURE2D_DESC& desc) {
        if (!material.normal || !material.specular || desc.SampleDesc.Count == 1)
            return material;
        if (!resolveMaterial || desc.SampleDesc.Count != 4 || !depthSource || depthAttachment.Get() != attachment)
            return {};

        if (resolvedMaterial[0].width != desc.Width || resolvedMaterial[0].height != desc.Height) {
            const auto target = PassTarget{{}, {}, desc.Width, desc.Height};
            resolvedMaterial = {target, target};
            if (!CreatePassTarget(*device.Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, resolvedMaterial[0]) ||
                !CreatePassTarget(*device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, resolvedMaterial[1])) {
                for (auto& failed : resolvedMaterial) {
                    failed.view.Reset();
                    failed.target.Reset();
                }
                if (!reportedMaterialTargetFailure) {
                    SPDLOG_WARN("Shader pack DX11 sem alvos para resolver material MSAA: {}", name);
                    reportedMaterialTargetFailure = true;
                }
            }
        }
        if (!resolvedMaterial[0].target || !resolvedMaterial[1].target)
            return {};

        const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0, 1};
        ID3D11RenderTargetView* outputs[] = {resolvedMaterial[0].target.Get(), resolvedMaterial[1].target.Get()};
        ID3D11ShaderResourceView* inputs[] = {depthSource.Get(), material.normal, material.specular};
        ctx.OMSetRenderTargets(ARRAYSIZE(outputs), outputs, nullptr);
        ctx.RSSetViewports(1, &viewport);
        ctx.PSSetShader(resolveMaterial.Get(), nullptr, 0);
        ctx.PSSetShaderResources(0, ARRAYSIZE(inputs), inputs);
        ctx.Draw(3, 0);
        ID3D11ShaderResourceView* empty[ARRAYSIZE(inputs)]{};
        ctx.PSSetShaderResources(0, ARRAYSIZE(empty), empty);
        return {resolvedMaterial[0].view.Get(), resolvedMaterial[1].view.Get()};
    }

    bool Draw(ID3D11DeviceContext& ctx, const CameraConstants* camera, const SceneConstants* world, uint16_t sceneId) {
        ComPtr<ID3D11RenderTargetView> target;
        ComPtr<ID3D11DepthStencilView> depth;
        ctx.OMGetRenderTargets(1, target.GetAddressOf(), depth.GetAddressOf());
        if (!target)
            return false;
        ComPtr<ID3D11Resource> resource;
        ComPtr<ID3D11Texture2D> color;
        target->GetResource(resource.GetAddressOf());
        if (FAILED(resource.As(&color)))
            return false;
        D3D11_TEXTURE2D_DESC desc{};
        color->GetDesc(&desc);
        // A preparação alocou recursos sem capturar cor. A névoa restaurou o
        // framebuffer/DSV; uma troca de alvo invalida a composição.
        if (target.Get() != frameTarget || depth.Get() != frameDepth ||
            desc.ArraySize != 1 || desc.MipLevels != 1 || !scene || sceneDesc.Width != desc.Width ||
            sceneDesc.Height != desc.Height || sceneDesc.Format != desc.Format ||
            (usesDepth && depthAttachment.Get() != depth.Get()))
            return false;

        const bool historyEnabled = hasHistory && Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
                                                      CVAR_DEVELOPER_TOOLS("PackHistory"), 1);
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        history.Begin(sceneId, desc.Width, desc.Height, historyEnabled, seconds);
        if (hasHistory && !history.Valid()) {
            const float zero[4]{};
            for (size_t i = 0; i < historyTargets.size(); ++i) {
                if (!passes[i].persistent)
                    continue;
                ctx.ClearRenderTargetView(targets[i].target.Get(), zero);
                ctx.ClearRenderTargetView(historyTargets[i].target.Get(), zero);
                for (auto& extra : extraTargets[i])
                    ctx.ClearRenderTargetView(extra.target.Get(), zero);
                for (auto& extra : extraHistoryTargets[i])
                    ctx.ClearRenderTargetView(extra.target.Get(), zero);
            }
        }

        ComPtr<ID3D11Buffer> oldHistoryBuffer;
        if (historyBuffer) {
            const SceneVector frame{ history.Delta(), history.Valid() ? 1.0f : 0.0f, 0.0f, 0.0f };
            ctx.PSGetConstantBuffers(8, 1, oldHistoryBuffer.GetAddressOf());
            ctx.UpdateSubresource(historyBuffer.Get(), 0, nullptr, frame.data(), 0, 0);
            ctx.PSSetConstantBuffers(8, 1, historyBuffer.GetAddressOf());
        }
        ComPtr<ID3D11VertexShader> oldVertex;
        ComPtr<ID3D11PixelShader> oldPixel;
        ComPtr<ID3D11InputLayout> oldLayout;
        std::array<ID3D11ShaderResourceView*, SavedSourceSlots> oldSources{};
        ComPtr<ID3D11SamplerState> oldSampler, oldWrapSampler;
        ComPtr<ID3D11Buffer> oldCameraBuffer;
        ComPtr<ID3D11Buffer> oldSceneBuffer;
        ComPtr<ID3D11Buffer> oldSunShadowBuffer;
        ComPtr<ID3D11Buffer> oldTransmissionBuffer;
        ComPtr<ID3D11Buffer> oldPointShadowBuffer;
        ComPtr<ID3D11Buffer> oldWeatherBuffer;
        ComPtr<ID3D11Buffer> oldDecalsBuffer;
        ComPtr<ID3D11RasterizerState> oldRaster;
        ComPtr<ID3D11BlendState> oldBlend;
        D3D11_PRIMITIVE_TOPOLOGY oldTopology{};
        D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        UINT viewportCount = ARRAYSIZE(oldViewports), sampleMask = 0;
        float blendFactor[4]{};
        ctx.VSGetShader(oldVertex.GetAddressOf(), nullptr, nullptr);
        ctx.PSGetShader(oldPixel.GetAddressOf(), nullptr, nullptr);
        ctx.IAGetInputLayout(oldLayout.GetAddressOf());
        ctx.IAGetPrimitiveTopology(&oldTopology);
        ctx.PSGetShaderResources(0, static_cast<UINT>(oldSources.size()), oldSources.data());
        ctx.PSGetSamplers(0, 1, oldSampler.GetAddressOf());
        ctx.PSGetSamplers(1, 1, oldWrapSampler.GetAddressOf());
        if (camera) {
            ctx.PSGetConstantBuffers(0, 1, oldCameraBuffer.GetAddressOf());
            ctx.UpdateSubresource(cameraBuffer.Get(), 0, nullptr, camera, 0, 0);
            ctx.PSSetConstantBuffers(0, 1, cameraBuffer.GetAddressOf());
        }
        if (world) {
            ctx.PSGetConstantBuffers(1, 1, oldSceneBuffer.GetAddressOf());
            ctx.UpdateSubresource(sceneBuffer.Get(), 0, nullptr, world, 0, 0);
            ctx.PSSetConstantBuffers(1, 1, sceneBuffer.GetAddressOf());
        }
        if (sunShadowBuffer) {
            ctx.PSGetConstantBuffers(2, 1, oldSunShadowBuffer.GetAddressOf());
            ctx.UpdateSubresource(sunShadowBuffer.Get(), 0, nullptr, shadowConstants.data(), 0, 0);
            ctx.PSSetConstantBuffers(2, 1, sunShadowBuffer.GetAddressOf());
        }
        if (transmissionBuffer) {
            ctx.PSGetConstantBuffers(3, 1, oldTransmissionBuffer.GetAddressOf());
            ctx.UpdateSubresource(transmissionBuffer.Get(), 0, nullptr, transmissionConstants.data(), 0, 0);
            ctx.PSSetConstantBuffers(3, 1, transmissionBuffer.GetAddressOf());
        }
        if (pointShadowBuffer) {
            ctx.PSGetConstantBuffers(4, 1, oldPointShadowBuffer.GetAddressOf());
            ctx.UpdateSubresource(pointShadowBuffer.Get(), 0, nullptr, pointShadowConstants.data(), 0, 0);
            ctx.PSSetConstantBuffers(4, 1, pointShadowBuffer.GetAddressOf());
        }
        if (weatherBuffer) {
            // Pack de 2 vetores lê só o molhado; o de 4 lê também o campo de vento (R054).
            const WeatherConstants weather{ WetSurface::Read(), WindField::Read() };
            ctx.PSGetConstantBuffers(6, 1, oldWeatherBuffer.GetAddressOf());
            ctx.UpdateSubresource(weatherBuffer.Get(), 0, nullptr, &weather, 0, 0);
            ctx.PSSetConstantBuffers(6, 1, weatherBuffer.GetAddressOf());
        }
        if (decalsBuffer) {
            const auto decals = ImpactDecals::Read();
            ctx.PSGetConstantBuffers(7, 1, oldDecalsBuffer.GetAddressOf());
            ctx.UpdateSubresource(decalsBuffer.Get(), 0, nullptr, &decals, 0, 0);
            ctx.PSSetConstantBuffers(7, 1, decalsBuffer.GetAddressOf());
        }
        ComPtr<ID3D11Buffer> oldRippleBuffer;
        ComPtr<ID3D11ShaderResourceView> oldRippleView;
        const bool ripple = usesWaterRipple && rippleView && rippleBuffer;
        if (ripple) {
            ctx.PSGetConstantBuffers(5, 1, oldRippleBuffer.GetAddressOf());
            ctx.PSGetShaderResources(4, 1, oldRippleView.GetAddressOf());
            ctx.PSSetConstantBuffers(5, 1, rippleBuffer.GetAddressOf());
            ctx.PSSetShaderResources(4, 1, rippleView.GetAddressOf());
        }
        ctx.RSGetState(oldRaster.GetAddressOf());
        ctx.RSGetViewports(&viewportCount, oldViewports);
        ctx.OMGetBlendState(oldBlend.GetAddressOf(), blendFactor, &sampleMask);

        ctx.OMSetRenderTargets(0, nullptr, nullptr);
        if (desc.SampleDesc.Count > 1)
            ctx.ResolveSubresource(scene.Get(), 0, color.Get(), 0, desc.Format);
        else
            ctx.CopyResource(scene.Get(), color.Get());

        ctx.OMSetBlendState(nullptr, nullptr, ~0u);
        ctx.RSSetState(raster.Get());
        ctx.IASetInputLayout(nullptr);
        ctx.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx.VSSetShader(vertex.Get(), nullptr, 0);
        // s0 linear com borda presa; s1 linear com repetição (RFC 0035, texturas e ruído de pack).
        ID3D11SamplerState* const samplers[2]{ sampler.Get(), wrapSampler.Get() };
        ctx.PSSetSamplers(0, 2, samplers);

        const std::array<ID3D11ShaderResourceView*, MaxPassInputs> unbound{};
        auto* depthInput = depthSource.Get();
        if (resolvedDepth.target && !fogLayerPrepared) {
            const D3D11_VIEWPORT viewport{
                0, 0, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0, 1
            };
            ctx.OMSetRenderTargets(1, resolvedDepth.target.GetAddressOf(), nullptr);
            ctx.RSSetViewports(1, &viewport);
            ctx.PSSetShader(resolveDepth.Get(), nullptr, 0);
            ctx.PSSetShaderResources(0, 1, depthSource.GetAddressOf());
            ctx.Draw(3, 0);
            ctx.PSSetShaderResources(0, LegacyPassInputs, unbound.data());
            depthInput = resolvedDepth.view.Get();
        }
        if (fogLayerPrepared && resolvedDepth.view)
            depthInput = resolvedDepth.view.Get();

        Fast::KokiriLabPbrMaterialViews material{};
        if (usesMaterial) {
            material = ResolveMaterial(ctx, Fast::ReadKokiriLabPbrMaterialBuffer(ctx, target.Get(), depth.Get()),
                                       depth.Get(), desc);
        }
        ComPtr<ID3D11Buffer> oldTemporalBuffer;
        std::array<ID3D11ShaderResourceView*, 3> oldTemporalSources{};
        std::array<ID3D11ShaderResourceView*, 3> temporalBound{};
        ID3D11ShaderResourceView* temporalInput = nullptr;
        bool temporalReady = false;
        if (usesTemporal) {
            const int resetSerial = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
                CVAR_DEVELOPER_TOOLS("PackTemporalReset"), 0);
            if (resetSerial != previousTemporalReset) temporalInitialized = false;
            previousTemporalReset = resetSerial;
            const auto index = static_cast<size_t>(passes.back().inputs[0] - 1);
            temporalInput = Output(index).view.Get();
            temporalReady = PrepareTemporal(temporalInput, depthInput);
            std::array<float, 3> eye{};
            temporalReady = temporalReady && camera && world && TemporalEye(*camera, eye);
            if (!temporalReady && !reportedTemporalFailure)
                SPDLOG_WARN("Shader pack temporal indisponivel: recursos/camera invalidos; usando quadro atual.");
            reportedTemporalFailure = !temporalReady;
            const bool valid = temporalReady && temporalInitialized && history.Valid() && history.Delta() <= 0.1f &&
                world->info[2] >= previousSceneTime && world->info[1] == previousRoom &&
                temporalAspect == previousTemporalAspect && !TemporalCameraCut(*camera, previousTemporalCamera);
            TemporalConstants constants{};
            std::memcpy(constants.projection, previousTemporalCamera.projection, sizeof(constants.projection));
            for (auto& row : constants.projection) row[0] *= previousTemporalAspect;
            std::memcpy(constants.view, previousTemporalCamera.view, sizeof(constants.view));
            std::memcpy(constants.info, previousTemporalCamera.info, sizeof(constants.info));
            constants.state[0] = valid ? 1.0f : 0.0f;
            constants.state[1] = history.Delta();
            constants.state[2] = static_cast<float>(temporalSerial & 65535u);
            constants.state[3] = temporalAspect;
            ctx.PSGetConstantBuffers(9, 1, oldTemporalBuffer.GetAddressOf());
            ctx.PSGetShaderResources(5, 3, oldTemporalSources.data());
            ctx.UpdateSubresource(temporalBuffer.Get(), 0, nullptr, &constants, 0, 0);
            ctx.PSSetConstantBuffers(9, 1, temporalBuffer.GetAddressOf());
            ID3D11ShaderResourceView* temporalSources[3]{ valid ? temporalColorView.Get() : nullptr,
                                                        valid ? temporalDepthView.Get() : nullptr,
                                                        temporalReady ? depthInput : nullptr };
            ctx.PSSetShaderResources(5, 3, temporalSources);
            std::copy(std::begin(temporalSources), std::end(temporalSources), temporalBound.begin());
        }
        // Vertex shaders do pack (RFC 0035) leem as mesmas entradas, o sampler s0 e b0, b1 e b10.
        const bool packVertex =
            std::any_of(passes.begin(), passes.end(), [](const PixelPass& pass) { return pass.vertexShader != nullptr; });
        std::array<ID3D11ShaderResourceView*, SavedSourceSlots> oldVertexSources{};
        std::array<ID3D11Buffer*, 11> oldVertexBuffers{};
        ComPtr<ID3D11SamplerState> oldVertexSampler, oldVertexWrapSampler;
        if (packVertex) {
            ctx.VSGetShaderResources(0, SavedSourceSlots, oldVertexSources.data());
            ctx.VSGetConstantBuffers(0, static_cast<UINT>(oldVertexBuffers.size()), oldVertexBuffers.data());
            ctx.VSGetSamplers(0, 1, oldVertexSampler.GetAddressOf());
            ctx.VSGetSamplers(1, 1, oldVertexWrapSampler.GetAddressOf());
            std::array<ID3D11Buffer*, 11> vertexBuffers{};
            vertexBuffers[0] = camera ? cameraBuffer.Get() : nullptr;
            vertexBuffers[1] = world ? sceneBuffer.Get() : nullptr;
            vertexBuffers[10] = irisBuffer.Get();
            ctx.VSSetConstantBuffers(0, static_cast<UINT>(vertexBuffers.size()), vertexBuffers.data());
            ctx.VSSetSamplers(0, 2, samplers);
        }
        ComPtr<ID3D11Buffer> oldIrisBuffer;
        if (irisBuffer) {
            // Primeiro quadro, troca de cena ou pausa: o quadro anterior é o atual, como no Iris.
            const bool valid = irisInitialized && camera && irisScene == sceneId && seconds >= irisTime &&
                               seconds - irisTime <= 0.25;
            const CameraConstants& previous = valid || !camera ? previousIrisCamera : *camera;
            const float previousAspect = valid || !camera ? previousIrisAspect : irisAspect;
            IrisConstants constants{};
            std::memcpy(constants.previousProjection, previous.projection, sizeof(constants.previousProjection));
            for (auto& row : constants.previousProjection) row[0] *= previousAspect;
            std::memcpy(constants.previousView, previous.view, sizeof(constants.previousView));
            std::copy(irisFog.begin(), irisFog.end(), constants.fog);
            constants.info[0] = valid ? 1.0f : 0.0f;
            constants.info[1] = valid ? static_cast<float>(seconds - irisTime) : 0.0f;
            constants.info[2] = irisAspect;
            constants.info[3] = irisFogEnd;
            irisFrame = (irisFrame + 1) % 720720u;
            irisClock = std::fmod(irisClock + constants.info[1], 3600.0);
            constants.clock[0] = static_cast<float>(irisFrame);
            constants.clock[1] = static_cast<float>(irisClock);
            // info[1] é 0 quando !valid (1º quadro, troca de cena, pausa): a rampa salta direto, sem deslizar.
            smoothedFogRamp = SmoothFogRamp(smoothedFogRamp, irisFogRamp, constants.info[1]);
            constants.clock[2] = smoothedFogRamp[0];
            constants.clock[3] = smoothedFogRamp[1];
            smoothedSkyOpenness = SmoothSkyOpenness(smoothedSkyOpenness, irisSky[0], constants.info[1]);
            constants.sky[0] = smoothedSkyOpenness;
            constants.sky[1] = irisSky[1];
            constants.sky[2] = irisSky[2];
            constants.sky[3] = irisSky[3];
            for (size_t k = 0; k < 4; ++k)
                constants.parameters[k] = providerParameters[k].load(std::memory_order_relaxed);
            ctx.PSGetConstantBuffers(10, 1, oldIrisBuffer.GetAddressOf());
            ctx.UpdateSubresource(irisBuffer.Get(), 0, nullptr, &constants, 0, 0);
            ctx.PSSetConstantBuffers(10, 1, irisBuffer.GetAddressOf());
        }
        GpuMark(GpuTimer::ComposeSetup, true);
        for (size_t i = 0; i < passes.size(); ++i) {
            const auto& pass = passes[i];
            GpuMark(GpuTimer::Pass0 + i, false);
            const bool final = i == targets.size();
            const auto width = final ? desc.Width : Output(i).width;
            const auto height = final ? desc.Height : Output(i).height;
            std::array<ID3D11RenderTargetView*, MaxPassOutputs> outputs{};
            const UINT outputCount = final ? 1 : pass.outputs;
            for (UINT k = 0; k < outputCount; ++k)
                outputs[k] = final ? target.Get() : Output(i, k).target.Get();
            const D3D11_VIEWPORT viewport{ 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
            std::array<ID3D11ShaderResourceView*, MaxPassInputs> inputs{};
            for (size_t slot = 0; slot < pass.inputSlots; ++slot) {
                const auto source = pass.inputs[slot];
                if (source == SceneColorInput)
                    inputs[slot] = sceneView.Get();
                else if (source == SceneDepthInput)
                    inputs[slot] = depthInput;
                else if (source == SceneMaterialNormalInput)
                    inputs[slot] = material.normal; // SRV nulo lê zero, inclusive na marca de validade.
                else if (source == SceneMaterialSpecularInput)
                    inputs[slot] = material.specular;
                else if (source == SceneSunShadowInput)
                    inputs[slot] = sunShadowView.Get();
                else if (source == ScenePointShadowInput && pointShadowConstants[0].light[0] > 0.0f) {
                    inputs[slot] = pointShadow->View();
                } else if ((source == SceneGeometryShadowInput || source == SceneFogShadowInput) &&
                           shadowConstants[3][0] > 0.0f) {
                    inputs[slot] = geometryShadow->View();
                } else if (source == SceneNoiseInput) {
                    inputs[slot] = noiseView.Get();
                } else if (source == OneInput) {
                    inputs[slot] = oneView.Get();
                } else if (ShaderPacks::IsPackTextureInput(source)) {
                    inputs[slot] = packTextureViews[ShaderPacks::PackTextureIndex(source)].Get();
                } else if (ShaderPacks::IsHistoryInput(source)) {
                    if (history.Valid())
                        inputs[slot] = Previous(ShaderPacks::HistoryPass(source), ShaderPacks::HistoryOutput(source))
                                           .view.Get();
                } else if (source > 0)
                    inputs[slot] = Output(ShaderPacks::InputPass(source), ShaderPacks::InputOutput(source)).view.Get();
                // ZeroInput fica nulo: o SRV vazio lê (0, 0, 0, 0).
            }
            ctx.OMSetRenderTargets(outputCount, outputs.data(), nullptr);
            ctx.RSSetViewports(1, &viewport);
            ctx.VSSetShader(pass.vertexShader ? pass.vertexShader.Get() : vertex.Get(), nullptr, 0);
            if (pass.vertexShader)
                ctx.VSSetShaderResources(0, pass.inputSlots, inputs.data());
            ctx.PSSetShader(pass.shader.Get(), nullptr, 0);
            ctx.PSSetShaderResources(0, pass.inputSlots, inputs.data());
            ctx.Draw(3, 0);
            ctx.PSSetShaderResources(0, pass.inputSlots, unbound.data());
            if (pass.vertexShader) {
                ctx.VSSetShaderResources(0, pass.inputSlots, unbound.data());
                ctx.VSSetShader(vertex.Get(), nullptr, 0);
            }
            if (pass.inputSlots > LegacyPassInputs) {
                // Um passe largo cobriu t4..t7: devolve a ondulação e o temporal aos passes seguintes.
                if (ripple)
                    ctx.PSSetShaderResources(4, 1, rippleView.GetAddressOf());
                if (usesTemporal)
                    ctx.PSSetShaderResources(5, 3, temporalBound.data());
            }
            GpuMark(GpuTimer::Pass0 + i, true);
            if (pass.fogOver && fogLayerPrepared) {
                GpuMark(GpuTimer::FogComposite, false);
                // Overlay do host no mesmo RTV FP16, antes dos consumidores deste passe.
                // A SRV e a saida sao recursos diferentes: sem read/write simultaneo.
                const int requestedMode = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
                    CVAR_DEVELOPER_TOOLS("FogLayerMode"), DefaultFogLayerMode);
                const int mode = requestedMode >= 0 && requestedMode < static_cast<int>(FogLayerModeCount)
                                     ? requestedMode : DefaultFogLayerMode;
                if (lastFogLayerMode != mode) {
                    SPDLOG_INFO("FogLayerMode={} (0=atual, 1=cor/2, 2=tau/2, 3=tau/4, 4=tau/8, 5=sem camada)", mode);
                    lastFogLayerMode = mode;
                }
                ctx.PSSetShader(fogComposite[mode].Get(), nullptr, 0);
                ctx.PSSetShaderResources(0, 1, fogLayer.view.GetAddressOf());
                ctx.OMSetBlendState(fogBlend.Get(), nullptr, ~0u);
                ctx.Draw(3, 0);
                ctx.PSSetShaderResources(0, LegacyPassInputs, unbound.data());
                ctx.OMSetBlendState(nullptr, nullptr, ~0u);
                GpuMark(GpuTimer::FogComposite, true);
            }
        }

        // Interior com céu automático: o Complementary quase não escurece a cena (a luz das paredes vem
        // pintada na geometria), então o host multiplica a imagem final por um ganho. Suavizado pela
        // mesma abertura do b10 (Market e céu visível contam como abertos). CVar gShaderPack.IndoorSceneScale (padrão 0,75: −24% medido em 22/11 na casa do Link, Deku Tree e gruta).
        if (irisBuffer && sceneGainShader && sceneGainBlend && smoothedSkyOpenness >= 0.0f &&
            smoothedSkyOpenness < 1.0f && providerParameters[3].load(std::memory_order_relaxed) < 0.0f) {
            const float requested = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetFloat(
                "gShaderPack.IndoorSceneScale", 0.75f);
            const float scale = std::isfinite(requested) ? std::clamp(requested, 0.0f, 1.0f) : 1.0f;
            const float gain = scale + (1.0f - scale) * std::clamp(smoothedSkyOpenness, 0.0f, 1.0f);
            if (gain < 0.999f) {
                const D3D11_VIEWPORT full{ 0, 0, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0, 1 };
                const float factor[4]{ gain, gain, gain, 1.0f };
                ctx.OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
                ctx.RSSetViewports(1, &full);
                ctx.VSSetShader(vertex.Get(), nullptr, 0);
                ctx.PSSetShader(sceneGainShader.Get(), nullptr, 0);
                ctx.OMSetBlendState(sceneGainBlend.Get(), factor, ~0u);
                ctx.Draw(3, 0);
                ctx.OMSetBlendState(nullptr, nullptr, ~0u);
            }
        }

        if (usesTemporal) {
            // Nunca copiar o backbuffer: a entrada linear do final precede HUD e falsos-cores.
            ID3D11ShaderResourceView* empty[3]{};
            ctx.PSSetShaderResources(5, 3, empty);
            if (temporalReady) {
                ComPtr<ID3D11Resource> colorResource, depthResource;
                temporalInput->GetResource(colorResource.GetAddressOf());
                depthInput->GetResource(depthResource.GetAddressOf());
                ctx.CopyResource(temporalColor.Get(), colorResource.Get());
                ctx.CopyResource(temporalDepth.Get(), depthResource.Get());
                previousTemporalCamera = *camera;
                previousTemporalAspect = temporalAspect;
                previousSceneTime = world->info[2]; previousRoom = world->info[1];
                ++temporalSerial;
            }
            temporalInitialized = temporalReady;
            ctx.PSSetConstantBuffers(9, 1, oldTemporalBuffer.GetAddressOf());
            ctx.PSSetShaderResources(5, 3, oldTemporalSources.data());
            for (auto* source : oldTemporalSources) if (source) source->Release();
        }
        if (packVertex) {
            ctx.VSSetShaderResources(0, SavedSourceSlots, oldVertexSources.data());
            ctx.VSSetConstantBuffers(0, static_cast<UINT>(oldVertexBuffers.size()), oldVertexBuffers.data());
            ctx.VSSetSamplers(0, 1, oldVertexSampler.GetAddressOf());
            ctx.VSSetSamplers(1, 1, oldVertexWrapSampler.GetAddressOf());
            for (auto* source : oldVertexSources)
                if (source) source->Release();
            for (auto* buffer : oldVertexBuffers)
                if (buffer) buffer->Release();
        }
        if (irisBuffer) {
            if (camera) {
                previousIrisCamera = *camera;
                previousIrisAspect = irisAspect;
                irisScene = sceneId;
                irisTime = seconds;
                irisInitialized = true;
            }
            ctx.PSSetConstantBuffers(10, 1, oldIrisBuffer.GetAddressOf());
        }
        history.Commit();
        if (historyBuffer)
            ctx.PSSetConstantBuffers(8, 1, oldHistoryBuffer.GetAddressOf());

        // Keep the Fast3D state cache valid for the rest of the display list.
        ctx.OMSetRenderTargets(1, target.GetAddressOf(), depth.Get());
        ctx.PSSetShaderResources(0, static_cast<UINT>(oldSources.size()), oldSources.data());
        for (auto* source : oldSources) {
            if (source)
                source->Release();
        }
        ctx.PSSetSamplers(0, 1, oldSampler.GetAddressOf());
        ctx.PSSetSamplers(1, 1, oldWrapSampler.GetAddressOf());
        if (camera)
            ctx.PSSetConstantBuffers(0, 1, oldCameraBuffer.GetAddressOf());
        if (world)
            ctx.PSSetConstantBuffers(1, 1, oldSceneBuffer.GetAddressOf());
        if (sunShadowBuffer)
            ctx.PSSetConstantBuffers(2, 1, oldSunShadowBuffer.GetAddressOf());
        if (transmissionBuffer)
            ctx.PSSetConstantBuffers(3, 1, oldTransmissionBuffer.GetAddressOf());
        if (pointShadowBuffer)
            ctx.PSSetConstantBuffers(4, 1, oldPointShadowBuffer.GetAddressOf());
        if (weatherBuffer)
            ctx.PSSetConstantBuffers(6, 1, oldWeatherBuffer.GetAddressOf());
        if (decalsBuffer)
            ctx.PSSetConstantBuffers(7, 1, oldDecalsBuffer.GetAddressOf());
        if (ripple) {
            ctx.PSSetConstantBuffers(5, 1, oldRippleBuffer.GetAddressOf());
            ctx.PSSetShaderResources(4, 1, oldRippleView.GetAddressOf());
        }
        ctx.PSSetShader(oldPixel.Get(), nullptr, 0);
        ctx.VSSetShader(oldVertex.Get(), nullptr, 0);
        ctx.IASetPrimitiveTopology(oldTopology);
        ctx.IASetInputLayout(oldLayout.Get());
        ctx.RSSetViewports(viewportCount, oldViewports);
        ctx.RSSetState(oldRaster.Get());
        ctx.OMSetBlendState(oldBlend.Get(), blendFactor, sampleMask);

        if (!drawn) {
            SPDLOG_INFO("Shader pack DX11 aplicado: {} ({}x{}, MSAA {}x, {} passes)", name, desc.Width, desc.Height,
                        desc.SampleDesc.Count, passes.size());
            drawn = true;
        }
        if (camera && !reportedInterpolatedCamera && camera->info[2] > 0.0f && camera->info[2] < 1.0f &&
            camera->info[3] > 0.0f) {
            SPDLOG_INFO("Shader pack DX11 câmera interpolada: {} (fração={:.3f}, matrizes={})", name, camera->info[2],
                        static_cast<int>(camera->info[3]));
            reportedInterpolatedCamera = true;
        }
        return true;
    }
};

std::atomic<std::shared_ptr<WorldPass>> activePass;

bool MatchWorldMaterial(uint16_t scene, std::string_view texture) {
    const auto pass = activePass.load();
    if (!pass || !pass->transmissionMaterials)
        return false;
    return std::any_of(
        pass->transmissionMaterials->begin(), pass->transmissionMaterials->end(),
        [scene, texture](const auto& material) { return material.scene == scene && material.texture == texture; });
}

bool ReadWaterFrame(Fast::Interpreter& interpreter, uint32_t ticket, Fast::WaterDisplace::Frame& frame) {
    const auto pass = activePass.load();
    WorldSnapshot snapshot{};
    if (!pass || !ReadWorldSnapshot(interpreter, ticket, snapshot) || snapshot.pass.lock() != pass ||
        !snapshot.waterDisplace) return false;
    auto* dx11 = dynamic_cast<Fast::GfxRenderingAPIDX11*>(interpreter.GetCurrentRenderingAPI());
    if (!dx11 || pass->device.Get() != dx11->mDevice.Get()) return false;
    frame.count = std::min(MaxWaterBoxes, static_cast<size_t>(snapshot.scene.counts[1]));
    frame.time = snapshot.scene.info[2];
    frame.centerX = snapshot.scene.waterPlanes[0][2];
    frame.centerZ = snapshot.scene.waterPlanes[0][3];
    for(size_t i=0;i<frame.count;++i) {
        const auto& b=snapshot.scene.waterBounds[i];
        frame.boxes[i]={b[0],b[1],b[2],b[3],snapshot.scene.waterPlanes[i][0]};
    }
    return frame.count != 0;
}

void DrawWorld(Fast::Interpreter& interpreter, uint32_t ticket, Fast::WorldPostProcessPhase phase) {
    auto pass = activePass.load();
    auto* dx11 = dynamic_cast<Fast::GfxRenderingAPIDX11*>(interpreter.GetCurrentRenderingAPI());
    if (!pass || pass->passes.empty() || !dx11 || pass->device.Get() != dx11->mDevice.Get())
        return;
    pass->SyncGpuTimings(*dx11);
    WorldSnapshot snapshot{};
    if (!ReadWorldSnapshot(interpreter, ticket, snapshot) || snapshot.pass.lock() != pass)
        return;
    const bool sameFrame = pass->frameInterpreter == &interpreter &&
                           pass->frameReplay == Fast::WorldPostProcessReplay();
    if (phase == Fast::WorldPostProcessPhase::Compose) {
        if (!sameFrame || pass->frameTicket != ticket || !pass->compositionPending)
            return;
        pass->compositionPending = false;
        Fast::worldFogLayer = {};
        pass->GpuMark(WorldPass::GpuTimer::BetweenPhases, true);
        pass->GpuMark(WorldPass::GpuTimer::ComposeSetup, false);
        if (pass->usesTemporal) pass->temporalAspect = interpreter.AdjXForAspectRatio(1.0f);
        pass->PrepareIris(interpreter, snapshot);
        const bool drawn = pass->Draw(*dx11->mContext.Get(), pass->cameraBuffer ? &snapshot.camera : nullptr,
                                      pass->sceneBuffer ? &snapshot.scene : nullptr, snapshot.sceneId);
        if (drawn && !pass->firstDrawSubmitted.load(std::memory_order_relaxed))
            pass->firstDrawSubmitted.store(true, std::memory_order_relaxed);
        pass->FinishGpuTimings(drawn);
        pass->fogLayerPrepared = false;
        return;
    }
    // Marcadores duplicados não redesenham sombras nem avançam o histórico duas vezes.
    if (sameFrame && pass->frameTicket == ticket)
        return;
    pass->frameInterpreter = &interpreter;
    pass->frameReplay = Fast::WorldPostProcessReplay();
    pass->frameTicket = ticket;
    pass->compositionPending = false;
    pass->fogLayerPrepared = false;
    Fast::worldFogLayer = {};
    Fast::worldFogSun = {};
    if (!pass->PrepareResources(*dx11->mContext.Get())) {
        pass->FinishGpuTimings(false);
        return;
    }
    pass->GpuMark(WorldPass::GpuTimer::Prepare, true);
    if (pass->usesSunShadow)
        pass->PrepareSunShadow(snapshot.shadowMesh);
    if (pass->transmissionBuffer)
        pass->PrepareTransmission(snapshot.sceneId);
    if (pass->pointShadow) {
        pass->GpuMark(WorldPass::GpuTimer::Point, false);
        pass->PreparePointShadow(*dx11->mContext.Get(), snapshot.scene);
        pass->GpuMark(WorldPass::GpuTimer::Point, true);
    }
    if (pass->geometryShadow) {
        pass->GpuMark(WorldPass::GpuTimer::Sun, false);
        pass->shadowConstants = {};
        const auto triangles = Fast::ReadWorldGeometry();
        const auto cutouts = Fast::ReadWorldCutouts();
        if (Fast::WorldGeometryOverflowed() && pass->reportedGeometryOverflowScene != snapshot.sceneId) {
            SPDLOG_WARN("Shader pack DX11 mapa de geometria cheio na cena {}: {} triângulos guardados, o resto "
                        "fica sem sombra",
                        snapshot.sceneId, triangles.size() / 3);
            pass->reportedGeometryOverflowScene = snapshot.sceneId;
        }
        ShaderPacks::SolarCascades::Camera camera{};
        std::memcpy(camera.projection, snapshot.camera.projection, sizeof(camera.projection));
        std::memcpy(camera.view, snapshot.camera.view, sizeof(camera.view));
        camera.nearDepth = snapshot.camera.info[0];
        camera.farDepth = std::min<double>(snapshot.camera.info[1], snapshot.sunShadowDistance);
        if (snapshot.hasGeometryFocus && snapshot.sunShadowTile != pass->geometryShadow->SolarTileSize() &&
            pass->reportedShadowTileRequest != snapshot.sunShadowTile) {
            const auto previous = pass->geometryShadow->SolarTileSize();
            if (pass->geometryShadow->ResizeSolar(*pass->device.Get(), snapshot.sunShadowTile)) {
                SPDLOG_INFO("Shader pack DX11 mapa de sombra solar: {} → {} por cascata", previous,
                            snapshot.sunShadowTile);
            } else {
                // Uma tentativa por valor pedido: sem memória de vídeo, segue no tamanho anterior.
                SPDLOG_WARN("Shader pack DX11 mapa de sombra solar de {} recusado; mantém {}", snapshot.sunShadowTile,
                            previous);
                pass->reportedShadowTileRequest = snapshot.sunShadowTile;
            }
        }
        auto shadowLight = snapshot.scene.sun;
        if (UseMoonShadow(shadowLight, static_cast<uint16_t>(snapshot.scene.info[3] * 65536.0f))) {
            for (unsigned i = 0; i < 3; ++i)
                shadowLight[i] = -shadowLight[i];
        }
        const bool cascades =
            snapshot.hasGeometryFocus &&
            pass->geometryShadow->DrawCascades(*dx11->mContext.Get(), triangles, cutouts, camera,
                                               snapshot.geometryFocus, shadowLight, snapshot.sunCascades,
                                               pass->shadowConstants);
        if (!cascades && pass->reportedGeometryFailureScene != snapshot.sceneId) {
            SPDLOG_WARN("Shader pack DX11 mapa de geometria recusado na cena {}: {} ({} triângulos, {} lotes de "
                        "recorte, foco={})",
                        snapshot.sceneId,
                        snapshot.hasGeometryFocus ? pass->geometryShadow->lastFailure : "sem jogador",
                        triangles.size() / 3, cutouts.batches.size(), snapshot.hasGeometryFocus);
            pass->reportedGeometryFailureScene = snapshot.sceneId;
        }
        if (cascades && pass->reportedGeometryShadowScene != snapshot.sceneId) {
            size_t cutoutTriangles = 0;
            for (const auto& batch : cutouts.batches)
                cutoutTriangles += batch.count / 3;
            SPDLOG_INFO("Shader pack DX11 mapa de geometria: {} ({} opacos, {} com recorte em {} lotes, {}x{})",
                        pass->name, triangles.size() / 3, cutoutTriangles, cutouts.batches.size(),
                        pass->geometryShadow->SolarTileSize() * pass->geometryShadow->SolarCapacity(),
                        pass->geometryShadow->SolarTileSize());
            pass->reportedGeometryShadowScene = snapshot.sceneId;
        }
        pass->GpuMark(WorldPass::GpuTimer::Sun, true);
    }
    if (pass->lightsFog && snapshot.fogSunTest != 2 && pass->shadowConstants[3][0] > 0.0f) {
        const auto day = snapshot.scene.info[3];
        const auto fade = [](float value) {
            value = std::clamp(value, 0.0f, 1.0f);
            return value * value * (3.0f - 2.0f * value);
        };
        const float daylight = fade((day - 0.25f) / 0.09f) * fade((0.75f - day) / 0.09f);
        if (daylight > 0.0f) {
            auto tint = snapshot.scene.sunColor;
            tint[3] = 0.58f * daylight;
            SunShadowMap::Constants legacy{};
            std::copy_n(pass->shadowConstants.begin(), legacy.size(), legacy.begin());
            Fast::worldFogSun = { dx11->mContext.Get(), pass->geometryShadow->View(), legacy, tint,
                                  snapshot.fogSunTest == 1 };
        }
    }
    pass->compositionPending = phase == Fast::WorldPostProcessPhase::Prepare;
    if (pass->hasFog && pass->compositionPending) {
        pass->GpuMark(WorldPass::GpuTimer::FogPrepare, false);
        pass->PrepareFogLayer(*dx11->mContext.Get());
        pass->GpuMark(WorldPass::GpuTimer::FogPrepare, true);
        Fast::worldFogLayer = { pass, dx11->mContext.Get(), pass->frameReplay, pass->frameTarget, pass->frameDepth,
                               pass->fogLayer.target.Get(),
                               pass->resolvedDepth.view ? pass->resolvedDepth.view.Get() : pass->depthSource.Get() };
    }
    if (pass->compositionPending)
        pass->GpuMark(WorldPass::GpuTimer::BetweenPhases, false);
    if (phase == Fast::WorldPostProcessPhase::Draw) {
        pass->GpuMark(WorldPass::GpuTimer::ComposeSetup, false);
        // Listas legadas mantêm a operação combinada e a publicação solar.
        if (pass->usesTemporal) pass->temporalAspect = interpreter.AdjXForAspectRatio(1.0f);
        pass->PrepareIris(interpreter, snapshot);
        const bool drawn = pass->Draw(*dx11->mContext.Get(), pass->cameraBuffer ? &snapshot.camera : nullptr,
                                      pass->sceneBuffer ? &snapshot.scene : nullptr, snapshot.sceneId);
        if (drawn && !pass->firstDrawSubmitted.load(std::memory_order_relaxed))
            pass->firstDrawSubmitted.store(true, std::memory_order_relaxed);
        pass->FinishGpuTimings(drawn);
    }
}

constexpr std::string_view fullscreenVertex = R"(
void main(uint vertex : SV_VertexID, out float4 position : SV_Position, out float2 uv : TEXCOORD0) {
    uv = float2(vertex == 1 ? 2.0 : 0.0, vertex == 2 ? 2.0 : 0.0);
    position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
}
)";

// A camada ja contem F linear premultiplicada e cobertura. O blend faz
// dst * (1 - a) + F; o alpha do passe permanece intacto.
constexpr std::string_view fogCompositeShader = R"(
Texture2D<float4> fogLayer : register(t0);
SamplerState linearClamp : register(s0);
float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 fog = fogLayer.SampleLevel(linearClamp, uv, 0);
#if FOG_LAYER_MODE == 1
    // Diagnostico de radiancia: cobertura intacta; nao recupera contraste.
    fog.rgb *= 0.5;
#elif FOG_LAYER_MODE >= 2 && FOG_LAYER_MODE <= 4
    // Reescala a espessura optica agregada, preservando a cor reta.
    // a==1 continua opaco: FP16 ja perdeu a transmitancia nesse extremo.
    float a = saturate(fog.a);
    if (a <= 0.0) return 0.0;
    const float scale = FOG_LAYER_MODE == 2 ? 0.5 : (FOG_LAYER_MODE == 3 ? 0.25 : 0.125);
    float coverage = 1.0 - pow(1.0 - a, scale);
    fog.rgb *= coverage / a;
    fog.a = coverage;
#elif FOG_LAYER_MODE == 5
    // Apenas world; a captura da camada continua ativa para um A/B equivalente.
    return 0.0;
#endif
    return fog;
}
)";

// A cor sai pelo blend (destino * fator); o valor do shader é descartado (SrcBlend = ZERO).
constexpr std::string_view sceneGainShaderSource = R"(
float4 main(float4 position : SV_Position) : SV_Target {
    return float4(0.0, 0.0, 0.0, 0.0);
}
)";

constexpr std::string_view resolveDepthShader = R"(
Texture2DMS<float> worldDepth : register(t0);

float main(float4 position : SV_Position) : SV_Target {
    uint width, height, samples;
    worldDepth.GetDimensions(width, height, samples);
    float nearest = 1.0;
    for (uint i = 0; i < samples; ++i) {
        nearest = min(nearest, worldDepth.Load(int2(position.xy), i));
    }
    return nearest;
}
)";

constexpr std::string_view resolveMaterialShader = R"(
Texture2DMS<float> SceneDepth : register(t0);
Texture2DMS<float4> MaterialNormal : register(t1);
Texture2DMS<float4> MaterialSpecular : register(t2);

struct Material {
    float4 normal : SV_Target0;
    float4 specular : SV_Target1;
};

Material main(float4 position : SV_Position) {
    const int2 pixel = int2(position.xy);
    uint width, height, samples;
    SceneDepth.GetDimensions(width, height, samples);
    float nearest = SceneDepth.Load(pixel, 0);
    uint sampleIndex = 0;
    for (uint i = 1; i < samples; ++i) {
        const float depth = SceneDepth.Load(pixel, i);
        if (depth < nearest) {
            nearest = depth;
            sampleIndex = i;
        }
    }

    Material result = (Material)0;
    const float4 normal = MaterialNormal.Load(pixel, sampleIndex);
    // The 1 + depth mark incurs one extra FP32 rounding, and D24 additionally quantizes the DSV.
    if (normal.a >= 1.0 && abs(normal.a - 1.0 - nearest) <= 2.38418579e-7) {
        result.normal = float4(normal.rgb, nearest + 1.0);
        result.specular = MaterialSpecular.Load(pixel, sampleIndex);
    }
    return result;
}
)";

bool CheckCameraBuffer(ID3D11ShaderReflectionConstantBuffer& buffer) {
    D3D11_SHADER_BUFFER_DESC desc{};
    if (FAILED(buffer.GetDesc(&desc)) || desc.Size != sizeof(CameraConstants) || desc.Variables != 3)
        return false;
    for (UINT i = 0; i < desc.Variables; ++i) {
        const bool matrix = i < 2;
        auto* variable = buffer.GetVariableByIndex(i);
        D3D11_SHADER_VARIABLE_DESC value{};
        D3D11_SHADER_TYPE_DESC type{};
        if (FAILED(variable->GetDesc(&value)) || FAILED(variable->GetType()->GetDesc(&type)) ||
            value.StartOffset != 64 * i || value.Size != (matrix ? 64u : 16u) ||
            type.Class != (matrix ? D3D_SVC_MATRIX_ROWS : D3D_SVC_VECTOR) || type.Type != D3D_SVT_FLOAT ||
            type.Rows != (matrix ? 4u : 1u) || type.Columns != 4 || type.Elements != 0)
            return false;
    }
    return true;
}

bool CheckVectorArrayBuffer(ID3D11ShaderReflectionConstantBuffer& buffer, UINT count) {
    D3D11_SHADER_BUFFER_DESC desc{};
    if (FAILED(buffer.GetDesc(&desc)) || desc.Size != count * 16u || desc.Variables != 1)
        return false;
    D3D11_SHADER_TYPE_DESC type{};
    return SUCCEEDED(buffer.GetVariableByIndex(0)->GetType()->GetDesc(&type)) && type.Class == D3D_SVC_VECTOR &&
           type.Type == D3D_SVT_FLOAT && type.Rows == 1 && type.Columns == 4 && type.Elements == count;
}

// LinkSpanIris (b10): float4[10..13] num pack HLSL (qualquer prefixo do bloco de 13 vetores: o buffer do host é
// maior ou igual ao declarado, o que o D3D11 aceita), ou dois row_major float4x4 e de dois a quatro float4 vindos do
// GLSL (o bloco inteiro ou um prefixo dele).
bool CheckIrisBuffer(ID3D11ShaderReflectionConstantBuffer& buffer) {
    D3D11_SHADER_BUFFER_DESC desc{};
    if (FAILED(buffer.GetDesc(&desc)) || desc.Size % 16 || desc.Size < MinIrisVectors * 16 ||
        desc.Size > sizeof(IrisConstants))
        return false;
    const UINT vectors = desc.Size / 16;
    if (desc.Variables == 1)
        return CheckVectorArrayBuffer(buffer, vectors);
    // Duas matrizes e os vetores 8.. um a um.
    if (desc.Variables != vectors - 6)
        return false;
    for (UINT i = 0; i < desc.Variables; ++i) {
        const bool matrix = i < 2;
        auto* variable = buffer.GetVariableByIndex(i);
        D3D11_SHADER_VARIABLE_DESC value{};
        D3D11_SHADER_TYPE_DESC type{};
        if (FAILED(variable->GetDesc(&value)) || FAILED(variable->GetType()->GetDesc(&type)) ||
            value.StartOffset != (matrix ? 64 * i : 128 + 16 * (i - 2)) || value.Size != (matrix ? 64u : 16u) ||
            type.Class != (matrix ? D3D_SVC_MATRIX_ROWS : D3D_SVC_VECTOR) || type.Type != D3D_SVT_FLOAT ||
            type.Rows != (matrix ? 4u : 1u) || type.Columns != 4 || type.Elements != 0)
            return false;
    }
    return true;
}

class PackIncludes final : public ID3DInclude {
  public:
    explicit PackIncludes(const ShaderPacks::PassSource& pass) : pass(pass) {
    }

    HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE, LPCSTR file, LPCVOID parent, LPCVOID* data, UINT* bytes) override try {
        if (!pass.includes || open.size() >= 16)
            return E_FAIL;
        const std::string_view name(file);
        if (name.empty() || name.size() > 160 || name.starts_with("//") ||
            name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-. /") !=
                name.npos) {
            return E_FAIL;
        }

        std::string_view parentFile = pass.file;
        if (parent && parent != pass.source.data()) {
            const auto active = open.find(parent);
            if (active == open.end())
                return E_FAIL;
            parentFile = active->second;
        }
        const std::string path = name.starts_with('/')
                                     ? "shaders/" + std::string(name.substr(1))
                                     : std::string(parentFile.substr(0, parentFile.rfind('/') + 1)) + std::string(name);
        const auto resolved = std::filesystem::path(path).lexically_normal().generic_string();
        if (!resolved.starts_with("shaders/"))
            return E_FAIL;
        const auto source = pass.includes->find(resolved);
        if (source == pass.includes->end() || open.contains(source->second.data()) ||
            expandedBytes + source->second.size() > 1024u * 1024u)
            return E_FAIL;

        open.emplace(source->second.data(), source->first);
        *data = source->second.data();
        *bytes = static_cast<UINT>(source->second.size());
        expandedBytes += *bytes;
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }

    HRESULT STDMETHODCALLTYPE Close(LPCVOID data) override {
        return open.erase(data) ? S_OK : E_FAIL;
    }

  private:
    const ShaderPacks::PassSource& pass;
    std::map<const void*, std::string_view> open;
    size_t expandedBytes = 0;
};

// Bytecode cache for pack passes. FXC can spend tens of seconds on a large pass (KokiriUnboundFull's world pass
// takes ~42 s), and activation runs on the boot path, so the result is kept on disk. The key covers everything
// that reaches the compiler: source, file name, macros, every include of the pack, entry, target and flags.
// The CheckPassInterface validation still runs on every load.
using CompileFunction = decltype(&D3DCompile);
using CreateBlobFunction = decltype(&D3DCreateBlob);
constexpr char ShaderCacheMagic[8] = { 'L', 'S', 'P', 'C', 'S', 'O', '0', '2' };
std::mutex shaderCacheMutex;
constexpr size_t ShaderCacheMaxBytes = 16u * 1024u * 1024u;
constexpr size_t ShaderCacheMaxFiles = 256;

struct ShaderCacheKey {
    uint64_t a = 14695981039346656037ull;
    uint64_t b = 0x84222325cbf29ce4ull;

    void Add(std::string_view text) {
        for (unsigned char c : text) {
            a = (a ^ c) * 1099511628211ull;
            b = (b ^ c) * 0x100000001b3ull + 0x9e3779b97f4a7c15ull;
        }
        // Length separator, so ("ab","c") and ("a","bc") differ.
        const auto size = text.size();
        for (size_t i = 0; i < sizeof(size); ++i) {
            const auto c = static_cast<unsigned char>(size >> (8 * i));
            a = (a ^ c) * 1099511628211ull;
            b = (b ^ c) * 0x100000001b3ull + 0x9e3779b97f4a7c15ull;
        }
    }

    std::string Hex() const {
        char text[33];
        std::snprintf(text, sizeof(text), "%016llx%016llx", static_cast<unsigned long long>(a),
                      static_cast<unsigned long long>(b));
        return text;
    }
};

std::filesystem::path ShaderCacheDirectory() {
    try {
        const auto app = Ship::Context::GetAppDirectoryPath();
        if (app.empty())
            return {};
        return std::filesystem::path(app) / "cache" / "shaderpacks";
    } catch (...) { return {}; }
}

std::string_view ShaderCompilerVersion(HMODULE compiler) {
    // The compiler remains loaded for the lifetime of the DX11 backend.
    const auto resource = FindResourceW(compiler, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16)); // VS_VERSION_INFO
    if (!resource)
        return {};
    const DWORD size = SizeofResource(compiler, resource);
    const auto loaded = LoadResource(compiler, resource);
    const auto* bytes = loaded ? static_cast<const char*>(LockResource(loaded)) : nullptr;
    return bytes && size ? std::string_view(bytes, size) : std::string_view{};
}

ShaderCacheKey MakeShaderCacheKey(std::string_view compilerVersion, std::string_view source, const char* file,
                                  const D3D_SHADER_MACRO* macros, const ShaderPacks::IncludeFiles* includes,
                                  const char* target, UINT flags) {
    ShaderCacheKey key;
    key.Add("d3dcompiler_47/main/cache-2");
    key.Add(compilerVersion);
    key.Add(source);
    key.Add(file ? file : "");
    for (auto* macro = macros; macro && macro->Name; ++macro) {
        key.Add("macro");
        key.Add(macro->Name);
        key.Add(macro->Definition ? "text" : "null");
        key.Add(macro->Definition ? macro->Definition : "");
    }
    if (includes) {
        for (const auto& [name, text] : *includes) {
            key.Add("include");
            key.Add(name);
            key.Add(text);
        }
    }
    key.Add(target);
    key.Add(std::to_string(flags));
    return key;
}

bool LoadCachedShader(const std::filesystem::path& path, const ShaderCacheKey& key, CreateBlobFunction createBlob,
                      ID3DBlob** code) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    char magic[sizeof(ShaderCacheMagic)];
    uint64_t stored[2]{};
    uint32_t size = 0;
    if (!file.read(magic, sizeof(magic)) || std::memcmp(magic, ShaderCacheMagic, sizeof(magic)) != 0 ||
        !file.read(reinterpret_cast<char*>(stored), sizeof(stored)) || stored[0] != key.a || stored[1] != key.b ||
        !file.read(reinterpret_cast<char*>(&size), sizeof(size)) || size == 0 || size > ShaderCacheMaxBytes)
        return false;
    ComPtr<ID3DBlob> blob;
    if (FAILED(createBlob(size, blob.GetAddressOf())) ||
        !file.read(static_cast<char*>(blob->GetBufferPointer()), size) || file.peek() != EOF)
        return false;
    *code = blob.Detach();
    return true;
}

void StoreCachedShader(const std::filesystem::path& path, const ShaderCacheKey& key, ID3DBlob& code) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error || code.GetBufferSize() == 0 || code.GetBufferSize() > ShaderCacheMaxBytes)
        return;
    auto temporary = path;
    temporary += ".tmp-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetCurrentThreadId());
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        const uint64_t stored[2] = { key.a, key.b };
        const auto size = static_cast<uint32_t>(code.GetBufferSize());
        file.write(ShaderCacheMagic, sizeof(ShaderCacheMagic));
        file.write(reinterpret_cast<const char*>(stored), sizeof(stored));
        file.write(reinterpret_cast<const char*>(&size), sizeof(size));
        file.write(static_cast<const char*>(code.GetBufferPointer()), size);
        file.close();
        if (!file) {
            std::filesystem::remove(temporary, error);
            return;
        }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
        std::filesystem::remove(temporary, error);
}

// Keeps the cache bounded: drops the oldest entries past ShaderCacheMaxFiles.
void PruneShaderCache(const std::filesystem::path& directory) {
    const std::lock_guard lock(shaderCacheMutex);
    std::error_code error;
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> entries;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error) && it->path().extension() == ".cso")
            entries.emplace_back(it->last_write_time(error), it->path());
    }
    if (entries.size() <= ShaderCacheMaxFiles)
        return;
    std::sort(entries.begin(), entries.end());
    for (size_t i = 0; i + ShaderCacheMaxFiles < entries.size(); ++i)
        std::filesystem::remove(entries[i].second, error);
}

// D3DCompile with the disk cache. Diagnostics only exist on a real compile.
HRESULT CompileCached(CompileFunction compile, CreateBlobFunction createBlob, const std::filesystem::path& directory,
                      std::string_view compilerVersion, std::string_view source, const char* file,
                      const D3D_SHADER_MACRO* macros, const ShaderPacks::IncludeFiles* includes, ID3DInclude* include,
                      const char* target, UINT flags, ID3DBlob** code, ID3DBlob** diagnostic, bool& cached) {
    cached = false;
    const auto runCompiler = [&] {
        return compile(source.data(), source.size(), file, macros, include, "main", target, flags, 0, code, diagnostic);
    };
    if (!createBlob || directory.empty())
        return runCompiler();

    const auto key = MakeShaderCacheKey(compilerVersion, source, file, macros, includes, target, flags);
    const auto path = directory / (key.Hex() + ".cso");
    {
        const std::lock_guard lock(shaderCacheMutex);
        if (LoadCachedShader(path, key, createBlob, code)) {
            cached = true;
            return S_OK;
        }
    }
    const HRESULT result = runCompiler();
    if (SUCCEEDED(result) && *code) {
        const std::lock_guard lock(shaderCacheMutex);
        StoreCachedShader(path, key, **code);
    }
    return result;
}

// Host-owned shaders use this function pointer synchronously on the activation thread.
struct HostCompileCache {
    CompileFunction compile;
    CreateBlobFunction createBlob;
    std::filesystem::path directory;
    std::string_view compilerVersion;
};
thread_local HostCompileCache hostCompileCache{};

HRESULT WINAPI CachedHostCompile(LPCVOID data, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO* macros,
                                 ID3DInclude* include, LPCSTR entry, LPCSTR target, UINT flags, UINT effectFlags,
                                 ID3DBlob** code, ID3DBlob** diagnostic) {
    const auto& cache = hostCompileCache;
    if (include || effectFlags || !entry || std::strcmp(entry, "main") != 0 || !target || !data)
        return cache.compile(data, size, name, macros, include, entry, target, flags, effectFlags, code, diagnostic);
    bool cached = false;
    return CompileCached(cache.compile, cache.createBlob, cache.directory, cache.compilerVersion,
                         std::string_view(static_cast<const char*>(data), size), name, macros, nullptr, nullptr,
                         target, flags, code, diagnostic, cached);
}

bool reflectedWaterRipple = false;

bool CheckPassInterface(decltype(&D3DReflect) reflect, ID3DBlob& bytecode, const ShaderPacks::PassSource& pass,
                        bool& usesCamera, bool& usesScene, bool& usesSunConstants, bool& usesTransmission,
                        size_t& pointLightCapacity, size_t& sunCascadeCapacity, bool& usesWeather, bool& usesDecals, bool& usesTemporal,
                        bool& usesIris, bool packVertex = false) {
    ComPtr<ID3D11ShaderReflection> shader;
    if (FAILED(reflect(bytecode.GetBufferPointer(), bytecode.GetBufferSize(), __uuidof(ID3D11ShaderReflection),
                       reinterpret_cast<void**>(shader.GetAddressOf()))))
        return false;
    D3D11_SHADER_DESC desc{};
    if (FAILED(shader->GetDesc(&desc)))
        return false;
    for (UINT i = 0; !packVertex && i < desc.InputParameters; ++i) {
        D3D11_SIGNATURE_PARAMETER_DESC input{};
        if (FAILED(shader->GetInputParameterDesc(i, &input)) || input.SemanticIndex != 0 ||
            input.ComponentType != D3D_REGISTER_COMPONENT_FLOAT32)
            return false;
        if (input.Register == 0 && input.SystemValueType == D3D_NAME_POSITION)
            continue;
        if (input.Register != 1 || (input.Mask & ~3u) || _stricmp(input.SemanticName, "TEXCOORD") != 0)
            return false;
    }
    for (UINT i = 0; i < desc.BoundResources; ++i) {
        D3D11_SHADER_INPUT_BIND_DESC binding{};
        if (FAILED(shader->GetResourceBindingDesc(i, &binding)) || binding.BindCount != 1)
            return false;
        if (binding.Type == D3D_SIT_SAMPLER && binding.BindPoint <= 1 && !(binding.uFlags & D3D_SIF_COMPARISON_SAMPLER))
            continue;
        if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 0 &&
            std::strcmp(binding.Name, "LinkSpanCamera") == 0) {
            auto* buffer = shader->GetConstantBufferByName(binding.Name);
            if (!CheckCameraBuffer(*buffer))
                return false;
            usesCamera = true;
            continue;
        }
        if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 1 &&
            std::strcmp(binding.Name, "LinkSpanScene") == 0) {
            auto* buffer = shader->GetConstantBufferByName(binding.Name);
            if (!CheckVectorArrayBuffer(*buffer, sizeof(SceneConstants) / 16))
                return false;
            usesScene = true;
            continue;
        }
        if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 2 &&
            std::string_view(binding.Name) == "LinkSpanSunShadow") {
            auto* buffer = shader->GetConstantBufferByName(binding.Name);
            size_t capacity = 0;
            if (CheckVectorArrayBuffer(*buffer, ShaderPacks::SolarCascades::ConstantVectors)) {
                capacity = ShaderPacks::SolarCascades::MaxCascades;
            } else if (CheckVectorArrayBuffer(*buffer, sizeof(SunShadowMap::Constants) / 16)) {
                capacity = 1;
            } else {
                return false;
            }
            // Todos os passes compartilham o mesmo SRV. Um consumidor antigo
            // não sabe limitar PCF ao tile: o menor contrato determina o atlas.
            sunCascadeCapacity = sunCascadeCapacity ? std::min(sunCascadeCapacity, capacity) : capacity;
            usesSunConstants = true;
            continue;
        }
        if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 3 &&
            std::string_view(binding.Name) == "LinkSpanTransmission") {
            auto* buffer = shader->GetConstantBufferByName(binding.Name);
            if (!pass.transmissionMaterials || !CheckVectorArrayBuffer(*buffer, sizeof(TransmissionConstants) / 16))
                return false;
            usesTransmission = true;
            continue;
        }
        if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 4 &&
            std::string_view(binding.Name) == "LinkSpanPointShadow") {
            auto* buffer = shader->GetConstantBufferByName(binding.Name);
            if (std::find(pass.inputs.begin(), pass.inputs.end(), ScenePointShadowInput) == pass.inputs.end()) {
                return false;
            }
            constexpr UINT vectorsPerLight = sizeof(PointShadowConstants) / 16;
            if (CheckVectorArrayBuffer(*buffer, vectorsPerLight * MaxPointLights)) {
                pointLightCapacity = MaxPointLights;
            } else if (CheckVectorArrayBuffer(*buffer, vectorsPerLight)) {
                pointLightCapacity = std::max<size_t>(pointLightCapacity, 1);
            } else {
                return false;
            }
            continue;
        }
        if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 5 &&
            std::string_view(binding.Name) == "LinkSpanWaterRipple") {
            if (!CheckVectorArrayBuffer(*shader->GetConstantBufferByName(binding.Name),
                                        sizeof(WaterRipple::Frame) / 16))
                return false;
            reflectedWaterRipple = true;
            continue;
        }
        if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 8 &&
            std::string_view(binding.Name) == "LinkSpanHistory") {
            if (!CheckVectorArrayBuffer(*shader->GetConstantBufferByName(binding.Name), 1))
                return false;
            continue;
        }
        if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 6 &&
            std::string_view(binding.Name) == "LinkSpanWeather") {
            auto& weather = *shader->GetConstantBufferByName(binding.Name);
            if (!CheckVectorArrayBuffer(weather, sizeof(WetSurface::Frame) / 16) &&
                !CheckVectorArrayBuffer(weather, sizeof(WeatherConstants) / 16))
                return false;
            usesWeather = true;
            continue;
        }
        if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 7 &&
            std::string_view(binding.Name) == "LinkSpanDecals") {
            if (!CheckVectorArrayBuffer(*shader->GetConstantBufferByName(binding.Name),
                                        sizeof(ImpactDecals::Frame) / 16))
                return false;
            usesDecals = true;
            continue;
        }
        if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 10 &&
            std::string_view(binding.Name) == "LinkSpanIris") {
            if (!CheckIrisBuffer(*shader->GetConstantBufferByName(binding.Name)))
                return false;
            usesIris = true;
            usesCamera = true;
            continue;
        }
        if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 9 &&
            std::string_view(binding.Name) == "LinkSpanTemporal") {
            if (!CheckVectorArrayBuffer(*shader->GetConstantBufferByName(binding.Name), 10)) return false;
            usesTemporal = true;
            usesCamera = usesScene = true;
            continue;
        }
        // t5..t7 são as texturas temporais só quando têm o nome delas; com 16 entradas (V3) os mesmos slots
        // também servem a entradas comuns do passe, validadas abaixo.
        static constexpr std::array<std::string_view, 3> temporalNames{
            "LinkSpanTemporalColor", "LinkSpanTemporalDepth", "LinkSpanTemporalCurrentDepth" };
        if (binding.Type == D3D_SIT_TEXTURE && binding.BindPoint >= 5 && binding.BindPoint <= 7 &&
            std::string_view(binding.Name) == temporalNames[binding.BindPoint - 5]) {
            if (binding.Dimension != D3D_SRV_DIMENSION_TEXTURE2D || binding.ReturnType != D3D_RETURN_TYPE_FLOAT ||
                pass.inputs[binding.BindPoint] != UnboundInput) return false;
            usesTemporal = true;
            usesCamera = usesScene = true;
            continue;
        }
        if (binding.Type == D3D_SIT_TEXTURE && binding.BindPoint == 4 &&
            std::string_view(binding.Name) == "LinkSpanWaterRippleField") {
            if (binding.Dimension != D3D_SRV_DIMENSION_TEXTURE2D || binding.ReturnType != D3D_RETURN_TYPE_FLOAT ||
                pass.inputs[4] != UnboundInput) {
                return false;
            }
            reflectedWaterRipple = true;
            continue;
        }
        if (binding.Type != D3D_SIT_TEXTURE || binding.Dimension != D3D_SRV_DIMENSION_TEXTURE2D ||
            binding.BindPoint >= pass.inputs.size())
            return false;
        const auto input = pass.inputs[binding.BindPoint];
        if (input == UnboundInput ||
            ((input == SceneDepthInput || input == SceneSunShadowInput || input == SceneGeometryShadowInput ||
              input == SceneFogShadowInput || input == ScenePointShadowInput || input == SceneMaterialNormalInput ||
               input == SceneMaterialSpecularInput) &&
             binding.ReturnType != D3D_RETURN_TYPE_FLOAT)) {
            return false;
        }
    }
    return true;
}

// Vertex shader do pack: só SV_VertexID na entrada, SV_Position na saída, as mesmas entradas do passe, s0 e
// LinkSpanCamera/LinkSpanScene/LinkSpanIris. Cada entrada do PS tem de sair do VS no mesmo registrador.
bool CheckVertexInterface(decltype(&D3DReflect) reflect, ID3DBlob& vertexCode, ID3DBlob& pixelCode,
                          const ShaderPacks::PassSource& pass, bool& usesCamera, bool& usesScene, bool& usesIris) {
    ComPtr<ID3D11ShaderReflection> vertex, pixel;
    if (FAILED(reflect(vertexCode.GetBufferPointer(), vertexCode.GetBufferSize(), __uuidof(ID3D11ShaderReflection),
                       reinterpret_cast<void**>(vertex.GetAddressOf()))) ||
        FAILED(reflect(pixelCode.GetBufferPointer(), pixelCode.GetBufferSize(), __uuidof(ID3D11ShaderReflection),
                       reinterpret_cast<void**>(pixel.GetAddressOf()))))
        return false;
    D3D11_SHADER_DESC vertexDesc{}, pixelDesc{};
    if (FAILED(vertex->GetDesc(&vertexDesc)) || FAILED(pixel->GetDesc(&pixelDesc)))
        return false;
    for (UINT i = 0; i < vertexDesc.InputParameters; ++i) {
        D3D11_SIGNATURE_PARAMETER_DESC input{};
        if (FAILED(vertex->GetInputParameterDesc(i, &input)) || input.SystemValueType != D3D_NAME_VERTEX_ID)
            return false;
    }
    bool position = false;
    std::vector<D3D11_SIGNATURE_PARAMETER_DESC> outputs(vertexDesc.OutputParameters);
    for (UINT i = 0; i < vertexDesc.OutputParameters; ++i) {
        if (FAILED(vertex->GetOutputParameterDesc(i, &outputs[i])))
            return false;
        position |= outputs[i].SystemValueType == D3D_NAME_POSITION;
    }
    if (!position)
        return false;
    for (UINT i = 0; i < pixelDesc.InputParameters; ++i) {
        D3D11_SIGNATURE_PARAMETER_DESC input{};
        if (FAILED(pixel->GetInputParameterDesc(i, &input)))
            return false;
        const auto match = std::find_if(outputs.begin(), outputs.end(), [&](const D3D11_SIGNATURE_PARAMETER_DESC& output) {
            return output.Register == input.Register && output.SemanticIndex == input.SemanticIndex &&
                   _stricmp(output.SemanticName, input.SemanticName) == 0 &&
                   output.ComponentType == input.ComponentType && (input.Mask & ~output.Mask) == 0;
        });
        if (match == outputs.end())
            return false;
    }
    for (UINT i = 0; i < vertexDesc.BoundResources; ++i) {
        D3D11_SHADER_INPUT_BIND_DESC binding{};
        if (FAILED(vertex->GetResourceBindingDesc(i, &binding)) || binding.BindCount != 1)
            return false;
        if (binding.Type == D3D_SIT_SAMPLER && binding.BindPoint <= 1 && !(binding.uFlags & D3D_SIF_COMPARISON_SAMPLER))
            continue;
        if (binding.Type == D3D_SIT_CBUFFER) {
            const std::string_view name(binding.Name);
            auto* buffer = vertex->GetConstantBufferByName(binding.Name);
            if (binding.BindPoint == 0 && name == "LinkSpanCamera" && CheckCameraBuffer(*buffer)) {
                usesCamera = true;
                continue;
            }
            if (binding.BindPoint == 1 && name == "LinkSpanScene" &&
                CheckVectorArrayBuffer(*buffer, sizeof(SceneConstants) / 16)) {
                usesScene = true;
                continue;
            }
            if (binding.BindPoint == 10 && name == "LinkSpanIris" && CheckIrisBuffer(*buffer)) {
                usesIris = usesCamera = true;
                continue;
            }
            return false;
        }
        if (binding.Type != D3D_SIT_TEXTURE || binding.Dimension != D3D_SRV_DIMENSION_TEXTURE2D ||
            binding.BindPoint >= pass.inputs.size() || pass.inputs[binding.BindPoint] == UnboundInput)
            return false;
    }
    return true;
}

} // namespace
#endif

namespace ShaderPacks {

void SetProviderParameters(const float* values, uint32_t count) {
#ifdef _WIN32
    for (uint32_t k = 0; k < 4; ++k) {
        const float value = values && k < count && std::isfinite(values[k]) ? values[k] : 0.0f;
        providerParameters[k].store(value, std::memory_order_relaxed);
    }
#endif
}

bool ActivateWorldPass(std::span<const PassSource> sources, std::string_view name, std::string& error) {
#ifdef _WIN32
    using Clock = std::chrono::steady_clock;
    const auto activationStart = Clock::now();
    auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
    auto interpreter = window ? window->GetInterpreterWeak().lock() : nullptr;
    auto* renderer = interpreter ? interpreter->GetCurrentRenderingAPI() : nullptr;
    auto* dx11 = dynamic_cast<Fast::GfxRenderingAPIDX11*>(renderer);
    if (!dx11 || !dx11->mDevice) {
        error = "Selecione o DirectX 11 para ativar este pack.";
        return false;
    }
    const auto compiler = GetModuleHandleW(L"D3DCompiler_47.dll");
    const auto rawCompile =
        compiler ? reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
    const auto reflect =
        compiler ? reinterpret_cast<decltype(&D3DReflect)>(GetProcAddress(compiler, "D3DReflect")) : nullptr;
    // Optional: without it the cache is skipped and every activation compiles.
    const auto createBlob =
        compiler ? reinterpret_cast<CreateBlobFunction>(GetProcAddress(compiler, "D3DCreateBlob")) : nullptr;
    if (!rawCompile || !reflect) {
        error = "Compilador DirectX 11 indisponível.";
        return false;
    }
    // Host-owned shaders (fullscreen vertex, fog variants, depth resolve, shadow maps) go through the same cache.
    const auto compilerVersion = ShaderCompilerVersion(compiler);
    const auto cacheDirectory = createBlob && !compilerVersion.empty() ? ShaderCacheDirectory()
                                                                       : std::filesystem::path{};
    hostCompileCache = { rawCompile, createBlob, cacheDirectory, compilerVersion };
    const CompileFunction compile = cacheDirectory.empty() ? rawCompile : CachedHostCompile;

    if (!ValidPassSources(sources)) {
        error = "Grafo de passes inválido.";
        return false;
    }
    auto next = std::make_shared<WorldPass>();
    next->device = dx11->mDevice;
    next->name = name;
    ComPtr<ID3DBlob> vs;
    bool usesCamera = false;
    bool usesScene = false;
    bool usesSunConstants = false;
    bool usesTransmission = false;
    bool usesGeometryShadow = false;
    bool usesWeather = false;
    bool usesDecals = false;
    bool usesIris = false;
    bool usesNoise = false;
    bool usesOne = false;
    // RFC 0035: "shaderModel": 5 vale para todo o programa (ValidPassSources exige o mesmo em todos os passes).
    const bool shaderModel5 = sources.front().shaderModel == 5;
    if (shaderModel5 && dx11->mFeatureLevel < D3D_FEATURE_LEVEL_11_0) {
        error = "Este pack pede Shader Model 5 (DirectX 11.0).";
        return false;
    }
    const char* const pixelProfile = shaderModel5 ? "ps_5_0" : "ps_4_0";
    const D3D_SHADER_MACRO capabilities[] = { { "LINKSPAN_HAS_WEATHER", "1" },
                                              { "LINKSPAN_HAS_DECALS", "1" },
                                              { "LINKSPAN_HAS_WIND_FIELD", "1" },
                                              { "LINKSPAN_HAS_TEMPORAL", "1" },
                                              { nullptr, nullptr } };
    constexpr UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL2;
    reflectedWaterRipple = false;
    struct ShaderBuild {
        ComPtr<ID3DBlob> code, diagnostic;
        HRESULT status = E_FAIL;
        bool cached = false;
        double milliseconds = 0.0;
    };
    struct PassBuild {
        ShaderBuild pixel, vertex;
        std::exception_ptr exception;
    };
    std::vector<PassBuild> builds(sources.size());
    std::vector<size_t> order(sources.size());
    std::iota(order.begin(), order.end(), size_t{ 0 });
    const auto size = [&](size_t i) { return sources[i].source.size() + sources[i].vertexSource.size(); };
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return size(a) > size(b); });

    constexpr unsigned maxWorkers = 8;
    const auto concurrency = std::clamp(std::thread::hardware_concurrency(), 1u, maxWorkers);
    const auto workerCount = std::min<size_t>(sources.size(), concurrency);
    const char* const vertexProfile = shaderModel5 ? "vs_5_0" : "vs_4_0";
    const auto elapsedMs = [](auto start, auto end) {
        return std::chrono::duration<double, std::milli>(end - start).count();
    };
    const auto compileShader = [&](ShaderBuild& out, std::string_view source, const char* file,
                                   const IncludeFiles* files, ID3DInclude* includes, const char* profile) {
        const auto start = Clock::now();
        out.status = CompileCached(rawCompile, createBlob, cacheDirectory, compilerVersion, source, file, capabilities,
                                   files, includes, profile, flags, out.code.GetAddressOf(),
                                   out.diagnostic.GetAddressOf(), out.cached);
        out.milliseconds = elapsedMs(start, Clock::now());
    };
    std::atomic<size_t> nextJob{ 0 };
    const auto compileWorker = [&] {
        // Each result slot and include resolver belongs to exactly one CPU job.
        for (size_t job = nextJob.fetch_add(1, std::memory_order_relaxed); job < order.size();
             job = nextJob.fetch_add(1, std::memory_order_relaxed)) {
            const size_t index = order[job];
            const auto& pass = sources[index];
            auto& result = builds[index];
            try {
                PackIncludes includes(pass);
                compileShader(result.pixel, pass.source, pass.file.c_str(), pass.includes.get(), &includes,
                              pixelProfile);
                if (!pass.vertexSource.empty())
                    compileShader(result.vertex, pass.vertexSource, pass.vertexFile.c_str(), nullptr, nullptr,
                                  vertexProfile);
            } catch (...) {
                result.exception = std::current_exception();
            }
        }
    };
    SPDLOG_INFO("Shader pack DX11 compilação início: {} passes={} workers={} flags={:#x} cache={}", name,
                sources.size(), workerCount, flags, !cacheDirectory.empty());
    const auto compileStart = Clock::now();
    size_t workersUsed = 1;
    {
        std::vector<std::jthread> workers;
        workers.reserve(workerCount - 1);
        try {
            for (size_t i = 1; i < workerCount; ++i)
                workers.emplace_back(compileWorker);
        } catch (const std::system_error&) {
            // The caller and any workers already started continue draining the same queue.
        }
        workersUsed += workers.size();
        compileWorker();
    } // Join before reflection, D3D11 object creation or any use of the results.
    const auto compiledAt = Clock::now();
    size_t pixelHits = 0, vertexHits = 0, vertexCount = 0;
    for (size_t i = 0; i < sources.size(); ++i) {
        const auto& pass = sources[i];
        const auto& result = builds[i];
        pixelHits += result.pixel.cached;
        vertexHits += result.vertex.cached;
        vertexCount += !pass.vertexSource.empty();
        SPDLOG_INFO("Shader pack DX11 passe {}/{}: {} ps_ms={:.2f} ps_hit={} ps_hr={:#x} "
                    "vs={} vs_ms={:.2f} vs_hit={} vs_hr={:#x} exceção={}",
                    i + 1, sources.size(), pass.file, result.pixel.milliseconds, result.pixel.cached,
                    static_cast<unsigned>(result.pixel.status), pass.vertexFile, result.vertex.milliseconds,
                    result.vertex.cached, static_cast<unsigned>(pass.vertexSource.empty() ? S_OK : result.vertex.status),
                    bool(result.exception));
    }
    if (!cacheDirectory.empty() && (pixelHits != sources.size() || vertexHits != vertexCount))
        PruneShaderCache(cacheDirectory);
    SPDLOG_INFO("Shader pack DX11 compilação fim: {} ps_hits={}/{} vs_hits={}/{} workers={} wall_ms={:.2f}",
                name, pixelHits, sources.size(), vertexHits, vertexCount, workersUsed,
                elapsedMs(compileStart, compiledAt));
    for (const auto& pass : sources) {
        const auto index = static_cast<size_t>(&pass - sources.data());
        const auto& result = builds[index];
        if (result.exception)
            std::rethrow_exception(result.exception);
        const auto& ps = result.pixel.code;
        if (FAILED(result.pixel.status) || !ps) {
            const auto& diagnostic = result.pixel.diagnostic;
            error = diagnostic ? static_cast<const char*>(diagnostic->GetBufferPointer())
                               : "Falha ao compilar HLSL: " + pass.file;
            return false;
        }
        const bool packVertex = !pass.vertexSource.empty();
        if (packVertex && (FAILED(result.vertex.status) || !result.vertex.code)) {
            const auto& diagnostic = result.vertex.diagnostic;
            error = diagnostic ? static_cast<const char*>(diagnostic->GetBufferPointer())
                               : "Falha ao compilar o vertex shader: " + pass.vertexFile;
            return false;
        }
        bool temporal = false;
        if (!CheckPassInterface(reflect, *ps.Get(), pass, usesCamera, usesScene, usesSunConstants, usesTransmission,
                                next->pointLightCapacity, next->sunCascadeCapacity, usesWeather, usesDecals, temporal,
                                usesIris, packVertex) ||
            (packVertex && !CheckVertexInterface(reflect, *result.vertex.code.Get(), *ps.Get(), pass, usesCamera,
                                                 usesScene, usesIris))) {
            error = "Assinatura ou recursos fora do contrato do passe: " + pass.file;
            return false;
        }
        if (temporal) {
            // Extensao valida apenas no final e com entrada linear de tela inteira.
            const int input = pass.inputs[0];
            if (&pass != &sources.back() || input <= 0 || static_cast<size_t>(input) > sources.size() ||
                sources[input - 1].width || sources[input - 1].divisor != 1) {
                error = "LinkSpanTemporal exige entrada t0 intermediaria de tela inteira no passe final.";
                return false;
            }
            next->usesTemporal = true;
            next->usesDepth = true;
            next->hasHistory = true;
        }
        const bool wide = std::any_of(pass.inputs.begin() + LegacyPassInputs, pass.inputs.end(),
                                      [](int input) { return input != UnboundInput; });
        PixelPass compiled{ {},          pass.divisor, pass.inputs, pass.width,   pass.height,
                            pass.persistent, pass.fogOver, pass.file,   pass.outputs,
                            wide ? static_cast<UINT>(MaxPassInputs) : LegacyPassInputs };
        usesNoise |= std::find(pass.inputs.begin(), pass.inputs.end(), SceneNoiseInput) != pass.inputs.end();
        if (packVertex && FAILED(next->device->CreateVertexShader(result.vertex.code->GetBufferPointer(),
                                                                  result.vertex.code->GetBufferSize(), nullptr,
                                                                  compiled.vertexShader.GetAddressOf()))) {
            error = "Falha ao criar o vertex shader DirectX 11: " + pass.vertexFile;
            return false;
        }
        usesOne |= std::find(pass.inputs.begin(), pass.inputs.end(), OneInput) != pass.inputs.end();
        next->hasHistory |= pass.persistent;
        next->hasFog |= pass.fogOver;
        if (FAILED(next->device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr,
                                                   compiled.shader.GetAddressOf()))) {
            error = "Falha ao criar o shader DirectX 11: " + pass.file;
            return false;
        }
        next->usesDepth |= std::find(pass.inputs.begin(), pass.inputs.end(), SceneDepthInput) != pass.inputs.end();
        next->usesMaterial |= std::any_of(pass.inputs.begin(), pass.inputs.end(), [](int input) {
            return input == SceneMaterialNormalInput || input == SceneMaterialSpecularInput;
        });
        next->usesSunShadow |=
            std::find(pass.inputs.begin(), pass.inputs.end(), SceneSunShadowInput) != pass.inputs.end();
        usesGeometryShadow |=
            std::find(pass.inputs.begin(), pass.inputs.end(), SceneGeometryShadowInput) != pass.inputs.end();
        next->lightsFog |= std::find(pass.inputs.begin(), pass.inputs.end(), SceneFogShadowInput) != pass.inputs.end();
        next->passes.push_back(std::move(compiled));
    }
    const auto passesReady = std::chrono::steady_clock::now();
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    raster.MultisampleEnable = TRUE;
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler.MaxAnisotropy = 1;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(compile(fullscreenVertex.data(), fullscreenVertex.size(), nullptr, nullptr, nullptr, "main", "vs_4_0",
                       flags, 0, vs.GetAddressOf(), nullptr)) ||
        FAILED(next->device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr,
                                                next->vertex.GetAddressOf())) ||
        FAILED(next->device->CreateRasterizerState(&raster, next->raster.GetAddressOf())) ||
        FAILED(next->device->CreateSamplerState(&sampler, next->sampler.GetAddressOf()))) {
        error = "Falha ao criar o passe DirectX 11.";
        return false;
    }
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    if (FAILED(next->device->CreateSamplerState(&sampler, next->wrapSampler.GetAddressOf()))) {
        error = "Falha ao criar o sampler s1 no DirectX 11.";
        return false;
    }
    if (next->hasFog) {
        // Mesmo um pack sem scene.depth explicita precisa de profundidade para a camada.
        next->usesDepth = true;
        ComPtr<ID3DBlob> composite;
        D3D11_BLEND_DESC blend{};
        auto& rt = blend.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D11_BLEND_ONE;
        rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ZERO;
        rt.DestBlendAlpha = D3D11_BLEND_ONE;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN |
                                   D3D11_COLOR_WRITE_ENABLE_BLUE;
        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
        // Variantes compiladas na ativacao: sem cbuffer/slot novo nem compilacao por quadro.
        constexpr const char* modes[FogLayerModeCount] = { "0", "1", "2", "3", "4", "5" };
        for (size_t mode = 0; mode < FogLayerModeCount; ++mode) {
            const D3D_SHADER_MACRO defines[] = { { "FOG_LAYER_MODE", modes[mode] }, { nullptr, nullptr } };
            composite.Reset();
            if (FAILED(compile(fogCompositeShader.data(), fogCompositeShader.size(), "fog.over", defines, nullptr,
                               "main", "ps_4_0", flags, 0, composite.GetAddressOf(), nullptr)) ||
                FAILED(next->device->CreatePixelShader(composite->GetBufferPointer(), composite->GetBufferSize(),
                                                       nullptr, next->fogComposite[mode].GetAddressOf()))) {
                error = "Falha ao criar variante de calibracao da nevoa no DirectX 11.";
                return false;
            }
        }
        if (!dx11->PrepareWorldFogLayer() ||
            FAILED(next->device->CreateBlendState(&blend, next->fogBlend.GetAddressOf())) ||
            FAILED(next->device->CreateDepthStencilState(&depth, next->fogDepthState.GetAddressOf()))) {
            error = "Falha ao criar a composição linear da névoa no DirectX 11.";
            return false;
        }
    }
    {
        ComPtr<ID3DBlob> gain;
        D3D11_BLEND_DESC gainBlend{};
        gainBlend.RenderTarget[0].BlendEnable = TRUE;
        gainBlend.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
        gainBlend.RenderTarget[0].DestBlend = D3D11_BLEND_BLEND_FACTOR;
        gainBlend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        gainBlend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
        gainBlend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        gainBlend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        gainBlend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(compile(sceneGainShaderSource.data(), sceneGainShaderSource.size(), "scene.gain", nullptr, nullptr,
                           "main", "ps_4_0", flags, 0, gain.GetAddressOf(), nullptr)) ||
            FAILED(next->device->CreatePixelShader(gain->GetBufferPointer(), gain->GetBufferSize(), nullptr,
                                                   next->sceneGainShader.GetAddressOf())) ||
            FAILED(next->device->CreateBlendState(&gainBlend, next->sceneGainBlend.GetAddressOf()))) {
            // Opcional: sem ele o interior só não escurece.
            next->sceneGainShader.Reset();
            next->sceneGainBlend.Reset();
        }
    }
    if (next->usesDepth && dx11->mFeatureLevel >= D3D_FEATURE_LEVEL_10_1) {
        ComPtr<ID3DBlob> resolve;
        if (FAILED(compile(resolveDepthShader.data(), resolveDepthShader.size(), "scene.depth", nullptr, nullptr,
                           "main", "ps_4_1", flags, 0, resolve.GetAddressOf(), nullptr)) ||
            FAILED(next->device->CreatePixelShader(resolve->GetBufferPointer(), resolve->GetBufferSize(), nullptr,
                                                   next->resolveDepth.GetAddressOf()))) {
            error = "Falha ao criar a resolução MSAA da profundidade.";
            return false;
        }
    }
    if (next->usesMaterial && dx11->mFeatureLevel >= D3D_FEATURE_LEVEL_10_1) {
        ComPtr<ID3DBlob> resolve, diagnostics;
        if (FAILED(compile(resolveMaterialShader.data(), resolveMaterialShader.size(), "scene.material", nullptr,
                           nullptr, "main", "ps_4_1", flags, 0, resolve.GetAddressOf(), diagnostics.GetAddressOf())) ||
            FAILED(next->device->CreatePixelShader(resolve->GetBufferPointer(), resolve->GetBufferSize(), nullptr,
                                                   next->resolveMaterial.GetAddressOf()))) {
            SPDLOG_WARN("Shader pack DX11 sem resolução MSAA de material: {}",
                        diagnostics ? static_cast<const char*>(diagnostics->GetBufferPointer()) : "CreatePixelShader");
        }
    }
    if (next->usesTemporal) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(TemporalConstants);
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(next->device->CreateBuffer(&desc, nullptr, next->temporalBuffer.GetAddressOf()))) {
            error = "Falha ao criar constantes temporais.";
            return false;
        }
    }
    if (usesCamera) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(CameraConstants);
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(next->device->CreateBuffer(&desc, nullptr, next->cameraBuffer.GetAddressOf()))) {
            error = "Falha ao criar os dados da câmera no DirectX 11.";
            return false;
        }
    }
    {
        D3D11_BUFFER_DESC historyDesc{};
        historyDesc.ByteWidth = sizeof(SceneVector);
        historyDesc.Usage = D3D11_USAGE_DEFAULT;
        historyDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(next->device->CreateBuffer(&historyDesc, nullptr, next->historyBuffer.GetAddressOf()))) {
            error = "Falha ao criar constantes de histórico.";
            return false;
        }
    }
    if (usesIris) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(IrisConstants);
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(next->device->CreateBuffer(&desc, nullptr, next->irisBuffer.GetAddressOf()))) {
            error = "Falha ao criar LinkSpanIris no DirectX 11.";
            return false;
        }
    }
    if (usesNoise) {
        // scene.noise: ruído branco fixo 256x256 RGBA8, como o noisetex padrão do Iris.
        constexpr UINT side = 256;
        std::vector<uint32_t> texels(side * side);
        uint32_t state = 0x9E3779B9u;
        for (auto& texel : texels) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            texel = state;
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = side;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc = { 1, 0 };
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA pixels{ texels.data(), side * 4, 0 };
        ComPtr<ID3D11Texture2D> texture;
        if (FAILED(next->device->CreateTexture2D(&desc, &pixels, texture.GetAddressOf())) ||
            FAILED(next->device->CreateShaderResourceView(texture.Get(), nullptr, next->noiseView.GetAddressOf()))) {
            error = "Falha ao criar scene.noise no DirectX 11.";
            return false;
        }
    }
    if (usesOne) {
        const float one[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.SampleDesc = { 1, 0 };
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA pixels{ one, sizeof(one), 0 };
        ComPtr<ID3D11Texture2D> texture;
        if (FAILED(next->device->CreateTexture2D(&desc, &pixels, texture.GetAddressOf())) ||
            FAILED(next->device->CreateShaderResourceView(texture.Get(), nullptr, next->oneView.GetAddressOf()))) {
            error = "Falha ao criar a entrada one no DirectX 11.";
            return false;
        }
    }
    if (const auto& textures = sources.front().textures) {
        // Texturas do pack: PNG (ou outro formato do stb_image) decodificado uma vez para RGBA8 imutável.
        for (size_t k = 0; k < textures->size(); ++k) {
            const auto& entry = (*textures)[k];
            int width = 0, height = 0, channels = 0;
            stbi_uc* decoded = entry.bytes.size() <= 0x7fffffff
                                   ? stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(entry.bytes.data()),
                                                           static_cast<int>(entry.bytes.size()), &width, &height,
                                                           &channels, STBI_rgb_alpha)
                                   : nullptr;
            std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(decoded, stbi_image_free);
            if (!pixels || width < 1 || height < 1 || width > 2048 || height > 2048) {
                error = "Textura do pack ilegível ou maior que 2048x2048: " + entry.file;
                return false;
            }
            if (entry.flip) {
                // Origem embaixo (OpenGL): a última linha do arquivo vira a primeira do recurso.
                const size_t row = static_cast<size_t>(width) * 4;
                for (int top = 0, bottom = height - 1; top < bottom; ++top, --bottom)
                    std::swap_ranges(pixels.get() + top * row, pixels.get() + (top + 1) * row, pixels.get() + bottom * row);
            }
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = static_cast<UINT>(width);
            desc.Height = static_cast<UINT>(height);
            desc.MipLevels = desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc = { 1, 0 };
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            const D3D11_SUBRESOURCE_DATA data{ pixels.get(), desc.Width * 4, 0 };
            ComPtr<ID3D11Texture2D> texture;
            if (FAILED(next->device->CreateTexture2D(&desc, &data, texture.GetAddressOf())) ||
                FAILED(next->device->CreateShaderResourceView(texture.Get(), nullptr,
                                                              next->packTextureViews[k].GetAddressOf()))) {
                error = "Falha ao criar a textura do pack no DirectX 11: " + entry.file;
                return false;
            }
        }
    }
    if (usesScene) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(SceneConstants);
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(next->device->CreateBuffer(&desc, nullptr, next->sceneBuffer.GetAddressOf()))) {
            error = "Falha ao criar os dados da cena no DirectX 11.";
            return false;
        }
    }
    usesGeometryShadow |= next->lightsFog;
    // O contrato WorldFogSun no Fast3D ainda consome somente o prefixo legado.
    if (next->lightsFog || !next->sunCascadeCapacity)
        next->sunCascadeCapacity = 1;
    if (next->usesSunShadow && usesGeometryShadow) {
        error = "O pack deve escolher um mapa de sombras para o buffer LinkSpanSunShadow.";
        return false;
    }
    const auto fixedReady = std::chrono::steady_clock::now();
    if (usesGeometryShadow) {
        // Settings → Graphics → Shadow Map Resolution. Se a GPU recusar o tamanho pedido, cai no padrão de 1024.
        const auto capacity = static_cast<unsigned>(next->sunCascadeCapacity);
        const auto tile = SolarCascades::ClampTileSize(Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            CVAR_SETTING("ShadowMapResolution"), SolarCascades::TileSize));
        next->geometryShadow = std::make_unique<GeometryShadow>();
        bool created = next->geometryShadow->Initialize(*next->device.Get(), compile, 0, capacity, tile);
        if (!created && tile != SolarCascades::TileSize) {
            SPDLOG_WARN("Shader pack DX11 mapa de sombra solar de {} recusado; usando {}", tile,
                        SolarCascades::TileSize);
            next->geometryShadow = std::make_unique<GeometryShadow>();
            created = next->geometryShadow->Initialize(*next->device.Get(), compile, 0, capacity);
        }
        if (!created) {
            error = "Falha ao criar o mapa de sombras da geometria no DirectX 11.";
            return false;
        }
    }
    if (usesSunConstants) {
        D3D11_BUFFER_DESC desc{};
        // Um buffer maior também é válido para shaders legados; necessário
        // para passes estendidos em um pack misto que negocie somente um tile.
        desc.ByteWidth = sizeof(SolarCascades::Constants);
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if ((!next->usesSunShadow && !usesGeometryShadow) ||
            FAILED(next->device->CreateBuffer(&desc, nullptr, next->sunShadowBuffer.GetAddressOf()))) {
            error = "LinkSpanSunShadow exige um mapa de sombras e um buffer DirectX 11 compatível.";
            return false;
        }
    }
    if (usesTransmission) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(TransmissionConstants);
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(next->device->CreateBuffer(&desc, nullptr, next->transmissionBuffer.GetAddressOf()))) {
            error = "Falha ao criar os dados de transmissão no DirectX 11.";
            return false;
        }
        next->transmissionMaterials = sources.front().transmissionMaterials;
    }
    if (next->pointLightCapacity) {
        if (!next->transmissionMaterials || next->transmissionMaterials->empty()) {
            error = "LinkSpanPointShadow exige materiais de transmissão.";
            return false;
        }
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(sizeof(PointShadowConstants) * next->pointLightCapacity);
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        next->pointShadow = std::make_unique<GeometryShadow>();
        if (!next->pointShadow->Initialize(*next->device.Get(), compile,
                                           static_cast<unsigned>(next->pointLightCapacity)) ||
            FAILED(next->device->CreateBuffer(&desc, nullptr, next->pointShadowBuffer.GetAddressOf()))) {
            error = "Falha ao criar o mapa de sombra da luz pontual no DirectX 11.";
            return false;
        }
    }
    if (usesWeather) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(WeatherConstants);
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(next->device->CreateBuffer(&desc, nullptr, next->weatherBuffer.GetAddressOf()))) {
            error = "Falha ao criar LinkSpanWeather no DirectX 11.";
            return false;
        }
    }
    if (usesDecals) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(ImpactDecals::Frame);
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        const ImpactDecals::Frame empty{};
        const D3D11_SUBRESOURCE_DATA initial{ &empty, 0, 0 };
        if (FAILED(next->device->CreateBuffer(&desc, &initial, next->decalsBuffer.GetAddressOf()))) {
            error = "Falha ao criar LinkSpanDecals no DirectX 11.";
            return false;
        }
    }
    Fast::SetWorldMaterialMatcher(MatchWorldMaterial);
    Fast::SetWorldPostProcess(DrawWorld);
    Fast::SetWorldWaterReader(ReadWaterFrame);
    next->usesWaterRipple = reflectedWaterRipple;
    WaterRipple::SetWanted(reflectedWaterRipple);
    ImpactDecals::SetWanted(usesDecals);
    const auto done = Clock::now();
    next->activationStart = activationStart;
    next->activatedAt = done;
    activePass.store(std::move(next));
    const auto milliseconds = [](auto from, auto to) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
    };
    // Phases: pack shaders (compile/cache + driver creation), fixed shaders and buffers, shadow maps and the rest.
    SPDLOG_INFO("Shader pack DX11 ativação: {} ({} ms: passes {}, fixos {}, sombras e buffers {})", name,
                milliseconds(compileStart, done), milliseconds(compileStart, passesReady),
                milliseconds(passesReady, fixedReady), milliseconds(fixedReady, done));
    return true;
#else
    error = "Este build não inclui DirectX 11.";
    return false;
#endif
}

void RecordPresentedWorldFrame() {
#ifdef _WIN32
    const auto pass = activePass.load();
    if (!pass)
        return;
    const bool firstFrame = !pass->firstFrameLogged.load(std::memory_order_relaxed) &&
                            !pass->firstFrameLogged.exchange(true, std::memory_order_relaxed);
    const bool firstPackFrame = pass->firstDrawSubmitted.load(std::memory_order_relaxed) &&
                                !pass->firstPackFrameLogged.load(std::memory_order_relaxed) &&
                                !pass->firstPackFrameLogged.exchange(true, std::memory_order_relaxed);
    if (firstFrame || firstPackFrame) {
        const auto now = std::chrono::steady_clock::now();
        const auto total = std::chrono::duration<double, std::milli>(now - pass->activationStart).count();
        const auto after = std::chrono::duration<double, std::milli>(now - pass->activatedAt).count();
        SPDLOG_INFO("Shader pack DX11 apresentação: {} primeiro_frame={} primeiro_frame_com_pack={} "
                    "desde_inicio_ativacao_ms={:.2f} apos_ativacao_ms={:.2f}",
                    pass->name, firstFrame, firstPackFrame, total, after);
    }
#endif
}

void DisableWorldPass() {
#ifdef _WIN32
    activePass.store(nullptr);
    WaterRipple::SetWanted(false);
    ImpactDecals::SetWanted(false);
#endif
}

} // namespace ShaderPacks

namespace {
#ifdef _WIN32
uint32_t QueueWorldPass(GraphicsContext* gfxCtx, Fast::WorldPostProcessPhase phase) {
    const auto pass = activePass.load();
    if (!pass || pass->passes.empty())
        return 0;
    const bool camera =
        pass->cameraBuffer.Get() != nullptr || (pass->geometryShadow && pass->sunCascadeCapacity > 1);
    const auto ticket = QueueWorldSnapshot(gfxCtx, pass, camera, pass->sceneBuffer.Get() != nullptr,
                                          pass->usesSunShadow, pass->geometryShadow != nullptr, pass->lightsFog,
                                          pass->pointShadow ? pass->transmissionMaterials.get() : nullptr,
                                          pass->pointLightCapacity);
    if (!ticket)
        return 0;
    auto* command = gfxCtx->overlay.p++;
    command->words.w0 = (uint32_t(FAST_GFX_WORLD_POSTPROCESS) << 24) | static_cast<uint32_t>(phase);
    command->words.w1 = ticket;
    return ticket;
}
#endif
} // namespace

extern "C" void ShaderPacks_DrawWorldPass(struct GraphicsContext* gfxCtx) {
#ifdef _WIN32
    QueueWorldPass(gfxCtx, Fast::WorldPostProcessPhase::Draw);
#endif
}

extern "C" uint32_t ShaderPacks_PrepareWorldPass(struct GraphicsContext* gfxCtx) {
#ifdef _WIN32
    return QueueWorldPass(gfxCtx, Fast::WorldPostProcessPhase::Prepare);
#else
    return 0;
#endif
}

extern "C" void ShaderPacks_ComposeWorldPass(struct GraphicsContext* gfxCtx, uint32_t ticket) {
#ifdef _WIN32
    // O pack pode desligar/trocar com uma lista pendente. Validar na emissão
    // e na execução do callback; não criar um segundo snapshot aqui.
    const auto pass = activePass.load();
    if (!ticket || !pass || pass->passes.empty())
        return;
    {
        const std::lock_guard lock(worldSnapshotsMutex);
        const auto& snapshot = worldSnapshots[ticket % worldSnapshots.size()];
        if (snapshot.ticket != ticket || snapshot.pass.lock() != pass)
            return;
    }
    auto* command = gfxCtx->overlay.p++;
    command->words.w0 = (uint32_t(FAST_GFX_WORLD_POSTPROCESS) << 24) | FAST_WORLD_COMPOSE;
    command->words.w1 = ticket;
#endif
}

extern "C" int ShaderPacks_WorldPassActive(void) {
#ifdef _WIN32
    const auto pass = activePass.load();
    return pass && !pass->passes.empty() ? 1 : 0;
#else
    return 0;
#endif
}

// Ganho da parcela ambiente nativa (LightContext_NewLights) nos interiores: o Complementary escurece
// a luz ambiente pelo lmCoord.y, que a luz do céu automática não alcança. Só com pack ativo, céu
// automático (provider manda sky_light < 0) e cena fechada (abertura 0, mesma regra do b10, que
// já trata o Market como aberto). CVar gShaderPack.IndoorAmbientScale (0..1, padrão 1 = sem efeito).
extern "C" float ShaderPacks_NativeAmbientScale(void) {
#ifdef _WIN32
    if (!gPlayState || providerParameters[3].load(std::memory_order_relaxed) >= 0.0f)
        return 1.0f;
    const auto pass = activePass.load();
    if (!pass || pass->passes.empty())
        return 1.0f;
    if (NativeSkyFacts(*gPlayState)[0] > SkyOpennessClosed)
        return 1.0f;
    const float scale = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetFloat(
        "gShaderPack.IndoorAmbientScale", 1.0f);
    return std::isfinite(scale) ? std::clamp(scale, 0.0f, 1.0f) : 1.0f;
#else
    return 1.0f;
#endif
}

extern "C" void ShaderPacks_CaptureWaterSurface(struct GraphicsContext* gfxCtx, int enable) {
#ifdef _WIN32
    // Sem pack ou com a CVar desligada a lista fica igual a de antes: nenhum comando novo.
    // O END so sai depois de um BEGIN emitido (thread do jogo); o Run do interpretador
    // tambem desarma a cada quadro, entao um BEGIN sem par nao vaza para o seguinte.
    static bool scopeOpen = false;
    uint32_t ticket = 0;
    if (enable) {
        const auto pass = activePass.load();
        if (!pass || !pass->sceneBuffer || !pass->cameraBuffer || !gPlayState)
            return;
        const auto cvars = Ship::Context::GetRawInstance()->GetConsoleVariables();
        const int scene = cvars->GetInteger("gDeveloperTools.WaterDisplaceScene", -1);
        if (!cvars->GetInteger("gDeveloperTools.WaterDisplace", 0) || (scene >= 0 && scene != gPlayState->sceneNum))
            return;
        ticket = QueueWorldSnapshot(gfxCtx, pass, false, true, false, false, false, nullptr, 0);
        if (!ticket)
            return;
        scopeOpen = true;
    } else {
        if (!scopeOpen)
            return;
        scopeOpen = false;
    }
    auto* command = gfxCtx->polyXlu.p++;
    command->words.w0 = (FAST_GFX_WORLD_POSTPROCESS << 24) | (enable ? FAST_WORLD_WATER_BEGIN : FAST_WORLD_WATER_END);
    command->words.w1 = ticket;
#endif
}

extern "C" void ShaderPacks_CaptureWorldGeometry(struct GraphicsContext* gfxCtx, int enable) {
#ifdef _WIN32
    const auto pass = activePass.load();
    const auto* play = gPlayState;
    if (!pass || !play)
        return;
    const auto scene = play->sceneNum;
    const bool materialScene = pass->transmissionMaterials &&
                               std::any_of(pass->transmissionMaterials->begin(), pass->transmissionMaterials->end(),
                                           [scene](const auto& material) { return material.scene == scene; });
    const bool captureSolar = pass->geometryShadow && UsesSolarGeometry(*play, pass->lightsFog);
    if ((pass->pointShadow && materialScene) || captureSolar) {
        auto* command = gfxCtx->polyOpa.p++;
        command->words.w0 =
            (FAST_GFX_WORLD_POSTPROCESS << 24) | (enable ? FAST_WORLD_GEOMETRY_BEGIN : FAST_WORLD_GEOMETRY_END);
        command->words.w1 = 0;
    }
    // Shared generic XLU classification: shadows and world-depth supplementation are independent.
    // Binary alpha is a material heuristic, not a semantic guarantee that a draw is vegetation.
    const auto cvars = Ship::Context::GetRawInstance()->GetConsoleVariables();
    const bool foliageShadows = captureSolar &&
                                cvars->GetInteger(CVAR_DEVELOPER_TOOLS("FoliageShadows"), 1) != 0;
    const bool foliageDepth = pass->usesDepth &&
                              cvars->GetInteger(CVAR_DEVELOPER_TOOLS("FoliageDepth"), 1) != 0;
    if (foliageShadows || foliageDepth) {
        auto* command = gfxCtx->polyXlu.p++;
        command->words.w0 =
            (FAST_GFX_WORLD_POSTPROCESS << 24) | (enable ? FAST_WORLD_GEOMETRY_BEGIN : FAST_WORLD_GEOMETRY_END);
        command->words.w1 = FAST_WORLD_GEOMETRY_CUTOUT_ONLY;
    }
    if (materialScene) {
        auto* command = gfxCtx->polyXlu.p++;
        command->words.w0 =
            (FAST_GFX_WORLD_POSTPROCESS << 24) | (enable ? FAST_WORLD_TRANSMISSION_BEGIN : FAST_WORLD_TRANSMISSION_END);
        command->words.w1 = scene;
    }
#endif
}

// GFX-UNIFY-001: luz principal do mapa = sol de dia, lua oposta a noite.
// Conserva o teste diurno; MoonShadowDebug 1 sincroniza dayTime % 4 com o pack de teste.
// O modo 1 restaura as sombras do jogo. KeepVanillaShadows continua tendo precedencia.
extern "C" int32_t ShaderPacks_ReplacesVanillaShadows(void) {
#ifdef _WIN32
    const auto* play = gPlayState;
    if (!play || !UsesSolarGeometry(*play))
        return 0;
    const auto pass = activePass.load();
    if (!pass || !pass->geometryShadow)
        return 0;
    if (Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_DEVELOPER_TOOLS("KeepVanillaShadows"),
                                                                           0))
        return 0;
    const auto& sun = play->envCtx.sunPos;
    const float length = std::sqrt(sun.x * sun.x + sun.y * sun.y + sun.z * sun.z);
    if (length > 1.0f && sun.y > 0.035f * length)
        return 1;
    return UseMoonShadow({ sun.x, sun.y, sun.z, 0.0f }, gSaveContext.dayTime) ? 1 : 0;
#else
    return 0;
#endif
}
