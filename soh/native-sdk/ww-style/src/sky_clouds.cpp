// Nuvens (WWClouds.cpp do fork) e fiapos de vento (WWWindWisps.cpp) do céu do Wind Waker.
//
// Nuvens: o dKankyo_vrkumo_Packet (via noclip): até 100 nuvens macias numa cúpula que segue a câmera, levadas pelo
// vento, com fade de entrada e saída, tamanho e tom pela distância. Cada nuvem são três billboards texturizados
// (cloudtx_01/02/03), deslocados pela tabela cloudRep para lerem como uma nuvem só. Antes delas vai a faixa do
// horizonte (vr_back_cloud): três anéis de 8 segmentos que amostram a tira duas vezes, uma cópia parada e outra
// rolando com o vento, e por isso a faixa muda devagar em vez de só deslizar. As constantes da simulação são as do
// noclip; a adaptação ao OoT é o passo por frame em frames de 30 Hz (SkyTicks), o vento do OoT e o tom pela paleta.
//
// Texturas: geradas aqui com o desenho do gen-ww-cloud-textures.py do fork (arte original no estilo do Wind Waker,
// sem conteúdo da Nintendo, sementes fixas: sai igual toda vez). Um pacote montado que traga os mesmos caminhos
// textures/wind-waker/clouds/* (OTEX RGBA32, potência de 2) as troca: o contrato de pacote de texturas do fork.
//
// Fiapos: as linhas de vento (dKankyo__Windline): um ponto voa com o vento ondulando numa senoide (20% dão uma
// volta completa), entra, cruza e sai, e renasce à frente da câmera. No Wind Waker o risco visível é o rastro de
// partículas do ponto; aqui o rastro vira uma fita translúcida afinada nas pontas, com teste de z para o terreno a
// esconder.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "cloud_textures.h"
#include "sky.h"

namespace WWStyle {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kTau = 2.0f * kPi;

// RNG local do fork, para nunca mexer no RNG do jogo.
struct Rng {
    uint32_t s;
    float Next01() {
        s = (s * 1664525u) + 1013904223u;
        return static_cast<float>(s >> 8) * (1.0f / 16777216.0f);
    }
    float F(float m) {
        return Next01() * m;
    }
    float FX(float m) {
        return ((Next01() * 2.0f) - 1.0f) * m;
    }
};

float Saturate(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

uint8_t ClampU8(float v) {
    return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f));
}

float WrapFrac(float v) {
    return v - std::floor(v);
}

// log2 inteiro para as máscaras de tile (as texturas são potência de 2).
int Log2i(int v) {
    int l = 0;
    while ((1 << (l + 1)) <= v) ++l;
    return l;
}

// cLib_addCalc do Wind Waker: anda até o alvo com o passo preso em [minVel, maxVel] (nessa ordem: maxVel vence) e
// encaixa no alvo quando o passo passaria dele. O encaixe importa: a nuvem só troca para a deriva rápida invisível
// com alfa == 0, e sem ele as nuvens apagadas ficariam presas na borda do disco.
float AddCalc(float src, float target, float speed, float maxVel, float minVel) {
    const float delta = target - src;
    float mag = std::fabs(speed * delta);
    if (mag < minVel) mag = minVel;
    if (mag > maxVel) mag = maxVel;
    if (mag > std::fabs(delta)) return target;
    return src + (delta < 0.0f ? -mag : mag);
}

// Variante de ângulo: o caminho mais curto, e `speed` é divisor (a convenção do Wind Waker), não taxa.
float AddCalcAngle(float src, float target, float speed, float maxVel, float minVel) {
    const float da = std::fmod(target - src, kTau);
    const float delta = std::fmod(2.0f * da, kTau) - da;
    float mag = std::fabs(delta / speed);
    if (mag < minVel) mag = minVel;
    if (mag > maxVel) mag = maxVel;
    if (mag > std::fabs(delta)) return src + delta;
    return src + (delta < 0.0f ? -mag : mag);
}

struct V3 {
    float x;
    float y;
    float z;
};

void WriteVtx(Vtx& v, V3 p, int16_t uTexel, int16_t vTexel, const uint8_t col[4]) {
    v = Vtx{};
    v.v.ob[0] = static_cast<int16_t>(p.x);
    v.v.ob[1] = static_cast<int16_t>(p.y);
    v.v.ob[2] = static_cast<int16_t>(p.z);
    v.v.tc[0] = static_cast<int16_t>(uTexel << 5); // texels S10.5
    v.v.tc[1] = static_cast<int16_t>(vTexel << 5);
    v.v.cn[0] = col[0];
    v.v.cn[1] = col[1];
    v.v.cn[2] = col[2];
    v.v.cn[3] = col[3];
}

// Estado de textura das duas passadas texturizadas. O céu de fundo (SETUPDL_40) deixa o RDP em 2 ciclos; o tipo de
// ciclo e a comparação de alfa ficam fora do gDPSetRenderMode, então vão explícitos em quem usa. A LUT e o resto do
// pipeline de textura voltam ao padrão, senão um modo de paleta que sobrou leria o RGBA como índice.
void TexturePipeline(Gfx*& g) {
    gDPSetAlphaCompare(g++, G_AC_NONE);
    gDPSetTextureLUT(g++, G_TT_NONE);
    gDPSetTexturePersp(g++, G_TP_PERSP);
    gDPSetTextureDetail(g++, G_TD_CLAMP);
    gDPSetTextureLOD(g++, G_TL_TILE);
    gDPSetTextureFilter(g++, G_TF_BILERP);
    gDPSetTextureConvert(g++, G_TC_FILT);
}

// --- texturas ---

constexpr int kSprite = kCloudSpriteSize;
constexpr int kBandW = kCloudBandWidth;
constexpr int kBandH = kCloudBandHeight;
constexpr int kLayers = 3;

constexpr const char* kCloudRes[kLayers] = {
    "textures/wind-waker/clouds/cloudtx_01",
    "textures/wind-waker/clouds/cloudtx_02",
    "textures/wind-waker/clouds/cloudtx_03",
};
// Tiras da faixa do horizonte: mae = camada da frente, naka = de trás.
constexpr const char* kBandRes[2] = {
    "textures/wind-waker/clouds/cloud_mae",
    "textures/wind-waker/clouds/cloud_naka",
};

// O Fast3D guarda textura pelo endereço e lê endereço ímpar como segmentado: os pixels ficam parados aqui, pares,
// e iguais a cada carga do mod.
alignas(16) uint8_t sSpritePx[kLayers][kSprite * kSprite * 4];
alignas(16) uint8_t sBandPx[2][kBandW * kBandH * 4];
// Arquivos de pacote lidos. Nunca são liberados nem reaproveitados: um endereço reusado com outro conteúdo mostraria
// a textura velha do cache do Fast3D. A lista só cresce quando o pacote muda.
std::vector<std::unique_ptr<std::vector<uint8_t>>> sPackFiles;
const std::vector<uint8_t>* sPackInUse[kLayers + 2] = {};
CloudTexture sSpriteTex[kLayers];
CloudTexture sBandTex[2];
bool sTexturesGenerated = false;
PlayStamp sTextureStamp;

