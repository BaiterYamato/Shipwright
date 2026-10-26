/* overlay-merge b8572a69587e c066e6b78a78 7caac66e68f4 */
void func_80837918(Player* this, s32 quadIndex, u32 dmgFlags) {
    if (LinkSpan_PlayerQuadOwned(&this->meleeWeaponQuads[quadIndex])) {
        return;
    }
    // Giant's Mask: Link's strikes land as hammer blows (MM behavior). Skijer's NEI
    extern s32 MmMaskWear_IsGiantMaskActive(void);
    if (MmMaskWear_IsGiantMaskActive()) {
        dmgFlags = DMG_HAMMER_SWING;
    }
    this->meleeWeaponQuads[quadIndex].info.toucher.dmgFlags = dmgFlags;

    if (dmgFlags == 2) {
        this->meleeWeaponQuads[quadIndex].info.toucherFlags = TOUCH_ON | TOUCH_NEAREST | TOUCH_SFX_WOOD;
    } else {
        this->meleeWeaponQuads[quadIndex].info.toucherFlags = TOUCH_ON | TOUCH_NEAREST;
    }
}
