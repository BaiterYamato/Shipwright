// Cel shading e sombra de ator — a política do fork ToonLighting.cpp sobre o transporte do host.
//
// O host (render V2/V3) abre o colchete toon no laço de atores e leva os opcodes ao renderer; aqui fica o que é
// política de OoT: qual luz é a chave de cada ator (a luz pontual mais próxima no alcance, senão o sol ou a lua),
// como a chave viaja de uma fonte para outra, quem fica fora do toon e quem projeta ou recebe sombra.
#include <algorithm>
#include <cmath>
#include <iterator>

#include "ww_style.h"
#include "macros.h"

namespace WWStyle {
namespace {

constexpr int16_t kNoClamp = -32768; // TOON_SHADOW_NO_CLAMP: a sombra parte dos pés capturados
constexpr float kShadowFadeTime = 0.15f;
constexpr uint32_t kPruneEvery = 64;
constexpr uint32_t kStaleFrames = 120;

// Pisos que o jogo cria como ator (ponte levadiça, ponte de Gerudo, plataformas): o host os desenha antes da
// descarga das sombras, então recebem sombra como o cenário. O pré-passe do host só percorre BG, PROP e SWITCH.
// O fork aceitava do BG_HAKA_GATE só o piso e a estátua (params & 0xFF em {0, 1}); a lista do host é por id, e o
// portão entra inteiro — a caveira e o portão continuam projetando sombra (ToonShadowExcluded).
const int16_t kShadowReceivers[] = {
    ACTOR_BG_SPOT00_HANEBASI, ACTOR_BG_SPOT09_OBJ, ACTOR_BG_MORI_BIGST, ACTOR_BG_HAKA_MEGANEBG,
    ACTOR_BG_MENKURI_KAITEN,  ACTOR_OBJ_SWITCH,    ACTOR_OBJ_BEAN,      ACTOR_BG_HAKA_GATE,
};

bool IsShadowReceiver(int16_t id) {
    return std::find(std::begin(kShadowReceivers), std::end(kShadowReceivers), id) != std::end(kShadowReceivers);
}

// Todo Bg_Spot* é cenário do overworld (pontes, cercas, portões, pedras, água do poço e do oásis). O fork
// comparava o prefixo do nome no ActorDB; a DLL não tem os nomes e lista os ids.
bool IsBgSpot(int16_t id) {
    switch (id) {
        case ACTOR_BG_SPOT00_BREAK:
        case ACTOR_BG_SPOT00_HANEBASI:
        case ACTOR_BG_SPOT01_FUSYA:
        case ACTOR_BG_SPOT01_IDOHASHIRA:
        case ACTOR_BG_SPOT01_IDOMIZU:
        case ACTOR_BG_SPOT01_IDOSOKO:
        case ACTOR_BG_SPOT01_OBJECTS2:
        case ACTOR_BG_SPOT02_OBJECTS:
        case ACTOR_BG_SPOT03_TAKI:
        case ACTOR_BG_SPOT05_SOKO:
        case ACTOR_BG_SPOT06_OBJECTS:
        case ACTOR_BG_SPOT07_TAKI:
        case ACTOR_BG_SPOT08_BAKUDANKABE:
        case ACTOR_BG_SPOT08_ICEBLOCK:
        case ACTOR_BG_SPOT09_OBJ:
        case ACTOR_BG_SPOT11_BAKUDANKABE:
        case ACTOR_BG_SPOT11_OASIS:
        case ACTOR_BG_SPOT12_GATE:
        case ACTOR_BG_SPOT12_SAKU:
        case ACTOR_BG_SPOT15_RRBOX:
        case ACTOR_BG_SPOT15_SAKU:
        case ACTOR_BG_SPOT16_BOMBSTONE:
        case ACTOR_BG_SPOT16_DOUGHNUT:
        case ACTOR_BG_SPOT17_BAKUDANKABE:
        case ACTOR_BG_SPOT17_FUNEN:
        case ACTOR_BG_SPOT18_BASKET:
        case ACTOR_BG_SPOT18_FUTA:
        case ACTOR_BG_SPOT18_OBJ:
        case ACTOR_BG_SPOT18_SHUTTER:
            return true;
        default:
            return false;
    }
}

// Fora do cel e da sombra: ficam errados reiluminados e errados projetando uma sombra achatada (portas, a Grande
// Árvore Deku, superfícies de água, árvores, e os interruptores e o feijão, que são receptores).
bool ToonActorExcluded(const Actor* actor) {
    if (actor->category == ACTORCAT_DOOR) {
        return true;
    }
    switch (actor->id) {
        case ACTOR_BG_TREEMOUTH:
        case ACTOR_BG_MIZU_WATER:
        case ACTOR_BG_HAKA_WATER:
        case ACTOR_EN_WOOD02:
        case ACTOR_OBJ_SWITCH:
        case ACTOR_OBJ_BEAN:
            return true;
        default:
            return IsBgSpot(actor->id);
    }
}

// A placa enterra o poste abaixo do chão; o renderer levanta os pés do volume até a altura do chão passada.
bool ToonShadowDeepRooted(const Actor* actor) {
    return actor->id == ACTOR_EN_KANBAN;
}

// Continuam no cel, mas sem sombra: grama pequena, Skull Kid, Deku Scrubs, Rei Zora, e os receptores (um piso
// projetando a própria silhueta no vazio fica errado). Do portão da Sombra, só o piso e a estátua são piso.
bool ToonShadowExcluded(const Actor* actor, int16_t params) {
    switch (actor->id) {
        case ACTOR_EN_KUSA:
        case ACTOR_EN_SKJ:
        case ACTOR_EN_DNT_NOMAL:
        case ACTOR_EN_KZ:
            return true;
        case ACTOR_BG_HAKA_GATE: {
            const uint16_t type = static_cast<uint16_t>(params) & 0xFF;
            return type == 0 || type == 1;
        }
        default:
            return IsShadowReceiver(actor->id);
    }
}

// SmoothDamp criticamente amortecido (Unity): acelera e desacelera até o alvo, sem passar dele.
float SmoothDamp(float current, float target, float* vel, float smoothTime, float dt) {
    smoothTime = std::max(smoothTime, 0.0001f);
    const float omega = 2.0f / smoothTime;
    const float x = omega * dt;
    const float expTerm = 1.0f / (1.0f + x + (0.48f * x * x) + (0.235f * x * x * x));
    const float change = current - target;
    const float temp = (*vel + (omega * change)) * dt;
    *vel = (*vel - (omega * temp)) * expTerm;
    return target + ((change + temp) * expTerm);
}

// Slerp de direção unitária seguro nos antípodas: gira pela esfera, então a chave não salta quando a luz
// dominante passa para o lado oposto (a luz de uma fada apagando).
void Slerp(const float from[3], const float to[3], float t, float out[3]) {
    float dot = std::clamp((from[0] * to[0]) + (from[1] * to[1]) + (from[2] * to[2]), -1.0f, 1.0f);
    if (dot > 0.9995f) {
        for (int i = 0; i < 3; ++i) out[i] = from[i] + ((to[i] - from[i]) * t);
        const float len = std::sqrt((out[0] * out[0]) + (out[1] * out[1]) + (out[2] * out[2]));
        if (len > 0.0001f) {
            for (int i = 0; i < 3; ++i) out[i] /= len;
        }
        return;
    }
    if (dot < -0.9995f) {
        // Quase opostos: o grande círculo é ambíguo; gira em volta de um eixo perpendicular qualquer.
        const float ref[3] = { std::fabs(from[0]) < 0.9f ? 1.0f : 0.0f, std::fabs(from[0]) < 0.9f ? 0.0f : 1.0f, 0.0f };
        const float d = (ref[0] * from[0]) + (ref[1] * from[1]) + (ref[2] * from[2]);
        float perp[3] = { ref[0] - (from[0] * d), ref[1] - (from[1] * d), ref[2] - (from[2] * d) };
        const float len = std::sqrt((perp[0] * perp[0]) + (perp[1] * perp[1]) + (perp[2] * perp[2]));
        if (len > 0.0001f) {
            for (int i = 0; i < 3; ++i) perp[i] /= len;
        }
        const float angle = t * 3.14159265f;
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        for (int i = 0; i < 3; ++i) out[i] = (from[i] * c) + (perp[i] * s);
        return;
    }
    const float theta = std::acos(dot);
    const float sinTheta = std::sin(theta);
    const float s0 = std::sin((1.0f - t) * theta) / sinTheta;
    const float s1 = std::sin(t * theta) / sinTheta;
    for (int i = 0; i < 3; ++i) out[i] = (s0 * from[i]) + (s1 * to[i]);
}

// A luz pontual mais próxima no alcance (raio × pointRange) vence; o brilho não conta, então tocha tremendo não
// muda a chave. Com a Navi desligada da seleção, as duas luzes dela saem por endereço.
bool ClosestPointLight(Mod& mod, PlayState* play, const Actor* actor, float dirOut[3], float colOut[3]) {
    const void* naviGlow = mod.cfg.useNaviLight ? nullptr : mod.navi.glow;
    const void* naviNoGlow = mod.cfg.useNaviLight ? nullptr : mod.navi.noGlow;
    float bestDistSq = -1.0f;
    for (LightNode* node = play->lightCtx.listHead; node != nullptr; node = node->next) {
        const LightInfo* info = node->info;
        if (info == nullptr || info->type == LIGHT_DIRECTIONAL) continue;
        if (info == naviGlow || info == naviNoGlow) {
            ++mod.stats.naviSkipped;
            continue;
        }
        const float dx = info->params.point.x - actor->world.pos.x;
        const float dy = info->params.point.y - actor->world.pos.y;
        const float dz = info->params.point.z - actor->world.pos.z;
        const float radius = info->params.point.radius * mod.cfg.pointLightRange;
        const float distSq = (dx * dx) + (dy * dy) + (dz * dz);
        if (radius > 0.0f && distSq > 0.0001f && distSq < (radius * radius) &&
            (bestDistSq < 0.0f || distSq < bestDistSq)) {
            const float dist = std::sqrt(distSq);
            bestDistSq = distSq;
            dirOut[0] = dx / dist;
            dirOut[1] = dy / dist;
            dirOut[2] = dz / dist;
            colOut[0] = info->params.point.color[0] / 255.0f;
            colOut[1] = info->params.point.color[1] / 255.0f;
            colOut[2] = info->params.point.color[2] / 255.0f;
        }
    }
    return bestDistSq >= 0.0f;
}

// Sol ou lua, o que estiver mais claro agora (acompanha o dia e a noite).
void EnvKey(PlayState* play, float dirOut[3], float colOut[3]) {
    const LightInfo* sun = &play->envCtx.dirLight1;
    const LightInfo* moon = &play->envCtx.dirLight2;
    const int sunLum = sun->params.dir.color[0] + sun->params.dir.color[1] + sun->params.dir.color[2];
    const int moonLum = moon->params.dir.color[0] + moon->params.dir.color[1] + moon->params.dir.color[2];
    const LightInfo* env = moonLum > sunLum ? moon : sun;
    const float d0 = env->params.dir.x;
    const float d1 = env->params.dir.y;
    const float d2 = env->params.dir.z;
    const float len = std::sqrt((d0 * d0) + (d1 * d1) + (d2 * d2));
    if (len > 0.001f) {
        dirOut[0] = d0 / len;
        dirOut[1] = d1 / len;
        dirOut[2] = d2 / len;
        colOut[0] = env->params.dir.color[0] / 255.0f;
        colOut[1] = env->params.dir.color[1] / 255.0f;
        colOut[2] = env->params.dir.color[2] / 255.0f;
    }
}

uint8_t ToByte(float value) {
    return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
}

// Chão sob o ator: o floorPoly do bg check, senão o raycast em cache do estado.
bool FloorUnder(Mod& mod, const Actor* actor, ToonKeyState& st, float* floorHeight) {
    if (actor->floorPoly != nullptr) {
        *floorHeight = actor->floorHeight;
        return true;
    }
    const float mdx = actor->world.pos.x - st.floorPos[0];
    const float mdy = actor->world.pos.y - st.floorPos[1];
    const float mdz = actor->world.pos.z - st.floorPos[2];
    if (!st.floorSampled || ((mdx * mdx) + (mdy * mdy) + (mdz * mdz)) > 16.0f) {
        ShipOotWorldHitV1 hit{sizeof(hit)};
        st.floorValid = mod.world &&
                        mod.world->raycast_floor(actor->world.pos.x, actor->world.pos.y + 1.0f, actor->world.pos.z,
                                                 &hit) == SHIP_NATIVE_OK &&
                        hit.hit != 0;
        st.floorY = st.floorValid ? hit.pos[1] : 0.0f;
        st.floorSampled = true;
        st.floorPos[0] = actor->world.pos.x;
        st.floorPos[1] = actor->world.pos.y;
        st.floorPos[2] = actor->world.pos.z;
        ++mod.stats.raycasts;
    }
    if (st.floorValid) *floorHeight = st.floorY;
    return st.floorValid;
}

// Light Source Viewer (DrawDebugOverlay do fork): um espinho por luz candidata, na cor da luz e mais longo quanto
// mais forte; um anel ciano no alcance de cada luz pontual, só na passada do Link; e a agulha magenta da chave
// escolhida. Translúcido e sem teste de depth, para todo raio ficar visível.
constexpr float kPi = 3.14159265f;
constexpr size_t kDebugGfxCap = 4096;
constexpr size_t kGfxPerRay = 7;

// Espinho fino de 4 lados sobre +Y (base na origem, ponta em y=1) e anel de 12 segmentos no plano XZ com raio-base
// 100 (vértices internos e externos alternados), os dois do fork.
Vtx sRayVtx[5];
Gfx sRayDL[5];
Vtx sRingVtx[24];
Gfx sRingDL[14];
bool sDebugBuilt = false;

void WriteDebugVert(Vtx& v, int16_t x, int16_t y, int16_t z) {
    v = Vtx{};
    v.v.ob[0] = x;
    v.v.ob[1] = y;
    v.v.ob[2] = z;
    v.v.cn[0] = v.v.cn[1] = v.v.cn[2] = v.v.cn[3] = 0xFF;
}

void BuildDebugGeometry() {
    const int16_t ray[5][3] = { { -1, 0, -1 }, { 1, 0, -1 }, { 1, 0, 1 }, { -1, 0, 1 }, { 0, 1, 0 } };
    for (int i = 0; i < 5; ++i) WriteDebugVert(sRayVtx[i], ray[i][0], ray[i][1], ray[i][2]);
    Gfx* g = sRayDL;
    __gSPVertex(g++, reinterpret_cast<uintptr_t>(sRayVtx), 5, 0);
    gSP2Triangles(g++, 0, 1, 4, 0, 1, 2, 4, 0);
    gSP2Triangles(g++, 2, 3, 4, 0, 3, 0, 4, 0);
    gSP2Triangles(g++, 0, 2, 1, 0, 0, 3, 2, 0);
    gSPEndDisplayList(g++);

    const int16_t ring[24][2] = { { 97, 0 },    { 103, 0 },   { 84, 48 },   { 89, 52 },   { 48, 84 },   { 52, 89 },
                                  { 0, 97 },    { 0, 103 },   { -48, 84 },  { -52, 89 },  { -84, 48 },  { -89, 52 },
                                  { -97, 0 },   { -103, 0 },  { -84, -48 }, { -89, -52 }, { -48, -84 }, { -52, -89 },
                                  { 0, -97 },   { 0, -103 },  { 48, -84 },  { 52, -89 },  { 84, -48 },  { 89, -52 } };
    for (int i = 0; i < 24; ++i) WriteDebugVert(sRingVtx[i], ring[i][0], 0, ring[i][1]);
    g = sRingDL;
    __gSPVertex(g++, reinterpret_cast<uintptr_t>(sRingVtx), 24, 0);
    for (int i = 0; i < 24; i += 2) {
        const int n = (i + 2) % 24;
        gSP2Triangles(g++, i, i + 1, n + 1, 0, i, n + 1, n, 0);
    }
    gSPEndDisplayList(g++);
    sDebugBuilt = true;
}

// Matriz corrente trocada pela translação (o MTXMODE_NEW do fork), exportada e devolvida ao host.
template <typename Transform> const void* DebugMatrix(Mod& mod, const float at[3], Transform&& transform) {
    const void* mtx = nullptr;
    if (mod.render->matrix_push() != SHIP_NATIVE_OK) return nullptr;
    ShipNativeStatus status = mod.render->matrix_translate_new(at[0], at[1], at[2]);
    if (status == SHIP_NATIVE_OK) status = transform();
    if (status == SHIP_NATIVE_OK) status = mod.render->export_current_matrix(&mtx);
    mod.render->matrix_pop();
    return status == SHIP_NATIVE_OK ? mtx : nullptr;
}

void DebugMaterial(Gfx*& g, uint8_t r, uint8_t gr, uint8_t b, uint8_t a, const void* mtx) {
    gDPPipeSync(g++);
    gSPClearGeometryMode(g++, G_LIGHTING | G_CULL_BACK | G_CULL_FRONT);
    gDPSetCombineLERP(g++, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE);
    gDPSetRenderMode(g++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    gDPSetPrimColor(g++, 0, 0, r, gr, b, a);
    gSPMatrix(g++, const_cast<void*>(mtx), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
}

// O espinho gira de +Y para `dir` em volta da perpendicular dos dois e escala para `length` × `thickness`.
bool DebugRay(Mod& mod, Gfx*& g, const float base[3], const float dir[3], const uint8_t col[3], float length,
              float thickness) {
    const float horiz = std::sqrt((dir[0] * dir[0]) + (dir[2] * dir[2]));
    const void* mtx = DebugMatrix(mod, base, [&] {
        ShipNativeStatus status = SHIP_NATIVE_OK;
        if (horiz > 0.001f) {
            // cross((0,1,0), dir) normalizado = (dir.z, 0, -dir.x)
            status = mod.render->matrix_rotate_axis(std::atan2(horiz, dir[1]), dir[2] / horiz, 0.0f, -dir[0] / horiz);
        } else if (dir[1] < 0.0f) {
            status = mod.render->matrix_rotate_axis(kPi, 1.0f, 0.0f, 0.0f); // para baixo
        }
        return status == SHIP_NATIVE_OK ? mod.render->matrix_scale(thickness, length, thickness) : status;
    });
    if (mtx == nullptr) return false;
    DebugMaterial(g, col[0], col[1], col[2], 200, mtx);
    __gSPDisplayList(g++, sRayDL);
    return true;
}

bool DebugRing(Mod& mod, Gfx*& g, const float center[3], float radius) {
    const void* mtx =
        DebugMatrix(mod, center, [&] { return mod.render->matrix_scale(radius * 0.01f, 1.0f, radius * 0.01f); });
    if (mtx == nullptr) return false;
    DebugMaterial(g, 0, 255, 255, 110, mtx);
    __gSPDisplayList(g++, sRingDL);
    return true;
}

void DrawDebugOverlay(Mod& mod, PlayState* play, const Actor* actor, const float chosenDir[3]) {
    ToonState& toon = mod.toon;
    size_t pointLights = 0;
    for (LightNode* node = play->lightCtx.listHead; node != nullptr; node = node->next) {
        if (node->info != nullptr && node->info->type != LIGHT_DIRECTIONAL) ++pointLights;
    }
    // Pior caso deste ator: sol, lua, raio e anel por luz pontual e a agulha, mais o fim da lista.
    const size_t need = ((3 + (2 * pointLights)) * kGfxPerRay) + 1;
    if (toon.debugGfx.size() < kDebugGfxCap || toon.debugUsed + need > toon.debugGfx.size()) {
        ++mod.stats.debugSkipped;
        return;
    }
    if (!sDebugBuilt) BuildDebugGeometry();
    Gfx* const start = &toon.debugGfx[toon.debugUsed];
    Gfx* g = start;
    uint32_t drawn = 0;
    const float base[3] = { actor->world.pos.x, actor->world.pos.y + 30.0f, actor->world.pos.z };
    const Player* player = GET_PLAYER(play);
    // Os anéis saem uma vez, na passada do Link, e não em todo ator da cena.
    const bool isPlayer = player != nullptr && actor == &player->actor;

    for (const LightInfo* env : { &play->envCtx.dirLight1, &play->envCtx.dirLight2 }) {
        const LightDirectional& d = env->params.dir;
        const float len = std::sqrt(static_cast<float>((d.x * d.x) + (d.y * d.y) + (d.z * d.z)));
        const float lum = (d.color[0] + d.color[1] + d.color[2]) / (3.0f * 255.0f);
        if (len <= 0.001f) continue;
        const float dir[3] = { d.x / len, d.y / len, d.z / len };
        if (DebugRay(mod, g, base, dir, d.color, 10.0f + (std::min(lum, 1.0f) * 15.0f), 1.2f)) ++drawn;
    }

    for (LightNode* node = play->lightCtx.listHead; node != nullptr; node = node->next) {
        const LightInfo* info = node->info;
        if (info == nullptr || info->type == LIGHT_DIRECTIONAL) continue;
        const LightPoint& p = info->params.point;
        const float dx = p.x - actor->world.pos.x;
        const float dy = p.y - actor->world.pos.y;
        const float dz = p.z - actor->world.pos.z;
        const float radius = p.radius * mod.cfg.pointLightRange;
        const float distSq = (dx * dx) + (dy * dy) + (dz * dz);
        // Anel em toda luz pontual, perto ou não, para o alcance do slider Point Light Range aparecer.
        if (isPlayer && radius > 0.0f) {
            const float center[3] = { static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z) };
            if (DebugRing(mod, g, center, radius)) ++drawn;
        }
        if (radius <= 0.0f || distSq >= radius * radius) continue;
        const float dist = std::sqrt(distSq);
        if (dist <= 0.001f) continue;
        const float scale = 1.0f - ((dist / radius) * (dist / radius));
        const float att = 0.5f + (0.5f * scale); // queda com a distância, só no comprimento visual
        const float lum = ((p.color[0] + p.color[1] + p.color[2]) / (3.0f * 255.0f)) * att;
        const float dir[3] = { dx / dist, dy / dist, dz / dist };
        const uint8_t col[3] = { static_cast<uint8_t>(p.color[0] * att), static_cast<uint8_t>(p.color[1] * att),
                                 static_cast<uint8_t>(p.color[2] * att) };
        if (DebugRay(mod, g, base, dir, col, 10.0f + (std::min(lum, 2.0f) * 12.5f), 1.2f)) ++drawn;
    }

    // A chave escolhida: agulha magenta fina no meio do cone da luz, que continua visível na cor dela.
    const uint8_t magenta[3] = { 255, 0, 255 };
    if (DebugRay(mod, g, base, chosenDir, magenta, 35.0f, 0.3f)) ++drawn;
    if (drawn == 0) return;
    gSPEndDisplayList(g++);
    toon.debugUsed += static_cast<size_t>(g - start);
    if (mod.render->draw_native_display_list(start, LINKSPAN_OOT_RENDER_TRANSLUCENT) == SHIP_NATIVE_OK) {
        mod.stats.debugRays += drawn;
    } else {
        ++mod.stats.debugSkipped;
    }
}

} // namespace

ShipNativeStatus ApplyRenderState(Mod& mod) {
    const Settings& cfg = mod.cfg;
    uint32_t features = 0;
    if (cfg.celEnabled) features |= LINKSPAN_OOT_RENDER_FEATURE_TOON_ACTORS;
    if (cfg.shadowsEnabled && cfg.suppressVanillaShadows)
        features |= LINKSPAN_OOT_RENDER_FEATURE_SUPPRESS_VANILLA_SHADOWS;
    // A projeção substitui o brilho desenhado no ponto das luzes GLOW (tochas, fogo).
    if (cfg.lightCasting && cfg.hideVanillaGlow) features |= LINKSPAN_OOT_RENDER_FEATURE_HIDE_VANILLA_POINT_GLOW;
    // Sem receptores o host não descarrega os volumes de sombra; a lista vai sempre que a sombra está ligada.
    const bool receivers = cfg.shadowsEnabled;
    ShipNativeStatus status = mod.render->set_state(
        mod.renderState, features, receivers ? kShadowReceivers : nullptr,
        receivers ? static_cast<uint32_t>(std::size(kShadowReceivers)) : 0u);
    if (status != SHIP_NATIVE_OK || (!cfg.celEnabled && !cfg.shadowsEnabled)) return status;
    status = mod.render->set_toon_ramp(cfg.rampCenter, cfg.rampSoftness, cfg.highlightIntensity, cfg.shadowIntensity,
                                       cfg.debugHighlightBands ? 1 : 0);
    if (status != SHIP_NATIVE_OK) return status;
    // Comprimento da sombra: quanto a chave é levantada antes de projetar. 0 => 0,95 (curta, sob o ator); 1 => 0,10.
    const float minElevation = 0.95f - (std::clamp(cfg.shadowLength, 0.0f, 1.0f) * 0.85f);
    return mod.render->set_toon_shadow_params(cfg.shadowOpacity, minElevation, cfg.shadowSlabDepth,
                                              cfg.shadowSlabRise, cfg.shadowEdgeSoftness,
                                              cfg.debugShadowVolume ? 1 : 0);
}

void ToonFrame(Mod& mod, PlayState* play) {
    ToonState& toon = mod.toon;
    ++toon.frame;
    toon.debugUsed = 0;
    if (mod.cfg.debugLightSources && toon.debugGfx.size() < kDebugGfxCap) toon.debugGfx.resize(kDebugGfxCap);
    if (play != toon.play) {
        toon.keys.clear();
        toon.play = play;
        mod.navi = {};
        mod.litFairies.clear();
    }
    if (!mod.cfg.celEnabled && !mod.cfg.shadowsEnabled) {
        // Desligado, o estado suavizado ficaria velho para quando religar.
        toon.keys.clear();
        return;
    }
    ShipOotRenderFrameInfoV1 info{sizeof(info)};
    toon.dt = mod.render->get_frame_info(&info) == SHIP_NATIVE_OK && info.delta_seconds > 0.0f ? info.delta_seconds
                                                                                               : 3.0f / 60.0f;
    const float transition = std::max(mod.cfg.transitionTime, 0.05f);
    toon.alpha = 1.0f - std::exp(-4.6f * toon.dt / transition);
    // A Navi registrada precisa continuar sendo a fada do Link; senão o endereço pode ser de outra coisa.
    const Player* player = play ? GET_PLAYER(play) : nullptr;
    if (player == nullptr || player->naviActor != mod.navi.actor) mod.navi = {};
    if (toon.frame % kPruneEvery == 0) {
        std::erase_if(toon.keys, [&](const auto& entry) { return toon.frame - entry.second.lastFrame > kStaleFrames; });
    }
}

void ToonActorDraw(Mod& mod, const ShipOotRenderActorDrawHookV1& payload) {
    const Settings& cfg = mod.cfg;
    if (!cfg.celEnabled && !cfg.shadowsEnabled) return;
    auto* play = static_cast<PlayState*>(payload.play_state);
    auto* actor = static_cast<Actor*>(payload.actor);
    if (play == nullptr || actor == nullptr) return;
    ++mod.stats.actorDraws;

    // Excluídos ficam com a luz vanilla (o host religa o colchete no ator seguinte) e marcam a borda da captura
    // de sombra, para a silhueta do ator anterior não engolir esta geometria (com o cel ligado a borda do
    // colchete já faz isso).
    if (ToonActorExcluded(actor)) {
        ++mod.stats.excluded;
        if (cfg.celEnabled && mod.render->set_actor_toon_enabled(0) == SHIP_NATIVE_OK) ++mod.stats.toonOff;
        if (cfg.shadowsEnabled) mod.render->emit_toon_shadow(LINKSPAN_OOT_RENDER_OPAQUE, kNoClamp, 0.0f);
        return;
    }

    float targetDir[3] = { 0.0f, 1.0f, 0.0f };
    float targetCol[3] = { 1.0f, 1.0f, 1.0f };
    if (ClosestPointLight(mod, play, actor, targetDir, targetCol)) {
        ++mod.stats.pointKeys;
    } else {
        EnvKey(play, targetDir, targetCol);
        ++mod.stats.envKeys;
    }

    auto [it, isNew] = mod.toon.keys.try_emplace(actor);
    ToonKeyState& st = it->second;
    if (isNew || st.actorId != actor->id) {
        st = ToonKeyState{};
        std::copy(targetDir, targetDir + 3, st.dir);
        std::copy(targetCol, targetCol + 3, st.col);
        st.actorId = actor->id; // shadowScale 0: a sombra cresce na primeira aparição
    } else {
        float next[3];
        Slerp(st.dir, targetDir, mod.toon.alpha, next);
        std::copy(next, next + 3, st.dir);
        for (int i = 0; i < 3; ++i) {
            st.col[i] = SmoothDamp(st.col[i], targetCol[i], &st.colVel[i], cfg.transitionTime, mod.toon.dt);
        }
    }
    st.lastFrame = mod.toon.frame;

    // Toda borda do colchete invalida a chave no renderer, e um ator excluído abre uma: a chave vai em todo ator,
    // nas duas camadas, sem o dedup do fork.
    const auto dx = static_cast<int8_t>(st.dir[0] * 127.0f);
    const auto dy = static_cast<int8_t>(st.dir[1] * 127.0f);
    const auto dz = static_cast<int8_t>(st.dir[2] * 127.0f);
    const uint8_t r = ToByte(st.col[0]);
    const uint8_t g = ToByte(st.col[1]);
    const uint8_t b = ToByte(st.col[2]);
    if (mod.render->emit_toon_key(LINKSPAN_OOT_RENDER_OPAQUE, dx, dy, dz, r, g, b) != SHIP_NATIVE_OK ||
        mod.render->emit_toon_key(LINKSPAN_OOT_RENDER_TRANSLUCENT, dx, dy, dz, r, g, b) != SHIP_NATIVE_OK) {
        ++mod.stats.keyFailures;
    }
    if (cfg.debugLightSources) DrawDebugOverlay(mod, play, actor, st.dir);

    if (!cfg.shadowsEnabled) {
        // Religar a sombra a faz crescer de novo em vez de estalar no tamanho congelado.
        st.shadowScale = 0.0f;
        st.shadowScaleVel = 0.0f;
        return;
    }
    // A sombra aparece com o ator no chão ou perto dele, dentro da distância e fora da parede (escada, parede de
    // escalar, borda), onde a lâmina cortaria a parede. O tamanho suaviza 0..1 em vez de estalar.
    bool onWall = false;
    if (actor->id == ACTOR_PLAYER) {
        const auto* player = reinterpret_cast<const Player*>(actor);
        onWall = (player->stateFlags1 & (PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CLIMBING_LEDGE |
                                         PLAYER_STATE1_CLIMBING_LADDER)) != 0;
    }
    bool hasFloor = false;
    float floorHeight = actor->floorHeight;
    // O limite de baixo importa com o culling estendido, que desenha atores atrás da câmera (z projetado negativo).
    const float projZ = actor->projectedPos.z;
    if (!ToonShadowExcluded(actor, payload.params) && projZ < static_cast<float>(cfg.shadowMaxDistance) &&
        projZ > -100.0f && FloorUnder(mod, actor, st, &floorHeight)) {
        const float distToFloor = actor->world.pos.y - floorHeight;
        hasFloor = distToFloor > -50.0f && distToFloor < 1500.0f;
    }
    st.shadowScale = SmoothDamp(st.shadowScale, (hasFloor && !onWall) ? 1.0f : 0.0f, &st.shadowScaleVel,
                                kShadowFadeTime, mod.toon.dt);
    if (st.shadowScale > 0.01f) {
        const float clampY = std::clamp(floorHeight, -32767.0f, 32767.0f);
        const int16_t feetClamp = ToonShadowDeepRooted(actor) ? static_cast<int16_t>(clampY) : kNoClamp;
        mod.render->emit_toon_shadow(LINKSPAN_OOT_RENDER_OPAQUE, feetClamp, st.shadowScale);
        ++mod.stats.shadowsArmed;
    } else {
        mod.render->emit_toon_shadow(LINKSPAN_OOT_RENDER_OPAQUE, kNoClamp, 0.0f); // desarma
        ++mod.stats.shadowsOff;
    }
}

} // namespace WWStyle
