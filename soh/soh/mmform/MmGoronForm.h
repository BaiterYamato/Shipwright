#pragma once

// ---------------------------------------------------------------------------
// Maquina de acoes da forma Goron de Majora's Mask, rodando dentro do OoT.
//
// Portada de skijer/Shipwright @ Not-Enough-Items,
// soh/mods/transformation_masks/mm_player_form.cpp — a unica implementacao de
// referencia que encontramos de uma forma do MM executada pelo motor do OoT.
// Copia das fontes e o porque de cada uma em
// link-span/research/skijer-transformation-masks/.
//
// A logica original ja fala a linguagem do host: o arquivo do skijer usa a
// struct `Player` do OoT em 165 lugares e a do MM em 20. Nao e codigo do MM
// colado verbatim, e por isso o port dispensa `mm_compat.h` e
// `mm_player_struct.h`.
//
// FRONTEIRA DE PROJETO: este modulo nao conhece Lua, nem a spec do mod, nem
// caminho de asset. Ele pede animacao PELO NOME e o host resolve — e a mesma
// fronteira que o resto do Link-Span mantem, e o que permite a forma Zora ou
// Deku reusarem a maquina trocando so os nomes.
// ---------------------------------------------------------------------------

#include <cstdint>

struct Player;
struct PlayState;

