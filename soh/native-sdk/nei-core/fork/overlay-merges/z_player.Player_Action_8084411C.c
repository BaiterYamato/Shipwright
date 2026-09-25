/* overlay-merge afb0677e0516 ad7fd4df1e64 57c77b5f4f19 */
void Player_Action_8084411C(Player* this, PlayState* play) {
    f32 sp4C;
    s16 sp4A;

    if (gSaveContext.respawn[RESPAWN_MODE_TOP].data > 40) {
        this->actor.gravity = 0.0f;
    } else if (Player_CheckHostileLockOn(this)) {
        this->actor.gravity = -1.2f;
    }

    Player_GetMovementSpeedAndYaw(this, &sp4C, &sp4A, SPEED_MODE_LINEAR, play);

    if (!(this->actor.bgCheckFlags & BGCHECKFLAG_GROUND)) {
        if (this->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) {
            Actor* heldActor = this->heldActor;

            u16 buttonsToCheck = BTN_A | BTN_B | BTN_CLEFT | BTN_CRIGHT | BTN_CDOWN;
            if (CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0) != 0) {
                buttonsToCheck |= BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT;
            }
            if (!func_80835644(play, this, heldActor) && (heldActor->id == ACTOR_EN_NIW) &&
                CHECK_BTN_ANY(sControlInput->press.button, buttonsToCheck)) {
                func_8084409C(play, this, this->linearVelocity + 2.0f, this->actor.velocity.y + 2.0f);
            }
        }

        LinkAnimation_Update(play, &this->skelAnime);

        if (!(this->stateFlags2 & PLAYER_STATE2_HOPPING)) {
            func_8083DFE0(this, &sp4C, &sp4A);
        }

        Player_UpdateUpperBody(this, play);

        if (((this->stateFlags2 & PLAYER_STATE2_HOPPING) && (this->av1.actionVar1 == 2)) ||
            !func_8083BBA0(this, play)) {
            if (this->actor.velocity.y < 0.0f) {
                if (this->av2.actionVar2 >= 0) {
                    if ((this->actor.bgCheckFlags & BGCHECKFLAG_WALL) || (this->av2.actionVar2 == 0) ||
                        (this->fallDistance > 0)) {
                        if ((sYDistToFloor > 800.0f) || (this->stateFlags1 & PLAYER_STATE1_HOOKSHOT_FALLING)) {
                            func_80843E14(this, NA_SE_VO_LI_FALL_S);
                            this->stateFlags1 &= ~PLAYER_STATE1_HOOKSHOT_FALLING;
                        }

                        LinkAnimation_Change(play, &this->skelAnime, &gPlayerAnim_link_normal_landing, 1.0f, 0.0f, 0.0f,
                                             ANIMMODE_ONCE, 8.0f);
                        this->av2.actionVar2 = -1;
                    }
                } else {
                    if ((this->av2.actionVar2 == -1) && (this->fallDistance > 120.0f) && (sYDistToFloor > 280.0f)) {
                        this->av2.actionVar2 = -2;
                        func_80843E14(this, NA_SE_VO_LI_FALL_L);
                    }

                    // Transformation masks: Goron cannot grab ledges (MM z_player.c:6209)
                    // Other forms (Zora, Deku, FD) CAN grab ledges; Deku limited by unk_14=49
                    // Zora form: allow ledge grab from water surface despite IN_WATER flag.
                    // Buoyancy keeps Zora at yDistToWater~44.8 (equilibrium), which is above
                    // unk_24=36 threshold that would naturally clear IN_WATER. Without this
                    // bypass, Zora can never grab ledges to climb out of water — vanilla Link
                    // doesn't have this issue because his swim equilibrium is also 44.8 but he
                    // exits via shallow-water walk. MM's Zora exits via this jump-grab path.
                    s32 inWaterBlocksGrab = (this->stateFlags1 & PLAYER_STATE1_IN_WATER) ? 1 : 0;
                    if (inWaterBlocksGrab && TransformMasks_IsTransformed() &&
                        MmForm_GetCurrentForm() == 2 /* MM_PLAYER_FORM_ZORA */ && this->actor.yDistToWater < 50.0f) {
                        inWaterBlocksGrab = 0;
                    }
                    if (!GameInteractor_GetDisableLedgeGrabsActive() && !ShipLua_ShouldBlockLedgeGrabs() &&
                        GameInteractor_Should(VB_PLAYER_GRAB_LEDGE, true, this) &&
                        (this->actor.bgCheckFlags & BGCHECKFLAG_PLAYER_WALL_INTERACT) &&
                        !(this->stateFlags2 & PLAYER_STATE2_HOPPING) &&
                        !(this->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) && !inWaterBlocksGrab &&
                        (this->linearVelocity > 0.0f)) {
                        if ((this->yDistToLedge >= 150.0f) &&
                            (this->controlStickDirections[this->controlStickDataIndex] == 0)) {
                            func_8083EC18(this, play, sTouchedWallFlags);
                        } else if ((this->ledgeClimbType >= 2) && (this->yDistToLedge < 150.0f) &&
                                   (((this->actor.world.pos.y - this->actor.floorHeight) + this->yDistToLedge) >
                                    (70.0f * this->ageProperties->unk_08))) {
                            AnimationContext_DisableQueue(play);
                            if (this->stateFlags1 & PLAYER_STATE1_HOOKSHOT_FALLING) {
                                Player_PlayVoiceSfx(this, NA_SE_VO_LI_HOOKSHOT_HANG);
                            } else {
                                Player_PlayVoiceSfx(this, NA_SE_VO_LI_HANG);
                            }
                            this->actor.world.pos.y += this->yDistToLedge;
                            func_8083A5C4(play, this, this->actor.wallPoly, this->distToInteractWall,
                                          GET_PLAYER_ANIM(PLAYER_ANIMGROUP_jump_climb_hold, this->modelAnimType));
                            this->actor.shape.rot.y = this->yaw += 0x8000;
                            this->stateFlags1 |= PLAYER_STATE1_HANGING_OFF_LEDGE;
                        }
                    }
                }
            }
        }
    } else {
        LinkAnimationHeader* anim = GET_PLAYER_ANIM(PLAYER_ANIMGROUP_landing, this->modelAnimType);
        s32 sp3C;

        if (this->stateFlags2 & PLAYER_STATE2_HOPPING) {
            if (Player_CheckHostileLockOn(this)) {
                anim = D_80853D4C[this->av1.actionVar1][2];
            } else {
                anim = D_80853D4C[this->av1.actionVar1][1];
            }
        } else if (this->skelAnime.animation == &gPlayerAnim_link_normal_run_jump) {
            anim = &gPlayerAnim_link_normal_run_jump_end;
        } else if (Player_CheckHostileLockOn(this)) {
            anim = &gPlayerAnim_link_anchor_landingR;
            func_80833C3C(this);
        } else if (this->fallDistance <= 80) {
            anim = GET_PLAYER_ANIM(PLAYER_ANIMGROUP_short_landing, this->modelAnimType);
        } else if ((this->fallDistance < 800) && (this->controlStickDirections[this->controlStickDataIndex] == 0) &&
                   !(this->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR)) {
            Player_SetupRoll(this, play);
            return;
        }

        sp3C = func_80843E64(play, this);

        if (sp3C > 0) {
            func_8083A098(this, GET_PLAYER_ANIM(PLAYER_ANIMGROUP_landing, this->modelAnimType), play);
            this->skelAnime.endFrame = 8.0f;
            if (sp3C == 1) {
                this->av2.actionVar2 = 10;
            } else {
                this->av2.actionVar2 = 20;
            }
        } else if (sp3C == 0) {
            func_8083A098(this, anim, play);
        }
    }

    // Run this after upper-body/item upkeep. Starting it before that update makes
    // ranged items enter and leave the aiming state in the same frame.
    if ((this->actionFunc == Player_Action_8084411C) && !(this->actor.bgCheckFlags & BGCHECKFLAG_GROUND) &&
        GameInteractor_Should(VB_PLAYER_ALLOW_MIDAIR_AIM, false, this)) {
        Player_ActionHandler_13(this, play);
    }
}