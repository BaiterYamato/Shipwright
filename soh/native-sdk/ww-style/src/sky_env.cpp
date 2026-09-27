// Base compartilhada do céu (WWSkyEnv.cpp do fork): paleta do mar do Wind Waker, hora pelo sol, sinal suavizado de
// nuvens/tempestade tirado do EnvironmentContext (o céu reage ao clima sem autoria por cena), vento, linha do
// horizonte e o debug de tela dividida.
#include <algorithm>
#include <cmath>

#include "sky.h"

namespace WWStyle {
namespace {

constexpr float kPi = 3.14159265f;

// Paleta do estágio do mar (stage.dzs Pale/Virt, segundo o fork) e a agenda l_time_attribute: seis vagas — aurora,
// manhã, meio-dia, tarde, crepúsculo, noite —, e o clima leva o conjunto limpo ao de chuva (colpat 1).
struct SkySlot {
    uint8_t sky[3];
    uint8_t kasumi[3];
    uint8_t usoUmi[3];
    uint8_t kumo[3];
    uint8_t kumoCenter[3];
};

constexpr SkySlot kSeaPalette[2][6] = {
    { // limpo (colpat 0)
      { { 79, 70, 78 }, { 180, 142, 121 }, { 113, 90, 73 }, { 74, 71, 79 }, { 108, 96, 92 } },            // aurora
      { { 180, 188, 201 }, { 241, 230, 220 }, { 193, 190, 197 }, { 247, 232, 216 }, { 255, 241, 223 } }, // manhã
      { { 80, 120, 255 }, { 163, 210, 255 }, { 80, 120, 255 }, { 255, 255, 255 }, { 255, 255, 255 } },   // meio-dia
      { { 219, 154, 99 }, { 236, 202, 137 }, { 200, 160, 100 }, { 233, 177, 108 }, { 208, 155, 98 } },   // tarde
      { { 100, 80, 78 }, { 231, 199, 150 }, { 103, 88, 79 }, { 96, 85, 90 }, { 117, 94, 91 } },          // crepúsculo
      { { 10, 50, 85 }, { 60, 75, 100 }, { 0, 49, 74 }, { 52, 86, 120 }, { 58, 100, 134 } } },           // noite
    { // chuva (colpat 1)
      { { 75, 71, 68 }, { 124, 112, 99 }, { 74, 73, 70 }, { 74, 69, 65 }, { 100, 87, 75 } },
      { { 120, 133, 127 }, { 164, 181, 182 }, { 122, 135, 127 }, { 161, 160, 150 }, { 176, 182, 167 } },
      { { 105, 130, 119 }, { 143, 161, 164 }, { 85, 107, 100 }, { 160, 180, 165 }, { 170, 190, 175 } },
      { { 127, 116, 89 }, { 78, 77, 61 }, { 71, 69, 52 }, { 130, 112, 84 }, { 108, 97, 82 } },
      { { 108, 99, 82 }, { 68, 67, 51 }, { 68, 65, 52 }, { 108, 97, 74 }, { 93, 87, 72 } },
      { { 21, 35, 33 }, { 33, 46, 42 }, { 15, 45, 46 }, { 50, 55, 56 }, { 45, 53, 59 } } },
};

// As vagas ao longo da descida do sol, da mais clara para baixo; entre duas paradas a cor vai da de cima para a de
// baixo conforme o sol desce. As metades do dia só mudam as vagas quentes (aurora/manhã subindo, tarde/crepúsculo
// descendo).
struct SunStop {
    float minHeight;
    int slot;
};
constexpr SunStop kStopsPM[] = { { 0.50f, 2 }, { 0.15f, 3 }, { 0.00f, 4 }, { -0.15f, 5 } };
constexpr SunStop kStopsAM[] = { { 0.50f, 2 }, { 0.15f, 1 }, { 0.00f, 0 }, { -0.15f, 5 } };

uint8_t LerpU8(uint8_t a, uint8_t b, float t) {
    return static_cast<uint8_t>(a + ((b - a) * t));
}

void SunSlots(float h, bool pm, int& slotA, int& slotB, float& u) {
    const SunStop* s = pm ? kStopsPM : kStopsAM;
    constexpr int n = 4;
    if (h >= s[0].minHeight) {
        slotA = slotB = s[0].slot;
        u = 0.0f;
        return;
    }
    for (int i = 1; i < n; ++i) {
        if (h >= s[i].minHeight) {
            slotA = s[i].slot;
            slotB = s[i - 1].slot;
            u = (h - s[i].minHeight) / (s[i - 1].minHeight - s[i].minHeight);
            return;
        }
    }
    slotA = slotB = s[n - 1].slot;
    u = 0.0f;
}

// Nublado suavizado: as nuvens chegam rápido e saem devagar (o crossfade do skybox do jogo leva ~5 s nos dois
// sentidos, o que volta ao azul antes do último raio). Avança uma vez por frame de jogo.
float sEasedCloudiness = 0.0f;
uint32_t sLastFrame = 0;
PlayStamp sWeatherStamp;

} // namespace

bool IsNewPlay(PlayStamp& stamp, const PlayState* play) {
    const auto frames = static_cast<uint32_t>(play->state.frames);
    const bool fresh = play != stamp.play || play->sceneNum != stamp.scene || frames < stamp.frames;
    stamp.play = play;
    stamp.scene = play->sceneNum;
    stamp.frames = frames;
    return fresh;
}

SkyWeather SampleWeather(PlayState* play) {
    const EnvironmentContext& env = play->envCtx;
    // unk_17/unk_18: linha de clima atual/seguinte do skybox (0 = vr_fine, 1 = vr_cloud). Com a troca em curso
    // (unk_19 >= 3) o skyboxBlend é a rampa fine<->cloud; parado, ele mistura dia/noite e não conta.
    const float cur = env.unk_17 == 1 ? 1.0f : 0.0f;
    const float next = env.unk_18 == 1 ? 1.0f : 0.0f;
    float cloudiness = cur;
    if (env.unk_19 >= 3 && cur != next) cloudiness = cur + ((next - cur) * (env.skyboxBlend / 255.0f));
    // unk_EE[1] é a contagem de gotas já suavizada; LIGHTNING_MODE_LAST ainda deve um raio.
    const float rain = std::clamp(env.unk_EE[1] / 30.0f, 0.0f, 1.0f);
    const bool lightning = env.lightningMode == LIGHTNING_MODE_ON || env.lightningMode == LIGHTNING_MODE_LAST;
    const float storm = lightning ? 1.0f : rain;
    const float target = std::clamp(std::max(cloudiness, storm), 0.0f, 1.0f);
    // Carga nova começa no alvo: sem suavizar a partir do céu da cena ou do arquivo anterior.
    if (IsNewPlay(sWeatherStamp, play)) {
        sEasedCloudiness = target;
        sLastFrame = static_cast<uint32_t>(play->state.frames);
    }
    if (static_cast<uint32_t>(play->state.frames) != sLastFrame) {
        sLastFrame = static_cast<uint32_t>(play->state.frames);
        const float rate = target > sEasedCloudiness ? 0.06f : 0.012f; // ~1 s para fechar, ~10 s para abrir
        sEasedCloudiness += (target - sEasedCloudiness) * rate;
        if (std::fabs(target - sEasedCloudiness) < 0.001f) sEasedCloudiness = target;
    }
    SkyWeather w{};
    w.cloudiness = sEasedCloudiness;
    w.storm = storm;
    w.fogColor[0] = env.lightSettings.fogColor[0];
    w.fogColor[1] = env.lightSettings.fogColor[1];
    w.fogColor[2] = env.lightSettings.fogColor[2];
    return w;
}

// Elevação do sol pela fórmula do próprio OoT (z_kankyo.c, sunPos.y = cos(dayTime - 0x8000)): +1 ao meio-dia, 0 no
// horizonte, -1 à meia-noite. A paleta segue o sol visível, não o relógio. O Math_CosS do jogo é tabelado; aqui é
// cos direto.
float SunHeight(const SaveContext* save) {
    const auto angle = static_cast<int16_t>(save->dayTime - 0x8000);
    return std::cos(angle * (2.0f * kPi / 65536.0f));
}

void SampleColors(const SaveContext* save, const SkyWeather& weather, SkyColors& out) {
    int slotA = 0;
    int slotB = 0;
    float u = 0.0f;
    SunSlots(SunHeight(save), save->dayTime >= 0x8000, slotA, slotB, u);
    // Mistura pela elevação dentro de cada conjunto de clima, depois do limpo para a chuva pelo nublado.
    const float c = weather.cloudiness;
    const SkySlot* clear = kSeaPalette[0];
    const SkySlot* rain = kSeaPalette[1];
    for (int i = 0; i < 3; ++i) {
        out.sky[i] = LerpU8(LerpU8(clear[slotA].sky[i], clear[slotB].sky[i], u),
                            LerpU8(rain[slotA].sky[i], rain[slotB].sky[i], u), c);
        out.kasumi[i] = LerpU8(LerpU8(clear[slotA].kasumi[i], clear[slotB].kasumi[i], u),
                               LerpU8(rain[slotA].kasumi[i], rain[slotB].kasumi[i], u), c);
        out.usoUmi[i] = LerpU8(LerpU8(clear[slotA].usoUmi[i], clear[slotB].usoUmi[i], u),
                               LerpU8(rain[slotA].usoUmi[i], rain[slotB].usoUmi[i], u), c);
        out.kumo[i] = LerpU8(LerpU8(clear[slotA].kumo[i], clear[slotB].kumo[i], u),
                             LerpU8(rain[slotA].kumo[i], rain[slotB].kumo[i], u), c);
        out.kumoCenter[i] = LerpU8(LerpU8(clear[slotA].kumoCenter[i], clear[slotB].kumoCenter[i], u),
                                   LerpU8(rain[slotA].kumoCenter[i], rain[slotB].kumoCenter[i], u), c);
    }
}

// Noite limpa fixa, para a tela de arquivos (o vanilla a mostra sempre à noite).
void NightColors(SkyColors& out) {
    const SkySlot& n = kSeaPalette[0][5];
    for (int i = 0; i < 3; ++i) {
        out.sky[i] = n.sky[i];
        out.kasumi[i] = n.kasumi[i];
        out.usoUmi[i] = n.usoUmi[i];
        out.kumo[i] = n.kumo[i];
        out.kumoCenter[i] = n.kumoCenter[i];
    }
}

// dKyw_get_wind_vecpow para o OoT: direção XZ unitária e a força do vento na faixa 0,3/0,6/0,9 do Wind Waker. Sem
// vento, uma brisa fixa para o céu sempre andar, como no mar do Wind Waker.
SkyWind Wind(const PlayState* play) {
    float dx = play->envCtx.windDirection.x;
    float dz = play->envCtx.windDirection.z;
    float len = std::sqrt((dx * dx) + (dz * dz));
    if (len < 1.0f) {
        dx = 1.0f;
        dz = 0.3f;
        len = std::sqrt((dx * dx) + (dz * dz));
    }
    const float p = std::clamp(play->envCtx.windSpeed / 255.0f, 0.0f, 1.0f);
    return { dx / len, dz / len, 0.3f + (0.6f * p) };
}

// O Wind Waker move a vrbox inteira (cúpula, mar falso, faixa de nuvens) como uma peça: o gradiente e a faixa
// dividem esta linha. Paralaxe 0 segue a câmera, 1 fica presa à altura do mundo.
float HorizonYForEye(const Settings& cfg, float eyeY) {
    return (eyeY * (1.0f - cfg.horizonParallax)) + cfg.horizonHeight;
}

// Só sobre o céu de verdade do mundo aberto, a mesma condição do desenho do skybox no Play_Draw.
bool SkyAvailable(const PlayState* play) {
    return play != nullptr && play->skyboxId == SKYBOX_NORMAL_SKY && !play->envCtx.skyboxDisabled;
}

// Debug de tela dividida: o céu do mod só na metade esquerda, o skybox vanilla (desenhado por baixo) à direita.
// Cada desenho restaura a tela cheia no fim, para o mundo desenhado depois nunca sair recortado.
void SplitBegin(const Settings& cfg, Gfx*& g) {
    if (cfg.debugSkySplit) gDPSetScissor(g++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH / 2, SCREEN_HEIGHT);
}

void SplitEnd(const Settings& cfg, Gfx*& g) {
    if (cfg.debugSkySplit) gDPSetScissor(g++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
}

const void* SkyMatrix(Mod& mod, float x, float y, float z) {
    ShipOotRenderFrameInfoV1 info{sizeof(info)};
    if (mod.render->get_frame_info(&info) != SHIP_NATIVE_OK) return nullptr;
    // Filho por camera_epoch, como o SkyboxDraw_Draw: a translação que segue a câmera interpola entre os frames de
    // 20 Hz mas pula no corte de câmera, em vez de o céu deslizar um frame.
    const void* mtx = nullptr;
    if (mod.render->interpolation_begin(nullptr, static_cast<int32_t>(info.camera_epoch)) != SHIP_NATIVE_OK)
        return nullptr;
    if (mod.render->matrix_push() == SHIP_NATIVE_OK) {
        if (mod.render->matrix_translate_new(x, y, z) != SHIP_NATIVE_OK ||
            mod.render->export_current_matrix(&mtx) != SHIP_NATIVE_OK)
            mtx = nullptr;
        mod.render->matrix_pop();
    }
    mod.render->interpolation_end();
    return mtx;
}

float SkyTicks(Mod& mod) {
    ShipOotRenderFrameInfoV1 info{sizeof(info)};
    const float dt =
        mod.render->get_frame_info(&info) == SHIP_NATIVE_OK && info.delta_seconds > 0.0f ? info.delta_seconds : 0.05f;
    return dt * 30.0f;
}

} // namespace WWStyle
