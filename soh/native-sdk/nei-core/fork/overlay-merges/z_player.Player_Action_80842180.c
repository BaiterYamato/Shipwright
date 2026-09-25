/* overlay-merge b58c6fbaeedf eeb881961dbf 4d10a22832e4 */
void Player_Action_80842180(Player* this, PlayState* play) {
    f32 speedTarget;
    s16 yawTarget;

    this->stateFlags2 |= PLAYER_STATE2_DISABLE_ROTATION_Z_TARGET;
    func_80841EE4(this, play);

    if (!Player_TryActionHandlerList(play, this, sActionHandlerList8, true)) {
        if (Player_IsZTargetingWithHostileUpdate(this)) {
            func_8083C858(this, play);
            return;
        }

        Player_GetMovementSpeedAndYaw(this, &speedTarget, &yawTarget, SPEED_MODE_CURVED, play);

        if (!func_8083C484(this, &speedTarget, &yawTarget)) {
            if (GameInteractor_Should(VB_PLAYER_MODIFY_RUN_SPEED, true, this, &speedTarget, &yawTarget)) {
                if (CVarGetInteger(CVAR_ENHANCEMENT("MMBunnyHood"), BUNNY_HOOD_VANILLA) != BUNNY_HOOD_VANILLA &&
                    this->currentMask == PLAYER_MASK_BUNNY) {
                    speedTarget *= 1.5f;
                }

                if (CVarGetFloat(CVAR_CHEAT("SpeedModifier.Value"), 1.0f) != 1.0f) {
                    if (CVarGetInteger(CVAR_CHEAT("SpeedModifier.SpeedToggle"), 0)) {
                        if (gWalkSpeedToggle) {
                            speedTarget *= CVarGetFloat(CVAR_CHEAT("SpeedModifier.Value"), 1.0f);
                        }
                    } else {
                        const s32 mod1Mask = CVarGetInteger(CVAR_CHEAT("SpeedModifier.Btn"), BTN_CUSTOM_MODIFIER1);

                        if (mod1Mask != 0 && CHECK_BTN_ALL(sControlInput->cur.button, mod1Mask)) {
                            speedTarget *= CVarGetFloat(CVAR_CHEAT("SpeedModifier.Value"), 1.0f);
                        }
                    }
                }

                if (SpiritualStone_KokiriWalkActive()) {
                    speedTarget *= 1.5f;
                }

                func_8083DF68(this, speedTarget, yawTarget);
                func_8083DDC8(this, play);
            };

            if ((this->linearVelocity == 0.0f) && (speedTarget == 0.0f)) {
                func_8083C0B8(this, play);
            }
        }
    }
}