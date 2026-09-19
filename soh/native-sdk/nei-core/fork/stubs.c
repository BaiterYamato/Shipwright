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
