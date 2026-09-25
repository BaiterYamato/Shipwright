/* overlay-merge 6424ac8052ef c01ff8d6abd7 98bc487e24b6 */
s32 Player_ActionToMeleeWeapon(s32 itemAction) {
    s32 sword = itemAction - PLAYER_IA_FISHING_POLE;

    if ((sword > 0) && (sword < 6)) {
        return sword;
    }

    // Custom melee weapons (Fire Rod, Ice Rod, Light Rod) - treated as Deku Stick (4)
    if (itemAction == PLAYER_IA_ROD_FIRE || itemAction == PLAYER_IA_ROD_ICE || itemAction == PLAYER_IA_ROD_LIGHT) {
        return 4; // Same as PLAYER_IA_DEKU_STICK
    }

    return 0;
}