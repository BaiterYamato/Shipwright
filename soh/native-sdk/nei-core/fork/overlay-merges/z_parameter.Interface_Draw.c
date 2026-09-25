/* overlay-merge 685dfedb0033 e365a318fb94 4211c9e48da9 */
void Interface_Draw(PlayState* play) {
    static s16 magicArrowEffectsR[] = { 255, 100, 255 };
    static s16 magicArrowEffectsG[] = { 0, 100, 255 };
    static s16 magicArrowEffectsB[] = { 0, 255, 100 };
    static s16 timerDigitLeftPos[] = { 16, 25, 34, 42, 51 };
    static s16 digitWidth[] = { 9, 9, 8, 9, 9 };
    // unused, most likely colors
    static s16 D_80125B1C[][3] = {
        { 0, 150, 0 }, { 100, 255, 0 }, { 255, 255, 255 }, { 0, 0, 0 }, { 255, 255, 255 },
    };
    static s16 rupeeDigitsFirst[] = { 1, 0, 0, 0 };
    static s16 rupeeDigitsCount[] = { 2, 3, 3, 3 };

    // courtesy of https://github.com/TestRunnerSRL/OoT-Randomizer/blob/Dev/ASM/c/hud_colors.c
    static Color_RGB8 rupeeWalletColors[4] = {
        { 0xC8, 0xFF, 0x64 }, // Base Wallet (Green)
        { 0x82, 0x82, 0xFF }, // Adult's Wallet (Blue)
        { 0xFF, 0x64, 0x64 }, // Giant's Wallet (Red)
        { 0xFF, 0x5A, 0xFF }, // Tycoon's Wallet (Purple). Only used in rando shopsanity.
    };
    Color_RGB8 rColor;

    Color_RGB8 keyCountColor = { 200, 230, 255 };
    if (CVarGetInteger(CVAR_COSMETIC("HUD.KeyCount.Changed"), 0)) {
        keyCountColor = CVarGetColor24(CVAR_COSMETIC("HUD.KeyCount.Value"), keyCountColor);
    }

    Color_RGB8 dPadColor = { 255, 255, 255 };
    if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.Changed"), 0)) {
        dPadColor = CVarGetColor24(CVAR_COSMETIC("HUD.Dpad.Value"), dPadColor);
    }

    Color_RGB8 aButtonColor = { 90, 90, 255 };
    if (CVarGetInteger(CVAR_COSMETIC("HUD.AButton.Changed"), 0)) {
        aButtonColor = CVarGetColor24(CVAR_COSMETIC("HUD.AButton.Value"), aButtonColor);
    } else if (CVarGetInteger(CVAR_COSMETIC("DefaultColorScheme"), COLORSCHEME_N64) == COLORSCHEME_GAMECUBE) {
        aButtonColor = (Color_RGB8){ 0, 200, 50 };
    }

    static s16 spoilingItemEntrances[] = { ENTR_LOST_WOODS_2, ENTR_ZORAS_DOMAIN_3, ENTR_ZORAS_DOMAIN_3 };
    static f32 D_80125B54[] = { -40.0f, -35.0f }; // unused
    static s16 D_80125B5C[] = { 91, 91 };         // unused
    static s16 sTimerNextSecondTimer;
    static s16 sTimerStateTimer;
    static s16 sSubTimerNextSecondTimer;
    static s16 sSubTimerStateTimer;
    static s16 timerDigits[5];
    InterfaceContext* interfaceCtx = &play->interfaceCtx;
    PauseContext* pauseCtx = &play->pauseCtx;
    MessageContext* msgCtx = &play->msgCtx;
    Player* player = GET_PLAYER(play);
    s16 svar1;
    s16 svar2;
    s16 svar3;
    s16 svar4;
    s16 svar5;
    s16 timerId;
    bool fullUi = !CVarGetInteger(CVAR_ENHANCEMENT("MinimalUI"), 0) || !R_MINIMAP_DISABLED || play->pauseCtx.state != 0;
    // #region SOH [NTSC]
    s32 languageOffset = gSaveContext.language;

    if (languageOffset == LANGUAGE_JPN) {
        languageOffset = LANGUAGE_ENG;
    }
    // #endregion

    if (GameInteractor_NoUIActive()) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);

    gSPSegment(OVERLAY_DISP++, 0x02, interfaceCtx->parameterSegment);
    gSPSegment(OVERLAY_DISP++, 0x07, interfaceCtx->doActionSegment);
    gSPSegment(OVERLAY_DISP++, 0x08, interfaceCtx->iconItemSegment);
    gSPSegment(OVERLAY_DISP++, 0x0B, interfaceCtx->mapSegment);

    if (pauseCtx->debugState == 0) {
        Interface_InitVertices(play);
        func_8008A994(interfaceCtx);
        // SM64 Mario mode: hide the OOT hearts. The SM64 power-meter HP dial
        // and the power-up panel are both drawn by the ImGui HUD (Sm64CapsHud.cpp)
        // — this call self-registers that GuiWindow on first use.
        if (CVarGetInteger("gSm64Mario", 0)) {
            Sm64CapsHud_DrawImGui();
        } else if (PikaMode_IsActive()) {
            // Pikachu MODE (system 2): hide the OOT hearts — HP, G-MAX and the
            // status chip live on the Pikachu ImGui HUD (pikachu_hud.cpp).
            PikachuHud_DrawImGui();
        } else if (fullUi || gSaveContext.health != gSaveContext.healthCapacity) {
            HealthMeter_Draw(play);
        }

        // Gerudo MHR Dual Blades HUD (wirebug pips + demon gauge). Self-gates on
        // the form and draws over the normal UI without replacing the hearts.
        GerudoHud_DrawImGui();

        // Dual Cane (Somaria / Pacci) radial skill wheel + placement hint
        // (soh/Enhancements/CaneWheelHud.cpp). Self-gates on the cane being held;
        // this call only self-registers the GuiWindow on first use.
        CaneWheelHud_DrawImGui();

        Gfx_SetupDL_39Overlay(play->state.gfxCtx);

        if (fullUi) {
            s16 PosX_RC;
            s16 PosY_RC;
            if (GameInteractor_Should(VB_RENDER_RUPEE_COUNTER, true)) {
                // Rupee Icon
                if (CVarGetInteger(CVAR_ENHANCEMENT("DynamicWalletIcon"), 0)) {
                    switch (CUR_UPG_VALUE(UPG_WALLET)) {
                        case 0:
                            if (CVarGetInteger(CVAR_COSMETIC("Consumable.GreenRupee.Changed"), 0)) {
                                rColor =
                                    CVarGetColor24(CVAR_COSMETIC("Consumable.GreenRupee.Value"), rupeeWalletColors[0]);
                            } else {
                                rColor = rupeeWalletColors[0];
                            }
                            break;
                        case 1:
                            if (CVarGetInteger(CVAR_COSMETIC("Consumable.BlueRupee.Changed"), 0)) {
                                rColor =
                                    CVarGetColor24(CVAR_COSMETIC("Consumable.BlueRupee.Value"), rupeeWalletColors[1]);
                            } else {
                                rColor = rupeeWalletColors[1];
                            }
                            break;
                        case 2:
                            if (CVarGetInteger(CVAR_COSMETIC("Consumable.RedRupee.Changed"), 0)) {
                                rColor =
                                    CVarGetColor24(CVAR_COSMETIC("Consumable.RedRupee.Value"), rupeeWalletColors[2]);
                            } else {
                                rColor = rupeeWalletColors[2];
                            }
                            break;
                        case 3:
                            if (CVarGetInteger(CVAR_COSMETIC("Consumable.PurpleRupee.Changed"), 0)) {
                                rColor =
                                    CVarGetColor24(CVAR_COSMETIC("Consumable.PurpleRupee.Value"), rupeeWalletColors[3]);
                            } else {
                                rColor = rupeeWalletColors[3];
                            }
                            break;
                    }
                } else {
                    if (CVarGetInteger(CVAR_COSMETIC("Consumable.GreenRupee.Changed"), rupeeWalletColors)) {
                        rColor = CVarGetColor24(CVAR_COSMETIC("Consumable.GreenRupee.Value"), rupeeWalletColors[0]);
                    } else {
                        rColor = rupeeWalletColors[0];
                    }
                }

                // Rupee icon & counter
                s16 X_Margins_RC;
                s16 Y_Margins_RC;
                if (CVarGetInteger(CVAR_COSMETIC("HUD.Rupees.UseMargins"), 0) != 0) {
                    if (CVarGetInteger(CVAR_COSMETIC("HUD.Rupees.PosType"), 0) == ORIGINAL_LOCATION) {
                        X_Margins_RC = Left_HUD_Margin;
                    };
                    Y_Margins_RC = Bottom_HUD_Margin;
                } else {
                    X_Margins_RC = 0;
                    Y_Margins_RC = 0;
                }
                s16 PosX_RC_ori = OTRGetRectDimensionFromLeftEdge(26 + X_Margins_RC);
                s16 PosY_RC_ori = 206 + Y_Margins_RC;
                if (CVarGetInteger(CVAR_COSMETIC("HUD.Rupees.PosType"), 0) != ORIGINAL_LOCATION) {
                    PosY_RC = CVarGetInteger(CVAR_COSMETIC("HUD.Rupees.PosY"), 0) + Y_Margins_RC;
                    if (CVarGetInteger(CVAR_COSMETIC("HUD.Rupees.PosType"), 0) == ANCHOR_LEFT) {
                        if (CVarGetInteger(CVAR_COSMETIC("HUD.Rupees.UseMargins"), 0) != 0) {
                            X_Margins_RC = Left_HUD_Margin;
                        };
                        PosX_RC = OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Rupees.PosX"), 0) +
                                                              X_Margins_RC);
                    } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Rupees.PosType"), 0) == ANCHOR_RIGHT) {
                        if (CVarGetInteger(CVAR_COSMETIC("HUD.Rupees.UseMargins"), 0) != 0) {
                            X_Margins_RC = Right_HUD_Margin;
                        };
                        PosX_RC = OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Rupees.PosX"), 0) +
                                                               X_Margins_RC);
                    } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Rupees.PosType"), 0) == ANCHOR_NONE) {
                        PosX_RC = CVarGetInteger(CVAR_COSMETIC("HUD.Rupees.PosX"), 0);
                    } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Rupees.PosType"), 0) == HIDDEN) {
                        PosX_RC = -9999;
                    }
                } else {
                    PosY_RC = PosY_RC_ori;
                    PosX_RC = PosX_RC_ori;
                }
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, rColor.r, rColor.g, rColor.b, interfaceCtx->magicAlpha);
                OVERLAY_DISP = Gfx_TextureIA8(OVERLAY_DISP, gRupeeCounterIconTex, 16, 16, PosX_RC, PosY_RC, 16, 16,
                                              1 << 10, 1 << 10);
            }

            if (GameInteractor_Should(VB_RENDER_KEY_COUNTER, true)) {
                switch (play->sceneNum) {
                    case SCENE_FOREST_TEMPLE:
                    case SCENE_FIRE_TEMPLE:
                    case SCENE_WATER_TEMPLE:
                    case SCENE_SPIRIT_TEMPLE:
                    case SCENE_SHADOW_TEMPLE:
                    case SCENE_BOTTOM_OF_THE_WELL:
                    case SCENE_ICE_CAVERN:
                    case SCENE_GANONS_TOWER:
                    case SCENE_GERUDO_TRAINING_GROUND:
                    case SCENE_THIEVES_HIDEOUT:
                    case SCENE_INSIDE_GANONS_CASTLE:
                    case SCENE_GANONS_TOWER_COLLAPSE_INTERIOR:
                    case SCENE_INSIDE_GANONS_CASTLE_COLLAPSE:
                    case SCENE_TREASURE_BOX_SHOP:
                        if (gSaveContext.inventory.dungeonKeys[gSaveContext.mapIndex] >= 0) {
                            s16 X_Margins_SKC;
                            s16 Y_Margins_SKC;
                            if (CVarGetInteger(CVAR_COSMETIC("HUD.SmallKey.UseMargins"), 0) != 0) {
                                if (CVarGetInteger(CVAR_COSMETIC("HUD.SmallKey.PosType"), 0) == ORIGINAL_LOCATION) {
                                    X_Margins_SKC = Left_HUD_Margin;
                                };
                                Y_Margins_SKC = Bottom_HUD_Margin;
                            } else {
                                X_Margins_SKC = 0;
                                Y_Margins_SKC = 0;
                            }
                            s16 PosX_SKC_ori = OTRGetRectDimensionFromLeftEdge(26 + X_Margins_SKC);
                            s16 PosY_SKC_ori = 190 + Y_Margins_SKC;
                            s16 PosX_SKC;
                            s16 PosY_SKC;
                            if (CVarGetInteger(CVAR_COSMETIC("HUD.SmallKey.PosType"), 0) != ORIGINAL_LOCATION) {
                                PosY_SKC = CVarGetInteger(CVAR_COSMETIC("HUD.SmallKey.PosY"), 0) + Y_Margins_SKC;
                                if (CVarGetInteger(CVAR_COSMETIC("HUD.SmallKey.PosType"), 0) == ANCHOR_LEFT) {
                                    if (CVarGetInteger(CVAR_COSMETIC("HUD.SmallKey.UseMargins"), 0) != 0) {
                                        X_Margins_SKC = Left_HUD_Margin;
                                    };
                                    PosX_SKC = OTRGetDimensionFromLeftEdge(
                                        CVarGetInteger(CVAR_COSMETIC("HUD.SmallKey.PosX"), 0) + X_Margins_SKC);
                                } else if (CVarGetInteger(CVAR_COSMETIC("HUD.SmallKey.PosType"), 0) == ANCHOR_RIGHT) {
                                    if (CVarGetInteger(CVAR_COSMETIC("HUD.SmallKey.UseMargins"), 0) != 0) {
                                        X_Margins_SKC = Right_HUD_Margin;
                                    };
                                    PosX_SKC = OTRGetDimensionFromRightEdge(
                                        CVarGetInteger(CVAR_COSMETIC("HUD.SmallKey.PosX"), 0) + X_Margins_SKC);
                                } else if (CVarGetInteger(CVAR_COSMETIC("HUD.SmallKey.PosType"), 0) == ANCHOR_NONE) {
                                    PosX_SKC = CVarGetInteger(CVAR_COSMETIC("HUD.SmallKey.PosX"), 0);
                                } else if (CVarGetInteger(CVAR_COSMETIC("HUD.SmallKey.PosType"), 0) == HIDDEN) {
                                    PosX_SKC = -9999;
                                }
                            } else {
                                PosY_SKC = PosY_SKC_ori;
                                PosX_SKC = PosX_SKC_ori;
                            }
                            // Small Key Icon
                            gDPPipeSync(OVERLAY_DISP++);

                            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, keyCountColor.r, keyCountColor.g, keyCountColor.b,
                                            interfaceCtx->magicAlpha);
                            gDPSetEnvColor(OVERLAY_DISP++, 0, 0, 20,
                                           255); // We reset this here so it match user color :)
                            OVERLAY_DISP = Gfx_TextureIA8(OVERLAY_DISP, gSmallKeyCounterIconTex, 16, 16, PosX_SKC,
                                                          PosY_SKC, 16, 16, 1 << 10, 1 << 10);

                            // Small Key Counter
                            gDPPipeSync(OVERLAY_DISP++);
                            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, interfaceCtx->magicAlpha);
                            gDPSetCombineLERP(OVERLAY_DISP++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0,
                                              PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);

                            interfaceCtx->counterDigits[2] = 0;
                            interfaceCtx->counterDigits[3] = gSaveContext.inventory.dungeonKeys[gSaveContext.mapIndex];

                            while (interfaceCtx->counterDigits[3] >= 10) {
                                interfaceCtx->counterDigits[2]++;
                                interfaceCtx->counterDigits[3] -= 10;
                            }

                            svar3 = 16;
                            if (interfaceCtx->counterDigits[2] != 0) {
                                OVERLAY_DISP = Gfx_TextureI8(
                                    OVERLAY_DISP, ((u8*)((u8*)digitTextures[interfaceCtx->counterDigits[2]])), 8, 16,
                                    PosX_SKC + 16, PosY_SKC, 8, 16, 1 << 10, 1 << 10);
                                svar3 = 24;
                            }

                            OVERLAY_DISP =
                                Gfx_TextureI8(OVERLAY_DISP, ((u8*)digitTextures[interfaceCtx->counterDigits[3]]), 8, 16,
                                              PosX_SKC + svar3, PosY_SKC, 8, 16, 1 << 10, 1 << 10);
                        }
                        break;
                    default:
                        break;
                }
            }

            if (GameInteractor_Should(VB_RENDER_RUPEE_COUNTER, true)) {
                // Rupee Counter
                gDPPipeSync(OVERLAY_DISP++);

                if (gSaveContext.rupees == CUR_CAPACITY(UPG_WALLET)) {
                    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 120, 255, 0, interfaceCtx->magicAlpha);
                } else if (gSaveContext.rupees != 0) {
                    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, interfaceCtx->magicAlpha);
                } else {
                    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 100, 100, 100, interfaceCtx->magicAlpha);
                }

                gDPSetCombineLERP(OVERLAY_DISP++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE,
                                  TEXEL0, 0, PRIMITIVE, 0);

                interfaceCtx->counterDigits[0] = interfaceCtx->counterDigits[1] = 0;
                interfaceCtx->counterDigits[2] = gSaveContext.rupees;

                if ((interfaceCtx->counterDigits[2] > 9999) || (interfaceCtx->counterDigits[2] < 0)) {
                    interfaceCtx->counterDigits[2] &= 0xDDD;
                }

                while (interfaceCtx->counterDigits[2] >= 100) {
                    interfaceCtx->counterDigits[0]++;
                    interfaceCtx->counterDigits[2] -= 100;
                }

                while (interfaceCtx->counterDigits[2] >= 10) {
                    interfaceCtx->counterDigits[1]++;
                    interfaceCtx->counterDigits[2] -= 10;
                }

                svar2 = rupeeDigitsFirst[CUR_UPG_VALUE(UPG_WALLET)];
                svar5 = rupeeDigitsCount[CUR_UPG_VALUE(UPG_WALLET)];

                for (svar1 = 0, svar3 = 16; svar1 < svar5; svar1++, svar2++, svar3 += 8) {
                    OVERLAY_DISP = Gfx_TextureI8(OVERLAY_DISP, ((u8*)digitTextures[interfaceCtx->counterDigits[svar2]]),
                                                 8, 16, PosX_RC + svar3, PosY_RC, 8, 16, 1 << 10, 1 << 10);
                }
            }
        } else {
            // Make sure item counts have black backgrounds
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 0, 0, 0, interfaceCtx->magicAlpha);
            gDPSetEnvColor(OVERLAY_DISP++, 0, 0, 0, 0);
        }

        if (CVarGetInteger(CVAR_ENHANCEMENT("DrawLineupTick"), 0)) {
            Interface_DrawLineupTick(play);
        }

        // SM64 Mario mode: magic is disabled — hide the magic bar.
        // Pikachu MODE: the G-MAX bar on the Pikachu card replaces it.
        if ((fullUi || gSaveContext.magicState > MAGIC_STATE_IDLE) && !CVarGetInteger("gSm64Mario", 0) &&
            !PikaMode_IsActive()) {
            Interface_DrawMagicBar(play);
        }

        // Gerudo Dual Blades: the rage meter, one row under the magic bar. Skijer's NEI
        if (fullUi && !PikaMode_IsActive()) {
            GerudoMhr_DrawRageMeter(play);
        }

        // Pikachu MODE: hide the minimap (Pokemon-style clean mix; it also
        // collides with the corner-HUD cluster).
        if (!PikaMode_IsActive()) {
            Minimap_Draw(play);
        }

        if ((R_PAUSE_MENU_MODE != 2) && (R_PAUSE_MENU_MODE != 3)) {
            if (CVarGetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 0)) {
                gSPMatrix(OVERLAY_DISP++, interfaceCtx->view.projectionFlippedPtr,
                          G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
            }
            Attention_Draw(&play->actorCtx.targetCtx, play); // Draw Z-Target
            if (CVarGetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 0)) {
                gSPMatrix(OVERLAY_DISP++, interfaceCtx->view.projectionPtr,
                          G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
            }

            // Render enemy health bar after Z-target to leverage set variables
            if (CVarGetInteger(CVAR_ENHANCEMENT("EnemyHealthBar"), 0)) {
                Interface_DrawEnemyHealthBar(&play->actorCtx.targetCtx, play);
            }
        }

        Gfx_SetupDL_39Overlay(play->state.gfxCtx);

        if (fullUi) {
            // SM64 Mario mode: hide the B button (the sword) + the C-buttons
            // (they're Mario moves now) by zeroing their alpha — drops backgrounds
            // and icons. A is KEPT: its do-action label (Speak/Check/Open) is the
            // contextual-interact indicator the player presses B to act on.
            // Recomputed each frame, so re-zeroing here is safe.
            if (CVarGetInteger("gSm64Mario", 0)) {
                interfaceCtx->bAlpha = 0;
                interfaceCtx->cLeftAlpha = 0;
                interfaceCtx->cDownAlpha = 0;
                interfaceCtx->cRightAlpha = 0;
            }
            // Pikachu MODE, corner-HUD style: the sticker clusters replace ALL
            // OOT buttons — zero every button alpha (frames, icons, ammo and
            // the A button all key off these). Overlay style (0) keeps them.
            if (PikaMode_IsActive() && PikaMode_HudStyle() == 1) {
                interfaceCtx->bAlpha = 0;
                interfaceCtx->aAlpha = 0;
                interfaceCtx->cLeftAlpha = 0;
                interfaceCtx->cDownAlpha = 0;
                interfaceCtx->cRightAlpha = 0;
                interfaceCtx->dpadUpAlpha = 0;
                interfaceCtx->dpadDownAlpha = 0;
                interfaceCtx->dpadLeftAlpha = 0;
                interfaceCtx->dpadRightAlpha = 0;
            }
            Interface_DrawItemButtons(play);
        }

        gDPPipeSync(OVERLAY_DISP++);
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, interfaceCtx->bAlpha);
        gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);

        if (!(interfaceCtx->unk_1FA)) {
            // B Button Icon & Ammo Count
            if (gSaveContext.equips.buttonItems[0] != ITEM_NONE) {
                if (fullUi && !TransformMasks_IsTransformed()) {
                    Interface_DrawItemIconTexture(play,
                                                  PikaMode_ButtonIcon(0, ExtInv_GetItemIcon(ExtButton_GetItem(0))), 0);
                }

                if (PLAYER_ON_REAL_HORSE(player) || (play->shootingGalleryStatus > 1) ||
                    ((play->sceneNum == SCENE_BOMBCHU_BOWLING_ALLEY) && Flags_GetSwitch(play, 0x38))) {

                    if (!fullUi && !TransformMasks_IsTransformed()) {
                        Interface_DrawItemIconTexture(
                            play, PikaMode_ButtonIcon(0, ExtInv_GetItemIcon(ExtButton_GetItem(0))), 0);
                    }

                    gDPPipeSync(OVERLAY_DISP++);
                    gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE,
                                      0, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
                    Interface_DrawAmmoCount(play, 0, interfaceCtx->bAlpha);
                }
            }
        } else {
            // B Button Do Action Label
            s16 PosX_adjust;
            s16 PosY_adjust;
            if (gSaveContext.language == LANGUAGE_FRA) {
                PosX_adjust = -12;
                PosY_adjust = 5;
            } else if (gSaveContext.language == LANGUAGE_GER) {
                PosY_adjust = 6;
                PosX_adjust = -9;
            } else {
                PosY_adjust = 6;
                PosX_adjust = -10;
            }

            s16 BbtnPosX;
            s16 BbtnPosY;
            s16 X_Margins_BtnB_label;
            s16 Y_Margins_BtnB_label;
            if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.UseMargins"), 0) != 0) {
                if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosType"), 0) == ORIGINAL_LOCATION) {
                    X_Margins_BtnB_label = Right_HUD_Margin;
                };
                Y_Margins_BtnB_label = (Top_HUD_Margin * -1);
            } else {
                X_Margins_BtnB_label = 0;
                Y_Margins_BtnB_label = 0;
            }
            if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosType"), 0) != ORIGINAL_LOCATION) {
                BbtnPosY = CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosY"), 0) + Y_Margins_BtnB_label + PosY_adjust;
                if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosType"), 0) == ANCHOR_LEFT) {
                    if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.UseMargins"), 0) != 0) {
                        X_Margins_BtnB_label = Left_HUD_Margin;
                    };
                    BbtnPosX = OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosX"), 0) +
                                                           X_Margins_BtnB_label + PosX_adjust);
                } else if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosType"), 0) == ANCHOR_RIGHT) {
                    if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.UseMargins"), 0) != 0) {
                        X_Margins_BtnB_label = Right_HUD_Margin;
                    };
                    BbtnPosX = OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosX"), 0) +
                                                            X_Margins_BtnB_label + PosX_adjust);
                } else if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosType"), 0) == ANCHOR_NONE) {
                    BbtnPosX = CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosX"), 0) + PosX_adjust;
                } else if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosType"), 0) == HIDDEN) {
                    BbtnPosX = -9999;
                }
            } else {
                BbtnPosX = OTRGetRectDimensionFromRightEdge(R_B_LABEL_X(languageOffset) + X_Margins_BtnB_label);
                BbtnPosY = R_B_LABEL_Y(languageOffset) + Y_Margins_BtnB_label;
            }
            gDPPipeSync(OVERLAY_DISP++);
            gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                              PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, interfaceCtx->bAlpha);

            gDPLoadTextureBlock_4b(OVERLAY_DISP++, interfaceCtx->doActionSegment[1], G_IM_FMT_IA, DO_ACTION_TEX_WIDTH(),
                                   DO_ACTION_TEX_HEIGHT(), 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP,
                                   G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);

            R_B_LABEL_DD = (1 << 10) / (WREG(37 + languageOffset) / 100.0f);
            gSPWideTextureRectangle(OVERLAY_DISP++, BbtnPosX << 2, BbtnPosY << 2,
                                    (BbtnPosX + DO_ACTION_TEX_WIDTH()) << 2, (BbtnPosY + DO_ACTION_TEX_HEIGHT()) << 2,
                                    G_TX_RENDERTILE, 0, 0, R_B_LABEL_DD, R_B_LABEL_DD);
        }

        gDPPipeSync(OVERLAY_DISP++);

        // C-Left Button Icon & Ammo Count
        // SM64 Mario mode: C-buttons become moves (spin/dash), not items — hide the icon.
        if (gSaveContext.equips.buttonItems[1] < ITEM_LAST_USED && !LinkSpan_ItemButtonHidden(1) &&
            !CVarGetInteger("gSm64Mario", 0)) {
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, interfaceCtx->cLeftAlpha);
            gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
            Interface_DrawItemIconTexture(play, PikaMode_ButtonIcon(1, ExtInv_GetItemIcon(ExtButton_GetItem(1))), 1);
            gDPPipeSync(OVERLAY_DISP++);
            gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                              PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
            Interface_DrawAmmoCount(play, 1, interfaceCtx->cLeftAlpha);
        }

        gDPPipeSync(OVERLAY_DISP++);

        // C-Down Button Icon & Ammo Count
        if (gSaveContext.equips.buttonItems[2] < ITEM_LAST_USED && !LinkSpan_ItemButtonHidden(2) &&
            !CVarGetInteger("gSm64Mario", 0)) {
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, interfaceCtx->cDownAlpha);
            gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
            Interface_DrawItemIconTexture(play, PikaMode_ButtonIcon(2, ExtInv_GetItemIcon(ExtButton_GetItem(2))), 2);
            gDPPipeSync(OVERLAY_DISP++);
            gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                              PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
            Interface_DrawAmmoCount(play, 2, interfaceCtx->cDownAlpha);
        }

        gDPPipeSync(OVERLAY_DISP++);

        // C-Right Button Icon & Ammo Count
        if (gSaveContext.equips.buttonItems[3] < ITEM_LAST_USED && !LinkSpan_ItemButtonHidden(3) &&
            !CVarGetInteger("gSm64Mario", 0)) {
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, interfaceCtx->cRightAlpha);
            gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
            Interface_DrawItemIconTexture(play, PikaMode_ButtonIcon(3, ExtInv_GetItemIcon(ExtButton_GetItem(3))), 3);
            gDPPipeSync(OVERLAY_DISP++);
            gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                              PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
            Interface_DrawAmmoCount(play, 3, interfaceCtx->cRightAlpha);
        }

        // Link-Span (OOT-MOVE-006): um provider nativo pode tomar o D-pad do HUD mesmo sem DpadEquips.
        // SM64 Mario mode: hide the whole D-pad HUD (cross + equipped items). Skijer's NEI
        if ((CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0) != 0 || LinkSpan_DpadHudOwned()) &&
            !CVarGetInteger("gSm64Mario", 0)) {
            // DPad is only greyed-out when all 4 DPad directions are too
            uint16_t dpadAlpha =
                MAX(MAX(MAX(interfaceCtx->dpadUpAlpha, interfaceCtx->dpadDownAlpha), interfaceCtx->dpadLeftAlpha),
                    interfaceCtx->dpadRightAlpha);

            // Draw DPad
            s16 DpadPosX;
            s16 DpadPosY;
            s16 X_Margins_Dpad;
            s16 Y_Margins_Dpad;
            if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.UseMargins"), 0) != 0) {
                if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0) == ORIGINAL_LOCATION) {
                    X_Margins_Dpad = Right_HUD_Margin;
                };
                Y_Margins_Dpad = (Top_HUD_Margin * -1);
            } else {
                Y_Margins_Dpad = 0;
                X_Margins_Dpad = 0;
            }
            if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0) != ORIGINAL_LOCATION) {
                DpadPosY = CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosY"), 0) + Y_Margins_Dpad;
                if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0) == ANCHOR_LEFT) {
                    if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.UseMargins"), 0) != 0) {
                        X_Margins_Dpad = Left_HUD_Margin;
                    };
                    DpadPosX =
                        OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) + X_Margins_Dpad);
                } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0) == ANCHOR_RIGHT) {
                    if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.UseMargins"), 0) != 0) {
                        X_Margins_Dpad = Right_HUD_Margin;
                    };
                    DpadPosX = OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) +
                                                            X_Margins_Dpad);
                } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0) == ANCHOR_NONE) {
                    DpadPosX = CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0);
                } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0) == HIDDEN) {
                    DpadPosX = -9999;
                }
            } else {
                DpadPosX = OTRGetRectDimensionFromRightEdge(DPAD_X + X_Margins_Dpad);
                DpadPosY = DPAD_Y + Y_Margins_Dpad;
            }

            gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, dPadColor.r, dPadColor.g, dPadColor.b, dpadAlpha);
            // Link-Span (OOT-MOVE-006): o HUD Lua é emitido antes deste Interface_Draw. Com o D-pad tomado, o
            // fundo só é capturado aqui e sai no hook do HUD, antes dos ícones do provider, para não cobri-los.
            s32 linkSpanDpad = LinkSpan_DpadHudOwned();
            if (fullUi && linkSpanDpad) {
                LinkSpan_CaptureDpadBackground(play, DpadPosX, DpadPosY, dPadColor.r, dPadColor.g, dPadColor.b,
                                               dpadAlpha);
            }
            if (fullUi && !linkSpanDpad) {
                gDPLoadTextureBlock(OVERLAY_DISP++, gDPadTex, G_IM_FMT_IA, G_IM_SIZ_16b, 32, 32, 0,
                                    G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                    G_TX_NOLOD, G_TX_NOLOD);
                gSPWideTextureRectangle(OVERLAY_DISP++, DpadPosX << 2, DpadPosY << 2, (DpadPosX + 32) << 2,
                                        (DpadPosY + 32) << 2, G_TX_RENDERTILE, 0, 0, (1 << 10), (1 << 10));
            }

            // Link-Span (OOT-MOVE-006): com o D-pad tomado, os ícones dos itens do D-pad dão lugar aos do
            // provider, que só precisa da posição de cada direção.
            if (linkSpanDpad) {
                for (s16 dpadButton = 4; dpadButton < 8; dpadButton++) {
                    Interface_DrawItemIconTexture(play, NULL, dpadButton);
                }
            }

            // DPad-Up Button Icon & Ammo Count
            if (gSaveContext.equips.buttonItems[4] < ITEM_LAST_USED && !linkSpanDpad) {
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, interfaceCtx->dpadUpAlpha);
                gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
                Interface_DrawItemIconTexture(play, PikaMode_ButtonIcon(4, ExtInv_GetItemIcon(ExtButton_GetItem(4))),
                                              4);
                gDPPipeSync(OVERLAY_DISP++);
                gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                                  PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
                Interface_DrawAmmoCount(play, 4, interfaceCtx->dpadUpAlpha);
            }

            // DPad-Down Button Icon & Ammo Count
            if (gSaveContext.equips.buttonItems[5] < ITEM_LAST_USED && !linkSpanDpad) {
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, interfaceCtx->dpadDownAlpha);
                gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
                Interface_DrawItemIconTexture(play, PikaMode_ButtonIcon(5, ExtInv_GetItemIcon(ExtButton_GetItem(5))),
                                              5);
                gDPPipeSync(OVERLAY_DISP++);
                gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                                  PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
                Interface_DrawAmmoCount(play, 5, interfaceCtx->dpadDownAlpha);
            }

            // DPad-Left Button Icon & Ammo Count
            if (gSaveContext.equips.buttonItems[6] < ITEM_LAST_USED && !linkSpanDpad) {
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, interfaceCtx->dpadLeftAlpha);
                gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
                Interface_DrawItemIconTexture(play, PikaMode_ButtonIcon(6, ExtInv_GetItemIcon(ExtButton_GetItem(6))),
                                              6);
                gDPPipeSync(OVERLAY_DISP++);
                gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                                  PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
                Interface_DrawAmmoCount(play, 6, interfaceCtx->dpadLeftAlpha);
            }

            // DPad-Right Button Icon & Ammo Count
            if (gSaveContext.equips.buttonItems[7] < ITEM_LAST_USED && !linkSpanDpad) {
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, interfaceCtx->dpadRightAlpha);
                gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
                Interface_DrawItemIconTexture(play, PikaMode_ButtonIcon(7, ExtInv_GetItemIcon(ExtButton_GetItem(7))),
                                              7);
                gDPPipeSync(OVERLAY_DISP++);
                gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                                  PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
                Interface_DrawAmmoCount(play, 7, interfaceCtx->dpadRightAlpha);
            }

            // SM64 Mario mode cap icons used to be overdrawn on the D-pad
            // slots here; they now live in the dedicated power-up timer panel
            // (Sm64MarioCapsHud_Draw, top-right) so the overdraw was removed.
        }

        // A Button
        Gfx_SetupDL_42Overlay(play->state.gfxCtx);
        s16 X_Margins_BtnA;
        s16 Y_Margins_BtnA;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.AButton.UseMargins"), 0) != 0) {
            X_Margins_BtnA = Right_HUD_Margin;
            Y_Margins_BtnA = (Top_HUD_Margin * -1);
        } else {
            X_Margins_BtnA = 0;
            Y_Margins_BtnA = 0;
        }
        s16 PosX_BtnA_ori = OTRGetDimensionFromRightEdge(R_A_BTN_X + X_Margins_BtnA);
        s16 PosY_BtnA_ori = R_A_BTN_Y + Y_Margins_BtnA;
        const f32 rAIconX_ori = OTRGetDimensionFromRightEdge(R_A_ICON_X + X_Margins_BtnA);
        const f32 rAIconY_ori = 98.0f - (R_A_ICON_Y + Y_Margins_BtnA);
        s16 PosX_BtnA;
        s16 PosY_BtnA;
        s16 rAIconX;
        s16 rAIconY;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.AButton.PosType"), 0) != ORIGINAL_LOCATION) {
            PosY_BtnA = CVarGetInteger(CVAR_COSMETIC("HUD.AButton.PosY"), 0) + Y_Margins_BtnA;
            rAIconY = 98.0f - PosY_BtnA;
            if (CVarGetInteger(CVAR_COSMETIC("HUD.AButton.PosType"), 0) == ANCHOR_LEFT) {
                if (CVarGetInteger(CVAR_COSMETIC("HUD.AButton.UseMargins"), 0) != 0) {
                    X_Margins_BtnA = Left_HUD_Margin;
                };
                PosX_BtnA =
                    OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.AButton.PosX"), 0) + X_Margins_BtnA);
                rAIconX =
                    OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.AButton.PosX"), 0) + X_Margins_BtnA);
            } else if (CVarGetInteger(CVAR_COSMETIC("HUD.AButton.PosType"), 0) == ANCHOR_RIGHT) {
                if (CVarGetInteger(CVAR_COSMETIC("HUD.AButton.UseMargins"), 0) != 0) {
                    X_Margins_BtnA = Right_HUD_Margin;
                };
                PosX_BtnA =
                    OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.AButton.PosX"), 0) + X_Margins_BtnA);
                rAIconX =
                    OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.AButton.PosX"), 0) + X_Margins_BtnA);
            } else if (CVarGetInteger(CVAR_COSMETIC("HUD.AButton.PosType"), 0) == ANCHOR_NONE) {
                PosX_BtnA = CVarGetInteger(CVAR_COSMETIC("HUD.AButton.PosX"), 0);
                rAIconX = CVarGetInteger(CVAR_COSMETIC("HUD.AButton.PosX"), 0);
            } else if (CVarGetInteger(CVAR_COSMETIC("HUD.AButton.PosType"), 0) == HIDDEN) {
                PosX_BtnA = -9999;
                rAIconX = -9999;
            }
        } else {
            PosY_BtnA = PosY_BtnA_ori;
            PosX_BtnA = PosX_BtnA_ori;
            rAIconY = rAIconY_ori;
            rAIconX = rAIconX_ori;
        }
        gSPClearGeometryMode(OVERLAY_DISP++, G_CULL_BOTH);
        gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, aButtonColor.r, aButtonColor.g, aButtonColor.b, interfaceCtx->aAlpha);
        if (fullUi) {
            Interface_DrawActionButton(play, PosX_BtnA, PosY_BtnA);
        }
        gDPPipeSync(OVERLAY_DISP++);
        gSPSetGeometryMode(OVERLAY_DISP++, G_CULL_BACK);
        gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                          PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, interfaceCtx->aAlpha);
        gDPSetEnvColor(OVERLAY_DISP++, 0, 0, 0, 0);
        Matrix_Translate(-138.0f + rAIconX, rAIconY, WREG(46 + languageOffset) / 10.0f, MTXMODE_NEW);
        Matrix_Scale(1.0f, 1.0f, 1.0f, MTXMODE_APPLY);
        Matrix_RotateX(interfaceCtx->unk_1F4 / 10000.0f, MTXMODE_APPLY);
        gSPMatrix(OVERLAY_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
        gSPVertex(OVERLAY_DISP++, &interfaceCtx->actionVtx[4], 4, 0);

        if ((interfaceCtx->unk_1EC < 2) || (interfaceCtx->unk_1EC == 3)) {
            Interface_DrawActionLabel(play->state.gfxCtx, interfaceCtx->doActionSegment[0]);
        } else {
            Interface_DrawActionLabel(play->state.gfxCtx, interfaceCtx->doActionSegment[1]);
        }

        gDPPipeSync(OVERLAY_DISP++);

        func_8008A994(interfaceCtx);
        svar3 = 16;

        if ((pauseCtx->state == 6) && (pauseCtx->unk_1E4 == 3)) {
            // Inventory Equip Effects
            gSPSegment(OVERLAY_DISP++, 0x08, pauseCtx->iconItemSegment);
            Gfx_SetupDL_42Overlay(play->state.gfxCtx);
            gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
            gSPMatrix(OVERLAY_DISP++, &gMtxClear, G_MTX_MODELVIEW | G_MTX_LOAD);

            pauseCtx->cursorVtx[svar3].v.ob[0] = pauseCtx->cursorVtx[18].v.ob[0] = svar2 = pauseCtx->equipAnimX / 10;
            pauseCtx->cursorVtx[17].v.ob[0] = pauseCtx->cursorVtx[19].v.ob[0] = svar2 =
                pauseCtx->cursorVtx[svar3].v.ob[0] + WREG(90) / 10;
            pauseCtx->cursorVtx[svar3].v.ob[1] = pauseCtx->cursorVtx[17].v.ob[1] = svar2 = pauseCtx->equipAnimY / 10;
            pauseCtx->cursorVtx[18].v.ob[1] = pauseCtx->cursorVtx[19].v.ob[1] = svar2 =
                pauseCtx->cursorVtx[svar3].v.ob[1] - WREG(90) / 10;

            if (pauseCtx->equipTargetItem < 0xBF) {
                // Normal Equip (icon goes from the inventory slot to the C button when equipping it)
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, pauseCtx->equipAnimAlpha);
                gSPVertex(OVERLAY_DISP++, &pauseCtx->cursorVtx[16], 4, 0);

                gDPLoadTextureBlock(OVERLAY_DISP++, ExtInv_GetItemIcon(pauseCtx->equipTargetItem), G_IM_FMT_RGBA,
                                    G_IM_SIZ_32b, 32, 32, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP,
                                    G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            } else {
                // Magic Arrow Equip Effect
                svar1 = pauseCtx->equipTargetItem - 0xBF;
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, magicArrowEffectsR[svar1], magicArrowEffectsG[svar1],
                                magicArrowEffectsB[svar1], pauseCtx->equipAnimAlpha);

                if ((pauseCtx->equipAnimAlpha > 0) && (pauseCtx->equipAnimAlpha < 255)) {
                    svar1 = (pauseCtx->equipAnimAlpha / 8) / 2;
                    pauseCtx->cursorVtx[16].v.ob[0] = pauseCtx->cursorVtx[18].v.ob[0] = svar2 =
                        pauseCtx->cursorVtx[16].v.ob[0] - svar1;
                    pauseCtx->cursorVtx[17].v.ob[0] = pauseCtx->cursorVtx[19].v.ob[0] = svar2 =
                        pauseCtx->cursorVtx[16].v.ob[0] + svar1 * 2 + 32;
                    pauseCtx->cursorVtx[16].v.ob[1] = pauseCtx->cursorVtx[17].v.ob[1] = svar2 =
                        pauseCtx->cursorVtx[16].v.ob[1] + svar1;
                    pauseCtx->cursorVtx[18].v.ob[1] = pauseCtx->cursorVtx[19].v.ob[1] = svar2 =
                        pauseCtx->cursorVtx[16].v.ob[1] - svar1 * 2 - 32;
                }

                gSPVertex(OVERLAY_DISP++, &pauseCtx->cursorVtx[16], 4, 0);
                gDPLoadTextureBlock(OVERLAY_DISP++, gMagicArrowEquipEffectTex, G_IM_FMT_IA, G_IM_SIZ_8b, 32, 32, 0,
                                    G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                    G_TX_NOLOD, G_TX_NOLOD);
            }

            gSP1Quadrangle(OVERLAY_DISP++, 0, 2, 3, 1, 0);
        }

        Gfx_SetupDL_39Overlay(play->state.gfxCtx);

        if ((play->pauseCtx.state == 0) && (play->pauseCtx.debugState == 0)) {
            if (gSaveContext.minigameState != 1) {
                // Carrots rendering if the action corresponds to riding a horse
                if (interfaceCtx->unk_1EE == 8 && !MasterCycle_IsRiding() &&
                    GameInteractor_Should(VB_DRAW_EPONA_BOOST_CARROTS, true)) {
                    // Load Carrot Icon
                    gDPLoadTextureBlock(OVERLAY_DISP++, gCarrotIconTex, G_IM_FMT_RGBA, G_IM_SIZ_32b, 16, 16, 0,
                                        G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                        G_TX_NOLOD, G_TX_NOLOD);

                    // Draw 6 carrots
                    s16 CarrotsPosX = ZREG(14);
                    s16 CarrotsPosY = ZREG(15);
                    s16 CarrotsMargins_X = 0;
                    if (CVarGetInteger(CVAR_COSMETIC("HUD.Carrots.PosType"), 0) != ORIGINAL_LOCATION) {
                        CarrotsPosY = CVarGetInteger(CVAR_COSMETIC("HUD.Carrots.PosY"), 0);
                        if (CVarGetInteger(CVAR_COSMETIC("HUD.Carrots.PosType"), 0) == ANCHOR_LEFT) {
                            if (CVarGetInteger(CVAR_COSMETIC("HUD.Carrots.UseMargins"), 0) != 0) {
                                CarrotsMargins_X = Left_HUD_Margin;
                            };
                            CarrotsPosX = OTRGetDimensionFromLeftEdge(
                                CVarGetInteger(CVAR_COSMETIC("HUD.Carrots.PosX"), 0) + CarrotsMargins_X);
                        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Carrots.PosType"), 0) == ANCHOR_RIGHT) {
                            if (CVarGetInteger(CVAR_COSMETIC("HUD.Carrots.UseMargins"), 0) != 0) {
                                CarrotsMargins_X = Right_HUD_Margin;
                            };
                            CarrotsPosX = OTRGetDimensionFromRightEdge(
                                CVarGetInteger(CVAR_COSMETIC("HUD.Carrots.PosX"), 0) + CarrotsMargins_X);
                        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Carrots.PosType"), 0) == ANCHOR_NONE) {
                            CarrotsPosX = CVarGetInteger(CVAR_COSMETIC("HUD.Carrots.PosX"), 0);
                        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Carrots.PosType"), 0) == HIDDEN) {
                            CarrotsPosX = -9999;
                        }
                    }
                    for (svar1 = 1, svar5 = CarrotsPosX; svar1 < 7; svar1++, svar5 += 16) {
                        // Carrot Color (based on availability)
                        if ((interfaceCtx->numHorseBoosts == 0) || (interfaceCtx->numHorseBoosts < svar1)) {
                            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 0, 150, 255, interfaceCtx->aAlpha);
                        } else {
                            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, interfaceCtx->aAlpha);
                        }

                        gSPWideTextureRectangle(OVERLAY_DISP++, svar5 << 2, CarrotsPosY << 2, (svar5 + 16) << 2,
                                                (CarrotsPosY + 16) << 2, G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);
                    }
                }
            } else {
                // Score for the Horseback Archery
                s32 X_Margins_Archery;
                if (CVarGetInteger(CVAR_COSMETIC("HUD.ArcheryScore.UseMargins"), 0) != 0) {
                    if (CVarGetInteger(CVAR_COSMETIC("HUD.ArcheryScore.PosType"), 0) == ORIGINAL_LOCATION) {
                        X_Margins_Archery = Right_HUD_Margin;
                    };
                } else {
                    X_Margins_Archery = 0;
                }
                s16 ArcheryPos_Y = ZREG(15);
                s16 ArcheryPos_X = OTRGetRectDimensionFromRightEdge(WREG(32) + X_Margins_Archery);

                if (CVarGetInteger(CVAR_COSMETIC("HUD.ArcheryScore.PosType"), 0) != ORIGINAL_LOCATION) {
                    ArcheryPos_Y = CVarGetInteger(CVAR_COSMETIC("HUD.ArcheryScore.PosY"), 0);
                    if (CVarGetInteger(CVAR_COSMETIC("HUD.ArcheryScore.PosType"), 0) == ANCHOR_LEFT) {
                        if (CVarGetInteger(CVAR_COSMETIC("HUD.ArcheryScore.UseMargins"), 0) != 0) {
                            X_Margins_Archery = Left_HUD_Margin;
                        };
                        ArcheryPos_X = OTRGetRectDimensionFromLeftEdge(
                            CVarGetInteger(CVAR_COSMETIC("HUD.ArcheryScore.PosX"), 0) + X_Margins_Archery);
                    } else if (CVarGetInteger(CVAR_COSMETIC("HUD.ArcheryScore.PosType"), 0) == ANCHOR_RIGHT) {
                        if (CVarGetInteger(CVAR_COSMETIC("HUD.ArcheryScore.UseMargins"), 0) != 0) {
                            X_Margins_Archery = Right_HUD_Margin;
                        };
                        ArcheryPos_X = OTRGetRectDimensionFromRightEdge(
                            CVarGetInteger(CVAR_COSMETIC("HUD.ArcheryScore.PosX"), 0) + X_Margins_Archery);
                    } else if (CVarGetInteger(CVAR_COSMETIC("HUD.ArcheryScore.PosType"), 0) == ANCHOR_NONE) {
                        ArcheryPos_X =
                            CVarGetInteger(CVAR_COSMETIC("HUD.ArcheryScore.PosX"), 0) + 204 + X_Margins_Archery;
                    } else if (CVarGetInteger(CVAR_COSMETIC("HUD.ArcheryScore.PosType"), 0) == HIDDEN) {
                        ArcheryPos_X = -9999;
                    }
                }

                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, interfaceCtx->bAlpha);

                // Target Icon
                gDPLoadTextureBlock(OVERLAY_DISP++, gArcheryScoreIconTex, G_IM_FMT_RGBA, G_IM_SIZ_16b, 24, 16, 0,
                                    G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                    G_TX_NOLOD, G_TX_NOLOD);

                gSPWideTextureRectangle(OVERLAY_DISP++, (ArcheryPos_X + 28) << 2, ArcheryPos_Y << 2,
                                        (ArcheryPos_X + 52) << 2, (ArcheryPos_Y + 16) << 2, G_TX_RENDERTILE, 0, 0,
                                        1 << 10, 1 << 10);

                // Score Counter
                gDPPipeSync(OVERLAY_DISP++);
                gDPSetCombineLERP(OVERLAY_DISP++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE,
                                  TEXEL0, 0, PRIMITIVE, 0);

                ArcheryPos_X = ArcheryPos_X + 6 * 9;

                for (svar1 = svar2 = 0; svar1 < 4; svar1++) {
                    if (sHBAScoreDigits[svar1] != 0 || (svar2 != 0) || (svar1 >= 3)) {
                        OVERLAY_DISP =
                            Gfx_TextureI8(OVERLAY_DISP, digitTextures[sHBAScoreDigits[svar1]], 8, 16, ArcheryPos_X,
                                          (ArcheryPos_Y - 2), digitWidth[0], VREG(42), VREG(43) << 1, VREG(43) << 1);
                        ArcheryPos_X += 9;
                        svar2++;
                    }
                }

                gDPPipeSync(OVERLAY_DISP++);
                gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
            }
        }

        if ((gSaveContext.subTimerState == SUBTIMER_STATE_RESPAWN) &&
            (Message_GetState(&play->msgCtx) == TEXT_STATE_EVENT)) {
            // Trade quest timer reached 0
            sSubTimerStateTimer = 40;
            gSaveContext.cutsceneIndex = 0;
            play->transitionTrigger = TRANS_TRIGGER_START;
            play->transitionType = TRANS_TYPE_FADE_WHITE;
            gSaveContext.subTimerState = SUBTIMER_STATE_OFF;

            if ((gSaveContext.equips.buttonItems[0] != ITEM_SWORD_KOKIRI) &&
                (gSaveContext.equips.buttonItems[0] != ITEM_SWORD_MASTER) &&
                (gSaveContext.equips.buttonItems[0] != ITEM_SWORD_BGS) &&
                (gSaveContext.equips.buttonItems[0] != ITEM_SWORD_KNIFE)) {
                if (gSaveContext.buttonStatus[0] != BTN_ENABLED) {
                    gSaveContext.equips.buttonItems[0] = gSaveContext.buttonStatus[0];
                    GameInteractor_Should(VB_TEMP_B_RESTORE_SWORDLESS, true);
                } else {
                    gSaveContext.equips.buttonItems[0] = ITEM_NONE;
                }
            }

            // Revert any spoiling trade quest items
            if (GameInteractor_Should(VB_REVERT_SPOILING_ITEMS, true)) {
                for (svar1 = 0; svar1 < ARRAY_COUNT(gSpoilingItems); svar1++) {
                    if (INV_CONTENT(ITEM_TRADE_ADULT) == gSpoilingItems[svar1]) {
                        gSaveContext.eventInf[0] &= 0x7F80;
                        osSyncPrintf("EVENT_INF=%x\n", gSaveContext.eventInf[0]);
                        play->nextEntranceIndex = spoilingItemEntrances[svar1];
                        INV_CONTENT(gSpoilingItemReverts[svar1]) = gSpoilingItemReverts[svar1];

                        for (svar2 = 1; svar2 < ARRAY_COUNT(gSaveContext.equips.buttonItems); svar2++) {
                            if (gSaveContext.equips.buttonItems[svar2] == gSpoilingItems[svar1]) {
                                gSaveContext.equips.buttonItems[svar2] = gSpoilingItemReverts[svar1];
                                Interface_LoadItemIcon1(play, svar2);
                            }
                        }
                    }
                }
            }
        }

        if ((play->pauseCtx.state == 0) && (play->pauseCtx.debugState == 0) &&
            (play->gameOverCtx.state == GAMEOVER_INACTIVE) && (msgCtx->msgMode == MSGMODE_NONE) &&
            !(player->stateFlags2 & PLAYER_STATE2_ATTEMPT_PLAY_FOR_ACTOR) &&
            (play->transitionTrigger == TRANS_TRIGGER_OFF) && (play->transitionMode == TRANS_MODE_OFF) &&
            !Play_InCsMode(play) && (gSaveContext.minigameState != 1) && (play->shootingGalleryStatus <= 1) &&
            !((play->sceneNum == SCENE_BOMBCHU_BOWLING_ALLEY) && Flags_GetSwitch(play, 0x38))) {
            timerId = TIMER_ID_MAIN;
            switch (gSaveContext.timerState) {
                case TIMER_STATE_ENV_HAZARD_INIT:
                    sTimerStateTimer = 20;
                    sTimerNextSecondTimer = 20;
                    gSaveContext.timerSeconds = gSaveContext.health >> 1;
                    gSaveContext.timerState = TIMER_STATE_ENV_HAZARD_PREVIEW;
                    break;
                case TIMER_STATE_ENV_HAZARD_PREVIEW:
                    sTimerStateTimer--;
                    if (sTimerStateTimer == 0) {
                        sTimerStateTimer = 20;
                        gSaveContext.timerState = TIMER_STATE_ENV_HAZARD_MOVE;
                    }
                    break;
                case TIMER_STATE_DOWN_INIT:
                case TIMER_STATE_UP_INIT:
                    sTimerStateTimer = 20;
                    sTimerNextSecondTimer = 20;
                    if (gSaveContext.timerState == TIMER_STATE_DOWN_INIT) {
                        gSaveContext.timerState = TIMER_STATE_DOWN_PREVIEW;
                    } else {
                        gSaveContext.timerState = TIMER_STATE_UP_PREVIEW;
                    }
                    break;
                case TIMER_STATE_DOWN_PREVIEW:
                case TIMER_STATE_UP_PREVIEW:
                    sTimerStateTimer--;
                    if (sTimerStateTimer == 0) {
                        sTimerStateTimer = 20;
                        if (gSaveContext.timerState == TIMER_STATE_DOWN_PREVIEW) {
                            gSaveContext.timerState = TIMER_STATE_DOWN_MOVE;
                        } else {
                            gSaveContext.timerState = TIMER_STATE_UP_MOVE;
                        }
                    }
                    break;
                case TIMER_STATE_ENV_HAZARD_MOVE:
                case TIMER_STATE_DOWN_MOVE:
                    svar1 = (gSaveContext.timerX[TIMER_ID_MAIN] - 26) / sTimerStateTimer;
                    gSaveContext.timerX[TIMER_ID_MAIN] -= svar1;

                    if (gSaveContext.healthCapacity > 0xA0) {
                        svar1 = (gSaveContext.timerY[TIMER_ID_MAIN] - 54) / sTimerStateTimer;
                    } else {
                        svar1 = (gSaveContext.timerY[TIMER_ID_MAIN] - 46) / sTimerStateTimer;
                    }
                    gSaveContext.timerY[TIMER_ID_MAIN] -= svar1;

                    sTimerStateTimer--;
                    if (sTimerStateTimer == 0) {
                        sTimerStateTimer = 20;
                        gSaveContext.timerX[TIMER_ID_MAIN] = 26;

                        if (gSaveContext.healthCapacity > 0xA0) {
                            gSaveContext.timerY[TIMER_ID_MAIN] = 54;
                        } else {
                            gSaveContext.timerY[TIMER_ID_MAIN] = 46;
                        }

                        if (gSaveContext.timerState == TIMER_STATE_ENV_HAZARD_MOVE) {
                            gSaveContext.timerState = TIMER_STATE_ENV_HAZARD_TICK;
                        } else {
                            gSaveContext.timerState = TIMER_STATE_DOWN_TICK;
                        }
                    }
                case TIMER_STATE_ENV_HAZARD_TICK:
                case TIMER_STATE_DOWN_TICK:
                    if ((gSaveContext.timerState == TIMER_STATE_ENV_HAZARD_TICK) ||
                        (gSaveContext.timerState == TIMER_STATE_DOWN_TICK)) {
                        if (gSaveContext.healthCapacity > 0xA0) {
                            gSaveContext.timerY[TIMER_ID_MAIN] = 54;
                        } else {
                            gSaveContext.timerY[TIMER_ID_MAIN] = 46;
                        }
                    }

                    if ((gSaveContext.timerState >= TIMER_STATE_ENV_HAZARD_MOVE) && (msgCtx->msgLength == 0)) {
                        sTimerNextSecondTimer--;
                        if (sTimerNextSecondTimer == 0) {
                            if (gSaveContext.timerSeconds != 0) {
                                gSaveContext.timerSeconds--;
                            }

                            sTimerNextSecondTimer = 20;

                            if (gSaveContext.timerSeconds == 0) {
                                gSaveContext.timerState = TIMER_STATE_STOP;
                                if (sEnvHazardActive) {
                                    gSaveContext.health = 0;
                                    play->damagePlayer(play, -(gSaveContext.health + 2));
                                }
                                sEnvHazardActive = false;
                            } else if (gSaveContext.timerSeconds > 60) {
                                if (timerDigits[4] == 1) {
                                    Audio_PlaySfxGeneral(NA_SE_SY_MESSAGE_WOMAN, &gSfxDefaultPos, 4,
                                                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                                         &gSfxDefaultReverb);
                                }
                            } else if (gSaveContext.timerSeconds >= 11) {
                                if (timerDigits[4] & 1) {
                                    Audio_PlaySfxGeneral(NA_SE_SY_WARNING_COUNT_N, &gSfxDefaultPos, 4,
                                                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                                         &gSfxDefaultReverb);
                                }
                            } else {
                                Audio_PlaySfxGeneral(NA_SE_SY_WARNING_COUNT_E, &gSfxDefaultPos, 4,
                                                     &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                                     &gSfxDefaultReverb);
                            }
                        }
                    }
                    break;
                case TIMER_STATE_UP_MOVE:
                    svar1 = (gSaveContext.timerX[TIMER_ID_MAIN] - 26) / sTimerStateTimer;
                    gSaveContext.timerX[TIMER_ID_MAIN] -= svar1;

                    if (gSaveContext.healthCapacity > 0xA0) {
                        svar1 = (gSaveContext.timerY[TIMER_ID_MAIN] - 54) / sTimerStateTimer;
                    } else {
                        svar1 = (gSaveContext.timerY[TIMER_ID_MAIN] - 46) / sTimerStateTimer;
                    }
                    gSaveContext.timerY[TIMER_ID_MAIN] -= svar1;

                    sTimerStateTimer--;
                    if (sTimerStateTimer == 0) {
                        sTimerStateTimer = 20;
                        gSaveContext.timerX[TIMER_ID_MAIN] = 26;
                        if (gSaveContext.healthCapacity > 0xA0) {
                            gSaveContext.timerY[TIMER_ID_MAIN] = 54;
                        } else {
                            gSaveContext.timerY[TIMER_ID_MAIN] = 46;
                        }

                        gSaveContext.timerState = TIMER_STATE_UP_TICK;
                    }
                case TIMER_STATE_UP_TICK:
                    if (gSaveContext.timerState == TIMER_STATE_UP_TICK) {
                        if (gSaveContext.healthCapacity > 0xA0) {
                            gSaveContext.timerY[TIMER_ID_MAIN] = 54;
                        } else {
                            gSaveContext.timerY[TIMER_ID_MAIN] = 46;
                        }
                    }

                    if (gSaveContext.timerState >= TIMER_STATE_ENV_HAZARD_MOVE) {
                        sTimerNextSecondTimer--;
                        if (sTimerNextSecondTimer == 0) {
                            gSaveContext.timerSeconds++;
                            sTimerNextSecondTimer = 20;

                            if (gSaveContext.timerSeconds == 3599) {
                                sTimerStateTimer = 40;
                                gSaveContext.timerState = TIMER_STATE_UP_FREEZE;
                            } else {
                                Audio_PlaySfxGeneral(NA_SE_SY_WARNING_COUNT_N, &gSfxDefaultPos, 4,
                                                     &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                                     &gSfxDefaultReverb);
                            }
                        }
                    }
                    break;
                case TIMER_STATE_STOP:
                    if (gSaveContext.subTimerState != SUBTIMER_STATE_OFF) {
                        sSubTimerStateTimer = 20;
                        sSubTimerNextSecondTimer = 20;
                        gSaveContext.timerX[TIMER_ID_SUB] = 140;
                        gSaveContext.timerY[TIMER_ID_SUB] = 80;

                        if (gSaveContext.subTimerState <= SUBTIMER_STATE_STOP) {
                            gSaveContext.subTimerState = SUBTIMER_STATE_DOWN_PREVIEW;
                        } else {
                            gSaveContext.subTimerState = SUBTIMER_STATE_UP_PREVIEW;
                        }

                        gSaveContext.timerState = TIMER_STATE_OFF;
                    } else {
                        gSaveContext.timerState = TIMER_STATE_OFF;
                    }
                case TIMER_STATE_UP_FREEZE:
                    break;
                default:
                    timerId = TIMER_ID_SUB;
                    switch (gSaveContext.subTimerState) {
                        case SUBTIMER_STATE_DOWN_INIT:
                        case SUBTIMER_STATE_UP_INIT:
                            sSubTimerStateTimer = 20;
                            sSubTimerNextSecondTimer = 20;
                            gSaveContext.timerX[TIMER_ID_SUB] = 140;
                            gSaveContext.timerY[TIMER_ID_SUB] = 80;
                            if (gSaveContext.subTimerState == SUBTIMER_STATE_DOWN_INIT) {
                                gSaveContext.subTimerState = SUBTIMER_STATE_DOWN_PREVIEW;
                            } else {
                                gSaveContext.subTimerState = SUBTIMER_STATE_UP_PREVIEW;
                            }
                            break;
                        case SUBTIMER_STATE_DOWN_PREVIEW:
                        case SUBTIMER_STATE_UP_PREVIEW:
                            sSubTimerStateTimer--;
                            if (sSubTimerStateTimer == 0) {
                                sSubTimerStateTimer = 20;
                                if (gSaveContext.subTimerState == SUBTIMER_STATE_DOWN_PREVIEW) {
                                    gSaveContext.subTimerState = SUBTIMER_STATE_DOWN_MOVE;
                                } else {
                                    gSaveContext.subTimerState = SUBTIMER_STATE_UP_MOVE;
                                }
                            }
                            break;
                        case SUBTIMER_STATE_DOWN_MOVE:
                        case SUBTIMER_STATE_UP_MOVE:
                            osSyncPrintf("event_xp[1]=%d,  event_yp[1]=%d  TOTAL_EVENT_TM=%d\n",
                                         svar5 = gSaveContext.timerX[TIMER_ID_SUB],
                                         svar2 = gSaveContext.timerY[TIMER_ID_SUB], gSaveContext.subTimerSeconds);
                            svar1 = (gSaveContext.timerX[TIMER_ID_SUB] - 26) / sSubTimerStateTimer;
                            gSaveContext.timerX[TIMER_ID_SUB] -= svar1;
                            if (gSaveContext.healthCapacity > 0xA0) {
                                svar1 = (gSaveContext.timerY[TIMER_ID_SUB] - 54) / sSubTimerStateTimer;
                            } else {
                                svar1 = (gSaveContext.timerY[TIMER_ID_SUB] - 46) / sSubTimerStateTimer;
                            }
                            gSaveContext.timerY[TIMER_ID_SUB] -= svar1;

                            sSubTimerStateTimer--;
                            if (sSubTimerStateTimer == 0) {
                                sSubTimerStateTimer = 20;
                                gSaveContext.timerX[TIMER_ID_SUB] = 26;

                                if (gSaveContext.healthCapacity > 0xA0) {
                                    gSaveContext.timerY[TIMER_ID_SUB] = 54;
                                } else {
                                    gSaveContext.timerY[TIMER_ID_SUB] = 46;
                                }

                                if (gSaveContext.subTimerState == SUBTIMER_STATE_DOWN_MOVE) {
                                    gSaveContext.subTimerState = SUBTIMER_STATE_DOWN_TICK;
                                } else {
                                    gSaveContext.subTimerState = SUBTIMER_STATE_UP_TICK;
                                }
                            }
                        case SUBTIMER_STATE_DOWN_TICK:
                        case SUBTIMER_STATE_UP_TICK:
                            if ((gSaveContext.subTimerState == SUBTIMER_STATE_DOWN_TICK) ||
                                (gSaveContext.subTimerState == SUBTIMER_STATE_UP_TICK)) {
                                if (gSaveContext.healthCapacity > 0xA0) {
                                    gSaveContext.timerY[TIMER_ID_SUB] = 54;
                                } else {
                                    gSaveContext.timerY[TIMER_ID_SUB] = 46;
                                }
                            }

                            if (gSaveContext.subTimerState >= SUBTIMER_STATE_DOWN_MOVE) {
                                sSubTimerNextSecondTimer--;
                                if (sSubTimerNextSecondTimer == 0) {
                                    sSubTimerNextSecondTimer = 20;
                                    if (gSaveContext.subTimerState == SUBTIMER_STATE_DOWN_TICK) {
                                        gSaveContext.subTimerSeconds--;
                                        osSyncPrintf("TOTAL_EVENT_TM=%d\n", gSaveContext.subTimerSeconds);

                                        if (gSaveContext.subTimerSeconds <= 0) {
                                            if (!Flags_GetSwitch(play, 0x37) ||
                                                ((play->sceneNum != SCENE_GANON_BOSS) &&
                                                 (play->sceneNum != SCENE_GANONS_TOWER_COLLAPSE_EXTERIOR) &&
                                                 (play->sceneNum != SCENE_GANONS_TOWER_COLLAPSE_INTERIOR) &&
                                                 (play->sceneNum != SCENE_INSIDE_GANONS_CASTLE_COLLAPSE))) {
                                                sSubTimerStateTimer = 40;
                                                gSaveContext.subTimerState = SUBTIMER_STATE_RESPAWN;
                                                gSaveContext.cutsceneIndex = 0;
                                                Message_StartTextbox(play, 0x71B0, NULL);
                                                Player_SetCsActionWithHaltedActors(play, NULL, 8);
                                            } else {
                                                sSubTimerStateTimer = 40;
                                                gSaveContext.subTimerState = SUBTIMER_STATE_STOP;
                                            }
                                        } else if (gSaveContext.subTimerSeconds > 60) {
                                            if (timerDigits[4] == 1) {
                                                Audio_PlaySfxGeneral(NA_SE_SY_MESSAGE_WOMAN, &gSfxDefaultPos, 4,
                                                                     &gSfxDefaultFreqAndVolScale,
                                                                     &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                                            }
                                        } else if (gSaveContext.subTimerSeconds > 10) {
                                            if ((timerDigits[4] & 1)) {
                                                Audio_PlaySfxGeneral(NA_SE_SY_WARNING_COUNT_N, &gSfxDefaultPos, 4,
                                                                     &gSfxDefaultFreqAndVolScale,
                                                                     &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                                            }
                                        } else {
                                            Audio_PlaySfxGeneral(NA_SE_SY_WARNING_COUNT_E, &gSfxDefaultPos, 4,
                                                                 &gSfxDefaultFreqAndVolScale,
                                                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                                        }
                                    } else {
                                        gSaveContext.subTimerSeconds++;
                                        if (gSaveContext.eventInf[1] & 1) {
                                            if (gSaveContext.subTimerSeconds == 240) {
                                                Message_StartTextbox(play, 0x6083, NULL);
                                                gSaveContext.eventInf[1] &= ~1;
                                                gSaveContext.subTimerState = SUBTIMER_STATE_OFF;
                                            }
                                        }
                                    }

                                    if ((gSaveContext.subTimerSeconds % 60) == 0) {
                                        Audio_PlaySfxGeneral(NA_SE_SY_WARNING_COUNT_N, &gSfxDefaultPos, 4,
                                                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                                             &gSfxDefaultReverb);
                                    }
                                }
                            }
                            break;
                        case 6:
                            sSubTimerStateTimer--;
                            if (sSubTimerStateTimer == 0) {
                                gSaveContext.subTimerState = SUBTIMER_STATE_OFF;
                            }
                            break;
                    }
                    break;
            }

            if (((gSaveContext.timerState != TIMER_STATE_OFF) && (gSaveContext.timerState != TIMER_STATE_STOP)) ||
                (gSaveContext.subTimerState != SUBTIMER_STATE_OFF)) {
                timerDigits[0] = timerDigits[1] = svar2 = timerDigits[3] = 0;
                timerDigits[2] = 10; // digit 10 is used as ':' (colon)

                if (gSaveContext.timerState != TIMER_STATE_OFF) {
                    timerDigits[4] = gSaveContext.timerSeconds;
                } else {
                    timerDigits[4] = gSaveContext.subTimerSeconds;
                }

                while (timerDigits[4] >= 60) {
                    timerDigits[1]++;
                    if (timerDigits[1] >= 10) {
                        timerDigits[0]++;
                        timerDigits[1] -= 10;
                    }
                    timerDigits[4] -= 60;
                }

                while (timerDigits[4] >= 10) {
                    timerDigits[3]++;
                    timerDigits[4] -= 10;
                }

                // Clock Icon
                gDPPipeSync(OVERLAY_DISP++);
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, 255);
                gDPSetEnvColor(OVERLAY_DISP++, 0, 0, 0, 0);
                s32 X_Margins_Timer;
                if (CVarGetInteger(CVAR_COSMETIC("HUD.Timers.UseMargins"), 0) != 0) {
                    if (CVarGetInteger(CVAR_COSMETIC("HUD.Timers.PosType"), 0) == ORIGINAL_LOCATION) {
                        X_Margins_Timer = Left_HUD_Margin;
                    };
                } else {
                    X_Margins_Timer = 0;
                }
                svar5 = OTRGetRectDimensionFromLeftEdge(gSaveContext.timerX[timerId] + X_Margins_Timer);
                svar2 = gSaveContext.timerY[timerId];
                if (CVarGetInteger(CVAR_COSMETIC("HUD.Timers.PosType"), 0) != ORIGINAL_LOCATION) {
                    svar2 = (CVarGetInteger(CVAR_COSMETIC("HUD.Timers.PosY"), 0));
                    if (CVarGetInteger(CVAR_COSMETIC("HUD.Timers.PosType"), 0) == ANCHOR_LEFT) {
                        if (CVarGetInteger(CVAR_COSMETIC("HUD.Timers.UseMargins"), 0) != 0) {
                            X_Margins_Timer = Left_HUD_Margin;
                        };
                        svar5 = OTRGetRectDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Timers.PosX"), 0) +
                                                                X_Margins_Timer);
                    } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Timers.PosType"), 0) == ANCHOR_RIGHT) {
                        if (CVarGetInteger(CVAR_COSMETIC("HUD.Timers.UseMargins"), 0) != 0) {
                            X_Margins_Timer = Right_HUD_Margin;
                        };
                        svar5 = OTRGetRectDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Timers.PosX"), 0) +
                                                                 X_Margins_Timer);
                    } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Timers.PosType"), 0) == ANCHOR_NONE) {
                        svar5 = CVarGetInteger(CVAR_COSMETIC("HUD.Timers.PosX"), 0) + 204 + X_Margins_Timer;
                    } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Timers.PosType"), 0) == HIDDEN) {
                        svar5 = -9999;
                    }
                }

                OVERLAY_DISP =
                    Gfx_TextureIA8(OVERLAY_DISP, gClockIconTex, 16, 16, svar5, svar2 + 2, 16, 16, 1 << 10, 1 << 10);

                // Timer Counter
                gDPPipeSync(OVERLAY_DISP++);
                gDPSetCombineLERP(OVERLAY_DISP++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE,
                                  TEXEL0, 0, PRIMITIVE, 0);

                if (gSaveContext.timerState != TIMER_STATE_OFF) {
                    if ((gSaveContext.timerSeconds < 10) && (gSaveContext.timerState <= TIMER_STATE_STOP)) {
                        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 50, 0, 255);
                    } else {
                        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, 255);
                    }
                } else {
                    if ((gSaveContext.subTimerSeconds < 10) && (gSaveContext.subTimerState <= SUBTIMER_STATE_RESPAWN)) {
                        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 50, 0, 255);
                    } else {
                        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 0, 255);
                    }
                }

                for (svar1 = 0; svar1 < 5; svar1++) {
                    // clang-format off
                    //svar5 = svar5 + 8;
                    //svar5 = OTRGetRectDimensionFromLeftEdge(gSaveContext.timerX[timerId]);
                    OVERLAY_DISP = Gfx_TextureI8(OVERLAY_DISP, digitTextures[timerDigits[svar1]], 8, 16,
                                      svar5 + timerDigitLeftPos[svar1],
                                      svar2, digitWidth[svar1], VREG(42), VREG(43) << 1,
                                      VREG(43) << 1);
                    // clang-format on
                }
            }
        }
    }

    if (pauseCtx->debugState == 3) {
        FlagSet_Update(play);
    }

    if (interfaceCtx->unk_244 != 0) {
        gDPPipeSync(OVERLAY_DISP++);
        gSPDisplayList(OVERLAY_DISP++, sSetupDL_80125A60);
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 0, 0, 0, interfaceCtx->unk_244);
        gDPFillRectangle(OVERLAY_DISP++, 0, 0, gScreenWidth - 1, gScreenHeight - 1);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}