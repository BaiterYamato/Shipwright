// Cúpula com gradiente e céu estrelado (WWSkyGradient.cpp e WWNightSky.cpp do fork).
//
// O céu do Wind Waker não é textura: é uma pilha de malhas com cor por vértice tingidas pelas cores agendadas. A
// cúpula aqui reproduz o perfil de três zonas medido (usoUmi abaixo do horizonte, a névoa kasumi do horizonte até
// +6,6° e o céu acima), recolorida a cada frame pela paleta. As estrelas são o dKankyo_star_Packet (via noclip):
// 16 da constelação fixa e o resto numa espiral em volta da câmera, com o brilho pela largura de uma onda
// compartilhada. Tudo segue a câmera (sem paralaxe) e desenha sem z logo depois do skybox, antes do sol, da lua e
// do mundo.
#include <algorithm>
#include <cmath>
#include <iterator>

#include "sky.h"

namespace WWStyle {
namespace {

constexpr float kPi = 3.14159265f;

// --- cúpula ---

// A faixa de névoa vai de cheia no horizonte a zero em +6,6° (alfa assado na malha do Wind Waker); fixa, como lá.
constexpr float kKasumiTopDeg = 6.6f;
// Linhas de latitude desiguais, para a faixa fina da névoa e a linha do horizonte terem vértices.
constexpr float kDomeElevations[] = {
    -90.0f, -40.0f, -15.0f, -5.0f, -1.0f, 0.0f, 1.65f, 3.3f, 4.95f, 6.6f, 12.0f, 20.0f, 35.0f, 60.0f, 90.0f,
};
constexpr int kDomeRows = static_cast<int>(std::size(kDomeElevations));
constexpr int kDomeSegs = 24;
constexpr float kDomeRadius = 6000.0f;
constexpr int kDomeVerts = (kDomeRows - 1) * kDomeSegs * 6;
constexpr int kChunkVerts = 30; // cabe no cache de 32 vértices; 10 triângulos por carga

Vtx sDomeVtx[kDomeVerts];
bool sDomeBuilt = false;
GfxList sDomeList;

void SetDomePos(Vtx& v, float x, float y, float z) {
    v = Vtx{};
    v.v.ob[0] = static_cast<int16_t>(x);
    v.v.ob[1] = static_cast<int16_t>(y);
    v.v.ob[2] = static_cast<int16_t>(z);
}

void BuildDome() {
    int idx = 0;
    for (int row = 0; row + 1 < kDomeRows; ++row) {
        const float phi0 = kDomeElevations[row] * (kPi / 180.0f);
        const float phi1 = kDomeElevations[row + 1] * (kPi / 180.0f);
        const float y0 = kDomeRadius * std::sin(phi0);
        const float rc0 = kDomeRadius * std::cos(phi0);
        const float y1 = kDomeRadius * std::sin(phi1);
        const float rc1 = kDomeRadius * std::cos(phi1);
        for (int seg = 0; seg < kDomeSegs; ++seg) {
            const float lam0 = 2.0f * kPi * (static_cast<float>(seg) / kDomeSegs);
            const float lam1 = 2.0f * kPi * (static_cast<float>(seg + 1) / kDomeSegs);
            const float c0 = std::cos(lam0);
            const float s0 = std::sin(lam0);
            const float c1 = std::cos(lam1);
            const float s1 = std::sin(lam1);
            // Dois triângulos por quadra; a cúpula desenha dos dois lados, então a ordem não importa.
            SetDomePos(sDomeVtx[idx++], rc0 * c0, y0, rc0 * s0);
            SetDomePos(sDomeVtx[idx++], rc1 * c0, y1, rc1 * s0);
            SetDomePos(sDomeVtx[idx++], rc1 * c1, y1, rc1 * s1);
            SetDomePos(sDomeVtx[idx++], rc0 * c0, y0, rc0 * s0);
            SetDomePos(sDomeVtx[idx++], rc1 * c1, y1, rc1 * s1);
            SetDomePos(sDomeVtx[idx++], rc0 * c1, y0, rc0 * s1);
        }
    }
    sDomeBuilt = true;
}

uint8_t ClampU8(float v) {
    return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f));
}

