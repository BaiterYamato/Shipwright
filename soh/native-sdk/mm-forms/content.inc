// Conteúdo retirado do bootstrap; a DLL recebe recursos e controle da F2.
struct FormsSpec {
    std::map<std::string, std::string> modelsPrefixed;
    std::map<std::string, std::string> animsPrefixed;
    bool waterVoid = true;
} gCustomBodySpec;
SkelAnime gCustomBodySkelAnime{}; // Só espelho do frame; o host possui os buffers.
std::string gCustomBodyCurrentAnim;
std::optional<std::string> gCustomBodyOverrideAnim;
bool gCustomBodyOverrideOnce = false;
bool gCustomBodyPunchHitActive = false;
enum class CustomBodyWaterVoidPhase : uint8_t { None, Curl, Ball, Triggered };
CustomBodyWaterVoidPhase gCustomBodyWaterVoidPhase = CustomBodyWaterVoidPhase::None;
bool gCustomBodyWaterVoidOwnsInputDisable = false;
int gCustomBodyWaterVoidTimer = 0;
bool CustomBodyRollSpikesForDisplay() { return ShipLua::MmForm::RollSpikesActive(); }
void CustomBodyClearRollHitbox(Player* player, PlayState* play) {
    ShipLua::MmForm::ReleaseBody(player);
}
bool CustomBodyStartAnimation(PlayState*, const std::string& name, f32 speed, bool once, bool reverse = false) {
    return StartAnimation(name.c_str(), speed, once, reverse);
}
void CustomBodyWaterVoidReset(Player* player) {
    if (player != nullptr && gCustomBodyWaterVoidOwnsInputDisable) {
        player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
    }
    gCustomBodyWaterVoidPhase = CustomBodyWaterVoidPhase::None;
    gCustomBodyWaterVoidOwnsInputDisable = false;
    gCustomBodyWaterVoidTimer = 0;
}

void CustomBodyUpdateWaterVoid(Player* player, PlayState* play) {
    if (!gCustomBodySpec.waterVoid || player == nullptr || play == nullptr) {
        return;
    }
    // O estado precisa continuar valido por CONDICAO a cada frame, e nao ficar
    // travado por ter sido ligado uma vez — e assim que o Player do MM funciona.
    // Sem esta invalidacao, Curl/Ball sobrevivem ao jogador sair da agua e
    // Triggered nunca volta depois do void-out. Qualquer uma das duas deixa o
    // corpo preso: `waterBall` em CustomBodyActorDraw liga a display list
    // enrolada SEM checar actionFunc, entao o Goron vira bola permanente mesmo
    // andando, sacando a ocarina ou defendendo com R.
    if (gCustomBodyWaterVoidPhase != CustomBodyWaterVoidPhase::None && player->actor.yDistToWater <= 20.0f) {
        gCustomBodyWaterVoidPhase = CustomBodyWaterVoidPhase::None;
        gCustomBodyWaterVoidTimer = 0;
        if (gCustomBodyWaterVoidOwnsInputDisable) {
            player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
            gCustomBodyWaterVoidOwnsInputDisable = false;
        }
        return;
    }
    if (gCustomBodyWaterVoidPhase == CustomBodyWaterVoidPhase::Triggered) {
        return;
    }
    if (gCustomBodyWaterVoidPhase == CustomBodyWaterVoidPhase::None) {
        if (player->actor.yDistToWater <= 20.0f || (player->stateFlags1 & PLAYER_STATE1_GETTING_ITEM) != 0 ||
            player->getItemId != GI_NONE) {
            return;
        }
        gCustomBodyWaterVoidPhase = CustomBodyWaterVoidPhase::Curl;
        gCustomBodyWaterVoidOwnsInputDisable = true;
        gCustomBodyWaterVoidTimer = 0;
        player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
        player->linearVelocity = 0.0f;
        player->actor.velocity.y = -2.0f;
        player->actor.gravity = -0.5f;
        CustomBodyClearRollHitbox(player, play);
        CustomBodyStartAnimation(play, "roll_enter", 0.67f, true);
        Player_PlaySfx(&player->actor, NA_SE_EV_DIVE_INTO_WATER);
        Player_PlaySfx(&player->actor, NA_SE_PL_BODY_HIT);
        return;
    }

    player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
    player->linearVelocity = 0.0f;
    if (gCustomBodyWaterVoidPhase == CustomBodyWaterVoidPhase::Curl) {
        player->actor.velocity.y = -2.0f;
        player->actor.gravity = -0.5f;
        const float last = AnimationLastFrame("roll_enter");
        if (last <= 0 || gCustomBodySkelAnime.curFrame >= last - 0.5f) {
            gCustomBodyWaterVoidPhase = CustomBodyWaterVoidPhase::Ball;
            gCustomBodyOverrideAnim.reset();
            gCustomBodyOverrideOnce = false;
            player->actor.gravity = -2.0f;
        }
        return;
    }

    // A bola não gira: ela só afunda por aproximadamente 15 frames antes da
    // transição. Isso replica a separação de fases do Player de MM.
    player->actor.velocity.y = -2.0f;
    player->actor.gravity = -2.0f;
    if (++gCustomBodyWaterVoidTimer >= 15) {
        gCustomBodyWaterVoidPhase = CustomBodyWaterVoidPhase::Triggered;
        Player_PlaySfx(&player->actor, NA_SE_OC_ABYSS);
        Play_TriggerVoidOut(play);
    }
}

constexpr int kMaskSetmaskFrames = 66;
constexpr int kMaskHensinFrames = 51;
constexpr int kMaskHensinStartFrame = kMaskSetmaskFrames;
constexpr int kMaskTransitionPoseFrames = kMaskHensinStartFrame + kMaskHensinFrames;
constexpr int kMaskTransitionFlashBuildFrames = 6;
constexpr int kMaskTransitionPeakFrame = kMaskTransitionPoseFrames + kMaskTransitionFlashBuildFrames;
constexpr int kMaskTransitionRevealFrames = 13;
constexpr int kMaskTransitionTotalFrames = kMaskTransitionPeakFrame + kMaskTransitionRevealFrames;

// As rampas de ambiente e luz eram numeros absolutos calibrados para uma pose
// de 84 frames. Derivar do fim da pose impede que deslizem toda vez que a
// duracao mudar — foi exatamente esse tipo de constante solta que fez o
// desenho da mascara e o relogio da cena se separarem.
constexpr int kMaskEnvRampStart = 16;
constexpr int kMaskEnvRampSurge = (kMaskTransitionPoseFrames * 59) / 84;
constexpr int kMaskEnvRampPeak = (kMaskTransitionPoseFrames * 63) / 84;
constexpr int kMaskPointRampPeak = (kMaskTransitionPoseFrames * 64) / 84;
enum class MaskTransitionPhase : uint8_t { None, Conceal, Reveal };
MaskTransitionPhase gMaskTransitionPhase = MaskTransitionPhase::None;
int gMaskTransitionFramesRemaining = 0;
int gMaskTransitionRevealTimeout = 0;
int gMaskTransitionElapsed = 0;
s16 gMaskTransitionFlashAlpha = 0;
bool gMaskTransitionOwnsInputDisable = false;
bool gMaskTransitionOwnsCutscene = false;
bool gMaskTransitionOwnsPointLight = false;
bool gMaskTransitionEnvSaved = false;
bool gMaskTransitionRemoving = false;
bool gMaskTransitionMaskHandDrawLogged = false;
bool gMaskTransitionMaskDrawLogged = false;
bool gMaskTransitionScreamDrawLogged = false;
EnvLightSettings gMaskTransitionSavedEnv{};
struct MaskTransitionSavedAdjustments {
    s16 ambient[3];
    s16 light1[3];
    s16 fog[3];
    s16 fogNear;
};
MaskTransitionSavedAdjustments gMaskTransitionSavedAdjustments{};