uint32_t ReadU32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool IsPow2(uint32_t v) {
    return v != 0 && (v & (v - 1)) == 0;
}

// Textura de um pacote montado: recurso OTEX little-endian, RGBA32, potência de 2, até maxWidth × 512. As tiras
// ficam em 256 de largura: o anel de 8 voltas cobre até 2 larguras por quadra, e acima disso a coordenada S10.5 do
// vértice estoura. Devolve os bytes do arquivo e onde estão os pixels.
bool ReadOverride(const ShipOotResourcesV1& res, const char* path, uint32_t maxWidth, std::vector<uint8_t>& bytes,
                  uint32_t& offset, int& width, int& height) {
    if (!res.has_file(path)) return false;
    uint32_t size = 0;
    if (res.read_file(path, nullptr, 0, &size) != SHIP_NATIVE_OK || size < 0x50u || size > (1u << 22)) return false;
    bytes.assign(size, 0);
    uint32_t read = 0;
    if (res.read_file(path, bytes.data(), size, &read) != SHIP_NATIVE_OK || read != size) return false;
    const uint8_t* p = bytes.data();
    if (p[0] != 0 || std::memcmp(p + 4, "XETO", 4) != 0) return false;
    const uint32_t version = ReadU32(p + 0x08);
    uint32_t dataSize = 0;
    if (version == 0) {
        dataSize = ReadU32(p + 0x4C);
        offset = 0x50;
    } else if (version == 1 && size >= 0x5Cu) {
        dataSize = ReadU32(p + 0x58);
        offset = 0x5C;
    } else {
        return false;
    }
    const uint32_t type = ReadU32(p + 0x40);
    const uint32_t w = ReadU32(p + 0x44);
    const uint32_t h = ReadU32(p + 0x48);
    constexpr uint32_t kRgba32 = 1;
    if (type != kRgba32 || !IsPow2(w) || !IsPow2(h) || w > maxWidth || h > 512u || dataSize != w * h * 4u ||
        dataSize > size - offset)
        return false;
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    return true;
}

// Uma vaga: a textura do pacote montado quando houver uma válida, senão a gerada.
void ResolveTexture(const Mod& mod, int slot, const char* path, uint32_t maxWidth, CloudTexture generated,
                    CloudTexture& live) {
    std::vector<uint8_t> bytes;
    uint32_t offset = 0;
    int width = 0;
    int height = 0;
    if (!mod.resources || !ReadOverride(*mod.resources, path, maxWidth, bytes, offset, width, height)) {
        sPackInUse[slot] = nullptr;
        live = generated;
        return;
    }
    if (sPackInUse[slot] == nullptr || *sPackInUse[slot] != bytes) {
        sPackFiles.push_back(std::make_unique<std::vector<uint8_t>>(std::move(bytes)));
        sPackInUse[slot] = sPackFiles.back().get();
    }
    live = {sPackInUse[slot]->data() + offset, width, height};
}

// Gera as cinco texturas uma vez; a cada carga de cena confere de novo o que um pacote montado traz (montar,
// trocar ou tirar um pacote vale na próxima cena).
void EnsureTextures(Mod& mod, const PlayState* play) {
    if (!sTexturesGenerated) {
        constexpr uint32_t kSpriteSeeds[kLayers] = {0x1A2B3C4Du, 0x2B3C4D5Eu, 0x3C4D5E6Fu};
        for (int i = 0; i < kLayers; ++i) GenerateCloudSprite(kSpriteSeeds[i], sSpritePx[i]);
        GenerateCloudBand(0x5E6F7081u, 4, 4, 1.0, false, sBandPx[0]); // frente: grupos espalhados
        GenerateCloudBand(0x4D5E6F70u, 5, 5, 1.5, true, sBandPx[1]);  // fundo: massa contínua
        sTexturesGenerated = true;
    }
    if (!IsNewPlay(sTextureStamp, play)) return;
    for (int i = 0; i < kLayers; ++i)
        ResolveTexture(mod, i, kCloudRes[i], 512u, {sSpritePx[i], kSprite, kSprite}, sSpriteTex[i]);
    for (int i = 0; i < 2; ++i)
        ResolveTexture(mod, kLayers + i, kBandRes[i], 256u, {sBandPx[i], kBandW, kBandH}, sBandTex[i]);
    mod.stats.skyTexOverrides = static_cast<uint32_t>(
        std::count_if(std::begin(sPackInUse), std::end(sPackInUse), [](const auto* file) { return file != nullptr; }));
}

// --- faixa do horizonte ---

// Medida do modelo: três anéis inteiros de 8 segmentos (colunas a cada 45°), y 0..2665, fase u = 0,5 +
// voltas·azimute/2π, v = 0 em cima. Ordem de desenho = de trás para a frente: mae 6 voltas a 1,0× do rolo, naka 4
// voltas a 0,8×, mae de novo 8 voltas a 1,6×.
struct BandRing {
    float radius;
    float uWraps;     // repetições da textura na volta inteira
    float scrollMult; // do rolo base do vento
    int tex;          // 0 = mae, 1 = naka
};
constexpr BandRing kBandRings[3] = {
    {14424.7f, 6.0f, 1.0f, 0},
    {14530.5f, 4.0f, 0.8f, 1},
    {14424.7f, 8.0f, 1.6f, 0},
};
constexpr float kBandHeight = 2665.2f;
constexpr int kBandSegs = 8;
// A faixa do Wind Waker fica em r≈14500 sob um plano distante enorme; escala para o de 12800 do OoT (presa à
// câmera, o raio só muda a distância, e o ângulo de elevação se mantém).
constexpr float kBandScale = 0.55f;

float sBandScroll[3] = {0.0f, 0.0f, 0.0f}; // fase da cópia que rola, em voltas de textura
Vtx sBandVtx[3][kBandSegs * 6];
GfxList sBandList;

void UpdateBandScroll(const PlayState* play, float dt, float driftTrim) {
    const SkyWind wind = Wind(play);
    // O Wind Waker rola a faixa pela parte do vento lateral à vista (daVrbox2_color_set).
    float fx = play->view.lookAt.x - play->view.eye.x;
    float fz = play->view.lookAt.z - play->view.eye.z;
    const float fl = std::sqrt((fx * fx) + (fz * fz));
    if (fl > 0.0001f) {
        fx /= fl;
        fz /= fl;
    }
    const float windScroll = wind.power * ((-wind.x * fz) - (-wind.z * fx));
    const float s0 = dt * 0.0005f * windScroll * driftTrim;
    for (int r = 0; r < 3; ++r) sBandScroll[r] = WrapFrac(sBandScroll[r] + (s0 * kBandRings[r].scrollMult));
}

