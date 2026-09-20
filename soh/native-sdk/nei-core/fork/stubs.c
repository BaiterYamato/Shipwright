// Stubs à mão do fork NEI: nomes que o código da DLL chama e que o fork só declara no ponto de uso, sem header
// que gen_stubs.py ache. Os demais stubs saem de stub-names.txt.

// O Harpoon (multiplayer do fork) não existe no host: o aviso aos outros jogadores não tem para onde ir.
void HarpoonCombat_BroadcastShieldParry_C(int shieldType, int effect) {
    (void)shieldType;
    (void)effect;
}

void HarpoonCombat_BroadcastShieldRevive_C(int restoredHealth) {
    (void)restoredHealth;
}

// O fork registra o id do EnPartner em runtime; no host ele é fixo.
s16 gEnPartnerId = ACTOR_EN_PARTNER;

// ── Kaleido do NEI-003 ───────────────────────────────────────────────────────
// Estes três são declarados só em header do host (soh/src e soh/soh), fora do que o gen_stubs.py
// varre, por isso ficam aqui e não em stub-names.txt.

// Move o cursor do menu para as setas de página. O LTCG inlinou a do host e ela sumiu do
// soh.symbols. Sem ela, as setas de página não capturam o cursor; a troca de página do NEI é pelo
// botão livre (ExtInv_SwitchPage), que não passa por aqui.
void KaleidoScope_MoveCursorToSpecialPos(PlayState* play, u16 specialPos) {
    (void)play;
    (void)specialPos;
}

// Ciclagem do item da célula do Nayru's Love (RocsFeatherCycle.c do host, também inlinada).
// Devolver ITEM_NONE deixa a célula sem alternativa, que é o estado de quem não tem o ciclo.
u8 Enhancement_GetNextNayrusItem(void) {
    return ITEM_NONE;
}

u8 Enhancement_GetPrevNayrusItem(void) {
    return ITEM_NONE;
}

// Multiplayer do fork: pedir drop pelo menu não tem para onde ir.
void HarpoonDrops_RequestDropFromPause(s16 item, s16 slot) {
    (void)item;
    (void)slot;
}