void ZoneColor(float elevDeg, const uint8_t sky[3], const uint8_t kasumi[3], const uint8_t usoUmi[3], uint8_t out[3]) {
    for (int i = 0; i < 3; ++i) {
        if (elevDeg < 0.0f) {
            out[i] = usoUmi[i];
        } else if (elevDeg >= kKasumiTopDeg) {
            out[i] = sky[i];
        } else {
            const float t = elevDeg / kKasumiTopDeg; // linear, como o alfa assado da malha
            out[i] = static_cast<uint8_t>(kasumi[i] + ((sky[i] - kasumi[i]) * t));
        }
    }
}

// Recolore a cúpula pelas três cores de zona, com o ajuste de brilho do menu.
void ColorDome(const SkyColors& colors, float brightness) {
    uint8_t sky[3];
    uint8_t kasumi[3];
    uint8_t usoUmi[3];
    for (int i = 0; i < 3; ++i) {
        sky[i] = ClampU8(colors.sky[i] * brightness);
        kasumi[i] = ClampU8(colors.kasumi[i] * brightness);
        usoUmi[i] = ClampU8(colors.usoUmi[i] * brightness);
    }
    for (Vtx& v : sDomeVtx) {
        const float sinElev = std::clamp(static_cast<float>(v.v.ob[1]) / kDomeRadius, -1.0f, 1.0f);
        uint8_t col[3];
        ZoneColor(std::asin(sinElev) * (180.0f / kPi), sky, kasumi, usoUmi, col);
        v.v.cn[0] = col[0];
        v.v.cn[1] = col[1];
        v.v.cn[2] = col[2];
        v.v.cn[3] = 255;
    }
}

