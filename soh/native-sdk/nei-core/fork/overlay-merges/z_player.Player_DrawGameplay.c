/* overlay-merge 897df4539d1d 967b40cf9119 cdbf0aa2b959 */
void Player_DrawGameplay(PlayState* play, Player* this, s32 lod, Gfx* cullDList, OverrideLimbDrawOpa overrideLimbDraw) {
    static s32 D_8085486C = 255;

    OPEN_DISPS(play->state.gfxCtx);

    gSPSegment(POLY_OPA_DISP++, 0x0C, cullDList);
    gSPSegment(POLY_XLU_DISP++, 0x0C, cullDList);

    // Garo form replaces Player_DrawImpl with a null-body pass — its skel is
    // a custom non-Link-compatible 19-bone rig, so Link's draw logic can't
    // walk it. Instead we run SkelAnime_DrawFlexLod with mesh suppressed so
    // the side-effect updates (bodyPartsPos, leftHandPos, focus.pos at HEAD,
    // feetPos, shieldMf) still fire. Garo's body renders later via
    // GaroForm_TryDrawSmoothSkin.
    //
    // Every other custom form (Gerudo, Kafei, Keaton, Rito…) is a Link-rigged
    // skin packed under `objects/forms/<model>/` (NOT alt-asset, so Link's
    // vanilla resources are never permanently hijacked). Their draw wraps the
    // limb-draw override with CustomForms_OverrideLimbDraw: the vanilla
    // override still decides what Link should be showing, and the wrapper
    // redirects each resource to the form's own copy WHEN THE FORM SHIPS ONE.
    // Anything it doesn't ship keeps drawing vanilla, so equipment still works.
    {
        u8 garoActive = 0;
        u8 formSwapActive = 0;
        if (O2rLoader_HasActiveModel()) {
            const char* mn = O2rLoader_GetForcedName();
            if (mn != NULL) {
                if (strcmp(mn, "garo") == 0) {
                    garoActive = 1;
                } else {
                    formSwapActive = CustomForms_WantsPathSwap();
                }
            }
        }
        if (garoActive) {
            extern void GaroForm_DrawNullBody(PlayState * play, Player * player, s32 lod);
            this->actor.shape.shadowScale = 1.0f;
            GaroForm_DrawNullBody(play, this, lod);
            if ((this->heldItemAction == PLAYER_IA_HOOKSHOT) || (this->heldItemAction == PLAYER_IA_LONGSHOT)) {
                this->unk_3C8.x = this->actor.world.pos.x;
                this->unk_3C8.y = this->actor.world.pos.y + 40.0f;
                this->unk_3C8.z = this->actor.world.pos.z;
            }
        } else if (formSwapActive) {
            // Chain to the vanilla override, then redirect to the form's copies.
            CustomForms_SetChainedOverride((void*)overrideLimbDraw);
            Player_DrawImpl(play, this->skelAnime.skeleton, this->skelAnime.jointTable, this->skelAnime.dListCount, lod,
                            this->currentTunic, this->currentBoots, this->actor.shape.face,
                            CustomForms_OverrideLimbDraw, Player_PostLimbDrawGameplay, this);
        } else {
            Player_DrawImpl(play, this->skelAnime.skeleton, this->skelAnime.jointTable, this->skelAnime.dListCount, lod,
                            this->currentTunic, this->currentBoots, this->actor.shape.face, overrideLimbDraw,
                            Player_PostLimbDrawGameplay, this);
        }
    }

    // O Player_Draw do host (o NEI só o embrulha) passa o endereço do host. Função de outro fonte usada como valor
    // não ganha o nei_fnaddr_ da extração, e a cópia da DLL tem outro endereço: sem isto, máscara nenhuma é desenhada.
    extern void* nei_fnaddr_Player_OverrideLimbDrawGameplayDefault;
    // FIX: Validate mask range to prevent crashes with custom items (masks are 1-8)
    if ((overrideLimbDraw == nei_fnaddr_Player_OverrideLimbDrawGameplayDefault ||
         overrideLimbDraw == Player_OverrideLimbDrawGameplayDefault) &&
        (this->currentMask >= PLAYER_MASK_KEATON && this->currentMask <= PLAYER_MASK_TRUTH)) {
        // Fixes a bug in vanilla where ice traps are rendered extremely large while wearing a bunny hood
        if (CVarGetInteger(CVAR_GENERAL("FixIceTrapWithBunnyHood"), 1))
            Matrix_Push();
        Mtx* bunnyEarMtx = Graph_Alloc(play->state.gfxCtx, 2 * sizeof(Mtx));

        if (this->currentMask == PLAYER_MASK_BUNNY) {
            Vec3s earRot;

            FrameInterpolation_RecordActorPosRotMatrix();
            gSPSegment(POLY_OPA_DISP++, 0x0B, bunnyEarMtx);

            // Right ear
            earRot.x = sBunnyEarKinematics.rot.y + 0x3E2;
            earRot.y = sBunnyEarKinematics.rot.z + 0xDBE;
            earRot.z = sBunnyEarKinematics.rot.x - 0x348A;
            Matrix_SetTranslateRotateYXZ(97.0f, -1203.0f - CVarGetFloat(CVAR_COSMETIC("BunnyHood.EarLength"), 0.0f),
                                         -240.0f - CVarGetFloat(CVAR_COSMETIC("BunnyHood.EarSpread"), 0.0f), &earRot);
            MATRIX_TOMTX(bunnyEarMtx++);

            // Left ear
            earRot.x = sBunnyEarKinematics.rot.y - 0x3E2;
            earRot.y = -0xDBE - sBunnyEarKinematics.rot.z;
            earRot.z = sBunnyEarKinematics.rot.x - 0x348A;
            Matrix_SetTranslateRotateYXZ(97.0f, -1203.0f - CVarGetFloat(CVAR_COSMETIC("BunnyHood.EarLength"), 0.0f),
                                         240.0f + CVarGetFloat(CVAR_COSMETIC("BunnyHood.EarSpread"), 0.0f), &earRot);
            MATRIX_TOMTX(bunnyEarMtx);
        }

        // Skip the Gerudo Mask DL when the Gerudo Form is active — the
        // player IS the gerudo, drawing the mask on her face would look
        // wrong (mask floating in front of an already-gerudo face).
        // O Bunny Hood escondido fica com o Player_DrawVanillaMask do host, que o gancho de máscara chama.
        u8 hideMask = (this->currentMask == PLAYER_MASK_GERUDO) && GerudoForm_IsActive();
        if (!GameInteractor_InvisibleLinkActive()) {
            LinkSpan_PlayerMaskDraw(
                play, this, GameInteractor_Should(VB_DRAW_PLAYER_MASK, true, this->currentMask, play) && !hideMask,
                Player_DrawVanillaMask);
        }

        if (CVarGetInteger(CVAR_GENERAL("FixIceTrapWithBunnyHood"), 1))
            Matrix_Pop();
    }

    if (Player_IsHovering(this) && !(this->actor.bgCheckFlags & BGCHECKFLAG_GROUND) &&
        !(this->stateFlags1 & PLAYER_STATE1_ON_HORSE) && (this->hoverBootsTimer != 0)) {
        s32 sp5C;
        s32 hoverBootsTimer = this->hoverBootsTimer;

        if (this->hoverBootsTimer < 19) {
            if (hoverBootsTimer >= 15) {
                D_8085486C = (19 - hoverBootsTimer) * 51.0f;
            } else if (hoverBootsTimer < 19) {
                sp5C = hoverBootsTimer;

                if (sp5C > 9) {
                    sp5C = 9;
                }

                D_8085486C = (-sp5C * 4) + 36;
                D_8085486C = D_8085486C * D_8085486C;
                D_8085486C = (s32)((Math_CosS(D_8085486C) * 100.0f) + 100.0f) + 55.0f;
                D_8085486C = D_8085486C * (sp5C * (1.0f / 9.0f));
            }

            FrameInterpolation_RecordActorPosRotMatrix();
            Matrix_SetTranslateRotateYXZ(this->actor.world.pos.x, this->actor.world.pos.y + 2.0f,
                                         this->actor.world.pos.z, &D_80854864);
            Matrix_Scale(4.0f, 4.0f, 4.0f, MTXMODE_APPLY);

            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPSegment(POLY_XLU_DISP++, 0x08,
                       Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0, 0, 16, 32, 1, 0, (play->gameplayFrames * -15) % 128,
                                          16, 32, 0, 0, 0, -15));
            gDPSetPrimColor(POLY_XLU_DISP++, 0x80, 0x80, 255, 255, 255, D_8085486C);
            gDPSetEnvColor(POLY_XLU_DISP++, 120, 90, 30, 128);
            gSPDisplayList(POLY_XLU_DISP++, gHoverBootsCircleDL);
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}
