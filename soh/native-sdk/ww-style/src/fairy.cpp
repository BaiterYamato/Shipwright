// Luzes das fadas (oot.light.fairy): o tom da luz da Navi, a identidade das luzes dela para a seleção da chave
// toon, e a luz das fadas soltas, que no vanilla não iluminam nada. Porta os trechos do fork em z_en_elf.c.
#include <algorithm>

#include "ww_style.h"
#include "macros.h"

namespace WWStyle {

void FairyLights(Mod& mod, ShipOotFairyLightHookV1& payload) {
    ++mod.stats.fairyCalls;
    const Settings& cfg = mod.cfg;
    auto* play = static_cast<PlayState*>(payload.play_state);
    if (payload.params == LINKSPAN_OOT_FAIRY_NAVI) {
        const Player* player = play ? GET_PLAYER(play) : nullptr;
        if (player != nullptr && player->naviActor == payload.actor) {
            mod.navi = { payload.actor, payload.glow.light, payload.no_glow.light };
            ++mod.stats.naviSeen;
        }
        // A luz da Navi é branca; o tom puxa para a cor da aura e da mira (amarelo no inimigo...). Na fonte, então o
        // cel, a projeção e a luz vanilla leem a mesma cor. Saturação 0 deixa igual ao vanilla. O host despacha a
        // Navi logo depois de EnElf_UpdateLights, com o branco recém-gravado.
        const float saturation = std::clamp(cfg.naviSaturation, 0.0f, 1.0f);
        if (saturation > 0.0f) {
            for (int i = 0; i < 3; ++i) {
                const float outer = std::clamp(payload.outer_color[i], 0.0f, 255.0f);
                const auto tinted = static_cast<uint8_t>(255.0f + ((outer - 255.0f) * saturation));
                payload.no_glow.color[i] = tinted;
                payload.glow.color[i] = tinted;
            }
            ++mod.stats.naviTinted;
        }
        return;
    }

    // Fadas soltas (floresta Kokiri e as de cura pelo mundo) deixam o raio em 0 no vanilla. Ligado, a luz sem brilho
    // ganha raio útil na posição da fada a cada frame, e o cel e a projeção a pegam como a da Navi; a grande
    // ilumina mais. Desligado, as que este mod acendeu voltam ao raio 0.
    const bool wild = payload.params == LINKSPAN_OOT_FAIRY_KOKIRI || payload.params == LINKSPAN_OOT_FAIRY_HEAL ||
                      payload.params == LINKSPAN_OOT_FAIRY_HEAL_BIG || payload.params == LINKSPAN_OOT_FAIRY_HEAL_TIMED;
    if (!wild) return;
    const auto* actor = static_cast<const Actor*>(payload.actor);
    if (cfg.otherFairyLights) {
        payload.no_glow.position[0] = actor->world.pos.x;
        payload.no_glow.position[1] = actor->world.pos.y;
        payload.no_glow.position[2] = actor->world.pos.z;
        payload.no_glow.radius = (payload.fairy_flags & LINKSPAN_OOT_FAIRY_FLAG_BIG) ? 150 : 100;
        payload.no_glow.color[0] = payload.no_glow.color[1] = payload.no_glow.color[2] = 255;
        mod.litFairies.insert(payload.actor);
        ++mod.stats.wildFairyLit;
    } else if (mod.litFairies.erase(payload.actor) != 0) {
        payload.no_glow.radius = 0;
    }
}

} // namespace WWStyle