// O centro da cúpula fica na linha do horizonte compartilhada, na horizontal do olho. `split` liga o debug de tela
// dividida (a tela de arquivos do fork não divide).
void EmitDome(Mod& mod, float eyeX, float horizonY, float eyeZ, bool split) {
    const void* mtx = SkyMatrix(mod, eyeX, horizonY, eyeZ);
    if (mtx == nullptr) {
        ++mod.stats.skyFailures;
        return;
    }
    Gfx* const start = sDomeList.Begin(16 + ((kDomeVerts / kChunkVerts + 1) * 6));
    Gfx* g = start;
    if (split) SplitBegin(mod.cfg, g);
    gSPMatrix(g++, const_cast<void*>(mtx), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gDPPipeSync(g++);
    // Sem luz, cor por vértice, dos dois lados (vista de dentro), sem névoa.
    gSPClearGeometryMode(g++, G_LIGHTING | G_CULL_FRONT | G_CULL_BACK | G_FOG);
    gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    // Sem z: a cúpula cobre o skybox todo; estrelas, sol, lua e mundo vêm depois e pintam por cima. Alfa 255.
    gDPSetRenderMode(g++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    for (int first = 0; first < kDomeVerts; first += kChunkVerts) {
        const int verts = std::min(kChunkVerts, kDomeVerts - first);
        __gSPVertex(g++, reinterpret_cast<uintptr_t>(&sDomeVtx[first]), verts, 0);
        for (int t = 0; t + 6 <= verts; t += 6) gSP2Triangles(g++, t + 0, t + 1, t + 2, 0, t + 3, t + 4, t + 5, 0);
    }
    if (split) SplitEnd(mod.cfg, g);
    gSPEndDisplayList(g++);
    if (mod.render->draw_native_display_list(start, LINKSPAN_OOT_RENDER_OPAQUE) == SHIP_NATIVE_OK)
        ++mod.stats.skyDomes;
    else
        ++mod.stats.skyFailures;
}

// --- estrelas ---

// Escalas por grupo: sem paralaxe, a escala some do tamanho na tela. A constelação (13k–36k unidades) desce para
// caber no plano distante de 12800; as estrelas pequenas sobem, senão os vértices s16 caem no mesmo inteiro e a
// estrela vira triângulo degenerado.
constexpr float kConstellationScale = 0.25f;
constexpr float kSmallStarScale = 25.0f;
constexpr int kMaxStars = 1000;
constexpr int kStarsPerChunk = 5; // 5 × 6 = 30 vértices

// Constelação fixa do Wind Waker ("hokuto"), deslocamentos relativos à câmera antes da escala.
constexpr float kHokutoPos[16][3] = {
    { 13000, 10500, -16000 }, { 9400, 9800, -12646 }, { 10200, 11800, -13525 }, { 10300, 13450, -13525 },
    { 15000, 18400, -16162 }, { 12500, 19800, -15000 }, { 9179, 17200, -14404 }, { 9500, 9800, -12646 },
    { -7421, 31005, 18798 },  { -10937, 28000, 15000 }, { -10000, 24902, 18400 }, { -9400, 22500, 15900 },
    { -9179, 21300, 14300 },  { -10300, 22000, 21000 }, { -16000, 25500, 20000 }, { 0, 30000, 19000 },
};
// Tons: quase todas azul-claro; as estrelas 6 e 8 rosa.
constexpr uint8_t kStarCol[4][4] = {
    { 0xDC, 0xE8, 0xFF, 0xFF }, { 0xFF, 0xC8, 0xC8, 0xFF }, { 0xFF, 0xFF, 0xC8, 0xFF }, { 0xC8, 0xC8, 0xFF, 0xFF },
};

Vtx sStarVtx[kMaxStars * 6];
GfxList sStarList;
double sAnimCounter = 0.0; // animCounter do Wind Waker; a onda é sin(isto)
float sRot = 0.0f;         // giro do campo em graus
float sStarAmount = 0.0f;  // quantidade suavizada pela hora
PlayStamp sStarStamp;

struct V3 {
    float x;
    float y;
    float z;
};
V3 Add(V3 a, V3 b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}
V3 Scale(V3 a, float s) {
    return { a.x * s, a.y * s, a.z * s };
}
V3 Cross(V3 a, V3 b) {
    return { (a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z), (a.x * b.y) - (a.y * b.x) };
}
V3 Norm(V3 a) {
    const float len = std::sqrt((a.x * a.x) + (a.y * a.y) + (a.z * a.z));
    return len < 1e-6f ? V3{ 0.0f, 0.0f, 0.0f } : V3{ a.x / len, a.y / len, a.z / len };
}

// Estrelas somem com o sol acima do horizonte e enchem bem abaixo dele, juntas com a parada noturna da paleta.
float StarAmountForSun(float sunHeight) {
    constexpr float fadeTop = 0.05f;
    constexpr float fadeBottom = -0.15f;
    if (sunHeight >= fadeTop) return 0.0f;
    if (sunHeight <= fadeBottom) return 1.0f;
    return (fadeTop - sunHeight) / (fadeTop - fadeBottom);
}

void WriteStar(int base, V3 center, float half, const V3 corner[3], const uint8_t col[4]) {
    // Um triângulo em +half e outro em -half: a estrela de seis pontas. half ~0 some (o piscar das pequenas).
    for (int i = 0; i < 6; ++i) {
        const V3 p = Add(center, Scale(corner[i % 3], i < 3 ? half : -half));
        Vtx& v = sStarVtx[base + i];
        v = Vtx{};
        v.v.ob[0] = static_cast<int16_t>(p.x);
        v.v.ob[1] = static_cast<int16_t>(p.y);
        v.v.ob[2] = static_cast<int16_t>(p.z);
        v.v.cn[0] = col[0];
        v.v.cn[1] = col[1];
        v.v.cn[2] = col[2];
        v.v.cn[3] = col[3];
    }
}

void BuildStars(int starCount, const View& view, float alphaMul) {
    const auto animWave = static_cast<float>(std::sin(sAnimCounter));
    // Base da câmera para os billboards.
    const V3 eye{ view.eye.x, view.eye.y, view.eye.z };
    const V3 fwd = Norm({ view.lookAt.x - eye.x, view.lookAt.y - eye.y, view.lookAt.z - eye.z });
    const V3 right = Norm(Cross(fwd, { view.up.x, view.up.y, view.up.z }));
    const V3 up = Norm(Cross(right, fwd));
    // Cantos da estrela no plano da vista, girados pelo giro do campo.
    const float rotRad = sRot * (kPi / 180.0f);
    const float cr = std::cos(rotRad);
    const float sr = std::sin(rotRad);
    constexpr float base2d[3][2] = { { 0.0f, 0.9f }, { 0.9f, -0.45f }, { -0.9f, -0.45f } };
    V3 corner[3];
    for (int k = 0; k < 3; ++k) {
        const float x = (base2d[k][0] * cr) - (base2d[k][1] * sr);
        const float y = (base2d[k][0] * sr) + (base2d[k][1] * cr);
        corner[k] = Add(Scale(right, x), Scale(up, y));
    }
    float radius = 0.0f;
    float angle = -kPi;
    float angleIncr = 0.0f;
    for (int i = 0; i < starCount; ++i) {
        V3 local;
        float half;
        if (i < 16) {
            half = (i < 8 ? 190.0f : 290.0f) + animWave;
            local = { kHokutoPos[i][0], kHokutoPos[i][1], kHokutoPos[i][2] };
        } else {
            // Espiral e o piscar pelo tamanho (dobra acima de 1, para piscarem em tempos diferentes).
            float scale = animWave + (0.066f * (i & 0x0F));
            if (scale > 1.0f) scale = 1.0f - (scale - 1.0f);
            half = scale;
            const float radiusXZ = 1.0f - (radius / 202.0f);
            local = { radiusXZ * -300.0f * std::sin(angle), radius + 45.0f, radiusXZ * 300.0f * std::cos(angle) };
            angle += angleIncr;
            angleIncr += 0x09C4 * (kPi * 2.0f / 65536.0f);
            radius += 1.0f + (3.0f * (radius / 8000000.0f));
            if (radius > 200.0f) radius = (20.0f * i) / 1000.0f;
        }
        int whichColor = 0;
        if (i == 6 || i == 8) {
            whichColor = 1;
        } else if ((i & 0x3F) == 0) {
            whichColor = (i >> 4) & 0x03;
        }
        const uint8_t col[4] = { kStarCol[whichColor][0], kStarCol[whichColor][1], kStarCol[whichColor][2],
                                 static_cast<uint8_t>(kStarCol[whichColor][3] * alphaMul) };
        const float groupScale = i < 16 ? kConstellationScale : kSmallStarScale;
        WriteStar(i * 6, Scale(local, groupScale), half * groupScale, corner, col);
    }
}

void EmitStars(Mod& mod, const View& view, int starCount, bool split) {
    // Translação até o olho: as estrelas seguem a câmera; a vista em si está na projeção, que interpola.
    const void* mtx = SkyMatrix(mod, view.eye.x, view.eye.y, view.eye.z);
    if (mtx == nullptr) {
        ++mod.stats.skyFailures;
        return;
    }
    Gfx* const start = sStarList.Begin(16 + ((starCount / kStarsPerChunk + 1) * (1 + kStarsPerChunk)));
    Gfx* g = start;
    if (split) SplitBegin(mod.cfg, g);
    gSPMatrix(g++, const_cast<void*>(mtx), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gDPPipeSync(g++);
    gSPClearGeometryMode(g++, G_LIGHTING | G_CULL_FRONT | G_CULL_BACK | G_FOG);
    gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    // Sem z: a lua e o terreno, desenhados depois com z, cobrem as estrelas de graça.
    gDPSetRenderMode(g++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    for (int first = 0; first < starCount; first += kStarsPerChunk) {
        const int stars = std::min(kStarsPerChunk, starCount - first);
        __gSPVertex(g++, reinterpret_cast<uintptr_t>(&sStarVtx[first * 6]), stars * 6, 0);
        for (int s = 0; s < stars; ++s) {
            const int b = s * 6;
            gSP2Triangles(g++, b + 0, b + 1, b + 2, 0, b + 3, b + 4, b + 5, 0);
        }
    }
    if (split) SplitEnd(mod.cfg, g);
    gSPEndDisplayList(g++);
    if (mod.render->draw_native_display_list(start, LINKSPAN_OOT_RENDER_OPAQUE) == SHIP_NATIVE_OK)
        mod.stats.skyStars = static_cast<uint32_t>(starCount);
    else
        ++mod.stats.skyFailures;
}

// Anima o piscar e o giro uma vez por frame de jogo (os passos por frame de 30 Hz do Wind Waker).
void AdvanceStars(Mod& mod) {
    const float ticks = SkyTicks(mod);
    sAnimCounter += 0.01 * ticks * mod.cfg.starTwinkleSpeed;
    sRot = std::fmod(sRot + ticks, 360.0f);
}

} // namespace

void DrawSkyGradient(Mod& mod, PlayState* play) {
    const Settings& cfg = mod.cfg;
    if (!cfg.skyEnabled || !cfg.gradientEnabled || !SkyAvailable(play) || mod.engine == nullptr) return;
    const auto* save = static_cast<const SaveContext*>(mod.engine->get_save_context());
    if (save == nullptr) return;
    if (!sDomeBuilt) BuildDome();
    const SkyWeather weather = SampleWeather(play);
    SkyColors colors;
    SampleColors(save, weather, colors);
    ColorDome(colors, cfg.gradientBrightness);
    EmitDome(mod, play->view.eye.x, HorizonYForEye(cfg, play->view.eye.y), play->view.eye.z, true);
}

void DrawNightSky(Mod& mod, PlayState* play) {
    const Settings& cfg = mod.cfg;
    if (!cfg.skyEnabled || !cfg.starsEnabled || !SkyAvailable(play) || mod.engine == nullptr) return;
    const auto* save = static_cast<const SaveContext*>(mod.engine->get_save_context());
    if (save == nullptr) return;
    AdvanceStars(mod);
    // A quantidade suaviza até o alvo da hora, e o céu nublado esconde as estrelas.
    const SkyWeather weather = SampleWeather(play);
    const float target = StarAmountForSun(SunHeight(save)) * (1.0f - weather.cloudiness);
    // Carga nova parte do alvo, em vez de descer das estrelas da cena ou do arquivo anterior (defeito do fork: a
    // noite deixava centenas de estrelas num arquivo diurno).
    if (IsNewPlay(sStarStamp, play)) sStarAmount = target;
    else sStarAmount += (target - sStarAmount) * 0.1f;
    mod.stats.skyStars = 0;
    if (sStarAmount < 0.001f) return;
    const int maxStars = std::min(cfg.starCount, kMaxStars);
    const int starCount = std::min(static_cast<int>(sStarAmount * static_cast<float>(maxStars)), kMaxStars);
    if (starCount <= 0) return;
    // O alfa também acompanha a quantidade, além da contagem, para a aurora e o crepúsculo não saltarem.
    const float alphaMul = std::min(sStarAmount * cfg.starBrightness, 1.0f);
    BuildStars(starCount, play->view, alphaMul);
    EmitStars(mod, play->view, starCount, true);
}

// Tela de arquivos: sem PlayState; cúpula na paleta da noite e o campo de estrelas cheio.
void DrawFileSelectSky(Mod& mod, View* view) {
    const Settings& cfg = mod.cfg;
    if (!cfg.skyEnabled || view == nullptr) return;
    if (cfg.gradientEnabled) {
        if (!sDomeBuilt) BuildDome();
        SkyColors colors;
        NightColors(colors);
        ColorDome(colors, cfg.gradientBrightness);
        EmitDome(mod, view->eye.x, HorizonYForEye(cfg, view->eye.y), view->eye.z, false);
    }
    if (!cfg.starsEnabled) return;
    AdvanceStars(mod);
    const int starCount = std::min(cfg.starCount, kMaxStars);
    if (starCount <= 0) return;
    BuildStars(starCount, *view, std::min(cfg.starBrightness, 1.0f));
    EmitStars(mod, *view, starCount, false);
}

} // namespace WWStyle
