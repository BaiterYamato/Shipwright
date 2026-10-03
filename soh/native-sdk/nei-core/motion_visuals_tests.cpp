#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstddef>

using s32 = std::int32_t;
using u8 = std::uint8_t;
using f32 = float;
struct Vec3f { f32 x{}, y{}, z{}; };
struct LinkAnimationHeader {};
struct SkelAnime { LinkAnimationHeader* animation{}; };
struct Actor { unsigned bgCheckFlags{}; Vec3f velocity{}, worldPos{};
    struct { Vec3f pos{}; } world; f32 yDistToWater{}, floorHeight{}; };
struct Player { Actor actor{}; unsigned stateFlags1{}; SkelAnime skelAnime{}; };
struct PlayState {};
struct ItemInputState { bool wasEquipped{}, isPressed{}; };
constexpr unsigned BGCHECKFLAG_GROUND = 1, BGCHECKFLAG_WATER = 1 << 5;
constexpr unsigned BGCHECKFLAG_WATER_TOUCH = 1 << 6;
constexpr unsigned PLAYER_STATE1_IN_WATER = 1 << 27;
constexpr int ITEM_ROCS_CAPE = 0x200;
constexpr float ROCSCAPE_WATER_JUMP_VELOCITY = 5.5f, ROCSCAPE_JUMP_VELOCITY = 11,
                ROCSCAPE_DOUBLE_JUMP_VELOCITY = 11;
constexpr int ROCSCAPE_SOUND_JUMP_ADULT = 0, ROCSCAPE_SOUND_JUMP_CHILD = 1,
              ROCSCAPE_SOUND_DOUBLE_ADULT = 2, ROCSCAPE_SOUND_DOUBLE_CHILD = 3;
constexpr int MM_ANIM_LINK_FIGHTER_BACKTURN_JUMP = 1,
              MM_ANIM_LINK_NORMAL_NEWROLL_JUMP_20F = 2;
#define ROCS_MM_ANIM_CVAR "mm"
static int rcJumpCount, rcMmAnimTimer;
static ItemInputState nextInput;
static bool blocked, winter, mmAvailable, mmEnabled;
static int plays, sparkles, shockwaves;
static char gPlayerAnim_link_fighter_backturn_jump[2];
static LinkAnimationHeader mmAnimation;
static void ItemInput_Update(ItemInputState* in, int, Player*, PlayState*) { *in = nextInput; }
static bool ItemInput_IsBlockedEx(Player*, PlayState*, int) { return blocked; }
static int CVarGetInteger(const char* name, int) { return name[0] == 'm' && mmEnabled; }
static bool MmAnim_IsAvailable() { return mmAvailable; }
static LinkAnimationHeader* MmAnim_Load(int) { return &mmAnimation; }
static bool MmForm_RitoAirRocsAllowed(Player*) { return false; }
static void LinkAnimation_PlayOnce(PlayState*, SkelAnime* skel, LinkAnimationHeader* animation) {
    skel->animation = animation; ++plays;
}
static void ItemVoice_Play(Player*, int, int) {}
static void FX_SpawnSparkles(Player*, PlayState*) { ++sparkles; }
static void FX_SpawnShockwaveSmall(PlayState*, Vec3f*, int, int) { ++shockwaves; }
static u8 Seasons_WalksOnWater() { return winter; }
#include "fork/motion_visuals.h"
#include "rocscape_under_test.inc"

static int failures;
#define CHECK(value) do { if (!(value)) { std::printf("line %d: %s\n", __LINE__, #value); ++failures; } } while (0)

