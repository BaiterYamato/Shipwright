/* overlay-merge 406d282dcc20 ffcd6ba7d43c 86a311bf715d */
void Player_DrawImpl(PlayState* play, void** skeleton, Vec3s* jointTable, s32 dListCount, s32 lod, s32 tunic, s32 boots,
                     s32 face, OverrideLimbDrawOpa overrideLimbDraw, PostLimbDrawOpa postLimbDraw, void* data) {
    Color_RGB8* color;
    s32 eyeIndex = (jointTable[22].x & 0xF) - 1;
    s32 mouthIndex = (jointTable[22].x >> 4) - 1;

    OPEN_DISPS(play->state.gfxCtx);

    if (eyeIndex < 0) {
        eyeIndex = sEyeMouthIndexes[face][0];
    }

    if (eyeIndex > 7)
        eyeIndex = 7;

#if defined(MODDING) || defined(_MSC_VER) || defined(__GNUC__)
    {
        void* pakEye = PakLoader_GetEyeTexture(eyeIndex);
        // A form's face textures ship under the vanilla eye symbol, so the same
        // name-based rule applies. Without this the head renders Link's eyes
        // through the form's own palette — the garbled face bug.
        void* formEye = pakEye ? NULL : CustomForms_ResolveVanillaTexture(sEyeTextures[gSaveContext.linkAge][eyeIndex]);
        if (pakEye) {
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)pakEye);
        } else if (formEye) {
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)formEye);
        } else {
            gSPSegment(POLY_OPA_DISP++, 0x08, SEGMENTED_TO_VIRTUAL(sEyeTextures[gSaveContext.linkAge][eyeIndex]));
        }
    }
#else
    gSPSegment(POLY_OPA_DISP++, 0x08, SEGMENTED_TO_VIRTUAL(sEyeTextures[eyeIndex]));
#endif
    if (mouthIndex < 0) {
        mouthIndex = sEyeMouthIndexes[face][1];
    }

    if (mouthIndex > 3)
        mouthIndex = 3;

#if defined(MODDING) || defined(_MSC_VER) || defined(__GNUC__)
    {
        void* pakMouth = PakLoader_GetMouthTexture(mouthIndex);
        void* formMouth =
            pakMouth ? NULL : CustomForms_ResolveVanillaTexture(sMouthTextures[gSaveContext.linkAge][mouthIndex]);
        if (pakMouth) {
            gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)pakMouth);
        } else if (formMouth) {
            gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)formMouth);
        } else {
            gSPSegment(POLY_OPA_DISP++, 0x09, SEGMENTED_TO_VIRTUAL(sMouthTextures[gSaveContext.linkAge][mouthIndex]));
        }
    }
#else
    gSPSegment(POLY_OPA_DISP++, 0x09, SEGMENTED_TO_VIRTUAL(sMouthTextures[eyeIndex]));
