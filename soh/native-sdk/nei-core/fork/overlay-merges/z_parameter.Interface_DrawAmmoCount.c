/* overlay-merge 91d97dfbe622 ae5ad986cc11 f5aaba371ac6 */
void Interface_DrawAmmoCount(PlayState* play, s16 button, s16 alpha) {
    s16 i;
    s16 ammo;
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
    const s16 ItemIconPos_ori[8][2] = {
        { R_ITEM_AMMO_X(0) + X_Margins_BtnB, R_ITEM_AMMO_Y(0) + Y_Margins_BtnB }, // Bow on Epona?
        { R_ITEM_AMMO_X(1) + X_Margins_CL, R_ITEM_AMMO_Y(1) + Y_Margins_CL },
        { R_ITEM_AMMO_X(2) + X_Margins_CD, R_ITEM_AMMO_Y(2) + Y_Margins_CD },
        { R_ITEM_AMMO_X(3) + X_Margins_CR, R_ITEM_AMMO_Y(3) + Y_Margins_CR },
        { DPAD_UP_X + X_Margins_DPad_Items, DPAD_UP_Y + 11 + Y_Margins_DPad_Items },
        { DPAD_DOWN_X + X_Margins_DPad_Items, DPAD_DOWN_Y + 11 + Y_Margins_DPad_Items },
        { DPAD_LEFT_X + X_Margins_DPad_Items, DPAD_LEFT_Y + 11 + Y_Margins_DPad_Items },
        { DPAD_RIGHT_X + X_Margins_DPad_Items, DPAD_RIGHT_Y + 11 + Y_Margins_DPad_Items }
    };
    s16 ItemIconPos[8][2]; //(X,Y)
    s16 DPad_ItemsOffset[4][2] = {
        { 7, 3 },   // Up
        { 7, 35 },  // Down
        { -9, 19 }, // Left
        { 23, 19 }, // Right
    };              //(X,Y) Used with custom position to place it properly.
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
    s16 PosX_adjust = 1;
    s16 PosY_adjust = 17;
    if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosType"), 0) != ORIGINAL_LOCATION) {
        ItemIconPos[0][1] = CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosY"), 0) + Y_Margins_BtnB + PosY_adjust;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosType"), 0) == ANCHOR_LEFT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.UseMargins"), 0) != 0) {
                X_Margins_BtnB = Left_HUD_Margin;
            };
            ItemIconPos[0][0] = OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosX"), 0) +
                                                            X_Margins_BtnB + PosX_adjust);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosType"), 0) == ANCHOR_RIGHT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.UseMargins"), 0) != 0) {
                X_Margins_BtnB = Right_HUD_Margin;
            };
            ItemIconPos[0][0] = OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosX"), 0) +
                                                             X_Margins_BtnB + PosX_adjust);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosType"), 0) == ANCHOR_NONE) {
            ItemIconPos[0][0] = CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosX"), 0) + PosX_adjust;
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.BButton.PosType"), 0) == HIDDEN) {
            ItemIconPos[0][0] = -9999;
        }
    } else {
        ItemIconPos[0][0] = OTRGetRectDimensionFromRightEdge(ItemIconPos_ori[0][0]);
        ItemIconPos[0][1] = ItemIconPos_ori[0][1];
    }
    // C button Left
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosType"), 0) != ORIGINAL_LOCATION) {
        ItemIconPos[1][1] = CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosY"), 0) + Y_Margins_CL + PosY_adjust;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosType"), 0) == ANCHOR_LEFT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.UseMargins"), 0) != 0) {
                X_Margins_CL = Left_HUD_Margin;
            };
            ItemIconPos[1][0] = OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosX"), 0) +
                                                            X_Margins_CL + PosX_adjust);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosType"), 0) == ANCHOR_RIGHT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.UseMargins"), 0) != 0) {
                X_Margins_CL = Right_HUD_Margin;
            };
            ItemIconPos[1][0] = OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosX"), 0) +
                                                             X_Margins_CL + PosX_adjust);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosType"), 0) == ANCHOR_NONE) {
            ItemIconPos[1][0] = CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosX"), 0) + PosX_adjust;
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CLeftButton.PosType"), 0) == HIDDEN) {
            ItemIconPos[1][0] = -9999;
        }
    } else {
        ItemIconPos[1][0] = OTRGetRectDimensionFromRightEdge(ItemIconPos_ori[1][0]);
        ItemIconPos[1][1] = ItemIconPos_ori[1][1];
    }
    // C Button down
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosType"), 0) != ORIGINAL_LOCATION) {
        ItemIconPos[2][1] = CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosY"), 0) + Y_Margins_CD + PosY_adjust;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosType"), 0) == ANCHOR_LEFT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.UseMargins"), 0) != 0) {
                X_Margins_CD = Left_HUD_Margin;
            };
            ItemIconPos[2][0] = OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosX"), 0) +
                                                            X_Margins_CD + PosX_adjust);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosType"), 0) == ANCHOR_RIGHT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.UseMargins"), 0) != 0) {
                X_Margins_CD = Right_HUD_Margin;
            };
            ItemIconPos[2][0] = OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosX"), 0) +
                                                             X_Margins_CD + PosX_adjust);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosType"), 0) == ANCHOR_NONE) {
            ItemIconPos[2][0] = CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosX"), 0) + PosX_adjust;
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CDownButton.PosType"), 0) == HIDDEN) {
            ItemIconPos[2][0] = -9999;
        }
    } else {
        ItemIconPos[2][0] = OTRGetRectDimensionFromRightEdge(ItemIconPos_ori[2][0]);
        ItemIconPos[2][1] = ItemIconPos_ori[2][1];
    }
    // C button Right
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosType"), 0) != ORIGINAL_LOCATION) {
        ItemIconPos[3][1] = CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosY"), 0) + Y_Margins_CR + PosY_adjust;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosType"), 0) == ANCHOR_LEFT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.UseMargins"), 0) != 0) {
                X_Margins_CR = Left_HUD_Margin;
            };
            ItemIconPos[3][0] = OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosX"), 0) +
                                                            X_Margins_CR + PosX_adjust);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosType"), 0) == ANCHOR_RIGHT) {
            if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.UseMargins"), 0) != 0) {
                X_Margins_CR = Right_HUD_Margin;
            };
            ItemIconPos[3][0] = OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosX"), 0) +
                                                             X_Margins_CR + PosX_adjust);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosType"), 0) == ANCHOR_NONE) {
            ItemIconPos[3][0] = CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosX"), 0) + PosX_adjust;
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.CRightButton.PosType"), 0) == HIDDEN) {
            ItemIconPos[3][0] = -9999;
        }
    } else {
        ItemIconPos[3][0] = OTRGetRectDimensionFromRightEdge(ItemIconPos_ori[3][0]);
        ItemIconPos[3][1] = ItemIconPos_ori[3][1];
    }

    OPEN_DISPS(play->state.gfxCtx);

    i = gSaveContext.equips.buttonItems[button];

    // Bottomless Bottle counter (Skijer's NEI): a multi-use content on SLOT_BOTTLE_4 shows its use-
    // counter as "ammo" — this number is the ONLY thing that identifies a Bottomless Bottle. Always
    // shown while owned: 0 when empty, else the remaining uses.
    s16 bottomlessAmmo = -1;
    if ((button >= 1) && (button <= 3) && (gSaveContext.equips.cButtonSlots[button - 1] == SLOT_BOTTLE_4) &&
        Bottle_BottomlessOwned()) {
        bottomlessAmmo = Bottle_BottomlessCount();
    }

    // Pictograph Box counter (Skijer's NEI): the Lens slot in pictobox mode shows 1 with a kept photo,
    // else 0. The Lens isn't a vanilla ammo item, so force the counter on for it.
    s16 pictoAmmo = -1;
    {
        extern unsigned char Picto_IsOwned(void);
        extern unsigned char Picto_IsOnLensActive(void);
        extern unsigned char Picto_HasPhoto(void);
        if ((i == ITEM_LENS) && Picto_IsOwned() && Picto_IsOnLensActive()) {
            pictoAmmo = Picto_HasPhoto() ? 1 : 0;
        }
    }

    s16 syntheticAmmo = 0;
    s32 syntheticFull = 0;
    if (LinkSpan_GetSyntheticItemAmmo(i, &syntheticAmmo, &syntheticFull)) {
        gDPPipeSync(OVERLAY_DISP++);
        ammo = syntheticAmmo;
        if (syntheticFull) {
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 120, 255, 0, alpha);
        }
        if (ammo == 0) {
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 100, 100, 100, alpha);
        }
        for (i = 0; ammo >= 10; i++) {
            ammo -= 10;
        }
        if (i != 0) {
            OVERLAY_DISP = Gfx_TextureIA8(OVERLAY_DISP, (u8*)_gAmmoDigit0Tex[i], 8, 8, ItemIconPos[button][0],
                                          ItemIconPos[button][1], 8, 8, 1 << 10, 1 << 10);
        }
        OVERLAY_DISP = Gfx_TextureIA8(OVERLAY_DISP, (u8*)_gAmmoDigit0Tex[ammo], 8, 8, ItemIconPos[button][0] + 6,
                                      ItemIconPos[button][1], 8, 8, 1 << 10, 1 << 10);
    } else if (GameInteractor_Should(VB_DRAW_AMMO_COUNT,
                              ((i == ITEM_STICK) || (i == ITEM_NUT) || (i == ITEM_BOMB) || (i == ITEM_BOW) ||
                               ((i >= ITEM_BOW_ARROW_FIRE) && (i <= ITEM_BOW_ARROW_LIGHT)) || (i == ITEM_SLINGSHOT) ||
                               (i == ITEM_BOMBCHU) || (i == ITEM_BEAN) ||
                               (i == ITEM_SWITCH_HOOK)) || // Skijer's NEI: Switch Hook charge pool (5 shots)
                                  (bottomlessAmmo >= 0) ||
                                  (pictoAmmo >= 0),
                              &i)) {
        if ((i >= ITEM_BOW_ARROW_FIRE) && (i <= ITEM_BOW_ARROW_LIGHT)) {
            i = ITEM_BOW;
        }

        if (i == ITEM_SWITCH_HOOK) {
            // Skijer's NEI: shots left in the 5-charge pool (0 through the 2-minute gray-out).
            // ITEM_SWITCH_HOOK (0xA5) is outside the vanilla AMMO()/SLOT() tables — never AMMO(i) it.
            extern u8 SwitchHook_GetCharges(void);
            ammo = SwitchHook_GetCharges();
        } else {
            ammo = AMMO(i);
        }

        if (bottomlessAmmo >= 0) {
            ammo = bottomlessAmmo; // override with the Bottomless Bottle counter
        }

        // Power Keg counter (Skijer's NEI): the Bomb slot in keg mode carries its OWN ammo, so show the
        // keg count instead of the regular bomb count.
        {
            extern unsigned char PowerKeg_IsOwned(void);
            extern unsigned char PowerKeg_IsOnBombActive(void);
            extern unsigned char PowerKeg_GetCount(void);
            if ((i == ITEM_BOMB) && PowerKeg_IsOwned() && PowerKeg_IsOnBombActive()) {
                ammo = PowerKeg_GetCount();
            }
        }

        if (pictoAmmo >= 0) {
            ammo = pictoAmmo; // Pictograph Box photo counter (0/1)
        }

        gDPPipeSync(OVERLAY_DISP++);

        if ((button == 0) && (gSaveContext.minigameState == 1)) {
            ammo = play->interfaceCtx.hbaAmmo;
        } else if ((button == 0) && (play->shootingGalleryStatus > 1)) {
            ammo = play->shootingGalleryStatus - 1;
        } else if ((button == 0) && (play->sceneNum == SCENE_BOMBCHU_BOWLING_ALLEY) && Flags_GetSwitch(play, 0x38)) {
            ammo = play->bombchuBowlingStatus;
            if (ammo < 0) {
                ammo = 0;
            }
        } else if (((i == ITEM_BOW) && (AMMO(i) == CUR_CAPACITY(UPG_QUIVER))) ||
                   ((i == ITEM_BOMB) && (AMMO(i) == CUR_CAPACITY(UPG_BOMB_BAG))) ||
                   ((i == ITEM_SLINGSHOT) && (AMMO(i) == CUR_CAPACITY(UPG_BULLET_BAG))) ||
                   ((i == ITEM_STICK) && (AMMO(i) == CUR_CAPACITY(UPG_STICKS))) ||
                   ((i == ITEM_NUT) && (AMMO(i) == CUR_CAPACITY(UPG_NUTS))) || ((i == ITEM_BOMBCHU) && (ammo == 50)) ||
                   ((i == ITEM_BEAN) && (ammo == 15)) ||
                   // Skijer's NEI: Switch Hook at full charge (5/5) — green
                   ((i == ITEM_SWITCH_HOOK) && (ammo == 5)) || GameInteractor_Should(VB_COLOR_AMMO_GREEN, false, i)) {
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 120, 255, 0, alpha);
        }

        if (ammo == 0) {
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 100, 100, 100, alpha);
        }

        for (i = 0; ammo >= 10; i++) {
            ammo -= 10;
        }

        if (i != 0) {
            OVERLAY_DISP = Gfx_TextureIA8(OVERLAY_DISP, (u8*)_gAmmoDigit0Tex[i], 8, 8, ItemIconPos[button][0],
                                          ItemIconPos[button][1], 8, 8, 1 << 10, 1 << 10);
        }

        OVERLAY_DISP = Gfx_TextureIA8(OVERLAY_DISP, (u8*)_gAmmoDigit0Tex[ammo], 8, 8, ItemIconPos[button][0] + 6,
                                      ItemIconPos[button][1], 8, 8, 1 << 10, 1 << 10);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}