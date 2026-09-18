#include "MmGoronForm.h"

#include <cmath>
#include <cstdint>
#include <cstring>

// Diagnostico do impacto em parede (ver o bloco de impacto em ActionGoronRoll).
#include <spdlog/spdlog.h>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
}

// ---------------------------------------------------------------------------
// Estado e dispatcher da forma Goron.
//
// Origem: skijer/Shipwright @ Not-Enough-Items,
// soh/mods/transformation_masks/mm_player_form.cpp (`gFormState` em :690-1110,
// `MmForm_UpdateActive` em :10999). Crédito em link-span/CREDITS.md.
//
// Este arquivo traz o estado, a cessao ao OoT, as transicoes de locomocao e de
// ar e a BOLA inteira (`MmForm_Action_GoronRoll`, referencia :6839), alem do
// soco, da defesa e do instrumento. O host conserva apenas assets, desenho e
// operacoes de engine que precisam do SkelAnime concreto.
//
// Na locomocao a maquina entra no host como FONTE DE FALLBACK da animacao: ela
// nao troca a pose sozinha nem toma o override de one-shot, entao o hook
// `body_anim_select` dos mods (escada, porta, bau) e as acoes que ja funcionam
// seguem intactos. A bola e a primeira excecao: ela dirige o MOVIMENTO, e por
// isso exige que o chamador pause a action func do Player enquanto durar
// (`WantsPlayerActionPaused()`).
//
// SUBSTITUICOES DE INFRAESTRUTURA nesta arvore, todas registradas onde ocorrem:
//
//   - `sFormProps[form].cylinderRadius` -> raio salvo/restaurado. Nao temos a
//     tabela de propriedades por forma da referencia.
//   - `MmSfx_*` (loops continuos com Stop) -> so temos disparo unico, pelo
//     `Player_PlaySfx` do OoT e pelo `playMmSample`. Ver `StopRollSfx()`.
//   - `MM_NA_SE_*` -> o segundo argumento de `MmForm_PlaySfx(player, mmId,
//     ootId)` da referencia JA declara o equivalente do OoT de cada som; sao
//     esses ids que usamos, nao uma escolha nova.
//   - `bgCheckFlags |= 0x800` -> nao portado: esse bit e do MM, o OoT nao
//     define nada acima de (1 << 9) em `bgCheckFlags`.
// ---------------------------------------------------------------------------