void BuildBand(float opacity, const uint8_t tint[3]) {
    const uint8_t col[4] = {tint[0], tint[1], tint[2], ClampU8(255.0f * opacity)};
    for (int r = 0; r < 3; ++r) {
        const BandRing& ring = kBandRings[r];
        const float rad = ring.radius * kBandScale;
        const float top = kBandHeight * kBandScale;
        const auto texW = static_cast<float>(sBandTex[ring.tex].width);
        const auto texH = static_cast<int16_t>(sBandTex[ring.tex].height);
        const float texelsPerSeg = ring.uWraps * texW / kBandSegs;
        for (int i = 0; i < kBandSegs; ++i) {
            const float az0 = -kPi + ((2.0f * kPi) * static_cast<float>(i) / kBandSegs);
            const float az1 = az0 + ((2.0f * kPi) / kBandSegs);
            // Fase medida do Wind Waker: u = 0,5 + voltas·az/2π. Cada segmento é uma quadra com o U de base dentro
            // de [0, largura), para a coordenada S10.5 nunca estourar; a máscara WRAP do tile emenda sem costura.
            const float u0f = (0.5f + (ring.uWraps * az0 / (2.0f * kPi))) * texW;
            const float base = u0f - (texW * std::floor(u0f / texW));
            const auto u0 = static_cast<int16_t>(base + 0.5f);
            const auto u1 = static_cast<int16_t>(base + texelsPerSeg + 0.5f);
            const V3 t0{rad * std::sin(az0), top, rad * std::cos(az0)};
            const V3 t1{rad * std::sin(az1), top, rad * std::cos(az1)};
            const V3 b0{rad * std::sin(az0), 0.0f, rad * std::cos(az0)};
            const V3 b1{rad * std::sin(az1), 0.0f, rad * std::cos(az1)};
            Vtx* q = &sBandVtx[r][i * 6];
            WriteVtx(q[0], t0, u0, 0, col); // v = 0 na borda de cima, como no modelo
            WriteVtx(q[1], t1, u1, 0, col);
            WriteVtx(q[2], b1, u1, texH, col);
            WriteVtx(q[3], t0, u0, 0, col);
            WriteVtx(q[4], b1, u1, texH, col);
            WriteVtx(q[5], b0, u0, texH, col);
        }
    }
}

// A faixa segue a câmera na horizontal; na vertical fica na linha do horizonte compartilhada com a cúpula (o Wind
// Waker move a vrbox inteira como uma peça).
void EmitBand(Mod& mod, float eyeX, float horizonY, float eyeZ) {
    const void* mtx = SkyMatrix(mod, eyeX, horizonY, eyeZ);
    if (mtx == nullptr) {
        ++mod.stats.skyFailures;
        return;
    }
    constexpr int kBandVerts = kBandSegs * 6;
    Gfx* const start = sBandList.Begin(32 + (3 * (16 + ((kBandVerts / 30 + 1) * 6))));
    Gfx* g = start;
    SplitBegin(mod.cfg, g);
    gSPMatrix(g++, const_cast<void*>(mtx), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gDPPipeSync(g++);
    gSPClearGeometryMode(g++, G_LIGHTING | G_FOG | G_CULL_FRONT | G_CULL_BACK | G_ZBUFFER);
    gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    // Duas cópias da tira: TEXEL0 parada, TEXEL1 rolando (pelo uls do tile 1). O ciclo 0 imita o TEV do Wind Waker:
    // rgb = (1 - T0)·T1 + T0 (screen), alfa = T0·T1; o ciclo 1 tinge pela cor do vértice (tom e opacidade).
    gDPSetCombineLERP(g++, 1, TEXEL0, TEXEL1, TEXEL0, TEXEL0, 0, TEXEL1, 0, COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE,
                      0);
    gDPSetCycleType(g++, G_CYC_2CYCLE);
    gDPSetRenderMode(g++, G_RM_PASS, G_RM_XLU_SURF2);
    TexturePipeline(g);
    for (int r = 0; r < 3; ++r) {
        const CloudTexture& bt = sBandTex[kBandRings[r].tex];
        const int mS = Log2i(bt.width);
        const int mT = Log2i(bt.height);
        gDPPipeSync(g++);
        gDPLoadTextureTile(g++, bt.data, G_IM_FMT_RGBA, G_IM_SIZ_32b, bt.width, bt.height, 0, 0, bt.width - 1,
                           bt.height - 1, 0, G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, mS, mT, G_TX_NOLOD,
                           G_TX_NOLOD);
        // Segundo amostrador no tile 1: a mesma tira (tmem 0), com o +0,2 U inicial do Wind Waker mais o rolo do
        // vento. O Fast3D subtrai o uls do tile do U do vértice, daí o sinal trocado.
        gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_32b, ((bt.width * 2) + 7) >> 3, 0, G_TX_RENDERTILE + 1, 0,
                   G_TX_WRAP | G_TX_NOMIRROR, mT, G_TX_NOLOD, G_TX_WRAP | G_TX_NOMIRROR, mS, G_TX_NOLOD);
        const auto uls = static_cast<uint16_t>(WrapFrac(-(0.2f + sBandScroll[r])) * bt.width * 4.0f);
        gDPSetTileSize(g++, G_TX_RENDERTILE + 1, uls, 0, uls + ((bt.width - 1) << 2), (bt.height - 1) << 2);
        for (int first = 0; first < kBandVerts; first += 30) {
            const int verts = std::min(30, kBandVerts - first);
            __gSPVertex(g++, reinterpret_cast<uintptr_t>(&sBandVtx[r][first]), verts, 0);
            for (int t = 0; t + 6 <= verts; t += 6) gSP2Triangles(g++, t + 0, t + 1, t + 2, 0, t + 3, t + 4, t + 5, 0);
        }
    }
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    SplitEnd(mod.cfg, g);
    gSPEndDisplayList(g++);
    if (mod.render->draw_native_display_list(start, LINKSPAN_OOT_RENDER_OPAQUE) == SHIP_NATIVE_OK)
        ++mod.stats.skyBands;
    else
        ++mod.stats.skyFailures;
}

// --- nuvens soltas ---

// O Wind Waker põe as nuvens num disco de raio 15000 e as projeta numa cúpula. O disco fica (só decide ângulo e
// queda) e a cúpula cabe no plano distante de 12800: presas à câmera, o raio só muda a distância, não o tamanho.
constexpr float kDiskRadius = 15000.0f;
constexpr float kCloudDomeRadius = 8000.0f;
constexpr int kMaxClouds = 100;

// Uma nuvem (VRKUMO_EFF). posY é refeito todo frame pela queda com a distância.
struct VrKumo {
    float posX;
    float posY;
    float posZ;
    float speed;  // multiplicador de deriva da nuvem
    float height; // pequena variação de altura
    float alpha;  // 0..1 suavizado
    float distFalloff;
};

VrKumo sClouds[kMaxClouds];
bool sCloudsInit = false;
float sStrength = 0.0f; // nublado 0..1, a cobertura
float sBounceTimer = 0.0f;
Rng sCloudRng{0x1a2b3c4du};
Vtx sCloudVtx[kLayers][kMaxClouds * 6]; // uma quadra (6 vértices) por nuvem e camada, refeita todo frame
GfxList sCloudList;