namespace ShipLua::MmForm {

// Subconjunto Goron do enum de acoes da referencia (mm_player_form.cpp:660-730).
// As acoes de Zora, Deku, Garo, Gerudo e Pikachu ficaram deliberadamente de
// fora; a maquina e generica mas o escopo desta tarefa e o Goron.
enum class GoronAction : std::uint8_t {
    // Cede ao OoT: o Player esta numa acao propria (item, dialogo, cutscene,
    // escada, ocarina) e a forma nao deve tomar o corpo.
    OotAction = 0,
    Idle,
    Walk,
    Run,
    Jump,
    Fall,
    Land,
    Damage,
    // Combo de socos. PunchEnd e a recuperacao; um B novo durante ela encadeia.
    PunchA,
    PunchB,
    PunchC,
    PunchEnd,
    // A bola de MM. NAO e o Player_Action_Roll do OoT (que e esquiva): e o
    // Player_Action_96, com fisica, carga e espinhos proprios. RollInit toca a
    // curl, Roll mantem a bola, RollJump/RollPound sao o ground pound e
    // RollUncurl desenrola.
    RollInit,
    Roll,
    RollJump,
    RollPound,
    RollUncurl,
    Shield,
    Gakki,
};

// Servicos que o host empresta a maquina. Sao ponteiros de funcao e nao uma
// interface virtual porque este modulo e compilado junto do host e nunca
// atravessa fronteira de ABI.
struct Services {
    // Troca a animacao corrente do corpo. `once` = ANIMMODE_ONCE.
    bool (*startAnimation)(const char* name, float speed, bool once, bool reverse);
    // A spec do mod declarou este nome? A maquina consulta antes de pedir, para
    // degradar em vez de travar quando uma forma nao declara tudo.
    bool (*hasAnimation)(const char* name);
    // Frame corrente da animacao do corpo.
    float (*currentFrame)();
    // Ultimo frame de uma animacao declarada, ou 0 quando o nome nao resolve.
    //
    // O combo de socos precisa comparar `curFrame >= endFrame` como a
    // referencia faz — `animationFinished` sozinho nao serve aqui, porque chega
    // com um frame de atraso (a maquina roda antes do update de animacao) e o
    // encadeamento do combo e sensivel a esse frame.
    float (*animationLastFrame)(const char* name);
    // Troca o instrumento do motor de ocarina. O MM faz isso por forma
    // (`sPlayerFormOcarinaInstruments`, z_message.c:4377); a maquina pede pelo
    // INDICE e nao conhece o motor de audio, mesma fronteira do resto.
    // Pode ser nulo: a forma fica com o timbre padrao, nao quebra.
    void (*setOcarinaInstrument)(std::uint8_t instrumentId);
    // Toca uma amostra do mm.o2r pelo caminho prefixado
    // ("mm/audio/samples/..."). Pode ser nulo: a forma fica muda, nao quebra.
    void (*playMmSample)(const char* prefixedSamplePath);
};

// Instala os servicos. Chamar uma vez, na ativacao do corpo.
void Init(const Services& services);

// Descarta o estado. Chamar na desativacao e na troca de cena.
void Reset();

// A bola da maquina esta no ar? Comeca DESLIGADA, e e proposital.
//
// `Update()` ja roda todo frame no host desde o incremento 2, entao a fisica da
// bola entraria em jogo no instante em que fosse portada — sem que o host
// pausasse a action func do Player, sem trocar o desenho para a esfera e
// concorrendo com o rolamento antigo (o sequestro do `Player_Action_Roll` do
// OoT). Seriam duas bolas disputando o mesmo corpo.
//
// Enquanto estiver desligada a maquina ignora o A e o comportamento em jogo e
// identico ao de hoje. Ligar e o incremento seguinte, e exige de uma vez:
//   1. reafirmar `PLAYER_STATE3_PAUSE_ACTION_FUNC` quando
//      `WantsPlayerActionPaused()` for true — o flag e limpo todo frame;
//   2. alimentar `animationFinished` com o retorno de `LinkAnimation_Update` do
//      frame anterior;
//   3. trocar o desenho por `IsBallBody()` / `RollSpikesActive()`;
//   4. aposentar o caminho antigo (`nativeRolling`, `CustomBodyUpdateGroundPound`,
//      `CustomBodyUpdateRollSpikes`, `CustomBodyUpdateRollHitbox`).
void SetBallEnabled(bool enabled);
bool BallEnabled();

// Neste frame nenhuma acao nova da forma pode tomar o corpo. Nao interrompe uma
// acao em curso — e um gate de ENTRADA, nao uma chave de posse.
//
// A maquina conhece as proprias acoes (bola, soco, defesa e instrumento), mas
// agua/void, transicao de mascara e o fallback legado vivem no host. Um unico
// sinal externo evita manter gates paralelos que poderiam divergir.
void SetEntryBlocked(bool blocked);

// O host acabou de entrar no `Player_Action_Roll` do OoT neste frame.
//
// A referencia nao disputa o A cru: ela converte o roll que o OoT ja aceitou
// em curl Goron. A maquina nao pode referenciar `Player_Action_Roll` porque o
// simbolo pertence ao overlay de z_player, entao recebe somente este fato
// booleano. E um sinal por frame, no mesmo desenho do gate acima.
void SetOotRollActive(bool active);

// O combo de socos da maquina esta no ar? Mesmo desenho da bola: enquanto for
// false, o B continua sendo do caminho antigo do host (`CustomBodyBeginPunch` e
// o dispatcher de combo dentro de `CustomBodyActorUpdate`). Os dois nunca podem
// valer juntos — seriam dois combos armando o mesmo quad.
void SetPunchEnabled(bool enabled);
bool PunchEnabled();

// Alguma fase do combo (os tres golpes ou a recuperacao)?
bool IsPunching();

// A janela de dano do golpe corrente esta aberta neste frame?
bool PunchHitActive();

// A acao corrente move o Link pela TRANSLACAO DE RAIZ da animacao?
//
// Quem aplica o deslocamento continua sendo o host: `SkelAnime_UpdateTranslation`
// precisa do SkelAnime do corpo, que e dele. A maquina so responde se a acao
// corrente e uma das que podem mover — e o perfil fechado que impede uma
// animacao qualquer declarada por Lua de virar uma forma de mover o Player.
bool UsesRootMotion();

// A defesa da maquina esta habilitada? O esqueleto dedicado continua sendo
// asset do host; esta chave apenas declara que ele foi carregado e que o host
// consegue materializar `GoronAction::Shield`. Sem isso R permanece com o OoT.
void SetShieldEnabled(bool enabled);
bool ShieldEnabled();

// R esta sob posse da maquina neste frame? O host usa esta resposta somente
// para reiniciar/avancar o SkelAnime de quatro limbs e atualizar o shieldQuad.
bool IsShielding();

// Devolve ao Player TUDO que a maquina possa ter pendurado nele: o cylinder da
// bola (raio e dano), o quad do soco, a flag de defesa, o input travado, a
// gravidade, a sombra e as travas de rotacao. Volta a acao para Idle.
//
// Os colliders pertencem ao Player e sao reusados pelo engine — largar um
// rolamento ou um soco sem restaurar deixa raio, dano e input grudados em tudo
// depois, e nada mais roda para corrigir. Chamar em toda saida fora do fluxo
// normal: troca de forma, troca de cena, mod desativado no meio da acao, e
// SEMPRE antes de `Reset()`, que descarta o estado sem devolver nada.
//
// Seguro de chamar sempre, inclusive com a maquina parada.
void ReleaseBody(Player* player);

GoronAction CurrentAction();
// Nome estavel da acao, para log e para o payload que o mod observa.
const char* CurrentActionName();

// Nome da animacao em LOOP que a acao corrente quer, ou nullptr quando a acao
// nao dirige a pose (cessao ao OoT, ou acao tocada por one-shot).
//
// A maquina devolve um nome em vez de trocar a animacao sozinha de proposito:
// assim ela entra como FONTE DE FALLBACK do host, sem tomar o override de
// one-shot nem passar por cima do hook `body_anim_select` que os mods usam para
// escada, porta e bau. Ligar a maquina por aqui nao regride nenhum caminho que
// ja funciona.
const char* DesiredAnimation();
std::uint8_t ComboStep();
std::uint8_t GakkiPhase();
std::uint8_t GakkiLastNote();

// A acao corrente toma o MOVIMENTO para si, e nao so a pose?
//
// Quando devolve true o chamador precisa reafirmar
// `PLAYER_STATE3_PAUSE_ACTION_FUNC` neste frame: a bola escreve
// `linearVelocity`, `yaw`, `velocity.y` e `gravity` por conta propria, e a
// action func do Player desfaria tudo no mesmo frame. Na locomocao devolve
// FALSE de proposito — la e justamente a action func do OoT que move o Link.
bool WantsPlayerActionPaused();

// A forma esta em qualquer fase do ciclo da bola — curl, rolamento, ground
// pound ou desenrolar?
bool IsRolling();

// A ESFERA e o corpo neste frame? So as tres fases do meio: nas duas pontas o
// que aparece e o esqueleto tocando a animacao de enrolar/desenrolar. E esta a
// pergunta que decide entre desenhar a display list enrolada e desenhar o
// esqueleto — `IsRolling()` sozinha responderia "bola" durante o curl.
bool IsBallBody();

// Espinhos ativos: carga completa mais magia. Liga a DL de espinhos e a de
// energia, e e o que mantem a bola armada mesmo com o A solto.
bool RollSpikesActive();

// Nivel de carga (`av1` do Player de MM). Comeca em 4 e sobe ate 0x36, quando
// os espinhos ativam. O desenho usa a partir de 5 para a intensidade das duas
// camadas de energia.
std::int16_t RollChargeLevel();

// Estado visual read-only da esfera. A maquina calcula estes valores junto da
// fisica; o host apenas os consome no draw e nunca os devolve ao movimento.
float RollSquash();
float RollColorLerp();
float RollBounce();
std::int16_t RollDriftYaw();
std::int16_t RollSfxCounter();
std::uint8_t RollWallBounceTimer();

// O cylinder do Player esta armado como arma pela bola neste frame?
bool RollAttackActive();

// Alguma das duas fases do ground pound (subida ou baque)?
bool IsGroundPound();

// Fase da bola por nome estavel, para log e para o payload do mod. Devolve
// nullptr fora do rolamento.
const char* RollPhaseName();

// Roda um frame da maquina.
//
// Devolve true quando a forma esta DIRIGINDO A POSE neste frame; false quando
// cedeu ao OoT (`OotAction`).
//
// ATENCAO: isto NAO e permissao para pausar a action func do Player. Na
// locomocao e justamente ela que move o Link — pausa-la o deixa parado no
// lugar. A referencia so pausa nas acoes que tomam o movimento para si (soco,
// defesa, bola); idle/walk/run deixam a action func do OoT rodando 1:1, para
// item, pulo e nado continuarem vanilla. Quem responde por isso e
// `WantsPlayerActionPaused()`, acao por acao.
//
// `animationFinished` e o retorno de `LinkAnimation_Update` do corpo NO FRAME
// ANTERIOR — e o unico sinal de fim de one-shot que a maquina tem, e e o que
// leva o curl a virar bola e o desenrolar a virar idle. Um chamador que passe
// sempre false NAO trava a bola: o curl cai num limite de frames documentado em
// `kRollInitTimeoutFrames`, mas a cadencia deixa de ser a da animacao.
bool Update(Player* player, PlayState* play, bool animationFinished);

} // namespace ShipLua::MmForm