namespace ShipLua::MmForm {
namespace {

struct FormState {
    Services services{};
    bool initialized = false;
    GoronAction action = GoronAction::Idle;
    // Frames dentro da acao corrente. A referencia usa `actionTimer` para o
    // mesmo fim; e incrementado ANTES do dispatch, entao a acao ve 1 no
    // primeiro frame — por isso a entrada de cada acao configura a animacao no
    // momento da transicao, e nunca depende de `actionTimer == 0`.
    std::uint32_t actionTimer = 0;
    std::uint8_t comboStep = 0;
    std::uint8_t gakkiPhase = 0;
    std::uint8_t gakkiLastNote = 0xFF;
    std::int16_t shieldLockedYaw = 0;
    // --- bola (incremento 3) ---
    // Estado do cylinder do Player antes de a bola o transformar em arma. O
    // collider pertence ao Player e e reusado pelo engine: sair do rolamento
    // sem restaurar deixa raio e dano de bola grudados em tudo depois.
    bool rollAttackArmed = false;
    f32 savedCylinderRadius = 0;
    // Campos da bola, com o nome do campo original do Player de MM ao lado —
    // e por eles que a referencia se le (mm_player_form.cpp:984-999).
    float rollBallSpeed = 0.0f;  // unk_B08: velocidade real da esfera (max 18)
    float rollBounce = 0.0f;     // unk_B0C: energia de quique acumulada
    float rollTilt = 0.0f;       // unk_B48: velocidade da deformacao visual
    float rollSquash = 0.0f;     // unk_ABC: achatamento/alongamento da esfera
    float rollColorLerp = 0.0f;  // unk_B10[0]: 0 branco, 1 azul (ground pound)
    std::int16_t rollHomeYaw = 0;      // actor.home.rot.y: direcao REAL do movimento
    std::int16_t rollChargeLevel = 0;  // av1: contador de carga (4 -> 0x36 -> espinhos)
    std::int16_t rollSpinRate = 0;     // av2: giro visual da esfera
    std::int16_t rollSpikeActive = 0;  // unk_B86[1]: 0 desligado, 1-7 ativo
    std::int16_t rollSfxCounter = 0;   // unk_B86[0]: contador do som de rolar
    std::int16_t rollDriftYaw = 0;     // unk_B28: direcao da derrapagem
    std::int16_t magicDrainTimer = 0;  // magicConsumptionTimer: 1 de magia a cada 10 frames
    std::uint8_t rollWallBounceTimer = 0;  // unk_B8C
    std::uint8_t rollNoInputTimer = 0;     // unk_B8E
    std::uint8_t rollGroundPoundTimer = 0; // unk_B8A
    // spC8 do MM: a taxa de curva do frame. O MM reusa o MESMO valor para o
    // passo do tombamento lateral (z_player.c:20124), entao ele precisa
    // sobreviver do bloco da fisica ate o bloco do tilt.
    std::int16_t rollTurnRate = 300;
    // spBC do MM: a velocidade que sobra na trajetoria depois do quique e do
    // desalinhamento. E dela que sai o piso do giro (`spBC * 500`), entao ela
    // tambem precisa atravessar do bloco da fisica ate a cauda comum.
    float rollForwardSpeed = 0.0f;
    // spDC do MM: o alvo de giro do frame. Calculado antes do ramo chao/ar
    // (z_player.c:19984) e possivelmente sobrescrito pela anti-reversao, por isso
    // vive no estado em vez de ser recalculado na cauda.
    std::int32_t rollSpinTarget = 0;
    // A anti-reversao esta freando a bola neste frame? Enquanto estiver, o MM
    // pula aceleracao, curva e tombamento.
    bool rollBraking = false;
    // Trava do log de impacto: uma linha por TOQUE em parede, nao por frame.
    bool rollWallLogged = false;
    float savedShadowScale = 0.0f;
    // Quem ligou PLAYER_STATE1_INPUT_DISABLED. A bola liga o flag para o OoT
    // parar de iniciar acoes proprias, mas outros caminhos do host (o void da
    // agua, por exemplo) tambem o usam — sem marcar a posse, a saida da bola
    // devolveria input que nao era dela para tirar.
    bool rollOwnsInputDisable = false;
    // A bola so entra quando o host declara que a assumiu. Ver SetBallEnabled
    // no header: `Update()` roda todo frame desde o incremento 2, e sem esta
    // chave o A ja tiraria o corpo do caminho antigo sem ninguem pausar a
    // action func do Player.
    bool ballEnabled = false;
    // Gate externo de ENTRADA por frame — agua/void, transicao de mascara ou
    // fallback legado no host. Nao interrompe uma acao em curso.
    bool entryBlocked = false;
    // O host ja entrou no roll de esquiva do OoT neste frame. A maquina
    // converte esse estado em curl antes de `PlayerOwnsBody` ceder a pose.
    bool ootRollActive = false;
    // --- socos (incremento 5) ---
    bool punchEnabled = false;
    // --- defesa (incremento 6) ---
    // O asset continua no host. Esta chave apenas declara que ele existe e que
    // a acao Shield pode ser materializada sem deixar o Player invisivel.
    bool shieldEnabled = false;
    // `comboBPressed` da referencia: um B durante o golpe corrente AGENDA o
    // proximo. Segurar o B nao encadeia — so a borda de pressao conta.
    bool comboBPressed = false;
    bool punchHitActive = false;
    // Dano ja foi observado nesta janela de PLAYER_STATE1_DAMAGED. O OoT
    // aplica vida, invencibilidade, yaw e velocidades antes de este ator
    // rodar; a maquina usa apenas a borda para possuir pose/recuperacao uma
    // vez, sem consumir o hit novamente.
    bool damageLatched = false;
    // Knockback que chegou a sair do piso precisa passar por Fall/Land antes
    // de devolver a locomocao. A flag sobrevive ao fim da pose de dano.
    bool damageWasAirborne = false;
    // Nome da animacao que a maquina pediu por ultimo. E como ela consulta o
    // ultimo frame sem guardar ponteiro de animacao (que e do host).
    const char* currentAnimName = nullptr;
};

FormState gState;

// A referencia usa MMFORM_ON_GROUND(player) para a mesma checagem.
bool OnGround(const Player* player) {
    return (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) != 0;
}

// As cinco fases da bola. A referencia chama este conjunto de "blocker"
// (:11300 e :11379): ele nao cede a cutscene, nao passa pela transicao
// generica de ar/chao e nao volta para a locomocao — cada uma dessas coisas a
// propria bola resolve por dentro.
bool IsRollAction(GoronAction action) {
    switch (action) {
        case GoronAction::RollInit:
        case GoronAction::Roll:
        case GoronAction::RollJump:
        case GoronAction::RollPound:
        case GoronAction::RollUncurl:
            return true;
        default:
            return false;
    }
}

// Fases em que a esfera ja e o corpo (o esqueleto do curl ainda e visivel em
// RollInit/RollUncurl, mas o host ja desenha a bola nas tres do meio).
bool IsBallAction(GoronAction action) {
    return action == GoronAction::Roll || action == GoronAction::RollJump || action == GoronAction::RollPound;
}

// Estados do OoT em que a forma NAO pode tomar o corpo. A ocarina entra aqui de
// proposito: ela e uma acao do Player que precisa continuar rodando — pausa-la
// quebra a propria execucao do instrumento. O corpo continua desenhado; quem
// dirige a pose durante a ocarina e a acao Gakki, que nao pausa nada.
bool PlayerOwnsBody(const Player* player) {
    return (player->stateFlags1 &
            (PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_GETTING_ITEM |
             PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_CLIMBING_LEDGE | PLAYER_STATE1_HANGING_OFF_LEDGE |
             PLAYER_STATE1_DEAD | PLAYER_STATE1_INPUT_DISABLED | PLAYER_STATE1_TALKING)) != 0;
}

bool HasAnim(const char* name) {
    return gState.services.hasAnimation != nullptr && gState.services.hasAnimation(name);
}

bool StartAnim(const char* name, float speed, bool once, bool reverse = false) {
    if (gState.services.startAnimation == nullptr) {
        return false;
    }
    if (!gState.services.startAnimation(name, speed, once, reverse)) {
        return false;
    }
    // `name` e sempre um literal deste arquivo ou uma constante estatica — a
    // maquina nunca recebe nome de fora. Guardar o ponteiro e seguro por isso, e
    // e o que permite consultar o ultimo frame sem conhecer a animacao.
    gState.currentAnimName = name;
    return true;
}

float CurrentFrame() {
    return gState.services.currentFrame != nullptr ? gState.services.currentFrame() : 0.0f;
}

// Ultimo frame da animacao que a maquina pediu por ultimo. Zero quando nao ha
// animacao propria no ar — quem chama trata isso como "sem fim conhecido".
float CurrentAnimLastFrame() {
    if (gState.services.animationLastFrame == nullptr || gState.currentAnimName == nullptr) {
        return 0.0f;
    }
    return gState.services.animationLastFrame(gState.currentAnimName);
}

// ---------------------------------------------------------------------------
// Perfis de gravidade (referencia :110-118 e `MmForm_GetGravity` :1324). Os
// tres que a bola usa: o normal do ar, o do apice do ground pound (flutua) e o
// do baque (desce como pedra).
// ---------------------------------------------------------------------------
enum class GravityProfile { Normal, RollApex, RollSlam };

f32 GetGravity(GravityProfile profile) {
    switch (profile) {
        case GravityProfile::RollApex:
            return -0.2f;
        case GravityProfile::RollSlam:
            return -10.0f;
        case GravityProfile::Normal:
        default:
            return -1.2f;
    }
}

// `MmForm_GetStickMagnitude` (referencia :5621). O modulo cru do analogico,
// nao a magnitude ja normalizada do Player: a bola tem a propria curva.
f32 StickMagnitude(const PlayState* play) {
    const Input* input = &play->state.input[0];
    const f32 sx = static_cast<f32>(input->rel.stick_x);
    const f32 sy = static_cast<f32>(input->rel.stick_y);
    return sqrtf(sx * sx + sy * sy);
}

void PlaySfx(Player* player, u16 ootSfxId) {
    Player_PlaySfx(&player->actor, ootSfxId);
}

// `MmForm_StopGoronRollSfx` (referencia :5537) — aqui, deliberadamente vazia.
//
// A referencia precisa dela porque o motor de amostras dela sustenta loops
// continuos (rolar, carregar, derrapar) que o MM cala sozinho pelo motor de
// sequencia. O NOSSO caminho de audio so tem DISPARO UNICO
// (`Player_PlaySfx` e `Services::playMmSample`), entao nao existe loop pendurado
// para calar: o som de rolar sai por gatilho a cada cruzamento de zero, que e
// exatamente o que a referencia ja faz nos modos 1 e 2.
//
// A funcao fica no lugar, e nao apagada, porque marca os pontos de saida da
// bola onde a referencia cala o som — e o dia em que ganharmos loops continuos
// e so preenche-la.
void StopRollSfx() {
}

// Troca de acao. A animacao de LOOP nao e disparada aqui: quem a pede e
// `DesiredAnimation()`, para a maquina entrar como fonte de fallback do host em
// vez de tomar o override. `StartAnim` fica reservado aos one-shots das acoes
// que ainda vao entrar (pouso, dano, curl da bola).
void SetAction(GoronAction action) {
    gState.action = action;
    gState.actionTimer = 0;
}

// ---------------------------------------------------------------------------
// Collider de ataque da bola.
//
// Portado de mm_form_combat / MmForm_SetRollAttack e MmForm_ClearRollAttack
// (mm_player_form.cpp:6784 e :6810), que por sua vez vem de
// Player_SetCylinderForAttack do MM (z_player.c:2901-2927).
//
// A regra de raio da referencia e por faixa, nao por acao: acima de 30 o golpe
// e "grande" (o ground pound usa 60) e entao a sobreposicao com atores e
// desligada e o proprio Goron fica imune durante o baque; abaixo disso e o
// rolamento normal (25) e a sobreposicao continua ligada.
// ---------------------------------------------------------------------------
void SetRollAttack(Player* player, std::uint32_t dmgFlags, std::int32_t damage, std::int16_t radius) {
    if (!gState.rollAttackArmed) {
        gState.savedCylinderRadius = player->cylinder.dim.radius;
        gState.rollAttackArmed = true;
    }
    player->cylinder.base.atFlags = AT_ON | AT_TYPE_PLAYER;
    player->cylinder.base.ocFlags1 = radius > 30 ? OC1_NONE : (OC1_ON | OC1_TYPE_ALL);
    player->cylinder.info.elemType = ELEMTYPE_UNK2;
    player->cylinder.info.toucherFlags = TOUCH_ON | TOUCH_NEAREST | TOUCH_SFX_NORMAL;
    player->cylinder.info.toucher.dmgFlags = dmgFlags;
    player->cylinder.info.toucher.damage = static_cast<std::uint8_t>(damage);
    player->cylinder.dim.radius = radius;
    if (radius > 30) {
        player->cylinder.base.acFlags = AC_NONE;
    }
}

void ClearRollAttack(Player* player) {
    player->cylinder.base.atFlags = AT_NONE;
    player->cylinder.base.ocFlags1 = OC1_ON | OC1_TYPE_ALL;
    player->cylinder.base.acFlags = AC_ON | AC_TYPE_ENEMY;
    player->cylinder.info.toucherFlags = TOUCH_NONE;
    // Zerar dmgFlags tambem: sem isto o DMG_HAMMER_SWING da bola fica cacheado
    // no cylinder, e qualquer codigo que rearme atFlags depois passa a bater com
    // dano de martelo. A referencia registra o sintoma real disso — combo de
    // outra forma quebrando rocha que so responde a martelo.
    player->cylinder.info.toucher.dmgFlags = 0;
    if (gState.rollAttackArmed) {
        player->cylinder.dim.radius = gState.savedCylinderRadius;
        gState.rollAttackArmed = false;
    }
    gState.rollSpikeActive = 0;
    // A referencia chama o silenciador aqui porque ClearRollAttack e o ponto
    // por onde passam TODAS as saidas de dano da bola (congelamento, eletrico,
    // repuxo forte e fraco) — uma linha cobre as quatro.
    StopRollSfx();
}

// Arma o cylinder E o inscreve na checagem de colisao do frame.
//
// A referencia faz os dois em duas linhas em cada ponto de uso; juntamos porque
// aqui falta um terceiro passo. O ator hospedeiro atualiza DEPOIS do Player:
// quando esta funcao roda, o `Collider_UpdateCylinder` do frame
// (z_player.c:12163) ja passou com o raio e a posicao ANTIGOS, e sem refrescar
// o cilindro entraria na checagem um passo atrasado — visivelmente, um raio 60
// de ground pound centrado onde o Goron estava antes de cair. E o mesmo motivo
// pelo qual `CustomBodyUpdateRollHitbox` no host tambem chama
// `Collider_UpdateCylinder` antes do `CollisionCheck_SetAT`.
void ArmRollAttack(Player* player, PlayState* play, u32 dmgFlags, s32 damage, s16 radius) {
    SetRollAttack(player, dmgFlags, damage, radius);
    Collider_UpdateCylinder(&player->actor, &player->cylinder);
    CollisionCheck_SetAT(play, &play->colChkCtx, &player->cylinder.base);
}

// ---------------------------------------------------------------------------
// A BOLA
//
// Origem: `MmForm_Action_GoronRoll` (referencia :6839), que por sua vez porta o
// `Player_Action_96` do MM (z_player.c:19886). Tres subestados dentro de uma
// funcao so — rolamento, subida do ground pound e o baque — mais o curl que
// entra e o desenrolar que sai.
//
// E o PRIMEIRO caminho desta arvore em que a forma escreve velocidade, yaw e
// gravidade do Player. O rolamento que existe hoje no host e outra coisa: ele
// sequestra o `Player_Action_Roll` do OoT, que e uma esquiva, e o reencadeia
// para fingir uma bola. Esta funcao substitui aquilo por fisica propria.
// ---------------------------------------------------------------------------

// O curl toca `roll_enter` (pg_maru_change) a 0.67 e dura 7 frames na
// referencia. Se o chamador nao alimentar `animationFinished`, este limite
// impede que a bola fique presa no curl para sempre — degrada a cadencia, nao
// trava o jogo.
constexpr std::uint32_t kRollInitTimeoutFrames = 24;

// Entrada na esfera, do bloco `GORON_ACT_ROLL_INIT` do dispatcher da referencia
// (:12255-12294, de func_80857A44 do MM).
void EnterBall(Player* player, PlayState* play) {
    gState.rollSpinRate = static_cast<s16>(player->linearVelocity * 500.0f);
    gState.rollBallSpeed = player->linearVelocity;
    gState.rollBounce = 0.0f;
    gState.rollTilt = 0.0f;
    gState.rollSquash = 0.0f;
    gState.rollColorLerp = 0.0f;
    // `this->actor.home.rot.y = this->yaw` (func_80857A44, z_player.c:19870).
    // A trajetoria nasce do yaw de MOVIMENTO, nao do yaw visual: com os dois
    // dessincronizados na entrada, o primeiro frame de bola ja saia torto.
    gState.rollHomeYaw = player->yaw;
    gState.rollChargeLevel = 4; // a referencia comeca em 4, nao em 0
    gState.rollSpikeActive = 0;
    gState.rollSfxCounter = 0;
    gState.rollWallBounceTimer = 0;
    gState.rollNoInputTimer = 0;
    gState.rollGroundPoundTimer = 0;
    gState.rollDriftYaw = 0;
    gState.magicDrainTimer = 0;

    SetAction(GoronAction::Roll);

    // MM_NA_SE_PL_GORON_TO_BALL; o equivalente do OoT que a propria referencia
    // declara e o baque de corpo.
    PlaySfx(player, NA_SE_PL_BODY_HIT);

    // Sombra menor durante a esfera. A referencia reafirma o valor todo frame
    // porque o UpdateActive dela troca a funcao de sombra junto; aqui uma vez
    // basta, e `z_player.c` nao escreve `shape.shadowScale` em nenhum ponto do
    // update do Player (conferido: zero ocorrencias no arquivo).
    gState.savedShadowScale = player->actor.shape.shadowScale;
    player->actor.shape.shadowScale = 30.0f;

    player->stateFlags2 |= (PLAYER_STATE2_DISABLE_ROTATION_Z_TARGET | PLAYER_STATE2_DISABLE_ROTATION_ALWAYS);
    // INPUT_DISABLED zera a copia de input do OoT e com isso impede que ele
    // inicie ataque, esquiva ou pulo por baixo da bola. Nos lemos
    // `play->state.input[0]` direto, entao continuamos enxergando os botoes.
    if ((player->stateFlags1 & PLAYER_STATE1_INPUT_DISABLED) == 0) {
        player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
        gState.rollOwnsInputDisable = true;
    }
}

// Saida da esfera (referencia :6847-6879, de func_80857950 do MM).
void ExitBall(Player* player, PlayState* play) {
    (void)play;
    ClearRollAttack(player);
    player->actor.gravity = GetGravity(GravityProfile::Normal);
    player->actor.shape.rot.x = 0;
    player->actor.shape.rot.z = 0;
    // Restaura a sombra so se houver o que restaurar: um 0.0f herdado de um
    // estado nunca inicializado apagaria a sombra do Goron em pe.
    if (gState.savedShadowScale > 0.0f) {
        player->actor.shape.shadowScale = gState.savedShadowScale;
    }
    player->stateFlags2 &= ~(PLAYER_STATE2_DISABLE_ROTATION_Z_TARGET | PLAYER_STATE2_DISABLE_ROTATION_ALWAYS);
    player->stateFlags1 &= ~PLAYER_STATE1_JUMPING;
    if (gState.rollOwnsInputDisable) {
        player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
        gState.rollOwnsInputDisable = false;
    }
    // Copia prevPos sobre world.pos: sem isto o corpo POPA visualmente no
    // instante em que a display list da esfera da lugar ao esqueleto.
    Math_Vec3f_Copy(&player->actor.world.pos, &player->actor.prevPos);
    StopRollSfx();
    // MM_NA_SE_PL_GORON_BALL_TO_GORON -> baque de corpo.
    PlaySfx(player, NA_SE_PL_BODY_HIT);

    // O desenrolar e a MESMA animacao do curl ao contrario, do ultimo frame
    // para o primeiro, a -0.67.
    if (StartAnim("roll_exit", 0.67f, true, /*reverse=*/true)) {
        SetAction(GoronAction::RollUncurl);
    } else {
        SetAction(GoronAction::Idle);
        player->linearVelocity = 0.0f;
    }
}

// Um frame de bola. Cobre Roll, RollJump e RollPound — os tres subestados que a
// referencia mantem numa unica funcao, porque compartilham o giro, a deformacao
// e o collider do fim.
void ActionGoronRoll(Player* player, PlayState* play) {
    Input* input = &play->state.input[0];
    const bool onGround = OnGround(player);

    // -----------------------------------------------------------------------
    // SAIDA, antes de qualquer fisica (referencia :6847). Sem espinhos e com o
    // A solto, desenrola. A ordem importa: a referencia checa isto primeiro.
    // -----------------------------------------------------------------------
    // `func_80857950` (2S2H z_player.c:19845) tem DUAS condicoes de saida, nao
    // uma:
    //
    //   1. sem espinhos e A solto                      -> desenrola normal
    //   2. `av1.actionVar1 == 3` e `velocity.y < 0.0f` -> desenrola NO AR
    //
    // A segunda e o knockback de parede que faltava, e so agora fez sentido: o
    // carregamento 3 e marcado exatamente no impacto FRONTAL com espinhos
    // (:19930). Ali `func_80834CD0` joga a bola para cima; quando ela comeca a
    // cair, o Goron SAI da bola sozinho, no ar.
    //
    // Sem isso, bater de frente com espinhos continuava rolando como se nada
    // tivesse acontecido — que e a diferenca de knockback relatada tres vezes.
    if (gState.action == GoronAction::Roll &&
        ((gState.rollSpikeActive == 0 && !CHECK_BTN_ALL(input->cur.button, BTN_A)) ||
         (gState.rollChargeLevel == 3 && player->actor.velocity.y < 0.0f))) {
        ExitBall(player, play);
        return;
    }

    // Os dois timers sao decrementados JUNTO do efeito que causam, logo abaixo —
    // e assim que o MM faz (z_player.c:19949-19959). Decrementar aqui em cima,
    // solto, foi o que me levou a tratar os dois como se fizessem a mesma coisa.

    // -----------------------------------------------------------------------
    // Alvo de yaw e de velocidade (referencia :6892-6907).
    //
    // O angulo TEM de sair de `rel.stick` somado ao yaw da camera por
    // `Camera_GetInputDirYaw`: e assim que o OoT mapeia o analogico, e usar
    // qualquer outra fonte faz a bola andar torto em relacao a camera.
    // -----------------------------------------------------------------------
    // Os dois timers do quique fazem coisas DIFERENTES. Fonte primaria: 2S2H
    // `mm/src/overlays/actors/ovl_player_actor/z_player.c:19949-19959`, que e o
    // proprio Player_Action_96 do MM.
    //
    //   unk_B8E (rollNoInputTimer)   -> nao le o analogico de jeito nenhum:
    //                                   nem velocidade, nem direcao.
    //   unk_B8C (rollWallBounceTimer)-> trava SO a direcao (`yawTarget = yaw`).
    //                                   A velocidade continua vindo do analogico.
    //
    // E por isso que no MM o quique nao freia a bola: ela sai da parede ainda
    // rapida, so nao consegue VIRAR por quatro frames. A versao anterior desta
    // funcao bloqueava o gate inteiro nos dois casos, o que zerava o
    // `speedTarget` e travava a bola no impacto — parecido, mas nao e o MM.
    //
    // A referencia skijer nao ajudava aqui: ela arma `unk_B8C` e nunca o le, e
    // le `unk_B8E` sem nunca arma-lo. As duas metades estavam soltas.
    f32 speedTarget = 0.0f;
    s16 yawTarget = player->yaw;
    if (gState.rollNoInputTimer != 0) {
        --gState.rollNoInputTimer;
    } else {
        const f32 stickMag = StickMagnitude(play);
        if (stickMag > 10.0f) {
            const s16 stickAngle =
                Math_Atan2S(static_cast<f32>(input->rel.stick_y), static_cast<f32>(-input->rel.stick_x));
            const s16 camYaw = Camera_GetInputDirYaw(GET_ACTIVE_CAM(play));
            yawTarget = static_cast<s16>(stickAngle + camYaw);
            speedTarget = (stickMag / 60.0f) * 8.0f * 2.6f;
        }
    }
    if (gState.rollWallBounceTimer != 0) {
        --gState.rollWallBounceTimer;
        yawTarget = player->yaw;
    }

    // -----------------------------------------------------------------------
    // GROUND POUND — subida (referencia :6912-6993).
    // -----------------------------------------------------------------------
    if (gState.action == GoronAction::RollJump) {
        if (player->actor.velocity.y > 0.0f) {
            // Subindo: segura o apice para ele nao virar um pulo seco.
            if ((player->actor.velocity.y + player->actor.gravity) < 0.0f) {
                player->actor.velocity.y = -player->actor.gravity;
            }
        } else {
            gState.rollGroundPoundTimer = 10;
            player->actor.gravity = player->actor.velocity.y > -1.0f ? GetGravity(GravityProfile::RollApex)
                                                                     : GetGravity(GravityProfile::RollSlam);
        }

        if (onGround && player->actor.velocity.y <= 0.0f) {
            // BAQUE.
            SetAction(GoronAction::RollPound);
            player->actor.gravity = GetGravity(GravityProfile::Normal);
            player->stateFlags1 &= ~PLAYER_STATE1_JUMPING;

            // O sinal que os atores leem para saber que houve um impacto do
            // jogador. O proprio Player do OoT escreve o mesmo 4 aqui
            // (z_player.c:9079) — nao e um valor inventado para o MM.
            play->actorCtx.unk_02 = 4;

            const s32 quakeIdx = Quake_Add(GET_ACTIVE_CAM(play), 3);
            if (quakeIdx != 0) {
                Quake_SetSpeed(static_cast<s16>(quakeIdx), 27767);
                Quake_SetQuakeValues(static_cast<s16>(quakeIdx), 7, 0, 0, 0);
                Quake_SetCountdown(static_cast<s16>(quakeIdx), 20);
            }

            // MM_NA_SE_PL_GORON_PUNCH -> baque de corpo.
            PlaySfx(player, NA_SE_PL_BODY_HIT);
            Rumble_Request(0.0f, 255, 20, 150);

            Vec3f shockPos = player->actor.world.pos;
            Vec3f zeroVec = { 0.0f, 0.0f, 0.0f };
            EffectSsBlast_SpawnWhiteShockwave(play, &shockPos, &zeroVec, &zeroVec);
            Actor_SpawnFloorDustRing(play, &player->actor, &player->actor.world.pos,
                                     player->actor.shape.shadowScale * 1.5f, 4, 8.0f, 500, 10, 1);

            gState.rollBallSpeed = 0.0f;
            gState.rollSpinRate = 0;

            // MM usa DMG_GORON_POUND, um bit proprio dele. O equivalente
            // semantico no OoT para um esmagamento vindo de um salto e
            // DMG_HAMMER_JUMP — martelo baixado de um pulo.
            ArmRollAttack(player, play, DMG_HAMMER_JUMP, 4, 60);
        }

        player->actor.shape.rot.x += gState.rollSpinRate;
        Math_ScaledStepToS(&player->actor.shape.rot.y, gState.rollHomeYaw, 0x7D0);
        return;
    }

    // -----------------------------------------------------------------------
    // GROUND POUND — pausa apos o baque (referencia :6995-7023).
    // -----------------------------------------------------------------------
    if (gState.action == GoronAction::RollPound) {
        if (gState.rollGroundPoundTimer > 0) {
            --gState.rollGroundPoundTimer;
            player->linearVelocity = 0.0f;
            // Na referencia ha aqui um `if (actionTimer == 0)` que rearmaria o
            // golpe no primeiro frame. Ele e INALCANCAVEL: o dispatcher
            // incrementa `actionTimer` ANTES de despachar (referencia :11589 e
            // o comentario em :3450), entao a acao nunca ve 0. O nosso
            // dispatcher tem a mesma ordem, e o resultado e o mesmo — a janela
            // de dano do baque e o unico frame armado la em cima, na aterrissagem.
            ClearRollAttack(player);
        } else {
            ClearRollAttack(player);
            gState.rollChargeLevel = 4;
            SetAction(GoronAction::Roll);
            // Sem restaurar a gravidade, a proxima vez que a bola sair do chao
            // ela despencaria com o -10 do baque ainda no lugar.
            player->actor.gravity = GetGravity(GravityProfile::Normal);
        }
        player->actor.shape.rot.x += gState.rollSpinRate;
        return;
    }

    // -----------------------------------------------------------------------
    // ROLAMENTO PRINCIPAL (referencia :7025+).
    // -----------------------------------------------------------------------

    // -----------------------------------------------------------------------
    // IMPACTO EM PAREDE — dois casos EXCLUSIVOS, nao um so.
    //
    // Fonte primaria: 2S2H z_player.c:19921-19945.
    //
    //   if (func_80840A30(...))            -> batida DE FRENTE: a bola FREIA
    //   else if (WALL && unk_B08 >= 12.0f) -> batida DE RASPAO: a bola REFLETE
    //
    // `func_80840A30` (:10859) exige velocidade >= 12 e
    // `sWorldYawToTouchedWall < 0x1C00` — o angulo entre o rumo do Goron e a
    // normal da parede, abaixo de ~39 graus. Esse angulo e calculado em :11943
    // como |yaw - (wallYaw + 0x8000)|, que e exatamente o `relWallAngle` daqui.
    //
    // O QUE FALTAVA: o port so freava quando havia PORTA, e refletia em todo o
    // resto. Bater de frente numa parede qualquer ricocheteava a bola em vez de
    // para-la — e como os dois ramos eram `if` soltos em vez de `if/else`, uma
    // batida frontal numa porta chegava a frear E quicar no mesmo frame.
    //
    // O freio nao e so perda de velocidade: `func_80834CD0(this, 10.0f, 0)`
    // (:6306) joga a bola PARA CIMA e a tira do chao. E esse pulinho para tras
    // que faz a batida frontal parecer um baque de verdade.
    {
        const s16 wallAngleFromNormal = static_cast<s16>(player->actor.wallYaw + 0x8000);
        const s16 relWallAngle = static_cast<s16>(player->yaw - wallAngleFromNormal);
        const bool fastEnough = gState.rollBallSpeed >= 12.0f;

        // DIAGNOSTICO DO IMPACTO.
        //
        // A versao anterior travava no PRIMEIRO toque e nunca mais falava. No
        // teste de 01/08 isso escondeu o que importava: o Link ja estava
        // encostado na parede ao entrar na bola, o log gravou `ballSpeed=0.60`,
        // e todos os impactos em velocidade alta — os unicos que podem quicar —
        // ficaram sem registro. Cheguei a concluir que a bola nao acelerava.
        //
        // Agora: registra todo toque com velocidade >= 12 (a faixa em que o
        // quique existe), e continua com uma linha unica por toque LENTO, para
        // nao encher o log rolando encostado numa parede.
        {
            const bool touching = (player->actor.bgCheckFlags & BGCHECKFLAG_WALL) != 0;
            if (!touching) {
                gState.rollWallLogged = false;
            } else if (fastEnough || !gState.rollWallLogged) {
                if (!fastEnough) {
                    gState.rollWallLogged = true;
                }
                const bool interact = (player->actor.bgCheckFlags & BGCHECKFLAG_PLAYER_WALL_INTERACT) != 0;
                const bool headOnNow = interact && ABS(relWallAngle) < 0x1C00;
                SPDLOG_INFO("ShipLua bola/parede: ballSpeed={:.2f} linear={:.2f} vy={:.2f} ang={} graus "
                            "interact={} spike={} carga={} door={} wallBgId={} -> ramo={}",
                            gState.rollBallSpeed, player->linearVelocity, player->actor.velocity.y,
                            static_cast<int>(ABS(relWallAngle) * 360.0f / 65536.0f), interact,
                            static_cast<int>(gState.rollSpikeActive), static_cast<int>(gState.rollChargeLevel),
                            static_cast<int>(player->doorType), static_cast<int>(player->actor.wallBgId),
                            (!fastEnough && player->doorType == PLAYER_DOORTYPE_NONE) ? "NENHUM(lento)"
                            : headOnNow                                              ? "FRONTAL(freia)"
                                                                                     : "RASPAO(reflete)");
            }
        }
        // A porta de escada usa limiar 0.0f no MM — qualquer velocidade serve.
        const bool doorOverride = player->doorType != PLAYER_DOORTYPE_NONE;
        const bool headOn = (player->actor.bgCheckFlags & BGCHECKFLAG_PLAYER_WALL_INTERACT) != 0 &&
                            ABS(relWallAngle) < 0x1C00;

        if ((fastEnough || doorOverride) && headOn) {
            player->linearVelocity *= 0.1f;
            // func_80834CD0(this, 10.0f, NA_SE_NONE): sobe e sai do chao. Sem
            // som — o sfxId zero do MM significa "nenhum".
            player->actor.velocity.y = 10.0f;
            player->actor.bgCheckFlags &= ~BGCHECKFLAG_GROUND;
            player->stateFlags1 |= PLAYER_STATE1_JUMPING;
            // `fallStartHeight` e s16 no OoT e f32 no MM — dai o cast.
            player->fallStartHeight = static_cast<s16>(player->actor.world.pos.y);
            if (gState.rollSpikeActive > 0) {
                gState.rollSpikeActive = 0;
                // carga 3, nao 4: o MM deixa a batida frontal um degrau abaixo
                // do cancelamento normal de espinhos.
                gState.rollChargeLevel = 3;
                Magic_Reset(play);
            }
            PlaySfx(player, NA_SE_PL_BODY_HIT);
            SPDLOG_INFO("ShipLua bola/parede: EXECUTOU FRONTAL — velocidade cortada para {:.2f}, vy={:.2f}, "
                        "carga={}",
                        player->linearVelocity, player->actor.velocity.y,
                        static_cast<int>(gState.rollChargeLevel));
        } else if ((player->actor.bgCheckFlags & BGCHECKFLAG_WALL) && fastEnough) {
            // Reflexao de raspao. Sem excecao de dyna: o MM nao tem nenhuma —
            // aquilo era invencao da referencia skijer, e fazia a bola atravessar
            // sem quicar toda vez que a parede pertencia a um objeto que o
            // cylinder tinha acabado de acertar.
            const s16 bounceAngle =
                static_cast<s16>(((relWallAngle >= 0) ? 1 : -1) * ((ABS(relWallAngle) + 0x100) & ~0x1FF));

            gState.rollBounce += gState.rollBallSpeed * 0.05f;
            player->yaw += static_cast<s16>(0x8000 - (bounceAngle * 2));
            gState.rollHomeYaw = player->yaw;
            player->actor.shape.rot.y = player->yaw;

            gState.rollWallBounceTimer = 4;

            // MM_NA_SE_IT_GORON_ROLLING_REFLECTION -> baque de corpo.
            PlaySfx(player, NA_SE_PL_BODY_HIT);
            SPDLOG_INFO("ShipLua bola/parede: EXECUTOU RASPAO — yaw refletido para {:#06x}, bounce={:.2f}, "
                        "ballSpeed={:.2f}",
                        static_cast<std::uint16_t>(player->yaw), gState.rollBounce, gState.rollBallSpeed);
        }
    }

    // Espinhos ativos: velocidade travada em 18 e magia drenando (:7078-7123).
    if (gState.rollSpikeActive > 0) {
        speedTarget = 18.0f;
        Math_StepToS(&gState.rollChargeLevel, 4, 1);

        // Dreno continuo: 1 de magia a cada 10 frames, como o
        // MAGIC_STATE_CONSUME_GORON_ZORA do z_parameter.c do MM.
        --gState.magicDrainTimer;
        if (gState.magicDrainTimer <= 0) {
            if (gSaveContext.magic > 0) {
                gSaveContext.magic--;
            }
            gState.magicDrainTimer = 10;
        }

        bool deactivateSpike = false;
        if (!CHECK_BTN_ALL(input->cur.button, BTN_A)) {
            deactivateSpike = true;
        }
        if (gSaveContext.magic <= 0) {
            deactivateSpike = true;
        }
        if (gState.rollChargeLevel == 4 && gState.rollBallSpeed < 12.0f) {
            deactivateSpike = true;
        }
        // Ladeira ingreme derruba os espinhos. So checa NO CHAO: no ar o
        // `floorPitch` esta velho e desligaria os espinhos ao sair de uma
        // borda.
        if (onGround) {
            const s16 yawDiff = static_cast<s16>(player->yaw - gState.rollHomeYaw);
            if ((ABS(yawDiff) + ABS(player->floorPitch)) > 0x3A98) {
                deactivateSpike = true;
            }
        }

        if (deactivateSpike) {
            if (Math_StepToS(&gState.rollSpikeActive, 0, 1)) {
                Magic_Reset(play);
                // MM_NA_SE_PL_GORON_BALL_CHARGE_FAILED -> baque de corpo.
                PlaySfx(player, NA_SE_PL_BODY_HIT);
            }
            gState.rollChargeLevel = 4;
        } else if (gState.rollSpikeActive < 7) {
            gState.rollSpikeActive++;
        }
    }

    s16 steerLean = 0;

    // `spDC = speedTarget * 900.0f` (z_player.c:19984), calculado antes do ramo
    // chao/ar. No ar e este valor que vale; no chao ele ainda recebe o piso da
    // velocidade real e pode ser invertido pela anti-reversao.
    gState.rollSpinTarget = static_cast<s32>(speedTarget * 900.0f);

    if (onGround) {
        // Gravidade normal enquanto no chao: limpa qualquer resto do baque
        // (-10) ou do voo com espinhos (-1). So passa a valer quando a bola
        // deixar o solo, mas tem de estar certa ANTES disso.
        player->actor.gravity = GetGravity(GravityProfile::Normal);

        // Ground pound: B no chao, fora do modo espinho (referencia :7132-7159).
        if (gState.rollSpikeActive == 0 && CHECK_BTN_ALL(input->press.button, BTN_B)) {
            player->actor.velocity.y = 14.0f;
            player->linearVelocity = 0.0f;
            // 0x1F40 e o giro MINIMO do salto, nao um deslocamento de yaw.
            if (gState.rollSpinRate < 0x1F40) {
                gState.rollSpinRate = 0x1F40;
            }
            gState.rollChargeLevel = 1;
            gState.rollTilt = 1.0f;
            SetAction(GoronAction::RollJump);
            player->actor.gravity = GetGravity(GravityProfile::Normal);
            // MM_NA_SE_PL_GORON_BALLJUMP -> pulo do OoT.
            PlaySfx(player, NA_SE_PL_JUMP);
            // JUMPING faz a camera do OoT escolher CAM_MODE_JUMP, o mais
            // proximo do CAM_MODE_GORONJUMP do MM.
            player->stateFlags1 |= PLAYER_STATE1_JUMPING;
            player->actor.shape.rot.x += gState.rollSpinRate;
            return;
        }

        // Carga dos espinhos, em duas fases (referencia :7161-7239).
        if (gState.rollSpikeActive == 0) {
            gState.rollBounce = 0.0f;

            // Fase 2 PRIMEIRO, como na referencia: se a carga chegou, ativa.
            if (gState.rollChargeLevel >= 0x36) {
                if (gSaveContext.magic >= 2) {
                    gSaveContext.magic -= 2;
                }
                gState.magicDrainTimer = 10;
                gState.rollBallSpeed = 18.0f;
                gState.rollSpikeActive = 1;
                // MM_NA_SE_PL_GORON_BALL_CHARGE_DASH -> baque de corpo.
                PlaySfx(player, NA_SE_PL_BODY_HIT);
            }

            // Fase 1: a carga sobe sozinha com o giro alto. MM NAO exige o A
            // aqui — carregar e consequencia de rolar rapido.
            if (gState.rollSpikeActive == 0) {
                if (gSaveContext.magicState == MAGIC_STATE_IDLE && gSaveContext.magic >= 2 &&
                    gState.rollSpinRate >= 0x36B0) {
                    if (gState.rollChargeLevel < 0x100) {
                        gState.rollChargeLevel++;
                    }
                    // O zumbido continuo da carga (MM_NA_SE_PL_GORON_BALL_CHARGE)
                    // fica de fora: e um LOOP, e so temos disparo unico. Repetir
                    // um one-shot por frame empilharia o som.
                } else {
                    gState.rollChargeLevel = 4;
                }
            }
        } else {
            gState.rollBounce = CLAMP(gState.rollBounce, 0.0f, 0.9f);

            // Ladeira ingreme cancela os espinhos (referencia :7228-7238).
            const s16 yawDiff = static_cast<s16>(player->yaw - gState.rollHomeYaw);
            if ((ABS(yawDiff) + ABS(player->floorPitch)) > 0x3A98) {
                gState.rollSpikeActive = 0;
                gState.rollChargeLevel = 4;
                gState.rollSpinRate = 0;
                // `unk_B8E = 0x14` (z_player.c:20137). Era esta a lacuna que eu
                // tinha registrado como "sem evidencia de quantos frames o MM
                // zera apos desligar os espinhos": sao VINTE. Cancelar os
                // espinhos numa ladeira tira o controle por 20 frames, e e isso
                // que faz a perda de carga doer em vez de passar despercebida.
                gState.rollNoInputTimer = 20;
                Magic_Reset(play);
            }
        }

        // Tombamento lateral proporcional a curva pedida no analogico, e a
        // energia que essa curva injeta no quique.
        steerLean = static_cast<s16>(static_cast<s16>(yawTarget - player->yaw) * -0.5f);
        gState.rollBounce += static_cast<f32>(SQ(steerLean)) * 8e-9f;

        // -------------------------------------------------------------------
        // Nucleo da fisica (referencia :7247-7356).
        //
        // A ideia: a bola tem uma direcao de TRAJETORIA (`rollHomeYaw`) separada
        // da direcao para onde o jogador aponta (`player->yaw`). O que sobra
        // entre as duas e derrapagem lateral, que decai sozinha. E por isso que
        // virar em alta velocidade desliza em vez de pivotar.
        // -------------------------------------------------------------------
        {
            const s16 visualYaw = player->yaw;
            const s16 yawOffTrajectory = static_cast<s16>(player->yaw - gState.rollHomeYaw);
            const f32 alignment = Math_CosS(yawOffTrajectory);

            f32 forwardSpeed = (1.0f - gState.rollBounce) * gState.rollBallSpeed * alignment;
            if ((forwardSpeed < 0.0f) || ((speedTarget == 0.0f) && (ABS(yawOffTrajectory) > 0xFA0))) {
                forwardSpeed = 0.0f;
            }

            Math_StepToF(&gState.rollBounce, 0.0f, fabsf(alignment) * 20.0f);
            gState.rollForwardSpeed = forwardSpeed;

            // z_player.c:20041-20047. `spinFloor` e o piso do giro; `accelBudget`
            // (spC0) e quanto de velocidade AINDA falta atingir, e e ele — nao o
            // giro — que decide a aceleracao de superficie escorregadia.
            const s32 spinFloor = CLAMP_MIN(static_cast<s32>(forwardSpeed * 500.0f), 0);
            const s32 accelBudget = CLAMP_MIN(static_cast<s32>(speedTarget * 400.0f) - spinFloor, 0);
            gState.rollSpinTarget = CLAMP_MIN(gState.rollSpinTarget, spinFloor);

            const f32 trajX = forwardSpeed * Math_SinS(gState.rollHomeYaw);
            const f32 trajZ = forwardSpeed * Math_CosS(gState.rollHomeYaw);
            f32 driftX = (gState.rollBallSpeed * Math_SinS(player->yaw)) - trajX;
            f32 driftZ = (gState.rollBallSpeed * Math_CosS(player->yaw)) - trajZ;

            player->linearVelocity = forwardSpeed;
            player->yaw = gState.rollHomeYaw;
            player->actor.world.rot.y = player->yaw;

            // Normal REAL do poligino do chao (`Actor_GetSlopeDirection`,
            // z_actor.c:1730): a ladeira empurra na direcao da propria inclinacao,
            // nao na direcao para onde o Goron olha.
            //
            // O que estava aqui — `sin(floorPitch) * sin/cos(shape.rot.y)` — era
            // aproximacao da referencia skijer e so coincide quando a esfera ja
            // aponta ladeira abaixo. Em rampa diagonal a bola era empurrada para
            // o lado errado.
            f32 slopeGravX = 0.0f;
            f32 slopeGravZ = 0.0f;
            if (player->actor.floorPoly != nullptr) {
                slopeGravX = COLPOLY_GET_NORMAL(player->actor.floorPoly->normal.x);
                slopeGravZ = COLPOLY_GET_NORMAL(player->actor.floorPoly->normal.z);
            }

            if (gState.rollSpikeActive == 0) {
                const f32 withSlopeX = (0.6f * slopeGravX) + driftX;
                const f32 withSlopeZ = (0.6f * slopeGravZ) + driftZ;
                const f32 withSlopeLen = sqrtf(SQ(withSlopeX) + SQ(withSlopeZ));
                const f32 origLen = sqrtf(SQ(driftX) + SQ(driftZ));
                if ((withSlopeLen < origLen) || (withSlopeLen < 6.0f)) {
                    driftX = withSlopeX;
                    driftZ = withSlopeZ;
                }
            }

            const f32 driftLen = sqrtf(SQ(driftX) + SQ(driftZ));
            if (driftLen != 0.0f) {
                f32 reduced = driftLen - 0.3f;
                if (reduced < 0.0f) {
                    reduced = 0.0f;
                }
                const f32 scale = reduced / driftLen;
                driftX *= scale;
                driftZ *= scale;
            }

            // ANTI-REVERSAO (`func_8083A4A4`, z_player.c:8578, chamada em :20061).
            //
            // Tentar inverter mais de 0x6000 (~135 graus) nao vira a bola: ela
            // FREIA ate parar. Enquanto estiver freando, o MM pula a aceleracao,
            // a curva e o tombamento inteiros — e, sem espinhos, manda o giro
            // para -0xFA0, ou seja, a esfera passa a girar ao contrario.
            //
            // Nada disso existia no port. Era possivel inverter o sentido da bola
            // instantaneamente em velocidade maxima.
            f32 revSpeedTarget = speedTarget;
            s16 revYawTarget = yawTarget;
            bool braking = false;
            {
                const f32 decelRate = (gState.rollChargeLevel >= 5) ? 0.0f : 1.0f;
                const s16 reverseDiff = static_cast<s16>(player->yaw - revYawTarget);
                if (ABS(reverseDiff) > 0x6000) {
                    if (Math_StepToF(&player->linearVelocity, 0.0f, decelRate)) {
                        revSpeedTarget = 0.0f;
                        revYawTarget = player->yaw;
                    } else {
                        braking = true;
                    }
                }
            }

            if (braking) {
                if (gState.rollSpikeActive == 0) {
                    gState.rollChargeLevel = 4;
                }
                if (gState.rollChargeLevel == 4) {
                    gState.rollSpinTarget = -0xFA0;
                }
            } else {
                // Aceleracao e frenagem. A condicao de superficie escorregadia usa
                // `accelBudget` (spC0), nao o giro — foi o que a referencia trocou.
                // A lista de superficies tambem ganhou a neve, que o MM inclui.
                f32 accel;
                if (accelBudget >= 0x7D0 && (player->floorSfxOffset == (NA_SE_PL_WALK_ICE - SFX_FLAG) ||
                                             player->floorSfxOffset == (NA_SE_PL_WALK_SAND - SFX_FLAG) ||
                                             player->floorSfxOffset == (NA_SE_PL_WALK_DIRT - SFX_FLAG))) {
                    accel = 0.08f;
                } else {
                    accel = 0.0003f * static_cast<f32>(gState.rollSpinRate);
                }
                accel = CLAMP_MIN(accel, 0.0f);
                f32 decel = (Math_SinS(player->floorPitch) * 8.0f) + 0.6f;
                decel = CLAMP_MIN(decel, 0.0f);

                // `if (speedTarget != spCC) this->yaw = yawTarget;` (:20098).
                // Quando a anti-reversao zerou o alvo, a bola encara o proprio yaw.
                if (speedTarget != revSpeedTarget) {
                    player->yaw = revYawTarget;
                }
                Math_AsymStepToF(&player->linearVelocity, speedTarget, accel, decel);

                // Taxa de curva, VERBATIM do MM (z_player.c:20106-20107):
                //   spC8 = TRUNCF_BINANG(fabsf(actor.speed) * 20.0f) + 300;
                //   spC8 = CLAMP_MIN(spC8, 100);
                //
                // O ramo de baixa velocidade que existia aqui — `speed*50 + 50`
                // abaixo de 2.0 — era invencao da referencia skijer, justificada
                // por "atrito menor do OoT". O MM tem uma formula so e um piso de
                // 100; e o piso que resolve a oscilacao em baixa velocidade.
                s16 turnRate = static_cast<s16>(fabsf(player->actor.speedXZ) * 20.0f) + 300;
                turnRate = CLAMP_MIN(turnRate, static_cast<s16>(100));
                Math_ScaledStepToS(&player->yaw, revYawTarget, turnRate);
                gState.rollTurnRate = turnRate;
            }
            // O tombamento lateral tambem pertence ao ramo nao-freando: o MM o
            // executa dentro do mesmo `else` (:20112-20124). Ele acontece mais
            // abaixo nesta funcao, entao a decisao viaja pelo estado.
            gState.rollBraking = braking;

            forwardSpeed = player->linearVelocity;
            gState.rollHomeYaw = player->yaw;
            player->yaw = visualYaw;

            const f32 totalX = (Math_SinS(gState.rollHomeYaw) * forwardSpeed) + driftX;
            const f32 totalZ = (Math_CosS(gState.rollHomeYaw) * forwardSpeed) + driftZ;

            gState.rollBallSpeed = sqrtf(SQ(totalX) + SQ(totalZ));
            if (gState.rollBallSpeed > 18.0f) {
                gState.rollBallSpeed = 18.0f;
            }
            player->yaw = Math_Atan2S(totalZ, totalX);
        }

        // Velocidade projetada na ladeira (referencia :7358-7361).
        player->linearVelocity = gState.rollBallSpeed * Math_CosS(player->floorPitch);
        player->actor.velocity.y = gState.rollBallSpeed * Math_SinS(player->floorPitch);
        player->actor.world.rot.y = player->yaw;

        // Direcao da derrapagem, para a inclinacao direcional da esfera.
        {
            const f32 driftX = player->actor.velocity.x - (gState.rollBallSpeed * Math_SinS(gState.rollHomeYaw));
            const f32 driftZ = player->actor.velocity.z - (gState.rollBallSpeed * Math_CosS(gState.rollHomeYaw));
            if (SQ(driftX) + SQ(driftZ) > 1.0f) {
                gState.rollDriftYaw = Math_Atan2S(driftZ, driftX);
            }
        }

        Math_AsymStepToF(&gState.rollColorLerp, (gState.rollGroundPoundTimer != 0) ? 1.0f : 0.0f, 0.8f, 0.05f);

        if (ABS(gState.rollSpinRate) > 0xFA0) {
            player->stateFlags2 |= PLAYER_STATE2_NAVI_ALERT;
        }

        // Inclinacao lateral pelo terreno: sonda o chao a 30 unidades para cada
        // lado e usa a diferenca de altura como angulo de tombamento.
        {
            CollisionPoly* leftPoly = nullptr;
            CollisionPoly* rightPoly = nullptr;
            s32 leftBgId = 0;
            s32 rightBgId = 0;
            const f32 perpSin = Math_SinS(static_cast<s16>(player->yaw + 0x4000));
            const f32 perpCos = Math_CosS(static_cast<s16>(player->yaw + 0x4000));
            Vec3f leftPos = { player->actor.world.pos.x - perpSin * 30.0f, player->actor.world.pos.y + 60.0f,
                              player->actor.world.pos.z - perpCos * 30.0f };
            Vec3f rightPos = { player->actor.world.pos.x + perpSin * 30.0f, player->actor.world.pos.y + 60.0f,
                               player->actor.world.pos.z + perpCos * 30.0f };
            const f32 leftY = BgCheck_EntityRaycastFloor3(&play->colCtx, &leftPoly, &leftBgId, &leftPos);
            const f32 rightY = BgCheck_EntityRaycastFloor3(&play->colCtx, &rightPoly, &rightBgId, &rightPos);

            // Os dois limites e o passo vem do MM (z_player.c:20112-20124):
            //   if (fabsf(var_fa1) > 100.0f) var_fa1 = 0.0f;   // degrau absurdo
            //   var_a3 = Math_Atan2S_XY(60.0f, var_fa1);
            //   if (ABS(var_a3) > 0x2AAA) var_a3 = 0;          // inclinacao absurda
            //   Math_ScaledStepToS(&shape.rot.z, var_a3 + sp7C, spC8);
            //
            // O passo e `spC8` — a MESMA taxa de curva do frame — e nao o 0x190
            // fixo que estava aqui. E os dois clamps nao existiam: sem eles, uma
            // sonda que pega um degrau alto joga a esfera num tombamento que o MM
            // descarta.
            f32 heightDiff = 0.0f;
            if (leftY > BGCHECK_Y_MIN && rightY > BGCHECK_Y_MIN) {
                heightDiff = rightY - leftY;
                if (fabsf(heightDiff) > 100.0f) {
                    heightDiff = 0.0f;
                }
            }
            s16 tiltTarget = Math_Atan2S(60.0f, heightDiff);
            if (ABS(tiltTarget) > 0x2AAA) {
                tiltTarget = 0;
            }
            if (!gState.rollBraking) {
                Math_ScaledStepToS(&player->actor.shape.rot.z, static_cast<s16>(tiltTarget + steerLean),
                                   gState.rollTurnRate);
            }
        }

        // Som de rolar, nos dois modos da referencia (:7409-7450). Ambos sao
        // GATILHO por cruzamento de zero, nao loop — e por isso que o disparo
        // unico do OoT serve aqui sem adaptacao.
        if (gState.rollSpinRate == 0) {
            const s16 prevCounter = gState.rollSfxCounter;
            const s16 increment = static_cast<s16>(gState.rollBallSpeed * 800.0f);
            gState.rollSfxCounter = static_cast<s16>(gState.rollSfxCounter + increment);
            if ((player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) && increment != 0 &&
                (static_cast<s32>(prevCounter + increment) * static_cast<s32>(prevCounter)) <= 0) {
                PlaySfx(player, NA_SE_PL_ROLL);
            }
        } else {
            Math_ScaledStepToS(&gState.rollSfxCounter, 0, ABS(gState.rollSpinRate));
            const s16 prevRotX = player->actor.shape.rot.x;
            if ((player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) &&
                ((static_cast<s32>(gState.rollSpinRate + prevRotX) * static_cast<s32>(prevRotX)) <= 0)) {
                PlaySfx(player, NA_SE_PL_ROLL);
            }
        }

        // Poeira e derrapagem (referencia :7452-7508). O fator de derrapagem e
        // a diferenca entre a velocidade real e a velocidade de rotacao.
        if (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) {
            s32 skidFactor = static_cast<s32>(((player->actor.velocity.z * Math_CosS(player->yaw)) +
                                               (player->actor.velocity.x * Math_SinS(player->yaw))) *
                                              800.0f);
            skidFactor -= gState.rollSpinRate;
            skidFactor = ABS(skidFactor);

            // O wrapper de som do MM deduplica sozinho; o nosso nao, entao o
            // portao de 16 frames reproduz a cadencia dele em vez de empilhar
            // o disparo a cada frame.
            if (skidFactor > 0x1770 && (gState.actionTimer & 0x0F) == 0 && gState.rollSpikeActive == 0) {
                PlaySfx(player, NA_SE_PL_SLIP);
            }

            if (skidFactor > 0x7D0 && (gState.actionTimer % 2) == 0) {
                Color_RGBA8 dustPrim;
                Color_RGBA8 dustEnv;
                if (player->floorSfxOffset == (NA_SE_PL_WALK_ICE - SFX_FLAG)) {
                    dustPrim = { 220, 220, 240, 255 };
                    dustEnv = { 180, 180, 200, 255 };
                } else if (player->floorSfxOffset == (NA_SE_PL_WALK_SAND - SFX_FLAG)) {
                    dustPrim = { 200, 170, 110, 255 };
                    dustEnv = { 130, 100, 60, 255 };
                } else if (player->floorSfxOffset == (NA_SE_PL_WALK_GRASS - SFX_FLAG)) {
                    dustPrim = { 120, 160, 80, 255 };
                    dustEnv = { 80, 120, 50, 255 };
                } else {
                    dustPrim = { 170, 130, 90, 255 };
                    dustEnv = { 100, 80, 60, 255 };
                }
                Vec3f dustPos = { player->actor.world.pos.x + Rand_CenteredFloat(10.0f), player->actor.world.pos.y,
                                  player->actor.world.pos.z + Rand_CenteredFloat(10.0f) };
                Vec3f dustVel = { -Math_SinS(player->yaw) * gState.rollBallSpeed * 0.1f, 1.5f,
                                  -Math_CosS(player->yaw) * gState.rollBallSpeed * 0.1f };
                Vec3f dustAccel = { 0.0f, 0.3f, 0.0f };
                s16 dustScale = static_cast<s16>((skidFactor >> 0xA) + 1);
                s16 dustLife = static_cast<s16>((skidFactor >> 7) + 160);
                if (dustScale > 200) {
                    dustScale = 200;
                }
                if (dustLife > 255) {
                    dustLife = 255;
                }
                func_8002829C(play, &dustPos, &dustVel, &dustAccel, &dustPrim, &dustEnv, dustScale, 5);
            }
        }
    } else {
        // === NO AR (referencia :7510-7532) ===
        Math_ScaledStepToS(&player->actor.shape.rot.z, 0, 0x190);
        gState.rollSfxCounter = 0;

        if (gState.rollSpikeActive > 0) {
            // Com espinhos a bola plana: gravidade quase nula e curva lenta.
            player->actor.gravity = -1.0f;
            Math_ScaledStepToS(&gState.rollHomeYaw, yawTarget, 0x190);
            gState.rollBallSpeed = sqrtf(SQ(player->linearVelocity) + SQ(player->actor.velocity.y)) *
                                   ((player->linearVelocity >= 0.0f) ? 1.0f : -1.0f);
            if (gState.rollBallSpeed > 18.0f) {
                gState.rollBallSpeed = 18.0f;
            }
        } else {
            gState.rollTilt += player->actor.velocity.y * 0.005f;
            gState.rollBallSpeed = player->linearVelocity;
        }
    }

    // --- Cauda comum aos tres subestados (referencia :7534-7594) ---

    Math_ScaledStepToS(&player->actor.shape.rot.y, gState.rollHomeYaw, 0x7D0);

    // Giro da esfera. O MM e `spDC = speedTarget * 900.0f` (z_player.c:19984),
    // com um PISO na velocidade real logo depois (`:20041-20047`):
    //   var_a0 = spBC * 500.0f;  CLAMP_MIN(var_a0, 0);
    //   spDC   = CLAMP_MIN(spDC, var_a0);
    //
    // O piso e a peca que faltava. Sem ele, a referencia skijer precisou
    // inventar um multiplicador reduzido abaixo de 2.0 para a bola nao girar
    // como piao — tratando o sintoma. Com o piso, o giro nunca fica abaixo da
    // velocidade que a esfera realmente tem, e o caso degenerado some sozinho.
    const s32 spinTarget = gState.rollSpinTarget;
    {
        // Passo assimetrico, escrito a mao: o OoT nao tem Math_AsymStepToS.
        const s16 diff = static_cast<s16>(spinTarget - gState.rollSpinRate);
        const s16 step = (diff >= 0) ? ((spinTarget >= 0) ? 0x7D0 : 0x4B0) : ((spinTarget >= 0) ? 0x4B0 : 0x3E8);
        if (ABS(diff) <= step) {
            gState.rollSpinRate = static_cast<s16>(spinTarget);
        } else {
            gState.rollSpinRate = static_cast<s16>(gState.rollSpinRate + (diff > 0 ? step : -step));
        }
    }
    if (gState.rollSpinRate != 0) {
        player->actor.shape.rot.x += gState.rollSpinRate;
    }

    // Deformacao de achatamento/alongamento (referencia :7556-7578).
    {
        const f32 squashTarget = static_cast<f32>(ABS(gState.rollSpinRate)) * 0.00004f;

        if (gState.rollSquash < squashTarget) {
            gState.rollTilt += 0.08f;
        } else {
            gState.rollTilt += -0.07f;
        }

        gState.rollTilt = CLAMP(gState.rollTilt, -0.2f, 0.14f);
        if (fabsf(gState.rollTilt) < 0.12f) {
            if (Math_StepUntilF(&gState.rollSquash, squashTarget, gState.rollTilt)) {
                gState.rollTilt = 0.0f;
            }
        } else {
            gState.rollSquash += gState.rollTilt;
            gState.rollSquash = CLAMP(gState.rollSquash, -0.7f, 0.3f);
        }
    }

    // Collider do frame (referencia :7580-7594).
    //
    // Com espinhos a bola machuca SEMPRE; sem eles, so acima de 2.0 de
    // velocidade — a esfera parada nao e uma arma. A referencia escreve os dois
    // ramos separados porque no MM eles usam bits diferentes (DMG_GORON_SPIKE e
    // DMG_NORMAL_ROLL); no OoT ambos caem no mesmo equivalente semantico,
    // DMG_HAMMER_SWING (pancada pesada, nao corte), entao aqui e uma condicao
    // so. O dia em que os dois precisarem divergir, e aqui que se separa.
    if (gState.rollSpikeActive > 0 || gState.rollBallSpeed > 2.0f) {
        ArmRollAttack(player, play, DMG_HAMMER_SWING, 1, 25);
    } else {
        ClearRollAttack(player);
    }
}

// Curl e desenrolar: as duas pontas one-shot, do dispatcher da referencia
// (:12255-12311).
void UpdateRollTransitions(Player* player, PlayState* play, bool animationFinished) {
    if (gState.action == GoronAction::RollInit) {
        if (animationFinished || gState.actionTimer >= kRollInitTimeoutFrames) {
            EnterBall(player, play);
        }
        return;
    }

    // RollUncurl: desacelera e volta ao idle quando a animacao reversa chega
    // perto do frame 0.
    Math_StepToF(&player->linearVelocity, 0.0f, 2.0f);
    if (animationFinished || CurrentFrame() <= 1.0f) {
        SetAction(GoronAction::Idle);
        player->linearVelocity = 0.0f;
    }
}

// Entrada na bola pelo botao (referencia :3782-3791): A no chao. A guarda de
// agarrar existe porque o OoT oferece empurrar/puxar no mesmo botao, e roubar o
// A ali deixaria o jogador sem como mover bloco.
bool TryStartRoll(Player* player, PlayState* play) {
    if (!gState.ballEnabled || gState.entryBlocked) {
        return false;
    }
    const Input* input = &play->state.input[0];
    const bool requested = gState.ootRollActive || CHECK_BTN_ALL(input->press.button, BTN_A);
    if (!requested || !OnGround(player)) {
        return false;
    }
    if ((player->stateFlags2 & (PLAYER_STATE2_DO_ACTION_GRAB | PLAYER_STATE2_GRABBING_DYNAPOLY)) != 0) {
        return false;
    }
    if (!HasAnim("roll_enter")) {
        return false;
    }
    player->linearVelocity = 0.0f;
    if (!StartAnim("roll_enter", 0.67f, true)) {
        return false;
    }
    SetAction(GoronAction::RollInit);
    // MM_NA_SE_PL_GORON_TO_BALL -> baque de corpo.
    PlaySfx(player, NA_SE_PL_BODY_HIT);
    return true;
}

// ---------------------------------------------------------------------------
// SOCOS
//
// Origem: `MmForm_StartPunch` (referencia :4312), `MmForm_Action_Punch`
// (:4426), `MmForm_GoronAction_PunchEnd` (:4670) e a tabela `sGoronPunchFrames`
// (:4209). O host ja tinha esta logica; o que muda aqui e o DONO — ela passa a
// viver dentro da maquina, junto das outras acoes, em vez de espalhada pelo
// update do corpo.
//
// A referencia tem tambem um recuo de parede (`MmForm_CheckWallHit`) que
// interrompe o golpe ao acertar geometria. NAO portado nesta rodada: depende de
// raycast proprio e de uma excecao de dyna com o mesmo cuidado do quique da
// bola. Fica registrado no handoff.
// ---------------------------------------------------------------------------

// Janelas de dano por passo, de `sMeleeAttackAnimInfo` do MM.
constexpr std::uint8_t kGoronPunchFrames[3][2] = {
    { 6, 8 },   // A — esquerda
    { 12, 18 }, // B — direita
    { 8, 14 },  // C — bundada
};

// MM liga o quad do Goron em 5.0 e nao no `hitStart` da tabela
// (`earlyStart = isGoron ? 5.0f : hitStart`, de func_8083FCF0). A janela por
// hitStart e mais estreita que a original.
constexpr float kGoronPunchEarlyStart = 5.0f;

// `D_8085D09C` do MM da DMG_GORON_PUNCH com dano 2. O equivalente semantico no
// OoT do impacto pesado e DMG_HAMMER_SWING — pancada, nao corte.
constexpr std::int32_t kGoronPunchDamage = 2;

// Geometria do quad direcional por passo. E o perfil FECHADO dos tres socos:
// nao ha collider vindo de Lua nem dano parametrizavel por mod.
struct PunchQuad {
    float nearDist;
    float farDist;
    float sideOffset;
    float halfWidth;
    float yBottom;
    float yTop;
};

constexpr PunchQuad kGoronPunchQuads[3] = {
    { 20.0f, 55.0f, -15.0f, 15.0f, 20.0f, 55.0f },
    { 20.0f, 55.0f, 15.0f, 15.0f, 20.0f, 55.0f },
    { -10.0f, 30.0f, 0.0f, 30.0f, 5.0f, 30.0f },
};

const char* PunchAnimName(std::uint8_t step) {
    switch (step) {
        case 0:
            return "punch_a";
        case 1:
            return "punch_b";
        case 2:
            return "punch_c";
        default:
            return nullptr;
    }
}

// MM escolhe a variante `R` por `Player_CheckHostileLockOn` — Z travado num
// inimigo — e nao por estar em movimento. `sMeleeAttackAnimInfo` guarda o par
// end/endR e o seletor e o lock-on.
const char* PunchEndAnimName(std::uint8_t step, bool lockedOn) {
    switch (step) {
        case 0:
            return lockedOn ? "punch_a_end_run" : "punch_a_end";
        case 1:
            return lockedOn ? "punch_b_end_run" : "punch_b_end";
        case 2:
            return lockedOn ? "punch_c_end_run" : "punch_c_end";
        default:
            return nullptr;
    }
}

std::uint8_t PunchStepOf(GoronAction action) {
    switch (action) {
        case GoronAction::PunchB:
            return 1;
        case GoronAction::PunchC:
            return 2;
        default:
            return 0;
    }
}

GoronAction PunchActionOf(std::uint8_t step) {
    switch (step) {
        case 1:
            return GoronAction::PunchB;
        case 2:
            return GoronAction::PunchC;
        default:
            return GoronAction::PunchA;
    }
}

bool IsPunchAction(GoronAction action) {
    return action == GoronAction::PunchA || action == GoronAction::PunchB || action == GoronAction::PunchC ||
           action == GoronAction::PunchEnd;
}

void DisablePunchQuad(Player* player) {
    player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
    player->meleeWeaponQuads[0].info.toucherFlags = TOUCH_NONE;
    player->meleeWeaponQuads[0].info.toucher.dmgFlags = 0;
    gState.punchHitActive = false;
}

// O Player_Update ja produziu os quads da espada antes de a maquina rodar
// (ACTORCAT_PLAYER vem antes do ator hospedeiro). Enquanto a animacao do Goron
// e a dona visual do golpe, os dois sao zerados para a espada invisivel nao
// acertar junto do punho.
void EnablePunchQuad(Player* player, PlayState* play, std::uint8_t step) {
    ColliderQuad* primary = &player->meleeWeaponQuads[0];
    ColliderQuad* secondary = &player->meleeWeaponQuads[1];
    Collider_ResetQuadAT(play, &primary->base);
    Collider_ResetQuadAT(play, &secondary->base);
    primary->info.toucher.dmgFlags = 0;
    secondary->info.toucher.dmgFlags = 0;

    const PunchQuad& quad = kGoronPunchQuads[step];
    const f32 sinYaw = Math_SinS(player->yaw);
    const f32 cosYaw = Math_CosS(player->yaw);
    const f32 rightX = cosYaw;
    const f32 rightZ = -sinYaw;
    const Vec3f& pos = player->actor.world.pos;

    const f32 farX = pos.x + sinYaw * quad.farDist + rightX * quad.sideOffset;
    const f32 farZ = pos.z + cosYaw * quad.farDist + rightZ * quad.sideOffset;
    const f32 nearX = pos.x + sinYaw * quad.nearDist + rightX * quad.sideOffset;
    const f32 nearZ = pos.z + cosYaw * quad.nearDist + rightZ * quad.sideOffset;
    Vec3f a = { farX - rightX * quad.halfWidth, pos.y + quad.yTop, farZ - rightZ * quad.halfWidth };
    Vec3f b = { farX + rightX * quad.halfWidth, pos.y + quad.yTop, farZ + rightZ * quad.halfWidth };
    Vec3f c = { nearX + rightX * quad.halfWidth, pos.y + quad.yBottom, nearZ + rightZ * quad.halfWidth };
    Vec3f d = { nearX - rightX * quad.halfWidth, pos.y + quad.yBottom, nearZ - rightZ * quad.halfWidth };
    Collider_SetQuadVertices(primary, &a, &b, &c, &d);

    primary->base.atFlags = AT_ON | AT_TYPE_PLAYER;
    primary->info.toucher.dmgFlags = DMG_HAMMER_SWING;
    primary->info.toucher.damage = static_cast<std::uint8_t>(kGoronPunchDamage);
    primary->info.toucherFlags = TOUCH_ON | TOUCH_NEAREST;
    CollisionCheck_SetAT(play, &play->colChkCtx, &primary->base);
    gState.punchHitActive = true;
}

// Limpa dano herdado de QUALQUER collider do Player antes de armar o quad. Uma
// flag deixada por uma bola Goron anterior contaria como segundo ataque — a
// referencia registra o sintoma real: combo quebrando rocha que so responde a
// martelo.
void WipeStalePlayerDamage(Player* player) {
    player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
    player->meleeWeaponQuads[0].info.toucher.dmgFlags = 0;
    player->meleeWeaponQuads[1].base.atFlags &= ~AT_ON;
    player->meleeWeaponQuads[1].info.toucher.dmgFlags = 0;
    player->cylinder.base.atFlags &= ~AT_ON;
    player->cylinder.info.toucher.dmgFlags = 0;
}

// Entrada do combo (referencia :4312). A entrada e o BOTAO lido direto do
// Input, e nao um estado do Player do OoT: a forma nao empunha espada, entao
// `meleeWeaponState` nunca sobe e uma checagem por ele nunca veria um soco.
bool TryStartPunch(Player* player, PlayState* play) {
    if (!gState.punchEnabled || gState.entryBlocked) {
        return false;
    }
    const Input* input = &play->state.input[0];
    if (!CHECK_BTN_ALL(input->press.button, BTN_B) || !OnGround(player)) {
        return false;
    }
    if (!HasAnim("punch_a")) {
        return false;
    }
    WipeStalePlayerDamage(player);
    if (!StartAnim("punch_a", 1.0f, true)) {
        return false;
    }
    gState.comboStep = 0;
    gState.comboBPressed = false;
    gState.punchHitActive = false;
    SetAction(GoronAction::PunchA);
    // `Player_StopHorizontalMovement` na entrada do golpe; o avanco real vem da
    // translacao de raiz da animacao.
    player->linearVelocity = 0.0f;
    // MM_NA_SE_IT_GORON_PUNCH_SWING -> o swing pesado do OoT.
    PlaySfx(player, NA_SE_IT_SWORD_SWING_HARD);
    // Poeira nos pes ao plantar o golpe.
    if (OnGround(player)) {
        EffectSsHahen_SpawnBurst(play, &player->bodyPartsPos[PLAYER_BODYPART_L_FOOT], 3.0f, 0, 6, 4, 2, -1, 10,
                                 nullptr);
        EffectSsHahen_SpawnBurst(play, &player->bodyPartsPos[PLAYER_BODYPART_R_FOOT], 3.0f, 0, 6, 4, 2, -1, 10,
                                 nullptr);
    }
    return true;
}

// Um frame de golpe (referencia :4426).
void ActionPunch(Player* player, PlayState* play) {
    const std::uint8_t step = PunchStepOf(gState.action);
    const f32 curFrame = CurrentFrame();
    const f32 endFrame = CurrentAnimLastFrame();
    const f32 hitStart = static_cast<f32>(kGoronPunchFrames[step][0]);
    const f32 hitEnd = static_cast<f32>(kGoronPunchFrames[step][1]);
    // Deteccao antecipada: o quad liga em 5.0, ou no hitStart se ele vier antes.
    const f32 earlyStart = kGoronPunchEarlyStart < hitStart ? kGoronPunchEarlyStart : hitStart;

    if (curFrame > hitEnd) {
        DisablePunchQuad(player);
    } else if (curFrame >= earlyStart) {
        EnablePunchQuad(player, play, step);
        // Bundada (passo 2): estilhaco no chao no frame do impacto.
        if (step == 2 && curFrame >= hitStart && curFrame < hitStart + 1.5f && OnGround(player)) {
            Vec3f burstPos = player->actor.world.pos;
            burstPos.y += 5.0f;
            EffectSsHahen_SpawnBurst(play, &burstPos, 4.0f, 0, 12, 6, 3, -1, 10, nullptr);
            // MM_NA_SE_PL_GORON_PUNCH -> a pancada de martelo do OoT.
            PlaySfx(player, NA_SE_IT_HAMMER_HIT);
        }
    }

    // MM desacelera durante o golpe; quem avanca de verdade e a translacao de
    // raiz, aplicada pelo host (ver `UsesRootMotion`).
    Math_StepToF(&player->linearVelocity, 0.0f, 5.0f);

    // Um B durante o golpe AGENDA o proximo. Segurar nao encadeia — a
    // referencia le `press.button`, nao `cur.button`.
    if (step < 2) {
        const Input* input = &play->state.input[0];
        if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
            gState.comboBPressed = true;
        }
    }