void SpawnCloud(VrKumo& k) {
    const float angle = sCloudRng.F(2.0f * kPi);
    float dist = sCloudRng.F(18000.0f);
    if (dist > 15000.0f) dist = 14000.0f + sCloudRng.F(1000.0f);
    k.posX = dist * std::sin(angle);
    k.posY = 0.0f;
    k.posZ = dist * std::cos(angle);
    k.alpha = 0.0f;
    k.speed = 0.5f + sCloudRng.F(4.0f);
    k.height = 0.3f * sCloudRng.FX(0.3f);
    k.distFalloff = 0.0f;
}

// Um frame da simulação: o vrkumo_move do noclip.
void UpdateClouds(const PlayState* play, float dt, int count, float driftTrim) {
    const SkyWind wind = Wind(play);
    const float windX = wind.x * wind.power * driftTrim;
    const float windZ = wind.z * wind.power * driftTrim;
    const float skyboxOffsY = 1000.0f + (sStrength * -500.0f);
    const float strengthY = 3000.0f + (sStrength * -1000.0f);
    const float strengthVel = 4.0f + (sStrength * 4.3f);
    for (int i = 0; i < kMaxClouds; ++i) {
        VrKumo& k = sClouds[i];
        float distXZ = std::sqrt((k.posX * k.posX) + (k.posZ * k.posZ));
        if (distXZ > 15000.0f) {
            if (distXZ <= 15100.0f) {
                k.posX *= -1.0f;
                k.posZ *= -1.0f;
            } else {
                k.posX = sCloudRng.FX(14000.0f);
                k.posZ = sCloudRng.FX(14000.0f);
                distXZ = std::sqrt((k.posX * k.posX) + (k.posZ * k.posZ));
            }
            k.alpha = 0.0f;
        }
        // A nuvem invisível corre (sem a queda) para cruzar a borda e renascer contra o vento. O noclip só escala
        // o termo de i por dt; aqui a corrida inteira, para a travessia ter a velocidade do GameCube a 30 fps.
        const float vel = k.alpha > 0.0f ? strengthVel * k.distFalloff * k.speed * dt
                                         : (strengthVel + ((static_cast<float>(i) / 1000.0f) * strengthVel)) * dt;
        k.posX += windX * vel;
        k.posZ += windZ * vel;
        const float dist01 = std::min(distXZ / kDiskRadius, 1.0f);
        const float centerCubic = 1.0f - (dist01 * dist01 * dist01);
        k.posY = (500.0f * (static_cast<float>(i) / 100.0f)) + skyboxOffsY + (strengthY * centerCubic);
        k.distFalloff = 1.0f - std::pow(dist01, 6.0f);
        float alphaTarget = 0.0f;
        float alphaMaxVel = 0.005f;
        if (i < count) {
            alphaMaxVel = 0.1f;
            if (k.distFalloff >= 0.05f && k.distFalloff < 0.2f) {
                alphaTarget = (k.distFalloff - 0.05f) / 0.15f;
            } else if (k.distFalloff >= 0.2f) {
                alphaTarget = 1.0f + (sStrength * -0.55f);
            }
        }
        // Some a nuvem que passa bem em cima, onde a projeção na cúpula quebra a ilusão: 0 no alto, 1 no horizonte.
        alphaTarget *= Saturate((0.98f - centerCubic) / (0.98f - 0.88f));
        // Os limites de velocidade são por frame no Wind Waker: escalam com dt também, senão todo fade limitado
        // roda 1,5× mais devagar.
        k.alpha = AddCalc(k.alpha, alphaTarget, 0.2f * dt, alphaMaxVel * dt, 0.01f * dt);
    }
    sBounceTimer += 200.0f * dt;
}

// Direção da cúpula (elevação, azimute) para um vértice relativo ao olho.
V3 DomePoint(float polar, float azimuth) {
    const float cp = std::cos(polar);
    const float sp = std::sin(polar);
    return {cp * std::sin(azimuth) * kCloudDomeRadius, sp * kCloudDomeRadius,
            cp * std::cos(azimuth) * kCloudDomeRadius};
}

// O Wind Waker desloca o segundo e o terceiro billboard de cada nuvem por um de quatro padrões (cloudRep = i & 3),
// para o trio ler como uma nuvem encaroçada em vez de três cópias empilhadas.
void CloudRepOffsets(int textureIdx, int i, float m0, float m1, float& polarOffs, float& azimuthOffs) {
    polarOffs = 0.0f;
    azimuthOffs = 0.0f;
    if (textureIdx == 0) return;
    switch (i & 3) {
    case 0:
        if (textureIdx == 2) {
            polarOffs = m1;
            azimuthOffs = m0;
        }
        break;
    case 1:
        if (textureIdx == 1) {
            polarOffs = -m0;
            azimuthOffs = m0;
        } else {
            polarOffs = -m1;
            azimuthOffs = m1;
        }
        break;
    case 2:
        if (textureIdx == 1) {
            polarOffs = m1;
            azimuthOffs = -m1;
        } else {
            polarOffs = m0;
            azimuthOffs = -m1;
        }
        break;
    default:
        if (textureIdx == 1) {
            polarOffs = -m1;
        } else {
            polarOffs = -m0;
            azimuthOffs = m0;
        }
        break;
    }
}

