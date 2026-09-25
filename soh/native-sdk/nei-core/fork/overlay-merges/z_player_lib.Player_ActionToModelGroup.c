/* overlay-merge 1384e16368ca b70e3f425fdd 1f675c04ae01 */
s32 Player_ActionToModelGroup(Player* this, s32 actionParam) {
    s32 modelGroup = ExtPlayer_GetActionModelGroup(actionParam);

    if ((modelGroup == PLAYER_MODELGROUP_SWORD_AND_SHIELD) && Player_IsChildWithHylianShield(this)) {
        // child, using kokiri sword with hylian shield equipped
        return PLAYER_MODELGROUP_CHILD_HYLIAN_SHIELD;
    } else {
        return modelGroup;
    }
}