constexpr char kMaskOnAnimationPath[] = "__OTR__mm/objects/gameplay_keep/gPlayerAnim_cl_setmask";
constexpr char kMaskOnEndAnimationPath[] = "__OTR__mm/objects/gameplay_keep/gPlayerAnim_cl_setmaskend";
// A contorcao da transformacao e o loop que a sustenta. Ver a nota de linha de
// tempo junto de `kMaskHensinFrames`.
constexpr char kMaskHensinAnimationPath[] = "__OTR__mm/objects/gameplay_keep/gPlayerAnim_al_hensin";
constexpr char kMaskHensinLoopAnimationPath[] = "__OTR__mm/objects/gameplay_keep/gPlayerAnim_al_hensin_loop";
constexpr char kMmGoronMaskDListPath[] = "__OTR__mm/objects/gameplay_keep/gGoronMaskDL";
// A MASCARA GRITANDO. Nao e a mesma malha da mascara normal: vive num objeto
// proprio (`object_mask_goron`) e tem a boca escancarada.
//
// Fonte primaria, 2S2H `mm/src/code/z_player_lib.c:4049-4056`: a partir do
// frame 51 de `cl_setmask` — ou durante `cl_setmaskend` — o MM soma 4 ao indice
// da tabela `D_801C0B20` e desenha OUTRA display list. Na tabela (`:2911`), o
// indice de PLAYER_MASK_GORON e `gGoronMaskDL` e o mesmo indice mais quatro e
// `object_mask_goron_DL_0014A0`.
//
// Era esta a troca que faltava: a cena desenhava `gGoronMaskDL` do inicio ao
// fim, entao a mascara nunca abria a boca por mais bem cronometrado que o resto
// estivesse.
constexpr char kMmGoronMaskScreamDListPath[] = "__OTR__mm/objects/object_mask_goron/object_mask_goron_DL_0014A0";
// Frame de `cl_setmask` em que a troca acontece (z_player_lib.c:4050).
constexpr int kMaskScreamStartFrame = 51;

struct MaskTransitionLightStage {
    s16 fogNear;
    u8 fog[3];
    u8 ambient[3];
};

constexpr std::array<MaskTransitionLightStage, 3> kMaskTransitionLightStages = { {
    { 650, { 0, 0, 0 }, { 10, 0, 30 } },
    { 300, { 200, 200, 255 }, { 0, 0, 0 } },
    { 600, { 0, 0, 0 }, { 0, 0, 200 } },
} };

struct MaskTransitionPointStage {
    u8 color[3];
    s16 radius;
};

// Perfil humano de D_8085D848. A entrada Goron comeca em Link humano; o
// perfil nao-humano de raio 100 tornava a coloracao azul quase invisivel.
constexpr std::array<MaskTransitionPointStage, 3> kMaskTransitionPointStages = { {
    { { 120, 200, 255 }, 1000 },
    { { 255, 255, 255 }, 5000 },
    { { 200, 200, 255 }, 5000 },
} };
bool gPointLightActive = false;
LightInfo gPointLightInfo{};
LightNode* gPointLightNode = nullptr;
f32 gPointLightOffY = 0.0f;
u8 gPointLightColor[3] = { 255, 255, 255 };
s16 gPointLightRadius = 0;

void PointLightDetach(PlayState* play) {
    if (gPointLightNode != nullptr && play != nullptr) {
        LightContext_RemoveLight(play, &play->lightCtx, gPointLightNode);
    }
    gPointLightNode = nullptr;
    gPointLightActive = false;
}

uint64_t cameraToken = 0;
bool gCutsceneActive = false;
int gCutsceneElapsed = 0;
int gCutsceneFrames = kMaskTransitionTotalFrames;
Vec3f gMaskCutsceneOriginalAt{}, gMaskCutsceneOriginalEye{};
float gMaskCutsceneOriginalFov = 60, gMaskCutsceneDistance = 40, gMaskCutsceneFov = 80;
float gMaskCutsceneYaw = 0, gMaskCutsceneRollSign = 1;
void CutsceneStop(PlayState*) {
    if (cameraToken) camera->release(cameraToken);
    cameraToken = 0;
    gCutsceneActive = false;
}
void MaskTransitionClearForcedMask(Player* player) {
    if (player && transitionMaskOwned) player->currentMask = previousMask;
    transitionMaskOwned = false;
}
ShipOotCameraViewV2 originalCamera{};
void PublishCamera(const Camera& cam) {
    ShipOotCameraViewV2 view{};
    view.size = sizeof(view);
    std::memcpy(view.at, &cam.at, sizeof(view.at));
    std::memcpy(view.eye, &cam.eye, sizeof(view.eye));
    view.fov = cam.fov;
    view.roll = cam.roll;
    camera->set_view_ex(cameraToken, &view);
}
void CutsceneUpdate(PlayState* play) {
    if (!gCutsceneActive || !play) return;
    Player* player = GET_PLAYER(play);
    if (!player || !camera->is_owned(cameraToken) || gCutsceneElapsed >= gCutsceneFrames) {
        CutsceneStop(play);
        return;
    }
    ShipOotCameraViewV2 view{};
    view.size = sizeof(view);
    if (camera->get_view_ex(&view) != SHIP_NATIVE_OK) return;
    Camera storage{};
    Camera* cam = &storage;
    std::memcpy(&cam->at, view.at, sizeof(view.at));
    std::memcpy(&cam->eye, view.eye, sizeof(view.eye));
    cam->fov = view.fov;
    cam->roll = view.roll;
    constexpr double kPi = 3.14159265358979323846;
        // Camera_Demo4 do MM usa o foco do ator, ligeiramente rebaixado em
        // direção à cintura. O deslocamento lateral e o roll começam no frame
        // 12, estabilizam no 38 e então a câmera mergulha 45 graus.
        Vec3f focus = player->actor.focus.pos;
        focus.y -= (focus.y - player->actor.world.pos.y) * 0.1f;
        const int frame = gCutsceneElapsed;
        f32 pitch = 0.0f;

        if (frame < 38) {
            f32 wave = 0.0f;
            if (frame >= 12) {
                const f32 waveDegrees = static_cast<f32>(frame - 12) * (135.0f / 13.0f);
                wave = std::sin(waveDegrees * static_cast<f32>(kPi / 180.0)) * gMaskCutsceneRollSign;
            }
            const f32 sway = static_cast<f32>(frame) * (6.0f / 19.0f) * wave;
            const f32 playerYaw = static_cast<f32>(player->actor.shape.rot.y) * static_cast<f32>(kPi / 32768.0);
            focus.x += std::sin(playerYaw + static_cast<f32>(kPi * 0.5)) * sway;
            focus.z += std::cos(playerYaw + static_cast<f32>(kPi * 0.5)) * sway;
            cam->at.x += (focus.x - cam->at.x) * 0.2f;
            cam->at.y += (focus.y - cam->at.y) * 0.2f;
            cam->at.z += (focus.z - cam->at.z) * 0.1f;
            const f32 distanceStep = 1.0f / static_cast<f32>(std::max(1, 38 - frame));
            gMaskCutsceneDistance += (30.0f - gMaskCutsceneDistance) * distanceStep;
            const f32 rollDegrees = static_cast<f32>(frame) * (30.0f / 19.0f) * wave;
            cam->roll = static_cast<s16>(std::lround(rollDegrees * (32768.0f / 180.0f)));
        } else if (frame < 62) {
            const int settleFrame = frame - 38;
            const f32 playerYaw = static_cast<f32>(player->actor.shape.rot.y) * static_cast<f32>(kPi / 32768.0);
            focus.x = player->actor.world.pos.x - std::sin(playerYaw) * 7.0f;
            focus.z = player->actor.world.pos.z - std::cos(playerYaw) * 7.0f;
            if (settleFrame == 0) {
                cam->at = focus;
            } else {
                cam->at.x += (focus.x - cam->at.x) * 0.25f;
                cam->at.y += (focus.y - cam->at.y) * 0.25f;
                cam->at.z += (focus.z - cam->at.z) * 0.1f;
            }
            gMaskCutsceneFov += (32.0f - gMaskCutsceneFov) / static_cast<f32>(std::max(1, 24 - settleFrame));
            gMaskCutsceneDistance = 35.0f;
            pitch = static_cast<f32>(kPi * 0.25);
            cam->roll = static_cast<s16>(std::lround(static_cast<f32>(cam->roll) * 0.9f));
        } else if (frame < kMaskTransitionPeakFrame) {
            const int faceFrame = frame - 62;
            cam->at.x += (focus.x - cam->at.x) * 0.25f;
            cam->at.y += (focus.y - cam->at.y) * 0.25f;
            cam->at.z += (focus.z - cam->at.z) * 0.1f;
            // No MM a soma 1..35 divide por 630 e leva o FOV de 32 a 60.
            gMaskCutsceneFov += ((60.0f - 32.0f) / 630.0f) * static_cast<f32>(faceFrame + 1);
            gMaskCutsceneDistance = 35.0f;
            pitch = static_cast<f32>(kPi * 0.25);
            cam->roll = static_cast<s16>(std::lround(static_cast<f32>(cam->roll) * 0.9f));
        } else {
            const f32 reveal = std::clamp(static_cast<f32>(frame - kMaskTransitionPeakFrame + 1) /
                                              static_cast<f32>(kMaskTransitionRevealFrames),
                                          0.0f, 1.0f);
            cam->at.x += (gMaskCutsceneOriginalAt.x - cam->at.x) * reveal;
            cam->at.y += (gMaskCutsceneOriginalAt.y - cam->at.y) * reveal;
            cam->at.z += (gMaskCutsceneOriginalAt.z - cam->at.z) * reveal;
            cam->eye.x += (gMaskCutsceneOriginalEye.x - cam->eye.x) * reveal;
            cam->eye.y += (gMaskCutsceneOriginalEye.y - cam->eye.y) * reveal;
            cam->eye.z += (gMaskCutsceneOriginalEye.z - cam->eye.z) * reveal;
            cam->eyeNext = cam->eye;
            cam->fov += (gMaskCutsceneOriginalFov - cam->fov) * reveal;
            cam->roll = static_cast<s16>(std::lround(static_cast<f32>(cam->roll) * (1.0f - reveal)));
            player->actor.shape.rot.y = static_cast<s16>(std::atan2(cam->at.x-cam->eye.x, cam->at.z-cam->eye.z) * (32768.0 / kPi)) + 0x8000;
            PublishCamera(*cam);
            ++gCutsceneElapsed;
            return;
        }

        const f32 horizontal = std::cos(pitch) * gMaskCutsceneDistance;
        cam->eye.x = cam->at.x + std::sin(gMaskCutsceneYaw) * horizontal;
        cam->eye.y = cam->at.y + std::sin(pitch) * gMaskCutsceneDistance;
        cam->eye.z = cam->at.z + std::cos(gMaskCutsceneYaw) * horizontal;
        cam->eyeNext = cam->eye;
        cam->fov = gMaskCutsceneFov;
        player->actor.shape.rot.y = static_cast<s16>(std::atan2(cam->at.x-cam->eye.x, cam->at.z-cam->eye.z) * (32768.0 / kPi)) + 0x8000;
        PublishCamera(*cam);
        ++gCutsceneElapsed;
        return;
 }