// Quadras de todas as nuvens visíveis nas três camadas (dKankyo_vrkumo_Packet.draw); devolve quantas.
int BuildClouds(float opacity, const uint8_t edge[3], const uint8_t center[3]) {
    int n = 0;
    for (int i = 0; i < kMaxClouds; ++i) {
        const VrKumo& k = sClouds[i];
        if (k.alpha <= 0.000001f) continue;
        const float distXZ = std::sqrt((k.posX * k.posX) + (k.posZ * k.posZ));
        // Tom da borda ao centro pela queda (vrKumoCol↔vrKumoCenterCol); alfa = da nuvem × opacidade.
        const float t = k.distFalloff;
        const uint8_t col[4] = {ClampU8(edge[0] + ((center[0] - edge[0]) * t)),
                                ClampU8(edge[1] + ((center[1] - edge[1]) * t)),
                                ClampU8(edge[2] + ((center[2] - edge[2]) * t)), ClampU8(255.0f * k.alpha * opacity)};
        for (int textureIdx = 0; textureIdx < kLayers; ++textureIdx) {
            const float size = k.distFalloff *
                               (1.0f - std::pow(static_cast<float>((textureIdx + i) & 0x0F) / 16.0f, 3.0f)) *
                               (0.45f + (sStrength * 0.55f));
            const float bounce = std::sin(static_cast<float>(textureIdx) + (0.0001f * sBounceTimer));
            const float sizeAnim = size + (0.06f * size * bounce * k.distFalloff);
            const float height = sizeAnim + (sizeAnim * k.height);
            const float m0 = 0.15f * sizeAnim;
            const float m1 = 0.65f * sizeAnim;
            float polarOffs = 0.0f;
            float azimuthOffs = 0.0f;
            CloudRepOffsets(textureIdx, i, m0, m1, polarOffs, azimuthOffs);
            const float polarY1 = std::atan2(k.posY, distXZ) + polarOffs;
            const float np = std::pow(std::min(polarY1 / 1.9f, 1.0f), 3.0f);
            const float azimuth = std::atan2(k.posX, k.posZ) + azimuthOffs;
            const float aOff0 = 0.6f * sizeAnim * (1.0f + (16.0f * np));
            const float aOff1 = 0.6f * sizeAnim * (1.0f + (2.0f * np));
            const float polarY0 = std::min(polarY1 + (0.9f * height * (1.0f + (-4.0f * np))), 1.21f);
            const V3 v0 = DomePoint(polarY0, azimuth + aOff0);
            const V3 v1 = DomePoint(polarY0, azimuth - aOff0);
            const V3 v2 = DomePoint(polarY1, azimuth - aOff1);
            const V3 v3 = DomePoint(polarY1, azimuth + aOff1);
            // As coordenadas de texel cobrem a textura inteira, do tamanho que ela tiver.
            const auto tw = static_cast<int16_t>(sSpriteTex[textureIdx].width);
            const auto th = static_cast<int16_t>(sSpriteTex[textureIdx].height);
            Vtx* q = &sCloudVtx[textureIdx][n * 6];
            WriteVtx(q[0], v0, 0, 0, col); // dois triângulos: (v0, v1, v2) e (v0, v2, v3)
            WriteVtx(q[1], v1, tw, 0, col);
            WriteVtx(q[2], v2, tw, th, col);
            WriteVtx(q[3], v0, 0, 0, col);
            WriteVtx(q[4], v2, tw, th, col);
            WriteVtx(q[5], v3, 0, th, col);
        }
        ++n;
    }
    return n;
}

