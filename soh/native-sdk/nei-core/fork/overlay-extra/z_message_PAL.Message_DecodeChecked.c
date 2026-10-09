/* overlay-extra 6b51213f1019 da39a3ee5e6b da39a3ee5e6b */
static bool Message_DecodeChecked(PlayState* play) {
    u8 temp_s2;
    u8 phi_s1;
    u16 phi_s0_3;
    s32 loadChar;
    s32 charTexIdx = 0;
    s16 playerNameLen;
    s16 decodedBufPos = 0;
    s16 numLines = 0;
    s16 i;
    s16 digits[4];
    f32 timeInSeconds;
    MessageContext* msgCtx = &play->msgCtx;
    Font* font = &play->msgCtx.font;

    s16 decodedLimit = ARRAY_COUNT(msgCtx->msgBufDecoded);
    u16 rawTokenStart = msgCtx->msgBufPos;

    // #region SOH [NTSC] - allow switching languages mid text
    sTextBoxNum++;
    // #endregion

    if ((msgCtx->msgMode >= MSGMODE_OCARINA_STARTING && msgCtx->msgMode <= MSGMODE_OCARINA_AWAIT_INPUT) ||
        msgCtx->textBoxType == TEXTBOX_TYPE_OCARINA) {
        // TODO: Figure out what specific textures to invalidate to prevent the ocarina textboxes from flashing
        gSPInvalidateTexCache(play->state.gfxCtx->polyOpa.p++, NULL);
    } else {
        for (u32 i = 0; i < FONT_CHAR_TEX_SIZE * 120; i += FONT_CHAR_TEX_SIZE) {
            if (&font->charTexBuf[i] != NULL) {
                gSPInvalidateTexCache(play->state.gfxCtx->polyOpa.p++, &font->charTexBuf[i]);
            }
        }
    }

    play->msgCtx.textDelayTimer = 0;
    play->msgCtx.textUnskippable = play->msgCtx.textDelay = play->msgCtx.textDelayTimer = 0;
    sTextFade = false;

    // #region SOH [NTSC] - Originally this is all in one function, but for ease of reading, the JP decoding will be
    // separated out
    if (gSaveContext.language == LANGUAGE_JPN && !sTextIsCredits && !sDisplayNextMessageAsEnglish) {
        return Message_DecodeJPN(play);
    }
    // #endregion

    while (true) {
        rawTokenStart = msgCtx->msgBufPos;
        if (LinkSpan_EngineExtended() && !Message_HasRawUnits(font, msgCtx->msgBufPos, 1, false)) {
            goto decodeOverflow;
        }
        if (LinkSpan_EngineExtended() &&
            !Message_HasRawUnits(font, msgCtx->msgBufPos,
                                 Message_RawControlUnits((u8)font->msgBuf[msgCtx->msgBufPos], false), false)) {
            goto decodeOverflow;
        }
        MESSAGE_DECODE_REQUIRE_INDEX(decodedBufPos);
        phi_s1 = temp_s2 = msgCtx->msgBufDecoded[decodedBufPos] = font->msgBuf[msgCtx->msgBufPos];

        // Don't require input for credits textboxes in randomizer
        if (CVarGetInteger(CVAR_ENHANCEMENT("NoInputForCredits"), 0) &&
            (msgCtx->textId == 0x706F || msgCtx->textId == 0x7091 || msgCtx->textId == 0x7092 ||
             msgCtx->textId == 0x7093 || msgCtx->textId == 0x7094 || msgCtx->textId == 0x7095)) {
            if (temp_s2 == MESSAGE_BOX_BREAK) {
                MESSAGE_DECODE_REQUIRE_INDEX(decodedBufPos);
                phi_s1 = temp_s2 = msgCtx->msgBufDecoded[decodedBufPos] = font->msgBuf[msgCtx->msgBufPos] =
                    MESSAGE_BOX_BREAK_DELAYED;
            } else if (temp_s2 == MESSAGE_END && !LinkSpan_EngineExtended()) {
                // OOT-VANILLA-001: cauda do upstream
                phi_s1 = temp_s2 = msgCtx->msgBufDecoded[decodedBufPos] = font->msgBuf[msgCtx->msgBufPos] =
                    MESSAGE_FADE2;
                phi_s1 = temp_s2 = msgCtx->msgBufDecoded[++decodedBufPos] = font->msgBuf[++msgCtx->msgBufPos] =
                    MESSAGE_END;
            } else if (temp_s2 == MESSAGE_END) {
                // SOH [Link-Span] R04: manter o byte alto legado; cauda deterministica.
                MESSAGE_DECODE_REQUIRE_INDEX(decodedBufPos + 3);
                msgCtx->msgBufDecoded[decodedBufPos] = MESSAGE_FADE2;
                msgCtx->msgBufDecoded[++decodedBufPos] = MESSAGE_END;
                msgCtx->msgBufDecoded[++decodedBufPos] = 0;
                msgCtx->msgBufDecoded[++decodedBufPos] = MESSAGE_END;
                sTextFade = true;
            }
        }

        if (temp_s2 == MESSAGE_BOX_BREAK || temp_s2 == MESSAGE_TEXTID || temp_s2 == MESSAGE_BOX_BREAK_DELAYED ||
            temp_s2 == MESSAGE_EVENT || temp_s2 == MESSAGE_END) {
            // Textbox decoding ends with any of the above text control characters
            msgCtx->msgMode = MSGMODE_TEXT_DISPLAYING;
            msgCtx->textDrawPos = 1;
            R_TEXT_INIT_YPOS = R_TEXTBOX_Y + 8;
            osSyncPrintf("ＪＪ＝%d\n", numLines);
            if (msgCtx->textBoxType != TEXTBOX_TYPE_NONE_BOTTOM) {
                if (numLines == 0) {
                    R_TEXT_INIT_YPOS = (u16)(R_TEXTBOX_Y + 26);
                } else if (numLines == 1) {
                    R_TEXT_INIT_YPOS = (u16)(R_TEXTBOX_Y + 20);
                } else if (numLines == 2) {
                    R_TEXT_INIT_YPOS = (u16)(R_TEXTBOX_Y + 16);
                }
            }
            if (phi_s1 == MESSAGE_TEXTID) {
                osSyncPrintf("NZ_NEXTMSG=%x, %x, %x\n", font->msgBuf[msgCtx->msgBufPos],
                             font->msgBuf[msgCtx->msgBufPos + 1], font->msgBuf[msgCtx->msgBufPos + 2]);
                MESSAGE_DECODE_REQUIRE_INDEX((decodedBufPos + 1));
                temp_s2 = msgCtx->msgBufDecoded[++decodedBufPos] = font->msgBuf[msgCtx->msgBufPos + 1];
                MESSAGE_DECODE_REQUIRE_INDEX((decodedBufPos + 1));
                msgCtx->msgBufDecoded[++decodedBufPos] = font->msgBuf[msgCtx->msgBufPos + 2];
                phi_s0_3 = temp_s2 << 8;
                sNextTextId = msgCtx->msgBufDecoded[decodedBufPos] | phi_s0_3;
            }
            if (phi_s1 == MESSAGE_BOX_BREAK_DELAYED) {
                MESSAGE_DECODE_REQUIRE_INDEX((decodedBufPos + 1));
                msgCtx->msgBufDecoded[++decodedBufPos] = font->msgBuf[msgCtx->msgBufPos + 1];
                msgCtx->msgBufPos += 2;
            }
            msgCtx->decodedTextLen = decodedBufPos;
            if (sTextboxSkipped) {
                msgCtx->textDrawPos =
                    (msgCtx->decodedTextLen != 0 || !LinkSpan_EngineExtended()) ? msgCtx->decodedTextLen : 1;
            }
            break;
        } else if (temp_s2 == MESSAGE_NAME) {
            // Substitute the player name control character for the file's player name.
            // #region SOH [NTSC] - Support PAL and NTSC with either language
            //                      Falha de capacidade encerra a caixa sem fallback.
            if (!Message_DecodeName(play, &decodedBufPos, &charTexIdx)) {
                goto decodeOverflow;
            }
            // #endregion
        } else if (temp_s2 == MESSAGE_MARATHON_TIME || temp_s2 == MESSAGE_RACE_TIME) {
            // Convert the values of the appropriate timer to digits and add the
            //  digits to the decoded buffer in place of the control character.
            // "EVENT timer"
            osSyncPrintf("\nＥＶＥＮＴタイマー ＝ ");
            digits[0] = digits[1] = digits[2] = 0;
            if (temp_s2 == MESSAGE_RACE_TIME) {
                digits[3] = gSaveContext.timerSeconds;
            } else {
                digits[3] = gSaveContext.subTimerSeconds;
            }

            while (digits[3] >= 60) {
                digits[1]++;
                if (digits[1] >= 10) {
                    digits[0]++;
                    digits[1] -= 10;
                }
                digits[3] -= 60;
            }
            while (digits[3] >= 10) {
                digits[2]++;
                digits[3] -= 10;
            }

            for (i = 0; i < 4; i++) {
                MESSAGE_DECODE_REQUIRE_GLYPH(charTexIdx);
                Font_LoadChar(font, digits[i] + '0' - ' ', charTexIdx);
                charTexIdx += FONT_CHAR_TEX_SIZE;
                MESSAGE_DECODE_REQUIRE_INDEX(decodedBufPos);
                msgCtx->msgBufDecoded[decodedBufPos] = digits[i] + '0';
                decodedBufPos++;
                if (i == 1) {
                    MESSAGE_DECODE_REQUIRE_GLYPH(charTexIdx);
                    Font_LoadChar(font, '"' - ' ', charTexIdx);
                    charTexIdx += FONT_CHAR_TEX_SIZE;
                    MESSAGE_DECODE_REQUIRE_INDEX(decodedBufPos);
                    msgCtx->msgBufDecoded[decodedBufPos] = '"';
                    decodedBufPos++;
                } else if (i == 3) {
                    MESSAGE_DECODE_REQUIRE_GLYPH(charTexIdx);
                    Font_LoadChar(font, '"' - ' ', charTexIdx);
                    charTexIdx += FONT_CHAR_TEX_SIZE;
                    MESSAGE_DECODE_REQUIRE_INDEX(decodedBufPos);
                    msgCtx->msgBufDecoded[decodedBufPos] = '"';
                }
            }
        } else if (temp_s2 == MESSAGE_POINTS) {
            // Convert the values of the current minigame score to digits and
            //  add the digits to the decoded buffer in place of the control character.
            // "Yabusame score"
            osSyncPrintf("\n流鏑馬スコア ＝ %d\n", gSaveContext.minigameScore);
            digits[0] = digits[1] = digits[2] = 0;
            digits[3] = gSaveContext.minigameScore;

            while (digits[3] >= 1000) {
                digits[0]++;
                digits[3] -= 1000;
            }
            while (digits[3] >= 100) {
                digits[1]++;
                digits[3] -= 100;
            }
            while (digits[3] >= 10) {
                digits[2]++;
                digits[3] -= 10;
            }

            loadChar = false;
            for (i = 0; i < 4; i++) {
                if (i == 3 || digits[i] != 0) {
                    loadChar = true;
                }
                if (loadChar) {
                    MESSAGE_DECODE_REQUIRE_GLYPH(charTexIdx);
                    Font_LoadChar(font, digits[i] + '0' - ' ', charTexIdx);
                    MESSAGE_DECODE_REQUIRE_INDEX(decodedBufPos);
                    msgCtx->msgBufDecoded[decodedBufPos] = digits[i] + '0';
                    charTexIdx += FONT_CHAR_TEX_SIZE;
                    decodedBufPos++;
                }
            }
            decodedBufPos--;
        } else if (temp_s2 == MESSAGE_TOKENS) {
            // Convert the current number of collected gold skulltula tokens to digits and
            //  add the digits to the decoded buffer in place of the control character.
            // "Total number of gold stars"
            osSyncPrintf("\n金スタ合計数 ＝ %d", gSaveContext.inventory.gsTokens);
            digits[0] = digits[1] = 0;
            digits[2] = gSaveContext.inventory.gsTokens;

            while (digits[2] >= 100) {
                digits[0]++;
                digits[2] -= 100;
            }
            while (digits[2] >= 10) {
                digits[1]++;
                digits[2] -= 10;
            }

            loadChar = false;
            for (i = 0; i < 3; i++) {
                if (i == 2 || digits[i] != 0) {
                    loadChar = true;
                }
                if (loadChar) {
                    MESSAGE_DECODE_REQUIRE_GLYPH(charTexIdx);
                    Font_LoadChar(font, digits[i] + '0' - ' ', charTexIdx);
                    MESSAGE_DECODE_REQUIRE_INDEX(decodedBufPos);
                    msgCtx->msgBufDecoded[decodedBufPos] = digits[i] + '0';
                    charTexIdx += FONT_CHAR_TEX_SIZE;
                    osSyncPrintf("%x(%x) ", digits[i] + '0' - ' ', digits[i]);
                    decodedBufPos++;
                }
            }
            decodedBufPos--;
        } else if (temp_s2 == MESSAGE_FISH_INFO) {
            // "Fishing hole fish size"
            osSyncPrintf("\n釣り堀魚サイズ ＝ ");
            digits[0] = 0;
            digits[1] = gSaveContext.minigameScore;

            while (digits[1] >= 10) {
                digits[0]++;
                digits[1] -= 10;
            }

            for (i = 0; i < 2; i++) {
                if (i == 1 || digits[i] != 0) {
                    MESSAGE_DECODE_REQUIRE_GLYPH(charTexIdx);
                    Font_LoadChar(font, digits[i] + '0' - ' ', charTexIdx);
                    MESSAGE_DECODE_REQUIRE_INDEX(decodedBufPos);
                    msgCtx->msgBufDecoded[decodedBufPos] = digits[i] + '0';
                    charTexIdx += FONT_CHAR_TEX_SIZE;
                    osSyncPrintf("%x(%x) ", digits[i] + '0' - ' ', digits[i]);
                    decodedBufPos++;
                }
            }
            decodedBufPos--;
        } else if (temp_s2 == MESSAGE_HIGHSCORE) {
            phi_s0_3 = HIGH_SCORE((u8)font->msgBuf[++msgCtx->msgBufPos]);
            // "Highscore"
            osSyncPrintf("ランキング＝%d\n", font->msgBuf[msgCtx->msgBufPos]);
            if ((font->msgBuf[msgCtx->msgBufPos] & 0xFF) == 2) {
                if (LINK_AGE_IN_YEARS == YEARS_CHILD) {
                    phi_s0_3 &= 0x7F;
                } else {
                    osSyncPrintf("HI_SCORE( kanfont->mbuff.nes_mes_buf[message->rdp] & 0xff000000 ) = %x\n",
                                 HIGH_SCORE(font->msgBufWide[msgCtx->msgBufPos] & 0xFF000000));
                    phi_s0_3 = ((HIGH_SCORE((u8)font->msgBuf[msgCtx->msgBufPos]) & 0xFF000000) >> 0x18) & 0x7F;
                }
                phi_s0_3 = SQ((f32)phi_s0_3) * 0.0036f + 0.5f;
                osSyncPrintf("score=%d\n", phi_s0_3);
            }
            switch (font->msgBuf[msgCtx->msgBufPos] & 0xFF) {
                case HS_HBA:
                case HS_POE_POINTS:
                case HS_FISHING:
                    digits[0] = digits[1] = digits[2] = 0;
                    digits[3] = phi_s0_3;

                    while (digits[3] >= 1000) {
                        digits[0]++;
                        digits[3] -= 1000;
                    }
                    while (digits[3] >= 100) {
                        digits[1]++;
                        digits[3] -= 100;
                    }
                    while (digits[3] >= 10) {
                        digits[2]++;
                        digits[3] -= 10;
                    }
                    if (temp_s2) {}

                    loadChar = false;
                    for (i = 0; i < 4; i++) {
                        if (i == 3 || digits[i] != 0) {
                            loadChar = true;
                        }
                        if (loadChar) {
                            MESSAGE_DECODE_REQUIRE_GLYPH(charTexIdx);
                            Font_LoadChar(font, digits[i] + '0' - ' ', charTexIdx);
                            MESSAGE_DECODE_REQUIRE_INDEX(decodedBufPos);
                            msgCtx->msgBufDecoded[decodedBufPos] = digits[i] + '0';
                            charTexIdx += FONT_CHAR_TEX_SIZE;
                            decodedBufPos++;
                        }
                    }
                    decodedBufPos--;
                    break;
                case HS_UNK_05:
                    break;
                case HS_HORSE_RACE:
                case HS_MARATHON:
                case HS_DAMPE_RACE:
                    digits[0] = digits[1] = digits[2] = 0;
                    digits[3] = phi_s0_3;

                    while (digits[3] >= 60) {
                        digits[1]++;
                        if (digits[1] >= 10) {
                            digits[0]++;
                            digits[1] -= 10;
                        }
                        digits[3] -= 60;
                    }
                    while (digits[3] >= 10) {
                        digits[2]++;
                        digits[3] -= 10;
                    }

                    for (i = 0; i < 4; i++) {
                        MESSAGE_DECODE_REQUIRE_GLYPH(charTexIdx);
                        Font_LoadChar(font, digits[i] + '0' - ' ', charTexIdx);
                        charTexIdx += FONT_CHAR_TEX_SIZE;
                        MESSAGE_DECODE_REQUIRE_INDEX(decodedBufPos);
                        msgCtx->msgBufDecoded[decodedBufPos] = digits[i] + '0';
                        decodedBufPos++;
                        if (i == 1) {
                            MESSAGE_DECODE_REQUIRE_GLYPH(charTexIdx);
                            Font_LoadChar(font, '"' - ' ', charTexIdx);
                            charTexIdx += FONT_CHAR_TEX_SIZE;
                            MESSAGE_DECODE_REQUIRE_INDEX(decodedBufPos);
                            msgCtx->msgBufDecoded[decodedBufPos] = '"';
                            decodedBufPos++;
                        } else if (i == 3) {
                            MESSAGE_DECODE_REQUIRE_GLYPH(charTexIdx);
                            Font_LoadChar(font, '"' - ' ', charTexIdx);
                            charTexIdx += FONT_CHAR_TEX_SIZE;
                            MESSAGE_DECODE_REQUIRE_INDEX(decodedBufPos);
                            msgCtx->msgBufDecoded[decodedBufPos] = '"';
                        }
                    }
                    break;
            }
        } else if (temp_s2 == MESSAGE_TIME) {
            // "Zelda time"
            osSyncPrintf("\nゼルダ時間 ＝ ");
            digits[0] = 0;
            timeInSeconds = gSaveContext.dayTime * (24.0f * 60.0f / 0x10000);

            digits[1] = timeInSeconds / 60.0f;
            while (digits[1] >= 10) {
                digits[0]++;
                digits[1] -= 10;
            }
            digits[2] = 0;
            digits[3] = (s16)timeInSeconds % 60;
            while (digits[3] >= 10) {
                digits[2]++;
                digits[3] -= 10;
            }

            for (i = 0; i < 4; i++) {
                MESSAGE_DECODE_REQUIRE_GLYPH(charTexIdx);
                Font_LoadChar(font, digits[i] + '0' - ' ', charTexIdx);
                charTexIdx += FONT_CHAR_TEX_SIZE;
                MESSAGE_DECODE_REQUIRE_INDEX(decodedBufPos);
                msgCtx->msgBufDecoded[decodedBufPos] = digits[i] + '0';
                decodedBufPos++;
                if (i == 1) {
                    MESSAGE_DECODE_REQUIRE_GLYPH(charTexIdx);
                    Font_LoadChar(font, ':' - ' ', charTexIdx);
                    charTexIdx += FONT_CHAR_TEX_SIZE;
                    MESSAGE_DECODE_REQUIRE_INDEX(decodedBufPos);
                    msgCtx->msgBufDecoded[decodedBufPos] = ':';
                    decodedBufPos++;
                }
            }
            decodedBufPos--;
        } else if (temp_s2 == MESSAGE_ITEM_ICON) {
            MESSAGE_DECODE_REQUIRE_INDEX((decodedBufPos + 1));
            msgCtx->msgBufDecoded[++decodedBufPos] = font->msgBuf[msgCtx->msgBufPos + 1];
            osSyncPrintf("ITEM_NO=(%d) (%d)\n", msgCtx->msgBufDecoded[decodedBufPos],
                         font->msgBuf[msgCtx->msgBufPos + 1]);
            // Skijer's NEI: ícone também para os itens acima de ITEM_CUSTOM (itens do NEI).
            if (GameInteractor_Should(VB_LOAD_ITEM_ICON, true, sDisplayNextMessageAsEnglish)) {
                Message_LoadItemIcon(play, (u8)font->msgBuf[msgCtx->msgBufPos + 1], R_TEXTBOX_Y + 10);
            }
        } else if (temp_s2 == MESSAGE_BACKGROUND) {
            msgCtx->textboxBackgroundIdx = font->msgBuf[msgCtx->msgBufPos + 1] * 2;
            msgCtx->textboxBackgroundForeColorIdx = (font->msgBuf[msgCtx->msgBufPos + 2] & 0xF0) >> 4;
            msgCtx->textboxBackgroundBackColorIdx = font->msgBuf[msgCtx->msgBufPos + 2] & 0xF;
            msgCtx->textboxBackgroundYOffsetIdx = (font->msgBuf[msgCtx->msgBufPos + 3] & 0xF0) >> 4;
            msgCtx->textboxBackgroundUnkArg = font->msgBuf[msgCtx->msgBufPos + 3] & 0xF;

            memcpy((uintptr_t)msgCtx->textboxSegment + MESSAGE_STATIC_TEX_SIZE, gRedMessageXLeftTex,
                   strlen(gRedMessageXLeftTex) + 1);
            memcpy((uintptr_t)msgCtx->textboxSegment + MESSAGE_STATIC_TEX_SIZE + 0x900, gRedMessageXRightTex,
                   strlen(gRedMessageXRightTex) + 1);

            msgCtx->msgBufPos += 3;
            R_TEXTBOX_BG_YPOS = R_TEXTBOX_Y + 8;
            numLines = 2;
            R_TEXT_INIT_XPOS = 50;
        } else if (temp_s2 == MESSAGE_COLOR) {
            MESSAGE_DECODE_REQUIRE_INDEX((decodedBufPos + 1));
            msgCtx->msgBufDecoded[++decodedBufPos] = font->msgBuf[++msgCtx->msgBufPos];
        } else if (temp_s2 == MESSAGE_NEWLINE) {
            numLines++;
        } else if (temp_s2 != MESSAGE_QUICKTEXT_ENABLE && temp_s2 != MESSAGE_QUICKTEXT_DISABLE &&
                   temp_s2 != MESSAGE_AWAIT_BUTTON_PRESS && temp_s2 != MESSAGE_OCARINA &&
                   temp_s2 != MESSAGE_PERSISTENT && temp_s2 != MESSAGE_UNSKIPPABLE) {
            if (temp_s2 == MESSAGE_FADE) {
                sTextFade = true;
                osSyncPrintf("NZ_TIMER_END (key_off_flag=%d)\n", sTextFade);
                MESSAGE_DECODE_REQUIRE_INDEX((decodedBufPos + 1));
                msgCtx->msgBufDecoded[++decodedBufPos] = font->msgBuf[++msgCtx->msgBufPos];
            } else if (temp_s2 == MESSAGE_FADE2) {
                sTextFade = true;
                osSyncPrintf("NZ_BGM (key_off_flag=%d)\n", sTextFade);
                MESSAGE_DECODE_REQUIRE_INDEX((decodedBufPos + 1));
                msgCtx->msgBufDecoded[++decodedBufPos] = font->msgBuf[++msgCtx->msgBufPos];
                MESSAGE_DECODE_REQUIRE_INDEX((decodedBufPos + 1));
                msgCtx->msgBufDecoded[++decodedBufPos] = font->msgBuf[++msgCtx->msgBufPos];
            } else if (temp_s2 == MESSAGE_SHIFT || temp_s2 == MESSAGE_TEXT_SPEED) {
                MESSAGE_DECODE_REQUIRE_INDEX((decodedBufPos + 1));
                msgCtx->msgBufDecoded[++decodedBufPos] = font->msgBuf[++msgCtx->msgBufPos] & 0xFF;
            } else if (temp_s2 == MESSAGE_SFX) {
                MESSAGE_DECODE_REQUIRE_INDEX((decodedBufPos + 1));
                msgCtx->msgBufDecoded[++decodedBufPos] = font->msgBuf[++msgCtx->msgBufPos];
                MESSAGE_DECODE_REQUIRE_INDEX((decodedBufPos + 1));
                msgCtx->msgBufDecoded[++decodedBufPos] = font->msgBuf[++msgCtx->msgBufPos];
            } else if (temp_s2 == MESSAGE_TWO_CHOICE) {
                msgCtx->choiceNum = 2;
            } else if (temp_s2 == MESSAGE_THREE_CHOICE) {
                msgCtx->choiceNum = 3;
            } else if (temp_s2 != ' ') {
                MESSAGE_DECODE_REQUIRE_GLYPH(charTexIdx);
                Font_LoadChar(font, temp_s2 - ' ', charTexIdx);
                charTexIdx += FONT_CHAR_TEX_SIZE;
            }
        }
        decodedBufPos++;
        msgCtx->msgBufPos++;
    }
    return true;

decodeOverflow:
    // SOH [Link-Span] R04: falha explicita, sem controles do prefixo.
    osSyncPrintf("[R04] message %04x lang=%d raw=%u decoded=%d capacity=%d glyph=%d glyphCapacity=%d: limite de texto/glifos ou entrada incompleta; encerrando\n",
                 msgCtx->textId, gSaveContext.language, rawTokenStart, decodedBufPos, decodedLimit,
                 charTexIdx, (int)ARRAY_COUNT(font->charTexBuf));
    msgCtx->msgBufPos = rawTokenStart;
    Message_DiscardDecoded(play, false);
    return false;
}