bool MaskTransitionStartCloseup(PlayState* play) {
    originalCamera = {};
    originalCamera.size = sizeof(originalCamera);
    ShipOotCameraLeaseV2 lease{};
    lease.size = sizeof(lease);
    if (camera->get_view_ex(&originalCamera) != SHIP_NATIVE_OK ||
        camera->acquire_ex(kOwner, &cameraToken, &lease) != SHIP_NATIVE_OK) return false;
    std::memcpy(&gMaskCutsceneOriginalAt, originalCamera.at, sizeof(originalCamera.at));
    std::memcpy(&gMaskCutsceneOriginalEye, originalCamera.eye, sizeof(originalCamera.eye));
    gMaskCutsceneOriginalFov = originalCamera.fov;
    const float dx = originalCamera.eye[0]-originalCamera.at[0];
    const float dy = originalCamera.eye[1]-originalCamera.at[1];
    const float dz = originalCamera.eye[2]-originalCamera.at[2];
    gMaskCutsceneDistance = std::clamp(std::sqrt(dx*dx+dy*dy+dz*dz), 1.0f, 40.0f);
    gMaskCutsceneYaw = std::atan2(dx,dz);
    gMaskCutsceneFov = 80.0f;
    gMaskCutsceneRollSign = (play->gameplayFrames & 1) ? -1.0f : 1.0f;
    gCutsceneFrames = kMaskTransitionTotalFrames;
    gCutsceneElapsed = 0;
    gCutsceneActive = true;
    CutsceneUpdate(play);
    return true;
}
u8 MaskTransitionLerpColor(u8 from, u8 to, float t) {
    return static_cast<u8>(std::clamp(static_cast<int>(std::lround(from + (to - from) * t)), 0, 255));
}

void MaskTransitionApplyEnvironment(PlayState* play, float phase, bool revealing) {
    if (play == nullptr || !gMaskTransitionEnvSaved) {
        return;
    }

    const float clamped = std::clamp(phase, 0.0f, 3.0f);
    const MaskTransitionLightStage* from = nullptr;
    const MaskTransitionLightStage* to = nullptr;
    float t = 0.0f;
    if (revealing) {
        from = &kMaskTransitionLightStages[2];
        t = clamped / 3.0f;
    } else if (clamped <= 1.0f) {
        to = &kMaskTransitionLightStages[0];
        t = clamped;
    } else if (clamped <= 2.0f) {
        from = &kMaskTransitionLightStages[0];
        to = &kMaskTransitionLightStages[1];
        t = clamped - 1.0f;
    } else {
        from = &kMaskTransitionLightStages[1];
        to = &kMaskTransitionLightStages[2];
        t = clamped - 2.0f;
    }

    EnvLightSettings& base = play->envCtx.lightSettings;
    if (revealing) {
        const s16 targetFogNear = static_cast<s16>(std::lround(base.fogNear + (from->fogNear - base.fogNear) * t));
        play->envCtx.adjFogNear = targetFogNear - base.fogNear;
        for (int i = 0; i < 3; ++i) {
            const u8 targetFog = MaskTransitionLerpColor(base.fogColor[i], from->fog[i], t);
            const u8 targetAmbient = MaskTransitionLerpColor(base.ambientColor[i], from->ambient[i], t);
            const u8 targetLight1 = MaskTransitionLerpColor(base.light1Color[i], 0, t);
            play->envCtx.adjFogColor[i] = static_cast<s16>(targetFog) - base.fogColor[i];
            play->envCtx.adjAmbientColor[i] = static_cast<s16>(targetAmbient) - base.ambientColor[i];
            play->envCtx.adjLight1Color[i] = static_cast<s16>(targetLight1) - base.light1Color[i];
        }
        return;
    }

    const s16 fromFogNear = from != nullptr ? from->fogNear : base.fogNear;
    const s16 toFogNear = to != nullptr ? to->fogNear : base.fogNear;
    const s16 targetFogNear = static_cast<s16>(std::lround(fromFogNear + (toFogNear - fromFogNear) * t));
    play->envCtx.adjFogNear = targetFogNear - base.fogNear;
    for (int i = 0; i < 3; ++i) {
        const u8 fromFog = from != nullptr ? from->fog[i] : base.fogColor[i];
        const u8 toFog = to != nullptr ? to->fog[i] : base.fogColor[i];
        const u8 fromAmbient = from != nullptr ? from->ambient[i] : base.ambientColor[i];
        const u8 toAmbient = to != nullptr ? to->ambient[i] : base.ambientColor[i];
        const u8 targetFog = MaskTransitionLerpColor(fromFog, toFog, t);
        const u8 targetAmbient = MaskTransitionLerpColor(fromAmbient, toAmbient, t);
        const u8 targetLight1 = MaskTransitionLerpColor(base.light1Color[i], 0, std::min(clamped, 1.0f));
        play->envCtx.adjFogColor[i] = static_cast<s16>(targetFog) - base.fogColor[i];
        play->envCtx.adjAmbientColor[i] = static_cast<s16>(targetAmbient) - base.ambientColor[i];
        play->envCtx.adjLight1Color[i] = static_cast<s16>(targetLight1) - base.light1Color[i];
    }
}

