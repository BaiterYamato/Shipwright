/* overlay-merge 86cc340174c3 e1255498422d e7b7d8ed0a98 */
s32 Player_HoldsTwoHandedWeapon(Player* this) {
    s32 result = (this->heldItemAction >= PLAYER_IA_SWORD_BIGGORON) && (this->heldItemAction <= PLAYER_IA_HAMMER);
    // Skijer's NEI: custom items / forms can be two-handed (FD sword, Fire/Ice/Light rods)
    return GameInteractor_Should(VB_PLAYER_HOLDS_TWO_HANDED_WEAPON, result, this);
}