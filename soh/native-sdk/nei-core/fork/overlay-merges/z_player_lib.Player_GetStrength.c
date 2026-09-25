/* overlay-merge 0f95c0fe98fc 88cd72baa550 3f1a8f6dc280 */
s32 Player_GetStrength(void) {
    s32 strengthUpgrade = CUR_UPG_VALUE(UPG_STRENGTH);

    if (CVarGetInteger(CVAR_ENHANCEMENT("ToggleStrength"), 0) &&
        CVarGetInteger(CVAR_ENHANCEMENT("StrengthDisabled"), 0)) {
        return PLAYER_STR_NONE;
    }

    // MM transformation forms have an intrinsic body strength (independent of save upgrade bits). Skijer's NEI
    if (TransformMasks_IsTransformed()) {
        extern s32 MmForm_GetStrengthOverride(void);
        s32 formStr = MmForm_GetStrengthOverride();
        if (formStr >= 0) {
            return formStr;
        }
    }

    // Giant's Mask grants max lift strength (Gold Gauntlets) without touching the
    // save upgrade bits, so randomizer progressive-strength logic stays intact. Skijer's NEI
    extern s32 MmMaskWear_IsGiantMaskActive(void);
    if (MmMaskWear_IsGiantMaskActive()) {
        return PLAYER_STR_GOLD_G;
    }

    if (GameInteractor_Should(VB_PLAYER_MEETS_AGE_REQ, LINK_IS_ADULT, LINK_AGE_ADULT)) {
        return strengthUpgrade;
    } else if (strengthUpgrade != 0) {
        return PLAYER_STR_BRACELET;
    } else {
        return PLAYER_STR_NONE;
    }
}