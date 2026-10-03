// Luzes do mundo — o WorldLighting.cpp e o DekuStickLight.cpp do fork sobre os serviços do host.
//
// - Tremulação de chama (oot.light.point_color): troca o ruído branco por frame das tochas por uma caminhada
//   aleatória lenta, na fonte, então a luz vanilla, o cel e as projeções leem o mesmo valor.
// - Projeção de luz (oot.render.world_lights): cada luz pontual projeta no cenário um polígono de luz, a seção de
//   uma icosfera girando, pela técnica de volume com stencil (duas passadas z-fail e uma composição que se limpa).
//   Roda depois do pré-passe dos receptores e antes dos outros atores, então pega o cenário e os pisos-ator.
// - Luz do Deku Stick: uma luz de mod (linkspan.oot.lights) na ponta acesa, com o ruído de uma tocha, para a
//   tremulação, o cel, as sombras e a projeção a tratarem como tocha.
#include <algorithm>
#include <cmath>

#include "ww_style.h"
#include "macros.h"

namespace WWStyle {
namespace {

constexpr float kPi = 3.14159265f;
// Giro do Bonbori do Wind Waker (rad/s): dois eixos não harmônicos, sem Z, para o polígono não parecer girar no
// lugar.
constexpr float kWWRotYRate = 0.598f; // 0xD0 unidades/frame a 30 Hz
constexpr float kWWRotXRate = 0.736f; // 0x100 unidades/frame a 30 Hz

// Mesmos valores de Fast::StencilMode (libultraship/include/fast/backends/gfx_rendering_api.h).
constexpr uint32_t kStencilOff = 0;
constexpr uint32_t kStencilIncr = 1;
constexpr uint32_t kStencilDecr = 2;
constexpr uint32_t kStencilComposite = 3;

// Luz do Deku Stick: raio da tocha de parede (obj_syokudai) e a cor do fogo padrão do En_Light. O fork aceitava
// trocar a cor por uma textura de pacote; aqui fica a cor padrão.
constexpr int16_t kStickLightRadius = 200;
constexpr uint8_t kStickColor[3] = { 255, 200, 0 };

// Icosfera de nível 2 (80 faces), um vértice por canto (240), em 8 cargas de 30 (o cache de vértices tem 32). A
// geometria fica no raio 100 e escala por raio × 0,01 na hora de desenhar.
constexpr int kIcoFaces = 80;
constexpr int kIcoVerts = kIcoFaces * 3;
constexpr int kIcoChunkVerts = 30;
constexpr int kIcoChunks = kIcoVerts / kIcoChunkVerts;
static_assert(kIcoChunkVerts == 30, "cada carga emite 10 triângulos com índices 0..29");

Vtx sIcoVtx[kIcoVerts];
Gfx sIcoDL[kIcoChunks * 6 + 1];
bool sIcoBuilt = false;

uint32_t NextRandom(uint32_t& state) {
    // xorshift32; o Rand_ZeroOne do jogo é função do host.
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float RandomZeroOne(uint32_t& state) {
    return static_cast<float>(NextRandom(state) >> 8) / 16777216.0f;
}

void IcoWriteVert(int index, float x, float y, float z, uint8_t shade) {
    const float len = std::sqrt((x * x) + (y * y) + (z * z));
    const float s = len > 0.0001f ? 100.0f / len : 0.0f;
    Vtx& v = sIcoVtx[index];
    v = Vtx{};
    v.v.ob[0] = static_cast<int16_t>(std::lround(x * s));
    v.v.ob[1] = static_cast<int16_t>(std::lround(y * s));
    v.v.ob[2] = static_cast<int16_t>(std::lround(z * s));
    v.v.cn[0] = v.v.cn[1] = v.v.cn[2] = shade;
    v.v.cn[3] = 255;
}

// Monta os 240 vértices e a display list em cargas. A sombra por face (clara/escura conforme uma direção fixa do
// próprio objeto) só aparece no debug, que multiplica a cor da luz por ela; as projeções usam só PRIMITIVE.
void BuildIcosphere() {
    if (sIcoBuilt) return;
    const float t = 1.618033988749895f;
    const float base[12][3] = {
        { -1, t, 0 }, { 1, t, 0 },  { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t },  { 0, 1, t },
        { 0, -1, -t }, { 0, 1, -t }, { t, 0, -1 },  { t, 0, 1 },  { -t, 0, -1 }, { -t, 0, 1 },
    };
    // Faces com a normal para fora (anti-horário), para o culling das passadas de stencil.
    const int faces[20][3] = {
        { 0, 11, 5 }, { 0, 5, 1 },  { 0, 1, 7 },  { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 },  { 5, 11, 4 },
        { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },  { 3, 9, 4 },  { 3, 4, 2 },   { 3, 2, 6 },  { 3, 6, 8 },
        { 3, 8, 9 },  { 4, 9, 5 },  { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 },   { 9, 8, 1 },
    };
    const float shadeDir[3] = { 0.40f, 0.72f, 0.57f };
    int w = 0;
    for (const auto& face : faces) {
        const float* a = base[face[0]];
        const float* b = base[face[1]];
        const float* c = base[face[2]];
        const float ab[3] = { (a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f, (a[2] + b[2]) * 0.5f };
        const float bc[3] = { (b[0] + c[0]) * 0.5f, (b[1] + c[1]) * 0.5f, (b[2] + c[2]) * 0.5f };
        const float ca[3] = { (c[0] + a[0]) * 0.5f, (c[1] + a[1]) * 0.5f, (c[2] + a[2]) * 0.5f };
        const float* sub[4][3] = { { a, ab, ca }, { ab, b, bc }, { ca, bc, c }, { ab, bc, ca } };
        for (const auto& tri : sub) {
            const float cx = tri[0][0] + tri[1][0] + tri[2][0];
            const float cy = tri[0][1] + tri[1][1] + tri[2][1];
            const float cz = tri[0][2] + tri[1][2] + tri[2][2];
            const float clen = std::sqrt((cx * cx) + (cy * cy) + (cz * cz));
            const float dot =
                clen > 0.0001f ? ((cx * shadeDir[0]) + (cy * shadeDir[1]) + (cz * shadeDir[2])) / clen : 0.0f;
            const auto shade = static_cast<uint8_t>((0.5f + (0.25f * (dot + 1.0f))) * 255.0f);
            for (const float* corner : tri) IcoWriteVert(w++, corner[0], corner[1], corner[2], shade);
        }
    }
    Gfx* g = sIcoDL;
    for (int chunk = 0; chunk < kIcoChunks; ++chunk) {
        __gSPVertex(g++, reinterpret_cast<uintptr_t>(&sIcoVtx[chunk * kIcoChunkVerts]), kIcoChunkVerts, 0);
        gSP2Triangles(g++, 0, 1, 2, 0, 3, 4, 5, 0);
        gSP2Triangles(g++, 6, 7, 8, 0, 9, 10, 11, 0);
        gSP2Triangles(g++, 12, 13, 14, 0, 15, 16, 17, 0);
        gSP2Triangles(g++, 18, 19, 20, 0, 21, 22, 23, 0);
        gSP2Triangles(g++, 24, 25, 26, 0, 27, 28, 29, 0);
    }
    gSPEndDisplayList(g++);
    sIcoBuilt = true;
}

// cLib_addCalc2 do Wind Waker: anda uma fração `speed30` do que falta a cada 1/30 s, limitada a `maxVel30` por
// passo (passa-baixa com limite de inclinação, sem ultrapassar), independente da taxa de frames.
float WWEase(float cur, float target, float speed30, float maxVel30, float dt) {
    const float ticks = dt * 30.0f;
    const float cap = maxVel30 * ticks;
    const float step = std::clamp((target - cur) * (1.0f - std::pow(1.0f - speed30, ticks)), -cap, cap);
    return cur + step;
}

// Intensidade 0..2 do menu para o alfa da composição.
uint8_t PoolAlpha(float intensity) {
    return static_cast<uint8_t>(std::clamp(intensity * 0.5f, 0.0f, 1.0f) * 255.0f);
}

WorldLightState& LightState(Mod& mod, const void* info) {
    auto [it, isNew] = mod.lighting.lights.try_emplace(info);
    WorldLightState& s = it->second;
    if (isNew) {
        // Fase pela posição da luz na memória, para as tochas não girarem juntas. O raio da Navi nasce em 0 e
        // cresce, em vez de estalar.
        const auto bits = static_cast<float>((reinterpret_cast<uintptr_t>(info) >> 4) & 0x3FF);
        const float phase = bits / 1024.0f * 2.0f * kPi;
        s = WorldLightState{};
        s.angleY = phase;
        s.angleX = phase * 0.7f;
        s.sizeCur = s.sizeTarget = 1.0f;
        s.alphaCur = s.alphaTarget = 1.0f;
    }
    s.gen = mod.lighting.gen;
    return s;
}

void MaskPass(Gfx*& g, uint32_t stencil, uint32_t cull) {
    gSPStencil(g++, stencil);
    gDPPipeSync(g++);
    gSPClearGeometryMode(g++, G_LIGHTING | G_CULL_FRONT | G_CULL_BACK);
    gSPSetGeometryMode(g++, G_ZBUFFER | cull);
    gDPSetCombineLERP(g++, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE);
    gDPSetRenderMode(g++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2); // testa o depth, não grava
    gDPSetPrimColor(g++, 0, 0, 0, 0, 0, 0);                            // alfa 0: invisível, o stencil atualiza
    __gSPDisplayList(g++, sIcoDL);
}

// Preenche a região marcada (stencil != 0: cenário dentro do volume) com a cor da luz, zerando o stencil. Sem teste
// de depth: as faces de trás cobrem toda a pegada do volume mesmo com a câmera dentro dele.
void CompositePass(Gfx*& g, const uint8_t col[3], uint8_t alpha) {
    gSPStencil(g++, kStencilComposite);
    gDPPipeSync(g++);
    gSPClearGeometryMode(g++, G_LIGHTING | G_ZBUFFER | G_CULL_BACK);
    gSPSetGeometryMode(g++, G_CULL_FRONT);
    gDPSetCombineLERP(g++, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE);
    gDPSetRenderMode(g++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    gDPSetPrimColor(g++, 0, 0, col[0], col[1], col[2], alpha);
    __gSPDisplayList(g++, sIcoDL);
}

// Debug: a icosfera translúcida com a cor da luz e a sombra por face, sem teste de depth.
void DebugSphere(Gfx*& g, const uint8_t col[3]) {
    gSPStencil(g++, kStencilOff);
    gDPPipeSync(g++);
    gSPClearGeometryMode(g++, G_LIGHTING | G_ZBUFFER | G_CULL_FRONT);
    gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK);
    gDPSetCombineLERP(g++, PRIMITIVE, 0, SHADE, 0, 0, 0, 0, PRIMITIVE, PRIMITIVE, 0, SHADE, 0, 0, 0, 0, PRIMITIVE);
    gDPSetRenderMode(g++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    gDPSetPrimColor(g++, 0, 0, col[0], col[1], col[2], 128);
    __gSPDisplayList(g++, sIcoDL);
}

// Comandos por luz: matriz + duas passadas de máscara + composição (+ debug), com folga.
constexpr size_t kGfxPerLight = 1 + (3 * 8) + 8;

} // namespace

// Chamada em todo Lights_PointSetColorAndRadius com a opção ligada. Uma chama pula de brilho de um frame para o
// outro; o salto (com uma retenção curta) separa as chamas das luzes estáveis, que ficam intactas. A chama ganha
// uma caminhada aleatória suavizada, mantendo o tom.
void FlameFlicker(Mod& mod, ShipOotPointLightColorHookV2& light) {
    if (!mod.cfg.improveFlameFlicker || light.light == nullptr) return;
    const int inMax = std::max({ light.r, light.g, light.b });
    if (inMax <= 0) return;
    auto& table = mod.lighting.flicker;
    // Toda luz que passa por aqui precisa da amostra anterior; o conjunto é pequeno e os endereços se repetem, então
    // um teto basta (zerar tudo custa um frame imperceptível).
    if (table.size() > 256) table.clear();
    auto [it, isNew] = table.try_emplace(light.light);
    FlameFlickerState& f = it->second;
    uint32_t& rng = mod.lighting.rng;
    if (isNew) {
        f = FlameFlickerState{};
        f.cur = 1.0f;
        f.prevTarget = f.nextTarget = 0.6f + (RandomZeroOne(rng) * 0.4f);
        f.maxSeen = static_cast<float>(inMax);
        f.lastInMax = inMax;
    }
    const int delta = std::abs(inMax - f.lastInMax);
    f.lastInMax = inMax;
    if (delta > 12) f.hold = 8;
    // Acompanha o brilho cheio da luz, adaptando devagar a uma chama que realmente enfraquece.
    f.maxSeen = std::max(f.maxSeen * 0.99f, static_cast<float>(inMax));
    if (f.hold <= 0) return;
    --f.hold;
    const float interval = 0.25f / std::max(mod.cfg.flickerSpeed, 0.05f);
    f.phase += mod.lighting.dt;
    while (f.phase >= interval) {
        f.phase -= interval;
        f.prevTarget = f.nextTarget;
        f.nextTarget = 0.6f + (RandomZeroOne(rng) * 0.4f); // chama preguiçosa: 60..100% do cheio
    }
    const float t = f.phase / interval;
    const float eased = t * t * (3.0f - (2.0f * t));
    f.cur = f.prevTarget + ((f.nextTarget - f.prevTarget) * eased);
    const float scale = (f.cur * f.maxSeen) / static_cast<float>(inMax);
    light.r = static_cast<uint8_t>(std::clamp(light.r * scale, 0.0f, 255.0f));
    light.g = static_cast<uint8_t>(std::clamp(light.g * scale, 0.0f, 255.0f));
    light.b = static_cast<uint8_t>(std::clamp(light.b * scale, 0.0f, 255.0f));
    ++mod.stats.flickered;
}

void WorldFrame(Mod& mod, PlayState* play) {
    LightingState& lighting = mod.lighting;
    const int16_t scene = play ? play->sceneNum : -1;
    const uint32_t frames = play ? static_cast<uint32_t>(play->state.frames) : 0;
    if (ObservePlay(lighting.stamp, play, scene, frames)) {
        // Outra carga de cena: as luzes e as fadas são outras, mesmo se o PlayState reutilizar o endereço.
        // A luz do Deku Stick já foi apagada pelo host; o destroy só solta o handle velho.
        lighting.flicker.clear();
        lighting.lights.clear();
        lighting.fairyNoGlow.clear();
        DropDekuStickLight(mod);
    }
    ShipOotRenderFrameInfoV1 info{sizeof(info)};
    lighting.dt = mod.render->get_frame_info(&info) == SHIP_NATIVE_OK && info.delta_seconds > 0.0f
                      ? info.delta_seconds
                      : 3.0f / 60.0f;
}

// Depois do update do Link: a ponta do Deku Stick já está na posição do frame.
void DekuStickLight(Mod& mod, PlayState* play) {
    const Player* player = play ? GET_PLAYER(play) : nullptr;
    // unk_860 é o tempo de queima (também usado pela vara de pesca), então o item na mão vem primeiro.
    const bool lit = mod.cfg.dekuStickLight && player != nullptr &&
                     player->heldItemAction == PLAYER_IA_DEKU_STICK && player->unk_860 != 0;
    if (!lit) {
        DropDekuStickLight(mod);
        return;
    }
    // O ruído branco de uma tocha (obj_syokudai): a tremulação o detecta e suaviza como em toda tocha.
    const float brightness = ((RandomZeroOne(mod.lighting.rng) * 127.0f) + 128.0f) / 255.0f;
    const Vec3f& tip = player->meleeWeaponInfo[0].tip;
    ShipOotPointLightV1 light{sizeof(light),
                              { tip.x, tip.y, tip.z },
                              kStickLightRadius,
                              { static_cast<uint8_t>(kStickColor[0] * brightness),
                                static_cast<uint8_t>(kStickColor[1] * brightness),
                                static_cast<uint8_t>(kStickColor[2] * brightness) },
                              0};
    if (mod.lighting.stickLight && mod.lights->update_point_light(mod.lighting.stickLight, &light) == SHIP_NATIVE_OK)
        return;
    // Handle velho (troca de cena) ou nenhum: cria de novo.
    mod.lighting.stickLight = 0;
    mod.lighting.stickInfo = nullptr;
    if (mod.lights->create_point_light(kOwner, &light, &mod.lighting.stickLight) == SHIP_NATIVE_OK) {
        mod.lights->get_point_light_info(mod.lighting.stickLight, &mod.lighting.stickInfo);
        ++mod.stats.stickLights;
    } else {
        mod.lighting.stickLight = 0;
    }
}

void DropDekuStickLight(Mod& mod) {
    if (mod.lighting.stickLight && mod.lights) mod.lights->destroy_point_light(mod.lighting.stickLight);
    mod.lighting.stickLight = 0;
    mod.lighting.stickInfo = nullptr;
}

void DrawWorldLights(Mod& mod, PlayState* play) {
    const Settings& cfg = mod.cfg;
    if (!cfg.lightCasting || play == nullptr) return;
    BuildIcosphere();
    LightingState& lighting = mod.lighting;
    ++lighting.gen;
    const float dt = lighting.dt;
    // "Movimento padrão do Wind Waker" fixa o giro e o pulso de tamanho em 1x.
    const float rotSpeed = cfg.wwDefaultMovement ? 1.0f : cfg.rotationSpeed;
    const float sizeFlicker = cfg.wwDefaultMovement ? 1.0f : cfg.sizeFlicker;
    const uint8_t alpha = PoolAlpha(cfg.lightIntensity);
    const uint8_t naviAlpha = PoolAlpha(cfg.naviIntensity);
    const uint8_t wildAlpha = PoolAlpha(cfg.wildFairyIntensity);

    // Fadas soltas vivas (floresta Kokiri e as de cura), como no fork: pela lista de atores do frame, com a luz que o
    // hook de fada viu. Só acendem com a opção; fora dela a luz fica com raio 0 e nem entra aqui.
    lighting.wildLights.clear();
    if (cfg.otherFairyLights) {
        Actor* actor = play->actorCtx.actorLists[ACTORCAT_ITEMACTION].head;
        for (; actor != nullptr; actor = actor->next) {
            if (actor->id != ACTOR_EN_ELF) continue;
            const auto found = lighting.fairyNoGlow.find(actor);
            if (found != lighting.fairyNoGlow.end()) lighting.wildLights.push_back(found->second);
        }
    }

    size_t lights = 0;
    for (LightNode* node = play->lightCtx.listHead; node != nullptr; node = node->next) ++lights;
    lighting.dl.resize((lights * kGfxPerLight) + 2);
    Gfx* g = lighting.dl.data();
    uint32_t drawn = 0;
    for (LightNode* node = play->lightCtx.listHead; node != nullptr; node = node->next) {
        LightInfo* info = node->info;
        if (info == nullptr || info->type == LIGHT_DIRECTIONAL) continue;
        const bool isNavi = info == mod.navi.glow || info == mod.navi.noGlow;
        const bool isWild = !isNavi && std::find(lighting.wildLights.begin(), lighting.wildLights.end(),
                                                 static_cast<const void*>(info)) != lighting.wildLights.end();
        const bool isStick = !isNavi && !isWild && lighting.stickInfo != nullptr && info == lighting.stickInfo;
        if (isNavi && !cfg.naviLightCasting) continue;
        WorldLightState& s = LightState(mod, info);
        const LightPoint& p = info->params.point;
        // Cor viva: a tremulação e o tom da Navi já agiram na fonte.
        const uint8_t col[3] = { p.color[0], p.color[1], p.color[2] };

        s.angleY = std::fmod(s.angleY + (kWWRotYRate * rotSpeed * dt), 2.0f * kPi);
        s.angleX = std::fmod(s.angleX + (kWWRotXRate * rotSpeed * dt), 2.0f * kPi);

        float worldRadius;
        uint8_t thisAlpha;
        if (isNavi) {
            // A Navi pisca: o raio vai de cheio a 0 quando ela entra no Link. O raio suaviza para a projeção
            // apagar em vez de estalar.
            const float target = p.radius > 0 ? static_cast<float>(p.radius) : 0.0f;
            s.spawnRadius = WWEase(s.spawnRadius, target, 0.3f, 10000.0f, dt);
            if (target <= 0.0f && s.spawnRadius < 0.5f) s.spawnRadius = 0.0f;
            worldRadius = s.spawnRadius * cfg.naviSphereSize;
            thisAlpha = naviAlpha;
        } else if (isWild) {
            worldRadius = p.radius * cfg.wildFairySphereSize; // fada não é chama: tamanho estável
            thisAlpha = wildAlpha;
        } else {
            // Pulso de tamanho do Wind Waker (o tremor dominante): novo alvo a cada 0,10..0,30 s, suavizado.
            s.sizeTimer -= dt;
            if (s.sizeTimer <= 0.0f) {
                s.sizeTimer = 0.10f + (RandomZeroOne(lighting.rng) * 0.20f);
                s.sizeTarget = 1.0f + ((RandomZeroOne(lighting.rng) - 0.5f) * 0.10f * sizeFlicker);
            }
            s.sizeCur = WWEase(s.sizeCur, s.sizeTarget, 0.4f, 0.05f, dt);
            worldRadius = p.radius * (isStick ? cfg.dekuStickSphereSize : cfg.sphereSize) * s.sizeCur;
            // Tremor de brilho fino e sutil no alfa.
            s.alphaTimer -= dt;
            if (s.alphaTimer <= 0.0f) {
                s.alphaTimer = RandomZeroOne(lighting.rng) * 0.167f;
                s.alphaTarget = 0.90f + (RandomZeroOne(lighting.rng) * 0.10f);
            }
            s.alphaCur = WWEase(s.alphaCur, s.alphaTarget, 1.0f, 0.08f, dt);
            thisAlpha = static_cast<uint8_t>(std::clamp(alpha * s.alphaCur, 0.0f, 255.0f));
        }
        if (worldRadius <= 0.0f) continue;

        // Matriz da luz num filho de interpolação: translação, giro Y, giro X e escala (a ordem do Wind Waker).
        const void* mtx = nullptr;
        ShipNativeStatus status = mod.render->interpolation_begin(info, 0);
        if (status == SHIP_NATIVE_OK) {
            status = mod.render->matrix_push();
            if (status == SHIP_NATIVE_OK) {
                const float scale = worldRadius * 0.01f;
                status = mod.render->matrix_translate_new(p.x, p.y, p.z);
                if (status == SHIP_NATIVE_OK) status = mod.render->matrix_rotate_axis(s.angleY, 0.0f, 1.0f, 0.0f);
                if (status == SHIP_NATIVE_OK) status = mod.render->matrix_rotate_axis(s.angleX, 1.0f, 0.0f, 0.0f);
                if (status == SHIP_NATIVE_OK) status = mod.render->matrix_scale(scale, scale, scale);
                if (status == SHIP_NATIVE_OK) status = mod.render->export_current_matrix(&mtx);
                mod.render->matrix_pop();
            }
            mod.render->interpolation_end();
        }
        if (status != SHIP_NATIVE_OK || mtx == nullptr) {
            ++mod.stats.poolFailures;
            continue;
        }
        gSPMatrix(g++, const_cast<void*>(mtx), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        MaskPass(g, kStencilIncr, G_CULL_FRONT); // faces de trás: stencil ++ (z-fail)
        MaskPass(g, kStencilDecr, G_CULL_BACK);  // faces da frente: stencil -- (z-fail)
        CompositePass(g, col, thisAlpha);
        if (cfg.debugLightSpheres) DebugSphere(g, col);
        ++drawn;
    }
    // O modo de stencil persiste no renderer: desliga para os atores seguintes.
    gSPStencil(g++, kStencilOff);
    gSPEndDisplayList(g++);
    if (mod.render->draw_native_display_list(lighting.dl.data(), LINKSPAN_OOT_RENDER_OPAQUE) == SHIP_NATIVE_OK) {
        mod.stats.pools += drawn;
    } else {
        ++mod.stats.poolFailures;
    }
    std::erase_if(lighting.lights, [&](const auto& entry) { return entry.second.gen != lighting.gen; });
}

} // namespace WWStyle