    if (endFrame <= 0.0f || curFrame < endFrame) {
        return;
    }

    DisablePunchQuad(player);

    if (gState.comboBPressed && step < 2) {
        const std::uint8_t nextStep = static_cast<std::uint8_t>(step + 1);
        const char* nextAnim = PunchAnimName(nextStep);
        if (nextAnim != nullptr && HasAnim(nextAnim) && StartAnim(nextAnim, 1.0f, true)) {
            gState.comboStep = nextStep;
            gState.comboBPressed = false;
            SetAction(PunchActionOf(nextStep));
            player->linearVelocity = 0.0f;
            PlaySfx(player, NA_SE_IT_SWORD_SWING_HARD);
            return;
        }
    }

    // Sem combo: recuperacao. A escolha end/endR e por lock-on.
    const bool lockedOn = (player->stateFlags1 & PLAYER_STATE1_HOSTILE_LOCK_ON) != 0;
    const char* endAnim = PunchEndAnimName(step, lockedOn);
    if (endAnim != nullptr && HasAnim(endAnim) && StartAnim(endAnim, 1.0f, true)) {
        SetAction(GoronAction::PunchEnd);
        return;
    }
    // Um mod pode declarar so a recuperacao parada; ela e melhor que cortar o
    // golpe direto para idle com o Player ainda em movimento.
    const char* stillAnim = PunchEndAnimName(step, false);
    if (lockedOn && stillAnim != nullptr && HasAnim(stillAnim) && StartAnim(stillAnim, 1.0f, true)) {
        SetAction(GoronAction::PunchEnd);
        return;
    }
    SetAction(GoronAction::Idle);
}

// Recuperacao (referencia :4670). Um B novo durante ela encadeia na hora, e o
// passo volta ciclicamente para A depois de C. Sem isto o combo morre no
// terceiro golpe e so volta a existir depois da recuperacao inteira.
void ActionPunchEnd(Player* player, PlayState* play) {
    DisablePunchQuad(player);
    Math_StepToF(&player->linearVelocity, 0.0f, 5.0f);

    const Input* input = &play->state.input[0];
    if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
        const std::uint8_t nextStep = static_cast<std::uint8_t>((gState.comboStep + 1) % 3);
        const char* nextAnim = PunchAnimName(nextStep);
        if (nextAnim != nullptr && HasAnim(nextAnim)) {
            WipeStalePlayerDamage(player);
            if (StartAnim(nextAnim, 1.0f, true)) {
                gState.comboStep = nextStep;
                gState.comboBPressed = false;
                SetAction(PunchActionOf(nextStep));
                player->linearVelocity = 0.0f;
                PlaySfx(player, NA_SE_IT_SWORD_SWING_HARD);
                return;
            }
        }
    }

