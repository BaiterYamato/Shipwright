/* overlay-merge a12c2827b712 c6d5eda38eb4 a401b15f0297 */
void Player_UpdateCommon(Player* this, PlayState* play, Input* input) {
    s32 pad;

    sControlInput = input;

    if (this->unk_A86 < 0) {
        this->unk_A86++;
        if (this->unk_A86 == 0) {
            this->unk_A86 = 1;
            Sfx_PlaySfxCentered(NA_SE_OC_REVENGE);
        }
    }

    Math_Vec3f_Copy(&this->actor.prevPos, &this->actor.home.pos);

    if (this->unk_A73 != 0) {
        this->unk_A73--;
    }

    if (this->textboxBtnCooldownTimer != 0) {
        this->textboxBtnCooldownTimer--;
    }

    if (this->unk_A87 != 0) {
        this->unk_A87--;
    }

    if (this->invincibilityTimer < 0) {
        this->invincibilityTimer++;
    } else if (this->invincibilityTimer > 0) {
        this->invincibilityTimer--;
    }

    if (this->unk_890 != 0) {
        this->unk_890--;
    }

    Player_UpdateInterface(play, this);
    Player_UpdateZTargeting(this, play);

    if (this->heldItemAction == PLAYER_IA_DEKU_STICK &&
        GameInteractor_Should(VB_DEKU_STICK_BE_ON_FIRE, this->unk_860 != 0)) {
        Player_UpdateBurningDekuStick(play, this);
    } else if ((this->heldItemAction == PLAYER_IA_FISHING_POLE) && (this->unk_860 < 0)) {
        this->unk_860++;
    }

    if (this->bodyShockTimer != 0) {
        Player_UpdateBodyShock(play, this);
    }

    if (this->bodyIsBurning) {
        Player_UpdateBodyBurn(play, this);
    }

    if ((this->stateFlags3 & PLAYER_STATE3_RESTORE_NAYRUS_LOVE) && (gSaveContext.nayrusLoveTimer != 0) &&
        (gSaveContext.magicState == MAGIC_STATE_IDLE)) {
        gSaveContext.magicState = MAGIC_STATE_METER_FLASH_1;
        Player_SpawnMagicSpell(play, this, 1);
        this->stateFlags3 &= ~PLAYER_STATE3_RESTORE_NAYRUS_LOVE;
    }

    if (this->stateFlags2 & PLAYER_STATE2_PAUSE_MOST_UPDATING) {
        if (!(this->actor.bgCheckFlags & BGCHECKFLAG_GROUND)) {
            Player_ZeroSpeedXZ(this);
            RocBoots_MoveWithGravity(this, Actor_MoveXZGravity); // half gravity while worn
        }

        Player_ProcessSceneCollision(play, this);
    } else {
        f32 temp_f0;
        f32 phi_f12;

        if (this->currentBoots != this->prevBoots) {
            if (this->currentBoots == PLAYER_BOOTS_IRON) {
                if (this->stateFlags1 & PLAYER_STATE1_IN_WATER) {
                    func_80832340(play, this);
                    if (this->ageProperties->unk_2C < this->actor.yDistToWater) {
                        this->stateFlags2 |= PLAYER_STATE2_UNDERWATER;
                    }
                }
            } else {
                if (this->stateFlags1 & PLAYER_STATE1_IN_WATER) {
                    if ((this->prevBoots == PLAYER_BOOTS_IRON) || (this->actor.bgCheckFlags & BGCHECKFLAG_GROUND)) {
                        func_8083D36C(play, this);
                        this->stateFlags2 &= ~PLAYER_STATE2_UNDERWATER;
                    }
                }
            }

            this->prevBoots = this->currentBoots;
        }

        if ((this->actor.parent == NULL) && (this->stateFlags1 & PLAYER_STATE1_ON_HORSE)) {
            this->actor.parent = this->rideActor;
            func_8083A360(play, this);
            this->stateFlags1 |= PLAYER_STATE1_ON_HORSE;
            Player_AnimPlayOnce(play, this, &gPlayerAnim_link_uma_wait_1);
            Player_StartAnimMovement(play, this, 0x9B);
            this->av2.actionVar2 = 99;
        }

        // Gerudo Dual Blades: keep the hold-B charge reachable after her long swings
        // (see GerudoMhr_HoldsChargeWindow).
        // Trident (ext sword 3): same fix for the same reason — its swings are 14-26
        // frames and vanilla's window only survives 7. Skijer's NEI
        if (GerudoMhr_HoldsChargeWindow(this) || Trident_HoldsChargeWindow(this)) {
            this->unk_844 = 3;
        }

        if (this->unk_844 == 0) {
            this->unk_845 = 0;
        } else if (this->unk_844 < 0) {
            this->unk_844++;
        } else {
            this->unk_844--;
        }

        Math_ScaledStepToS(&this->unk_6C2, 0, 400);
        FaceChange_UpdateBlinking(this->unk_3A8, 20, 80, 6);

        this->actor.shape.face = this->unk_3A8[0] + ((play->gameplayFrames & 32) ? 0 : 3);

        if (this->currentMask == PLAYER_MASK_BUNNY) {
            Player_UpdateBunnyEars(this);
        }

        if (func_8002DD6C(this) != 0) {
            func_8084FF7C(this);
        }

        if (!(this->skelAnime.movementFlags & 0x80)) {
            if (((this->actor.bgCheckFlags & BGCHECKFLAG_GROUND) && (sFloorType == 5) &&
                 (this->currentBoots != PLAYER_BOOTS_IRON) && !ClimbBoots_HasGrip()) ||
                ((this->currentBoots == PLAYER_BOOTS_HOVER || GameInteractor_GetSlipperyFloorActive()) &&
                 !(this->stateFlags1 & (PLAYER_STATE1_IN_WATER | PLAYER_STATE1_IN_CUTSCENE)))) {
                f32 sp70 = this->linearVelocity;
                s16 sp6E = this->yaw;
                s16 yawDiff = this->actor.world.rot.y - sp6E;
                s32 pad;

                if ((ABS(yawDiff) > 0x6000) && (this->actor.speedXZ != 0.0f)) {
                    sp70 = 0.0f;
                    sp6E += 0x8000;
                }

                if (Math_StepToF(&this->actor.speedXZ, sp70, 0.35f) && (sp70 == 0.0f)) {
                    this->actor.world.rot.y = this->yaw;
                }

                if (this->linearVelocity != 0.0f) {
                    s32 phi_v0;

                    phi_v0 = (fabsf(this->linearVelocity) * 700.0f) - (fabsf(this->actor.speedXZ) * 100.0f);
                    phi_v0 = CLAMP(phi_v0, 0, 1350);

                    Math_ScaledStepToS(&this->actor.world.rot.y, sp6E, phi_v0);
                }

                if ((this->linearVelocity == 0.0f) && (this->actor.speedXZ != 0.0f)) {
                    func_800F4138(&this->actor.projectedPos, 0xD0, this->actor.speedXZ);
                }
            } else {
                this->actor.speedXZ = this->linearVelocity;
                this->actor.world.rot.y = this->yaw;
            }

            RocBoots_MoveWithGravity(this, Actor_UpdateVelocityXZGravity); // half gravity while worn

            if ((this->pushedSpeed != 0.0f) && !Player_InCsMode(play) &&
                !(this->stateFlags1 &
                  (PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CLIMBING_LEDGE | PLAYER_STATE1_CLIMBING_LADDER)) &&
                (Player_Action_80845668 != this->actionFunc) && (Player_Action_808507F4 != this->actionFunc)) {
                this->actor.velocity.x += this->pushedSpeed * Math_SinS(this->pushedYaw);
                this->actor.velocity.z += this->pushedSpeed * Math_CosS(this->pushedYaw);
            }

            Actor_UpdatePos(&this->actor);
            Player_ProcessSceneCollision(play, this);
        } else {
            if (GameInteractor_Should(VB_SET_STATIC_FLOOR_TYPE, true, this)) {
                sFloorType = 0;
            }
            this->floorProperty = 0;

            if (!(this->stateFlags1 & PLAYER_STATE1_LOADING) && (this->stateFlags1 & PLAYER_STATE1_ON_HORSE)) {
                EnHorse* rideActor = (EnHorse*)this->rideActor;
                CollisionPoly* sp5C;
                s32 sp58;
                Vec3f sp4C;

                if (!(rideActor->actor.bgCheckFlags & 1)) {
                    func_808396F4(play, this, &D_80854814, &sp4C, &sp5C, &sp58);
                } else {
                    sp5C = rideActor->actor.floorPoly;
                    sp58 = rideActor->actor.floorBgId;
                }

                if ((sp5C != NULL) && Player_HandleExitsAndVoids(play, this, sp5C, sp58)) {
                    if (DREG(25) != 0) {
                        DREG(25) = 0;
                    } else {
                        AREG(6) = 1;
                    }
                }
            }

            sConveyorSpeed = 0;
            this->pushedSpeed = 0.0f;
        }

        if ((sConveyorSpeed != 0) && (this->currentBoots != PLAYER_BOOTS_IRON)) {
            f32 sp48;

            sConveyorSpeed--;

            if (sIsFloorConveyor == 0) {
                sp48 = sWaterConveyorSpeeds[sConveyorSpeed];

                if (!(this->stateFlags1 & PLAYER_STATE1_IN_WATER)) {
                    sp48 *= 0.25f;
                }
            } else {
                sp48 = sFloorConveyorSpeeds[sConveyorSpeed];
            }

            Math_StepToF(&this->pushedSpeed, sp48, sp48 * 0.1f);

            Math_ScaledStepToS(&this->pushedYaw, sConveyorYaw,
                               ((this->stateFlags1 & PLAYER_STATE1_IN_WATER) ? 400.0f : 800.0f) * sp48);
        } else if (this->pushedSpeed != 0.0f) {
            Math_StepToF(&this->pushedSpeed, 0.0f, (this->stateFlags1 & PLAYER_STATE1_IN_WATER) ? 0.5f : 1.0f);
        }

        if (!Player_InBlockingCsMode(play, this) && !(this->stateFlags2 & PLAYER_STATE2_CRAWLING)) {
            func_8083D53C(play, this);

            if ((this->actor.category == ACTORCAT_PLAYER) && (gSaveContext.health == 0)) {
                if (this->stateFlags1 &
                    (PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CLIMBING_LEDGE | PLAYER_STATE1_CLIMBING_LADDER)) {
                    func_80832440(play, this);
                    func_80837B9C(this, play);
                } else if ((this->actor.bgCheckFlags & BGCHECKFLAG_GROUND) ||
                           (this->stateFlags1 & PLAYER_STATE1_IN_WATER)) {
                    func_80836448(play, this,
                                  func_808332B8(this)           ? &gPlayerAnim_link_swimer_swim_down
                                  : (this->bodyShockTimer != 0) ? &gPlayerAnim_link_normal_electric_shock_end
                                                                : &gPlayerAnim_link_derth_rebirth);
                }
            } else {
                if ((this->actor.parent == NULL) && ((play->transitionTrigger == TRANS_TRIGGER_START) ||
                                                     (this->unk_A87 != 0) || !func_808382DC(this, play))) {
                    func_8083AA10(this, play);
                } else {
                    this->fallStartHeight = this->actor.world.pos.y;
                }
                Player_DetectRumbleSecrets(this);
            }
        }

        if ((play->csCtx.state != CS_STATE_IDLE) && (this->csAction != 6) &&
            !(this->stateFlags1 & PLAYER_STATE1_ON_HORSE) && !(this->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY) &&
            (this->actor.category == ACTORCAT_PLAYER)) {
            CsCmdActorCue* linkActionCsCmd = play->csCtx.linkAction;

            if ((linkActionCsCmd != NULL) && (sCueToCsActionMap[linkActionCsCmd->action] != 0)) {
                Player_SetCsActionWithHaltedActors(play, NULL, 6);
                Player_ZeroSpeedXZ(this);
            } else if ((this->csAction == 0) && !(this->stateFlags2 & PLAYER_STATE2_UNDERWATER) &&
                       (play->csCtx.state != CS_STATE_UNSKIPPABLE_INIT)) {
                Player_SetCsActionWithHaltedActors(play, NULL, 0x31);
                Player_ZeroSpeedXZ(this);
            }
        }

        if (this->csAction != 0) {
            if ((this->csAction != 7) ||
                !(this->stateFlags1 & (PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CLIMBING_LEDGE |
                                       PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_DAMAGED))) {
                this->unk_6AD = 3;
            } else if (Player_Action_CsAction != this->actionFunc) {
                func_80852944(play, this, NULL);
            }
        } else {
            this->prevCsAction = 0;
        }

        func_8083D6EC(play, this);

        if ((this->focusActor == NULL) && (this->naviTextId == 0)) {
            this->stateFlags2 &= ~(PLAYER_STATE2_CAN_ACCEPT_TALK_OFFER | PLAYER_STATE2_NAVI_ALERT);
        }

        this->stateFlags1 &= ~(PLAYER_STATE1_SWINGING_BOTTLE | PLAYER_STATE1_READY_TO_FIRE |
                               PLAYER_STATE1_CHARGING_SPIN_ATTACK | PLAYER_STATE1_SHIELDING);
        this->stateFlags2 &= ~(PLAYER_STATE2_DO_ACTION_GRAB | PLAYER_STATE2_DO_ACTION_CLIMB | PLAYER_STATE2_FOOTSTEP |
                               PLAYER_STATE2_DISABLE_ROTATION_Z_TARGET | PLAYER_STATE2_DISABLE_ROTATION_ALWAYS |
                               PLAYER_STATE2_GRABBING_DYNAPOLY | PLAYER_STATE2_FORCE_SAND_FLOOR_SOUND |
                               PLAYER_STATE2_STATIONARY_LADDER | PLAYER_STATE2_FROZEN | PLAYER_STATE2_DO_ACTION_ENTER |
                               PLAYER_STATE2_DO_ACTION_DOWN | PLAYER_STATE2_REFLECTION);
        this->stateFlags3 &= ~PLAYER_STATE3_CHECK_FLOOR_WATER_COLLISION;

        func_80847298(this);
        Player_ProcessControlStick(play, this);

        if (this->stateFlags1 & PLAYER_STATE1_IN_WATER) {
            sWaterSpeedFactor = 0.5f;
        } else {
            sWaterSpeedFactor = 1.0f;
        }

        sInvWaterSpeedFactor = 1.0f / sWaterSpeedFactor;
        sUseHeldItem = sHeldItemButtonIsHeldDown = 0;
        sSavedCurrentMask = this->currentMask;

        // Transformation masks: ledge grab while a custom action holds PAUSE, plus the
        // underwater item pickup the Zora swim states could not reach. Lives in
        // mm_player_form.cpp; must be called from HERE because interactRangeActor is
        // cleared below (:13592) long before TransformMasks_Update runs (:13907).
        {
            extern void MmForm_HandleFormInteractions(Player * player, PlayState * play);
            MmForm_HandleFormInteractions(this, play);
        }

        // SM64 Mario: same yield pattern. When paused and the player can
        // interact with something (door, NPC, grabbable actor, ledge),
        // unpause so the vanilla action handler runs the animation. Adds a
        // temporary strength boost so Mario can lift bronze-gauntlet-level
        // objects ("como si tuviera strength 1"). Strength is restored in the
        // same frame so the save file isn't polluted — mirror of the
        // gFormState.savedStrength pattern in mm_player_form.cpp:2363-2381.
        if ((this->stateFlags3 & PLAYER_STATE3_PAUSE_ACTION_FUNC) && Sm64Mario_IsReady()) {
            s16 sm64SavedStrength = CUR_UPG_VALUE(UPG_STRENGTH);
            if (sm64SavedStrength < 1) {
                Inventory_ChangeUpgrade(UPG_STRENGTH, 1);
            }
            // Handler call order matters — doors first (can pre-empt talk on
            // NPC-doors like Mido), then talk, grab, ledge. NOTE: we do NOT
            // inject B→A here anymore. A is Mario's jump; grabbing bombs /
            // bomb flowers / liftable rocks happens via the B-proximity path
            // in sm64_mario.c (Sm64Mario_TryGrabOrThrow), which manages
            // heldActor directly without going through these handlers.
            s32 sm64Handled = Player_ActionHandler_1(this, play) ||    // doors (knob + sliding)
                              Player_ActionHandler_Talk(this, play) || // NPC textbox
                              Player_ActionHandler_2(this, play) ||    // A-triggered pickup / offer grab
                              Player_ActionHandler_12(this, play);     // ledge climb
            if (sm64SavedStrength < 1) {
                Inventory_ChangeUpgrade(UPG_STRENGTH, sm64SavedStrength);
            }
            if (sm64Handled) {
                this->stateFlags3 &= ~PLAYER_STATE3_PAUSE_ACTION_FUNC;
            }
        }

        // A ação passa pelo gancho do host (RFC 0029), que chama a actionFunc quando nenhum mod a substitui.
        LinkSpan_PlayerAction(this, play, input,
                              GameInteractor_Should(VB_EXECUTE_PLAYER_ACTION_FUNC,
                                                    !(this->stateFlags3 & PLAYER_STATE3_PAUSE_ACTION_FUNC), this,
                                                    input));

        Player_UpdateCamAndSeqModes(play, this);

        if (this->skelAnime.movementFlags & 8) {
            AnimationContext_SetMoveActor(play, &this->actor, &this->skelAnime,
                                          (this->skelAnime.movementFlags & 4) ? 1.0f : this->ageProperties->unk_08);
        }

        Player_UpdateShapeYaw(this, play);

        if (CHECK_FLAG_ALL(this->actor.flags, ACTOR_FLAG_TALK)) {
            this->talkActorDistance = 0.0f;
        } else {
            this->talkActor = NULL;
            this->talkActorDistance = FLT_MAX;
            this->exchangeItemId = EXCH_ITEM_NONE;
        }

        if (!(this->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR)) {
            this->interactRangeActor = NULL;
            this->getItemDirection = 0x6000;
        }

        if (this->actor.parent == NULL) {
            this->rideActor = NULL;
        }

        this->naviTextId = 0;

        if (!(this->stateFlags2 & PLAYER_STATE2_PLAY_FOR_ACTOR)) {
            this->unk_6A8 = NULL;
        }

        this->stateFlags2 &= ~PLAYER_STATE2_NEAR_OCARINA_ACTOR;
        this->closestSecretDistSq = FLT_MAX;

        temp_f0 = this->actor.world.pos.y - this->actor.prevPos.y;

        this->doorType = PLAYER_DOORTYPE_NONE;
        this->knockbackType = 0;
        this->autoLockOnActor = NULL;

        phi_f12 =
            ((this->bodyPartsPos[PLAYER_BODYPART_L_FOOT].y + this->bodyPartsPos[PLAYER_BODYPART_R_FOOT].y) * 0.5f) +
            temp_f0;
        temp_f0 += this->bodyPartsPos[PLAYER_BODYPART_HEAD].y + 10.0f;

        this->cylinder.dim.height = LinkSpan_S16F(temp_f0 - phi_f12);

        if (this->cylinder.dim.height < 0) {
            phi_f12 = temp_f0;
            this->cylinder.dim.height = LinkSpan_S16F(-this->cylinder.dim.height);
        }

        this->cylinder.dim.yShift = LinkSpan_S16F(phi_f12 - this->actor.world.pos.y);

        if (this->stateFlags1 & PLAYER_STATE1_SHIELDING) {
            this->cylinder.dim.height = LinkSpan_S16F(this->cylinder.dim.height * 0.8f);
        }

        Collider_UpdateCylinder(&this->actor, &this->cylinder);

        if (!(this->stateFlags2 & PLAYER_STATE2_FROZEN)) {
            if (!(this->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_HANGING_OFF_LEDGE |
                                       PLAYER_STATE1_CLIMBING_LEDGE | PLAYER_STATE1_ON_HORSE))) {
                CollisionCheck_SetOC(play, &play->colChkCtx, &this->cylinder.base);
            }

            if (!(this->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_DAMAGED)) &&
                (this->invincibilityTimer <= 0)) {
                CollisionCheck_SetAC(play, &play->colChkCtx, &this->cylinder.base);

                if (this->invincibilityTimer < 0) {
                    CollisionCheck_SetAT(play, &play->colChkCtx, &this->cylinder.base);
                }
            }
        }

        AnimationContext_SetNextQueue(play);
    }

    Math_Vec3f_Copy(&this->actor.home.pos, &this->actor.world.pos);
    Math_Vec3f_Copy(&this->unk_A88, &this->bodyPartsPos[PLAYER_BODYPART_WAIST]);

    if (this->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_IN_CUTSCENE)) {
        this->actor.colChkInfo.mass = MASS_IMMOVABLE;
    } else {
        this->actor.colChkInfo.mass = 50;
    }

    this->stateFlags3 &= ~PLAYER_STATE3_PAUSE_ACTION_FUNC;

    Collider_ResetCylinderAC(play, &this->cylinder.base);

    // Gerudo Dual Blades: read this frame's blade hits BEFORE the reset below wipes
    // AT_HIT. Her controller runs after this function (TransformMasks_Update), so
    // this is the only place the flags are still live for it.
    GerudoMhr_ScanBladeHits(this);
    KeatonForm_ScanBlock(this);

    Collider_ResetQuadAT(play, &this->meleeWeaponQuads[0].base);
    Collider_ResetQuadAT(play, &this->meleeWeaponQuads[1].base);

    Collider_ResetQuadAC(play, &this->shieldQuad.base);
    Collider_ResetQuadAT(play, &this->shieldQuad.base);
}
