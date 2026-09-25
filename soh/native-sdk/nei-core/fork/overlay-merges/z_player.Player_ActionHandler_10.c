/* overlay-merge 36ad46ee86ad c7f3c9d9141d d5078fc091c7 */
s32 Player_ActionHandler_10(Player* this, PlayState* play) {
    s32 controlStickDirection;

    // Transformation masks: ALL forms can use Handler_10 (jump/sidehop/backflip).
    // Only the ROLL handler (Handler_Roll) is blocked for Goron/Deku.

    if (CHECK_BTN_ALL(sControlInput->press.button, BTN_A) &&
        (play->roomCtx.curRoom.behaviorType1 != ROOM_BEHAVIOR_TYPE1_2) && (sFloorType != 7) &&
        (ClimbBoots_HasGrip() || // gripped slopes count as flat ground, so A can jump off them
         SurfaceType_GetFloorEffect(&play->colCtx, this->actor.floorPoly, this->actor.floorBgId) != FLOOR_EFFECT_1)) {
        controlStickDirection = this->controlStickDirections[this->controlStickDataIndex];

        if (controlStickDirection <= PLAYER_STICK_DIR_FORWARD) {
            if (Player_IsZTargeting(this)) {
                if (this->actor.category != ACTORCAT_PLAYER) {
                    if (controlStickDirection <= PLAYER_STICK_DIR_NONE) {
                        func_808389E8(this, &gPlayerAnim_link_normal_jump, REG(69) / 100.0f, play);
                    } else {
                        Player_SetupRoll(this, play);
                    }
                } else {
                    if (KafeiForm_ReplacesJumpslash()) {
                        // Kafei never jump-slashes off the ground: A under Z-target is a
                        // plain jump. Same call the non-player branch above uses, so it is
                        // vanilla's own jump animation and launch.
                        func_808389E8(this, &gPlayerAnim_link_normal_jump, REG(69) / 100.0f, play);
                    } else if ((Player_GetMeleeWeaponHeld(this) != 0) && Player_CanUpdateItems(this)) {
                        func_8083BA90(play, this, PLAYER_MWA_JUMPSLASH_START, 5.0f, 5.0f);
                    } else if (TransformMasks_IsTransformed()) {

                        s32 form = MmForm_GetCurrentForm();
                        // Wolf Link shares Pikachu's slot but owns its own aerial lunge, so OOT must
                        // not jump-slash for it. Gerudo never reaches here: with the blades stowed
                        // Player_GetMeleeWeaponHeld already returned 0 above and she rolls instead.
                        if (form == MM_PLAYER_FORM_ZORA || form == MM_PLAYER_FORM_FIERCE_DEITY ||
                            (form == MM_PLAYER_FORM_PIKACHU && !WolfLinkForm_IsSelected())) {
                            func_8083BA90(play, this, PLAYER_MWA_JUMPSLASH_START, 5.0f, 5.0f);
                        } else {
                            // Goron → curl, Deku → spin (via Player_SetupRoll redirects)
                            Player_SetupRoll(this, play);
                        }
                    } else {
                        Player_SetupRoll(this, play);
                    }
                }

                return 1;
            }
        } else {
            func_8083BCD0(this, play, controlStickDirection);

            if (controlStickDirection == 1 || controlStickDirection == 3) {
                gSaveContext.ship.stats.count[COUNT_SIDEHOPS]++;
            }
            if (controlStickDirection == 2) {
                gSaveContext.ship.stats.count[COUNT_BACKFLIPS]++;
            }

            return 1;
        }
    }

    return 0;
}