    const f32 endFrame = CurrentAnimLastFrame();
    if (endFrame <= 0.0f || CurrentFrame() >= endFrame) {
        SetAction(GoronAction::Idle);
    }
}

// Entrada da defesa Goron (referencia :3585 e :3682). A maquina e dona da
// decisao e do movimento; o host observa `IsShielding()` para materializar o
// esqueleto dedicado e o shieldQuad, sem manter uma segunda action enum.
bool TryStartShield(Player* player, PlayState* play) {
    if (!gState.shieldEnabled || gState.entryBlocked || !OnGround(player)) {
        return false;
    }
    const Input* input = &play->state.input[0];
    if (!CHECK_BTN_ALL(input->cur.button, BTN_R)) {
        return false;
    }

    gState.shieldLockedYaw = player->actor.shape.rot.y;
    player->linearVelocity = 0.0f;
    player->stateFlags1 |= PLAYER_STATE1_SHIELDING;
    player->upperLimbRot.x = 0;
    player->upperLimbRot.y = 0;
    player->upperLimbRot.z = 0;
    SetAction(GoronAction::Shield);

    // MM_NA_SE_PL_GORON_SQUAT ainda nao tem loop nativo no host; o baque de
    // corpo do OoT e o mesmo fallback que o caminho anterior usava.
    PlaySfx(player, NA_SE_PL_BODY_HIT);
    return true;
}

