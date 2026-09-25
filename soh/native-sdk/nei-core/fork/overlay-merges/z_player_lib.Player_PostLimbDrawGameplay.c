/* overlay-merge 29ee0b84b096 f8ac85e6787f 2c2690a66ccf */
void Player_PostLimbDrawGameplay(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* thisx) {
    Player* this = (Player*)thisx;

    LinkSpan_PlayerLimbDraw(play, limbIndex, &this->actor);

    if (*dList != NULL) {
        Matrix_MultVec3f(&sZeroVec, D_80160000);
    }

    if (limbIndex == PLAYER_LIMB_L_HAND) {
        MtxF sp14C;
        Actor* hookedActor;

        Math_Vec3f_Copy(&this->leftHandPos, D_80160000);
        ShipLua_DrawMaskTransitionHand(play, this);

        // Boss Remains: draw Odolwa's sword on the hand bone (the native sword was hidden to a
        // closed fist in Player_OverrideLimbDrawGameplayDefault, so *dList != NULL means a hand DL
        // — where a sword would be — is drawing). Self-guards on Odolwa-worn + sword-in-hand; own
        // push/pop + transform. Mirrors the MM 2ship L_HAND post-limb hook.
        if ((*dList != NULL) && (this->actor.scale.y >= 0.0f)) {
            BossRemains_DrawOdolwaSword(play, this);
        }

        if (this->itemAction == PLAYER_IA_DEKU_STICK || this->itemAction == PLAYER_IA_ROD_FIRE ||
            this->itemAction == PLAYER_IA_ROD_ICE || this->itemAction == PLAYER_IA_ROD_LIGHT) {
            Vec3f sp124[3];
            u8 isCustomRod = (this->itemAction == PLAYER_IA_ROD_FIRE || this->itemAction == PLAYER_IA_ROD_ICE ||
                              this->itemAction == PLAYER_IA_ROD_LIGHT);

            OPEN_DISPS(play->state.gfxCtx);

            if (this->actor.scale.y >= 0.0f) {
                D_80126080.x = this->unk_85C * 5000.0f;
                func_80090A28(this, sp124);
                if (this->meleeWeaponState != 0) {
                    EffectBlureShip_ChangeType(Effect_GetByIndex(this->meleeWeaponEffectIndex), TRAIL_TYPE_STICK);
                    func_800906D4(play, this, sp124);
                } else {
                    Math_Vec3f_Copy(&this->meleeWeaponInfo[0].tip, &sp124[0]);
                }
            }

            Matrix_Translate(-428.26f, 267.2f, -33.82f, MTXMODE_APPLY);
            Matrix_RotateZYX(-0x8000, 0, 0x4000, MTXMODE_APPLY);
            Matrix_Scale(1.0f, this->unk_85C, 1.0f, MTXMODE_APPLY);

            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

            if (isCustomRod) {
                // Custom rod - don't draw Deku Stick here
                // Fire Rod is drawn in CustomItems_DrawFireRod following leftHandPos
            } else {
                // Normal Deku Stick
                gSPDisplayList(POLY_OPA_DISP++, gLinkChildLinkDekuStickDL);
            }

            CLOSE_DISPS(play->state.gfxCtx);
        } else if (ExtEquip_ShouldHideSwordDL() && (this->actor.scale.y >= 0.0f)) {
            // Cane of Byrna: draw blue cane using limb matrix (follows hand rotation exactly)
            OPEN_DISPS(play->state.gfxCtx);

            // Melee weapon trail/collision (same as normal sword)
            if (ExtEquip_TridentTrailBegin()) {
                // Trident: the trail and the quads are measured in the LANCE's frame,
                // so they follow the drawn weapon (and its Item Editor placement)
                // instead of the sword that is hidden. The tip is refreshed even
                // between swings so the charge ball can sit on the real lance tip.
                // Skijer's NEI
                Vec3f spE4_trident[3];
                D_80126080.x = ExtEquip_TridentTrailLength();
                if (this->meleeWeaponState != 0) {
                    EffectBlure_ChangeType(Effect_GetByIndex(this->meleeWeaponEffectIndex),
                                           sSwordTypes[Player_GetMeleeWeaponHeld(this)]);
                    func_80090A28(this, spE4_trident);
                    func_800906D4(play, this, spE4_trident);
                } else {
                    // Not func_80090A28 here: it also bumps unk_845 (the combo counter)
                    // as a side effect, which is only right mid-swing.
                    Matrix_MultVec3f(&D_80126080, &this->meleeWeaponInfo[0].tip);
                }
                Matrix_Pop();
            } else if (ExtEquip_ByrnaTrailBegin()) {
                // Same reason as the Trident above: the cane is drawn far from the
                // hidden sword, so the streak and the quads have to be measured in
                // the cane's frame or they trail empty air. Skijer's NEI
                Vec3f spE4_byrnaCane[3];
                D_80126080.x = ExtEquip_ByrnaTrailLength();
                if (this->meleeWeaponState != 0) {
                    EffectBlure_ChangeType(Effect_GetByIndex(this->meleeWeaponEffectIndex),
                                           sSwordTypes[Player_GetMeleeWeaponHeld(this)]);
                    func_80090A28(this, spE4_byrnaCane);
                    func_800906D4(play, this, spE4_byrnaCane);
                } else {
                    // func_80090A28 also bumps unk_845 (the combo counter), which is
                    // only right mid-swing.
                    Matrix_MultVec3f(&D_80126080, &this->meleeWeaponInfo[0].tip);
                }
                Matrix_Pop();
            } else if (this->meleeWeaponState != 0) {
                Vec3f spE4_byrna[3];
                D_80126080.x = sMeleeWeaponLengths[Player_GetMeleeWeaponHeld(this)];

                // Hammer upgrade (Iron Knuckle's Axe): double the hitbox reach
                if (WeaponUpgrade_HasHammerAxe()) {
                    D_80126080.x = 8000.0f; // 2x normal hammer reach (~4000)
                }

                EffectBlure_ChangeType(Effect_GetByIndex(this->meleeWeaponEffectIndex),
                                       sSwordTypes[Player_GetMeleeWeaponHeld(this)]);
                func_80090A28(this, spE4_byrna);
                func_800906D4(play, this, spE4_byrna);
            }

            // Draw Byrna cane model using current limb matrix
            Matrix_Push();
            ExtEquip_ApplySwordDLMatrix();

            Gfx_SetupDL_25Opa(play->state.gfxCtx);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            ExtEquip_DrawSwordDL(play);
            Matrix_Pop();

            CLOSE_DISPS(play->state.gfxCtx);
        } else if ((this->heldItemId == ITEM_NET) && (this->actor.scale.y >= 0.0f)) {
            // Net (Skijer's NEI): wields via the sword IA. Draw the net using THIS limb matrix (the
            // hand BONE) so it follows the hand's full rotation/roll 1:1 like the sword — a
            // forearm->hand reconstruction could not roll. Then run the sword weapon update so the
            // blade-capture works (func_800906D4 catches instead of dealing damage — gated inside).
            CustomItems_DrawNet(this, play); // uses the current (hand-bone) matrix; Push/Pop internally
            if (this->meleeWeaponState != 0) {
                Vec3f spNet[3];
                D_80126080.x = sMeleeWeaponLengths[Player_GetMeleeWeaponHeld(this)];
                func_80090A28(this, spNet);
                func_800906D4(play, this, spNet);
            }
        } else if ((this->actor.scale.y >= 0.0f) && (this->meleeWeaponState != 0)) {
            Vec3f spE4[3];

            if (TransformMasks_IsFDSkinMode()) {
                // Fierce Deity sword reach: 5500 units (from MM z_player_lib.c)
                // Player_GetMeleeWeaponHeld returns BGS index (3) for FD
                D_80126080.x = 5500.0f;
                EffectBlure_ChangeType(Effect_GetByIndex(this->meleeWeaponEffectIndex),
                                       sSwordTypes[Player_GetMeleeWeaponHeld(this)]);
            } else if (Player_HoldsBrokenKnife(this)) {
                D_80126080.x = 1500.0f;
            } else {
                D_80126080.x = sMeleeWeaponLengths[Player_GetMeleeWeaponHeld(this)];
                EffectBlureShip_ChangeType(Effect_GetByIndex(this->meleeWeaponEffectIndex),
                                           sSwordTypes[Player_GetMeleeWeaponHeld(this)]);
            }

            func_80090A28(this, spE4);
            func_800906D4(play, this, spE4);
        } else if ((*dList != NULL) && (this->leftHandType == PLAYER_MODELTYPE_LH_BOTTLE)) {
            Color_RGB8* bottleColor = &sBottleColors[Player_ActionToBottle(this, this->itemAction)];

            OPEN_DISPS(play->state.gfxCtx);

            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            if (GameInteractor_Should(VB_PLAYER_DRAW_BOTTLE, true, this, play)) {
                gDPSetEnvColor(POLY_XLU_DISP++, bottleColor->r, bottleColor->g, bottleColor->b, 0);
                gSPDisplayList(POLY_XLU_DISP++, sBottleDLists[gSaveContext.linkAge]);
            }

            CLOSE_DISPS(play->state.gfxCtx);
        }

        if (this->actor.scale.y >= 0.0f) {
            if (!Player_HoldsHookshot(this) && ((hookedActor = this->heldActor) != NULL)) {
                if (this->stateFlags1 & PLAYER_STATE1_READY_TO_FIRE) {
                    Matrix_MultVec3f(&sLeftHandArrowVec3, &hookedActor->world.pos);
                    Matrix_RotateZYX(0x69E8, -0x5708, 0x458E, MTXMODE_APPLY);
                    Matrix_Get(&sp14C);
                    Matrix_MtxFToYXZRotS(&sp14C, &hookedActor->world.rot, 0);
                    hookedActor->shape.rot = hookedActor->world.rot;
                } else if (this->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) {
                    Vec3s spB8;

                    Matrix_Get(&sp14C);
                    Matrix_MtxFToYXZRotS(&sp14C, &spB8, 0);

                    if (hookedActor->flags & ACTOR_FLAG_CARRY_X_ROT_INFLUENCE) {
                        hookedActor->world.rot.x = hookedActor->shape.rot.x = spB8.x - this->unk_3BC.x;
                    } else {
                        hookedActor->world.rot.y = hookedActor->shape.rot.y = this->actor.shape.rot.y + this->unk_3BC.y;
                    }
                }
            } else {
                Matrix_Get(&this->mf_9E0);
                Matrix_MtxFToYXZRotS(&this->mf_9E0, &this->unk_3BC, 0);
            }
        }
    } else if (limbIndex == PLAYER_LIMB_R_HAND) {
        Actor* heldActor = this->heldActor;

        ItemEquip_CaptureHandMatrix();

        if (this->rightHandType == PLAYER_MODELTYPE_RH_FF) {
            Matrix_Get(&this->shieldMf);
        } else if ((this->rightHandType == PLAYER_MODELTYPE_RH_BOW_SLINGSHOT) ||
                   (this->rightHandType == PLAYER_MODELTYPE_RH_BOW_SLINGSHOT_2)) {
            s32 stringModelToUse = gSaveContext.linkAge;
            if (CVarGetInteger(CVAR_ENHANCEMENT("BowSlingshotAmmoFix"), 0) ||
                CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0)) {
                stringModelToUse = Player_HoldsSlingshot(this);
            }
            BowStringData* stringData = &sBowStringData[stringModelToUse];

            OPEN_DISPS(play->state.gfxCtx);

            Matrix_Push();
            Matrix_Translate(stringData->pos.x, stringData->pos.y, stringData->pos.z, MTXMODE_APPLY);

            if ((this->stateFlags1 & PLAYER_STATE1_READY_TO_FIRE) && (this->unk_860 >= 0) && (this->unk_834 <= 10)) {
                Vec3f sp90;
                f32 distXYZ;

                Matrix_MultVec3f(&sZeroVec, &sp90);
                distXYZ = Math_Vec3f_DistXYZ(D_80160000, &sp90);

                this->unk_858 = distXYZ - 3.0f;
                if (distXYZ < 3.0f) {
                    this->unk_858 = 0.0f;
                } else {
                    this->unk_858 *= 1.6f;
                    if (this->unk_858 > 1.0f) {
                        this->unk_858 = 1.0f;
                    }
                }

                this->unk_85C = -0.5f;
            }

            Matrix_Scale(1.0f, this->unk_858, 1.0f, MTXMODE_APPLY);

            if (!LINK_IS_ADULT) {
                Matrix_RotateZ(this->unk_858 * -0.2f, MTXMODE_APPLY);
            }

            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, stringData->dList);

            Matrix_Pop();

            CLOSE_DISPS(play->state.gfxCtx);
        } else if ((this->actor.scale.y >= 0.0f) && (this->rightHandType == PLAYER_MODELTYPE_RH_SHIELD)) {
            Matrix_Get(&this->shieldMf);
            Player_UpdateShieldCollider(play, this, &this->shieldQuad, sRightHandLimbModelShieldQuadVertices);

            // Gerudo: skip the shield DL — the dual scimitar at R_HAND was
            // already drawn by GerudoForm_GetSwordDL_R via OverrideLimbDraw,
            // and the player sees both swords held up as the "shield" visual
            // (arms-only kf_hanare_loop override). Mechanics still fire:
            // shieldMf is captured above and shieldQuad collider was just
            // activated, so Mirror Shield reflection / deflection / sword
            // sparks all work 1:1 vanilla. Only the model render is suppressed.
            if (!GerudoForm_IsActive()) {
                // Shield of Ikana: draw MM Mirror Shield from mm.o2r
                ExtEquip_DrawShieldDL(play);
                // Boss Remains: draw Odolwa's shield in the raised hand (the native shield was
                // swapped to an open hand in the override above). Self-guards on Odolwa-worn;
                // own push/pop + transform. Mirrors the MM 2ship R_HAND shield hook.
                BossRemains_DrawOdolwaShield(play, this);
            }
        }

        if (this->actor.scale.y >= 0.0f) {
            if (GameInteractor_Should(VB_DRAW_ADDITIONAL_RETICLES,
                                      (this->heldItemAction == PLAYER_IA_HOOKSHOT) ||
                                          (this->heldItemAction == PLAYER_IA_LONGSHOT),
                                      this)) {
                Matrix_MultVec3f(&D_80126184, &this->unk_3C8);

                if (heldActor != NULL) {
                    MtxF sp44;
                    s32 pad;

                    Matrix_MultVec3f(&D_80126190, &heldActor->world.pos);
                    Matrix_RotateZYX(0, -0x4000, -0x4000, MTXMODE_APPLY);
                    Matrix_Get(&sp44);
                    Matrix_MtxFToYXZRotS(&sp44, &heldActor->world.rot, 0);
                    heldActor->shape.rot = heldActor->world.rot;

                    if (func_8002DD78(this) != 0) {
                        // Skijer's NEI hookshot overhaul — Ultrashot: the Longshot reaches TWICE as
                        // far while the unlock is owned, so its reticle raycast must too or it
                        // vanishes over the far half of the range. No other change: same Longshot
                        // in-hand DL and reticle, just double distance.
                        extern u8 Nei_UltrashotOwned(void);
                        f32 reticleRange = (this->heldItemAction == PLAYER_IA_HOOKSHOT) ? 38600.0f : 77600.0f;

                        if ((this->heldItemAction == PLAYER_IA_LONGSHOT) && Nei_UltrashotOwned()) {
                            reticleRange *= 2.0f;
                        }
                        Matrix_Translate(500.0f, 300.0f, 0.0f, MTXMODE_APPLY);
                        Player_DrawHookshotReticle(
                            play, this, reticleRange * CVarGetFloat(CVAR_CHEAT("HookshotReachMultiplier"), 1.0f));
                    }
                }
            }

            if ((this->unk_862 != 0) || ((func_8002DD6C(this) == 0) && (heldActor != NULL))) {
                if (!(this->stateFlags1 & PLAYER_STATE1_GETTING_ITEM) && (this->unk_862 != 0) &&
                    (this->exchangeItemId != EXCH_ITEM_NONE)) {
                    Math_Vec3f_Copy(&sGetItemRefPos, &this->leftHandPos);
                } else {
                    sGetItemRefPos.x = (this->bodyPartsPos[15].x + this->leftHandPos.x) * 0.5f;
                    sGetItemRefPos.y = (this->bodyPartsPos[15].y + this->leftHandPos.y) * 0.5f;
                    sGetItemRefPos.z = (this->bodyPartsPos[15].z + this->leftHandPos.z) * 0.5f;
                }

                if (this->unk_862 == 0) {
                    Math_Vec3f_Copy(&heldActor->world.pos, &sGetItemRefPos);
                }
            }
        }
    } else if (this->actor.scale.y >= 0.0f) {
        if (limbIndex == PLAYER_LIMB_SHEATH) {
            if ((this->rightHandType != PLAYER_MODELTYPE_RH_SHIELD) &&
                (this->rightHandType != PLAYER_MODELTYPE_RH_FF)) {
                if (Player_IsChildWithHylianShield(this)) {
                    Player_UpdateShieldCollider(play, this, &this->shieldQuad, sSheathLimbModelShieldQuadVertices);
                }

                Matrix_TranslateRotateZYX(&sSheathLimbModelShieldOnBackPos, &sSheathLimbModelShieldOnBackZyxRot);
                Matrix_Get(&this->shieldMf);

                // Shield of Ikana: draw MM Mirror Shield on back
                ExtEquip_DrawShieldBackDL(play);
            }

        } else if (limbIndex == PLAYER_LIMB_HEAD) {
            Matrix_MultVec3f(&sPlayerFocusOffsetFromHead, &this->actor.focus.pos);
            ShipLua_DrawMaskTransitionHead(play, this);

            // Draw worn MM mask on Link's head (matrix is in head limb space)
            TransformMasks_WearDraw(play, this);

            // Boss Remains: draw the worn boss-remains mask on Link's face using the head-limb
            // matrix (current here, same one the mask draw above uses). No-op unless a remains is
            // worn. Mirrors the MM 2ship PLAYER_LIMB_HEAD hook.
            BossRemains_DrawWornMask(play, this);

        } else if (limbIndex == PLAYER_LIMB_ROOT) {
            // Kite Shield: the board under Link's feet while shield surfing. Self-guards on the
            // surf being active, and hides the hand/back shield for as long as it draws.
            ExtEquip_DrawKiteSurfBoard(play);
        } else if (limbIndex == PLAYER_LIMB_UPPER) {
            // Spirit Breastplate: draw Iron Knuckle armor on torso
            ExtEquip_DrawBreastplate(play);
        } else if (limbIndex == PLAYER_LIMB_L_SHOULDER || limbIndex == PLAYER_LIMB_R_SHOULDER) {
            // Magic Cape + Champion's Scarf: capture shoulder world positions
            ExtEquip_CaptureCapeShoulderPos(limbIndex);
        } else if (limbIndex == PLAYER_LIMB_L_FOOT || limbIndex == PLAYER_LIMB_R_FOOT) {
            Vec3f* vec = &sLeftRightFootLimbModelFootPos[(gSaveContext.linkAge)];

            Actor_SetFeetPos(&this->actor, limbIndex, PLAYER_LIMB_L_FOOT, vec, PLAYER_LIMB_R_FOOT, vec);

            // Pegasus Anklet no longer draws a custom per-foot model (torus + wings removed) — its
            // look is now the RED hover boots drawn with the body in Player_DrawImpl (Skijer 2026-07-15).
        }
    }
}