void EmitClouds(Mod& mod, const Vec3f& eye, int clouds) {
    const void* mtx = SkyMatrix(mod, eye.x, eye.y, eye.z);
    if (mtx == nullptr) {
        ++mod.stats.skyFailures;
        return;
    }
    constexpr int kChunks = (kMaxClouds * 6 / 30) + 1;
    Gfx* const start = sCloudList.Begin(32 + (kLayers * (16 + (kChunks * 6))));
    Gfx* g = start;
    SplitBegin(mod.cfg, g);
    gSPMatrix(g++, const_cast<void*>(mtx), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gDPPipeSync(g++);
    gSPClearGeometryMode(g++, G_LIGHTING | G_FOG | G_CULL_FRONT | G_CULL_BACK | G_ZBUFFER);
    gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombineMode(g++, G_CC_MODULATERGBA, G_CC_MODULATERGBA); // TEXEL0 × cor do vértice
    // Translúcido sem AA: o modo XLU com AA engole o alfa por pixel da textura e mostra o retângulo inteiro.
    gDPSetRenderMode(g++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    // 1 ciclo é o que segura: em 2 ciclos o Fast3D descarta o combinador do ciclo 1 e lê o TEXEL0 do ciclo 2 no
    // slot 1, e as quadras amostravam o tile velho do skybox (os retângulos azuis do fork).
    gDPSetCycleType(g++, G_CYC_1CYCLE);
    TexturePipeline(g);
    // O Wind Waker desenha as camadas de trás para a frente (textura 3, 2 e a 1 por cima).
    const int verts = clouds * 6;
    for (int textureIdx = kLayers - 1; textureIdx >= 0; --textureIdx) {
        const CloudTexture& st = sSpriteTex[textureIdx];
        gDPPipeSync(g++);
        gDPLoadTextureTile(g++, st.data, G_IM_FMT_RGBA, G_IM_SIZ_32b, st.width, st.height, 0, 0, st.width - 1,
                           st.height - 1, 0, G_TX_CLAMP | G_TX_NOMIRROR, G_TX_CLAMP | G_TX_NOMIRROR, Log2i(st.width),
                           Log2i(st.height), G_TX_NOLOD, G_TX_NOLOD);
        for (int first = 0; first < verts; first += 30) {
            const int v = std::min(30, verts - first);
            __gSPVertex(g++, reinterpret_cast<uintptr_t>(&sCloudVtx[textureIdx][first]), v, 0);
            for (int t = 0; t + 6 <= v; t += 6) gSP2Triangles(g++, t + 0, t + 1, t + 2, 0, t + 3, t + 4, t + 5, 0);
        }
    }
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    SplitEnd(mod.cfg, g);
    gSPEndDisplayList(g++);
    if (mod.render->draw_native_display_list(start, LINKSPAN_OOT_RENDER_OPAQUE) == SHIP_NATIVE_OK)
        mod.stats.skyClouds = static_cast<uint32_t>(clouds);
    else
        ++mod.stats.skyFailures;
}

// --- fiapos de vento ---

constexpr float kS2Rad = kTau / 65536.0f; // ângulo binário → radianos (cM_s2rad)
constexpr int kMaxWisps = 50;             // vagas do noclip (o Wind Waker usa 30)
constexpr int kTrailLen = 16;             // amostras do rastro (~0,8 s de voo a 20 fps)
// Perfil do corte: núcleo com alfa cheio e borda que cai a zero logo depois, como a partícula texturizada do Wind
// Waker; uma fita de três vértices seria um gradiente linear na largura inteira, borrado.
constexpr float kCoreHalfWidth = 12.0f;
constexpr float kEdgeHalfWidth = 24.0f;

// Uma amostra congelada do rastro, no lugar de uma partícula: posição, lado da fita e alfa fixos no nascimento;
// depois só a idade muda. Recalcular por frame faz o risco se contorcer.
struct TrailPt {
    V3 pos;
    V3 side;
    float alpha;
};

// Uma linha de vento (WIND_EFF) com o histórico do rastro.
struct WindEff {
    int state; // 0 parada, 1 entrando/cruzando, 2 saindo
    V3 basePos;
    V3 animPos;
    float respawnTimer; // frames parada antes de poder nascer de novo
    float stateTimer;
    float alpha;
    float swerveAnimCounter;
    float swerveAngleXZ;
    float swerveAngleY;
    float loopCounter;
    bool doLoop;
    TrailPt trail[kTrailLen]; // anel, em coordenadas do mundo
    int trailCount;
    int trailNext;
    V3 lastSide; // reserva quando a direção de voo degenera contra o raio da vista
};

WindEff sWisps[kMaxWisps];
uint32_t sWispFrame = 0;
bool sWispsSeeded = false;
PlayStamp sWispStamp;
Rng sWispRng{0x77123455u};
// Uma região de vértices por fiapo: o gSPVertex guarda o ponteiro e o renderer só o lê no fim do frame.
Vtx sWispVtx[kMaxWisps][kTrailLen * 4];
GfxList sWispList;

void SpawnWisp(const View& view, float aspect, WindEff& e, float windX, float windZ) {
    // Nasce à frente da câmera (dKy_set_eyevect_calc2), na horizontal: o pitch fica preso no horizonte em vez de
    // zerado, e a câmera olhando o céu ainda nasce alto. A distância vai para a metade de fora da bolha de 12,8k do
    // OoT (menor na tela e mais lento, o "vasto") e o levantamento cresce junto, para a elevação na vista se manter.
    float fx = view.lookAt.x - view.eye.x;
    float fy = std::max(view.lookAt.y - view.eye.y, 0.0f);
    float fz = view.lookAt.z - view.eye.z;
    float fl = std::sqrt((fx * fx) + (fy * fy) + (fz * fz));
    if (fl < 0.001f) fl = 1.0f;
    fx /= fl;
    fy /= fl;
    fz /= fl;
    e.basePos = {view.eye.x + (fx * 6000.0f), view.eye.y + (fy * 6000.0f) + 2200.0f, view.eye.z + (fz * 6000.0f)};
    // Espalhamento lateral pelo tamanho da tela: meia largura do frustum na distância de nascimento
    // (tan(fovy/2) × aspecto × distância), com uma folga, ao longo do eixo direito da tela.
    const float halfW = std::min(std::tan(view.fovy * 0.5f * kPi / 180.0f) * aspect * 6000.0f * 1.15f, 8500.0f);
    float rx = fz;
    float rz = -fx;
    float rl = std::sqrt((rx * rx) + (rz * rz));
    if (rl < 0.001f) {
        rx = 1.0f;
        rz = 0.0f;
        rl = 1.0f;
    }
    const float lateral = sWispRng.FX(halfW) / rl;
    const float depth = sWispRng.FX(2500.0f);
    const float upwind = 2000.0f + sWispRng.F(2000.0f);
    e.animPos.x = (rx * lateral) + (fx * depth) - (windX * upwind);
    e.animPos.y = sWispRng.FX(2400.0f) + (fy * depth);
    e.animPos.z = (rz * lateral) + (fz * depth) - (windZ * upwind);
    // Traz um ponto fora de volta para dentro do raio de reciclagem (nascer em 10500 < reciclar em 11500), para o
    // fiapo novo não nascer já saindo: o maior s em [0, 1] com |(base - olho) + s·anim| <= limite.
    const float bx = e.basePos.x - view.eye.x;
    const float by = e.basePos.y - view.eye.y;
    const float bz = e.basePos.z - view.eye.z;
    const float aa = (e.animPos.x * e.animPos.x) + (e.animPos.y * e.animPos.y) + (e.animPos.z * e.animPos.z);
    const float ab = (e.animPos.x * bx) + (e.animPos.y * by) + (e.animPos.z * bz);
    const float bb = (bx * bx) + (by * by) + (bz * bz) - (10500.0f * 10500.0f);
    const float disc = (ab * ab) - (aa * bb);
    if (aa > 0.001f && disc > 0.0f) {
        const float s = (-ab + std::sqrt(disc)) / aa;
        if (s < 1.0f) {
            e.animPos.x *= s;
            e.animPos.y *= s;
            e.animPos.z *= s;
        }
    }
    // No lugar da checagem de chão do Wind Waker, bem acima do olho: abaixo dele o terreno esconde o fiapo, e como o
    // voo é horizontal a altura do nascimento é a do voo inteiro.
    const float minY = view.eye.y + 1500.0f;
    if (e.basePos.y + e.animPos.y < minY) e.animPos.y = minY - e.basePos.y + sWispRng.F(1500.0f);
    e.swerveAnimCounter = sWispRng.F(kTau);
    e.swerveAngleXZ = std::atan2(windX, windZ);
    e.swerveAngleY = 0.0f;
    e.loopCounter = 0.0f;
    e.doLoop = sWispRng.Next01() < 0.2f;
    e.stateTimer = 0.0f;
    e.alpha = 0.0f;
    e.trailCount = 0;
    e.trailNext = 0;
    e.state = 1;
}

// `dt` marca o ciclo de fade (tempo do GameCube); `mdt` = dt × o slider de velocidade marca o voo: ondulação,
// voltas e trajeto desaceleram juntos e o desenho do caminho continua o do Wind Waker.
void UpdateWisps(const PlayState* play, float aspect, int count, float dt, float mdt) {
    const SkyWind wind = Wind(play);
    const View& view = play->view;
    // Carga nova: os rastros estão em coordenadas do mundo da cena anterior e reapareceriam nesta. Recomeça a
    // população (defeito do fork).
    if (IsNewPlay(sWispStamp, play)) {
        for (WindEff& e : sWisps) {
            e.state = 0;
            e.alpha = 0.0f;
            e.trailCount = 0;
            e.trailNext = 0;
        }
        sWispsSeeded = false;
    }
    ++sWispFrame;
    // Na primeira ativação, espalha os nascimentos por um ciclo inteiro: a população começa sem onda.
    if (!sWispsSeeded) {
        for (WindEff& e : sWisps) e.respawnTimer = sWispRng.F(200.0f);
        sWispsSeeded = true;
    }
    for (int i = 0; i < kMaxWisps; ++i) {
        WindEff& e = sWisps[i];
        if (e.state == 0) {
            // Intervalo aleatório em vez do rodízio do Wind Waker: o ciclo de fade é quase fixo (~170 frames) e
            // qualquer cadência fixa mais curta trava a população em ondas de nascer e morrer juntos.
            e.respawnTimer -= dt;
            if (i < count && e.respawnTimer <= 0.0f) SpawnWisp(view, aspect, e, wind.x, wind.z);
            continue;
        }
        // Ondulação: uma senoide por cima da volta suave à direção do vento.
        e.swerveAnimCounter += kS2Rad * 800.0f * mdt;
        const float swerveAnimMag = kS2Rad * (250.0f - (0.2f * 250.0f * (1.0f - wind.power)));
        const float change = mdt * swerveAnimMag * std::sin(e.swerveAnimCounter);
        e.swerveAngleY += change;
        e.swerveAngleXZ += (i & 1) ? change : -change;
        if (e.stateTimer <= 0.5f || !e.doLoop) {
            // A suavização por tick do Wind Waker em tempo de relógio: o divisor encolhe com o passo e os limites
            // por frame crescem com ele.
            const float targetXZ = std::atan2(wind.x, wind.z);
            e.swerveAngleXZ = AddCalcAngle(e.swerveAngleXZ, targetXZ, 10.0f / mdt, kS2Rad * 1000.0f * mdt,
                                           kS2Rad * 1.0f * mdt);
            e.swerveAngleY = AddCalcAngle(e.swerveAngleY, 0.0f, 10.0f / mdt, kS2Rad * 1000.0f * mdt,
                                          kS2Rad * 1.0f * mdt);
        } else {
            // A volta completa, e depois segue cruzando. 2000 por tick é a taxa do noclip (o hardware usa 3600).
            const float loopStep = kS2Rad * 2000.0f * mdt;
            e.loopCounter += loopStep;
            e.swerveAngleY += loopStep;
            if (e.loopCounter > kS2Rad * 60535.0f) e.doLoop = false;
        }
        const float swerveT = Saturate(e.swerveAnimCounter / kTau);
        const float mag = ((1.3f * 80.0f) - (0.2f * 80.0f * (1.0f - wind.power))) * swerveT;
        e.animPos.x += std::cos(e.swerveAngleY) * std::sin(e.swerveAngleXZ) * mag * mdt;
        e.animPos.y += std::sin(e.swerveAngleY) * mag * mdt;
        e.animPos.z += std::cos(e.swerveAngleY) * std::cos(e.swerveAngleXZ) * mag * mdt;
        const V3 head{e.basePos.x + e.animPos.x, e.basePos.y + e.animPos.y, e.basePos.z + e.animPos.z};
        // Além do plano distante do OoT o fiapo é voo invisível: força a saída para a vaga soltar logo, mas nunca
        // mata na hora (o rastro inteiro sumiria num frame).
        const float ddx = head.x - view.eye.x;
        const float ddy = head.y - view.eye.y;
        const float ddz = head.z - view.eye.z;
        if ((ddx * ddx) + (ddy * ddy) + (ddz * ddz) > 11500.0f * 11500.0f) {
            if (e.state == 1) e.state = 2;
            e.stateTimer = std::min(e.stateTimer, 0.45f); // abaixo de 0,5: o alfa começa a descer agora
        }
        // Grava a cabeça no anel do rastro, com o lado da fita congelado: direção do voo × raio da vista agora.
        TrailPt& pt = e.trail[e.trailNext];
        pt.pos = head;
        pt.alpha = e.alpha;
        V3 along{wind.x, 0.0f, wind.z};
        if (e.trailCount > 0) {
            const V3& prev = e.trail[(e.trailNext - 1 + kTrailLen) % kTrailLen].pos;
            along = {head.x - prev.x, head.y - prev.y, head.z - prev.z};
        }
        const V3 toEye{view.eye.x - head.x, view.eye.y - head.y, view.eye.z - head.z};
        V3 side{(along.y * toEye.z) - (along.z * toEye.y), (along.z * toEye.x) - (along.x * toEye.z),
                (along.x * toEye.y) - (along.y * toEye.x)};
        const float sl = std::sqrt((side.x * side.x) + (side.y * side.y) + (side.z * side.z));
        if (sl > 0.001f) {
            side = {side.x / sl, side.y / sl, side.z / sl};
            e.lastSide = side;
        } else {
            side = e.lastSide;
        }
        pt.side = side;
        e.trailNext = (e.trailNext + 1) % kTrailLen;
        if (e.trailCount < kTrailLen) ++e.trailCount;
        // Os limites do AddCalc são por frame na simulação de 30 fps: escalam com dt, senão o fiapo ficava ~50%
        // mais tempo invisível depois de nascer.
        const float maxVel = 0.08f + (0.008f * (static_cast<float>(i) / 30.0f));
        if (e.state == 1) {
            e.stateTimer = AddCalc(e.stateTimer, 1.0f, 0.3f * dt, 0.1f * maxVel * dt, 0.01f * dt);
            if (e.stateTimer >= 1.0f) e.state = 2;
            if (e.stateTimer > 0.5f) e.alpha = AddCalc(e.alpha, 1.0f, 0.5f * dt, 0.05f * dt, 0.001f * dt);
        } else {
            e.stateTimer = AddCalc(e.stateTimer, 0.0f, 0.5f * dt,
                                   maxVel * (0.1f + (0.01f * (static_cast<float>(i) / 30.0f))) * dt, 0.01f * dt);
            if (e.stateTimer < 0.5f) e.alpha = AddCalc(e.alpha, 0.0f, 0.5f * dt, 0.05f * dt, 0.001f * dt);
            if (e.stateTimer <= 0.0f) {
                e.state = 0;
                e.respawnTimer = sWispRng.F(60.0f);
            }
        }
    }
}

void WriteWispVtx(Vtx& v, float x, float y, float z, const uint8_t col[3], uint8_t alpha) {
    v = Vtx{};
    v.v.ob[0] = static_cast<int16_t>(x);
    v.v.ob[1] = static_cast<int16_t>(y);
    v.v.ob[2] = static_cast<int16_t>(z);
    v.v.cn[0] = col[0];
    v.v.cn[1] = col[1];
    v.v.cn[2] = col[2];
    v.v.cn[3] = alpha;
}

// A fita de um fiapo pelas amostras congeladas: um corte de 4 vértices por amostra (borda transparente, núcleo
// cheio, borda transparente). Posições e lados nunca mudam depois de emitidos, então o risco fica preso ao mundo.
bool EmitWisp(Mod& mod, const WindEff& e, int slot, const uint8_t col[3], float alphaScale, Gfx*& g) {
    const int n = e.trailCount;
    if (n < 2) return false;
    Vtx* vtx = sWispVtx[slot];
    const int tail = (e.trailNext - n + kTrailLen) % kTrailLen;
    const V3 origin = e.trail[tail].pos; // qualquer referência fixa serve; os vértices são posições exatas
    // Filho por (fiapo, frame de jogo): nunca casa com o frame anterior e a matriz vale como está nos frames
    // interpolados. Precisa: a origem anda a cada tick com os vértices compensando, e interpolar a matriz com os
    // vértices parados fazia o rastro nadar de lado. A câmera continua suave: a vista interpolada vai por cima.
    const void* mtx = nullptr;
    if (mod.render->interpolation_begin(&e, static_cast<int32_t>(sWispFrame)) != SHIP_NATIVE_OK) return false;
    if (mod.render->matrix_push() == SHIP_NATIVE_OK) {
        if (mod.render->matrix_translate_new(origin.x, origin.y, origin.z) != SHIP_NATIVE_OK ||
            mod.render->export_current_matrix(&mtx) != SHIP_NATIVE_OK)
            mtx = nullptr;
        mod.render->matrix_pop();
    }
    mod.render->interpolation_end();
    if (mtx == nullptr) return false;
    gSPMatrix(g++, const_cast<void*>(mtx), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    for (int k = 0; k < n; ++k) {
        const TrailPt& pt = e.trail[(tail + k) % kTrailLen];
        const float age = static_cast<float>(n - 1 - k) / (kTrailLen - 1); // 0 = recém-emitida, 1 = a mais velha
        // Oval esticado: mais largo no meio do rastro e fino nas duas pontas (a cabeça mais larga lê como fada). O
        // alfa é uniforme; as pontas somem pela geometria, então a amostra que sai do anel nunca estala.
        const float widthScale = std::sqrt(4.0f * age * (1.0f - age));
        const uint8_t a = ClampU8(255.0f * alphaScale * pt.alpha);
        const float coreW = kCoreHalfWidth * widthScale;
        const float edgeW = kEdgeHalfWidth * widthScale;
        const float px = pt.pos.x - origin.x;
        const float py = pt.pos.y - origin.y;
        const float pz = pt.pos.z - origin.z;
        Vtx* row = &vtx[k * 4];
        WriteWispVtx(row[0], px - (pt.side.x * edgeW), py - (pt.side.y * edgeW), pz - (pt.side.z * edgeW), col, 0);
        WriteWispVtx(row[1], px - (pt.side.x * coreW), py - (pt.side.y * coreW), pz - (pt.side.z * coreW), col, a);
        WriteWispVtx(row[2], px + (pt.side.x * coreW), py + (pt.side.y * coreW), pz + (pt.side.z * coreW), col, a);
        WriteWispVtx(row[3], px + (pt.side.x * edgeW), py + (pt.side.y * edgeW), pz + (pt.side.z * edgeW), col, 0);
    }
    // Por segmento: 8 vértices = dois cortes, 6 triângulos.
    for (int k = 0; k + 1 < n; ++k) {
        __gSPVertex(g++, reinterpret_cast<uintptr_t>(&vtx[k * 4]), 8, 0);
        gSP2Triangles(g++, 0, 1, 5, 0, 0, 5, 4, 0);
        gSP2Triangles(g++, 1, 2, 6, 0, 1, 6, 5, 0);
        gSP2Triangles(g++, 2, 3, 7, 0, 2, 7, 6, 0);
    }
    return true;
}

} // namespace

void DrawClouds(Mod& mod, PlayState* play) {
    const Settings& cfg = mod.cfg;
    mod.stats.skyClouds = 0;
    if (!cfg.skyEnabled || !cfg.cloudsEnabled || !SkyAvailable(play) || mod.engine == nullptr) return;
    const auto* save = static_cast<const SaveContext*>(mod.engine->get_save_context());
    if (save == nullptr) return;
    EnsureTextures(mod, play);
    const float dt = SkyTicks(mod);
    // O céu fechado sobe a cobertura até o nublado cheio do Wind Waker (o slider é o piso do céu limpo) e a
    // tempestade sopra mais forte.
    const SkyWeather weather = SampleWeather(play);
    const float coverage = std::max(cfg.cloudsCoverage, weather.cloudiness);
    const float drift = cfg.cloudsDriftSpeed * (1.0f + weather.storm);
    // Tons pela vrKumoCol (borda) e vrKumoCenterCol (centro) agendadas do Wind Waker.
    SkyColors colors;
    SampleColors(save, weather, colors);
    // A faixa do horizonte primeiro, para as nuvens pintarem por cima.
    UpdateBandScroll(play, dt, drift);
    BuildBand(cfg.cloudsOpacity, colors.kumo);
    EmitBand(mod, play->view.eye.x, HorizonYForEye(cfg, play->view.eye.y), play->view.eye.z);
    if (!sCloudsInit) {
        for (VrKumo& k : sClouds) SpawnCloud(k);
        sCloudsInit = true;
    }
    // A cobertura decide quantas nuvens ficam ativas, como a força do clima do Wind Waker (50 limpo, 100 fechado).
    sStrength = coverage;
    const int count = std::min(static_cast<int>(50.0f + (50.0f * coverage)), kMaxClouds);
    UpdateClouds(play, dt, count, drift);
    const int clouds = BuildClouds(cfg.cloudsOpacity, colors.kumo, colors.kumoCenter);
    if (clouds > 0) EmitClouds(mod, play->view.eye, clouds);
}

// Os fiapos vão para a camada XLU, que roda depois do mundo não importa quando este hook a alimenta.
void DrawWindWisps(Mod& mod, PlayState* play) {
    const Settings& cfg = mod.cfg;
    mod.stats.skyWisps = 0;
    if (!cfg.skyEnabled || !cfg.wispsEnabled || !SkyAvailable(play) || mod.engine == nullptr) return;
    const auto* save = static_cast<const SaveContext*>(mod.engine->get_save_context());
    if (save == nullptr) return;
    ShipOotRenderFrameInfoV1 info{sizeof(info)};
    const bool haveInfo = mod.render->get_frame_info(&info) == SHIP_NATIVE_OK;
    const float aspect = haveInfo && info.aspect_ratio >= 0.1f ? info.aspect_ratio : 4.0f / 3.0f;
    const float dt = SkyTicks(mod);
    const SkyWind wind = Wind(play);
    // Wind Waker: 10 linhas com vento cheio, vezes o slider.
    const int count = std::min(static_cast<int>(10.0f * wind.power * cfg.wispAmount), kMaxWisps);
    const float speed = std::max(cfg.wispSpeed, 0.1f); // mdt divide a velocidade do giro: fica positivo
    UpdateWisps(play, aspect, count, dt, dt * speed);
    // Tom e brilho seguem a cor do centro das nuvens: à noite os fiapos escurecem, como os do Wind Waker (que
    // escalam o alfa pelo quadrado do brilho ambiente).
    const SkyWeather weather = SampleWeather(play);
    SkyColors colors;
    SampleColors(save, weather, colors);
    const float colorAvg =
        static_cast<float>(colors.kumoCenter[0] + colors.kumoCenter[1] + colors.kumoCenter[2]) / (3.0f * 255.0f);
    const auto visible = [](const WindEff& e) { return e.state != 0 && e.alpha > 0.01f; };
    if (std::none_of(std::begin(sWisps), std::end(sWisps), visible)) return;
    Gfx* const start = sWispList.Begin(16 + (kMaxWisps * (1 + ((kTrailLen - 1) * 4))));
    Gfx* g = start;
    gDPPipeSync(g++);
    gSPClearGeometryMode(g++, G_LIGHTING | G_FOG | G_CULL_FRONT | G_CULL_BACK);
    gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    // Testa o z (o terreno esconde os fiapos), não grava, mistura macia; ciclo e comparação de alfa explícitos.
    gDPSetRenderMode(g++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
    gDPSetCycleType(g++, G_CYC_1CYCLE);
    gDPSetAlphaCompare(g++, G_AC_NONE);
    uint32_t drawn = 0;
    for (int i = 0; i < kMaxWisps; ++i) {
        const WindEff& e = sWisps[i];
        if (!visible(e)) continue;
        const float dx = e.basePos.x + e.animPos.x - play->view.eye.x;
        const float dy = e.basePos.y + e.animPos.y - play->view.eye.y;
        const float dz = e.basePos.z + e.animPos.z - play->view.eye.z;
        const float distFade = std::min(std::sqrt((dx * dx) + (dy * dy) + (dz * dz)) / 200.0f, 1.0f);
        const float alphaFade = std::max(wind.power * distFade * colorAvg * colorAvg, 0.5f);
        if (EmitWisp(mod, e, i, colors.kumoCenter, alphaFade * e.alpha, g)) ++drawn;
    }
    gSPEndDisplayList(g++);
    if (mod.render->draw_native_display_list(start, LINKSPAN_OOT_RENDER_TRANSLUCENT) == SHIP_NATIVE_OK)
        mod.stats.skyWisps = drawn;
    else
        ++mod.stats.skyFailures;
}

} // namespace WWStyle