void ActionShield(Player* player, PlayState* play) {
    const Input* input = &play->state.input[0];
    if (!gState.shieldEnabled || !CHECK_BTN_ALL(input->cur.button, BTN_R) || !OnGround(player) ||
        PlayerOwnsBody(player)) {
        player->stateFlags1 &= ~PLAYER_STATE1_SHIELDING;
        SetAction(GoronAction::Idle);
        PlaySfx(player, NA_SE_PL_BODY_HIT);
        return;
    }

    player->linearVelocity = 0.0f;
    player->actor.velocity.y = 0.0f;
    player->actor.shape.rot.y = gState.shieldLockedYaw;
    player->actor.world.rot.y = gState.shieldLockedYaw;
    player->yaw = gState.shieldLockedYaw;
    // Player_UpdateCommon limpa a flag todo frame; a maquina precisa reafirmar
    // enquanto R estiver sustentado.
    player->stateFlags1 |= PLAYER_STATE1_SHIELDING;
}

// As cinco notas do staff do OoT para as cinco animacoes de tambor do Goron.
// O Player ja consumiu os botoes quando a ocarina esta ativa; ler Input aqui
// perderia quase todas as notas, portanto a fonte autoritativa e o playStaff.
const char* GakkiNoteAnimation(std::uint8_t buttonIndex) {
    switch (buttonIndex) {
        case OCARINA_BTN_A:
            return "gakki_play_a";
        case OCARINA_BTN_C_DOWN:
            return "gakki_play_d";
        case OCARINA_BTN_C_RIGHT:
            return "gakki_play_r";
        case OCARINA_BTN_C_LEFT:
            return "gakki_play_l";
        case OCARINA_BTN_C_UP:
            return "gakki_play_u";
        default:
            return nullptr;
    }
}

