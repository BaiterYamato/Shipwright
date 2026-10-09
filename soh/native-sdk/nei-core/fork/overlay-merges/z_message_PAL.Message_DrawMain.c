/* overlay-merge 36c469eff387 7e3ee85e57d8 ca5b24efdc90 */
void Message_DrawMain(PlayState* play, Gfx** p) {
    static s16 sOcarinaEffectActorIds[] = {
        ACTOR_OCEFF_WIPE3, ACTOR_OCEFF_WIPE2, ACTOR_OCEFF_WIPE,  ACTOR_OCEFF_SPOT,
        ACTOR_OCEFF_WIPE,  ACTOR_OCEFF_STORM, ACTOR_OCEFF_WIPE4,
    };
    static s16 sOcarinaEffectActorParams[] = { 0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0000, 0x0000 };
    static void* sOcarinaNoteTextures[] = {
        gOcarinaBtnIconATex,     gOcarinaBtnIconCDownTex, gOcarinaBtnIconCRightTex,
        gOcarinaBtnIconCLeftTex, gOcarinaBtnIconCUpTex,
    };

    // SoH [Cosmetics] The following Color_RGB8 were originally static
    Color_RGB8 sOcarinaNoteAPrimColors[2] = {
        { 80, 150, 255 },
        { 100, 200, 255 },
    };
    Color_RGB8 sOcarinaNoteAEnvColors[2] = {
        { 10, 10, 10 },
        { 50, 50, 255 },
    };
    if (CVarGetInteger(CVAR_COSMETIC("HUD.AButton.Changed"), 0)) {
        Color_RGB8 color = CVarGetColor24(CVAR_COSMETIC("HUD.AButton.Value"), (Color_RGB8){ 100, 200, 255 });
        sOcarinaNoteAPrimColors[0].r = (color.r / 255.0f) * 95;
        sOcarinaNoteAPrimColors[0].g = (color.g / 255.0f) * 95;
        sOcarinaNoteAPrimColors[0].b = (color.b / 255.0f) * 95;
        sOcarinaNoteAPrimColors[1] = color;
        sOcarinaNoteAEnvColors[1] = color;
    } else if (CVarGetInteger(CVAR_COSMETIC("DefaultColorScheme"), COLORSCHEME_N64) == COLORSCHEME_GAMECUBE) {
        sOcarinaNoteAPrimColors[0] = (Color_RGB8){ 80, 255, 150 };
        sOcarinaNoteAPrimColors[1] = (Color_RGB8){ 100, 255, 200 };
        sOcarinaNoteAEnvColors[1] = (Color_RGB8){ 50, 255, 50 };
    }

    Color_RGB8 sOcarinaNoteCPrimColors[2] = {
        { 255, 255, 50 },
        { 255, 255, 180 },
    };
    Color_RGB8 sOcarinaNoteCEnvColors[2] = {
        { 10, 10, 10 },
        { 110, 110, 50 },
    };
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CButtons.Changed"), 0)) {
        Color_RGB8 color = CVarGetColor24(CVAR_COSMETIC("HUD.CButtons.Value"), (Color_RGB8){ 100, 200, 255 });
        sOcarinaNoteCPrimColors[0] = color;
        sOcarinaNoteCPrimColors[1] = color;
        sOcarinaNoteCEnvColors[1].r = (color.r / 255.0f) * 95;
        sOcarinaNoteCEnvColors[1].g = (color.g / 255.0f) * 95;
        sOcarinaNoteCEnvColors[1].b = (color.b / 255.0f) * 95;
    }

    Color_RGB8 sOcarinaNoteCUpPrimColors[2] = {
        { 255, 255, 50 },
        { 255, 255, 180 },
    };
    Color_RGB8 sOcarinaNoteCUpEnvColors[2] = {
        { 10, 10, 10 },
        { 110, 110, 50 },
    };
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CUpButton.Changed"), 0)) {
        Color_RGB8 color = CVarGetColor24(CVAR_COSMETIC("HUD.CUpButton.Value"), (Color_RGB8){ 100, 200, 255 });
        sOcarinaNoteCUpPrimColors[0] = color;
        sOcarinaNoteCUpPrimColors[1] = color;
        sOcarinaNoteCUpEnvColors[1].r = (color.r / 255.0f) * 95;
        sOcarinaNoteCUpEnvColors[1].g = (color.g / 255.0f) * 95;
        sOcarinaNoteCUpEnvColors[1].b = (color.b / 255.0f) * 95;
    }

    Color_RGB8 sOcarinaNoteCDownPrimColors[2] = {
        { 255, 255, 50 },
        { 255, 255, 180 },
    };
    Color_RGB8 sOcarinaNoteCDownEnvColors[2] = {
        { 10, 10, 10 },
        { 110, 110, 50 },
    };
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.Changed"), 0)) {
        Color_RGB8 color = CVarGetColor24(CVAR_COSMETIC("HUD.CDownButton.Value"), (Color_RGB8){ 100, 200, 255 });
        sOcarinaNoteCDownPrimColors[0] = color;
        sOcarinaNoteCDownPrimColors[1] = color;
        sOcarinaNoteCDownEnvColors[1].r = (color.r / 255.0f) * 95;
        sOcarinaNoteCDownEnvColors[1].g = (color.g / 255.0f) * 95;
        sOcarinaNoteCDownEnvColors[1].b = (color.b / 255.0f) * 95;
    }

    Color_RGB8 sOcarinaNoteCLeftPrimColors[2] = {
        { 255, 255, 50 },
        { 255, 255, 180 },
    };
    Color_RGB8 sOcarinaNoteCLeftEnvColors[2] = {
        { 10, 10, 10 },
        { 110, 110, 50 },
    };
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.Changed"), 0)) {
        Color_RGB8 color = CVarGetColor24(CVAR_COSMETIC("HUD.CLeftButton.Value"), (Color_RGB8){ 100, 200, 255 });
        sOcarinaNoteCLeftPrimColors[0] = color;
        sOcarinaNoteCLeftPrimColors[1] = color;
        sOcarinaNoteCLeftEnvColors[1].r = (color.r / 255.0f) * 95;
        sOcarinaNoteCLeftEnvColors[1].g = (color.g / 255.0f) * 95;
        sOcarinaNoteCLeftEnvColors[1].b = (color.b / 255.0f) * 95;
    }

    Color_RGB8 sOcarinaNoteCRightPrimColors[2] = {
        { 255, 255, 50 },
        { 255, 255, 180 },
    };
    Color_RGB8 sOcarinaNoteCRightEnvColors[2] = {
        { 10, 10, 10 },
        { 110, 110, 50 },
    };
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.Changed"), 0)) {
        Color_RGB8 color = CVarGetColor24(CVAR_COSMETIC("HUD.CRightButton.Value"), (Color_RGB8){ 100, 200, 255 });
        sOcarinaNoteCRightPrimColors[0] = color;
        sOcarinaNoteCRightPrimColors[1] = color;
        sOcarinaNoteCRightEnvColors[1].r = (color.r / 255.0f) * 95;
        sOcarinaNoteCRightEnvColors[1].g = (color.g / 255.0f) * 95;
        sOcarinaNoteCRightEnvColors[1].b = (color.b / 255.0f) * 95;
    }

    static s16 sOcarinaNoteFlashTimer = 12;
    static s16 sOcarinaNoteFlashColorIdx = 1;
    static s16 sOcarinaSongFanfares[] = {
        NA_BGM_OCA_MINUET,   NA_BGM_OCA_BOLERO, NA_BGM_OCA_SERENADE, NA_BGM_OCA_REQUIEM,
        NA_BGM_OCA_NOCTURNE, NA_BGM_OCA_LIGHT,  NA_BGM_OCA_SARIA,    NA_BGM_OCA_EPONA,
        NA_BGM_OCA_ZELDA,    NA_BGM_OCA_SUNS,   NA_BGM_OCA_TIME,     NA_BGM_OCA_STORM,
    };
    InterfaceContext* interfaceCtx = &play->interfaceCtx;
    MessageContext* msgCtx = &play->msgCtx;
    u16 noteBufPos;
    Player* player = GET_PLAYER(play);
    s32 pad;
    Gfx* gfx = *p;
    s16 r;
    s16 g;
    s16 b;
    u16 i;
    u16 notePosX;
    u16 pad1;
    u16 j;

    gSPSegment(gfx++, 0x02, play->interfaceCtx.parameterSegment);
    gSPSegment(gfx++, 0x07, msgCtx->textboxSegment);

    if (msgCtx->msgLength != 0) {
        // #region SOH [NTSC] - allow switching languages mid text
        if (gSaveContext.language != sLastLanguage) {
            u16 drawPos = msgCtx->textDrawPos;
            u16 choiceNum = msgCtx->choiceNum;
            u16 textUnskippable = msgCtx->textUnskippable;
            u16 textboxEndType = msgCtx->textboxEndType;
            u8 msgMode = msgCtx->msgMode;
            s16 textboxColorAlphaCurrent = msgCtx->textboxColorAlphaCurrent;
            s32 textBoxNum;
            s32 textBoxMax = sTextBoxNum - 1;
            bool decodeOk;
            Message_OpenText(play, msgCtx->textId);
            decodeOk = Message_DecodeChecked(play);
            // Move to correct textbox
            for (textBoxNum = 0; decodeOk && textBoxNum < textBoxMax; textBoxNum++) {
                msgCtx->msgBufPos++;
                decodeOk = Message_DecodeChecked(play);
            }
            if (decodeOk) {
                // SOH [Link-Span] R04: nao restaurar controles apos descarte.
                msgCtx->textDrawPos = (drawPos > msgCtx->decodedTextLen) ? msgCtx->decodedTextLen : drawPos;
                msgCtx->choiceNum = choiceNum;
                msgCtx->textUnskippable = textUnskippable;
                msgCtx->textboxEndType = textboxEndType;
                msgCtx->msgMode = msgMode;
                msgCtx->textboxColorAlphaCurrent = textboxColorAlphaCurrent;
            }
        }
        // #endregion

        if (msgCtx->ocarinaAction != OCARINA_ACTION_FROGS && msgCtx->msgMode != MSGMODE_SONG_PLAYED_ACT &&
            msgCtx->msgMode >= MSGMODE_TEXT_BOX_GROWING && msgCtx->msgMode < MSGMODE_TEXT_CLOSING &&
            msgCtx->textBoxType < TEXTBOX_TYPE_NONE_BOTTOM) {
            Message_SetView(&msgCtx->view);
            Gfx_SetupDL_39Ptr(&gfx);
            Message_DrawTextBox(play, &gfx);
        }

        Gfx_SetupDL_39Ptr(&gfx);

        gDPSetAlphaCompare(gfx++, G_AC_NONE);
        gDPSetCombineLERP(gfx++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE,
                          0);

        bool isB_Held = CVarGetInteger(CVAR_ENHANCEMENT("SkipText"), 0) != 0
                            ? CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_B)
                            : CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B);

        switch (msgCtx->msgMode) {
            case MSGMODE_TEXT_START:
            case MSGMODE_TEXT_BOX_GROWING:
            case MSGMODE_TEXT_STARTING:
            case MSGMODE_TEXT_NEXT_MSG:
                break;
            case MSGMODE_TEXT_CONTINUING:
                if (msgCtx->stateTimer == 1) {
                    for (j = 0, i = 0; i < 48; i++) {
                        Font_LoadCharWide(&play->msgCtx.font, 0x8140, j);
                        j += FONT_CHAR_TEX_SIZE;
                    }
                    if (gSaveContext.language == LANGUAGE_JPN && !sTextIsCredits && !sDisplayNextMessageAsEnglish) {
                        Message_DrawTextJPN(play, &gfx);
                    } else {
                        Message_DrawText(play, &gfx);
                    }
                }
                break;
            case MSGMODE_TEXT_DISPLAYING:
            case MSGMODE_TEXT_DELAYED_BREAK:
                if (gSaveContext.language == LANGUAGE_JPN && !sTextIsCredits && !sDisplayNextMessageAsEnglish) {
                    Message_DrawTextJPN(play, &gfx);
                } else {
                    Message_DrawText(play, &gfx);
                }
                break;
            case MSGMODE_TEXT_AWAIT_INPUT:
            case MSGMODE_TEXT_AWAIT_NEXT:
                if (gSaveContext.language == LANGUAGE_JPN && !sTextIsCredits && !sDisplayNextMessageAsEnglish) {
                    Message_DrawTextJPN(play, &gfx);
                } else {
                    Message_DrawText(play, &gfx);
                }
                Message_DrawTextboxIcon(play, &gfx, R_TEXTBOX_END_XPOS, R_TEXTBOX_END_YPOS);
                break;
            case MSGMODE_OCARINA_STARTING:
            case MSGMODE_SONG_DEMONSTRATION_STARTING:
            case MSGMODE_SONG_PLAYBACK_STARTING:
                AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_DEFAULT);
                msgCtx->ocarinaStaff = AudioOcarina_GetPlayingStaff();
                msgCtx->ocarinaStaff->pos = sOcarinaButtonIndexBufPos = 0;
                play->msgCtx.ocarinaMode = OCARINA_MODE_01;
                Message_ResetOcarinaNoteState();
                sOcarinaNoteFlashTimer = 3;
                sOcarinaNoteFlashColorIdx = 1;
                if (msgCtx->msgMode == MSGMODE_OCARINA_STARTING) {
                    if (msgCtx->ocarinaAction == OCARINA_ACTION_UNK_0 ||
                        msgCtx->ocarinaAction == OCARINA_ACTION_FREE_PLAY ||
                        msgCtx->ocarinaAction == OCARINA_ACTION_SCARECROW_RECORDING ||
                        msgCtx->ocarinaAction == OCARINA_ACTION_CHECK_NOWARP ||
                        msgCtx->ocarinaAction >= OCARINA_ACTION_CHECK_SARIA) {
                        if (msgCtx->ocarinaAction == OCARINA_ACTION_FREE_PLAY ||
                            msgCtx->ocarinaAction == OCARINA_ACTION_CHECK_NOWARP) {
                            AudioOcarina_Start(sOcarinaSongBitFlags + 0xC000);
                        } else {
                            // "On Stage Performance"
                            osSyncPrintf("台上演奏\n");
                            AudioOcarina_Start(sOcarinaSongBitFlags);
                        }
                    } else {
                        osSyncPrintf("Na_StartOcarinaSinglePlayCheck2( message->ocarina_no );\n");
                        AudioOcarina_Start((1 << (msgCtx->ocarinaAction % 32)) + 0x8000);
                    }
                    msgCtx->msgMode = MSGMODE_OCARINA_PLAYING;
                } else if (msgCtx->msgMode == MSGMODE_SONG_DEMONSTRATION_STARTING) {
                    msgCtx->stateTimer = 20;
                    msgCtx->msgMode = MSGMODE_SONG_DEMONSTRATION_SELECT_INSTRUMENT;
                } else {
                    AudioOcarina_Start((1 << ((msgCtx->ocarinaAction + 0x11) % 32)) + 0x8000);
                    // "Performance Check"
                    osSyncPrintf("演奏チェック=%d\n", msgCtx->ocarinaAction - OCARINA_ACTION_PLAYBACK_MINUET);
                    msgCtx->msgMode = MSGMODE_SONG_PLAYBACK;
                }
                if (msgCtx->ocarinaAction != OCARINA_ACTION_FREE_PLAY &&
                    msgCtx->ocarinaAction != OCARINA_ACTION_CHECK_NOWARP) {
                    if (gSaveContext.language == LANGUAGE_JPN && !sTextIsCredits && !sDisplayNextMessageAsEnglish) {
                        Message_DrawTextJPN(play, &gfx);
                    } else {
                        Message_DrawText(play, &gfx);
                    }
                }
                break;
            case MSGMODE_OCARINA_PLAYING:
                msgCtx->ocarinaStaff = AudioOcarina_GetPlayingStaff();

                // Skijer's NEI "Pause Play": deterministic forced-success handoff (mirror of the 2ship
                // side — the audio-side played-song latch survives only one ocarina-update tick, so the
                // quest page hands the song here directly and we stamp it into the staff state, which
                // the chain below consumes synchronously).
                {
                    extern s16 gNeiPausePlayForcedSong; // z_kaleido_collect.c
                    if ((gNeiPausePlayForcedSong >= 0) && (msgCtx->ocarinaAction == OCARINA_ACTION_FREE_PLAY)) {
                        msgCtx->ocarinaStaff->state = (u8)gNeiPausePlayForcedSong;
                        gNeiPausePlayForcedSong = -1;
                    }
                }

                if (msgCtx->ocarinaStaff->pos) {
                    osSyncPrintf("locate=%d  onpu_pt=%d\n", msgCtx->ocarinaStaff->pos, sOcarinaButtonIndexBufPos);
                    if (msgCtx->ocarinaStaff->pos == 1 && sOcarinaButtonIndexBufPos == 8) {
                        sOcarinaButtonIndexBufPos = 0;
                    }
                    if (sOcarinaButtonIndexBufPos == msgCtx->ocarinaStaff->pos - 1) {
                        msgCtx->lastOcaNoteIdx = sOcarinaButtonIndexBuf[msgCtx->ocarinaStaff->pos - 1] =
                            msgCtx->ocarinaStaff->buttonIndex;
                        sOcarinaButtonIndexBuf[msgCtx->ocarinaStaff->pos] = OCARINA_BTN_INVALID;
                        sOcarinaButtonIndexBufPos++;
                    }
                }
                msgCtx->lastPlayedSong = msgCtx->ocarinaStaff->state;

                // Skijer's NEI: MM songs + customs (slots 14-23). The vanilla chain below only knows
                // songs < MEMORY_GAME (its owned-check indexes gOcarinaSongItemMap — OOB for 14+), so
                // run the same FREE_PLAY success path for them here: generic ocarina textbox +
                // MSGMODE_SONG_PLAYED, which then replays the melody like any other song.
                if ((msgCtx->ocarinaAction == OCARINA_ACTION_FREE_PLAY) &&
                    (msgCtx->ocarinaStaff->state >= OCARINA_SONG_MM_FIRST) &&
                    (msgCtx->ocarinaStaff->state < OCARINA_SONG_MAX)) {
                    sLastPlayedSong = msgCtx->unk_E3F2 = msgCtx->lastPlayedSong = msgCtx->ocarinaStaff->state;
                    Message_ContinueTextbox(play, 0x86F); // Ocarina staff box
                    msgCtx->msgMode = MSGMODE_SONG_PLAYED;
                    msgCtx->textBoxType = TEXTBOX_TYPE_OCARINA;
                    msgCtx->stateTimer = 10;
                    Audio_PlaySoundGeneral(NA_SE_SY_TRE_BOX_APPEAR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    Interface_ChangeHudVisibilityMode(1);
                    break;
                }

                if (msgCtx->ocarinaStaff->state < OCARINA_SONG_MEMORY_GAME) {
                    if (msgCtx->ocarinaStaff->state == OCARINA_SONG_SCARECROW_SPAWN ||
                        CHECK_QUEST_ITEM(QUEST_SONG_MINUET + gOcarinaSongItemMap[msgCtx->ocarinaStaff->state])) {
                        sLastPlayedSong = msgCtx->unk_E3F2 = msgCtx->lastPlayedSong = msgCtx->ocarinaStaff->state;
                        msgCtx->msgMode = MSGMODE_OCARINA_CORRECT_PLAYBACK;
                        msgCtx->stateTimer = 20;
                        if (msgCtx->ocarinaAction == OCARINA_ACTION_CHECK_NOWARP) {
                            if (msgCtx->ocarinaStaff->state < OCARINA_SONG_SARIAS ||
                                msgCtx->ocarinaStaff->state == OCARINA_SONG_SCARECROW_SPAWN) {
                                AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
                                Audio_PlaySfxGeneral(NA_SE_SY_OCARINA_ERROR, &gSfxDefaultPos, 4,
                                                     &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                                     &gSfxDefaultReverb);
                                msgCtx->msgMode = MSGMODE_OCARINA_STARTING;
                            } else {
                                // "Ocarina_Flog Correct Example Performance"
                                osSyncPrintf("Ocarina_Flog 正解模範演奏=%x\n", msgCtx->lastPlayedSong);
                                Message_ContinueTextbox(play, 0x86F); // Ocarina
                                msgCtx->msgMode = MSGMODE_SONG_PLAYED;
                                msgCtx->textBoxType = TEXTBOX_TYPE_OCARINA;
                                msgCtx->stateTimer = 10;
                                Audio_PlaySfxGeneral(NA_SE_SY_TRE_BOX_APPEAR, &gSfxDefaultPos, 4,
                                                     &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                                     &gSfxDefaultReverb);
                                Interface_ChangeHudVisibilityMode(1);
                            }
                        } else if (msgCtx->ocarinaAction == OCARINA_ACTION_CHECK_SCARECROW) {
                            if (msgCtx->ocarinaStaff->state < OCARINA_SONG_SCARECROW_SPAWN) {
                                AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
                                Audio_PlaySfxGeneral(NA_SE_SY_OCARINA_ERROR, &gSfxDefaultPos, 4,
                                                     &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                                     &gSfxDefaultReverb);
                                msgCtx->stateTimer = 10;
                                msgCtx->msgMode = MSGMODE_OCARINA_FAIL;
                            } else {
                                // "Ocarina_Flog Correct Example Performance"
                                osSyncPrintf("Ocarina_Flog 正解模範演奏=%x\n", msgCtx->lastPlayedSong);
                                Message_ContinueTextbox(play, 0x86F); // Ocarina
                                msgCtx->msgMode = MSGMODE_SONG_PLAYED;
                                msgCtx->textBoxType = TEXTBOX_TYPE_OCARINA;
                                msgCtx->stateTimer = 10;
                                Audio_PlaySfxGeneral(NA_SE_SY_TRE_BOX_APPEAR, &gSfxDefaultPos, 4,
                                                     &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                                     &gSfxDefaultReverb);
                                Interface_ChangeHudVisibilityMode(1);
                            }
                        } else if (msgCtx->ocarinaAction == OCARINA_ACTION_FREE_PLAY) {
                            // "Ocarina_Free Correct Example Performance"
                            osSyncPrintf("Ocarina_Free 正解模範演奏=%x\n", msgCtx->lastPlayedSong);
                            Message_ContinueTextbox(play, 0x86F); // Ocarina
                            msgCtx->msgMode = MSGMODE_SONG_PLAYED;
                            msgCtx->textBoxType = TEXTBOX_TYPE_OCARINA;
                            msgCtx->stateTimer = 10;
                            Audio_PlaySfxGeneral(NA_SE_SY_TRE_BOX_APPEAR, &gSfxDefaultPos, 4,
                                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                                 &gSfxDefaultReverb);
                        } else {
                            Audio_PlaySfxGeneral(NA_SE_SY_TRE_BOX_APPEAR, &gSfxDefaultPos, 4,
                                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                                 &gSfxDefaultReverb);
                        }
                        Interface_ChangeHudVisibilityMode(1);
                    } else {
                        AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
                        Audio_PlaySfxGeneral(NA_SE_SY_OCARINA_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                        msgCtx->msgMode = MSGMODE_OCARINA_STARTING;
                    }
                } else if (msgCtx->ocarinaStaff->state == 0xFF) {
                    AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
                    Audio_PlaySfxGeneral(NA_SE_SY_OCARINA_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    msgCtx->stateTimer = 10;
                    msgCtx->msgMode = MSGMODE_OCARINA_FAIL;
                } else if (isB_Held) {
                    AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
                    play->msgCtx.ocarinaMode = OCARINA_MODE_04;
                    Message_CloseTextbox(play);
                }
                if (msgCtx->ocarinaAction != OCARINA_ACTION_FREE_PLAY &&
                    msgCtx->ocarinaAction != OCARINA_ACTION_CHECK_NOWARP) {
                    if (gSaveContext.language == LANGUAGE_JPN && !sTextIsCredits && !sDisplayNextMessageAsEnglish) {
                        Message_DrawTextJPN(play, &gfx);
                    } else {
                        Message_DrawText(play, &gfx);
                    }
                }
                break;
            case MSGMODE_OCARINA_CORRECT_PLAYBACK:
            case MSGMODE_SONG_PLAYBACK_SUCCESS:
            case MSGMODE_SCARECROW_RECORDING_DONE:
                FLASH_NOTE_COLORS(sOcarinaNoteABtnPrim, sOcarinaNoteAPrimColors)
                FLASH_NOTE_COLORS(sOcarinaNoteABtnEnv, sOcarinaNoteAEnvColors)
                FLASH_NOTE_COLORS(sOcarinaNoteCBtnPrim, sOcarinaNoteCPrimColors)
                FLASH_NOTE_COLORS(sOcarinaNoteCBtnEnv, sOcarinaNoteCEnvColors)
                FLASH_NOTE_COLORS(sOcarinaNoteCUpBtnPrim, sOcarinaNoteCUpPrimColors)
                FLASH_NOTE_COLORS(sOcarinaNoteCBtnEnv, sOcarinaNoteCUpEnvColors)
                FLASH_NOTE_COLORS(sOcarinaNoteCDownBtnPrim, sOcarinaNoteCDownPrimColors)
                FLASH_NOTE_COLORS(sOcarinaNoteCBtnEnv, sOcarinaNoteCDownEnvColors)
                FLASH_NOTE_COLORS(sOcarinaNoteCLeftBtnPrim, sOcarinaNoteCLeftPrimColors)
                FLASH_NOTE_COLORS(sOcarinaNoteCBtnEnv, sOcarinaNoteCLeftEnvColors)
                FLASH_NOTE_COLORS(sOcarinaNoteCRightBtnPrim, sOcarinaNoteCRightPrimColors)
                FLASH_NOTE_COLORS(sOcarinaNoteCBtnEnv, sOcarinaNoteCRightEnvColors)

                sOcarinaNoteFlashTimer--;
                if (sOcarinaNoteFlashTimer == 0) {
                    sOcarinaNoteABtnPrim = sOcarinaNoteAPrimColors[sOcarinaNoteFlashColorIdx];
                    sOcarinaNoteABtnEnv = sOcarinaNoteAEnvColors[sOcarinaNoteFlashColorIdx];
                    sOcarinaNoteCBtnPrim = sOcarinaNoteCPrimColors[sOcarinaNoteFlashColorIdx];
                    sOcarinaNoteCBtnEnv = sOcarinaNoteCEnvColors[sOcarinaNoteFlashColorIdx];
                    sOcarinaNoteCUpBtnPrim = sOcarinaNoteCUpPrimColors[sOcarinaNoteFlashColorIdx];
                    sOcarinaNoteCDownBtnPrim = sOcarinaNoteCDownPrimColors[sOcarinaNoteFlashColorIdx];
                    sOcarinaNoteCLeftBtnPrim = sOcarinaNoteCLeftPrimColors[sOcarinaNoteFlashColorIdx];
                    sOcarinaNoteCRightBtnPrim = sOcarinaNoteCRightPrimColors[sOcarinaNoteFlashColorIdx];
                    sOcarinaNoteFlashTimer = 3;
                    sOcarinaNoteFlashColorIdx ^= 1;
                }

                msgCtx->stateTimer--;
                if (msgCtx->stateTimer == 0) {
                    AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
                    if (msgCtx->msgMode == MSGMODE_OCARINA_CORRECT_PLAYBACK) {
                        // "Correct Example Performance"
                        osSyncPrintf("正解模範演奏=%x\n", msgCtx->lastPlayedSong);
                        Message_ContinueTextbox(play, 0x86F); // Ocarina
                        msgCtx->msgMode = MSGMODE_SONG_PLAYED;
                        msgCtx->textBoxType = TEXTBOX_TYPE_OCARINA;
                        msgCtx->stateTimer = 1;
                    } else if (msgCtx->msgMode == MSGMODE_SONG_PLAYBACK_SUCCESS) {
                        if (msgCtx->lastPlayedSong >= OCARINA_SONG_SARIAS) {
                            Message_ContinueTextbox(play, 0x86F); // Ocarina
                            msgCtx->msgMode = MSGMODE_SONG_PLAYED;
                            msgCtx->textBoxType = TEXTBOX_TYPE_OCARINA;
                            msgCtx->stateTimer = 1;
                        } else {
                            Message_CloseTextbox(play);
                            play->msgCtx.ocarinaMode = OCARINA_MODE_04;
                        }
                    } else {
                        Message_CloseTextbox(play);
                        play->msgCtx.ocarinaMode = OCARINA_MODE_03;
                    }
                }
                if (gSaveContext.language == LANGUAGE_JPN && !sDisplayNextMessageAsEnglish) {
                    Message_DrawTextJPN(play, &gfx);
                } else {
                    Message_DrawText(play, &gfx);
                }
                break;
            case MSGMODE_OCARINA_FAIL:
            case MSGMODE_SONG_PLAYBACK_FAIL:
                if (gSaveContext.language == LANGUAGE_JPN && !sDisplayNextMessageAsEnglish) {
                    Message_DrawTextJPN(play, &gfx);
                } else {
                    Message_DrawText(play, &gfx);
                }
            case MSGMODE_OCARINA_FAIL_NO_TEXT:
                msgCtx->stateTimer--;
                if (msgCtx->stateTimer == 0) {
                    R_OCARINA_NOTES_YPOS_OFFSET = 1;
                    if (msgCtx->msgMode == MSGMODE_SONG_PLAYBACK_FAIL) {
                        // "kokokokokoko"
                        osSyncPrintf("ここここここ\n");
                        Message_ContinueTextbox(play, 0x88B); // red X background
                        if (!Message_DecodeChecked(play)) {
                            // SOH [Link-Span] R04: manter o encerramento de erro.
                            break;
                        }
                        msgCtx->msgMode = MSGMODE_SONG_PLAYBACK_NOTES_DROP;
                    } else {
                        msgCtx->msgMode = MSGMODE_OCARINA_NOTES_DROP;
                    }
                    // "Cancel"
                    osSyncPrintf("キャンセル\n");
                }
                break;
            case MSGMODE_OCARINA_NOTES_DROP:
            case MSGMODE_SONG_PLAYBACK_NOTES_DROP:
                for (i = 0; i < 5; i++) {
                    R_OCARINA_NOTES_YPOS(i) += R_OCARINA_NOTES_YPOS_OFFSET;
                }
                R_OCARINA_NOTES_YPOS_OFFSET += R_OCARINA_NOTES_YPOS_OFFSET;
                if (R_OCARINA_NOTES_YPOS_OFFSET >= 550) {
                    sOcarinaButtonIndexBuf[0] = OCARINA_BTN_INVALID;
                    sOcarinaNotesAlphaValues[0] = sOcarinaNotesAlphaValues[1] = sOcarinaNotesAlphaValues[2] =
                        sOcarinaNotesAlphaValues[3] = sOcarinaNotesAlphaValues[4] = sOcarinaNotesAlphaValues[5] =
                            sOcarinaNotesAlphaValues[6] = sOcarinaNotesAlphaValues[7] = sOcarinaNotesAlphaValues[8] = 0;
                    if (msgCtx->msgMode == MSGMODE_SONG_PLAYBACK_NOTES_DROP) {
                        msgCtx->msgMode = MSGMODE_OCARINA_AWAIT_INPUT;
                    } else {
                        msgCtx->msgMode = MSGMODE_OCARINA_STARTING;
                    }
                }
                break;
            case MSGMODE_SONG_PLAYED:
                msgCtx->stateTimer--;
                if (msgCtx->stateTimer == 0) {
                    AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
                    osSyncPrintf(VT_FGCOL(GREEN));
                    osSyncPrintf("Na_StopOcarinaMode();\n");
                    osSyncPrintf("Na_StopOcarinaMode();\n");
                    osSyncPrintf("Na_StopOcarinaMode();\n");
                    osSyncPrintf(VT_RST);
                    if (!Message_DecodeChecked(play)) {
                        // SOH [Link-Span] R04: manter o encerramento de erro.
                        break;
                    }
                    msgCtx->msgMode = MSGMODE_SETUP_DISPLAY_SONG_PLAYED;
                    msgCtx->ocarinaStaff = AudioOcarina_GetPlayingStaff();
                    msgCtx->ocarinaStaff->pos = sOcarinaButtonIndexBufPos = 0;
                    Message_ResetOcarinaNoteState();
                    if (msgCtx->lastPlayedSong >= OCARINA_SONG_SARIAS &&
                        msgCtx->lastPlayedSong < OCARINA_SONG_MEMORY_GAME) {
                        Actor_Spawn(&play->actorCtx, play,
                                    sOcarinaEffectActorIds[msgCtx->lastPlayedSong - OCARINA_SONG_SARIAS],
                                    player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z, 0,
                                    0, 0, sOcarinaEffectActorParams[msgCtx->lastPlayedSong - OCARINA_SONG_SARIAS]);
                    } else if (msgCtx->lastPlayedSong >= OCARINA_SONG_MM_FIRST &&
                               msgCtx->lastPlayedSong < OCARINA_SONG_MAX) {
                        // Skijer's NEI: MM + custom songs had NO "song played" visual — the vanilla
                        // table above only covers Saria..Storms. Spawn OCEFF_WIPE (the rising colored
                        // frustum around Link); its draw now loads the frustum assets from mm.o2r
                        // (soh.o2r ships no OcEff assets in this combo build). Every MM/custom song
                        // uses the one actor so the single mm.o2r-backed effect covers them all.
                        Actor_Spawn(&play->actorCtx, play, ACTOR_OCEFF_WIPE, player->actor.world.pos.x,
                                    player->actor.world.pos.y, player->actor.world.pos.z, 0, 0, 0, 0);
                    }
                }
                break;
            case MSGMODE_SETUP_DISPLAY_SONG_PLAYED:
                if (CVarGetInteger(CVAR_ENHANCEMENT("FastOcarinaPlayback"), 0) == 0 ||
                    play->msgCtx.lastPlayedSong == OCARINA_SONG_TIME ||
                    play->msgCtx.lastPlayedSong == OCARINA_SONG_STORMS ||
                    play->msgCtx.lastPlayedSong == OCARINA_SONG_SUNS) {
                    if (gSaveContext.language == LANGUAGE_JPN && !sDisplayNextMessageAsEnglish) {
                        Message_DrawTextJPN(play, &gfx);
                    } else {
                        Message_DrawText(play, &gfx);
                    }
                    // MM resets to DEFAULT and then re-selects the FORM's instrument before
                    // starting the replay (z_message.c MSGMODE_SETUP_DISPLAY_SONG_PLAYED), so
                    // a song played while transformed is replayed in that form's voice. Both
                    // of MM's calls were transcribed as DEFAULT here, which is why the replay
                    // always came back as the plain ocarina.
                    AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_DEFAULT);
                    AudioOcarina_SetInstrument(MmForm_GetOcarinaPlaybackInstrument());
                    AudioOcarina_SetPlaybackSong(msgCtx->lastPlayedSong + 1, 1);
                } else {
                    AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_DEFAULT);
                    AudioOcarina_SetInstrument(MmForm_GetOcarinaPlaybackInstrument());
                }
                // Skijer's NEI: sOcarinaSongFanfares has 12 entries — slots 12+ (scarecrow, memory
                // game, MM songs 14-20, customs 21-23) must not index it. MM songs get their real MM
                // fanfare sequence from mm.o2r via MmBgm (silent no-op if the sequence is missing);
                // customs rely on the ocarina melody replay above (mod window pending, like 2ship).
                if (msgCtx->lastPlayedSong < ARRAY_COUNT(sOcarinaSongFanfares)) {
                    // A form with its own MM voice sings the jingle itself: OoT's sequence
                    // hard-codes the ocarina instrument and is bound to a soundfont that does
                    // not contain the form voices, so it cannot be re-voiced in place.
                    if (!FormJingle_Start(msgCtx->lastPlayedSong)) {
                        Audio_PlayFanfare(sOcarinaSongFanfares[msgCtx->lastPlayedSong]);
                    }
                    Audio_SetSfxBanksMute(0x20);
                } else if ((msgCtx->lastPlayedSong >= OCARINA_SONG_MM_FIRST) &&
                           (msgCtx->lastPlayedSong <= OCARINA_SONG_MM_LAST)) {
                    static const char* sNeiMmSongFanfareNames[7] = {
                        "SonataOfAwakening(Ocarina)_4B", "GoronLullaby(Ocarina)_4C", "NewWaveBossaNova(Ocarina)_5D",
                        "ElegyOfEmptiness(Ocarina)_5E",  "OathToOrder(Ocarina)_5F",  "SongOfSoaring(Ocarina)_47",
                        "SongOfHealing(Ocarina)_48",
                    };
                    extern void MmBgm_PlayFanfare(const char* mmBgmName, u8 melodyInstrument); // sound_translator
                    // MM voices the fanfare's melody with the FORM's instrument too, not just
                    // the note replay above (z_message.c: Audio_PlayFanfareWithPlayerIOPort7
                    // with sOcarinaSongFanfareIoData[CUR_FORM]).
                    MmBgm_PlayFanfare(sNeiMmSongFanfareNames[msgCtx->lastPlayedSong - OCARINA_SONG_MM_FIRST],
                                      MmForm_GetSongFanfareInstrument());
                }
                play->msgCtx.ocarinaMode = OCARINA_MODE_01;
                if (msgCtx->ocarinaAction == OCARINA_ACTION_FREE_PLAY) {
                    msgCtx->ocarinaAction = OCARINA_ACTION_FREE_PLAY_DONE;
                }
                if (msgCtx->ocarinaAction == OCARINA_ACTION_CHECK_NOWARP) {
                    msgCtx->ocarinaAction = OCARINA_ACTION_CHECK_NOWARP_DONE;
                }
                sOcarinaButtonIndexBufPos = 0;
                msgCtx->msgMode = MSGMODE_DISPLAY_SONG_PLAYED;
                break;
            case MSGMODE_SONG_DEMONSTRATION_SELECT_INSTRUMENT:
                msgCtx->stateTimer--;
                if (msgCtx->stateTimer == 0) {
                    // "ocarina_no=%d Song Chosen=%d"
                    osSyncPrintf("ocarina_no=%d  選曲=%d\n", msgCtx->ocarinaAction, 0x16);
                    if (msgCtx->ocarinaAction < OCARINA_ACTION_TEACH_SARIA) {
                        AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_HARP);
                    } else if (msgCtx->ocarinaAction == OCARINA_ACTION_TEACH_EPONA) {
                        AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_MALON);
                    } else if (msgCtx->ocarinaAction == OCARINA_ACTION_TEACH_LULLABY) {
                        AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_WHISTLE);
                    } else if (msgCtx->ocarinaAction == OCARINA_ACTION_TEACH_STORMS) {
                        AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_GRIND_ORGAN);
                    } else {
                        AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_DEFAULT);
                    }
                    // "Example Performance"
                    osSyncPrintf("模範演奏=%x\n", msgCtx->ocarinaAction - OCARINA_ACTION_TEACH_MINUET);
                    AudioOcarina_SetPlaybackSong(msgCtx->ocarinaAction - OCARINA_ACTION_TEACH_MINUET + 1, 2);
                    sOcarinaButtonIndexBufPos = 0;
                    msgCtx->msgMode = MSGMODE_SONG_DEMONSTRATION;
                }
                if (gSaveContext.language == LANGUAGE_JPN && !sDisplayNextMessageAsEnglish) {
                    Message_DrawTextJPN(play, &gfx);
                } else {
                    Message_DrawText(play, &gfx);
                }
                break;
            case MSGMODE_DISPLAY_SONG_PLAYED_TEXT_BEGIN:
                // Skijer's NEI: the "You played [song name]" message is `lastPlayedSong + 0x893`, but
                // those texts only exist for OoT's 12 native songs (0x893..0x89E). MM songs (14-20)
                // and customs (21-23) would resolve to 0x8A1+ which is NOT in the message table →
                // Message_FindMessage leaves msgOffset bogus → memcpy in Message_OpenText crashes
                // (0xC0000005). Show the generic ocarina staff box for them instead (the song name
                // already appears on the quest page; a per-song "You played" text is a later pass,
                // mirroring the 2ship mod-window). See [[project_mm_songs_in_oot_mirror]].
                if (msgCtx->lastPlayedSong >= OCARINA_SONG_MM_FIRST) {
                    Message_ContinueTextbox(play, 0x86F); // Ocarina (generic, valid message)
                } else {
                    Message_ContinueTextbox(play, msgCtx->lastPlayedSong + 0x893); // You played [song name]
                }
                if (!Message_DecodeChecked(play)) {
                    // SOH [Link-Span] R04: manter o encerramento de erro.
                    break;
                }
                msgCtx->msgMode = MSGMODE_DISPLAY_SONG_PLAYED_TEXT;

                if (CVarGetInteger(CVAR_ENHANCEMENT("FastOcarinaPlayback"), 0) == 0 ||
                    play->msgCtx.lastPlayedSong == OCARINA_SONG_TIME ||
                    play->msgCtx.lastPlayedSong == OCARINA_SONG_STORMS ||
                    play->msgCtx.lastPlayedSong == OCARINA_SONG_SUNS) {
                    msgCtx->stateTimer = 20;
                } else {
                    msgCtx->stateTimer = 1;
                }

                if (gSaveContext.language == LANGUAGE_JPN && !sDisplayNextMessageAsEnglish) {
                    Message_DrawTextJPN(play, &gfx);
                } else {
                    Message_DrawText(play, &gfx);
                }
                break;
            case MSGMODE_DISPLAY_SONG_PLAYED_TEXT:
                msgCtx->stateTimer--;
                if (msgCtx->stateTimer == 0) {
                    msgCtx->msgMode = MSGMODE_SONG_PLAYED_ACT_BEGIN;
                }
                if (gSaveContext.language == LANGUAGE_JPN && !sDisplayNextMessageAsEnglish) {
                    Message_DrawTextJPN(play, &gfx);
                } else {
                    Message_DrawText(play, &gfx);
                }
                break;
            case MSGMODE_SONG_PLAYED_ACT_BEGIN:
                AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
                Message_ResetOcarinaNoteState();
                msgCtx->msgMode = MSGMODE_SONG_PLAYED_ACT;
                msgCtx->stateTimer = 2;
                if (gSaveContext.language == LANGUAGE_JPN && !sDisplayNextMessageAsEnglish) {
                    Message_DrawTextJPN(play, &gfx);
                } else {
                    Message_DrawText(play, &gfx);
                }
                break;
            case MSGMODE_SONG_PLAYED_ACT:
                msgCtx->stateTimer--;
                if (msgCtx->stateTimer == 0) {
                    if (msgCtx->lastPlayedSong < OCARINA_SONG_SARIAS &&
                        (msgCtx->ocarinaAction < OCARINA_ACTION_PLAYBACK_MINUET ||
                         msgCtx->ocarinaAction >= OCARINA_ACTION_PLAYBACK_SARIA)) {
                        if (msgCtx->disableWarpSongs || (interfaceCtx->restrictions.warpSongs == 3 && !IS_RANDO)) {
                            Message_StartTextbox(play, 0x88C, NULL); // "You can't warp here!"
                            play->msgCtx.ocarinaMode = OCARINA_MODE_04;
                        } else if ((gSaveContext.eventInf[0] & 0xF) != 1) {
                            Message_StartTextbox(play, msgCtx->lastPlayedSong + 0x88D,
                                                 NULL); // "Warp to [place name]?"
                            play->msgCtx.ocarinaMode = OCARINA_MODE_01;
                        } else {
                            Message_CloseTextbox(play);
                        }
                    } else {
                        Message_CloseTextbox(play);
                        if (msgCtx->lastPlayedSong == OCARINA_SONG_EPONAS) {
                            DREG(53) = 1;
                        }
                        osSyncPrintf(VT_FGCOL(YELLOW));
                        osSyncPrintf("☆☆☆ocarina=%d   message->ocarina_no=%d  ", msgCtx->lastPlayedSong,
                                     msgCtx->ocarinaAction);
                        if (msgCtx->ocarinaAction == OCARINA_ACTION_FREE_PLAY_DONE) {
                            play->msgCtx.ocarinaMode = OCARINA_MODE_01;
                            if (msgCtx->lastPlayedSong == OCARINA_SONG_SCARECROW_SPAWN) {
                                play->msgCtx.ocarinaMode = OCARINA_MODE_0B;
                            }
                        } else if (msgCtx->ocarinaAction >= OCARINA_ACTION_CHECK_MINUET) {
                            osSyncPrintf(VT_FGCOL(YELLOW));
                            osSyncPrintf("Ocarina_PC_Wind=%d(%d) ☆☆☆   ", OCARINA_ACTION_CHECK_MINUET,
                                         msgCtx->ocarinaAction - OCARINA_ACTION_CHECK_MINUET);
                            if (msgCtx->lastPlayedSong + OCARINA_ACTION_CHECK_MINUET == msgCtx->ocarinaAction) {
                                play->msgCtx.ocarinaMode = OCARINA_MODE_03;
                            } else {
                                play->msgCtx.ocarinaMode = msgCtx->lastPlayedSong - 1;
                            }
                        } else {
                            osSyncPrintf(VT_FGCOL(GREEN));
                            osSyncPrintf("Ocarina_C_Wind=%d(%d) ☆☆☆   ", OCARINA_ACTION_PLAYBACK_MINUET,
                                         msgCtx->ocarinaAction - OCARINA_ACTION_PLAYBACK_MINUET);
                            if (msgCtx->lastPlayedSong + OCARINA_ACTION_PLAYBACK_MINUET == msgCtx->ocarinaAction) {
                                play->msgCtx.ocarinaMode = OCARINA_MODE_03;
                            } else {
                                play->msgCtx.ocarinaMode = OCARINA_MODE_04;
                            }
                        }
                        osSyncPrintf(VT_RST);
                        osSyncPrintf("→  OCARINA_MODE=%d\n", play->msgCtx.ocarinaMode);
                    }
                    GameInteractor_ExecuteOnOcarinaSongAction();
                }
                break;
            case MSGMODE_DISPLAY_SONG_PLAYED:
            case MSGMODE_SONG_DEMONSTRATION:
                msgCtx->ocarinaStaff = AudioOcarina_GetPlaybackStaff();
                if (msgCtx->ocarinaStaff->state == 0) {
                    if (msgCtx->msgMode == MSGMODE_DISPLAY_SONG_PLAYED) {
                        msgCtx->msgMode = MSGMODE_DISPLAY_SONG_PLAYED_TEXT_BEGIN;
                    } else {
                        msgCtx->msgMode = MSGMODE_SONG_DEMONSTRATION_DONE;
                    }
                    osSyncPrintf("onpu_buff[%d]=%x\n", msgCtx->ocarinaStaff->pos,
                                 sOcarinaButtonIndexBuf[msgCtx->ocarinaStaff->pos]);
                } else {
                    if (sOcarinaButtonIndexBufPos != 0 && msgCtx->ocarinaStaff->pos == 1) {
                        sOcarinaButtonIndexBufPos = 0;
                    }
                    if (msgCtx->ocarinaStaff->pos && sOcarinaButtonIndexBufPos == msgCtx->ocarinaStaff->pos - 1) {
                        msgCtx->lastOcaNoteIdx = sOcarinaButtonIndexBuf[msgCtx->ocarinaStaff->pos - 1] =
                            msgCtx->ocarinaStaff->buttonIndex;
                        sOcarinaButtonIndexBuf[msgCtx->ocarinaStaff->pos] = OCARINA_BTN_INVALID;
                        sOcarinaButtonIndexBufPos++;
                    }
                }
            case MSGMODE_SONG_DEMONSTRATION_DONE:
                if (gSaveContext.language == LANGUAGE_JPN && !sDisplayNextMessageAsEnglish) {
                    Message_DrawTextJPN(play, &gfx);
                } else {
                    Message_DrawText(play, &gfx);
                }
                break;
            case MSGMODE_SONG_PLAYBACK:
                msgCtx->ocarinaStaff = AudioOcarina_GetPlayingStaff();
                if (msgCtx->ocarinaStaff->pos && sOcarinaButtonIndexBufPos == msgCtx->ocarinaStaff->pos - 1) {
                    sOcarinaButtonIndexBuf[msgCtx->ocarinaStaff->pos - 1] = msgCtx->ocarinaStaff->buttonIndex;
                    sOcarinaButtonIndexBuf[msgCtx->ocarinaStaff->pos] = OCARINA_BTN_INVALID;
                    sOcarinaButtonIndexBufPos++;
                }
                if (msgCtx->ocarinaStaff->state < OCARINA_SONG_MEMORY_GAME) {
                    osSyncPrintf("M_OCARINA20 : ocarina_no=%x    status=%x\n", msgCtx->ocarinaAction,
                                 msgCtx->ocarinaStaff->state);
                    msgCtx->lastPlayedSong = msgCtx->ocarinaStaff->state;
                    msgCtx->msgMode = MSGMODE_SONG_PLAYBACK_SUCCESS;

                    u8 songItemId = ITEM_SONG_MINUET + gOcarinaSongItemMap[msgCtx->ocarinaStaff->state];

                    if (GameInteractor_Should(VB_GIVE_ITEM_SONG, true, songItemId)) {
                        Item_Give(play, songItemId);
                    }

                    osSyncPrintf(VT_FGCOL(YELLOW));
                    // "z_message.c Song Acquired"
                    osSyncPrintf("z_message.c 取得メロディ＝%d\n", ITEM_SONG_MINUET + msgCtx->ocarinaStaff->state);
                    osSyncPrintf(VT_RST);
                    msgCtx->stateTimer = 20;
                    Audio_PlaySfxGeneral(NA_SE_SY_TRE_BOX_APPEAR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                } else if (msgCtx->ocarinaStaff->state == 0xFF) {
                    Audio_PlaySfxGeneral(NA_SE_SY_OCARINA_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    msgCtx->stateTimer = 10;
                    msgCtx->msgMode = MSGMODE_SONG_PLAYBACK_FAIL;
                }
                if (gSaveContext.language == LANGUAGE_JPN && !sDisplayNextMessageAsEnglish) {
                    Message_DrawTextJPN(play, &gfx);
                } else {
                    Message_DrawText(play, &gfx);
                }
                break;
            case MSGMODE_OCARINA_AWAIT_INPUT:
                if (gSaveContext.language == LANGUAGE_JPN && !sDisplayNextMessageAsEnglish) {
                    Message_DrawTextJPN(play, &gfx);
                } else {
                    Message_DrawText(play, &gfx);
                }
                if (Message_ShouldAdvance(play)) {
                    func_8010BD58(play, msgCtx->ocarinaAction);
                }
                break;
            case MSGMODE_SCARECROW_LONG_RECORDING_START:
                // "Scarecrow Recording Initialization"
                osSyncPrintf("案山子録音 初期化\n");
                AudioOcarina_SetRecordingState(OCARINA_RECORD_SCARECROW_LONG);
                AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_DEFAULT);
                msgCtx->ocarinaStaff = AudioOcarina_GetRecordingStaff();
                msgCtx->ocarinaStaff->pos = sOcarinaButtonIndexBufPos = 0;
                sOcarinaButtonIndexBufLen = 0;
                Message_ResetOcarinaNoteState();
                msgCtx->msgMode = MSGMODE_SCARECROW_LONG_RECORDING_ONGOING;
                if (gSaveContext.language == LANGUAGE_JPN && !sDisplayNextMessageAsEnglish) {
                    Message_DrawTextJPN(play, &gfx);
                } else {
                    Message_DrawText(play, &gfx);
                }
                break;
            case MSGMODE_SCARECROW_LONG_RECORDING_ONGOING:
                msgCtx->ocarinaStaff = AudioOcarina_GetRecordingStaff();
                osSyncPrintf("\nonpu_pt=%d, locate=%d", sOcarinaButtonIndexBufPos, msgCtx->ocarinaStaff->pos);
                if (msgCtx->ocarinaStaff->pos && sOcarinaButtonIndexBufPos == msgCtx->ocarinaStaff->pos - 1) {
                    if (sOcarinaButtonIndexBufLen >= 8) {
                        for (noteBufPos = sOcarinaButtonIndexBufLen - 8, i = 0; i < 8; i++, noteBufPos++) {
                            sOcarinaButtonIndexBuf[noteBufPos] = sOcarinaButtonIndexBuf[noteBufPos + 1];
                        }
                        sOcarinaButtonIndexBufLen--;
                    }
                    // "Button Entered"
                    osSyncPrintf("    入力ボタン【%d】=%d", sOcarinaButtonIndexBufLen,
                                 msgCtx->ocarinaStaff->buttonIndex);
                    msgCtx->lastOcaNoteIdx = sOcarinaButtonIndexBuf[sOcarinaButtonIndexBufLen] =
                        msgCtx->ocarinaStaff->buttonIndex;
                    sOcarinaButtonIndexBufLen++;
                    sOcarinaButtonIndexBuf[sOcarinaButtonIndexBufLen] = OCARINA_BTN_INVALID;
                    sOcarinaButtonIndexBufPos++;
                    if (msgCtx->ocarinaStaff->pos == 8) {
                        sOcarinaButtonIndexBufPos = 0;
                    }
                }
                if (msgCtx->ocarinaStaff->state == 0 || isB_Held) {
                    if (sOcarinaButtonIndexBufLen != 0) {
                        // "Recording complete！！！！！！！！！"
                        osSyncPrintf("録音終了！！！！！！！！！  message->info->status=%d \n",
                                     msgCtx->ocarinaStaff->state);
                        gSaveContext.scarecrowLongSongSet = true;
                    }
                    Audio_PlaySfxGeneral(NA_SE_SY_OCARINA_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    osSyncPrintf("aaaaaaaaaaaaaa\n");
                    AudioOcarina_SetRecordingState(OCARINA_RECORD_OFF);
                    msgCtx->stateTimer = 10;
                    play->msgCtx.ocarinaMode = OCARINA_MODE_04;
                    Message_CloseTextbox(play);
                    // "Recording complete！！！！！！！！！Recording Complete"
                    osSyncPrintf("録音終了！！！！！！！！！録音終了\n");
                    osSyncPrintf(VT_FGCOL(YELLOW));
                    osSyncPrintf("\n====================================================================\n");
                    memcpy(gSaveContext.scarecrowLongSong, gScarecrowLongSongPtr,
                           sizeof(gSaveContext.scarecrowLongSong));
                    for (i = 0; i < ARRAY_COUNT(gSaveContext.scarecrowLongSong); i++) {
                        osSyncPrintf("%d, ", gSaveContext.scarecrowLongSong[i]);
                    }
                    osSyncPrintf(VT_RST);
                    osSyncPrintf("\n====================================================================\n");
                }
                if (gSaveContext.language == LANGUAGE_JPN && !sDisplayNextMessageAsEnglish) {
                    Message_DrawTextJPN(play, &gfx);
                } else {
                    Message_DrawText(play, &gfx);
                }
                break;
            case MSGMODE_SCARECROW_LONG_PLAYBACK:
            case MSGMODE_SCARECROW_PLAYBACK:
                msgCtx->ocarinaStaff = AudioOcarina_GetPlaybackStaff();
                if (msgCtx->ocarinaStaff->pos && sOcarinaButtonIndexBufPos == msgCtx->ocarinaStaff->pos - 1) {
                    if (sOcarinaButtonIndexBufLen >= 8) {
                        for (noteBufPos = sOcarinaButtonIndexBufLen - 8, i = 0; i < 8; i++, noteBufPos++) {
                            sOcarinaButtonIndexBuf[noteBufPos] = sOcarinaButtonIndexBuf[noteBufPos + 1];
                        }
                        sOcarinaButtonIndexBufLen--;
                    }
                    sOcarinaButtonIndexBuf[sOcarinaButtonIndexBufLen] = msgCtx->ocarinaStaff->buttonIndex;
                    sOcarinaButtonIndexBufLen++;
                    sOcarinaButtonIndexBuf[sOcarinaButtonIndexBufLen] = OCARINA_BTN_INVALID;
                    sOcarinaButtonIndexBufPos++;
                    if (msgCtx->ocarinaStaff->pos == 8) {
                        sOcarinaButtonIndexBufLen = sOcarinaButtonIndexBufPos = 0;
                    }
                }
                osSyncPrintf("status=%d (%d)\n", msgCtx->ocarinaStaff->state, 0);
                if (msgCtx->stateTimer == 0) {
                    if (msgCtx->ocarinaStaff->state == 0) {
                        osSyncPrintf("bbbbbbbbbbb\n");
                        AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
                        play->msgCtx.ocarinaMode = OCARINA_MODE_0F;
                        Message_CloseTextbox(play);
                    }
                } else {
                    msgCtx->stateTimer--;
                }
                break;
            case MSGMODE_SCARECROW_RECORDING_START:
                AudioOcarina_SetRecordingState(OCARINA_RECORD_SCARECROW_SPAWN);
                AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_DEFAULT);
                msgCtx->msgMode = MSGMODE_SCARECROW_RECORDING_ONGOING;
                if (gSaveContext.language == LANGUAGE_JPN && !sDisplayNextMessageAsEnglish) {
                    Message_DrawTextJPN(play, &gfx);
                } else {
                    Message_DrawText(play, &gfx);
                }
                break;
            case MSGMODE_SCARECROW_RECORDING_ONGOING:
                msgCtx->ocarinaStaff = AudioOcarina_GetRecordingStaff();
                if (msgCtx->ocarinaStaff->pos && sOcarinaButtonIndexBufPos == msgCtx->ocarinaStaff->pos - 1) {
                    msgCtx->lastOcaNoteIdx = sOcarinaButtonIndexBuf[sOcarinaButtonIndexBufPos] =
                        msgCtx->ocarinaStaff->buttonIndex;
                    sOcarinaButtonIndexBufPos++;
                    sOcarinaButtonIndexBuf[sOcarinaButtonIndexBufPos] = OCARINA_BTN_INVALID;
                }
                if (msgCtx->ocarinaStaff->state == 0) {
                    // "8 Note Recording ＯＫ！"
                    osSyncPrintf("８音録音ＯＫ！\n");
                    msgCtx->stateTimer = 20;
                    gSaveContext.scarecrowSpawnSongSet = true;
                    msgCtx->msgMode = MSGMODE_SCARECROW_RECORDING_DONE;
                    Audio_PlaySfxGeneral(NA_SE_SY_TRE_BOX_APPEAR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    osSyncPrintf(VT_FGCOL(YELLOW));
                    osSyncPrintf("\n====================================================================\n");
                    memcpy(gSaveContext.scarecrowSpawnSong, gScarecrowSpawnSongPtr,
                           sizeof(gSaveContext.scarecrowSpawnSong));
                    for (i = 0; i < ARRAY_COUNT(gSaveContext.scarecrowSpawnSong); i++) {
                        osSyncPrintf("%d, ", gSaveContext.scarecrowSpawnSong[i]);
                    }
                    osSyncPrintf(VT_RST);
                    osSyncPrintf("\n====================================================================\n");
                } else if (msgCtx->ocarinaStaff->state == OCARINA_RECORD_REJECTED || isB_Held) {
                    // "Played an existing song！！！"
                    osSyncPrintf("すでに存在する曲吹いた！！！ \n");
                    AudioOcarina_SetRecordingState(OCARINA_RECORD_OFF);
                    Audio_PlaySfxGeneral(NA_SE_SY_OCARINA_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    Message_CloseTextbox(play);
                    msgCtx->msgMode = MSGMODE_SCARECROW_RECORDING_FAILED;
                }
                if (gSaveContext.language == LANGUAGE_JPN && !sDisplayNextMessageAsEnglish) {
                    Message_DrawTextJPN(play, &gfx);
                } else {
                    Message_DrawText(play, &gfx);
                }
                break;
            case MSGMODE_SCARECROW_RECORDING_FAILED:
                osSyncPrintf("cccccccccccc\n");
                AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
                Message_StartTextbox(play, 0x40AD, NULL); // Bonooru doesn't remember your song
                play->msgCtx.ocarinaMode = OCARINA_MODE_04;
                break;
            case MSGMODE_MEMORY_GAME_START:
                AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_DEFAULT);
                AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_FLUTE);
                AudioOcarina_MemoryGameInit(gSaveContext.ocarinaGameRoundNum);
                msgCtx->ocarinaStaff = AudioOcarina_GetPlaybackStaff();
                msgCtx->ocarinaStaff->pos = sOcarinaButtonIndexBufPos = 0;
                Message_ResetOcarinaNoteState();
                AudioOcarina_SetPlaybackSong(OCARINA_SONG_MEMORY_GAME + 1, 1);
                msgCtx->msgMode = MSGMODE_MEMORY_GAME_LEFT_SKULLKID_PLAYING;
                msgCtx->stateTimer = 2;
                break;
            case MSGMODE_MEMORY_GAME_LEFT_SKULLKID_PLAYING:
            case MSGMODE_MEMORY_GAME_RIGHT_SKULLKID_PLAYING:
                Audio_PlaySfxGeneral(NA_SE_SY_METRONOME_LV - SFX_FLAG, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                     &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                msgCtx->ocarinaStaff = AudioOcarina_GetPlaybackStaff();
                if (msgCtx->ocarinaStaff->pos && sOcarinaButtonIndexBufPos == msgCtx->ocarinaStaff->pos - 1) {
                    sOcarinaButtonIndexBuf[msgCtx->ocarinaStaff->pos - 1] = msgCtx->ocarinaStaff->buttonIndex;
                    sOcarinaButtonIndexBuf[msgCtx->ocarinaStaff->pos] = OCARINA_BTN_INVALID;
                    sOcarinaButtonIndexBufPos++;
                }
                if (msgCtx->stateTimer == 0) {
                    if (msgCtx->ocarinaStaff->state == 0) {
                        if (msgCtx->msgMode == MSGMODE_MEMORY_GAME_LEFT_SKULLKID_PLAYING) {
                            Audio_PlaySfxGeneral(NA_SE_SY_METRONOME, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                        } else {
                            Audio_PlaySfxGeneral(NA_SE_SY_METRONOME_2, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                        }
                        msgCtx->msgMode++;
                    }
                } else {
                    msgCtx->stateTimer--;
                }
                break;
            case MSGMODE_MEMORY_GAME_LEFT_SKULLKID_WAIT:
            case MSGMODE_MEMORY_GAME_RIGHT_SKULLKID_WAIT:
                msgCtx->ocarinaStaff = AudioOcarina_GetPlaybackStaff();
                if (msgCtx->ocarinaStaff->pos && sOcarinaButtonIndexBufPos == msgCtx->ocarinaStaff->pos - 1) {
                    sOcarinaButtonIndexBuf[msgCtx->ocarinaStaff->pos - 1] = msgCtx->ocarinaStaff->buttonIndex;
                    sOcarinaButtonIndexBuf[msgCtx->ocarinaStaff->pos] = OCARINA_BTN_INVALID;
                    sOcarinaButtonIndexBufPos++;
                }
                break;
            case MSGMODE_MEMORY_GAME_PLAYER_PLAYING:
                Audio_PlaySfxGeneral(NA_SE_SY_METRONOME_LV - SFX_FLAG, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                     &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                msgCtx->ocarinaStaff = AudioOcarina_GetPlayingStaff();
                if (msgCtx->ocarinaStaff->pos && sOcarinaButtonIndexBufPos == msgCtx->ocarinaStaff->pos - 1) {
                    sOcarinaButtonIndexBuf[msgCtx->ocarinaStaff->pos - 1] = msgCtx->ocarinaStaff->buttonIndex;
                    sOcarinaButtonIndexBuf[msgCtx->ocarinaStaff->pos] = OCARINA_BTN_INVALID;
                    sOcarinaButtonIndexBufPos++;
                }
                if (msgCtx->ocarinaStaff->state == 0xFF) {
                    // "Musical round failed！！！！！！！！！"
                    osSyncPrintf("輪唱失敗！！！！！！！！！\n");
                    AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
                    Audio_PlaySfxGeneral(NA_SE_SY_OCARINA_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    msgCtx->stateTimer = 10;
                    play->msgCtx.ocarinaMode = OCARINA_MODE_03;
                } else if (msgCtx->ocarinaStaff->state == 0xD) {
                    // "Musical round succeeded！！！！！！！！！"
                    osSyncPrintf("輪唱成功！！！！！！！！！\n");
                    Audio_PlaySfxGeneral(NA_SE_SY_GET_ITEM, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    msgCtx->msgMode = MSGMODE_MEMORY_GAME_ROUND_SUCCESS;
                    msgCtx->stateTimer = 30;
                }
                if (gSaveContext.language == LANGUAGE_JPN && !sDisplayNextMessageAsEnglish) {
                    Message_DrawTextJPN(play, &gfx);
                } else {
                    Message_DrawText(play, &gfx);
                }
                break;
            case MSGMODE_MEMORY_GAME_ROUND_SUCCESS:
                msgCtx->ocarinaStaff = AudioOcarina_GetPlayingStaff();
                if (msgCtx->ocarinaStaff->pos && sOcarinaButtonIndexBufPos == msgCtx->ocarinaStaff->pos - 1) {
                    sOcarinaButtonIndexBuf[msgCtx->ocarinaStaff->pos - 1] = msgCtx->ocarinaStaff->buttonIndex;
                    sOcarinaButtonIndexBuf[msgCtx->ocarinaStaff->pos] = OCARINA_BTN_INVALID;
                    sOcarinaButtonIndexBufPos++;
                }
                msgCtx->stateTimer--;
                if (msgCtx->stateTimer == 0) {
                    if (AudioOcarina_MemoryGameNextNote() != 1) {
                        Audio_PlaySfxGeneral(NA_SE_SY_METRONOME, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                        msgCtx->ocarinaStaff = AudioOcarina_GetPlayingStaff();
                        msgCtx->ocarinaStaff->pos = sOcarinaButtonIndexBufPos = 0;
                        Message_ResetOcarinaNoteState();
                        msgCtx->msgMode = MSGMODE_MEMORY_GAME_START_NEXT_ROUND;
                    } else {
                        play->msgCtx.ocarinaMode = OCARINA_MODE_0F;
                    }
                }
                if (gSaveContext.language == LANGUAGE_JPN && !sDisplayNextMessageAsEnglish) {
                    Message_DrawTextJPN(play, &gfx);
                } else {
                    Message_DrawText(play, &gfx);
                }
                break;
            case MSGMODE_MEMORY_GAME_START_NEXT_ROUND:
                if (!Audio_IsSfxPlaying(NA_SE_SY_METRONOME)) {
                    msgCtx->ocarinaStaff = AudioOcarina_GetPlaybackStaff();
                    msgCtx->ocarinaStaff->pos = sOcarinaButtonIndexBufPos = 0;
                    Message_ResetOcarinaNoteState();
                    AudioOcarina_SetPlaybackSong(OCARINA_SONG_MEMORY_GAME + 1, 1);
                }
                break;
            case MSGMODE_FROGS_START:
                AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_DEFAULT);
                msgCtx->ocarinaStaff = AudioOcarina_GetPlayingStaff();
                msgCtx->ocarinaStaff->pos = sOcarinaButtonIndexBufPos = 0;
                play->msgCtx.ocarinaMode = OCARINA_MODE_01;
                Message_ResetOcarinaNoteState();
                AudioOcarina_Start(sOcarinaSongBitFlags + 0xC000);
                msgCtx->msgMode = MSGMODE_FROGS_PLAYING;
                break;
            case MSGMODE_FROGS_PLAYING:
                msgCtx->ocarinaStaff = AudioOcarina_GetPlayingStaff();
                if (msgCtx->ocarinaStaff->pos && sOcarinaButtonIndexBufPos == msgCtx->ocarinaStaff->pos - 1) {
                    msgCtx->lastOcaNoteIdx = msgCtx->ocarinaStaff->buttonIndex;
                    msgCtx->ocarinaStaff->pos = sOcarinaButtonIndexBufPos = 0;
                    Message_ResetOcarinaNoteState();
                    msgCtx->msgMode = MSGMODE_FROGS_WAITING;
                }
            case MSGMODE_FROGS_WAITING:
                break;
            case MSGMODE_TEXT_DONE:
                if (gSaveContext.language == LANGUAGE_JPN && !sTextIsCredits && !sDisplayNextMessageAsEnglish) {
                    Message_DrawTextJPN(play, &gfx);
                } else {
                    Message_DrawText(play, &gfx);
                }

                switch (msgCtx->textboxEndType) {
                    case TEXTBOX_ENDTYPE_2_CHOICE:
                        Message_HandleChoiceSelection(play, 1);
                        Message_DrawTextboxIcon(play, &gfx, msgCtx->textPosX, msgCtx->textPosY);
                        break;
                    case TEXTBOX_ENDTYPE_3_CHOICE:
                        Message_HandleChoiceSelection(play, 2);
                        Message_DrawTextboxIcon(play, &gfx, msgCtx->textPosX, msgCtx->textPosY);
                        break;
                    case TEXTBOX_ENDTYPE_PERSISTENT:
                        if (msgCtx->textId >= 0x6D && msgCtx->textId < 0x73) {
                            msgCtx->stateTimer++;
                            if (msgCtx->stateTimer >= 31) {
                                msgCtx->stateTimer = 2;
                                msgCtx->msgMode = MSGMODE_TEXT_CLOSING;
                            }
                        }
                        break;
                    case TEXTBOX_ENDTYPE_EVENT:
                    default:
                        Message_DrawTextboxIcon(play, &gfx, R_TEXTBOX_END_XPOS, R_TEXTBOX_END_YPOS);
                    case TEXTBOX_ENDTYPE_FADING:
                        break;
                }
                break;
            case MSGMODE_TEXT_CLOSING:
                if (sDisplayNextMessageAsEnglish) {
                    sDisplayNextMessageAsEnglish = false;
                }
                /* fallthrough */
            case MSGMODE_PAUSED:
                break;
            case MSGMODE_UNK_20:
            default:
                msgCtx->msgMode = MSGMODE_TEXT_DISPLAYING;
                break;
        }

        if (msgCtx->msgMode >= MSGMODE_OCARINA_PLAYING && msgCtx->msgMode < MSGMODE_TEXT_AWAIT_NEXT &&
            msgCtx->ocarinaAction != OCARINA_ACTION_FREE_PLAY && msgCtx->ocarinaAction != OCARINA_ACTION_CHECK_NOWARP) {
            Gfx_SetupDL_39Ptr(&gfx);

            gDPSetCombineLERP(gfx++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, PRIMITIVE,
                              ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);

            // Which song's full fingering staff (greyed target notes) to show. Vanilla only shows it
            // while DEMONSTRATING a song (MSGMODE_SONG_PLAYBACK, indexed by the teach/playback action).
            // Skijer's NEI: MM + custom songs are played via free-play (pause-play), whose success runs
            // through MSGMODE_DISPLAY_SONG_PLAYED — which otherwise reveals only the one-by-one replay
            // notes. Draw the target staff there too (keyed by lastPlayedSong) so the song's notes are
            // clearly visible during the replay and it reads as a real song.
            g = -1;
            if (msgCtx->msgMode == MSGMODE_SONG_PLAYBACK) {
                g = msgCtx->ocarinaAction - OCARINA_ACTION_PLAYBACK_MINUET;
            } else if ((msgCtx->msgMode == MSGMODE_DISPLAY_SONG_PLAYED) &&
                       (msgCtx->lastPlayedSong >= OCARINA_SONG_MM_FIRST) &&
                       (msgCtx->lastPlayedSong < OCARINA_SONG_MAX)) {
                g = msgCtx->lastPlayedSong;
            }
            if (g >= 0) {
                r = gOcarinaSongButtons[g].numButtons;
                for (notePosX = R_OCARINA_NOTES_XPOS, i = 0; i < r; i++, notePosX += R_OCARINA_NOTES_XPOS_OFFSET) {
                    gDPPipeSync(gfx++);
                    gDPSetPrimColor(gfx++, 0, 0, 150, 150, 150, 150);
                    gDPSetEnvColor(gfx++, 10, 10, 10, 0);

                    gDPLoadTextureBlock(gfx++, sOcarinaNoteTextures[gOcarinaSongButtons[g].buttonsIndex[i]],
                                        G_IM_FMT_IA, G_IM_SIZ_8b, 16, 16, 0, G_TX_NOMIRROR | G_TX_WRAP,
                                        G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);

                    gSPTextureRectangle(
                        gfx++, notePosX << 2, R_OCARINA_NOTES_YPOS(gOcarinaSongButtons[g].buttonsIndex[i]) << 2,
                        (notePosX + 16) << 2, (R_OCARINA_NOTES_YPOS(gOcarinaSongButtons[g].buttonsIndex[i]) + 16) << 2,
                        G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);
                }
            }

            if (msgCtx->msgMode != MSGMODE_SCARECROW_LONG_RECORDING_START &&
                msgCtx->msgMode != MSGMODE_MEMORY_GAME_START) {
                for (notePosX = R_OCARINA_NOTES_XPOS, i = 0; i < 8; i++, notePosX += R_OCARINA_NOTES_XPOS_OFFSET) {
                    if (sOcarinaButtonIndexBuf[i] == OCARINA_BTN_INVALID) {
                        break;
                    }

                    if (sOcarinaNotesAlphaValues[i] != 255) {
                        sOcarinaNotesAlphaValues[i] += VREG(50);
                        if (sOcarinaNotesAlphaValues[i] >= 255) {
                            sOcarinaNotesAlphaValues[i] = 255;
                        }
                    }

                    gDPPipeSync(gfx++);

                    // Since I don't know what exactly these Env vars are used for, I elected keep their usage
                    // consistent with the note played, rather than having AEnv be used for whatever note A happens to
                    // play at the moment and CEnv for everything else, even with custom controls enabled.
                    if (sOcarinaButtonIndexBuf[i] == OCARINA_BTN_A) {
                        gDPSetPrimColor(gfx++, 0, 0, sOcarinaNoteABtnPrim.r, sOcarinaNoteABtnPrim.g,
                                        sOcarinaNoteABtnPrim.b, sOcarinaNotesAlphaValues[i]);
                        gDPSetEnvColor(gfx++, sOcarinaNoteABtnEnv.r, sOcarinaNoteABtnEnv.g, sOcarinaNoteABtnEnv.b, 0);
                    } else {
                        if (sOcarinaButtonIndexBuf[i] == OCARINA_BTN_C_UP) {
                            gDPSetPrimColor(gfx++, 0, 0, sOcarinaNoteCUpBtnPrim.r, sOcarinaNoteCUpBtnPrim.g,
                                            sOcarinaNoteCUpBtnPrim.b, sOcarinaNotesAlphaValues[i]);
                        } else if (sOcarinaButtonIndexBuf[i] == OCARINA_BTN_C_LEFT) {
                            gDPSetPrimColor(gfx++, 0, 0, sOcarinaNoteCLeftBtnPrim.r, sOcarinaNoteCLeftBtnPrim.g,
                                            sOcarinaNoteCLeftBtnPrim.b, sOcarinaNotesAlphaValues[i]);
                        } else if (sOcarinaButtonIndexBuf[i] == OCARINA_BTN_C_RIGHT) {
                            gDPSetPrimColor(gfx++, 0, 0, sOcarinaNoteCRightBtnPrim.r, sOcarinaNoteCRightBtnPrim.g,
                                            sOcarinaNoteCRightBtnPrim.b, sOcarinaNotesAlphaValues[i]);
                        } else if (sOcarinaButtonIndexBuf[i] == OCARINA_BTN_C_DOWN) {
                            gDPSetPrimColor(gfx++, 0, 0, sOcarinaNoteCDownBtnPrim.r, sOcarinaNoteCDownBtnPrim.g,
                                            sOcarinaNoteCDownBtnPrim.b, sOcarinaNotesAlphaValues[i]);
                        }
                        gDPSetEnvColor(gfx++, sOcarinaNoteCBtnEnv.r, sOcarinaNoteCBtnEnv.g, sOcarinaNoteCBtnEnv.b, 0);
                    }

                    gDPLoadTextureBlock(gfx++, sOcarinaNoteTextures[sOcarinaButtonIndexBuf[i]], G_IM_FMT_IA,
                                        G_IM_SIZ_8b, 16, 16, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP,
                                        G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);

                    gSPTextureRectangle(gfx++, notePosX << 2, R_OCARINA_NOTES_YPOS(sOcarinaButtonIndexBuf[i]) << 2,
                                        (notePosX + 16) << 2,
                                        (R_OCARINA_NOTES_YPOS(sOcarinaButtonIndexBuf[i]) + 16) << 2, G_TX_RENDERTILE, 0,
                                        0, 1 << 10, 1 << 10);
                }
            }
        }
    }
    *p = gfx;
}