void MaskTransitionApplyPointLight(PlayState* play, Player* player, float phase, bool revealing) {
    if (!gMaskTransitionOwnsPointLight || play == nullptr || player == nullptr) {
        return;
    }

    const float clamped = std::clamp(phase, 0.0f, 3.0f);
    int stageIndex = 0;
    float radiusScale = clamped;
    if (clamped > 2.0f) {
        stageIndex = 2;
        radiusScale = clamped - 2.0f;
    } else if (clamped > 1.0f) {
        stageIndex = 1;
        radiusScale = clamped - 1.0f;
    }
    if (revealing) {
        stageIndex = 2;
        radiusScale = clamped / 3.0f;
    }

    const auto& stage = kMaskTransitionPointStages[stageIndex];
    gPointLightColor[0] = stage.color[0];
    gPointLightColor[1] = stage.color[1];
    gPointLightColor[2] = stage.color[2];
    gPointLightOffY = 28.0f;
    gPointLightRadius = static_cast<s16>(std::lround(stage.radius * std::clamp(radiusScale, 0.0f, 1.0f)));
    Lights_PointNoGlowSetInfo(&gPointLightInfo, static_cast<s16>(player->actor.world.pos.x),
                              static_cast<s16>(player->actor.world.pos.y + gPointLightOffY),
                              static_cast<s16>(player->actor.world.pos.z), gPointLightColor[0], gPointLightColor[1],
                              gPointLightColor[2], gPointLightRadius);
    if (!gPointLightActive && gPointLightRadius > 0) {
        gPointLightNode = LightContext_InsertLight(play, &play->lightCtx, &gPointLightInfo);
        gPointLightActive = gPointLightNode != nullptr;
    }
}

bool MaskTransitionShouldDraw(Player* player) {
    return player != nullptr && gMaskTransitionPhase == MaskTransitionPhase::Conceal && !gMaskTransitionRemoving &&
           player->currentMask == PLAYER_MASK_GORON;
}

int MaskTransitionDrawFrame(Player* player) {
    if (player == nullptr) {
        return gMaskTransitionElapsed;
    }
    const bool playingSetmask = reinterpret_cast<const void*>(player->skelAnime.animation) ==
                                reinterpret_cast<const void*>(kMaskOnAnimationPath);
    if (!playingSetmask) {
        static bool reportedDesync = false;
        if (!reportedDesync && gMaskTransitionElapsed > 0 && gMaskTransitionElapsed < kMaskSetmaskFrames) {
            reportedDesync = true;
            SPDLOG_WARN("ShipLua mascara: cl_setmask nao esta no ar no frame {} da cena; o desenho da mascara "
                        "voltou ao contador do host",
                        gMaskTransitionElapsed);
        }
        return gMaskTransitionElapsed;
    }
    return static_cast<int>(player->skelAnime.curFrame);
}

void DrawMaskTransitionHandImpl(PlayState* play, Player* player) {
    const int drawFrame = MaskTransitionDrawFrame(player);
    if (play == nullptr || !MaskTransitionShouldDraw(player) || drawFrame < 8 || drawFrame >= 12) {
        return;
    }

    // func_80128640 do MM: entre os frames 8 e 12 de cl_setmask, a máscara
    // ainda está na mão esquerda. Estes offsets são os valores originais do
    // draw, aplicados enquanto a matriz de PLAYER_LIMB_L_HAND está viva.
    Matrix_Push();
    Matrix_Translate(-323.67f, 412.15f, -969.96f, MTXMODE_APPLY);
    Matrix_RotateZYX(-0x32BE, -0x50DE, -0x7717, MTXMODE_APPLY);
    EmitScaledDisplayList(play, kMmGoronMaskDListPath, 1.0f, 1.0f, 1.0f);
    Matrix_Pop();
    if (!gMaskTransitionMaskHandDrawLogged) {
        gMaskTransitionMaskHandDrawLogged = true;
        SPDLOG_INFO("ShipLua mascara: gGoronMaskDL desenhada na mao esquerda (frame {} da animacao)", drawFrame);
    }
}

void DrawMaskTransitionHeadImpl(PlayState* play, Player* player) {
    // A mascara passa da mao para o rosto no frame 12 da animacao. Depois que
    // `cl_setmask` termina (66) a cena segura `cl_setmaskend`, e ai o relogio do
    // host volta a mandar — por isso o max com o contador: uma vez no rosto, a
    // mascara nao pode sumir so porque a animacao de vestir acabou.
    const int animFrame = MaskTransitionDrawFrame(player);
    const int drawFrame =
        gMaskTransitionElapsed >= kMaskSetmaskFrames ? std::max(animFrame, gMaskTransitionElapsed) : animFrame;
    if (play == nullptr || !MaskTransitionShouldDraw(player) || drawFrame < 12) {
        return;
    }

    // No rosto, Player_DrawTransformationMask do MM achata Z durante o
    // sofrimento e estica Y. Aqui a matriz ainda é a do PLAYER_LIMB_HEAD.
    float flattenZ = 0.0f;
    if (drawFrame >= 14 && drawFrame < 64) {
        flattenZ = 0.3f;
    } else if (drawFrame >= 64) {
        flattenZ = std::max(0.0f, 0.3f - (drawFrame - 63) * 0.015f);
    }
    float stretchY = 0.0f;
    if (drawFrame >= 16 && drawFrame <= 65) {
        stretchY = 0.1f;
    } else if (drawFrame > 65) {
        stretchY = std::max(0.0f, 0.1f - (drawFrame - 65) * 0.02f);
    }

    // A troca para a mascara de boca aberta (ver kMmGoronMaskScreamDListPath).
    const bool screaming = drawFrame >= kMaskScreamStartFrame;
    const char* maskPath = screaming ? kMmGoronMaskScreamDListPath : kMmGoronMaskDListPath;
    EmitScaledDisplayList(play, maskPath, 1.0f, 1.0f + stretchY, 1.0f - flattenZ);

    if (!gMaskTransitionMaskDrawLogged) {
        gMaskTransitionMaskDrawLogged = true;
        SPDLOG_INFO("ShipLua mascara: gGoronMaskDL desenhada no membro HEAD (frame {} da animacao, cena em {})",
                    drawFrame, gMaskTransitionElapsed);
    }
    if (screaming && !gMaskTransitionScreamDrawLogged) {
        gMaskTransitionScreamDrawLogged = true;
        SPDLOG_INFO("ShipLua mascara: troca para object_mask_goron_DL_0014A0 (boca aberta) no frame {} da animacao",
                    drawFrame);
    }
}