bool OcarinaHeld(const Player* player) {
    return (player->stateFlags2 & PLAYER_STATE2_OCARINA_PLAYING) != 0;
}

bool StartGakkiWait() {
    if (HasAnim("gakki_wait") && StartAnim("gakki_wait", 1.0f, false)) {
        return true;
    }
    return HasAnim("gakki_play") && StartAnim("gakki_play", 1.0f, false);
}

// ---------------------------------------------------------------------------
// INSTRUMENTO DA OCARINA POR FORMA
//
// Fonte primaria: `sPlayerFormOcarinaInstruments[]` em
// 2S2H `mm/src/code/z_message.c:4377-4382`, indexada por `CUR_FORM`:
//   DEFAULT, GORON_DRUMS, ZORA_GUITAR, DEKU_PIPES
// e aplicada por `AudioOcarina_SetInstrument(sPlayerFormOcarinaInstruments[CUR_FORM])`
// em nove pontos do subsistema de mensagem (`:4536` em diante).
//
// Dois achados que corrigem o que este handoff dizia antes:
//
//   1. a troca NAO mora no ator do Player — `z_player.c` so chama
//      `SetInstrument(OFF)`. Mora no subsistema de ocarina/mensagem.
//   2. nao existem "cinco IDs de nota" a portar. A nota continua sendo a da
//      ocarina; o que muda e o TIMBRE, por um indice de instrumento.
//
// LIMITE CONHECIDO: no OoT o enum vai ate 6 (`z64audio.h:1119`) e o indice do
// instrumento e resolvido dentro da SEQUENCIA 0, que e a do OoT. O 7 dos
// tambores existe na sequencia do MM, nao na dele. Esta chamada e o teste barato
// e reversivel dessa fronteira: se a sequencia do OoT ignorar o indice, nada
// toca e nada quebra; se responder, ganhamos o timbre sem portar amostra
// nenhuma. Se nao responder, o caminho e o mesmo indice pelo `MmSeq`, que ja
// interpreta a sequencia 0 do MM no canal 5 (o da ocarina).
constexpr std::uint8_t kOcarinaInstrumentGoronDrums = 7;

