/* overlay-merge 20388a05c1a1 2beba19e0c71 e6a6ca9d71f5 */
void KaleidoScope_Update(PlayState* play) {
    static s16 D_8082B258 = 0;
    static s16 D_8082B25C = 10;
    static s16 D_8082B260 = 0;
    PauseContext* pauseCtx = &play->pauseCtx;
    InterfaceContext* interfaceCtx = &play->interfaceCtx;
    GameOverContext* gameOverCtx = &play->gameOverCtx;
    Player* player = GET_PLAYER(play);
    Input* input = &play->state.input[0];
    size_t size = 0;
    size_t size0 = 0;
    size_t size1 = 0;
    size_t size2 = 0;
    u16 i = 0;
    s16 stepR;
    s16 stepG;
    s16 stepB;
    s16 stepA;
    s32 pad;

    if ((R_PAUSE_MENU_MODE >= 3) && (((pauseCtx->state >= 4) && (pauseCtx->state <= 7)) ||
                                     ((pauseCtx->state >= 0xA) && (pauseCtx->state <= 0x12)))) {

        if ((!pauseCtx->unk_1E4 || (pauseCtx->unk_1E4 == 8)) && (pauseCtx->state == 6)) {
            pauseCtx->stickRelX = input->rel.stick_x;
            pauseCtx->stickRelY = input->rel.stick_y;
            KaleidoScope_UpdateCursorSize(&play->pauseCtx);
            // Crossover Items / transform selection now lives on the Equipment page's
            // 3rd page (z_kaleido_equipment.c), so the Map page just does the normal
            // page rotation again.
            KaleidoScope_HandlePageToggles(pauseCtx, input);
        } else if ((pauseCtx->pageIndex == PAUSE_QUEST) && ((pauseCtx->unk_1E4 < 3) || (pauseCtx->unk_1E4 == 5))) {
            KaleidoScope_UpdateCursorSize(&play->pauseCtx);
        }

        if (pauseCtx->state == 6) {
            KaleidoScope_UpdateNamePanel(play);
        }
    }

    switch (pauseCtx->state) {
        case 3:
            for (int buttonIndex = 0; buttonIndex < ARRAY_COUNT(gSaveContext.buttonStatus); buttonIndex++) {
                sButtonStatusSave[buttonIndex] = gSaveContext.buttonStatus[buttonIndex];
            }

            pauseCtx->cursorX[PAUSE_MAP] = 0;
            pauseCtx->cursorSlot[PAUSE_MAP] = pauseCtx->cursorPoint[PAUSE_MAP] = pauseCtx->dungeonMapSlot =
                VREG(30) + 3;

            WREG(16) = -175;
            WREG(17) = 155;

            pauseCtx->unk_204 = -314.0f;

            pauseCtx->playerSegment = (void*)(((uintptr_t)play->objectCtx.spaceStart + 0x30) & ~0x3F);

            size1 = func_80091738(play, pauseCtx->playerSegment, &pauseCtx->playerSkelAnime);
            osSyncPrintf("プレイヤー size1＝%x\n", size1);

            pauseCtx->iconItemSegment = (void*)(((uintptr_t)pauseCtx->playerSegment + size1 + 0xF) & ~0xF);

            size0 = (uintptr_t)_icon_item_staticSegmentRomEnd - (uintptr_t)_icon_item_staticSegmentRomStart;
            osSyncPrintf("icon_item size0=%x\n", size0);
            DmaMgr_SendRequest1(pauseCtx->iconItemSegment, (uintptr_t)_icon_item_staticSegmentRomStart, size0, __FILE__,
                                __LINE__);

            gSegments[8] = VIRTUAL_TO_PHYSICAL(pauseCtx->iconItemSegment);

            pauseCtx->iconItem24Segment = (void*)(((uintptr_t)pauseCtx->iconItemSegment + size0 + 0xF) & ~0xF);

#if 1
            // size = (uintptr_t)_icon_item_24_staticSegmentRomEnd - (uintptr_t)_icon_item_24_staticSegmentRomStart;
            // osSyncPrintf("icon_item24 size=%x\n", size);
            // DmaMgr_SendRequest1(pauseCtx->iconItem24Segment, (uintptr_t)_icon_item_24_staticSegmentRomStart, size,
            //__FILE__, __LINE__);

            // pauseCtx->iconItemAltSegment = (void*)(((uintptr_t)pauseCtx->iconItem24Segment + size + 0xF) & ~0xF);
#endif

            switch (play->sceneNum) {
                case SCENE_DEKU_TREE:
                case SCENE_DODONGOS_CAVERN:
                case SCENE_JABU_JABU:
                case SCENE_FOREST_TEMPLE:
                case SCENE_FIRE_TEMPLE:
                case SCENE_WATER_TEMPLE:
                case SCENE_SPIRIT_TEMPLE:
                case SCENE_SHADOW_TEMPLE:
                case SCENE_BOTTOM_OF_THE_WELL:
                case SCENE_ICE_CAVERN:
                case SCENE_DEKU_TREE_BOSS:
                case SCENE_DODONGOS_CAVERN_BOSS:
                case SCENE_JABU_JABU_BOSS:
                case SCENE_FOREST_TEMPLE_BOSS:
                case SCENE_FIRE_TEMPLE_BOSS:
                case SCENE_WATER_TEMPLE_BOSS:
                case SCENE_SPIRIT_TEMPLE_BOSS:
                case SCENE_SHADOW_TEMPLE_BOSS:
#if 1
                    sInDungeonScene = true;
                    // size2 = (uintptr_t)_icon_item_dungeon_staticSegmentRomEnd -
                    // (uintptr_t)_icon_item_dungeon_staticSegmentRomStart; osSyncPrintf("icon_item_dungeon
                    // dungeon-size2=%x\n", size2); DmaMgr_SendRequest1(pauseCtx->iconItemAltSegment,
                    // (uintptr_t)_icon_item_dungeon_staticSegmentRomStart, size2, __FILE__, __LINE__);
#endif

                    interfaceCtx->mapPalette[28] = 6;
                    interfaceCtx->mapPalette[29] = 99;
                    KaleidoScope_UpdateDungeonMap(play);
                    break;

                default:
#if 1
                    sInDungeonScene = false;
                    // size2 = (uintptr_t)_icon_item_field_staticSegmentRomEnd -
                    // (uintptr_t)_icon_item_field_staticSegmentRomStart; osSyncPrintf("icon_item_field
                    // field-size2=%x\n", size2); DmaMgr_SendRequest1(pauseCtx->iconItemAltSegment,
                    // (uintptr_t)_icon_item_field_staticSegmentRomStart, size2, __FILE__, __LINE__);
#endif
                    break;
            }

                // OTRTODO: LANGUAGE SUPPORT
#if 1
            // pauseCtx->iconItemLangSegment = (void*)(((uintptr_t)pauseCtx->iconItemAltSegment + size2 + 0xF) & ~0xF);

            if (gSaveContext.language == LANGUAGE_ENG) {
                // size = (uintptr_t)_icon_item_nes_staticSegmentRomEnd -
                // (uintptr_t)_icon_item_nes_staticSegmentRomStart; osSyncPrintf("icon_item_dungeon dungeon-size=%x\n",
                // size); DmaMgr_SendRequest1(pauseCtx->iconItemLangSegment, _icon_item_nes_staticSegmentRomStart, size,
                //__FILE__, __LINE__);
            } else if (gSaveContext.language == LANGUAGE_GER) {
                // size = (uintptr_t)_icon_item_ger_staticSegmentRomEnd -
                // (uintptr_t)_icon_item_ger_staticSegmentRomStart; osSyncPrintf("icon_item_dungeon dungeon-size=%x\n",
                // size); DmaMgr_SendRequest1(pauseCtx->iconItemLangSegment,
                // (uintptr_t)_icon_item_ger_staticSegmentRomStart, size,
                //__FILE__, __LINE__);
            } else {
                // size = (uintptr_t)_icon_item_fra_staticSegmentRomEnd -
                // (uintptr_t)_icon_item_fra_staticSegmentRomStart; osSyncPrintf("icon_item_dungeon dungeon-size=%x\n",
                // size); DmaMgr_SendRequest1(pauseCtx->iconItemLangSegment,
                // (uintptr_t)_icon_item_fra_staticSegmentRomStart, size,
                //__FILE__, __LINE__);
            }
#endif

            // pauseCtx->nameSegment = (void*)(((uintptr_t)pauseCtx->iconItemLangSegment + size + 0xF) & ~0xF);
            pauseCtx->nameSegment = malloc(0x400 + 0xA00); // OTRTODO: GET RID OF THIS

            osSyncPrintf("サイズ＝%x\n", size2 + size1 + size0 + size);
            osSyncPrintf("item_name I_N_PT=%x\n", 0x800);
            Interface_SetDoAction(play, DO_ACTION_DECIDE);
            osSyncPrintf("サイズ＝%x\n", size2 + size1 + size0 + size + 0x800);

            if (((void)0, gSaveContext.worldMapArea) < 22) {
                // SOH [NTSC] - change offsets to fit jpn textures
                const uint8_t offsets[] = { 48, 70, 92, 114 };
                const char* textureName = mapNameTextures[offsets[gSaveContext.language] + gSaveContext.worldMapArea];
                memcpy(pauseCtx->nameSegment + 0x400, textureName, strlen(textureName) + 1);
            }
// OTRTODO - player on pause
#if 1
            // HDTODO: Remove sPreRenderCvg stuff?
            sPreRenderCvg = (void*)(((uintptr_t)pauseCtx->nameSegment + 0x400 + 0xA00 + 0xF) & ~0xF);

            PreRender_Init(&sPlayerPreRender);
            PreRender_SetValuesSave(&sPlayerPreRender, PAUSE_EQUIP_PLAYER_WIDTH, PAUSE_EQUIP_PLAYER_HEIGHT,
                                    pauseCtx->playerSegment, NULL, sPreRenderCvg);

            KaleidoScope_DrawPlayerWork(play);
// KaleidoScope_SetupPlayerPreRender(play);
#endif
            for (i = 0; i < ARRAY_COUNT(pauseCtx->worldMapPoints); i++) {
                pauseCtx->worldMapPoints[i] = 0;
            }

            if (CHECK_QUEST_ITEM(QUEST_GERUDO_CARD)) {
                pauseCtx->worldMapPoints[0] = 2;
            }

            if (CHECK_QUEST_ITEM(QUEST_MEDALLION_SPIRIT)) {
                pauseCtx->worldMapPoints[0] = 1;
            }

            if (INV_CONTENT(ITEM_LONGSHOT) == ITEM_LONGSHOT) {
                pauseCtx->worldMapPoints[1] = 2;
            }

            if (CHECK_QUEST_ITEM(QUEST_GERUDO_CARD)) {
                pauseCtx->worldMapPoints[1] = 1;
            }

            if (Flags_GetEventChkInf(EVENTCHKINF_ENTERED_GERUDO_VALLEY)) {
                pauseCtx->worldMapPoints[2] = 1;
            }

            if (INV_CONTENT(ITEM_LONGSHOT) == ITEM_LONGSHOT) {
                pauseCtx->worldMapPoints[2] = 2;
            }

            if (CHECK_QUEST_ITEM(QUEST_GERUDO_CARD)) {
                pauseCtx->worldMapPoints[2] = 1;
            }

            if (CUR_UPG_VALUE(UPG_SCALE)) {
                pauseCtx->worldMapPoints[3] = 1;
            }

            if (CHECK_OWNED_EQUIP(EQUIP_TYPE_BOOTS, EQUIP_INV_BOOTS_IRON)) {
                pauseCtx->worldMapPoints[3] = 2;
            }

            if (CHECK_QUEST_ITEM(QUEST_MEDALLION_WATER)) {
                pauseCtx->worldMapPoints[3] = 1;
            }

            if (Flags_GetEventChkInf(EVENTCHKINF_USED_DEKU_TREE_BLUE_WARP)) {
                pauseCtx->worldMapPoints[4] = 1;
            }

            if (INV_CONTENT(ITEM_OCARINA_FAIRY) != ITEM_NONE) {
                pauseCtx->worldMapPoints[4] = 2;
            }

            if (CHECK_QUEST_ITEM(QUEST_SONG_EPONA)) {
                pauseCtx->worldMapPoints[4] = 1;
            }

            if (Flags_GetEventChkInf(EVENTCHKINF_TALON_WOKEN_IN_KAKARIKO)) {
                pauseCtx->worldMapPoints[4] = 2;
            }

            if (Flags_GetEventChkInf(EVENTCHKINF_EPONA_OBTAINED)) {
                pauseCtx->worldMapPoints[4] = 1;
            }

            if (Flags_GetEventChkInf(EVENTCHKINF_USED_DEKU_TREE_BLUE_WARP)) {
                pauseCtx->worldMapPoints[5] = 2;
            }

            if (Flags_GetEventChkInf(EVENTCHKINF_OBTAINED_ZELDAS_LETTER)) {
                pauseCtx->worldMapPoints[5] = 1;
            }

            if (INV_CONTENT(ITEM_OCARINA_TIME) == ITEM_OCARINA_TIME) {
                pauseCtx->worldMapPoints[5] = 2;
            }

            if (Flags_GetEventChkInf(EVENTCHKINF_PULLED_MASTER_SWORD_FROM_PEDESTAL)) {
                pauseCtx->worldMapPoints[5] = 1;
            }

            if (INV_CONTENT(ITEM_ARROW_LIGHT) == ITEM_ARROW_LIGHT) {
                pauseCtx->worldMapPoints[5] = 2;
            }

            if (Flags_GetEventChkInf(EVENTCHKINF_USED_DEKU_TREE_BLUE_WARP)) {
                pauseCtx->worldMapPoints[6] = 1;
            }

            if (Flags_GetEventChkInf(EVENTCHKINF_OBTAINED_ZELDAS_LETTER)) {
                pauseCtx->worldMapPoints[7] = 2;
            }

            if (Flags_GetEventChkInf(EVENTCHKINF_USED_DODONGOS_CAVERN_BLUE_WARP)) {
                pauseCtx->worldMapPoints[7] = 1;
            }

            if (INV_CONTENT(ITEM_HOOKSHOT) == ITEM_HOOKSHOT) {
                pauseCtx->worldMapPoints[7] = 2;
            }

            if (Flags_GetEventChkInf(EVENTCHKINF_USED_FIRE_TEMPLE_BLUE_WARP)) {
                pauseCtx->worldMapPoints[7] = 1;
            }

            if (gBitFlags[1] & gSaveContext.worldMapAreaData) {
                pauseCtx->worldMapPoints[8] = 1;
            }

            if (CHECK_QUEST_ITEM(QUEST_SONG_LULLABY)) {
                pauseCtx->worldMapPoints[8] = 2;
            }

            if (CHECK_QUEST_ITEM(QUEST_SONG_SUN)) {
                pauseCtx->worldMapPoints[8] = 1;
            }

            if (Flags_GetEventChkInf(EVENTCHKINF_PULLED_MASTER_SWORD_FROM_PEDESTAL)) {
                pauseCtx->worldMapPoints[8] = 2;
            }

            if (INV_CONTENT(ITEM_HOOKSHOT) == ITEM_HOOKSHOT) {
                pauseCtx->worldMapPoints[8] = 1;
            }

            if (CHECK_QUEST_ITEM(QUEST_SONG_STORMS)) {
                pauseCtx->worldMapPoints[8] = 2;
            }

            if (Flags_GetEventChkInf(EVENTCHKINF_DRAINED_WELL_IN_KAKARIKO)) {
                pauseCtx->worldMapPoints[8] = 1;
            }

            if (Flags_GetEventChkInf(EVENTCHKINF_BONGO_BONGO_ESCAPED_FROM_WELL)) {
                pauseCtx->worldMapPoints[8] = 2;
            }

            if (CHECK_QUEST_ITEM(QUEST_MEDALLION_SHADOW)) {
                pauseCtx->worldMapPoints[8] = 1;
            }

            if (gBitFlags[10] & gSaveContext.worldMapAreaData) {
                pauseCtx->worldMapPoints[9] = 1;
            }

            if (Flags_GetEventChkInf(EVENTCHKINF_SPOKE_TO_MIDO_ABOUT_SARIA)) {
                pauseCtx->worldMapPoints[9] = 2;
            }

            if (CHECK_QUEST_ITEM(QUEST_SONG_SARIA)) {
                pauseCtx->worldMapPoints[9] = 1;
            }

            if (INV_CONTENT(ITEM_HOOKSHOT) == ITEM_HOOKSHOT) {
                pauseCtx->worldMapPoints[9] = 2;
            }

            if (Flags_GetEventChkInf(EVENTCHKINF_USED_FOREST_TEMPLE_BLUE_WARP)) {
                pauseCtx->worldMapPoints[9] = 1;
            }

            pauseCtx->worldMapPoints[10] = 2;

            if (Flags_GetEventChkInf(EVENTCHKINF_USED_DEKU_TREE_BLUE_WARP)) {
                pauseCtx->worldMapPoints[10] = 1;
            }

            if (Flags_GetEventChkInf(EVENTCHKINF_6E)) {
                pauseCtx->worldMapPoints[10] = 2;
            }

            if (Flags_GetEventChkInf(EVENTCHKINF_SPOKE_TO_MIDO_ABOUT_SARIA)) {
                pauseCtx->worldMapPoints[10] = 1;
            }

            if (CHECK_QUEST_ITEM(QUEST_SONG_LULLABY)) {
                pauseCtx->worldMapPoints[11] = 1;
            }

            if (Flags_GetEventChkInf(EVENTCHKINF_USED_DODONGOS_CAVERN_BLUE_WARP)) {
                pauseCtx->worldMapPoints[11] = 2;
            }

            if (Flags_GetEventChkInf(EVENTCHKINF_USED_JABU_JABUS_BELLY_BLUE_WARP)) {
                pauseCtx->worldMapPoints[11] = 1;
            }

            if (INV_CONTENT(ITEM_HOOKSHOT) == ITEM_HOOKSHOT) {
                pauseCtx->worldMapPoints[11] = 2;
            }

            if (CHECK_OWNED_EQUIP(EQUIP_TYPE_BOOTS, EQUIP_INV_BOOTS_IRON)) {
                pauseCtx->worldMapPoints[11] = 1;
            }

            pauseCtx->tradeQuestLocation = 0xFF;

            i = INV_CONTENT(ITEM_TRADE_ADULT);
            if (LINK_AGE_IN_YEARS == YEARS_ADULT) {
                if ((i <= ITEM_POCKET_CUCCO) || (i == ITEM_ODD_MUSHROOM)) {
                    pauseCtx->tradeQuestLocation = 8;
                }
                if ((i == ITEM_COJIRO) || (i == ITEM_ODD_POTION)) {
                    pauseCtx->tradeQuestLocation = 9;
                }
                if (i == ITEM_SAW) {
                    pauseCtx->tradeQuestLocation = 2;
                }
                if ((i == ITEM_SWORD_BROKEN) || (i == ITEM_EYEDROPS)) {
                    pauseCtx->tradeQuestLocation = 7;
                }
                if (i == ITEM_PRESCRIPTION) {
                    pauseCtx->tradeQuestLocation = 11;
                }
                if (i == ITEM_FROG) {
                    pauseCtx->tradeQuestLocation = 3;
                }
                if ((i == ITEM_CLAIM_CHECK) && (gSaveContext.bgsFlag == 0)) {
                    pauseCtx->tradeQuestLocation = 7;
                }
            }

            KaleidoScope_ResetItemCycling();

            pauseCtx->state = 4;
            break;

        case 4:
            if (pauseCtx->unk_1F4 == 160.0f) {
                KaleidoScope_SetDefaultCursor(play);
                // OTRTODO - Player on pause
                // KaleidoScope_ProcessPlayerPreRender();
            }

            pauseCtx->unk_1F4 = pauseCtx->unk_1F8 = pauseCtx->unk_1FC = pauseCtx->unk_200 -= 160.0f / WREG(6);
            pauseCtx->infoPanelOffsetY += 40 / WREG(6);
            interfaceCtx->startAlpha += 255 / WREG(6);
            WREG(16) += WREG(25) / WREG(6);
            WREG(17) += WREG(26) / WREG(6);
            XREG(5) += 150 / WREG(6);
            pauseCtx->alpha += (u16)(255 / (WREG(6) + WREG(4)));

            if (pauseCtx->unk_1F4 == 0) {
                interfaceCtx->startAlpha = 255;
                WREG(2) = 0;
                pauseCtx->state = 5;
            }

            func_808265BC(play);
            break;

        case 5:
            pauseCtx->alpha += (u16)(255 / (WREG(6) + WREG(4)));
            func_808265BC(play);
            if (pauseCtx->state == 6) {
                KaleidoScope_UpdateNamePanel(play);
            }
            break;

        case 6:
            switch (pauseCtx->unk_1E4) {
                case 0:
                    if (GameInteractor_Should(VB_CLOSE_PAUSE_MENU, CHECK_BTN_ALL(input->press.button, BTN_START))) {
                        Interface_SetDoAction(play, DO_ACTION_NONE);
                        pauseCtx->state = 0x12;
                        WREG(2) = -6240;
                        func_800F64E0(0);
                    } else if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
                        pauseCtx->mode = 0;
                        pauseCtx->promptChoice = 0;
                        Audio_PlaySfxGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                        gSaveContext.buttonStatus[0] = gSaveContext.buttonStatus[1] = gSaveContext.buttonStatus[2] =
                            gSaveContext.buttonStatus[3] = BTN_DISABLED;
                        gSaveContext.buttonStatus[4] = BTN_ENABLED;
                        gSaveContext.buttonStatus[5] = gSaveContext.buttonStatus[6] = gSaveContext.buttonStatus[7] =
                            gSaveContext.buttonStatus[8] = BTN_DISABLED;
                        gSaveContext.hudVisibilityMode = 0;
                        Interface_ChangeHudVisibilityMode(50);
                        pauseCtx->unk_1EC = 0;
                        pauseCtx->state = 7;
                    } else if (CHECK_BTN_ALL(input->press.button, BTN_CUP) && pauseCtx->cursorSpecialPos == 0) {
                        u16 descTextId =
                            PauseItemDesc_GetTextId(pauseCtx->cursorItem[pauseCtx->pageIndex], pauseCtx->pageIndex);
                        if (descTextId != 0) {
                            Audio_PlaySfxGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                            Message_StartTextbox(play, descTextId, NULL);
                            pauseCtx->unk_1E4 = 10;
                        } else if (IS_RANDO && pauseCtx->pageIndex == PAUSE_QUEST) {
                            Audio_PlaySfxGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                            pauseCtx->randoQuestMode ^= 1;
                        }
                    }
                    break;

                case 1:
                    func_808237B4(play, play->state.input);
                    break;

                case 2:
                    pauseCtx->ocarinaStaff = AudioOcarina_GetPlaybackStaff();
                    if (pauseCtx->ocarinaStaff->state == 0) {
                        pauseCtx->unk_1E4 = 4;
                        AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
                    }
                    break;

                case 3:
                    KaleidoScope_UpdateItemEquip(play);
                    break;

                case 4:
                    break;

                case 5:
                    pauseCtx->ocarinaStaff = AudioOcarina_GetPlayingStaff();

                    if (CHECK_BTN_ALL(input->press.button, BTN_START)) {
                        AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
                        Interface_SetDoAction(play, DO_ACTION_NONE);
                        pauseCtx->state = 0x12;
                        WREG(2) = -6240;
                        func_800F64E0(0);
                        pauseCtx->unk_1E4 = 0;
                        break;
                    } else if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
                        AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
                        pauseCtx->unk_1E4 = 0;
                        pauseCtx->mode = 0;
                        pauseCtx->promptChoice = 0;
                        Audio_PlaySfxGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                        gSaveContext.buttonStatus[0] = gSaveContext.buttonStatus[1] = gSaveContext.buttonStatus[2] =
                            gSaveContext.buttonStatus[3] = BTN_DISABLED;
                        gSaveContext.buttonStatus[4] = BTN_ENABLED;
                        gSaveContext.buttonStatus[5] = gSaveContext.buttonStatus[6] = gSaveContext.buttonStatus[7] =
                            gSaveContext.buttonStatus[8] = BTN_DISABLED;
                        gSaveContext.hudVisibilityMode = 0;
                        Interface_ChangeHudVisibilityMode(50);
                        pauseCtx->unk_1EC = 0;
                        pauseCtx->state = 7;
                    } else if (pauseCtx->ocarinaStaff->state == pauseCtx->ocarinaSongIdx) {
                        Audio_PlaySfxGeneral(NA_SE_SY_TRE_BOX_APPEAR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                        D_8082B258 = 0;
                        D_8082B25C = 30;
                        pauseCtx->unk_1E4 = 6;
                    } else if (pauseCtx->ocarinaStaff->state == 0xFF) {
                        Audio_PlaySfxGeneral(NA_SE_SY_OCARINA_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                        D_8082B258 = 4;
                        D_8082B25C = 20;
                        pauseCtx->unk_1E4 = 6;
                    }
                    break;

                case 6:
                    D_8082B25C--;
                    if (D_8082B25C == 0) {
                        pauseCtx->unk_1E4 = D_8082B258;
                        if (pauseCtx->unk_1E4 == 0) {
                            AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
                        }
                    }
                    break;

                case 7:
                    break;

                case 8:
                    if (CHECK_BTN_ALL(input->press.button, BTN_START)) {
                        AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
                        Interface_SetDoAction(play, DO_ACTION_NONE);
                        pauseCtx->state = 0x12;
                        WREG(2) = -6240;
                        func_800F64E0(0);
                        pauseCtx->unk_1E4 = 0;
                    } else if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
                        AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
                        pauseCtx->unk_1E4 = 0;
                        pauseCtx->mode = 0;
                        pauseCtx->promptChoice = 0;
                        Audio_PlaySfxGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                        gSaveContext.buttonStatus[0] = gSaveContext.buttonStatus[1] = gSaveContext.buttonStatus[2] =
                            gSaveContext.buttonStatus[3] = BTN_DISABLED;
                        gSaveContext.buttonStatus[4] = BTN_ENABLED;
                        gSaveContext.buttonStatus[5] = gSaveContext.buttonStatus[6] = gSaveContext.buttonStatus[7] =
                            gSaveContext.buttonStatus[8] = BTN_DISABLED;
                        gSaveContext.hudVisibilityMode = 0;
                        Interface_ChangeHudVisibilityMode(50);
                        pauseCtx->unk_1EC = 0;
                        pauseCtx->state = 7;
                    }
                    break;

                case 9:
                    break;

                case 10: // C-Up item description textbox active
                    // Message_Update doesn't run during pause, so drive it manually
                    Message_Update(play);
                    if (CHECK_BTN_ALL(input->press.button, BTN_B) || CHECK_BTN_ALL(input->press.button, BTN_A) ||
                        play->msgCtx.msgMode == MSGMODE_NONE) {
                        Message_CloseTextbox(play);
                        pauseCtx->unk_1E4 = 0;
                    }
                    break;

                case 11: { // Quartz of Motion category list (Stone of Agony L2)
                    // Owning a non-zero sub-state is what keeps the state-0
                    // branch — the one that closes the pause menu on B/START —
                    // from running while the list is up.
                    extern void Quartz_UpdateModal(PlayState * play, Input * input);
                    Quartz_UpdateModal(play, input);
                    break;
                }

                default:
                    pauseCtx->unk_1E4 = 0;
                    break;
            }
            break;

        case 7:
            if (GameInteractor_Should(VB_LOAD_SAVE_MENU, 1, play)) {
                switch (pauseCtx->unk_1EC) {
                    case 0:
                        pauseCtx->unk_204 -= 314.0f / WREG(6);
                        WREG(16) -= WREG(25) / WREG(6);
                        WREG(17) -= WREG(26) / WREG(6);
                        if (pauseCtx->unk_204 <= -628.0f) {
                            pauseCtx->unk_204 = -628.0f;
                            pauseCtx->unk_1EC = 1;
                        }
                        break;

                    case 1:
                        if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
                            if (pauseCtx->promptChoice != 0) {
                                Interface_SetDoAction(play, DO_ACTION_NONE);
                                gSaveContext.buttonStatus[0] = gSaveContext.buttonStatus[1] =
                                    gSaveContext.buttonStatus[2] = gSaveContext.buttonStatus[3] = BTN_ENABLED;
                                gSaveContext.buttonStatus[5] = gSaveContext.buttonStatus[6] =
                                    gSaveContext.buttonStatus[7] = gSaveContext.buttonStatus[8] = BTN_ENABLED;
                                gSaveContext.hudVisibilityMode = 0;
                                Interface_ChangeHudVisibilityMode(50);
                                pauseCtx->unk_1EC = 2;
                                WREG(2) = -6240;
                                YREG(8) = pauseCtx->unk_204;
                                func_800F64E0(0);
                            } else {
                                Audio_PlaySfxGeneral(NA_SE_SY_PIECE_OF_HEART, &gSfxDefaultPos, 4,
                                                     &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                                     &gSfxDefaultReverb);
                                Play_PerformSave(play);
                                pauseCtx->unk_1EC = 4;
                                D_8082B25C = CVarGetInteger(CVAR_ENHANCEMENT("SkipSaveConfirmation"), 0)
                                                 ? 3 /* 0.1 sec */
                                                 : 90 /* 3 secs */;
                            }
                        } else if (CHECK_BTN_ALL(input->press.button, BTN_START) ||
                                   CHECK_BTN_ALL(input->press.button, BTN_B)) {
                            Interface_SetDoAction(play, DO_ACTION_NONE);
                            pauseCtx->unk_1EC = 2;
                            WREG(2) = -6240;
                            YREG(8) = pauseCtx->unk_204;
                            func_800F64E0(0);
                            gSaveContext.buttonStatus[0] = gSaveContext.buttonStatus[1] = gSaveContext.buttonStatus[2] =
                                gSaveContext.buttonStatus[3] = BTN_ENABLED;
                            gSaveContext.buttonStatus[5] = gSaveContext.buttonStatus[6] = gSaveContext.buttonStatus[7] =
                                gSaveContext.buttonStatus[8] = BTN_ENABLED;
                            gSaveContext.hudVisibilityMode = 0;
                            Interface_ChangeHudVisibilityMode(50);
                        }
                        break;

                    case 4:
                        if (CHECK_BTN_ALL(input->press.button, BTN_B) || CHECK_BTN_ALL(input->press.button, BTN_A) ||
                            CHECK_BTN_ALL(input->press.button, BTN_START) || (--D_8082B25C == 0)) {
                            Interface_SetDoAction(play, DO_ACTION_NONE);
                            gSaveContext.buttonStatus[0] = gSaveContext.buttonStatus[1] = gSaveContext.buttonStatus[2] =
                                gSaveContext.buttonStatus[3] = BTN_ENABLED;
                            gSaveContext.buttonStatus[5] = gSaveContext.buttonStatus[6] = gSaveContext.buttonStatus[7] =
                                gSaveContext.buttonStatus[8] = BTN_ENABLED;
                            gSaveContext.hudVisibilityMode = 0;
                            Interface_ChangeHudVisibilityMode(50);
                            pauseCtx->unk_1EC = 5;
                            WREG(2) = -6240;
                            YREG(8) = pauseCtx->unk_204;
                            func_800F64E0(0);
                        }
                        break;

                    case 3:
                    case 6:
                        pauseCtx->unk_204 += 314.0f / WREG(6);
                        WREG(16) += WREG(25) / WREG(6);
                        WREG(17) += WREG(26) / WREG(6);
                        if (pauseCtx->unk_204 >= -314.0f) {
                            pauseCtx->state = 6;
                            pauseCtx->unk_1EC = 0;
                            pauseCtx->unk_1F4 = pauseCtx->unk_1F8 = pauseCtx->unk_1FC = pauseCtx->unk_200 = 0.0f;
                            pauseCtx->unk_204 = -314.0f;
                        }
                        break;

                    case 2:
                    case 5:
                        if (pauseCtx->unk_204 != (YREG(8) + 160.0f)) {
                            pauseCtx->unk_1F4 = pauseCtx->unk_1F8 = pauseCtx->unk_1FC = pauseCtx->unk_200 +=
                                160.0f / WREG(6);
                            pauseCtx->unk_204 += 160.0f / WREG(6);
                            pauseCtx->infoPanelOffsetY -= 40 / WREG(6);
                            WREG(16) -= WREG(25) / WREG(6);
                            WREG(17) -= WREG(26) / WREG(6);
                            XREG(5) -= 150 / WREG(6);
                            pauseCtx->alpha -= (u16)(255 / WREG(6));
                            if (pauseCtx->unk_204 == (YREG(8) + 160.0f)) {
                                pauseCtx->alpha = 0;
                            }
                        } else {
                            pauseCtx->debugState = 0;
                            pauseCtx->state = 0x13;
                            pauseCtx->unk_1F4 = pauseCtx->unk_1F8 = pauseCtx->unk_1FC = pauseCtx->unk_200 = 160.0f;
                            pauseCtx->namedItem = PAUSE_ITEM_NONE;
                            pauseCtx->unk_1E4 = 0;
                            pauseCtx->unk_204 = -434.0f;
                        }
                        break;
                }
            }
            break;

        case 0xA:
            pauseCtx->cursorSlot[PAUSE_MAP] = pauseCtx->cursorPoint[PAUSE_MAP] = pauseCtx->dungeonMapSlot =
                VREG(30) + 3;
            WREG(16) = -175;
            WREG(17) = 155;
            pauseCtx->unk_204 = -434.0f;
            Interface_ChangeHudVisibilityMode(1);