void MaskTransitionReset(Player* player) {
    PlayState* play = gPlayState;
    const bool wasActive = gMaskTransitionPhase != MaskTransitionPhase::None;
    if (player != nullptr && gMaskTransitionOwnsInputDisable) {
        player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
    }
    // DEVOLVER A ANIMACAO. A cena deixa `al_hensin_loop` pendurada na skelAnime
    // do Player e, enquanto a forma Goron vive, a action func fica pausada e
    // ninguem a substitui. Ao tirar a mascara o Link humano voltava com a
    // contorcao ainda tocando — e como `Player_Action_Idle` so troca de animacao
    // quando ela TERMINA (`animDone`, z_player.c:8358), um LOOP nunca terminava
    // e o rosto ficava travado de dor para sempre.
    //
    // Restaurar aqui cobre as tres saidas de uma vez: fim normal, cancelamento e
    // troca de cena.
    if (wasActive && player != nullptr && play != nullptr) {
        if (action) movement->restore_player_animation(action);
    }
    if (gMaskTransitionOwnsCutscene) {
        CutsceneStop(play);
    }
    if (gMaskTransitionOwnsPointLight) {
        PointLightDetach(play);
    }
    if (gMaskTransitionEnvSaved && play != nullptr) {
        for (int i = 0; i < 3; ++i) {
            play->envCtx.adjAmbientColor[i] = gMaskTransitionSavedAdjustments.ambient[i];
            play->envCtx.adjLight1Color[i] = gMaskTransitionSavedAdjustments.light1[i];
            play->envCtx.adjFogColor[i] = gMaskTransitionSavedAdjustments.fog[i];
        }
        play->envCtx.adjFogNear = gMaskTransitionSavedAdjustments.fogNear;
    }
    gMaskTransitionPhase = MaskTransitionPhase::None;
    gMaskTransitionFramesRemaining = 0;
    gMaskTransitionRevealTimeout = 0;
    gMaskTransitionElapsed = 0;
    gMaskTransitionFlashAlpha = 0;
    gMaskTransitionOwnsInputDisable = false;
    gMaskTransitionOwnsCutscene = false;
    gMaskTransitionOwnsPointLight = false;
    gMaskTransitionEnvSaved = false;
    gMaskTransitionRemoving = false;
    gMaskTransitionMaskHandDrawLogged = false;
    gMaskTransitionMaskDrawLogged = false;
    gMaskTransitionScreamDrawLogged = false;
    if (wasActive) {
        SPDLOG_INFO("ShipLua mascara: camera, input, luz e ambiente restaurados");
    }
}

int MaskTransitionStart(Player* player, PlayState* play) {
    MaskTransitionReset(player);
    gMaskTransitionSavedEnv = play->envCtx.lightSettings;
    for (int i = 0; i < 3; ++i) {
        gMaskTransitionSavedAdjustments.ambient[i] = play->envCtx.adjAmbientColor[i];
        gMaskTransitionSavedAdjustments.light1[i] = play->envCtx.adjLight1Color[i];
        gMaskTransitionSavedAdjustments.fog[i] = play->envCtx.adjFogColor[i];
    }
    gMaskTransitionSavedAdjustments.fogNear = play->envCtx.adjFogNear;
    gMaskTransitionEnvSaved = true;
    gMaskTransitionPhase = MaskTransitionPhase::Conceal;
    gMaskTransitionFramesRemaining = kMaskTransitionPeakFrame;
    gMaskTransitionElapsed = 0;
    gMaskTransitionOwnsInputDisable = true;
    gMaskTransitionOwnsPointLight = !gPointLightActive;
    gMaskTransitionOwnsCutscene = MaskTransitionStartCloseup(play);
    player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
    player->linearVelocity = 0.0f;
    player->actor.velocity.y = 0.0f;
    SPDLOG_INFO("ShipLua mascara: cutscene MM iniciada; troca no frame {} (camera={})", kMaskTransitionPeakFrame,
                gMaskTransitionOwnsCutscene ? "close-up" : "indisponivel");
    return kMaskTransitionPeakFrame;
}

void MaskTransitionStartRemoval(Player* player, int animationFrames) {
    MaskTransitionReset(player);
    gMaskTransitionPhase = MaskTransitionPhase::Conceal;
    gMaskTransitionFramesRemaining = std::max(animationFrames, 1);
    gMaskTransitionElapsed = 0;
    gMaskTransitionOwnsInputDisable = true;
    gMaskTransitionRemoving = true;
    player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
    player->linearVelocity = 0.0f;
    player->actor.velocity.y = 0.0f;
    SPDLOG_INFO("ShipLua mascara: retirada iniciada; troca em {} frames", gMaskTransitionFramesRemaining);
}

void MaskTransitionBeginReveal(Player* player) {
    if (gMaskTransitionPhase != MaskTransitionPhase::Conceal) {
        return;
    }
    // A timer Lua troca o corpo no fim da animacao. Fixar alpha no pico neste
    // ponto garante que nenhum frame mostre Link e Goron simultaneamente.
    gMaskTransitionPhase = MaskTransitionPhase::Reveal;
    gMaskTransitionFramesRemaining = 0;
    gMaskTransitionRevealTimeout = 0;
    gMaskTransitionFlashAlpha = 255;
    player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
    SPDLOG_INFO("ShipLua mascara: corpo trocado sob o pico branco; iniciando reveal");
}

