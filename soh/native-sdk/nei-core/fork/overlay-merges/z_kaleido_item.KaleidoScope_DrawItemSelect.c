/* overlay-merge f537f85bd023 4c1a0f76fe3d 956e73cd62ea */
void KaleidoScope_DrawItemSelect(PlayState* play) {
    static s16 magicArrowEffectsR[] = { 255, 100, 255 };
    static s16 magicArrowEffectsG[] = { 0, 100, 255 };
    static s16 magicArrowEffectsB[] = { 0, 255, 100 };
    static u8 sLinkSpanEquipMenuOpen = 0;
    static u8 sLinkSpanEquipChoice = 1;
    static LinkSpanMenuInput sLinkSpanEquipNavigation = { 0 };
    static LinkSpanMenuInput sLinkSpanVariantNavigation = { 0 };
    static NeiInventoryGesture sLinkSpanAGesture = { 0 };
    static u16 sLinkSpanPreviousPad = 0;
    static u16 sLinkSpanEquipItem = ITEM_NONE;
    static u16 sLinkSpanEquipSlot = 0;
    static u16 sLinkSpanEquipVisualSlot = 0;
    Input* input = &play->state.input[0];
    PauseContext* pauseCtx = &play->pauseCtx;
    u16 i;
    u16 j;
    u16 cursorItem;
    u16 cursorSlot = 0;
    u16 index;
    s16 cursorPoint;
    s16 cursorX;
    s16 cursorY;
    s16 oldCursorPoint;
    s16 moveCursorResult;
    NeiInventoryAction aAction = NEI_INVENTORY_NONE;
    const bool aDown = CHECK_BTN_ALL(input->cur.button, BTN_A);
    const bool variantWasOpen = IsItemCycling() || sGustOverlayActive || sArrowWheelOverlayActive;
    const bool itemContext = pauseCtx->state == 6 && pauseCtx->pageIndex == PAUSE_ITEM &&
                             pauseCtx->unk_1E4 == 0 && pauseCtx->cursorSpecialPos == 0;
    const u16 physicalPad = NeiInventory_PadButtons();
    input->press.button |= physicalPad & ~sLinkSpanPreviousPad;
    input->cur.button |= physicalPad;
    sLinkSpanPreviousPad = physicalPad;
    if ((physicalPad & BTN_DRIGHT) && LinkSpan_DpadHudOwned()) {
        // The D-pad HUD owner maps this physical direction to C-Up outside the inventory.
        input->press.button &= ~BTN_CUP;
        input->cur.button &= ~BTN_CUP;
    }

    if (!itemContext || sLinkSpanEquipMenuOpen || variantWasOpen) {
        NeiInventoryCancel(&sLinkSpanAGesture, aDown);
    } else {
        const u32 context = (ExtInv_GetCurrentPage() * 24 + pauseCtx->cursorSlot[PAUSE_ITEM]) |
                            ((u32)pauseCtx->cursorItem[PAUSE_ITEM] << 8);
        aAction = NeiInventoryInput(&sLinkSpanAGesture, aDown, context, NeiInventory_Milliseconds());
        input->press.button &= ~BTN_A;
        if (aAction == NEI_INVENTORY_VARIANT) input->press.button |= BTN_A;
        // Keep the cursor on the item whose A gesture is pending.
        if (sLinkSpanAGesture.down && !sLinkSpanAGesture.fired) {
            pauseCtx->stickRelX = pauseCtx->stickRelY = 0;
            input->press.button &= ~(BTN_DLEFT | BTN_DRIGHT | BTN_DUP | BTN_DDOWN);
        }
    }
    if (variantWasOpen) {
        const int step = LinkSpanMenuStep(&sLinkSpanVariantNavigation, input->rel.stick_x,
                                          input->rel.right_stick_x, 0);
        pauseCtx->stickRelX = step * 40;
        pauseCtx->stickRelY = 0;
    } else {
        sLinkSpanVariantNavigation = (LinkSpanMenuInput){ 0 };
    }

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_42Opa(play->state.gfxCtx);

    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

    if (!LinkSpan_PauseEquipSelectorEnabled() || pauseCtx->state != 6 ||
        pauseCtx->pageIndex != PAUSE_ITEM || pauseCtx->unk_1E4 != 0 || pauseCtx->cursorSpecialPos != 0 ||
        (sLinkSpanEquipMenuOpen && pauseCtx->cursorSlot[PAUSE_ITEM] != sLinkSpanEquipVisualSlot)) {
        sLinkSpanEquipMenuOpen = 0;
    }
    if (sLinkSpanEquipMenuOpen) {
        const bool next = CHECK_BTN_ANY(input->press.button, BTN_DRIGHT | BTN_DUP);
        const bool previous = CHECK_BTN_ANY(input->press.button, BTN_DLEFT | BTN_DDOWN);
        const int step = LinkSpanMenuStep(&sLinkSpanEquipNavigation, input->rel.stick_x,
                                          input->rel.right_stick_x, next == previous ? 0 : next ? 1 : -1);
        if (step > 0) {
            if (sLinkSpanEquipChoice < 3) ++sLinkSpanEquipChoice;
        } else if (step < 0) {
            if (sLinkSpanEquipChoice > 1) --sLinkSpanEquipChoice;
        }
        if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
            sLinkSpanEquipMenuOpen = 0;
        } else if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
            const u16 selectedButton = sLinkSpanEquipChoice == 1 ? BTN_CLEFT :
                                       sLinkSpanEquipChoice == 2 ? BTN_CDOWN : BTN_CRIGHT;
            if (GameInteractor_Should(VB_EQUIP_ITEM_TO_C_BUTTON, true, play, sLinkSpanEquipSlot,
                                      sLinkSpanEquipItem)) {
                input->press.button |= selectedButton;
                KaleidoScope_SetupItemEquip(play, sLinkSpanEquipItem, sLinkSpanEquipSlot,
                                            pauseCtx->itemVtx[sLinkSpanEquipVisualSlot * 4].v.ob[0] * 10,
                                            pauseCtx->itemVtx[sLinkSpanEquipVisualSlot * 4].v.ob[1] * 10);
                input->press.button &= ~selectedButton;
            }
            sLinkSpanEquipMenuOpen = 0;
        }
        input->press.button &= ~(BTN_A | BTN_B | BTN_DLEFT | BTN_DRIGHT | BTN_DUP | BTN_DDOWN);
        pauseCtx->stickRelX = pauseCtx->stickRelY = 0;
    }

    pauseCtx->cursorColorSet = 0;
    pauseCtx->nameColorSet = 0;

    // Update extended inventory pagination timer
    ExtInv_Update();

    if ((pauseCtx->state == 6) && (pauseCtx->unk_1E4 == 0) && (pauseCtx->pageIndex == PAUSE_ITEM)) {
        // Harpoon GM-mode: HOLD C-Up for 20 frames (~1/3 sec) while
        // hovering an inventory slot to drop the item. Multiplayer-only
        // (the C bridge no-ops if not in a Harpoon room). Holding (not
        // press-only) prevents accidental drops when the player taps
        // C-Up to switch into D-Pad swap mode. Counter resets when the
        // slot changes or C-Up is released.
        {
            static s32 sHarpoonHoldFrames = 0;
            static s16 sHarpoonHoldSlot = -1;
            s16 curSlot = pauseCtx->cursorSlot[PAUSE_ITEM];
            if (CHECK_BTN_ALL(input->cur.button, BTN_CUP) && curSlot >= 0) {
                if (sHarpoonHoldSlot != curSlot) {
                    sHarpoonHoldSlot = curSlot;
                    sHarpoonHoldFrames = 0;
                }
                sHarpoonHoldFrames++;
                if (sHarpoonHoldFrames == 20) {
                    extern void HarpoonDrops_RequestDropFromPause(int tabId, int slot);
                    HarpoonDrops_RequestDropFromPause(/*tabId=items*/ 0, curSlot);
                    // Continue counting so a long hold doesn't re-fire
                    // every frame — only the single fire at exactly 20.
                }
            } else {
                sHarpoonHoldFrames = 0;
                sHarpoonHoldSlot = -1;
            }
        }
        bool dpad = (CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0) && !CHECK_BTN_ALL(input->cur.button, BTN_CUP));
        bool pauseAnyCursor =
            pauseCtx->cursorSpecialPos == 0 &&
            ((CVarGetInteger(CVAR_ENHANCEMENT("PauseAnyCursor"), 0) == PAUSE_ANY_CURSOR_RANDO_ONLY && IS_RANDO) ||
             (CVarGetInteger(CVAR_ENHANCEMENT("PauseAnyCursor"), 0) == PAUSE_ANY_CURSOR_ALWAYS_ON));

        moveCursorResult = 0 || IsItemCycling() || sGustOverlayActive || sArrowWheelOverlayActive;
        oldCursorPoint = pauseCtx->cursorPoint[PAUSE_ITEM];

        cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];
        cursorSlot = pauseCtx->cursorSlot[PAUSE_ITEM];

        if (pauseCtx->cursorSpecialPos == 0) {
            pauseCtx->cursorColorSet = 4;

            // Inventory sub-page switch (vanilla / custom items / MM masks).
            // It uses whichever shoulder button is NOT bound to kaleido tab
            // switching: tab switching (KaleidoScope_HandlePageToggles) uses L
            // by default (NGCKaleidoSwitcher) and Z when the switcher is off, so
            // this takes the freed button and the two never collide.
            bool ngcMode = CVarGetInteger(CVAR_ENHANCEMENT("NGCKaleidoSwitcher"), 0) != 0;
            s16 freedBtn = ngcMode ? BTN_Z : BTN_L;

            if (ExtInv_CanSwitchPage() && CHECK_BTN_ALL(input->press.button, freedBtn) && !IsItemCycling()) {
                ExtInv_SwitchPage();
                Audio_PlaySoundGeneral(NA_SE_SY_HP_RECOVER, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                moveCursorResult = 2;
            }

            if (cursorItem == PAUSE_ITEM_NONE) {
                pauseCtx->stickRelX = 40;
            }

            if ((ABS(pauseCtx->stickRelX) > 30) ||
                (dpad && CHECK_BTN_ANY(input->press.button, BTN_DLEFT | BTN_DRIGHT))) {
                cursorPoint = pauseCtx->cursorPoint[PAUSE_ITEM];
                cursorX = pauseCtx->cursorX[PAUSE_ITEM];
                cursorY = pauseCtx->cursorY[PAUSE_ITEM];

                osSyncPrintf("now=%d  ccc=%d\n", cursorPoint, cursorItem);

                // Seem necessary to match
                if (pauseCtx->cursorX[PAUSE_ITEM]) {}
                if (ExtInv_GetSlotItem(pauseCtx->cursorPoint[PAUSE_ITEM])) {} // Skijer's NEI

                while (moveCursorResult == 0) {
                    if ((pauseCtx->stickRelX < -30) || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DLEFT))) {
                        if (pauseCtx->cursorX[PAUSE_ITEM] != 0) {
                            pauseCtx->cursorX[PAUSE_ITEM] -= 1;
                            pauseCtx->cursorPoint[PAUSE_ITEM] -= 1;
                            if ((ExtInv_GetSlotItem(ExtInv_GetInventorySlot(pauseCtx->cursorPoint[PAUSE_ITEM])) !=
                                 ITEM_NONE) || // Skijer's NEI
                                pauseAnyCursor) {
                                moveCursorResult = 1;
                            }
                        } else {
                            pauseCtx->cursorX[PAUSE_ITEM] = cursorX;
                            pauseCtx->cursorY[PAUSE_ITEM] += 1;

                            if (pauseCtx->cursorY[PAUSE_ITEM] >= 4) {
                                pauseCtx->cursorY[PAUSE_ITEM] = 0;
                            }

                            pauseCtx->cursorPoint[PAUSE_ITEM] =
                                pauseCtx->cursorX[PAUSE_ITEM] + (pauseCtx->cursorY[PAUSE_ITEM] * 6);

                            if (pauseCtx->cursorPoint[PAUSE_ITEM] >= 24) {
                                pauseCtx->cursorPoint[PAUSE_ITEM] = pauseCtx->cursorX[PAUSE_ITEM];
                            }

                            if (cursorY == pauseCtx->cursorY[PAUSE_ITEM]) {
                                pauseCtx->cursorX[PAUSE_ITEM] = cursorX;
                                pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;

                                KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_LEFT);

                                moveCursorResult = 2;
                            }
                        }
                    } else if ((pauseCtx->stickRelX > 30) || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DRIGHT))) {
                        if (pauseCtx->cursorX[PAUSE_ITEM] < 5) {
                            pauseCtx->cursorX[PAUSE_ITEM] += 1;
                            pauseCtx->cursorPoint[PAUSE_ITEM] += 1;
                            if ((ExtInv_GetSlotItem(ExtInv_GetInventorySlot(pauseCtx->cursorPoint[PAUSE_ITEM])) !=
                                 ITEM_NONE) || // Skijer's NEI
                                pauseAnyCursor) {
                                moveCursorResult = 1;
                            }
                        } else {
                            pauseCtx->cursorX[PAUSE_ITEM] = cursorX;
                            pauseCtx->cursorY[PAUSE_ITEM] += 1;

                            if (pauseCtx->cursorY[PAUSE_ITEM] >= 4) {
                                pauseCtx->cursorY[PAUSE_ITEM] = 0;
                            }

                            pauseCtx->cursorPoint[PAUSE_ITEM] =
                                pauseCtx->cursorX[PAUSE_ITEM] + (pauseCtx->cursorY[PAUSE_ITEM] * 6);

                            if (pauseCtx->cursorPoint[PAUSE_ITEM] >= 24) {
                                pauseCtx->cursorPoint[PAUSE_ITEM] = pauseCtx->cursorX[PAUSE_ITEM];
                            }

                            if (cursorY == pauseCtx->cursorY[PAUSE_ITEM]) {
                                pauseCtx->cursorX[PAUSE_ITEM] = cursorX;
                                pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;

                                KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_RIGHT);

                                moveCursorResult = 2;
                            }
                        }
                    }
                }

                if (moveCursorResult == 1) {
                    cursorItem =
                        ExtInv_GetSlotItem(ExtInv_GetInventorySlot(pauseCtx->cursorPoint[PAUSE_ITEM])); // Skijer's NEI
                }

                osSyncPrintf("【Ｘ cursor=%d(%) (cur_xpt=%d)(ok_fg=%d)(ccc=%d)(key_angle=%d)】  ",
                             pauseCtx->cursorPoint[PAUSE_ITEM], pauseCtx->cursorX[PAUSE_ITEM], moveCursorResult,
                             cursorItem, pauseCtx->cursorSpecialPos);
            }
        } else if (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_LEFT) {
            if ((pauseCtx->stickRelX > 30) || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DRIGHT))) {
                pauseCtx->nameDisplayTimer = 0;
                pauseCtx->cursorSpecialPos = 0;

                Audio_PlaySfxGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                     &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);

                cursorPoint = cursorX = cursorY = 0;
                while (true) {
                    if (ExtInv_GetSlotItem(ExtInv_GetInventorySlot(cursorPoint)) != ITEM_NONE) { // Skijer's NEI
                        pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;
                        pauseCtx->cursorX[PAUSE_ITEM] = cursorX;
                        pauseCtx->cursorY[PAUSE_ITEM] = cursorY;
                        moveCursorResult = 1;
                        break;
                    }

                    cursorY = cursorY + 1;
                    cursorPoint = cursorPoint + 6;
                    if (cursorY < 4) {
                        continue;
                    }

                    cursorY = 0;
                    cursorPoint = cursorX + 1;
                    cursorX = cursorPoint;
                    if (cursorX < 6) {
                        continue;
                    }

                    KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_RIGHT);
                    break;
                }
            }
        } else {
            if ((pauseCtx->stickRelX < -30) || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DLEFT))) {
                pauseCtx->nameDisplayTimer = 0;
                pauseCtx->cursorSpecialPos = 0;

                Audio_PlaySfxGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                     &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);

                cursorPoint = cursorX = 5;
                cursorY = 0;
                while (true) {
                    if (ExtInv_GetSlotItem(ExtInv_GetInventorySlot(cursorPoint)) != ITEM_NONE) { // Skijer's NEI
                        pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;
                        pauseCtx->cursorX[PAUSE_ITEM] = cursorX;
                        pauseCtx->cursorY[PAUSE_ITEM] = cursorY;
                        moveCursorResult = 1;
                        break;
                    }

                    cursorY = cursorY + 1;
                    cursorPoint = cursorPoint + 6;
                    if (cursorY < 4) {
                        continue;
                    }

                    cursorY = 0;
                    cursorPoint = cursorX - 1;
                    cursorX = cursorPoint;
                    if (cursorX >= 0) {
                        continue;
                    }

                    KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_LEFT);
                    break;
                }
            }
        }

        if (pauseCtx->cursorSpecialPos == 0) {
            if (cursorItem != PAUSE_ITEM_NONE) {
                if ((ABS(pauseCtx->stickRelY) > 30) ||
                    (dpad && CHECK_BTN_ANY(input->press.button, BTN_DDOWN | BTN_DUP))) {
                    moveCursorResult = 0 || IsItemCycling() || sGustOverlayActive || sArrowWheelOverlayActive;

                    cursorPoint = pauseCtx->cursorPoint[PAUSE_ITEM];
                    cursorY = pauseCtx->cursorY[PAUSE_ITEM];
                    while (moveCursorResult == 0) {
                        if ((pauseCtx->stickRelY > 30) || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DUP))) {
                            if (pauseCtx->cursorY[PAUSE_ITEM] != 0) {
                                pauseCtx->cursorY[PAUSE_ITEM] -= 1;
                                pauseCtx->cursorPoint[PAUSE_ITEM] -= 6;
                                if ((ExtInv_GetSlotItem(ExtInv_GetInventorySlot(pauseCtx->cursorPoint[PAUSE_ITEM])) !=
                                     ITEM_NONE) || // Skijer's NEI
                                    pauseAnyCursor) {
                                    moveCursorResult = 1;
                                }
                            } else {
                                pauseCtx->cursorY[PAUSE_ITEM] = cursorY;
                                pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;

                                moveCursorResult = 2;
                            }
                        } else if ((pauseCtx->stickRelY < -30) ||
                                   (dpad && CHECK_BTN_ALL(input->press.button, BTN_DDOWN))) {
                            if (pauseCtx->cursorY[PAUSE_ITEM] < 3) {
                                pauseCtx->cursorY[PAUSE_ITEM] += 1;
                                pauseCtx->cursorPoint[PAUSE_ITEM] += 6;
                                if ((ExtInv_GetSlotItem(ExtInv_GetInventorySlot(pauseCtx->cursorPoint[PAUSE_ITEM])) !=
                                     ITEM_NONE) || // Skijer's NEI
                                    pauseAnyCursor) {
                                    moveCursorResult = 1;
                                }
                            } else {
                                pauseCtx->cursorY[PAUSE_ITEM] = cursorY;
                                pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;

                                moveCursorResult = 2;
                            }
                        }
                    }

                    cursorPoint = PAUSE_ITEM;
                    osSyncPrintf("【Ｙ cursor=%d(%) (cur_ypt=%d)(ok_fg=%d)(ccc=%d)】  ",
                                 pauseCtx->cursorPoint[cursorPoint], pauseCtx->cursorY[PAUSE_ITEM], moveCursorResult,
                                 cursorItem);
                }
            }

            cursorSlot = pauseCtx->cursorPoint[PAUSE_ITEM];

            pauseCtx->cursorColorSet = 4;

            // Calculate inventory slot with page offset using modular system
            int inventorySlot = ExtInv_GetInventorySlot(pauseCtx->cursorPoint[PAUSE_ITEM]);

            if (moveCursorResult == 1) {
                cursorItem = ExtInv_GetSlotItem(inventorySlot); // Skijer's NEI
            } else if (moveCursorResult != 2) {
                cursorItem = ExtInv_GetSlotItem(inventorySlot); // Skijer's NEI
            }

            pauseCtx->cursorItem[PAUSE_ITEM] = cursorItem;
            pauseCtx->cursorSlot[PAUSE_ITEM] = cursorSlot;

            if (!CHECK_AGE_REQ_SLOT(inventorySlot)) {
                pauseCtx->nameColorSet = 1;
            }

            if (cursorItem != PAUSE_ITEM_NONE) {
                index = cursorSlot * 4; // required to match?
                KaleidoScope_SetCursorVtx(pauseCtx, index, pauseCtx->itemVtx);

                if ((pauseCtx->debugState == 0) && (pauseCtx->state == 6) && (pauseCtx->unk_1E4 == 0)) {
                    if (LinkSpan_PauseEquipSelectorEnabled() && !sLinkSpanEquipMenuOpen &&
                        aAction == NEI_INVENTORY_EQUIP && NeiInv_CheckAgeReqSlot(inventorySlot) &&
                        cursorItem != ITEM_NONE && cursorItem != ITEM_SOLD_OUT) {
                        sLinkSpanEquipMenuOpen = 1;
                        sLinkSpanEquipChoice = 1;
                        sLinkSpanEquipNavigation = (LinkSpanMenuInput){ 0 };
                        sLinkSpanEquipItem = cursorItem;
                        sLinkSpanEquipSlot = inventorySlot;
                        sLinkSpanEquipVisualSlot = cursorSlot;
                        input->press.button &= ~BTN_A;
                    } else if (!sLinkSpanEquipMenuOpen) {
                        KaleidoScope_HandleItemCycles(play);
                    }
                    u16 buttonsToCheck = BTN_CLEFT | BTN_CDOWN | BTN_CRIGHT;
                    if (!IsItemCycling() && !sLinkSpanEquipMenuOpen &&
                        CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0) &&
                        (!CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0) ||
                         CHECK_BTN_ALL(input->cur.button, BTN_CUP))) {
                        buttonsToCheck |= BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT;
                    }
                    if (CHECK_BTN_ANY(input->press.button, buttonsToCheck) && !sGustOverlayActive &&
                        !sArrowWheelOverlayActive) {
                        if (CHECK_AGE_REQ_SLOT(inventorySlot) && (cursorItem != ITEM_SOLD_OUT) &&
                            (cursorItem != ITEM_NONE)) {
                            // Use inventorySlot (real slot 0-47) instead of cursorSlot (visual slot 0-23)
                            // This allows items from page 1 and page 2 with the same relative position to be equipped
                            // simultaneously
                            if (GameInteractor_Should(VB_EQUIP_ITEM_TO_C_BUTTON, true, play, inventorySlot,
                                                      cursorItem)) {
                                KaleidoScope_SetupItemEquip(play, cursorItem, inventorySlot,
                                                            pauseCtx->itemVtx[index].v.ob[0] * 10,
                                                            pauseCtx->itemVtx[index].v.ob[1] * 10);
                            }
                        } else {
                            Audio_PlaySfxGeneral(NA_SE_SY_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                        }
                    }
                }
            } else {
                pauseCtx->cursorVtx[0].v.ob[0] = pauseCtx->cursorVtx[2].v.ob[0] = pauseCtx->cursorVtx[1].v.ob[0] =
                    pauseCtx->cursorVtx[3].v.ob[0] = 0;

                pauseCtx->cursorVtx[0].v.ob[1] = pauseCtx->cursorVtx[1].v.ob[1] = pauseCtx->cursorVtx[2].v.ob[1] =
                    pauseCtx->cursorVtx[3].v.ob[1] = -200;
            }
        } else {
            pauseCtx->cursorItem[PAUSE_ITEM] = PAUSE_ITEM_NONE;
        }

        if (oldCursorPoint != pauseCtx->cursorPoint[PAUSE_ITEM]) {
            Audio_PlaySfxGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
    } else if ((pauseCtx->unk_1E4 == 3) && (pauseCtx->pageIndex == PAUSE_ITEM)) {
        KaleidoScope_SetCursorVtx(pauseCtx, cursorSlot * 4, pauseCtx->itemVtx);
        pauseCtx->cursorColorSet = 4;
    }

    gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, PRIMITIVE,
                      ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
    gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 0);

    for (i = 0, j = 24 * 4; i < ARRAY_COUNT(gSaveContext.equips.cButtonSlots); i++, j += 4) {
        if ((gSaveContext.equips.buttonItems[i + 1] != ITEM_NONE) &&
            !((gSaveContext.equips.buttonItems[i + 1] >= ITEM_SHIELD_DEKU) &&
              (gSaveContext.equips.buttonItems[i + 1] <= ITEM_BOOTS_HOVER))) {
            gSPVertex(POLY_OPA_DISP++, &pauseCtx->itemVtx[j], 4, 0);
            POLY_OPA_DISP = KaleidoScope_QuadTextureIA8(POLY_OPA_DISP, gEquippedItemOutlineTex, 32, 32, 0);
        }
    }

    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

    for (i = j = 0; i < 24; i++, j += 4) {
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);

        int drawSlot = ExtInv_GetInventorySlot(i);
        if (ExtInv_GetSlotItem(drawSlot) != ITEM_NONE) { // Skijer's NEI
            if ((pauseCtx->unk_1E4 == 0) && (pauseCtx->pageIndex == PAUSE_ITEM) && (pauseCtx->cursorSpecialPos == 0)) {
                if (CHECK_AGE_REQ_SLOT(drawSlot)) {
                    if ((sEquipState == 2) && (i == 3)) {
                        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, magicArrowEffectsR[pauseCtx->equipTargetItem - 0xBF],
                                        magicArrowEffectsG[pauseCtx->equipTargetItem - 0xBF],
                                        magicArrowEffectsB[pauseCtx->equipTargetItem - 0xBF], pauseCtx->alpha);

                        pauseCtx->itemVtx[j + 0].v.ob[0] = pauseCtx->itemVtx[j + 2].v.ob[0] =
                            pauseCtx->itemVtx[j + 0].v.ob[0] - 2;

                        pauseCtx->itemVtx[j + 1].v.ob[0] = pauseCtx->itemVtx[j + 3].v.ob[0] =
                            pauseCtx->itemVtx[j + 0].v.ob[0] + 32;

                        pauseCtx->itemVtx[j + 0].v.ob[1] = pauseCtx->itemVtx[j + 1].v.ob[1] =
                            pauseCtx->itemVtx[j + 0].v.ob[1] + 2;

                        pauseCtx->itemVtx[j + 2].v.ob[1] = pauseCtx->itemVtx[j + 3].v.ob[1] =
                            pauseCtx->itemVtx[j + 0].v.ob[1] - 32;
                    } else if (i == cursorSlot) {
                        pauseCtx->itemVtx[j + 0].v.ob[0] = pauseCtx->itemVtx[j + 2].v.ob[0] =
                            pauseCtx->itemVtx[j + 0].v.ob[0] - 2;

                        pauseCtx->itemVtx[j + 1].v.ob[0] = pauseCtx->itemVtx[j + 3].v.ob[0] =
                            pauseCtx->itemVtx[j + 0].v.ob[0] + 32;

                        pauseCtx->itemVtx[j + 0].v.ob[1] = pauseCtx->itemVtx[j + 1].v.ob[1] =
                            pauseCtx->itemVtx[j + 0].v.ob[1] + 2;

                        pauseCtx->itemVtx[j + 2].v.ob[1] = pauseCtx->itemVtx[j + 3].v.ob[1] =
                            pauseCtx->itemVtx[j + 0].v.ob[1] - 32;
                    }
                }
            }

            gSPVertex(POLY_OPA_DISP++, &pauseCtx->itemVtx[j + 0], 4, 0);
            int itemId = ExtInv_GetSlotItem(drawSlot); // Skijer's NEI
            bool not_acquired = !CHECK_AGE_REQ_SLOT(drawSlot);
            if (not_acquired) {
                gDPSetGrayscaleColor(POLY_OPA_DISP++, 109, 109, 109, 255);
                gSPGrayscale(POLY_OPA_DISP++, true);
            }
            KaleidoScope_DrawQuadTextureRGBA32(play->state.gfxCtx, ExtInv_GetItemIcon(itemId), 32, 32, 0);
            gSPGrayscale(POLY_OPA_DISP++, false);
            // (Twilight L badge removed from kaleido — the A-press
            //  selector in Clawshot_/Gale_DrawKaleidoSelector now
            //  serves as the visual mode-toggle hint. The L hint stays
            //  on the C-button HUD for in-gameplay binding feedback.)

            // Skijer's NEI — Ultrashot: while owned, the hookshot cell keeps the Longshot ICON; a
            // small Light medallion on the cell's TOP-RIGHT corner (+ the "Ultrashot" name tex) is
            // what tells it apart. Suppressed while the Twilight clawshot MODE is on (claw icon).
            if ((drawSlot == SLOT_HOOKSHOT) && (itemId == ITEM_LONGSHOT) && Nei_Save()->ultrashotOwned &&
                !TwilightUpgrade_IsClawshotActive()) {
                Vtx* cellVtx = &pauseCtx->itemVtx[j + 0];
                Vtx* mv = (Vtx*)Graph_Alloc(play->state.gfxCtx, 4 * sizeof(Vtx));
                s16 cx = (cellVtx[0].v.ob[0] + cellVtx[3].v.ob[0]) / 2;
                s16 cy = (cellVtx[0].v.ob[1] + cellVtx[3].v.ob[1]) / 2;
                s16 mSize = 14;
                s16 mx0 = cx + 18 - mSize; // marker's right edge 2px past the 32px cell's right edge
                s16 myTop = cy + 18;       // marker's top edge 2px past the cell's top edge
                s32 mvi;

                for (mvi = 0; mvi < 4; mvi++) {
                    mv[mvi] = cellVtx[0];
                }
                mv[0].v.ob[0] = mx0;
                mv[0].v.ob[1] = myTop;
                mv[0].v.tc[0] = 0;
                mv[0].v.tc[1] = 0;
                mv[1].v.ob[0] = mx0 + mSize;
                mv[1].v.ob[1] = myTop;
                mv[1].v.tc[0] = 24 << 5;
                mv[1].v.tc[1] = 0;
                mv[2].v.ob[0] = mx0;
                mv[2].v.ob[1] = myTop - mSize;
                mv[2].v.tc[0] = 0;
                mv[2].v.tc[1] = 24 << 5;
                mv[3].v.ob[0] = mx0 + mSize;
                mv[3].v.ob[1] = myTop - mSize;
                mv[3].v.tc[0] = 24 << 5;
                mv[3].v.tc[1] = 24 << 5;

                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
                gSPVertex(POLY_OPA_DISP++, mv, 4, 0);
                KaleidoScope_DrawQuadTextureRGBA32(
                    play->state.gfxCtx, (u8*)"__OTR__textures/icon_item_24_static/gQuestIconMedallionLightTex", 24, 24,
                    0);
            }
        }
    }

    if (pauseCtx->cursorSpecialPos == 0) {
        KaleidoScope_DrawCursor(play, PAUSE_ITEM);
    }

    if (sLinkSpanEquipMenuOpen) {
        LinkSpan_DrawEquipChoice(play, sLinkSpanEquipChoice);
    }

    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetCombineLERP(POLY_OPA_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, PRIMITIVE,
                      ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);

    u8 gBetterAmmoRendering = CVarGetInteger(CVAR_ENHANCEMENT("BetterAmmoRendering"), 0);

    if (ExtInv_GetCurrentPage() == 0) {
        for (i = 0; i < (gBetterAmmoRendering ? 24 : 15); i++) {
            // Skijer's NEI: the Pictograph Box on the Lens slot shows a 0/1 photo counter even though the
            // Lens isn't a vanilla ammo item. Force ITEM_LENS so DrawAmmoCount's override picks it up.
            u8 pictoOnLens = (i == SLOT_LENS) && Picto_IsOwned() && Picto_IsOnLensActive();
            s16 dispItem = pictoOnLens ? ITEM_LENS : gSaveContext.inventory.items[i];
            if (((gBetterAmmoRendering ? ItemInSlotUsesAmmo(i) : gAmmoItems[i] != ITEM_NONE) || pictoOnLens) &&
                (dispItem != ITEM_NONE)) {
                KaleidoScope_DrawAmmoCount(pauseCtx, play->state.gfxCtx, dispItem, i);
            }
        }
        // Bottomless Bottle counter: SLOT_BOTTLE_4 (slot 21) is past the default 15-slot loop above, so
        // draw its use-counter here too — the counter is what identifies a multi-use content. Skijer's NEI
        if (!gBetterAmmoRendering && ItemInSlotUsesAmmo(SLOT_BOTTLE_4) &&
            gSaveContext.inventory.items[SLOT_BOTTLE_4] != ITEM_NONE) {
            KaleidoScope_DrawAmmoCount(pauseCtx, play->state.gfxCtx, gSaveContext.inventory.items[SLOT_BOTTLE_4],
                                       SLOT_BOTTLE_4);
        }
    }

    KaleidoScope_DrawItemCycles(play);

    CLOSE_DISPS(play->state.gfxCtx);
}