#endif

    Color_RGB8 sTemp;
    color = &sTunicColors[tunic];
    if (tunic == PLAYER_TUNIC_KOKIRI && CVarGetInteger(CVAR_COSMETIC("Link.KokiriTunic.Changed"), 0)) {
        sTemp = CVarGetColor24(CVAR_COSMETIC("Link.KokiriTunic.Value"), sTunicColors[PLAYER_TUNIC_KOKIRI]);
        color = &sTemp;
    } else if (tunic == PLAYER_TUNIC_GORON && CVarGetInteger(CVAR_COSMETIC("Link.GoronTunic.Changed"), 0)) {
        sTemp = CVarGetColor24(CVAR_COSMETIC("Link.GoronTunic.Value"), sTunicColors[PLAYER_TUNIC_GORON]);
        color = &sTemp;
    } else if (tunic == PLAYER_TUNIC_ZORA && CVarGetInteger(CVAR_COSMETIC("Link.ZoraTunic.Changed"), 0)) {
        sTemp = CVarGetColor24(CVAR_COSMETIC("Link.ZoraTunic.Value"), sTunicColors[PLAYER_TUNIC_ZORA]);
        color = &sTemp;
    }

    // Extended RECOLOR tunics (Skijer 2026-07-16): each ext tunic paints Link's tunic 1:1 like the
    // vanilla Goron/Zora recolor (ext tunics equip with Kokiri base, so `tunic`==0 and this wins).
    if (ExtEquip_IsChampionTunic()) {
        // Champion's Tunic — blue #38b6f1
        sTemp.r = 56;
        sTemp.g = 182;
        sTemp.b = 241;
        color = &sTemp;
    } else if (ExtEquip_IsSpiritTunic()) {
        // Spirit Tunic — ORANGE when active (has rupees), BLACK when deactivated (broke).
        if (ExtEquip_SpiritHasMoney()) {
            sTemp.r = 235;
            sTemp.g = 110;
            sTemp.b = 20;
        } else {
            sTemp.r = 20;
            sTemp.g = 20;
            sTemp.b = 20;
        }
        color = &sTemp;
    } else if (ExtEquip_IsSagesTunic()) {
        // Sage's Tunic — white, briefly dyed with a medallion's color while its
        // resistance is absorbing damage (ExtEquip_SagesFlash).
        ExtEquip_GetSagesTunicColor(&sTemp.r, &sTemp.g, &sTemp.b);
        color = &sTemp;
    }

    // Trident (ext sword 3): the opening frames of the max-charge release make Link
    // untouchable, and the tunic goes gold so that is something you can SEE. Last in
    // the chain on purpose — it outranks every tunic, ext ones included, because it
    // is a state and not a garment. Skijer's NEI
    if (Trident_GoldenArmor()) {
        sTemp.r = 255;
        sTemp.g = 205;
        sTemp.b = 40;
        color = &sTemp;
    }

    // Skijer's NEI: remember the body tunic env so the held-sword compound DL (WeaponUpgrade_
    // ApplyHeldSwordDL) can re-apply it after a combined MM sword DL that sets its own env color.
    sPlayerBodyEnvColor = *color;

    if (GameInteractor_Should(VB_APPLY_TUNIC_COLOR, true, data, color)) {
        gDPSetEnvColor(POLY_OPA_DISP++, color->r, color->g, color->b, 0);
    }

    // If we have a custom link model, always use the most detailed LOD
    if (Player_IsCustomLinkModel()) {
        lod = 0;
    }

    sDListsLodOffset = lod * 2;

    if (GameInteractor_Should(VB_PLAYER_DRAW, true, play, data)) {
        // R059/R060: delimita o draw gameplay normal; inventário/primeira pessoa/outros caminhos ficam fora.
        HatPhysics_BeginDraw(play, data,
                             overrideLimbDraw == Player_OverrideLimbDrawGameplayDefault &&
                                 postLimbDraw == Player_PostLimbDrawGameplay);
        SkelAnime_DrawFlexLod(play, skeleton, jointTable, dListCount, overrideLimbDraw, postLimbDraw, data, lod);
        HatPhysics_EndDraw();
    }

    if (!GameInteractor_InvisibleLinkActive() &&
        ((CVarGetInteger(CVAR_ENHANCEMENT("FirstPersonGauntlets"), 0) && LINK_IS_ADULT) ||
         (overrideLimbDraw != Player_OverrideLimbDrawGameplayFirstPerson)) &&
        (overrideLimbDraw != Player_OverrideLimbDrawGameplayCrawling) &&
        (gSaveContext.gameMode != GAMEMODE_END_CREDITS)) {
        if (LINK_IS_ADULT) {
            s32 strengthUpgrade = CUR_UPG_VALUE(UPG_STRENGTH);

            // Mogma Mitts: force white gauntlets visible even without strength upgrade
            if (gMogmaMittsForceGauntlets || strengthUpgrade >= 2) {
                gDPPipeSync(POLY_OPA_DISP++);

                // Mogma Mitts always uses white (silver) gauntlets
                if (gMogmaMittsForceGauntlets) {
                    color = &sGauntletColors[0]; // White/silver color
                } else {
                    color = &sGauntletColors[strengthUpgrade - 2];
                }
                if (!gMogmaMittsForceGauntlets && strengthUpgrade == PLAYER_STR_SILVER_G &&
                    CVarGetInteger(CVAR_COSMETIC("Gloves.SilverGauntlets.Changed"), 0)) {
                    sTemp = CVarGetColor24(CVAR_COSMETIC("Gloves.SilverGauntlets.Value"), *color);
                    color = &sTemp;
                } else if (!gMogmaMittsForceGauntlets && strengthUpgrade == PLAYER_STR_GOLD_G &&
                           CVarGetInteger(CVAR_COSMETIC("Gloves.GoldenGauntlets.Changed"), 0)) {
                    sTemp = CVarGetColor24(CVAR_COSMETIC("Gloves.GoldenGauntlets.Value"), *color);
                    color = &sTemp;
                }
                gDPSetEnvColor(POLY_OPA_DISP++, color->r, color->g, color->b, 0);

                gSPDisplayList(POLY_OPA_DISP++, gLinkAdultLeftGauntletPlate1DL);
                gSPDisplayList(POLY_OPA_DISP++, gLinkAdultRightGauntletPlate1DL);
                gSPDisplayList(POLY_OPA_DISP++, (sLeftHandType == PLAYER_MODELTYPE_LH_OPEN)
                                                    ? gLinkAdultLeftGauntletPlate2DL
                                                    : gLinkAdultLeftGauntletPlate3DL);
                gSPDisplayList(POLY_OPA_DISP++, (sRightHandType == PLAYER_MODELTYPE_RH_OPEN)
                                                    ? gLinkAdultRightGauntletPlate2DL
                                                    : gLinkAdultRightGauntletPlate3DL);
            }

            // Skijer 2026-07-15: Pegasus Anklet's model = the vanilla HOVER BOOTS recolored RED (no
            // custom DL anymore). When Pegasus is the current ext boots and no vanilla boot model is
            // shown, draw the hover-boot DLs through the grayscale-recolor path (same technique as
            // the age-restricted icon tint — reliable on any DL regardless of its combiner).
            {
                u8 pegasusRed = (boots == 0) && ExtEquip_IsEnabled() && (ExtEquip_GetCurrent(EQUIP_TYPE_BOOTS) == 1);

                if ((boots != 0) || pegasusRed) {
                    Gfx** bootDLists =
                        pegasusRed ? sBootDListGroups[PLAYER_BOOTS_HOVER - 1] : sBootDListGroups[boots - 1];

                    if (pegasusRed) {
                        gDPSetGrayscaleColor(POLY_OPA_DISP++, 210, 30, 30, 255);
                        gSPGrayscale(POLY_OPA_DISP++, true);
                    }
                    gSPDisplayList(POLY_OPA_DISP++, bootDLists[0]);
                    gSPDisplayList(POLY_OPA_DISP++, bootDLists[1]);
                    if (pegasusRed) {
                        gSPGrayscale(POLY_OPA_DISP++, false);
                    }
                }
            }
        } else {
            // Child Link
            if (gMogmaMittsForceGauntlets) {
                // Mogma Mitts: force white gauntlets visible on child Link too
                // Use adult gauntlet models scaled for child
                gDPPipeSync(POLY_OPA_DISP++);
                color = &sGauntletColors[0]; // White/silver color
                gDPSetEnvColor(POLY_OPA_DISP++, color->r, color->g, color->b, 0);

                gSPDisplayList(POLY_OPA_DISP++, gLinkAdultLeftGauntletPlate1DL);
                gSPDisplayList(POLY_OPA_DISP++, gLinkAdultRightGauntletPlate1DL);
                gSPDisplayList(POLY_OPA_DISP++, (sLeftHandType == PLAYER_MODELTYPE_LH_OPEN)
                                                    ? gLinkAdultLeftGauntletPlate2DL
                                                    : gLinkAdultLeftGauntletPlate3DL);
                gSPDisplayList(POLY_OPA_DISP++, (sRightHandType == PLAYER_MODELTYPE_RH_OPEN)
                                                    ? gLinkAdultRightGauntletPlate2DL
                                                    : gLinkAdultRightGauntletPlate3DL);
            } else if (Player_GetStrength() > PLAYER_STR_NONE) {
                gSPDisplayList(POLY_OPA_DISP++, gLinkChildGoronBraceletDL);
            }
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

