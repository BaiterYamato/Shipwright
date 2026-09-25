/* overlay-merge 1e170d43b1af d3c1527159ad ab331f1f5e30 */
void Interface_DrawItemIconTexture(PlayState* play, void* texture, s16 button) {
    OPEN_DISPS(play->state.gfxCtx);
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    InterfaceContext* interfaceCtx = &play->interfaceCtx;
    s16 X_Margins_CL;
    s16 X_Margins_CR;
    s16 X_Margins_CD;
    s16 Y_Margins_CL;
    s16 Y_Margins_CR;
    s16 Y_Margins_CD;
    s16 X_Margins_BtnB;
    s16 Y_Margins_BtnB;
    s16 X_Margins_DPad_Items;
    s16 Y_Margins_DPad_Items;
    if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.UseMargins"), 0) != 0) {
        if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosType"), 0) == ORIGINAL_LOCATION) {
            X_Margins_BtnB = Right_HUD_Margin;
        };
        Y_Margins_BtnB = (Top_HUD_Margin * -1);
    } else {
        X_Margins_BtnB = 0;
        Y_Margins_BtnB = 0;
    }
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.UseMargins"), 0) != 0) {
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosType"), 0) == ORIGINAL_LOCATION) {
            X_Margins_CL = Right_HUD_Margin;
        };
        Y_Margins_CL = (Top_HUD_Margin * -1);
    } else {
        X_Margins_CL = 0;
        Y_Margins_CL = 0;
    }
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.UseMargins"), 0) != 0) {
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosType"), 0) == ORIGINAL_LOCATION) {
            X_Margins_CR = Right_HUD_Margin;
        };
        Y_Margins_CR = (Top_HUD_Margin * -1);
    } else {
        X_Margins_CR = 0;
        Y_Margins_CR = 0;
    }
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.UseMargins"), 0) != 0) {
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosType"), 0) == ORIGINAL_LOCATION) {
            X_Margins_CD = Right_HUD_Margin;
        };
        Y_Margins_CD = (Top_HUD_Margin * -1);
    } else {
        X_Margins_CD = 0;
        Y_Margins_CD = 0;
    }
    if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.UseMargins"), 0) != 0) {
        if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0) == ORIGINAL_LOCATION) {
            X_Margins_DPad_Items = Right_HUD_Margin;
        };
        Y_Margins_DPad_Items = (Top_HUD_Margin * -1);
    } else {
        X_Margins_DPad_Items = 0;
        Y_Margins_DPad_Items = 0;
    }
    const s16 ItemIconPos_ori[8][2] = { { B_BUTTON_X + X_Margins_BtnB, B_BUTTON_Y + Y_Margins_BtnB },
                                        { C_LEFT_BUTTON_X + X_Margins_CL, C_LEFT_BUTTON_Y + Y_Margins_CL },
                                        { C_DOWN_BUTTON_X + X_Margins_CD, C_DOWN_BUTTON_Y + Y_Margins_CD },
                                        { C_RIGHT_BUTTON_X + X_Margins_CR, C_RIGHT_BUTTON_Y + Y_Margins_CR },
                                        { DPAD_UP_X + X_Margins_DPad_Items, DPAD_UP_Y + Y_Margins_DPad_Items },
                                        { DPAD_DOWN_X + X_Margins_DPad_Items, DPAD_DOWN_Y + Y_Margins_DPad_Items },
                                        { DPAD_LEFT_X + X_Margins_DPad_Items, DPAD_LEFT_Y + Y_Margins_DPad_Items },
                                        { DPAD_RIGHT_X + X_Margins_DPad_Items, DPAD_RIGHT_Y + Y_Margins_DPad_Items } };
    u16 ItemsSlotsAlpha[8] = { interfaceCtx->bAlpha,        interfaceCtx->cLeftAlpha,    interfaceCtx->cRightAlpha,
                               interfaceCtx->cDownAlpha,    interfaceCtx->dpadUpAlpha,   interfaceCtx->dpadDownAlpha,
                               interfaceCtx->dpadLeftAlpha, interfaceCtx->dpadRightAlpha };
    s16 DPad_ItemsOffset[4][2] = {
        { 7, -8 },         // Up
        { 7, 24 },         // Down
        { -9, 8 },         // Left
        { 23, 8 },         // Right
    };                     //(X,Y) Used with custom position to place it properly.
    s16 ItemIconPos[8][2]; //(X,Y)
    // DPadItems
    if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0) != ORIGINAL_LOCATION) {
        ItemIconPos[4][1] =
            CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosY"), 0) + Y_Margins_DPad_Items + DPad_ItemsOffset[0][1]; // Up
        ItemIconPos[5][1] =
            CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosY"), 0) + Y_Margins_DPad_Items + DPad_ItemsOffset[1][1]; // Down
        ItemIconPos[6][1] =
            CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosY"), 0) + Y_Margins_DPad_Items + DPad_ItemsOffset[2][1]; // Left
        ItemIconPos[7][1] =
            CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosY"), 0) + Y_Margins_DPad_Items + DPad_ItemsOffset[3][1]; // Right
        if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0) == ANCHOR_LEFT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.UseMargins"), 0) != 0) {
                X_Margins_DPad_Items = Left_HUD_Margin;
            };
            ItemIconPos[4][0] = OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) +
                                                            X_Margins_DPad_Items + DPad_ItemsOffset[0][0]);
            ItemIconPos[5][0] = OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) +
                                                            X_Margins_DPad_Items + DPad_ItemsOffset[1][0]);
            ItemIconPos[6][0] = OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) +
                                                            X_Margins_DPad_Items + DPad_ItemsOffset[2][0]);
            ItemIconPos[7][0] = OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) +
                                                            X_Margins_DPad_Items + DPad_ItemsOffset[3][0]);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0) == ANCHOR_RIGHT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.UseMargins"), 0) != 0) {
                X_Margins_DPad_Items = Right_HUD_Margin;
            };
            ItemIconPos[4][0] = OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) +
                                                             X_Margins_DPad_Items + DPad_ItemsOffset[0][0]);
            ItemIconPos[5][0] = OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) +
                                                             X_Margins_DPad_Items + DPad_ItemsOffset[1][0]);
            ItemIconPos[6][0] = OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) +
                                                             X_Margins_DPad_Items + DPad_ItemsOffset[2][0]);
            ItemIconPos[7][0] = OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) +
                                                             X_Margins_DPad_Items + DPad_ItemsOffset[3][0]);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0) == ANCHOR_NONE) {
            ItemIconPos[4][0] = CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) + DPad_ItemsOffset[0][0];
            ItemIconPos[5][0] = CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) + DPad_ItemsOffset[1][0];
            ItemIconPos[6][0] = CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) + DPad_ItemsOffset[2][0];
            ItemIconPos[7][0] = CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 0) + DPad_ItemsOffset[3][0];
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 0) == HIDDEN) {
            ItemIconPos[4][0] = -9999;
            ItemIconPos[5][0] = -9999;
            ItemIconPos[6][0] = -9999;
            ItemIconPos[7][0] = -9999;
        }
    } else {
        ItemIconPos[4][0] = OTRGetDimensionFromRightEdge(ItemIconPos_ori[4][0]);
        ItemIconPos[5][0] = OTRGetDimensionFromRightEdge(ItemIconPos_ori[5][0]);
        ItemIconPos[6][0] = OTRGetDimensionFromRightEdge(ItemIconPos_ori[6][0]);
        ItemIconPos[7][0] = OTRGetDimensionFromRightEdge(ItemIconPos_ori[7][0]);
        ItemIconPos[4][1] = ItemIconPos_ori[4][1];
        ItemIconPos[5][1] = ItemIconPos_ori[5][1];
        ItemIconPos[6][1] = ItemIconPos_ori[6][1];
        ItemIconPos[7][1] = ItemIconPos_ori[7][1];
    }
    // B Button
    if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosType"), 0) != ORIGINAL_LOCATION) {
        ItemIconPos[0][1] = CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosY"), 0) + Y_Margins_BtnB;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosType"), 0) == ANCHOR_LEFT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.UseMargins"), 0) != 0) {
                X_Margins_BtnB = Left_HUD_Margin;
            };
            ItemIconPos[0][0] =
                OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosX"), 0) + X_Margins_BtnB);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosType"), 0) == ANCHOR_RIGHT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.UseMargins"), 0) != 0) {
                X_Margins_BtnB = Right_HUD_Margin;
            };
            ItemIconPos[0][0] =
                OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosX"), 0) + X_Margins_BtnB);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosType"), 0) == ANCHOR_NONE) {
            ItemIconPos[0][0] = CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosX"), 0);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosType"), 0) == HIDDEN) {
            ItemIconPos[0][0] = -9999;
        }
    } else {
        ItemIconPos[0][0] = OTRGetRectDimensionFromRightEdge(ItemIconPos_ori[0][0]);
        ItemIconPos[0][1] = ItemIconPos_ori[0][1];
    }
    // C button Left
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosType"), 0) != ORIGINAL_LOCATION) {
        ItemIconPos[1][1] = CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosY"), 0) + Y_Margins_CL;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosType"), 0) == ANCHOR_LEFT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.UseMargins"), 0) != 0) {
                X_Margins_CL = Left_HUD_Margin;
            };
            ItemIconPos[1][0] =
                OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosX"), 0) + X_Margins_CL);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosType"), 0) == ANCHOR_RIGHT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.UseMargins"), 0) != 0) {
                X_Margins_CL = Right_HUD_Margin;
            };
            ItemIconPos[1][0] =
                OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosX"), 0) + X_Margins_CL);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosType"), 0) == ANCHOR_NONE) {
            ItemIconPos[1][0] = CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosX"), 0);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosType"), 0) == HIDDEN) {
            ItemIconPos[1][0] = -9999;
        }
    } else {
        ItemIconPos[1][0] = OTRGetRectDimensionFromRightEdge(ItemIconPos_ori[1][0]);
        ItemIconPos[1][1] = ItemIconPos_ori[1][1];
    }
    // C Button down
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosType"), 0) != ORIGINAL_LOCATION) {
        ItemIconPos[2][1] = CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosY"), 0) + Y_Margins_CD;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosType"), 0) == ANCHOR_LEFT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.UseMargins"), 0) != 0) {
                X_Margins_CD = Left_HUD_Margin;
            };
            ItemIconPos[2][0] =
                OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosX"), 0) + X_Margins_CD);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosType"), 0) == ANCHOR_RIGHT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.UseMargins"), 0) != 0) {
                X_Margins_CD = Right_HUD_Margin;
            };
            ItemIconPos[2][0] =
                OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosX"), 0) + X_Margins_CD);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosType"), 0) == ANCHOR_NONE) {
            ItemIconPos[2][0] = CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosX"), 0);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosType"), 0) == HIDDEN) {
            ItemIconPos[2][0] = -9999;
        }
    } else {
        ItemIconPos[2][0] = OTRGetRectDimensionFromRightEdge(ItemIconPos_ori[2][0]);
        ItemIconPos[2][1] = ItemIconPos_ori[2][1];
    }
    // C button Right
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosType"), 0) != ORIGINAL_LOCATION) {
        ItemIconPos[3][1] = CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosY"), 0) + Y_Margins_CR;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosType"), 0) == ANCHOR_LEFT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.UseMargins"), 0) != 0) {
                X_Margins_CR = Left_HUD_Margin;
            };
            ItemIconPos[3][0] =
                OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosX"), 0) + X_Margins_CR);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosType"), 0) == ANCHOR_RIGHT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.UseMargins"), 0) != 0) {
                X_Margins_CR = Right_HUD_Margin;
            };
            ItemIconPos[3][0] =
                OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosX"), 0) + X_Margins_CR);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosType"), 0) == ANCHOR_NONE) {
            ItemIconPos[3][0] = CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosX"), 0);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosType"), 0) == HIDDEN) {
            ItemIconPos[3][0] = -9999;
        }
    } else {
        ItemIconPos[3][0] = OTRGetRectDimensionFromRightEdge(ItemIconPos_ori[3][0]);
        ItemIconPos[3][1] = ItemIconPos_ori[3][1];
    }

    // Link-Span (OOT-MOVE-006): posição final dos ícones do D-pad para providers nativos. Sem
    // textura, o provider desenha o ícone e aqui só a posição é capturada.
    if (button >= 4) {
        LinkSpan_CaptureItemButton(play, button, ItemIconPos[button][0], ItemIconPos[button][1],
                                   gItemIconWidth[button], ItemsSlotsAlpha[button]);
    }
    // CLOSE_DISPS fecha a chave aberta por OPEN_DISPS: sem retorno antecipado.
    if (texture != NULL) {
        // Quest items (songs, medallions, stones, etc.) use 24x24 textures from icon_item_24_static.
        // Regular items use 32x32 from icon_item_static. Detect and load with correct dimensions.
        u8 btnItem = gSaveContext.equips.buttonItems[button];
        // Extended-button infra: resolve the u8 marker (ITEM_EXT_BUTTON) to its real u16 id for the
        // icon-size test. Everything below stays on the raw u8 btnItem — the marker never falls in any of
        // those ranges, and the real u16 ids are outside the u8 space entirely.
        u16 effBtnItem = ExtButton_GetItem(button);
        s32 isSmallIcon = (effBtnItem >= ITEM_SONG_MINUET && effBtnItem <= ITEM_SKULL_TOKEN);

        // Composite icon for elemental weapon mode: medallion at half alpha behind, the weapon on top.
        // Skijer's NEI — the button holds a PLAIN bow/slingshot now and the element is a flag, so the
        // trigger is the flag, not the item id, and `texture` is already the weapon (it used to be the
        // medallion, hence the layer order below is the reverse of what it once was).
        u8 sw97IsSling = Sw97_IsSlingItem(btnItem);
        u8 sw97Elem = (Sw97_IsBowItem(btnItem) || sw97IsSling) ? Sw97_EffectiveElement(sw97IsSling) : SW97_ELEM_NONE;
        // Bomb Arrows is drawn as a corner badge instead (its icon is a full 32x32 item, not an
        // underlay), so it is excluded here and handled next to the Ultrashot marker below.
        s32 isElementalWeapon = (sw97Elem >= SW97_ELEM_FIRE) && (sw97Elem <= SW97_ELEM_WIND);

        // Skijer's NEI: Switch Hook grayed out while its charge pool recovers (spent the 5th shot).
        s16 shGrayAlpha = -1;
        {
            extern u8 SwitchHook_IsDepleted(void);
            if ((btnItem == ITEM_SWITCH_HOOK) && SwitchHook_IsDepleted()) {
                switch (button) {
                    case 1:
                        shGrayAlpha = interfaceCtx->cLeftAlpha;
                        break;
                    case 2:
                        shGrayAlpha = interfaceCtx->cDownAlpha;
                        break;
                    case 3:
                        shGrayAlpha = interfaceCtx->cRightAlpha;
                        break;
                    case 4:
                        shGrayAlpha = interfaceCtx->dpadUpAlpha;
                        break;
                    case 5:
                        shGrayAlpha = interfaceCtx->dpadDownAlpha;
                        break;
                    case 6:
                        shGrayAlpha = interfaceCtx->dpadLeftAlpha;
                        break;
                    case 7:
                        shGrayAlpha = interfaceCtx->dpadRightAlpha;
                        break;
                    default:
                        shGrayAlpha = interfaceCtx->bAlpha;
                        break;
                }
                // Switch Hook depleted: the icon itself draws gray for the 2-minute recovery.
                gDPPipeSync(OVERLAY_DISP++);
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 100, 100, 100, shGrayAlpha);
            }
        }

        if (isElementalWeapon) {
            s16 iconW = gItemIconWidth[button];
            s32 x0 = ItemIconPos[button][0];
            s32 y0 = ItemIconPos[button][1];

            // Layer 1: the element's medallion at 50% alpha, filling the cell (24x24 source).
            void* medTex = ExtInv_GetItemIcon(Sw97_ElementIcon(sw97Elem));
            gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, 128);
            gDPLoadTextureBlock(OVERLAY_DISP++, medTex, G_IM_FMT_RGBA, G_IM_SIZ_32b, 24, 24, 0, G_TX_NOMIRROR | G_TX_WRAP,
                                G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            s32 dd24 = 24 * 1024 / iconW;
            gSPWideTextureRectangle(OVERLAY_DISP++, x0 << 2, y0 << 2, (x0 + iconW) << 2, (y0 + iconW) << 2, G_TX_RENDERTILE,
                                    0, 0, dd24, dd24);

            // Layer 2: the weapon itself at full alpha, centered ~75% size. `texture` is whatever the
            // button's item resolved to, so the bow/slingshot split is already decided for us.
            s16 smallW = (iconW * 3) / 4;
            s16 offset = (iconW - smallW) / 2;
            s32 dd32 = 32 * 1024 / smallW;
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, 255);
            gDPLoadTextureBlock(OVERLAY_DISP++, texture, G_IM_FMT_RGBA, G_IM_SIZ_32b, 32, 32, 0, G_TX_NOMIRROR | G_TX_WRAP,
                                G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            gSPWideTextureRectangle(OVERLAY_DISP++, (x0 + offset) << 2, (y0 + offset) << 2, (x0 + offset + smallW) << 2,
                                    (y0 + offset + smallW) << 2, G_TX_RENDERTILE, 0, 0, dd32, dd32);

            // Restore combine mode for subsequent draws
            gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, 255);
        } else if (isSmallIcon) {
            gDPLoadTextureBlock(OVERLAY_DISP++, texture, G_IM_FMT_RGBA, G_IM_SIZ_32b, 24, 24, 0, G_TX_NOMIRROR | G_TX_WRAP,
                                G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            s32 dd = 24 * 1024 / gItemIconWidth[button];
            gSPWideTextureRectangle(OVERLAY_DISP++, ItemIconPos[button][0] << 2, ItemIconPos[button][1] << 2,
                                    (ItemIconPos[button][0] + gItemIconWidth[button]) << 2,
                                    (ItemIconPos[button][1] + gItemIconWidth[button]) << 2, G_TX_RENDERTILE, 0, 0, dd, dd);
        } else {
            gDPLoadTextureBlock(OVERLAY_DISP++, texture, G_IM_FMT_RGBA, G_IM_SIZ_32b, 32, 32, 0, G_TX_NOMIRROR | G_TX_WRAP,
                                G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        gSPWideTextureRectangle(OVERLAY_DISP++, ItemIconPos[button][0] << 2, ItemIconPos[button][1] << 2,
                                (ItemIconPos[button][0] + gItemIconWidth[button]) << 2,
                                (ItemIconPos[button][1] + gItemIconWidth[button]) << 2, G_TX_RENDERTILE, 0, 0,
                                gItemIconDD[button] << 1, gItemIconDD[button] << 1);
    }

        // Skijer's NEI: restore the caller's expected prim state for whatever draws next (ammo digits,
        // overlays, etc.) after the Switch Hook gray draw above.
        if (shGrayAlpha >= 0) {
            gDPPipeSync(OVERLAY_DISP++);
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, shGrayAlpha);
        }

        // Twilight L-badge — small `L` icon in the bottom-right corner of the
        // C-button (or D-pad) icon, shown when the equipped item is hookshot /
        // longshot / boomerang AND the matching Twilight Upgrade bit is owned.
        // Hints that pressing L on the controller activates the upgraded
        // behaviour (clawshot pull-toggle, gale multi-target). Skipped on B
        // button since those items live on C-slots.
        {
            extern u8 TwilightUpgrade_HasClawshot(void);
            extern u8 TwilightUpgrade_HasGaleBoomerang(void);
            u8 wantLBadge = 0;
            if ((btnItem == ITEM_HOOKSHOT || btnItem == ITEM_LONGSHOT) && TwilightUpgrade_HasClawshot()) {
                wantLBadge = 1;
            } else if (btnItem == ITEM_BOOMERANG && TwilightUpgrade_HasGaleBoomerang()) {
                wantLBadge = 1;
            }
            if (wantLBadge && button >= 1 && button <= 7) {
                // Same footprint and rendering pattern as the gust jar medallion
                // overlay below (lines 4836-4864): 12 wide × 12 tall in the
                // TOP-RIGHT corner of the icon. Source texture is gLButtonTex
                // (24×32 ia8), drawn at 12×12 dest. dd matches gust jar's 2048
                // for horizontal (24/12 × 1024); vertical stretch is acceptable
                // since the L glyph is roughly square inside its texture.
                s16 iconW = gItemIconWidth[button];
                s16 overlayW = 12;
                s32 x0 = ItemIconPos[button][0] + (iconW - overlayW);
                s32 y0 = ItemIconPos[button][1];
                s32 ddOverlay = 24 * 1024 / overlayW; // 2048, matches gust jar overlay
                gDPPipeSync(OVERLAY_DISP++);
                gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, 255);
                gDPLoadTextureBlock(OVERLAY_DISP++, gLButtonTex, G_IM_FMT_IA, G_IM_SIZ_8b, 24, 32, 0,
                                    G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                    G_TX_NOLOD, G_TX_NOLOD);
                gSPWideTextureRectangle(OVERLAY_DISP++, x0 << 2, y0 << 2, (x0 + overlayW) << 2, (y0 + overlayW) << 2,
                                        G_TX_RENDERTILE, 0, 0, ddOverlay, ddOverlay);
            }
        }

        // Gust Jar element overlay — small medallion in the top-right corner of the
        // C-button icon, shown when the player has selected a non-WIND element
        // (cycled via R/L in first-person or L+R during suck). Lets the player see
        // which element is primed without opening the kaleido. Skipped on B-button
        // (button 0) since Gust Jar lives on a C-slot, not B. Skijer's NEI: extended to the D-pad
        // (buttons 4-7) — a badge that only shows on C-buttons made a D-pad Gust Jar look elementless.
        if (btnItem == ITEM_GUST_JAR && button >= 1 && button <= 7) {
            extern s32 GustJar_GetActiveMedallionItem(void);
            extern void* ExtInv_GetItemIcon(uint16_t itemId);
            s32 medallionItem = GustJar_GetActiveMedallionItem();
            if (medallionItem >= 0) {
                void* medTex = ExtInv_GetItemIcon((u16)medallionItem);
                if (medTex != NULL) {
                    // Place the 12x12 overlay in the top-right corner of the icon.
                    s16 iconW = gItemIconWidth[button];
                    s16 overlayW = 12;
                    s32 x0 = ItemIconPos[button][0] + (iconW - overlayW);
                    s32 y0 = ItemIconPos[button][1];
                    s32 ddOverlay = 24 * 1024 / overlayW; // medallion icons are 24x24
                    gDPPipeSync(OVERLAY_DISP++);
                    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
                    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, 255);
                    gDPLoadTextureBlock(OVERLAY_DISP++, medTex, G_IM_FMT_RGBA, G_IM_SIZ_32b, 24, 24, 0,
                                        G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                        G_TX_NOLOD, G_TX_NOLOD);
                    gSPWideTextureRectangle(OVERLAY_DISP++, x0 << 2, y0 << 2, (x0 + overlayW) << 2, (y0 + overlayW) << 2,
                                            G_TX_RENDERTILE, 0, 0, ddOverlay, ddOverlay);
                }
            }
        }

        // Elemental Wand: the same corner badge, for the same reason. All six rods share one staff
        // icon, so without the medallion the HUD cannot say which one is on the button. Skijer's NEI
        if (btnItem == ITEM_ELEMENTAL_WAND && button >= 1 && button <= 7) {
            extern uint16_t Wand_ModeMedallion(uint8_t mode);
            extern uint8_t Wand_GetMode(void);
            extern void* ExtInv_GetItemIcon(uint16_t itemId);
            void* medTex = ExtInv_GetItemIcon(Wand_ModeMedallion(Wand_GetMode()));

            if (medTex != NULL) {
                s16 iconW = gItemIconWidth[button];
                s16 overlayW = 12;
                s32 x0 = ItemIconPos[button][0] + (iconW - overlayW);
                s32 y0 = ItemIconPos[button][1];
                s32 ddOverlay = 24 * 1024 / overlayW; // medallion icons are 24x24

                gDPPipeSync(OVERLAY_DISP++);
                gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, 255);
                gDPLoadTextureBlock(OVERLAY_DISP++, medTex, G_IM_FMT_RGBA, G_IM_SIZ_32b, 24, 24, 0,
                                    G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                    G_TX_NOLOD, G_TX_NOLOD);
                gSPWideTextureRectangle(OVERLAY_DISP++, x0 << 2, y0 << 2, (x0 + overlayW) << 2, (y0 + overlayW) << 2,
                                        G_TX_RENDERTILE, 0, 0, ddOverlay, ddOverlay);
            }
        }

        // Skijer's NEI — Ultrashot: Light-medallion marker on the equipped button's TOP-RIGHT corner
        // (the Ultrashot keeps the Longshot ICON; the medallion is what tells it apart). 24x24 quest
        // icon drawn at 12x12 over the icon's corner, at the button's own alpha. Suppressed while the
        // Twilight clawshot MODE is toggled on (the icon shows the claw then, not the Longshot).
        extern u8 TwilightUpgrade_IsClawshotActive(void);
        if ((btnItem == ITEM_LONGSHOT) && Nei_Save()->ultrashotOwned && !TwilightUpgrade_IsClawshotActive()) {
            s16 markSize = 12;
            s16 markLeft = ItemIconPos[button][0] + gItemIconWidth[button] - markSize + 2;
            s16 markTop = ItemIconPos[button][1] - 2;
            s16 markAlpha;
            s32 ddMark = 24 * 1024 / markSize; // 24x24 source drawn 12x12

            switch (button) {
                case 1:
                    markAlpha = interfaceCtx->cLeftAlpha;
                    break;
                case 2:
                    markAlpha = interfaceCtx->cDownAlpha;
                    break;
                case 3:
                    markAlpha = interfaceCtx->cRightAlpha;
                    break;
                case 4:
                    markAlpha = interfaceCtx->dpadUpAlpha;
                    break;
                case 5:
                    markAlpha = interfaceCtx->dpadDownAlpha;
                    break;
                case 6:
                    markAlpha = interfaceCtx->dpadLeftAlpha;
                    break;
                case 7:
                    markAlpha = interfaceCtx->dpadRightAlpha;
                    break;
                default:
                    markAlpha = interfaceCtx->bAlpha;
                    break;
            }

            gDPPipeSync(OVERLAY_DISP++);
            gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, markAlpha);
            gDPLoadTextureBlock(OVERLAY_DISP++, (u8*)"__OTR__textures/icon_item_24_static/gQuestIconMedallionLightTex",
                                G_IM_FMT_RGBA, G_IM_SIZ_32b, 24, 24, 0, G_TX_NOMIRROR | G_TX_WRAP,
                                G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            gSPWideTextureRectangle(OVERLAY_DISP++, markLeft << 2, markTop << 2, (markLeft + markSize) << 2,
                                    (markTop + markSize) << 2, G_TX_RENDERTILE, 0, 0, ddMark, ddMark);
        }

        // Skijer's NEI — Bomb Arrows: same top-right corner marker as the Ultrashot above. Bomb Arrows
        // is the 7th value of the bow's element flag and owns no inventory cell, so the button shows a
        // plain bow; this badge is the only thing that tells the two apart. It is a badge rather than
        // the half-alpha underlay the medallions use because its icon is a full 32x32 item, which reads
        // as a second item behind the bow instead of as a tint.
        if (sw97Elem == SW97_ELEM_BOMB) {
            void* bombTex = ExtInv_GetItemIcon(ITEM_BOMB_ARROWS);
            if (bombTex != NULL) {
                s16 markSize = 12;
                s16 markLeft = ItemIconPos[button][0] + gItemIconWidth[button] - markSize + 2;
                s16 markTop = ItemIconPos[button][1] - 2;
                s32 ddMark = 32 * 1024 / markSize; // 32x32 source (NOT 24x24 like the medallions)
                s16 markAlpha;

                switch (button) {
                    case 1:
                        markAlpha = interfaceCtx->cLeftAlpha;
                        break;
                    case 2:
                        markAlpha = interfaceCtx->cDownAlpha;
                        break;
                    case 3:
                        markAlpha = interfaceCtx->cRightAlpha;
                        break;
                    case 4:
                        markAlpha = interfaceCtx->dpadUpAlpha;
                        break;
                    case 5:
                        markAlpha = interfaceCtx->dpadDownAlpha;
                        break;
                    case 6:
                        markAlpha = interfaceCtx->dpadLeftAlpha;
                        break;
                    case 7:
                        markAlpha = interfaceCtx->dpadRightAlpha;
                        break;
                    default:
                        markAlpha = interfaceCtx->bAlpha;
                        break;
                }

                gDPPipeSync(OVERLAY_DISP++);
                gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, markAlpha);
                gDPLoadTextureBlock(OVERLAY_DISP++, bombTex, G_IM_FMT_RGBA, G_IM_SIZ_32b, 32, 32, 0,
                                    G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                    G_TX_NOLOD, G_TX_NOLOD);
                gSPWideTextureRectangle(OVERLAY_DISP++, markLeft << 2, markTop << 2, (markLeft + markSize) << 2,
                                        (markTop + markSize) << 2, G_TX_RENDERTILE, 0, 0, ddMark, ddMark);
            }
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}