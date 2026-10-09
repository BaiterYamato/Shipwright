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

// Sem FleetShipCombo, o jogo não participa de uma sessão OoT/MM. O stub gerado
// retornava 0 (OoT ativo no combo), então FleetWarp_Tick apagava toda transição
// vanilla e deixava Link correndo sem sair das portas.
int FleetShipCombo_GetActiveGame(void) {
    return -1;
}

// Variáveis do fork lidas pelas funções do host que a DLL sobrepõe (NEI-HOST-001), definidas em C++ fora dela.
// Os atores da SW97 são registrados no ActorDB pelo sw97_init.cpp; sem ele o id fica -1, como no fork antes do
// registro — nunca 0, que é o id do Player.
s16 gSw97ActorId_ArrowFire = -1;
s16 gSw97ActorId_ArrowIce = -1;
s16 gSw97ActorId_ArrowLight = -1;
s16 gSw97ActorId_ArrowDark = -1;
s16 gSw97ActorId_ArrowSoul = -1;
s16 gSw97ActorId_ArrowWind = -1;
// Formas do MM (mm_player_form.cpp, pikachu_form.cpp): sem forma ativa, matriz zerada, modos desligados e sem DL.
MtxF gGerudoRightHandMtx;
u8 gPikaGigantamaxMode = 0;
u8 gPikaThunderActive = 0;
Gfx* gZoraFinBoomerangLDL = NULL;
Gfx* gZoraFinBoomerangRDL = NULL;

// kafei_landmine.cpp: sem a forma Kafei, a bombchu nunca vira mina (o fork devolve 0 fora da forma). À mão porque o
// EnBomChu vem do header do ator, que o gen_stubs.py não inclui.
struct EnBomChu;
u8 KafeiLandmine_Settle(struct EnBomChu* chu, PlayState* play) {
    (void)chu;
    (void)play;
    return 0;
}

// Sem declaração que o gen_stubs.py ache (o fork declara com extern dentro das funções, ou o header é C++).
// Formas do MM (mm_player_form.cpp): sem forma, velocidade 1, força sem override (-1) e sem nado de Zora.
void MmForm_ApplyBootData(void) {
}

f32 MmForm_GetSpeedMultiplier(void) {
    return 1.0f;
}

s32 MmForm_GetStrengthOverride(void) {
    return -1;
}

// Sem forma, o instrumento é o da forma humana, como no fork. O 0 do stub gerado é OCARINA_INSTRUMENT_OFF: o replay
// da música não termina e a mensagem trava em MSGMODE_DISPLAY_SONG_PLAYED.
u8 MmForm_GetOcarinaPlaybackInstrument(void) {
    return 1; // OCARINA_INSTRUMENT_DEFAULT
}

u8 MmForm_GetSongFanfareInstrument(void) {
    return 0x35; // MMFORM_FANFARE_INSTRUMENT_DEFAULT do fork (Humano / Fierce Deity)
}

void MmForm_HandleFormInteractions(Player* player, PlayState* play) {
    (void)player;
    (void)play;
}

u8 MmForm_IsZoraSwimming(Player* player) {
    (void)player;
    return 0;
}

// HUDs em ImGui (CaneWheelHud.cpp, Sm64CapsHud.cpp) ficam fora da DLL.
void CaneWheelHud_DrawImGui(void) {
}

void Sm64CapsHud_DrawImGui(void) {
}

// Randomizer (ShuffleFairies.cpp): sem fada embaralhada na pedra, o Gossip Stone segue o vanilla.
struct EnGs;
s32 ShuffleFairies_SpawnStoneFairyOnTalk(struct EnGs* gossipStone) {
    (void)gossipStone;
    return 0;
}

// Expansão SM64 (sm64_mario.c, libsm64): sem Mario, nada a misturar, desenhar ou curar.
void Sm64Audio_MixInto(int16_t* outBuf, uint32_t numSamples) {
    (void)outBuf;
    (void)numSamples;
}

