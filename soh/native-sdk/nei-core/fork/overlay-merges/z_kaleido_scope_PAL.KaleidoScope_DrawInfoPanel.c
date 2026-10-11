/* overlay-merge 1cee5f46899a 4db395157b0c 56bac45d4e8a */
void KaleidoScope_DrawInfoPanel(PlayState* play) {
    Color_RGB8 aButtonColor = { 0, 100, 255 };
    if (CVarGetInteger(CVAR_COSMETIC("HUD.AButton.Changed"), 0)) {
        aButtonColor = CVarGetColor24(CVAR_COSMETIC("HUD.AButton.Value"), aButtonColor);
    } else if (CVarGetInteger(CVAR_COSMETIC("DefaultColorScheme"), COLORSCHEME_N64) == COLORSCHEME_GAMECUBE) {
        aButtonColor = (Color_RGB8){ 0, 255, 100 };
    }

    Color_RGB8 cButtonsColor = { 255, 160, 0 };
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CButtons.Changed"), 0)) {
        cButtonsColor = CVarGetColor24(CVAR_COSMETIC("HUD.CButtons.Value"), cButtonsColor);
    }
    Color_RGB8 cUpButtonColor = cButtonsColor;
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CUpButton.Changed"), 0)) {
        cUpButtonColor = CVarGetColor24(CVAR_COSMETIC("HUD.CUpButton.Value"), cUpButtonColor);
    }
    Color_RGB8 cDownButtonColor = cButtonsColor;
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.Changed"), 0)) {
        cDownButtonColor = CVarGetColor24(CVAR_COSMETIC("HUD.CDownButton.Value"), cDownButtonColor);
    }
    Color_RGB8 cLeftButtonColor = cButtonsColor;
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.Changed"), 0)) {
        cLeftButtonColor = CVarGetColor24(CVAR_COSMETIC("HUD.CLeftButton.Value"), cLeftButtonColor);
    }
    Color_RGB8 cRightButtonColor = cButtonsColor;
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.Changed"), 0)) {
        cRightButtonColor = CVarGetColor24(CVAR_COSMETIC("HUD.CRightButton.Value"), cRightButtonColor);
    }

    static const void* sToEquipTextures[4] = {
        gPauseToEquipENGTex,
        gPauseToEquipGERTex,
        gPauseToEquipFRATex,
        gPauseToEquipJPNTex,
    };
    static const void* sToDecideTextures[4] = {
        gPauseToDecideENGTex,
        gPauseToDecideGERTex,
        gPauseToDecideFRATex,
        gPauseToDecideJPNTex,
    };
    static const void* sPlayMelodyTextures[4] = {
        gPauseToPlayMelodyENGTex,
        gPauseToPlayMelodyGERTex,
        gPauseToPlayMelodyFRATex,
        gPauseToPlayMelodyJPNTex,
    };
    static const void* D_8082AD78[][4] = {
        { gPauseToEquipmentENGTex, gPauseToEquipmentGERTex, gPauseToEquipmentFRATex, gPauseToEquipmentJPNTex },
        { gPauseToSelectItemENGTex, gPauseToSelectItemGERTex, gPauseToSelectItemFRATex, gPauseToSelectItemJPNTex },
        { gPauseToMapENGTex, gPauseToMapGERTex, gPauseToMapFRATex, gPauseToMapJPNTex },
        { gPauseToQuestStatusENGTex, gPauseToQuestStatusGERTex, gPauseToQuestStatusFRATex, gPauseToQuestStatusJPNTex },
    };
    static void* D_8082ADA8[][4] = {
        { gPauseToMapENGTex, gPauseToMapGERTex, gPauseToMapFRATex, gPauseToMapJPNTex },
        { gPauseToQuestStatusENGTex, gPauseToQuestStatusGERTex, gPauseToQuestStatusFRATex, gPauseToQuestStatusJPNTex },
        { gPauseToEquipmentENGTex, gPauseToEquipmentGERTex, gPauseToEquipmentFRATex, gPauseToEquipmentJPNTex },
        { gPauseToSelectItemENGTex, gPauseToSelectItemGERTex, gPauseToSelectItemFRATex, gPauseToSelectItemJPNTex },
    };
    static u16 D_8082ADD8[4] = { 56, 88, 80, 56 };
    static u16 D_8082ADE0[4] = { 64, 88, 72, 48 };
    static u16 D_8082ADE8[4] = { 80, 104, 112, 80 };
    static s16 D_8082ADF0[][4] = {
        { 180, 210, 255, 220 },
        { 100, 100, 150, 220 },
    };
    static s16 D_8082AE00 = 20;
    static s16 D_8082AE04 = 0;
    static s16 D_8082AE08[] = {
        10, 16, 16, 17, 12, 13, 18, 17, 17, 19, 13, 21, 20, 21, 14, 15, 15, 15, 11, 14,
    };
    static s16 D_8082AE30[] = {
        21, 20, 19, 18, 11, 14, 10, 15, 16, 13, 12, 17,
    };
    static s16 D_808321A0;
    static s16 D_808321A2;
    static s16 D_808321A4;
    static s16 D_808321A6;
    PauseContext* pauseCtx = &play->pauseCtx;
    s16 stepR;
    s16 stepG;
    s16 stepB;
    s16 stepA;
    s16 temp;
    s16 i;
    s16 j;

    // #region SOH [NTSC] - register access assumes 3 languages, make jpn use same as eng
    s32 languageOffset = gSaveContext.language;
    if (languageOffset == LANGUAGE_JPN) {
        languageOffset = LANGUAGE_ENG;
    }
    // #endregion

    OPEN_DISPS(play->state.gfxCtx);

    stepR = ABS(D_808321A0 - D_8082ADF0[D_8082AE04][0]) / D_8082AE00;
    stepG = ABS(D_808321A2 - D_8082ADF0[D_8082AE04][1]) / D_8082AE00;
    stepB = ABS(D_808321A4 - D_8082ADF0[D_8082AE04][2]) / D_8082AE00;
    stepA = ABS(D_808321A6 - D_8082ADF0[D_8082AE04][3]) / D_8082AE00;
    if (D_808321A0 >= D_8082ADF0[D_8082AE04][0]) {
        D_808321A0 -= stepR;
    } else {
        D_808321A0 += stepR;
    }
    if (D_808321A2 >= D_8082ADF0[D_8082AE04][1]) {
        D_808321A2 -= stepG;
    } else {
        D_808321A2 += stepG;
    }
    if (D_808321A4 >= D_8082ADF0[D_8082AE04][2]) {
        D_808321A4 -= stepB;
    } else {
        D_808321A4 += stepB;
    }
    if (D_808321A6 >= D_8082ADF0[D_8082AE04][3]) {
        D_808321A6 -= stepA;
    } else {
        D_808321A6 += stepA;
    }

    D_8082AE00--;
    if (D_8082AE00 == 0) {
        D_808321A0 = D_8082ADF0[D_8082AE04][0];
        D_808321A2 = D_8082ADF0[D_8082AE04][1];
        D_808321A4 = D_8082ADF0[D_8082AE04][2];
        D_808321A6 = D_8082ADF0[D_8082AE04][3];
        D_8082AE00 = ZREG(28);
        D_8082AE04 ^= 1;
    }

    temp = pauseCtx->infoPanelOffsetY - 76;
    for (j = 0, i = 0; i < 7; i++, j += 4) {
        pauseCtx->infoPanelVtx[j + 0].v.ob[0] = pauseCtx->infoPanelVtx[j + 2].v.ob[0] = -72;

        pauseCtx->infoPanelVtx[j + 1].v.ob[0] = pauseCtx->infoPanelVtx[j + 3].v.ob[0] = 0;

        pauseCtx->infoPanelVtx[j + 0].v.ob[1] = pauseCtx->infoPanelVtx[j + 1].v.ob[1] = temp;

        pauseCtx->infoPanelVtx[j + 2].v.ob[1] = pauseCtx->infoPanelVtx[j + 3].v.ob[1] = temp - 24;

        pauseCtx->infoPanelVtx[j + 0].v.ob[2] = pauseCtx->infoPanelVtx[j + 1].v.ob[2] =
            pauseCtx->infoPanelVtx[j + 2].v.ob[2] = pauseCtx->infoPanelVtx[j + 3].v.ob[2] = 0;

        pauseCtx->infoPanelVtx[j + 0].v.flag = pauseCtx->infoPanelVtx[j + 1].v.flag =
            pauseCtx->infoPanelVtx[j + 2].v.flag = pauseCtx->infoPanelVtx[j + 3].v.flag = 0;

        pauseCtx->infoPanelVtx[j + 0].v.tc[0] = pauseCtx->infoPanelVtx[j + 0].v.tc[1] =
            pauseCtx->infoPanelVtx[j + 1].v.tc[1] = pauseCtx->infoPanelVtx[j + 2].v.tc[0] = 0;

        pauseCtx->infoPanelVtx[j + 1].v.tc[0] = pauseCtx->infoPanelVtx[j + 3].v.tc[0] = 0x900;

        pauseCtx->infoPanelVtx[j + 2].v.tc[1] = pauseCtx->infoPanelVtx[j + 3].v.tc[1] = 0x300;

        pauseCtx->infoPanelVtx[j + 0].v.cn[0] = pauseCtx->infoPanelVtx[j + 2].v.cn[0] =
            pauseCtx->infoPanelVtx[j + 0].v.cn[1] = pauseCtx->infoPanelVtx[j + 2].v.cn[1] =
                pauseCtx->infoPanelVtx[j + 0].v.cn[2] = pauseCtx->infoPanelVtx[j + 2].v.cn[2] =
                    pauseCtx->infoPanelVtx[j + 1].v.cn[0] = pauseCtx->infoPanelVtx[j + 3].v.cn[0] =
                        pauseCtx->infoPanelVtx[j + 1].v.cn[1] = pauseCtx->infoPanelVtx[j + 3].v.cn[1] =
                            pauseCtx->infoPanelVtx[j + 1].v.cn[2] = pauseCtx->infoPanelVtx[j + 3].v.cn[2] = 200;

        pauseCtx->infoPanelVtx[j + 0].v.cn[3] = pauseCtx->infoPanelVtx[j + 2].v.cn[3] =
            pauseCtx->infoPanelVtx[j + 1].v.cn[3] = pauseCtx->infoPanelVtx[j + 3].v.cn[3] = pauseCtx->alpha;
    }

    pauseCtx->infoPanelVtx[4].v.ob[0] = pauseCtx->infoPanelVtx[6].v.ob[0] = pauseCtx->infoPanelVtx[0].v.ob[0] + 72;

    pauseCtx->infoPanelVtx[5].v.ob[0] = pauseCtx->infoPanelVtx[7].v.ob[0] = pauseCtx->infoPanelVtx[4].v.ob[0] + 72;

    if ((pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_LEFT) && (pauseCtx->unk_1E4 == 0)) {
        pauseCtx->infoPanelVtx[8].v.ob[0] = pauseCtx->infoPanelVtx[10].v.ob[0] = WREG(16);

        pauseCtx->infoPanelVtx[9].v.ob[0] = pauseCtx->infoPanelVtx[11].v.ob[0] = pauseCtx->infoPanelVtx[8].v.ob[0] + 24;

        pauseCtx->infoPanelVtx[8].v.ob[1] = pauseCtx->infoPanelVtx[9].v.ob[1] = WREG(18);

        pauseCtx->infoPanelVtx[10].v.ob[1] = pauseCtx->infoPanelVtx[11].v.ob[1] =
            pauseCtx->infoPanelVtx[8].v.ob[1] - 32;
    } else {
        pauseCtx->infoPanelVtx[8].v.ob[0] = pauseCtx->infoPanelVtx[10].v.ob[0] = WREG(16) + 3;

        pauseCtx->infoPanelVtx[9].v.ob[0] = pauseCtx->infoPanelVtx[11].v.ob[0] = pauseCtx->infoPanelVtx[8].v.ob[0] + 18;

        pauseCtx->infoPanelVtx[8].v.ob[1] = pauseCtx->infoPanelVtx[9].v.ob[1] = WREG(18) - 3;

        pauseCtx->infoPanelVtx[10].v.ob[1] = pauseCtx->infoPanelVtx[11].v.ob[1] =
            pauseCtx->infoPanelVtx[8].v.ob[1] - 26;
    }

    if ((pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_RIGHT) && (pauseCtx->unk_1E4 == 0)) {
        pauseCtx->infoPanelVtx[12].v.ob[0] = pauseCtx->infoPanelVtx[14].v.ob[0] = WREG(17);

        pauseCtx->infoPanelVtx[13].v.ob[0] = pauseCtx->infoPanelVtx[15].v.ob[0] =
            pauseCtx->infoPanelVtx[12].v.ob[0] + 24;

        pauseCtx->infoPanelVtx[12].v.ob[1] = pauseCtx->infoPanelVtx[13].v.ob[1] = WREG(18);

        pauseCtx->infoPanelVtx[14].v.ob[1] = pauseCtx->infoPanelVtx[15].v.ob[1] =
            pauseCtx->infoPanelVtx[12].v.ob[1] - 32;
    } else {
        pauseCtx->infoPanelVtx[12].v.ob[0] = pauseCtx->infoPanelVtx[14].v.ob[0] = WREG(17) + 3;

        pauseCtx->infoPanelVtx[13].v.ob[0] = pauseCtx->infoPanelVtx[15].v.ob[0] =
            pauseCtx->infoPanelVtx[12].v.ob[0] + 18;

        pauseCtx->infoPanelVtx[12].v.ob[1] = pauseCtx->infoPanelVtx[13].v.ob[1] = WREG(18) - 3;

        pauseCtx->infoPanelVtx[14].v.ob[1] = pauseCtx->infoPanelVtx[15].v.ob[1] =
            pauseCtx->infoPanelVtx[12].v.ob[1] - 26;
    }

    pauseCtx->infoPanelVtx[9].v.tc[0] = pauseCtx->infoPanelVtx[11].v.tc[0] = pauseCtx->infoPanelVtx[13].v.tc[0] =
        pauseCtx->infoPanelVtx[15].v.tc[0] = 0x300;

    pauseCtx->infoPanelVtx[10].v.tc[1] = pauseCtx->infoPanelVtx[11].v.tc[1] = pauseCtx->infoPanelVtx[14].v.tc[1] =
        pauseCtx->infoPanelVtx[15].v.tc[1] = 0x400;

    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

    Matrix_Translate(0.0f, 0.0f, -144.0f, MTXMODE_NEW);
    Matrix_Scale(1.0f, 1.0f, 1.0f, MTXMODE_APPLY);

    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    const Color_RGBA8 namePanelColor =
        CVarGetColor(CVAR_COSMETIC("Kaleido.NamePanel.Value"), (Color_RGBA8){ 90, 100, 130, 255 });

    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, namePanelColor.r, namePanelColor.g, namePanelColor.b, namePanelColor.a);
    gSPVertex(POLY_OPA_DISP++, &pauseCtx->infoPanelVtx[0], 16, 0);

    gSPDisplayList(POLY_OPA_DISP++, gItemNamePanelDL);

    if (CVarGetInteger(CVAR_ENHANCEMENT("FixMenuLR"), 0) == 0) { // Restore the misplace gDPSetPrimColor
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 90, 100, 130, 255);
    }

    if ((pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_LEFT) && (pauseCtx->unk_1E4 == 0)) {
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, D_808321A0, D_808321A2, D_808321A4, D_808321A6);
    } else {
        if (CVarGetInteger(CVAR_ENHANCEMENT("FixMenuLR"), 0) != 0) {
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 180, 210, 255, 255);
        }
    }

    gSPDisplayList(POLY_OPA_DISP++, gLButtonIconDL);

    if (CVarGetInteger(CVAR_ENHANCEMENT("FixMenuLR"), 0) == 0) { // Restore the misplace gDPSetPrimColor
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 180, 210, 255, 255);
    }

    if ((pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_RIGHT) && (pauseCtx->unk_1E4 == 0)) {
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, D_808321A0, D_808321A2, D_808321A4, D_808321A6);
    } else {
        if (CVarGetInteger(CVAR_ENHANCEMENT("FixMenuLR"), 0) != 0) {
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 180, 210, 255, 255);
        }
    }

    gSPDisplayList(POLY_OPA_DISP++, gRButtonIconDL);

    if (pauseCtx->cursorSpecialPos != 0) {
        j = (pauseCtx->cursorSpecialPos * 4) - 32;
        pauseCtx->cursorVtx[0].v.ob[0] = pauseCtx->infoPanelVtx[j].v.ob[0];
        pauseCtx->cursorVtx[0].v.ob[1] = pauseCtx->infoPanelVtx[j].v.ob[1];
        KaleidoScope_UpdateCursorSize(pauseCtx);
        KaleidoScope_DrawCursor(play, pauseCtx->pageIndex);
    }

    temp = pauseCtx->infoPanelOffsetY - 80;
    pauseCtx->infoPanelVtx[16].v.ob[1] = pauseCtx->infoPanelVtx[17].v.ob[1] = temp;

    pauseCtx->infoPanelVtx[18].v.ob[1] = pauseCtx->infoPanelVtx[19].v.ob[1] = pauseCtx->infoPanelVtx[16].v.ob[1] - 16;

    pauseCtx->infoPanelVtx[18].v.tc[1] = pauseCtx->infoPanelVtx[19].v.tc[1] = 0x200;

    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetCombineLERP(POLY_OPA_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, PRIMITIVE,
                      ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
    gDPSetEnvColor(POLY_OPA_DISP++, 20, 30, 40, 0);

    if ((pauseCtx->state == 6) && (pauseCtx->namedItem != PAUSE_ITEM_NONE) && (pauseCtx->nameDisplayTimer < WREG(89)) &&
        (!pauseCtx->unk_1E4 || (pauseCtx->unk_1E4 == 2) || ((pauseCtx->unk_1E4 >= 4) && (pauseCtx->unk_1E4 <= 7)) ||
         (pauseCtx->unk_1E4 == 8)) &&
        (pauseCtx->cursorSpecialPos == 0)) {
        if (!pauseCtx->unk_1E4 || (pauseCtx->unk_1E4 == 2) || ((pauseCtx->unk_1E4 >= 4) && (pauseCtx->unk_1E4 <= 7)) ||
            (pauseCtx->unk_1E4 == 8)) {
            pauseCtx->infoPanelVtx[16].v.ob[0] = pauseCtx->infoPanelVtx[18].v.ob[0] = -63;

            pauseCtx->infoPanelVtx[17].v.ob[0] = pauseCtx->infoPanelVtx[19].v.ob[0] =
                pauseCtx->infoPanelVtx[16].v.ob[0] + 128;

            pauseCtx->infoPanelVtx[17].v.tc[0] = pauseCtx->infoPanelVtx[19].v.tc[0] = 0x1000;

            gSPVertex(POLY_OPA_DISP++, &pauseCtx->infoPanelVtx[16], 4, 0);

            if (pauseCtx->nameColorSet == 1) {
                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 70, 70, 70, 255);
            } else {
                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255);
            }

            // Twilight Upgrade mode-name swap — applied per-frame (NOT only when
            // cursor changes). The version at the cursor-change site only fires
            // once per nav, so toggling clawshot/gale mode while staying on the
            // slot left a stale vanilla name in nameSegment. Re-apply here so the
            // displayed name always matches the current mode.
            {
                const char* weaponUpgradeOverrideDraw = KaleidoScope_GetWeaponUpgradeNameOverride(pauseCtx->namedItem);
                const char* twilightOverrideDraw = KaleidoScope_GetTwilightNameOverride(pauseCtx->namedItem);
                if (weaponUpgradeOverrideDraw != NULL) {
                    memcpy(pauseCtx->nameSegment, weaponUpgradeOverrideDraw, strlen(weaponUpgradeOverrideDraw) + 1);
                } else if (twilightOverrideDraw != NULL) {
                    memcpy(pauseCtx->nameSegment, twilightOverrideDraw, strlen(twilightOverrideDraw) + 1);
                }
            }

            POLY_OPA_DISP = KaleidoScope_QuadTextureIA4(POLY_OPA_DISP, pauseCtx->nameSegment, 128, 16, 0);
        }

        if (pauseCtx->pageIndex == PAUSE_MAP && CVarGetInteger(CVAR_DEVELOPER_TOOLS("SkulltulaDebugEnabled"), 0) != 0) {
            if (YREG(7) != 0) {
                osSyncPrintf(VT_FGCOL(YELLOW));
                osSyncPrintf("キンスタ数(%d) Get_KIN_STA=%x (%x)  (%x)\n", YREG(6), GET_GS_FLAGS(YREG(6)),
                             gAreaGsFlags[YREG(6)], gSaveContext.gsFlags[YREG(6) >> 2]);
                osSyncPrintf(VT_RST);

                YREG(7) = 0;
                SET_GS_FLAGS(D_8082AE30[pauseCtx->cursorPoint[PAUSE_WORLD_MAP]],
                             gAreaGsFlags[D_8082AE30[pauseCtx->cursorPoint[PAUSE_WORLD_MAP]]]);
            }
        }

        if ((pauseCtx->pageIndex == PAUSE_MAP) && !sInDungeonScene) {
            if (GET_GS_FLAGS(D_8082AE30[pauseCtx->cursorPoint[PAUSE_WORLD_MAP]]) ==
                gAreaGsFlags[D_8082AE30[pauseCtx->cursorPoint[PAUSE_WORLD_MAP]]]) {

                pauseCtx->infoPanelVtx[24].v.ob[0] = pauseCtx->infoPanelVtx[26].v.ob[0] = -74;

                pauseCtx->infoPanelVtx[25].v.ob[0] = pauseCtx->infoPanelVtx[27].v.ob[0] =
                    pauseCtx->infoPanelVtx[24].v.ob[0] + 19;

                pauseCtx->infoPanelVtx[24].v.ob[1] = pauseCtx->infoPanelVtx[25].v.ob[1] =
                    pauseCtx->infoPanelVtx[24].v.ob[1] - 2;

                pauseCtx->infoPanelVtx[26].v.ob[1] = pauseCtx->infoPanelVtx[27].v.ob[1] =
                    pauseCtx->infoPanelVtx[24].v.ob[1] - 19;

                pauseCtx->infoPanelVtx[25].v.tc[0] = pauseCtx->infoPanelVtx[27].v.tc[0] = 0x300;

                gDPPipeSync(POLY_OPA_DISP++);
                gSPVertex(POLY_OPA_DISP++, &pauseCtx->infoPanelVtx[24], 4, 0);

                gDPSetCombineLERP(POLY_OPA_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                                  PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
                gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 0);

                KaleidoScope_DrawQuadTextureRGBA32(play->state.gfxCtx, gQuestIconGoldSkulltulaTex, 24, 24, 0);
            }
        }
    } else if ((pauseCtx->unk_1E4 < 3) || (pauseCtx->unk_1E4 == 7) || (pauseCtx->unk_1E4 == 8)) {
        pauseCtx->infoPanelVtx[20].v.ob[1] = pauseCtx->infoPanelVtx[21].v.ob[1] = temp;

        pauseCtx->infoPanelVtx[22].v.ob[1] = pauseCtx->infoPanelVtx[23].v.ob[1] =
            pauseCtx->infoPanelVtx[20].v.ob[1] - 16;

        pauseCtx->infoPanelVtx[22].v.tc[1] = pauseCtx->infoPanelVtx[23].v.tc[1] = 0x200;

        gSPVertex(POLY_OPA_DISP++, &pauseCtx->infoPanelVtx[16], 8, 0);

        if (pauseCtx->state == 7) {
            pauseCtx->infoPanelVtx[16].v.ob[0] = pauseCtx->infoPanelVtx[18].v.ob[0] = WREG(61 + languageOffset);

            pauseCtx->infoPanelVtx[17].v.ob[0] = pauseCtx->infoPanelVtx[19].v.ob[0] =
                pauseCtx->infoPanelVtx[16].v.ob[0] + 24;

            pauseCtx->infoPanelVtx[20].v.ob[0] = pauseCtx->infoPanelVtx[22].v.ob[0] =
                pauseCtx->infoPanelVtx[16].v.ob[0] + WREG(52 + languageOffset);

            pauseCtx->infoPanelVtx[21].v.ob[0] = pauseCtx->infoPanelVtx[23].v.ob[0] =
                pauseCtx->infoPanelVtx[20].v.ob[0] + D_8082ADE0[gSaveContext.language];

            pauseCtx->infoPanelVtx[17].v.tc[0] = pauseCtx->infoPanelVtx[19].v.tc[0] = 0x300;

            pauseCtx->infoPanelVtx[21].v.tc[0] = pauseCtx->infoPanelVtx[23].v.tc[0] = D_8082ADE0[gSaveContext.language]
                                                                                      << 5;
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, aButtonColor.r, aButtonColor.g, aButtonColor.b, 255);
            // gSPDisplayList(POLY_OPA_DISP++, gAButtonIconDL);//This is changed to load the texture only so we can prim
            // color it.
            gDPLoadTextureBlock(POLY_OPA_DISP++, gABtnSymbolTex, G_IM_FMT_IA, G_IM_SIZ_8b, 24, 16, 0,
                                G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 4, 4, G_TX_NOLOD, G_TX_NOLOD);
            gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);

            gDPPipeSync(POLY_OPA_DISP++);
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255);

            POLY_OPA_DISP = KaleidoScope_QuadTextureIA8(POLY_OPA_DISP, sToDecideTextures[gSaveContext.language],
                                                        D_8082ADE0[gSaveContext.language], 16, 4);
        } else if (pauseCtx->cursorSpecialPos != 0) {
            if ((pauseCtx->state == 6) && (pauseCtx->unk_1E4 == 0)) {
                pauseCtx->infoPanelVtx[16].v.ob[0] = pauseCtx->infoPanelVtx[18].v.ob[0] = -63;

                pauseCtx->infoPanelVtx[17].v.ob[0] = pauseCtx->infoPanelVtx[19].v.ob[0] =
                    pauseCtx->infoPanelVtx[16].v.ob[0] + 128;

                pauseCtx->infoPanelVtx[17].v.tc[0] = pauseCtx->infoPanelVtx[19].v.tc[0] = 0x1000;

                gDPPipeSync(POLY_OPA_DISP++);
                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 200, 0, 255);

                if (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_LEFT) {
                    POLY_OPA_DISP = KaleidoScope_QuadTextureIA8(
                        POLY_OPA_DISP, D_8082AD78[pauseCtx->pageIndex][gSaveContext.language], 128, 16, 0);
                } else {
                    POLY_OPA_DISP = KaleidoScope_QuadTextureIA8(
                        POLY_OPA_DISP, D_8082ADA8[pauseCtx->pageIndex][gSaveContext.language], 128, 16, 0);
                }
            }
        } else {
            bool pauseAnyCursor =
                (CVarGetInteger(CVAR_ENHANCEMENT("PauseAnyCursor"), 0) == PAUSE_ANY_CURSOR_RANDO_ONLY && IS_RANDO) ||
                (CVarGetInteger(CVAR_ENHANCEMENT("PauseAnyCursor"), 0) == PAUSE_ANY_CURSOR_ALWAYS_ON);
            if (!pauseCtx->pageIndex &&
                (!pauseAnyCursor || (ExtInv_GetSlotItem(ExtInv_GetInventorySlot(pauseCtx->cursorPoint[PAUSE_ITEM])) !=
                                     ITEM_NONE))) { // pageIndex == PAUSE_ITEM // Skijer's NEI
                pauseCtx->infoPanelVtx[16].v.ob[0] = pauseCtx->infoPanelVtx[18].v.ob[0] = WREG(49 + languageOffset);

                pauseCtx->infoPanelVtx[17].v.ob[0] = pauseCtx->infoPanelVtx[19].v.ob[0] =
                    pauseCtx->infoPanelVtx[16].v.ob[0] + 48;

                pauseCtx->infoPanelVtx[20].v.ob[0] = pauseCtx->infoPanelVtx[22].v.ob[0] =
                    pauseCtx->infoPanelVtx[16].v.ob[0] + WREG(58 + languageOffset);

                pauseCtx->infoPanelVtx[21].v.ob[0] = pauseCtx->infoPanelVtx[23].v.ob[0] =
                    pauseCtx->infoPanelVtx[20].v.ob[0] + D_8082ADD8[gSaveContext.language];

                pauseCtx->infoPanelVtx[17].v.tc[0] = pauseCtx->infoPanelVtx[19].v.tc[0] = 0x600;

                pauseCtx->infoPanelVtx[21].v.tc[0] = pauseCtx->infoPanelVtx[23].v.tc[0] =
                    D_8082ADD8[gSaveContext.language] << 5;

                s16 PosX; // General Pos of C button icon
                if (gSaveContext.language == LANGUAGE_ENG || gSaveContext.language == LANGUAGE_JPN) {
                    PosX = 112;
                } else if (gSaveContext.language == LANGUAGE_GER) {
                    PosX = 175;
                } else { // French
                    PosX = 98;
                }
                s16 PosY = 200 - pauseCtx->infoPanelOffsetY; // General Pos of C button icon
                s16 icon_w = 46;                             // Original texture size
                s16 icon_h = 16;
                s32 icon_x_offset;
                s16 icon_w_crop = 17.0f;             // Left
                int height = icon_h * 1.0f;          // Adjust Height with scale
                int width = icon_w * 1.0f;           // Adjust Width with scale
                int width_crop = icon_w_crop * 1.0f; // Adjust Width with scale
                int height_factor = (1 << 10) * icon_h / height;
                int width_factor = (1 << 10) * icon_w / width;
                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, cButtonsColor.r, cButtonsColor.g, cButtonsColor.b, 255);
                for (s16 i = 0; i < 3; i++) {
                    if (i == 0) {
                        icon_x_offset = width_crop * 3 - 3;
                        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, cRightButtonColor.r, cRightButtonColor.g,
                                        cRightButtonColor.b, 255);
                    } else if (i == 1) {
                        icon_x_offset = width_crop * 2 - 3;
                        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, cDownButtonColor.r, cDownButtonColor.g,
                                        cDownButtonColor.b, 255);
                    } else if (i == 2) {
                        icon_x_offset = width_crop;
                        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, cLeftButtonColor.r, cLeftButtonColor.g,
                                        cLeftButtonColor.b, 255);
                    }
                    gDPLoadTextureBlock(POLY_OPA_DISP++, gCBtnSymbolsTex, G_IM_FMT_IA, G_IM_SIZ_8b, icon_w, icon_h, 0,
                                        G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                        G_TX_NOLOD, G_TX_NOLOD);
                    // gSPScisTextureRectangle(POLY_OPA_DISP++,PosX << 2, PosY << 2, (PosX + icon_x_offset) << 2, (PosY
                    // + height) << 2, G_TX_RENDERTILE, 0, 0, width_factor, height_factor);
                    gSPWideTextureRectangle(POLY_OPA_DISP++, PosX << 2, PosY << 2, (PosX + icon_x_offset) << 2,
                                            (PosY + height) << 2, G_TX_WRAP, 0, 0, width_factor, height_factor);
                }
                gDPPipeSync(POLY_OPA_DISP++);
                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255);
                POLY_OPA_DISP = KaleidoScope_QuadTextureIA8(POLY_OPA_DISP, sToEquipTextures[gSaveContext.language],
                                                            D_8082ADD8[gSaveContext.language], 16, 4);
            } else if ((pauseCtx->pageIndex == PAUSE_MAP) && sInDungeonScene) {

            } else if ((pauseCtx->pageIndex == PAUSE_QUEST) && (pauseCtx->cursorSlot[PAUSE_QUEST] >= 6) &&
                       (pauseCtx->cursorSlot[PAUSE_QUEST] <= 0x11)) {
                if (pauseCtx->namedItem != PAUSE_ITEM_NONE) {
                    pauseCtx->infoPanelVtx[16].v.ob[0] = pauseCtx->infoPanelVtx[18].v.ob[0] = WREG(55 + languageOffset);

                    pauseCtx->infoPanelVtx[17].v.ob[0] = pauseCtx->infoPanelVtx[19].v.ob[0] =
                        pauseCtx->infoPanelVtx[16].v.ob[0] + 24;

                    pauseCtx->infoPanelVtx[20].v.ob[0] = pauseCtx->infoPanelVtx[22].v.ob[0] =
                        pauseCtx->infoPanelVtx[16].v.ob[0] + WREG(52 + languageOffset);

                    if (gSaveContext.language == LANGUAGE_GER) {
                        pauseCtx->infoPanelVtx[20].v.ob[0] = pauseCtx->infoPanelVtx[22].v.ob[0] =
                            pauseCtx->infoPanelVtx[16].v.ob[0] - 99;
                    }

                    pauseCtx->infoPanelVtx[21].v.ob[0] = pauseCtx->infoPanelVtx[23].v.ob[0] =
                        pauseCtx->infoPanelVtx[20].v.ob[0] + D_8082ADE8[gSaveContext.language];

                    pauseCtx->infoPanelVtx[17].v.tc[0] = pauseCtx->infoPanelVtx[19].v.tc[0] = 0x300;

                    pauseCtx->infoPanelVtx[21].v.tc[0] = pauseCtx->infoPanelVtx[23].v.tc[0] =
                        D_8082ADE8[gSaveContext.language] << 5;

                    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, aButtonColor.r, aButtonColor.g, aButtonColor.b, 255);
                    // gSPDisplayList(POLY_OPA_DISP++, gAButtonIconDL);
                    gDPLoadTextureBlock(POLY_OPA_DISP++, gABtnSymbolTex, G_IM_FMT_IA, G_IM_SIZ_8b, 24, 16, 0,
                                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 4, 4, G_TX_NOLOD,
                                        G_TX_NOLOD);
                    gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);

                    gDPPipeSync(POLY_OPA_DISP++);
                    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255);

                    POLY_OPA_DISP =
                        KaleidoScope_QuadTextureIA8(POLY_OPA_DISP, sPlayMelodyTextures[gSaveContext.language],
                                                    D_8082ADE8[gSaveContext.language], 16, 4);
                }
            } else if (pauseCtx->pageIndex == PAUSE_EQUIP) {
                pauseCtx->infoPanelVtx[16].v.ob[0] = pauseCtx->infoPanelVtx[18].v.ob[0] = WREG(64 + languageOffset);

                pauseCtx->infoPanelVtx[17].v.ob[0] = pauseCtx->infoPanelVtx[19].v.ob[0] =
                    pauseCtx->infoPanelVtx[16].v.ob[0] + 24;

                pauseCtx->infoPanelVtx[20].v.ob[0] = pauseCtx->infoPanelVtx[22].v.ob[0] =
                    pauseCtx->infoPanelVtx[16].v.ob[0] + WREG(52 + languageOffset);

                pauseCtx->infoPanelVtx[21].v.ob[0] = pauseCtx->infoPanelVtx[23].v.ob[0] =
                    pauseCtx->infoPanelVtx[20].v.ob[0] + D_8082ADD8[gSaveContext.language];

                pauseCtx->infoPanelVtx[17].v.tc[0] = pauseCtx->infoPanelVtx[19].v.tc[0] = 0x300;

                pauseCtx->infoPanelVtx[21].v.tc[0] = pauseCtx->infoPanelVtx[23].v.tc[0] =
                    D_8082ADD8[gSaveContext.language] << 5;

                // SOH [Bugfix] Page and column first: with cursorX 0 the macro would read gBitFlags[-1].
                if ((pauseCtx->pageIndex == PAUSE_EQUIP) && (pauseCtx->cursorX[PAUSE_EQUIP] != 0) &&
                    !(ExtEquip_GetPage() == 1) &&
                    !(CHECK_OWNED_EQUIP(pauseCtx->cursorY[PAUSE_EQUIP], pauseCtx->cursorX[PAUSE_EQUIP] - 1))) {
                    return;
                }

                // gSPDisplayList(POLY_OPA_DISP++, gAButtonIconDL);
                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, aButtonColor.r, aButtonColor.g, aButtonColor.b, 255);
                gDPLoadTextureBlock(POLY_OPA_DISP++, gABtnSymbolTex, G_IM_FMT_IA, G_IM_SIZ_8b, 24, 16, 0,
                                    G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 4, 4, G_TX_NOLOD,
                                    G_TX_NOLOD);
                gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);

                gDPPipeSync(POLY_OPA_DISP++);
                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255);

                POLY_OPA_DISP = KaleidoScope_QuadTextureIA8(POLY_OPA_DISP, sToEquipTextures[gSaveContext.language],
                                                            D_8082ADD8[gSaveContext.language], 16, 4);
            }
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}