#if 1
            pauseCtx->iconItemSegment = (void*)(((uintptr_t)play->objectCtx.spaceStart + 0x30) & ~0x3F);
            size0 = (uintptr_t)_icon_item_staticSegmentRomEnd - (uintptr_t)_icon_item_staticSegmentRomStart;
            osSyncPrintf("icon_item size0=%x\n", size0);
            DmaMgr_SendRequest1(pauseCtx->iconItemSegment, (uintptr_t)_icon_item_staticSegmentRomStart, size0, __FILE__,
                                __LINE__);

            pauseCtx->iconItem24Segment = (void*)(((uintptr_t)pauseCtx->iconItemSegment + size0 + 0xF) & ~0xF);
            // size = (uintptr_t)_icon_item_24_staticSegmentRomEnd - (uintptr_t)_icon_item_24_staticSegmentRomStart;
            osSyncPrintf("icon_item24 size=%x\n", size);
            // DmaMgr_SendRequest1(pauseCtx->iconItem24Segment, (uintptr_t)_icon_item_24_staticSegmentRomStart, size,
            //__FILE__, __LINE__);

            pauseCtx->iconItemAltSegment = (void*)(((uintptr_t)pauseCtx->iconItem24Segment + size + 0xF) & ~0xF);
            // size2 = (uintptr_t)_icon_item_gameover_staticSegmentRomEnd -
            // (uintptr_t)_icon_item_gameover_staticSegmentRomStart; osSyncPrintf("icon_item_dungeon
            // gameover-size2=%x\n", size2); DmaMgr_SendRequest1(pauseCtx->iconItemAltSegment,
            // (uintptr_t)_icon_item_gameover_staticSegmentRomStart, size2,
            //__FILE__, __LINE__);

            pauseCtx->iconItemLangSegment = (void*)(((uintptr_t)pauseCtx->iconItemAltSegment + size2 + 0xF) & ~0xF);

            /*
            if (gSaveContext.language == LANGUAGE_ENG) {
                size = (uintptr_t)_icon_item_nes_staticSegmentRomEnd - (uintptr_t)_icon_item_nes_staticSegmentRomStart;
                osSyncPrintf("icon_item_dungeon dungeon-size=%x\n", size);
                DmaMgr_SendRequest1(pauseCtx->iconItemLangSegment, (uintptr_t)_icon_item_nes_staticSegmentRomStart,
            size,
                                    __FILE__, __LINE__);
            } else if (gSaveContext.language == LANGUAGE_GER) {
                size = (uintptr_t)_icon_item_ger_staticSegmentRomEnd - (uintptr_t)_icon_item_ger_staticSegmentRomStart;
                osSyncPrintf("icon_item_dungeon dungeon-size=%x\n", size);
                DmaMgr_SendRequest1(pauseCtx->iconItemLangSegment, (uintptr_t)_icon_item_ger_staticSegmentRomStart,
            size,
                                    __FILE__, __LINE__);
            } else {
                size = (uintptr_t)_icon_item_fra_staticSegmentRomEnd - (uintptr_t)_icon_item_fra_staticSegmentRomStart;
                osSyncPrintf("icon_item_dungeon dungeon-size=%x\n", size);
                DmaMgr_SendRequest1(pauseCtx->iconItemLangSegment, (uintptr_t)_icon_item_fra_staticSegmentRomStart,
            size,
                                    __FILE__, __LINE__);
            }
            */