int main() {
    Player p;
    PlayState play;
    nextInput = { true, true };
    p.actor.bgCheckFlags = BGCHECKFLAG_GROUND;
    Handle_RocsCape(&p, &play);
    CHECK(p.actor.velocity.y == 11 && rcJumpCount == 0 && plays == 0);
    p.actor.bgCheckFlags = 0;
    Handle_RocsCape(&p, &play);
    CHECK(rcJumpCount == 1 && rcMmAnimTimer == -2 && shockwaves == 1);
    nextInput.isPressed = false;
    Handle_RocsCape(&p, &play);
    CHECK(plays == 0);
    Handle_RocsCape(&p, &play);
    CHECK(plays == 1 && p.skelAnime.animation == reinterpret_cast<LinkAnimationHeader*>(gPlayerAnim_link_fighter_backturn_jump));
    nextInput.isPressed = true;
    Handle_RocsCape(&p, &play);
    CHECK(plays == 1 && shockwaves == 1); // terceiro salto recusado
    p.actor.bgCheckFlags = BGCHECKFLAG_GROUND;
    nextInput.isPressed = false;
    Handle_RocsCape(&p, &play);
    CHECK(rcJumpCount == 0 && rcMmAnimTimer == 0);
    p.actor.bgCheckFlags = 0;
    nextInput.isPressed = true;
    Handle_RocsCape(&p, &play);
    nextInput.wasEquipped = false;
    Handle_RocsCape(&p, &play);
    CHECK(rcMmAnimTimer == 0);
    nextInput = { true, false };
    Handle_RocsCape(&p, &play);
    CHECK(plays == 1); // animação pendente não reaparece ao reequipar
    p.actor.bgCheckFlags = BGCHECKFLAG_GROUND;
    Handle_RocsCape(&p, &play);
    p.actor.bgCheckFlags = 0;
    mmAvailable = mmEnabled = true;
    nextInput.isPressed = true;
    Handle_RocsCape(&p, &play);
    nextInput.isPressed = false;
    Handle_RocsCape(&p, &play);
    Handle_RocsCape(&p, &play);
    CHECK(plays == 2 && p.skelAnime.animation == &mmAnimation);
    p.stateFlags1 = PLAYER_STATE1_IN_WATER;
    nextInput.isPressed = true;
    Handle_RocsCape(&p, &play);
    CHECK(p.actor.velocity.y == 5.5f); // primeiro salto na água preservado

    p = {};
    winter = true;
    p.actor.bgCheckFlags = BGCHECKFLAG_WATER;
    CHECK(NeiVisual_OnFrozenWater(&p)); // primeiro contato, sem pin anterior
    p.actor.bgCheckFlags = BGCHECKFLAG_WATER_TOUCH;
    CHECK(NeiVisual_OnFrozenWater(&p)); // entrada cria efeito antes de ligar WATER
    p.actor.bgCheckFlags = BGCHECKFLAG_GROUND;
    CHECK(NeiVisual_OnFrozenWater(&p)); // pin já retirou a flag WATER
    Vec3f ripple{};
    CHECK(NeiVisual_SuppressPlayerRipple(&p, &ripple));
    ripple.x = 100;
    CHECK(!NeiVisual_SuppressPlayerRipple(&p, &ripple)); // outro ator distante
    ripple = {0, 50, 0};
    CHECK(!NeiVisual_SuppressPlayerRipple(&p, &ripple)); // outra altura
    ripple = {};
    CHECK(!NeiVisual_SuppressPlayerRipple(&p, nullptr));
    p.actor.yDistToWater = -100;
    CHECK(!NeiVisual_OnFrozenWater(&p)); // no ar acima da superfície
    p.actor.yDistToWater = 0;
    p.stateFlags1 = PLAYER_STATE1_IN_WATER;
    CHECK(!NeiVisual_OnFrozenWater(&p)); // nadando sob o gelo
    CHECK(!NeiVisual_SuppressPlayerRipple(&p, &ripple));
    p.stateFlags1 = 0;
    p.actor.yDistToWater = 30;
    CHECK(!NeiVisual_OnFrozenWater(&p)); // não encobrir água profunda
    p.actor.yDistToWater = 0;
    winter = false;
    CHECK(!NeiVisual_OnFrozenWater(&p)); // água normal conserva efeitos
    CHECK(!NeiVisual_SuppressPlayerRipple(&p, &ripple));
    CHECK(!NeiVisual_OnFrozenWater(nullptr));
    std::puts(failures ? "motion visuals: FAILED" : "motion visuals: ok");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