void ApplyGakkiInstrument(bool on) {
    if (gState.services.setOcarinaInstrument == nullptr) {
        return;
    }
    gState.services.setOcarinaInstrument(on ? kOcarinaInstrumentGoronDrums : 0);
}

void EndGakki() {
    gState.gakkiPhase = 0;
    gState.gakkiLastNote = 0xFF;
    // O MM zera o instrumento de forma centralizada em `Player_SetAction`
    // (z_player.c:4480). Aqui a saida do instrumento e o ponto equivalente.
    ApplyGakkiInstrument(false);
    SetAction(GoronAction::Idle);
}

// Entrada do instrumento. A abertura e opcional: a fase precisa existir mesmo
// se o asset nao carregar, para os modelos ainda aparecerem e a ocarina do OoT
// continuar controlavel.
bool TryStartGakki(Player* player) {
    if (gState.entryBlocked || !OcarinaHeld(player)) {
        return false;
    }

    SetAction(GoronAction::Gakki);
    gState.gakkiLastNote = 0xFF;
    ApplyGakkiInstrument(true);
    if (HasAnim("gakki_start") && StartAnim("gakki_start", 1.0f, true)) {
        gState.gakkiPhase = 1;
    } else {
        gState.gakkiPhase = 2;
        StartGakkiWait();
    }
    return true;
}

// Fases da referencia (:10912): 1 abre, 2 toca em loop, 3 recolhe a abertura
// ao contrario. Esta acao dirige somente a POSE e nunca pausa a action func do
// Player; e ela que mantem a ocarina, as notas e a saida do OoT funcionando.
void ActionGakki(Player* player, bool animationFinished) {
    if (gState.entryBlocked) {
        EndGakki();
        return;
    }

    if (!OcarinaHeld(player) && gState.gakkiPhase != 3) {
        gState.gakkiPhase = 3;
        gState.gakkiLastNote = 0xFF;
        if (!HasAnim("gakki_start") || !StartAnim("gakki_start", 1.0f, true, true)) {
            EndGakki();
        }
        return;
    }

    switch (gState.gakkiPhase) {
        case 1:
            if (animationFinished) {
                gState.gakkiPhase = 2;
                StartGakkiWait();
            }
            break;
        case 2: {
            OcarinaStaff* staff = AudioOcarina_GetPlayingStaff();
            const std::uint8_t note =
                (staff != nullptr && staff->state != 0 && staff->buttonIndex < 5) ? staff->buttonIndex : 0xFF;
            if (note != gState.gakkiLastNote) {
                gState.gakkiLastNote = note;
                const char* noteAnim = GakkiNoteAnimation(note);
                if (noteAnim != nullptr && HasAnim(noteAnim)) {
                    StartAnim(noteAnim, 1.0f, false);
                } else if (note == 0xFF) {
                    StartGakkiWait();
                }
            }
            break;
        }
        case 3:
            if (animationFinished || CurrentFrame() <= 0.5f) {
                EndGakki();
            }
            break;
        default:
            EndGakki();
            break;
    }
}

// Limiares de locomocao do Player de MM: abaixo de 0.5 e parado, acima de 4.0 e
// corrida. Sao os mesmos numeros que o fallback antigo usava, agora dentro de
// uma acao com dono em vez de uma escolha solta por frame.
constexpr float kWalkThreshold = 0.5f;
constexpr float kRunThreshold = 4.0f;
constexpr float kLandDecelRate = 8.0f;

// Borda ar -> chao. A referencia usa a queda acumulada para acelerar o pouso
// curto e conserva a animacao completa nas quedas longas. A spec desta forma
// declara uma unica chave `land`; a diferenca continua sendo a velocidade.
bool EnterLand(Player* player) {
    if (!HasAnim("land")) {
        SetAction(GoronAction::Idle);
        return false;
    }

    const bool shortLanding = player->fallDistance <= 80;
    if (!StartAnim("land", shortLanding ? 1.5f : 1.0f, true)) {
        SetAction(GoronAction::Idle);
        return false;
    }

    if (!shortLanding) {
        // Mesmo primeiro passo da referencia antes de a acao Land assumir a
        // desaceleracao por frame.
        Math_StepToF(&player->linearVelocity, 0.0f, 3.0f);
    }
    PlaySfx(player, NA_SE_PL_LAND);
    SetAction(GoronAction::Land);
    return true;
}

void ActionLand(Player* player, bool animationFinished) {
    // `WantsPlayerActionPaused()` permanece false: o OoT conserva a propria
    // action func, enquanto a maquina possui apenas a pose e a recuperacao.
    Math_StepToF(&player->linearVelocity, 0.0f, kLandDecelRate);
    if (animationFinished) {
        SetAction(GoronAction::Idle);
    }
}

bool EnterDamage(Player* player) {
    gState.damageLatched = true;
    // O primeiro frame do knockback forte ainda pode carregar GROUND enquanto
    // o OoT ja escreveu velocity.y positiva. Tratar os dois sinais evita
    // classificar o lancamento como dano de solo.
    gState.damageWasAirborne = !OnGround(player) || player->actor.velocity.y > 0.0f;

    if (!HasAnim("damage") || !StartAnim("damage", 1.0f, true)) {
        SetAction(gState.damageWasAirborne
                      ? (player->actor.velocity.y < 0.0f ? GoronAction::Fall : GoronAction::Jump)
                      : GoronAction::Idle);
        return false;
    }

    SetAction(GoronAction::Damage);
    return true;
}

void ActionDamage(Player* player, bool animationFinished) {
    if (!OnGround(player)) {
        gState.damageWasAirborne = true;
    }

    // `MmForm_GoronAction_Damage` desacelera por 8.0 quando a referencia
    // possui o pipeline de dano inteiro. Aqui o OoT ja possui a action func de
    // knockback e faz essa desaceleracao. Repetir Math_StepToF neste modulo
    // encurtaria o impulso; por isso observamos yaw/velocidades sem reescrever.
    if (gState.damageWasAirborne) {
        if (OnGround(player) && player->actor.velocity.y <= 0.0f) {
            EnterLand(player);
            return;
        }
        // Se a pose acabar ainda no ar, a maquina devolve a pose generica de
        // subida/queda, mas conserva o latch ate PLAYER_STATE1_DAMAGED cair.
        if (animationFinished) {
            SetAction(player->actor.velocity.y < 0.0f ? GoronAction::Fall : GoronAction::Jump);
        }
        return;
    }

    // Dano de solo: o OoT decide quando o controle volta; a maquina somente
    // deixa o one-shot terminar e devolve a pose de locomocao.
    if (animationFinished) {
        SetAction(GoronAction::Idle);
    }
}

void UpdateLocomotion(Player* player) {
    const float speed = std::fabs(player->linearVelocity);
    const GoronAction wanted = speed >= kRunThreshold  ? GoronAction::Run
                               : speed >= kWalkThreshold ? GoronAction::Walk
                                                         : GoronAction::Idle;
    if (gState.action != wanted) {
        SetAction(wanted);
    }
}

} // namespace

void Init(const Services& services) {
    gState = FormState{};
    gState.services = services;
    gState.initialized = true;
    gState.action = GoronAction::Idle;
}

void Reset() {
    gState = FormState{};
}

// `Init` e `Reset` recriam o estado do zero, entao a chave volta a false nos
// dois. E o comportamento certo: quem religa o corpo declara de novo se assumiu
// a bola, em vez de herdar a decisao de uma cena anterior.
void SetBallEnabled(bool enabled) {
    gState.ballEnabled = enabled;
}

bool BallEnabled() {
    return gState.ballEnabled;
}

void SetEntryBlocked(bool blocked) {
    gState.entryBlocked = blocked;
}

void SetOotRollActive(bool active) {
    gState.ootRollActive = active;
}