u8 Sm64Kaleido_DrawForm(PlayState* play) {
    (void)play;
    return 0;
}

void Sm64Mario_InitAttackCollider(PlayState* play, Player* player) {
    (void)play;
    (void)player;
}

u8 Sm64Mario_IsReady(void) {
    return 0;
}

u8 Sm64Mario_IsVanishActive(void) {
    return 0;
}

void Sm64Mario_OnPlayerInit(PlayState* play, Player* player) {
    (void)play;
    (void)player;
}

void Sm64Mario_QueueOotHeal(s16 healthChangeQuarters) {
    (void)healthChangeQuarters;
}

void Sm64Surfaces_RefreshActorColliders(PlayState* play) {
    (void)play;
}

// Garrafas MM (mm_bottles_behavior.cpp, C++ fora da DLL): nenhum item é conteúdo MM. O stub gerado devolvia 0,
// que é um conteúdo válido, e o Player_UseItem(ITEM_NONE) do Player_Init caía no "não pode usar aqui" e abria o
// texto 0x6F0A, que só existe com o hook de texto do fork.
int MmBottle_FromItemId(unsigned short ootItemId) {
    (void)ootItemId;
    return -1; // MM_BOTTLE_NONE
}

int MmBottle_GetUseBehavior(int content) {
    (void)content;
    return 2; // MM_BOTTLE_USE_NATIVE
}

// Valores neutros: o stub gerado devolve 0, que nestes nomes é um valor de verdade. Forma 0 é a Fierce Deity,
// multiplicador 0 para o Link, e as funções que repassam o argumento devolvem o próprio argumento sem o recurso.
typedef struct Player Player;
typedef struct PlayState PlayState;

int MmForm_GetCurrentForm(void) {
    return 4; // MM_PLAYER_FORM_HUMAN
}

int MmPlayer_GetForm(void) {
    return 4; // MM_PLAYER_FORM_HUMAN
}

s32 MmMaskWear_GetCurrent(void) {
    return 0xFF; // ITEM_NONE
}

s32 TransformMasks_WearGetCurrent(void) {
    return 0xFF; // ITEM_NONE
}

s32 RitoItem_OtherItem(void) {
    return 0xFF; // ITEM_NONE
}

f32 BossRemains_RunSpeedMul(void) {
    return 1.0f;
}

f32 GerudoMhr_RunSpeedMul(void) {
    return 1.0f;
}

f32 GerudoMhr_RunAnimRateMul(void) {
    return 1.0f;
}

f32 GerudoMhr_HopSpeedMul(void) {
    return 1.0f;
}

f32 GerudoMhr_ChargeRateMul(Player* player) {
    (void)player;
    return 1.0f;
}

f32 KafeiForm_RunSpeedMul(void) {
    return 1.0f;
}

f32 KafeiForm_RunAnimRateMul(void) {
    return 1.0f;
}

f32 MmForm_GetIncomingDamageMult(void) {
    return 1.0f;
}

s32 GerudoMhr_DamageTier(Player* player, s32 tier) {
    (void)player;
    return tier;
}

s32 GerudoMhr_NextComboMwa(Player* player, s32 requested) {
    (void)player;
    return requested;
}

// Sem a forma do Pikachu nem o mm.o2r, estas devolvem o próprio argumento: o stub gerado devolvia NULL, e o
// Interface_Draw do fork passa todo ícone de botão pelo PikaMode_ButtonIcon (os botões ficavam vazios); o
// MmDL_Or escolhe entre o DL vanilla do gancho e o do MM.
void* PikaMode_ButtonIcon(s32 button, void* orig) {
    (void)button;
    return orig;
}

Gfx* MmDL_Or(Gfx* vanillaDL, Gfx* mmDL) {
    return (mmDL != NULL) ? mmDL : vanillaDL;
}