void MaskTransitionUpdate(Player* player) {
    if (gMaskTransitionPhase == MaskTransitionPhase::None) {
        return;
    }

    PlayState* play = gPlayState;
    if (play == nullptr || gSaveContext.gameMode != GAMEMODE_NORMAL ||
        (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_WATER)) != 0) {
        MaskTransitionClearForcedMask(player);
        MaskTransitionReset(player);
        return;
    }

    player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
    player->linearVelocity = 0.0f;
    player->actor.velocity.y = 0.0f;

    // A CENA ASSUME A ANIMACAO DO PLAYER.
    //
    // `Player_Action_Idle` faz duas coisas que arruinavam a cutscene: e ela quem
    // chama `LinkAnimation_Update` (z_player.c:8349) e e ela quem TROCA a
    // animacao por uma pose de espera ou um fidget assim que a atual termina
    // (`Player_ChooseNextIdleAnim`, :8370). Com a cena prendendo o Link parado, o
    // seletor de idle roubava `cl_setmask` no meio — o aviso de dessincronizacao
    // pegou isso no frame 45 — e dai vinham o flicker e a volta repentina para a
    // pose padrao.
    //
    // Pausar sozinho nao resolve: pausar a action func congelaria a animacao,
    // porque e ela que avanca o frame. Entao a cena faz as duas pontas — pausa e
    // avanca por conta propria.
    //
    // Este hook roda em z_player.c:12335, DEPOIS do clear do flag em :12193, e
    // por isso o pause vale para o frame seguinte. E o mesmo padrao de
    // `CustomBodyHoldPlayerAction`.
    if (play != nullptr) {
        LinkAnimation_Update(play, &player->skelAnime);
    }

    if (gMaskTransitionPhase == MaskTransitionPhase::Conceal) {
        if (gMaskTransitionFramesRemaining > 0) {
            --gMaskTransitionFramesRemaining;
            ++gMaskTransitionElapsed;
        }

        if (gMaskTransitionRemoving) {
            if (gMaskTransitionElapsed == 8) {
                audio->play_sfx_id(0x1856); // NA_SE_IT_SET_TRANSFORM_MASK
            }
            if (gMaskTransitionFramesRemaining <= kMaskTransitionFlashBuildFrames) {
                const int builtFrames = kMaskTransitionFlashBuildFrames - gMaskTransitionFramesRemaining + 1;
                gMaskTransitionFlashAlpha =
                    static_cast<s16>(std::clamp(builtFrames * 255 / kMaskTransitionFlashBuildFrames, 0, 255));
            }
            if (gMaskTransitionFramesRemaining == 0 && ++gMaskTransitionRevealTimeout >= 15) {
                MaskTransitionReset(player);
            }
            return;
        }

        // D_8085D8F0 / Player_Action_86. Os IDs usam as amostras reais do
        // soundfont do MM, sem substituicao por efeitos do OoT.
        switch (gMaskTransitionElapsed) {
            case 2:
                audio->play_sfx_id(0x0877); // NA_SE_PL_PUT_OUT_ITEM
                break;
            case 4:
                audio->play_sfx_id(0x1856); // NA_SE_IT_SET_TRANSFORM_MASK
                break;
            case 11:
                audio->play_sfx_id(0x0874); // NA_SE_PL_FREEZE_S
                break;
            case 20:
                audio->play_sfx_id(0x1858); // NA_SE_IT_TRANSFORM_MASK_BROKEN
                break;
            case 30:
                audio->play_sfx_id(0x09AA); // NA_SE_PL_TRANSFORM_VOICE
                break;
            case kMaskEnvRampSurge:
                // Unico SFX que nao vem de `D_8085D8F0`: e o raio que a cena
                // dispara junto da virada da rampa azul, entao acompanha ela.
                audio->play_sfx_id(0x2912); // NA_SE_EV_LIGHTNING_HARD
                break;
            case kMaskTransitionPoseFrames + 1:
                audio->play_sfx_id(0x484F); // NA_SE_SY_TRANSFORM_MASK_FLASH
                break;
            default:
                break;
        }

        // Fim do gesto de vestir -> comeca a CONTORCAO. Antes daqui a cena
        // trocava para `cl_setmaskend` (tres frames) e a segurava em loop, o
        // que le como pose parada. `al_hensin` e a transformacao de verdade.
        if (gMaskTransitionElapsed == kMaskHensinStartFrame) {
            if (auto* hensin = ResourceMgr_LoadAnimByName(kMaskHensinAnimationPath); hensin != nullptr) {
                const f32 lastFrame = Animation_GetLastFrame(reinterpret_cast<LinkAnimationHeader*>(hensin));
                LinkAnimation_Change(
                    play, &player->skelAnime,
                    reinterpret_cast<LinkAnimationHeader*>(const_cast<char*>(kMaskHensinAnimationPath)), 1.0f, 0.0f,
                    lastFrame, ANIMMODE_ONCE, 0.0f);
                SPDLOG_INFO("ShipLua mascara: contorcao al_hensin iniciada no frame {} ({} frames)",
                            gMaskTransitionElapsed, kMaskHensinFrames);
            } else {
                // Sem saida silenciosa: se a contorcao nao carregar, a cena
                // continua com a pose curta antiga, mas o log diz o motivo.
                SPDLOG_WARN("ShipLua mascara: al_hensin nao carregou; caindo para cl_setmaskend (pose curta)");
                if (auto* endAnim = ResourceMgr_LoadAnimByName(kMaskOnEndAnimationPath); endAnim != nullptr) {
                    const f32 lastFrame = Animation_GetLastFrame(reinterpret_cast<LinkAnimationHeader*>(endAnim));
                    LinkAnimation_Change(
                        play, &player->skelAnime,
                        reinterpret_cast<LinkAnimationHeader*>(const_cast<char*>(kMaskOnEndAnimationPath)), 1.0f, 0.0f,
                        lastFrame, ANIMMODE_LOOP, 0.0f);
                }
            }
        }

        // A contorcao acaba junto com a pose. Se o flash atrasar (timeout de
        // reveal, mod lento), `al_hensin_loop` segura o sofrimento em vez de
        // deixar o Link congelado no ultimo frame.
        //
        // ANIMMODE_ONCE, nao LOOP: em loop ela nunca termina, e uma animacao que
        // nunca termina e uma animacao que `Player_Action_Idle` nunca substitui.
        // Era assim que o rosto contorcido sobrevivia ate depois de tirar a
        // mascara. Os 48 frames dela cobrem de sobra os seis que faltam ate o
        // pico do flash.
        if (gMaskTransitionElapsed == kMaskTransitionPoseFrames) {
            if (auto* loop = ResourceMgr_LoadAnimByName(kMaskHensinLoopAnimationPath); loop != nullptr) {
                const f32 lastFrame = Animation_GetLastFrame(reinterpret_cast<LinkAnimationHeader*>(loop));
                LinkAnimation_Change(
                    play, &player->skelAnime,
                    reinterpret_cast<LinkAnimationHeader*>(const_cast<char*>(kMaskHensinLoopAnimationPath)), 1.0f, 0.0f,
                    lastFrame, ANIMMODE_ONCE, 0.0f);
            }
        }

        float envPhase = 0.0f;
        float pointPhase = 0.0f;
        if (gMaskTransitionElapsed >= kMaskEnvRampStart) {
            if (gMaskTransitionElapsed < kMaskEnvRampSurge) {
                envPhase = std::min(1.0f, (gMaskTransitionElapsed - (kMaskEnvRampStart - 1)) * 0.1f);
            } else if (gMaskTransitionElapsed < kMaskEnvRampPeak) {
                envPhase = std::min(2.0f, 1.0f + (gMaskTransitionElapsed - (kMaskEnvRampSurge - 1)) * 0.5f);
            } else {
                envPhase = std::min(3.0f, 2.0f + (gMaskTransitionElapsed - (kMaskEnvRampPeak - 1)) * 0.2f);
            }
            if (gMaskTransitionElapsed < kMaskPointRampPeak) {
                pointPhase = std::min(1.0f, (gMaskTransitionElapsed - (kMaskEnvRampStart - 1)) * 0.2f);
            } else {
                pointPhase = std::min(3.0f, 1.0f + (gMaskTransitionElapsed - (kMaskPointRampPeak - 1)) * 0.55f);
            }
        }
        MaskTransitionApplyEnvironment(play, envPhase, false);
        MaskTransitionApplyPointLight(play, player, pointPhase, false);

        if (gMaskTransitionElapsed > kMaskTransitionPoseFrames) {
            const int builtFrames = gMaskTransitionElapsed - kMaskTransitionPoseFrames;
            gMaskTransitionFlashAlpha =
                static_cast<s16>(std::clamp(builtFrames * 255 / kMaskTransitionFlashBuildFrames, 0, 255));
        }
        if (gMaskTransitionFramesRemaining == 0) {
            // Uma falha no callback Lua nao pode deixar o jogador sem input.
            // O body normalmente e ativado no mesmo frame; esta margem so e
            // usada como escape seguro se o mod for descarregado no meio.
            if (++gMaskTransitionRevealTimeout >= 15) {
                MaskTransitionClearForcedMask(player);
                MaskTransitionReset(player);
            }
        }
        return;
    }

    gMaskTransitionFlashAlpha = std::max<s16>(0, gMaskTransitionFlashAlpha - 20);
    const float revealPhase = 3.0f * static_cast<float>(gMaskTransitionFlashAlpha) / 255.0f;
    MaskTransitionApplyEnvironment(play, revealPhase, true);
    MaskTransitionApplyPointLight(play, player, revealPhase, true);
    if (gMaskTransitionFlashAlpha == 0) {
        MaskTransitionReset(player);
    }
}