void ReleaseBody(Player* player) {
    if (player == nullptr) {
        return;
    }
    if (gState.rollAttackArmed) {
        ClearRollAttack(player);
    }
    if (IsPunchAction(gState.action) || gState.punchHitActive) {
        DisablePunchQuad(player);
        gState.comboStep = 0;
        gState.comboBPressed = false;
        SetAction(GoronAction::Idle);
    }
    if (gState.action == GoronAction::Shield) {
        player->stateFlags1 &= ~PLAYER_STATE1_SHIELDING;
        SetAction(GoronAction::Idle);
    }
    if (gState.action == GoronAction::Gakki) {
        EndGakki();
    }
    if (gState.action == GoronAction::Damage || gState.action == GoronAction::Land) {
        SetAction(GoronAction::Idle);
    }
    gState.damageLatched = false;
    gState.damageWasAirborne = false;
    // O collider nao e a unica coisa que a bola pendura no Player. Um
    // desligamento fora do fluxo normal (troca de forma, troca de cena, o mod
    // desativado no meio do rolamento) tem de devolver TAMBEM o input, a
    // gravidade, a sombra e as travas de rotacao — senao o Link fica sem
    // controle, ou pequeno, ou com a gravidade de baque, e nada disso se
    // corrige sozinho depois.
    if (IsRollAction(gState.action)) {
        player->actor.gravity = GetGravity(GravityProfile::Normal);
        player->actor.shape.rot.x = 0;
        player->actor.shape.rot.z = 0;
        if (gState.savedShadowScale > 0.0f) {
            player->actor.shape.shadowScale = gState.savedShadowScale;
        }
        player->stateFlags2 &= ~(PLAYER_STATE2_DISABLE_ROTATION_Z_TARGET | PLAYER_STATE2_DISABLE_ROTATION_ALWAYS);
        player->stateFlags1 &= ~PLAYER_STATE1_JUMPING;
        SetAction(GoronAction::Idle);
    }
    if (gState.rollOwnsInputDisable) {
        player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
        gState.rollOwnsInputDisable = false;
    }
}

GoronAction CurrentAction() {
    return gState.action;
}

const char* CurrentActionName() {
    switch (gState.action) {
        case GoronAction::OotAction:
            return "oot";
        case GoronAction::Idle:
            return "idle";
        case GoronAction::Walk:
            return "walk";
        case GoronAction::Run:
            return "run";
        case GoronAction::Jump:
            return "jump";
        case GoronAction::Fall:
            return "fall";
        case GoronAction::Land:
            return "land";
        case GoronAction::Damage:
            return "damage";
        case GoronAction::PunchA:
            return "punch_a";
        case GoronAction::PunchB:
            return "punch_b";
        case GoronAction::PunchC:
            return "punch_c";
        case GoronAction::PunchEnd:
            return "punch_end";
        case GoronAction::RollInit:
            return "roll_init";
        case GoronAction::Roll:
            return "roll";
        case GoronAction::RollJump:
            return "roll_jump";
        case GoronAction::RollPound:
            return "roll_pound";
        case GoronAction::RollUncurl:
            return "roll_uncurl";
        case GoronAction::Shield:
            return "shield";
        case GoronAction::Gakki:
            return "gakki";
    }
    return "?";
}

const char* DesiredAnimation() {
    switch (gState.action) {
        case GoronAction::Idle:
            return "idle";
        case GoronAction::Walk:
            return "walk";
        case GoronAction::Run:
            return "run";
        case GoronAction::Jump:
            // A referencia usa o header de pulo normal do Player de MM para as
            // formas; a spec do mod e quem decide o caminho.
            return HasAnim("jump") ? "jump" : nullptr;
        case GoronAction::Fall:
            return HasAnim("fall") ? "fall" : nullptr;
        default:
            // Acoes dirigidas por one-shot, ou cessao ao OoT: a maquina nao
            // opina sobre a pose.
            return nullptr;
    }
}

std::uint8_t ComboStep() {
    return gState.comboStep;
}

std::uint8_t GakkiPhase() {
    return gState.gakkiPhase;
}

std::uint8_t GakkiLastNote() {
    return gState.gakkiLastNote;
}

bool WantsPlayerActionPaused() {
    // Bola, socos e defesa tomam o movimento para si. Idle/walk/run continuam
    // sendo movidos pela action func do OoT, e pausa-la neles deixaria o Link
    // parado no lugar.
    return IsRollAction(gState.action) || IsPunchAction(gState.action) || gState.action == GoronAction::Shield;
}

void SetPunchEnabled(bool enabled) {
    gState.punchEnabled = enabled;
}

bool PunchEnabled() {
    return gState.punchEnabled;
}

bool IsPunching() {
    return IsPunchAction(gState.action);
}

bool PunchHitActive() {
    return gState.punchHitActive;
}

bool UsesRootMotion() {
    // So os tres golpes. A recuperacao NAO entra: no MM ela nao carrega
    // translacao util, e deixa-la mover foi o que produziu o "terceiro soco que
    // teleporta e volta" quando o perfil era escolhido por nome de animacao.
    return gState.action == GoronAction::PunchA || gState.action == GoronAction::PunchB ||
           gState.action == GoronAction::PunchC;
}

void SetShieldEnabled(bool enabled) {
    gState.shieldEnabled = enabled;
}

bool ShieldEnabled() {
    return gState.shieldEnabled;
}

bool IsShielding() {
    return gState.action == GoronAction::Shield;
}

bool IsRolling() {
    return IsRollAction(gState.action);
}

bool IsBallBody() {
    return IsBallAction(gState.action);
}

bool RollSpikesActive() {
    return gState.rollSpikeActive > 0;
}

std::int16_t RollChargeLevel() {
    return gState.rollChargeLevel;
}

float RollSquash() {
    return gState.rollSquash;
}

float RollColorLerp() {
    return gState.rollColorLerp;
}

float RollBounce() {
    return gState.rollBounce;
}

std::int16_t RollDriftYaw() {
    return gState.rollDriftYaw;
}

std::int16_t RollSfxCounter() {
    return gState.rollSfxCounter;
}

std::uint8_t RollWallBounceTimer() {
    return gState.rollWallBounceTimer;
}

bool RollAttackActive() {
    return gState.rollAttackArmed;
}

bool IsGroundPound() {
    return gState.action == GoronAction::RollJump || gState.action == GoronAction::RollPound;
}

const char* RollPhaseName() {
    switch (gState.action) {
        case GoronAction::RollInit:
            return "enter";
        case GoronAction::Roll:
            return "rolling";
        case GoronAction::RollJump:
            return "ground_pound_rise";
        case GoronAction::RollPound:
            return "ground_pound_impact";
        case GoronAction::RollUncurl:
            return "exit";
        default:
            return nullptr;
    }
}

bool Update(Player* player, PlayState* play, bool animationFinished) {
    if (!gState.initialized || player == nullptr || play == nullptr) {
        return false;
    }

    ++gState.actionTimer;

    const bool damaged = (player->stateFlags1 & PLAYER_STATE1_DAMAGED) != 0;
    if (!damaged) {
        gState.damageLatched = false;
    }

    // `MmForm_CheckDamage` da referencia consumia um payload salvo antes do
    // update e reaplicava modificadores. Nesta arvore Health_ChangeBy e a
    // action func do OoT ja rodaram: PLAYER_STATE1_DAMAGED e a borda
    // autoritativa. Interrompemos apenas as posses da forma, sem tocar vida,
    // invencibilidade, yaw, linearVelocity ou velocity.y.
    if (damaged && !gState.damageLatched && !gState.entryBlocked) {
        // FLINCH EM VELOCIDADE — `func_80833B18`, z_player.c:5974-5980:
        //
        //   } else if ((this->speedXZ > 4.0f) && !Player_CheckHostileLockOn(this)) {
        //       this->unk_B64 = 20;
        //       Player_RequestRumble(play, this, 120, 20, 10, SQ(0));
        //       Player_AnimSfx_PlayVoice(this, NA_SE_VO_LI_DAMAGE_S);
        //       return;
        //   }
        //
        // Acima de 4.0 e sem lock-on hostil, o MM **nao interrompe a acao**: so
        // sacode o controle e solta a voz. E por isso que a bola do Goron
        // ATROPELA dano em MM em vez de desenrolar a cada arranhao.
        //
        // O port derrubava a forma em qualquer dano, e o efeito era o oposto do
        // esperado justamente no momento em que a bola esta rapida — ou seja,
        // sempre que ela importa.
        const bool lockedOn = (player->stateFlags1 & PLAYER_STATE1_HOSTILE_LOCK_ON) != 0;
        if (fabsf(player->linearVelocity) > 4.0f && !lockedOn) {
            gState.damageLatched = true;
            Rumble_Request(0.0f, 120, 20, 10);
            return true;
        }
        ReleaseBody(player);
        EnterDamage(player);
        return true;
    }
    if (gState.action == GoronAction::Damage) {
        // Cutscene/item/dialogo continuam sendo do OoT. Damage nunca pausa a
        // action func, inclusive se a borda coincidir com a ocarina.
        if (gState.entryBlocked || PlayerOwnsBody(player)) {
            SetAction(GoronAction::OotAction);
            return false;
        }
        ActionDamage(player, animationFinished);
        return true;
    }

    const bool rolling = IsRollAction(gState.action);
    const bool punching = IsPunchAction(gState.action);
    const bool shielding = gState.action == GoronAction::Shield;
    const bool gakki = gState.action == GoronAction::Gakki;

    // A ocarina vive dentro de PLAYER_STATE1_IN_ITEM_CS. Entre nela ANTES da
    // cessao generica ao OoT, mas sem disputar qualquer acao blocker da forma.
    // Diferente de bola/soco/defesa, Gakki nunca toma o movimento.
    if (!rolling && !punching && !shielding && !gakki && TryStartGakki(player)) {
        return true;
    }
    if (gakki) {
        ActionGakki(player, animationFinished);
        return true;
    }

    // A referencia converte o roll que o OoT ja aceitou em curl, em vez de
    // disputar o mesmo A cru. Isto precisa acontecer ANTES de PlayerOwnsBody:
    // `Player_Action_Roll` e justamente uma das acoes que fariam a maquina
    // ceder e deixariam apenas a esquiva dirigir o corpo externo.
    if (!rolling && !punching && !shielding && gState.ootRollActive && TryStartRoll(player, play)) {
        return true;
    }

    // Cede ao OoT enquanto ele estiver dirigindo o Player. A forma nao disputa:
    // e a licao mais cara desta arvore — o corpo externo e a action func do
    // Player escrevendo a pose no mesmo frame produziram todos os sintomas de
    // "animacao que nao vai" de OOT-GORON-001.
    //
    // A bola e a excecao declarada da referencia (:11293-11318): ela e
    // "blocker" e nao cede. Aqui isso e mais que estetica — a propria bola liga
    // `PLAYER_STATE1_INPUT_DISABLED`, que esta na lista de `PlayerOwnsBody`;
    // sem a excecao ela cederia ao OoT no frame seguinte ao proprio inicio.
    if (!rolling && !punching && !shielding && PlayerOwnsBody(player)) {
        gState.action = GoronAction::OotAction;
        return false;
    }
    if (gState.action == GoronAction::OotAction) {
        // Saiu do controle do OoT: volta para a locomocao e deixa o proximo
        // bloco escolher a pose pela velocidade real.
        SetAction(GoronAction::Idle);
    }

    // A bola trata ar e chao por dentro — a fisica dela e outra nos dois casos,
    // e passar pela transicao generica abaixo a jogaria para Fall no primeiro
    // quique. Por isso ela despacha ANTES.
    if (rolling) {
        if (gState.action == GoronAction::RollInit || gState.action == GoronAction::RollUncurl) {
            UpdateRollTransitions(player, play, animationFinished);
        } else {
            ActionGoronRoll(player, play);
        }
        return true;
    }

    // O combo tambem despacha antes da transicao de ar: um soco perto de uma
    // borda nao pode virar Fall no meio do golpe, e a recuperacao precisa
    // terminar mesmo se o Link escorregar do chao.
    if (punching) {
        if (gState.action == GoronAction::PunchEnd) {
            ActionPunchEnd(player, play);
        } else {
            ActionPunch(player, play);
        }
        return true;
    }

    // Shield tambem e blocker: enquanto R estiver sustentado ela nao passa pela
    // transicao generica de ar/chao nem pela locomocao.
    if (shielding) {
        ActionShield(player, play);
        return true;
    }

    // Ar e chao antes do dispatch, como MmForm_UpdateActive faz: qualquer acao
    // de solo perde o corpo ao sair do chao, e qualquer acao de ar volta ao
    // pouso, sem cada acao ter de tratar isso por conta propria.
    if (!OnGround(player)) {
        const GoronAction air = player->actor.velocity.y < 0.0f ? GoronAction::Fall : GoronAction::Jump;
        if (gState.action != air) {
            SetAction(air);
        }
        return true;
    }

    // O frame anterior ainda era Jump/Fall e este ja esta no piso: a maquina
    // assume o one-shot uma unica vez. Enquanto Land estiver ativo, nao passa
    // por entradas nem por locomocao; no fim da animacao volta a Idle.
    if (gState.action == GoronAction::Jump || gState.action == GoronAction::Fall) {
        EnterLand(player);
        return true;
    }
    if (gState.action == GoronAction::Land) {
        ActionLand(player, animationFinished);
        return true;
    }

    // A no chao entra na bola. Vem antes da locomocao porque e uma acao com
    // dono: a partir daqui a forma dirige o movimento, nao so a pose.
    if (TryStartRoll(player, play)) {
        return true;
    }
    if (TryStartShield(player, play)) {
        return true;
    }
    if (TryStartPunch(player, play)) {
        return true;
    }

    UpdateLocomotion(player);
    return true;
}

} // namespace ShipLua::MmForm
