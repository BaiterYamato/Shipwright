#pragma once

// IDs consumidos pelo seletor nos testes do DMR: arco, hookshot, ocarina e item estendido.
// O slot 0 é preservado para o sentinela do public-probe; ITEM_NONE nunca é indexado.
inline void FillDmrPublicIcons(const void** icons) {
    icons[3] = "__OTR__textures/f5/bow";
    icons[10] = "__OTR__textures/f5/hookshot";
    icons[7] = "__OTR__textures/f5/ocarina";
    icons[0xA2] = "__OTR__textures/f5/extended";
}