extern "C" void CustomBodyDrawRoll(PlayState* play, const char* path, const char* spikesPath, const char* energy1Path,
                                   const char* energy2Path, int charge, bool machineVisuals) {
    Gfx* dList = reinterpret_cast<Gfx*>(const_cast<char*>(path));
    // NAO ha "kick visual" de parede no MM. Conferido na fonte primaria: em
    // `Player_Action_96` (2S2H z_player.c:19932-19944) o quique muda o yaw,
    // soma `unk_B0C` e toca NA_SE_IT_GORON_ROLLING_REFLECTION — nada mais. A
    // deformacao da esfera vem de `func_808577E0`, que e continua e ligada ao
    // giro, nunca ao impacto.
    //
    // O pulso que existia aqui era invencao nossa e deformava a bola a cada
    // batida. Removido: o pedido e ficar como o MM original do 2S2H.
    const f32 rollSquash = machineVisuals ? ShipLua::MmForm::RollSquash() : 0.0f;
    const f32 squash = std::clamp(rollSquash, -0.7f, 0.3f);
    const f32 scaleY = 1.0f + squash;
    const f32 scaleX = 1.0f - (squash * 0.5f);
    const f32 scaleZ = std::max(scaleX, scaleY);
    const f32 binangToRad = static_cast<f32>(M_PI) / 32768.0f;
    const s16 rollDriftYaw = machineVisuals ? ShipLua::MmForm::RollDriftYaw() : 0;
    const s16 rollSfxCounter = machineVisuals ? ShipLua::MmForm::RollSfxCounter() : 0;
    const f32 driftYaw = rollDriftYaw * binangToRad;
    const f32 driftTilt = rollSfxCounter * binangToRad;

    // Tudo abaixo pertence somente a matriz de desenho: nada aqui escreve de
    // volta no Player, no collider ou na action func.
    Matrix_Push();
    if (rollSfxCounter != 0 && rollDriftYaw != 0) {
        Matrix_RotateY(driftYaw, MTXMODE_APPLY);
        Matrix_RotateX(driftTilt, MTXMODE_APPLY);
        Matrix_RotateY(-driftYaw, MTXMODE_APPLY);
    }
    Matrix_Scale(scaleX, scaleY, scaleZ, MTXMODE_APPLY);

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    Gfx* twoTexScroll = Gfx_TwoTexScroll(play->state.gfxCtx, 0, 0, 0, 0x40, 0x40, 1, play->gameplayFrames * 2,
                                         play->gameplayFrames * 2, 0x40, 0x40);
    gSPSegment(POLY_OPA_DISP++, 0x08, reinterpret_cast<uintptr_t>(twoTexScroll));
    const f32 colorLerp = machineVisuals ? std::clamp(ShipLua::MmForm::RollColorLerp(), 0.0f, 1.0f) : 0.0f;
    const u8 envR = static_cast<u8>(255.0f - (175.0f * colorLerp));
    const u8 envG = static_cast<u8>(255.0f - (175.0f * colorLerp));
    const u8 envB = static_cast<u8>(255.0f - (55.0f * colorLerp));
    gDPSetEnvColor(POLY_OPA_DISP++, envR, envG, envB, 255);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    if (ResourceMgr_OTRSigCheck(reinterpret_cast<char*>(dList)) == 1) {
        gsSPPushCD(POLY_OPA_DISP++, dList);
    }
    gSPDisplayList(POLY_OPA_DISP++, dList);
    if (spikesPath != nullptr) {
        Gfx* spikes = reinterpret_cast<Gfx*>(const_cast<char*>(spikesPath));
        if (ResourceMgr_OTRSigCheck(reinterpret_cast<char*>(spikes)) == 1) {
            gsSPPushCD(POLY_OPA_DISP++, spikes);
        }
        gSPDisplayList(POLY_OPA_DISP++, spikes);
    }

    // `grt_01_model` e `grt_02_model`: começam a aparecer no nível 5 de
    // carga, como no Player_Draw de MM. Após os espinhos, a energia fica
    // compacta e opaca em vez de continuar crescendo indefinidamente.
    if (charge >= 5 && energy1Path != nullptr && energy2Path != nullptr) {
        const bool spikesActive = CustomBodyRollSpikesForDisplay();
        const f32 energyScale = spikesActive ? 0.65f : std::min((charge - 4) * 0.02f, 1.0f);
        const u8 alpha = spikesActive ? 255 : static_cast<u8>(std::min(200.0f, energyScale * 200.0f));
        Gfx* energy1 = reinterpret_cast<Gfx*>(const_cast<char*>(energy1Path));
        Gfx* energy2 = reinterpret_cast<Gfx*>(const_cast<char*>(energy2Path));
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gSPSegment(POLY_XLU_DISP++, 0x08, reinterpret_cast<uintptr_t>(twoTexScroll));
        Matrix_Push();
        Matrix_Scale(1.0f, energyScale, energyScale, MTXMODE_APPLY);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
        gDPSetEnvColor(POLY_XLU_DISP++, 155, 0, 0, alpha);
        if (ResourceMgr_OTRSigCheck(reinterpret_cast<char*>(energy1)) == 1) {
            gsSPPushCD(POLY_XLU_DISP++, energy1);
        }
        gSPDisplayList(POLY_XLU_DISP++, energy1);
        gDPSetEnvColor(POLY_XLU_DISP++, (play->gameplayFrames & 1) == 0 ? 100 : 200, 0, 0, 255);
        if (ResourceMgr_OTRSigCheck(reinterpret_cast<char*>(energy2)) == 1) {
            gsSPPushCD(POLY_XLU_DISP++, energy2);
        }
        gSPDisplayList(POLY_XLU_DISP++, energy2);
        Matrix_Pop();
    }
    CLOSE_DISPS(play->state.gfxCtx);
    Matrix_Pop();
}

bool CustomBodyIsGakkiAnimation() {
    return ShipLua::MmForm::GakkiPhase() != 0;
}

struct CustomBodyGakkiKey {
    f32 frame;
    Vec3f scale;
};

// Tabelas de escala do instrumento, do z_player_lib.c de MM. O formato
// original e struct_80124618 { s16 frame; Vec3s scale; } com escala x0.01;
// aqui ja convertida. Sao as mesmas tres que a referencia usa em
// sGakkiGoronStart/Play/Wait — antes so a primeira existia, e por isso o
// conjunto ficava estatico depois de aberto.

// D_801C0428 — gakkistart: os tambores surgem. Reutilizada na saida, com a
// animacao correndo ao contrario.
static const std::array<CustomBodyGakkiKey, 7> kGakkiGoronStart = {
    CustomBodyGakkiKey{ 0.0f, { 0.0f, 0.0f, 0.0f } },  CustomBodyGakkiKey{ 6.0f, { 0.0f, 0.0f, 0.0f } },
    CustomBodyGakkiKey{ 7.0f, { 0.6f, 0.6f, 0.5f } },  CustomBodyGakkiKey{ 8.0f, { 1.2f, 1.3f, 1.0f } },
    CustomBodyGakkiKey{ 9.0f, { 1.0f, 1.2f, 0.8f } },  CustomBodyGakkiKey{ 11.0f, { 1.0f, 1.0f, 1.0f } },
    CustomBodyGakkiKey{ 13.0f, { 1.0f, 1.0f, 1.0f } },
};
// D_801C0490 — gakkiplay: a pulsacao ritmica enquanto a nota soa.
static const std::array<CustomBodyGakkiKey, 16> kGakkiGoronPlay = {
    CustomBodyGakkiKey{ 0.0f, { 1.0f, 1.0f, 1.0f } },   CustomBodyGakkiKey{ 1.0f, { 1.0f, 1.0f, 1.0f } },
    CustomBodyGakkiKey{ 2.0f, { 0.9f, 1.0f, 1.05f } },  CustomBodyGakkiKey{ 4.0f, { 1.1f, 1.0f, 1.0f } },
    CustomBodyGakkiKey{ 5.0f, { 0.9f, 1.0f, 1.05f } },  CustomBodyGakkiKey{ 6.0f, { 1.0f, 1.0f, 1.0f } },
    CustomBodyGakkiKey{ 7.0f, { 0.9f, 1.0f, 1.05f } },  CustomBodyGakkiKey{ 8.0f, { 1.0f, 1.0f, 1.0f } },
    CustomBodyGakkiKey{ 9.0f, { 0.9f, 1.0f, 1.05f } },  CustomBodyGakkiKey{ 10.0f, { 1.0f, 1.0f, 1.0f } },
    CustomBodyGakkiKey{ 11.0f, { 0.9f, 1.0f, 1.05f } }, CustomBodyGakkiKey{ 12.0f, { 1.1f, 1.0f, 1.0f } },
    CustomBodyGakkiKey{ 13.0f, { 1.0f, 1.0f, 1.0f } },  CustomBodyGakkiKey{ 14.0f, { 0.9f, 1.0f, 1.05f } },
    CustomBodyGakkiKey{ 15.0f, { 0.9f, 1.0f, 1.05f } }, CustomBodyGakkiKey{ 17.0f, { 1.0f, 1.0f, 1.0f } },
};
// D_801C0510 — repouso entre notas.
static const std::array<CustomBodyGakkiKey, 5> kGakkiGoronWait = {
    CustomBodyGakkiKey{ 0.0f, { 1.0f, 1.0f, 1.0f } }, CustomBodyGakkiKey{ 4.0f, { 1.0f, 1.0f, 1.0f } },
    CustomBodyGakkiKey{ 5.0f, { 0.9f, 1.1f, 1.0f } }, CustomBodyGakkiKey{ 6.0f, { 1.1f, 1.05f, 1.0f } },
    CustomBodyGakkiKey{ 8.0f, { 1.0f, 1.0f, 1.0f } },
};