#endif

            D_8082AB8C = 255;
            D_8082AB90 = 130;
            D_8082AB94 = 0;
            D_8082AB98 = 0;
            D_8082AB9C = 30;
            D_8082ABA0 = 0;
            D_8082ABA4 = 0;
            D_8082B260 = 30;
            VREG(88) = 98;
            pauseCtx->promptChoice = 0;
            pauseCtx->state++;
            break;

        case 0xB:
            stepR = ABS(D_8082AB8C - 30) / D_8082B260;
            stepG = ABS(D_8082AB90) / D_8082B260;
            stepB = ABS(D_8082AB94) / D_8082B260;
            stepA = ABS(D_8082AB98 - 255) / D_8082B260;
            if (D_8082AB8C >= 30) {
                D_8082AB8C -= stepR;
            } else {
                D_8082AB8C += stepR;
            }
            if (D_8082AB90 >= 0) {
                D_8082AB90 -= stepG;
            } else {
                D_8082AB90 += stepG;
            }
            if (D_8082AB94 >= 0) {
                D_8082AB94 -= stepB;
            } else {
                D_8082AB94 += stepB;
            }
            if (D_8082AB98 >= 255) {
                D_8082AB98 -= stepA;
            } else {
                D_8082AB98 += stepA;
            }

            stepR = ABS(D_8082AB9C - 255) / D_8082B260;
            stepG = ABS(D_8082ABA0 - 130) / D_8082B260;
            stepB = ABS(D_8082ABA4) / D_8082B260;
            if (D_8082AB9C >= 255) {
                D_8082AB9C -= stepR;
            } else {
                D_8082AB9C += stepR;
            }
            if (D_8082ABA0 >= 130) {
                D_8082ABA0 -= stepG;
            } else {
                D_8082ABA0 += stepG;
            }
            if (D_8082ABA4 >= 0) {
                D_8082ABA4 -= stepB;
            } else {
                D_8082ABA4 += stepB;
            }

            D_8082B260--;
            if (D_8082B260 == 0) {
                D_8082AB8C = 30;
                D_8082AB90 = 0;
                D_8082AB94 = 0;
                D_8082AB98 = 255;

                D_8082AB9C = 255;
                D_8082ABA0 = 130;
                D_8082ABA4 = 0;

                pauseCtx->state++;
                D_8082B260 = 40;
            }
            break;

        case 0xC:
            D_8082B260--;
            if (D_8082B260 == 0) {
                pauseCtx->state = 0xD;
            }
            break;

        case 0xD:
            pauseCtx->unk_1F4 = pauseCtx->unk_1F8 = pauseCtx->unk_1FC = pauseCtx->unk_200 = pauseCtx->unk_204 -=
                160.0f / WREG(6);
            pauseCtx->infoPanelOffsetY += 40 / WREG(6);
            interfaceCtx->startAlpha += 255 / WREG(6);
            VREG(88) -= 3;
            WREG(16) += WREG(25) / WREG(6);
            WREG(17) += WREG(26) / WREG(6);
            XREG(5) += 150 / WREG(6);
            pauseCtx->alpha += (u16)(255 / (WREG(6) + WREG(4)));
            if (pauseCtx->unk_204 < -628.0f) {
                pauseCtx->unk_204 = -628.0f;
                interfaceCtx->startAlpha = 255;
                VREG(88) = 66;
                WREG(2) = 0;
                pauseCtx->alpha = 255;
                if (GameInteractor_Should(VB_TRANSITION_TO_SAVE_SCREEN_ON_DEATH, true, pauseCtx)) {
                    pauseCtx->state = 0xE;
                }
                gSaveContext.deaths++;
                if (gSaveContext.deaths > 999) {
                    gSaveContext.deaths = 999;
                }
            }
            osSyncPrintf("kscope->angle_s = %f\n", pauseCtx->unk_204);
            break;

        case 0xE:
            if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
                if (pauseCtx->promptChoice != 0) {
                    pauseCtx->promptChoice = 0;
                    Audio_PlaySfxGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    pauseCtx->state = 0x10;
                    gameOverCtx->state++;
                } else {
                    Audio_PlaySfxGeneral(NA_SE_SY_PIECE_OF_HEART, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    pauseCtx->promptChoice = 0;
                    Play_SaveSceneFlags(play);
                    gSaveContext.savedSceneNum = play->sceneNum;
                    Save_SaveFile();
                    pauseCtx->state = 0xF;
                    D_8082B25C =
                        CVarGetInteger(CVAR_ENHANCEMENT("SkipSaveConfirmation"), 0) ? 3 /* 0.1 sec */ : 90 /* 3 secs */;
                }
            }
            break;

        case 0xF:
            D_8082B25C--;
            if (D_8082B25C == 0) {
                pauseCtx->state = 0x10;
                gameOverCtx->state++;
            } else if ((D_8082B25C <= 80) &&
                       (CHECK_BTN_ALL(input->press.button, BTN_A) || CHECK_BTN_ALL(input->press.button, BTN_START))) {
                pauseCtx->state = 0x10;
                gameOverCtx->state++;
                func_800F64E0(0);
            }
            break;

        case 0x10:
            if (CHECK_BTN_ALL(input->press.button, BTN_A) || CHECK_BTN_ALL(input->press.button, BTN_START)) {
                if (pauseCtx->promptChoice == 0 && GameInteractor_Should(VB_BE_ABLE_TO_SAVE, true)) {
                    Audio_PlaySfxGeneral(NA_SE_SY_PIECE_OF_HEART, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    Play_SaveSceneFlags(play);

                    switch (gSaveContext.entranceIndex) {
                        case ENTR_DEKU_TREE_ENTRANCE:
                        case ENTR_DODONGOS_CAVERN_ENTRANCE:
                        case ENTR_JABU_JABU_ENTRANCE:
                        case ENTR_FOREST_TEMPLE_ENTRANCE:
                        case ENTR_FIRE_TEMPLE_ENTRANCE:
                        case ENTR_WATER_TEMPLE_ENTRANCE:
                        case ENTR_SPIRIT_TEMPLE_ENTRANCE:
                        case ENTR_SHADOW_TEMPLE_ENTRANCE:
                        case ENTR_GANONS_TOWER_0:
                        case ENTR_GERUDO_TRAINING_GROUND_ENTRANCE:
                        case ENTR_ICE_CAVERN_ENTRANCE:
                        case ENTR_THIEVES_HIDEOUT_0:
                        case ENTR_BOTTOM_OF_THE_WELL_ENTRANCE:
                        case ENTR_INSIDE_GANONS_CASTLE_ENTRANCE:
                        case ENTR_GANONS_TOWER_COLLAPSE_INTERIOR_0:
                            break;
                        case ENTR_DEKU_TREE_BOSS_ENTRANCE:
                            gSaveContext.entranceIndex = ENTR_DEKU_TREE_ENTRANCE;
                            break;
                        case ENTR_DODONGOS_CAVERN_BOSS_ENTRANCE:
                            gSaveContext.entranceIndex = ENTR_DODONGOS_CAVERN_ENTRANCE;
                            break;
                        case ENTR_JABU_JABU_BOSS_ENTRANCE:
                            gSaveContext.entranceIndex = ENTR_JABU_JABU_ENTRANCE;
                            break;
                        case ENTR_FOREST_TEMPLE_BOSS_ENTRANCE:
                            gSaveContext.entranceIndex = ENTR_FOREST_TEMPLE_ENTRANCE;
                            break;
                        case ENTR_FIRE_TEMPLE_BOSS_ENTRANCE:
                            gSaveContext.entranceIndex = ENTR_FIRE_TEMPLE_ENTRANCE;
                            break;
                        case ENTR_WATER_TEMPLE_BOSS_ENTRANCE:
                            gSaveContext.entranceIndex = ENTR_WATER_TEMPLE_ENTRANCE;
                            break;
                        case ENTR_SPIRIT_TEMPLE_BOSS_ENTRANCE:
                            gSaveContext.entranceIndex = ENTR_SPIRIT_TEMPLE_ENTRANCE;
                            break;
                        case ENTR_SHADOW_TEMPLE_BOSS_ENTRANCE:
                            gSaveContext.entranceIndex = ENTR_SHADOW_TEMPLE_ENTRANCE;
                            break;
                        case ENTR_GANONDORF_BOSS_0:
                            gSaveContext.entranceIndex = ENTR_GANONS_TOWER_0;
                            break;
                    }
                } else {
                    Audio_PlaySfxGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                }

                pauseCtx->state = 0x11;
            }
            break;

        case 0x11:
            if (interfaceCtx->unk_244 != 255) {
                interfaceCtx->unk_244 += 10;
                if (interfaceCtx->unk_244 >= 255) {
                    interfaceCtx->unk_244 = 255;
                    pauseCtx->state = 0;
                    R_UPDATE_RATE = 3;
                    R_PAUSE_MENU_MODE = 0;
                    func_800981B8(&play->objectCtx);
                    func_800418D0(&play->colCtx, play);
                    if (pauseCtx->promptChoice == 0 && GameInteractor_Should(VB_BE_ABLE_TO_SAVE, true)) {
                        Play_TriggerRespawn(play);
                        gSaveContext.respawnFlag = -2;
                        // Reset frame counter to prevent autosave on respawn
                        play->gameplayFrames = 0;
                        gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK;
                        gSaveContext.health = CVarGetInteger(CVAR_ENHANCEMENT("FullHealthSpawn"), 0)
                                                  ? gSaveContext.healthCapacity
                                                  : STARTING_HEALTH;
                        Audio_QueueSeqCmd(0xF << 28 | SEQ_PLAYER_BGM_MAIN << 24 | 0xA);
                        gSaveContext.healthAccumulator = 0;
                        gSaveContext.magicState = MAGIC_STATE_IDLE;
                        gSaveContext.prevMagicState = MAGIC_STATE_IDLE;
                        osSyncPrintf(VT_FGCOL(YELLOW));
                        osSyncPrintf("MAGIC_NOW=%d ", gSaveContext.magic);
                        osSyncPrintf("Z_MAGIC_NOW_NOW=%d   →  ", gSaveContext.magicFillTarget);
                        gSaveContext.magicCapacity = 0;
                        gSaveContext.magicFillTarget = gSaveContext.magic;
                        gSaveContext.magicLevel = gSaveContext.magic = 0;
                        osSyncPrintf("MAGIC_NOW=%d ", gSaveContext.magic);
                        osSyncPrintf("Z_MAGIC_NOW_NOW=%d\n", gSaveContext.magicFillTarget);
                        osSyncPrintf(VT_RST);
                    } else {
                        play->state.running = 0;
                        SET_NEXT_GAMESTATE(&play->state, Opening_Init, OpeningContext);
                        GameInteractor_ExecuteOnExitGame(gSaveContext.fileNum);
                    }
                }
            }
            break;

        case 0x12:
            if (pauseCtx->unk_1F4 != 160.0f) {
                pauseCtx->unk_1F4 = pauseCtx->unk_1F8 = pauseCtx->unk_1FC = pauseCtx->unk_200 += 160.0f / WREG(6);
                pauseCtx->infoPanelOffsetY -= 40 / WREG(6);
                interfaceCtx->startAlpha -= 255 / WREG(6);
                WREG(16) -= WREG(25) / WREG(6);
                WREG(17) -= WREG(26) / WREG(6);
                XREG(5) -= 150 / WREG(6);
                pauseCtx->alpha -= (u16)(255 / WREG(6));
                if (pauseCtx->unk_1F4 == 160.0f) {
                    pauseCtx->alpha = 0;
                }
            } else {
                pauseCtx->debugState = 0;
                pauseCtx->state = 0x13;
                pauseCtx->unk_200 = 160.0f;
                pauseCtx->unk_1FC = 160.0f;
                pauseCtx->unk_1F8 = 160.0f;
                pauseCtx->unk_1F4 = 160.0f;
                pauseCtx->namedItem = PAUSE_ITEM_NONE;
                play->interfaceCtx.startAlpha = 0;
            }
            break;

        case 0x13:
            if (!GameInteractor_Should(VB_KALEIDO_UNPAUSE_CLOSE, true)) {
                break;
            }

            pauseCtx->state = 0;
            R_UPDATE_RATE = 3;
            R_PAUSE_MENU_MODE = 0;

            CVarSetInteger(CVAR_GENERAL("PauseMenuAnimatedLinkTriforce"), 0);

            func_800981B8(&play->objectCtx);
            func_800418D0(&play->colCtx, play);

            switch (play->sceneNum) {
                case SCENE_DEKU_TREE:
                case SCENE_DODONGOS_CAVERN:
                case SCENE_JABU_JABU:
                case SCENE_FOREST_TEMPLE:
                case SCENE_FIRE_TEMPLE:
                case SCENE_WATER_TEMPLE:
                case SCENE_SPIRIT_TEMPLE:
                case SCENE_SHADOW_TEMPLE:
                case SCENE_BOTTOM_OF_THE_WELL:
                case SCENE_ICE_CAVERN:
                case SCENE_DEKU_TREE_BOSS:
                case SCENE_DODONGOS_CAVERN_BOSS:
                case SCENE_JABU_JABU_BOSS:
                case SCENE_FOREST_TEMPLE_BOSS:
                case SCENE_FIRE_TEMPLE_BOSS:
                case SCENE_WATER_TEMPLE_BOSS:
                case SCENE_SPIRIT_TEMPLE_BOSS:
                case SCENE_SHADOW_TEMPLE_BOSS:
                    Map_InitData(play, play->interfaceCtx.mapRoomNum);
                    break;
            }

            for (int buttonIndex = 0; buttonIndex < ARRAY_COUNT(gSaveContext.buttonStatus); buttonIndex++) {
                gSaveContext.buttonStatus[buttonIndex] = sButtonStatusSave[buttonIndex];
            }

            // Used to clear swordless temp B after unpause so minigame/epona handling restarts
            GameInteractor_Should(VB_TEMP_B_RESTORE_SWORDLESS, true);

            interfaceCtx->unk_1FA = interfaceCtx->unk_1FC = 0;
            osSyncPrintf(VT_FGCOL(YELLOW));
            osSyncPrintf("i=%d  LAST_TIME_TYPE=%d\n", i, gSaveContext.prevHudVisibilityMode);
            gSaveContext.hudVisibilityMode = 0;
            Interface_ChangeHudVisibilityMode(gSaveContext.prevHudVisibilityMode);
            player->talkActor = NULL;
            Player_SetEquipmentData(play, player);
            osSyncPrintf(VT_RST);
            break;
    }

    GameInteractor_ExecuteOnKaleidoscopeUpdate(sInDungeonScene);
}