#include "HatPhysics.h"
#include "hat_chain.h"
#include "../Weather/WindField.h"
#include <libultraship/bridge/consolevariablebridge.h>
#include <spdlog/spdlog.h>
extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
}
#include "functions.h"

namespace {
hat::Chain chain;
hat::Config config;
PlayState* ownerPlay = nullptr;
Player* owner = nullptr;
Player* drawing = nullptr;
int scene = -1, room = -1, age = -1;
unsigned lastTick = 0;
bool hasTick = false, haveTorso = false;
MtxF torso;
float anchor = 700;

bool Enabled() {
    // Padrão 0: muda o visual vanilla mesmo sem shaderpack (OOT-VANILLA-001, sem mods = upstream).
    return CVarGetInteger("gDeveloperTools.HatPhysics", 0) != 0;
}
bool Paused(const PlayState* p) {
    return p->pauseCtx.state != 0 || p->pauseCtx.debugState != 0;
}
bool Real(PlayState* p, Player* player) {
    // REFLECTION permanece ativo também no draw real; testar a escala, não a flag.
    return p && player && player == GET_PLAYER(p) && player->actor.scale.y > 0 && player->csAction == 0 &&
           p->csCtx.state == CS_STATE_IDLE &&
           !(player->stateFlags1 & (PLAYER_STATE1_LOADING | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_IN_ITEM_CS));
}
hat::Vec3 Point(const MtxF& m, hat::Vec3 p) {
    return { m.xw + m.xx * p.x + m.xy * p.y + m.xz * p.z, m.yw + m.yx * p.x + m.yy * p.y + m.yz * p.z,
             m.zw + m.zx * p.x + m.zy * p.y + m.zz * p.z };
}
// Colunas da matriz do host: remove escala uniforme; rejeita shear/reflexão.
bool Orientation(const MtxF& m, hat::Quat& out, float& scale) {
    hat::Vec3 x{ m.xx, m.yx, m.zx }, y{ m.xy, m.yy, m.zy }, z{ m.xz, m.yz, m.zz };
    float sx = hat::Length(x), sy = hat::Length(y), sz = hat::Length(z);
    if (!hat::Finite(x) || !hat::Finite(y) || !hat::Finite(z) || !hat::Finite(Point(m, {})) || sx < 1e-5f ||
        sy < 1e-5f || sz < 1e-5f)
        return false;
    scale = (sx + sy + sz) / 3;
    x = x / sx;
    y = y / sy;
    z = z / sz;
    if (std::abs(sx - scale) > scale * .05f || std::abs(sy - scale) > scale * .05f ||
        std::abs(sz - scale) > scale * .05f || std::abs(hat::Dot(x, y)) > .02f || std::abs(hat::Dot(x, z)) > .02f ||
        std::abs(hat::Dot(y, z)) > .02f || hat::Dot(hat::Cross(x, y), z) < .98f)
        return false;
    float trace = x.x + y.y + z.z;
    hat::Quat q;
    if (trace > 0) {
        float s = 2 * std::sqrt(trace + 1);
        q = { s * .25f, (y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s };
    } else if (x.x > y.y && x.x > z.z) {
        float s = 2 * std::sqrt(1 + x.x - y.y - z.z);
        q = { (y.z - z.y) / s, s * .25f, (y.x + x.y) / s, (z.x + x.z) / s };
    } else if (y.y > z.z) {
        float s = 2 * std::sqrt(1 + y.y - x.x - z.z);
        q = { (z.x - x.z) / s, (y.x + x.y) / s, s * .25f, (z.y + y.z) / s };
    } else {
        float s = 2 * std::sqrt(1 + z.z - x.x - y.y);
        q = { (x.y - y.x) / s, (z.x + x.z) / s, (z.y + y.z) / s, s * .25f };
    }
    out = hat::Unit(q);
    return true;
}
void Forget() {
    chain.Clear();
    hasTick = false;
    ownerPlay = nullptr;
    owner = nullptr;
}
} // namespace

extern "C" void HatPhysics_BeginDraw(PlayState* play, void* data, int gameplayPath) {
    drawing = nullptr;
    haveTorso = false;
    if (!Enabled()) {
        Forget();
        return;
    }
    // Compara void* antes de ler Player: o inventário usa outro tipo de data.
    if (gameplayPath && play && data == GET_PLAYER(play) && Real(play, static_cast<Player*>(data)))
        drawing = static_cast<Player*>(data);
}
extern "C" void HatPhysics_EndDraw(void) {
    drawing = nullptr;
    haveTorso = false;
}

extern "C" void HatPhysics_OnLimb(PlayState* play, Player* player, int limb) {
    if (drawing != player || !Real(play, player) || !Enabled())
        return;
    if (limb == PLAYER_LIMB_UPPER) {
        Matrix_Get(&torso);
        haveTorso = true;
        return;
    }
    if (limb != PLAYER_LIMB_HEAD || Paused(play) || !haveTorso)
        return;
    unsigned tick = play->gameplayFrames;
    const bool adult = LINK_IS_ADULT;
    bool changed = ownerPlay != play || owner != player || scene != play->sceneNum ||
                   room != play->roomCtx.curRoom.num || age != static_cast<int>(adult);
    if (!changed && hasTick && tick == lastTick)
        return;
    MtxF head, animated;
    Matrix_Get(&head); // Ainda é a matriz mundial da cabeça, antes de visitar filhos.
    auto* limbData = static_cast<LodLimb*>(SEGMENTED_TO_VIRTUAL(player->skelAnime.skeleton[PLAYER_LIMB_HAT - 1]));
    Vec3f joint{ static_cast<float>(limbData->jointPos.x), static_cast<float>(limbData->jointPos.y),
                 static_cast<float>(limbData->jointPos.z) };
    Vec3s rotation = player->skelAnime.jointTable[PLAYER_LIMB_HAT];
    anchor = adult ? 700.0f : 500.0f;
    Matrix_Push();
    Matrix_TranslateRotateZYX(&joint, &rotation);
    Matrix_Translate(anchor, 0, 0, MTXMODE_APPLY); // Dobradiça virtual no topo da cabeça.
    Matrix_Get(&animated);
    Matrix_Pop();
    hat::Input input;
    float scale;
    if (!Orientation(animated, input.root.orientation, scale)) {
        Forget();
        return;
    }
    input.root.position = Point(animated, {});
    hat::Config wanted = hat::ForAge(adult);
    const float ratio = scale / .01f;
    wanted.length *= ratio;
    wanted.thickness *= ratio;
    if (changed || std::abs(config.length - wanted.length) > .001f) {
        config = wanted;
        chain = hat::Chain(config);
        changed = true;
    }
    auto local = [&](const MtxF& matrix, hat::Vec3 point) { return hat::Local(input.root, Point(matrix, point)); };
    // Cabeça em seu próprio osso; ombros/tronco seguem UPPER, não a rotação da cabeça.
    input.colliders.spheres[0] = { local(head, { 350, 0, 0 }), (adult ? 5.5f : 4.3f) * ratio };
    input.colliders.spheres[1] = { local(torso, { 0, 0, 700 }), (adult ? 4.2f : 3.2f) * ratio };
    input.colliders.spheres[2] = { local(torso, { 0, 0, -700 }), (adult ? 4.2f : 3.2f) * ratio };
    input.colliders.torso = { local(torso, { 0, 0, 0 }), local(torso, { -1000, 0, 0 }), (adult ? 6.0f : 4.5f) * ratio };
    float position[3]{ input.root.position.x, input.root.position.y, input.root.position.z };
    float wind[3]{};
    WindField_Sample(position, wind);
    input.wind = { wind[0], wind[1], wind[2] };
    input.underwater = (player->stateFlags2 & PLAYER_STATE2_UNDERWATER) != 0;
    // Troca de cena/sala/ator/idade, rewind ou lacuna de draw não acumula energia.
    input.discontinuity = changed || !hasTick || tick - lastTick != 1;
    chain.Step(input);
    // Diagnóstico por CVar (padrão desligado): ângulo da correção por tick, para medir a resposta.
    if (CVarGetInteger("gDeveloperTools.HatPhysicsLog", 0) != 0) {
        const auto& out = chain.Read();
        const float w = std::clamp(std::abs(out.correction.w), 0.0f, 1.0f);
        SPDLOG_INFO("HatPhysics: tick {} correcao {:.1f} graus vel ({:.0f}, {:.0f}) vento ({:.0f}, {:.0f}) viavel {}",
                    tick, 2.0f * std::acos(w) * 57.29578f, player->actor.velocity.x * 20, player->actor.velocity.z * 20,
                    input.wind.x, input.wind.z, out.feasible);
    }
    ownerPlay = play;
    owner = player;
    scene = play->sceneNum;
    room = play->roomCtx.curRoom.num;
    age = static_cast<int>(adult);
    lastTick = tick;
    hasTick = true;
}

extern "C" int HatPhysics_Apply(PlayState* play, Player* player, Vec3f* pos, Vec3s* rot) {
    if (drawing != player || !Real(play, player) || !Enabled() || !hasTick || ownerPlay != play || owner != player ||
        scene != play->sceneNum || room != play->roomCtx.curRoom.num || age != static_cast<int>(LINK_IS_ADULT) ||
        !chain.Read().feasible || (!Paused(play) && lastTick != play->gameplayFrames))
        return 0;
    const auto q = chain.Read().correction;
    // Não toca matriz/pos/rot para a correção identidade, inclusive no primeiro draw.
    if (q.x * q.x + q.y * q.y + q.z * q.z < 1e-12f)
        return 0;
    MtxF m{};
    m.xx = 1 - 2 * (q.y * q.y + q.z * q.z);
    m.xy = 2 * (q.x * q.y - q.z * q.w);
    m.xz = 2 * (q.x * q.z + q.y * q.w);
    m.yx = 2 * (q.x * q.y + q.z * q.w);
    m.yy = 1 - 2 * (q.x * q.x + q.z * q.z);
    m.yz = 2 * (q.y * q.z - q.x * q.w);
    m.zx = 2 * (q.x * q.z - q.y * q.w);
    m.zy = 2 * (q.y * q.z + q.x * q.w);
    m.zz = 1 - 2 * (q.x * q.x + q.y * q.y);
    m.ww = 1;
    // head * T(joint) * R(anim) * T(topo) * R(correção) * T(-topo).
    // Mesma ordem ZYX do esqueleto, sem somar Euler nem perder o cone.
    Matrix_TranslateRotateZYX(pos, rot);
    Matrix_Translate(anchor, 0, 0, MTXMODE_APPLY);
    Matrix_Mult(&m, MTXMODE_APPLY);
    Matrix_Translate(-anchor, 0, 0, MTXMODE_APPLY);
    *pos = { 0, 0, 0 };
    *rot = { 0, 0, 0 }; // O traversal ainda emite a dList e visita filhos normalmente.
    return 1;
}