Vec3f CustomBodyGakkiInterp(const CustomBodyGakkiKey* keys, size_t count, f32 frame) {
    if (keys == nullptr || count == 0) {
        return { 1.0f, 1.0f, 1.0f };
    }
    for (size_t i = 1; i < count; ++i) {
        if (frame <= keys[i].frame) {
            const auto& previous = keys[i - 1];
            const auto& next = keys[i];
            const f32 span = next.frame - previous.frame;
            const f32 t = span <= 0.0f ? 0.0f : std::clamp((frame - previous.frame) / span, 0.0f, 1.0f);
            return {
                previous.scale.x + (next.scale.x - previous.scale.x) * t,
                previous.scale.y + (next.scale.y - previous.scale.y) * t,
                previous.scale.z + (next.scale.z - previous.scale.z) * t,
            };
        }
    }
    return keys[count - 1].scale;
}

// Manter os tres eixos separados preserva o "pop" assimetrico da abertura e faz
// a saida recolher os tambores no ritmo certo.
Vec3f CustomBodyGakkiScale() {
    const f32 frame = gCustomBodySkelAnime.curFrame;
    switch (ShipLua::MmForm::GakkiPhase()) {
        case 1:
        case 3:
            return CustomBodyGakkiInterp(kGakkiGoronStart.data(), kGakkiGoronStart.size(), frame);
        case 2:
            // Tocando uma nota usa a tabela de pulsacao; entre notas, a de repouso.
            return ShipLua::MmForm::GakkiLastNote() == 0xFF
                       ? CustomBodyGakkiInterp(kGakkiGoronWait.data(), kGakkiGoronWait.size(), frame)
                       : CustomBodyGakkiInterp(kGakkiGoronPlay.data(), kGakkiGoronPlay.size(), frame);
        default:
            return { 1.0f, 1.0f, 1.0f };
    }
}

// O efeito de soco é uma DL translúcida própria de MM. A mesma tabela de
// impacto já usada pelo collider decide quando desenhá-la; A usa mão esquerda,
extern "C" void CustomBodyDrawPunchEffect(PlayState* play, s32 limbIndex) {
    if (!gCustomBodyPunchHitActive) {
        return;
    }
    const bool atTargetLimb = (gCustomBodyCurrentAnim == "punch_a" && limbIndex == PLAYER_LIMB_L_HAND) ||
                              (gCustomBodyCurrentAnim == "punch_b" && limbIndex == PLAYER_LIMB_R_HAND) ||
                              (gCustomBodyCurrentAnim == "punch_c" && limbIndex == PLAYER_LIMB_WAIST);
    const auto effect = gCustomBodySpec.modelsPrefixed.find("punch_effect");
    if (!atTargetLimb || effect == gCustomBodySpec.modelsPrefixed.end()) {
        return;
    }

    Gfx* dList = reinterpret_cast<Gfx*>(const_cast<char*>(effect->second.c_str()));
    // Mesma regra dos tambores: recurso que nao resolve nao vai para o
    // renderizador. Emitir a display list com a assinatura reprovada entrega
    // lixo ao pipeline.
    if (ResourceMgr_OTRSigCheck(reinterpret_cast<char*>(dList)) != 1) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            SPDLOG_WARN("ShipLua corpo: 'punch_effect' nao resolve como recurso; pulando o desenho");
        }
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gDPSetEnvColor(POLY_XLU_DISP++, 255, 0, 0, 180);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gsSPPushCD(POLY_XLU_DISP++, dList);
    gSPDisplayList(POLY_XLU_DISP++, dList);
    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void CustomBodyPostLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* arg) {
    constexpr s32 kGoronTorsoLimb = 0x15;
    CustomBodyDrawPunchEffect(play, limbIndex);
    if (limbIndex != kGoronTorsoLimb || !CustomBodyIsGakkiAnimation()) {
        return;
    }

    static constexpr std::array<const char*, 6> kGakkiModels = {
        "gakki_container", "gakki_piece_1", "gakki_piece_2", "gakki_piece_3", "gakki_piece_4", "gakki_piece_5",
    };
    const Vec3f scale = CustomBodyGakkiScale();
    if (scale.x <= 0.0f && scale.y <= 0.0f && scale.z <= 0.0f) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    for (const char* name : kGakkiModels) {
        const auto model = gCustomBodySpec.modelsPrefixed.find(name);
        if (model == gCustomBodySpec.modelsPrefixed.end()) {
            continue;
        }
        Gfx* dList = reinterpret_cast<Gfx*>(const_cast<char*>(model->second.c_str()));
        // O `gSPDisplayList` era emitido MESMO com a assinatura OTR reprovada —
        // ou seja, com um ponteiro que nao e um recurso valido. Isso entrega
        // lixo ao renderizador; foi o que travou a tela quando a fase do
        // instrumento passou a existir de verdade e estes DLs comecaram a ser
        // desenhados pela primeira vez. Recurso que nao resolve agora e pulado.
        if (ResourceMgr_OTRSigCheck(reinterpret_cast<char*>(dList)) != 1) {
            static std::set<std::string> reported;
            if (reported.insert(model->second).second) {
                SPDLOG_WARN("ShipLua corpo: modelo '{}' nao resolve como recurso; pulando o desenho", name);
            }
            continue;
        }
        Matrix_Push();
        Matrix_Scale(scale.x, scale.y, scale.z, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
        gsSPPushCD(POLY_OPA_DISP++, dList);
        gSPDisplayList(POLY_OPA_DISP++, dList);
        Matrix_Pop();
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

void DrawMaskTransitionFlash(PlayState* play) {
    if (play == nullptr || gMaskTransitionFlashAlpha <= 0) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_44Xlu(play->state.gfxCtx);
    gDPPipeSync(POLY_XLU_DISP++);
    gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE);
    const u8 alpha = static_cast<u8>(gMaskTransitionFlashAlpha);
    const u8 red = static_cast<u8>(180 + (75 * alpha / 255));
    const u8 green = static_cast<u8>(215 + (40 * alpha / 255));
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, red, green, 255, alpha);
    gDPFillRectangle(POLY_XLU_DISP++, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
    gDPPipeSync(POLY_XLU_DISP++);
    CLOSE_DISPS(play->state.gfxCtx);
}
