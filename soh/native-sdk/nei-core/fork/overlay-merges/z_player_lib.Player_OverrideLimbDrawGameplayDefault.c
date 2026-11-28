/* overlay-merge 98b045789c48 5d532fd00eea b51691cbdd2b */
s32 Player_OverrideLimbDrawGameplayDefault(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot,
                                           void* thisx) {
    Player* this = (Player*)thisx;

    if (!Player_OverrideLimbDrawGameplayCommon(play, limbIndex, dList, pos, rot, thisx)) {
        // R059/R060: compõe a correção limitada sobre a pose animada; com CVar=0 não altera nada.
        if (limbIndex == PLAYER_LIMB_HAT) {
            HatPhysics_Apply(play, this, pos, rot);
        }
        // Gerudo Form dual-wield (hand = scimitar DL, sheath hidden). Skijer's NEI
        u8 gerudoHandled = GerudoForm_ResolveLimbDL(limbIndex, dList);

        // PAK Loader: When a custom model or equipment pak is active, try equipment DLs first.
        // If GetEquipDL returns a DL or STUB, use it. If NULL, fall through to vanilla code.
        if (!gerudoHandled) {
            u8 pakHandled = 0;
            if (PakLoader_HasActiveModel() && (limbIndex == PLAYER_LIMB_L_HAND || limbIndex == PLAYER_LIMB_R_HAND ||
                                               limbIndex == PLAYER_LIMB_SHEATH || limbIndex == PLAYER_LIMB_WAIST)) {
                Gfx* pakDL = PakLoader_GetEquipDL(this, limbIndex);
                if (pakDL == PAK_DL_STUB) {
                    *dList = NULL;
                    pakHandled = 1;
                } else if (pakDL != NULL) {
                    *dList = pakDL;
                    pakHandled = 1;
                }
                // pakDL == NULL for hands/sheath → fall through to vanilla for that limb
                // pakDL == NULL for WAIST → skeleton swap already provides the custom DL, don't let vanilla overwrite
                if (pakDL == NULL && limbIndex == PLAYER_LIMB_WAIST && PakLoader_GetSelectedIndex() >= 0) {
                    pakHandled = 1; // Keep skeleton's custom waist DL
                }
            }
            if (!pakHandled && limbIndex == PLAYER_LIMB_L_HAND) {
                Gfx** dLists = this->leftHandDLists;

                if ((sLeftHandType == PLAYER_MODELTYPE_LH_BGS) && (gSaveContext.swordHealth <= 0.0f)) {
                    dLists += 4;
                } else if ((sLeftHandType == PLAYER_MODELTYPE_LH_BOOMERANG) &&
                           (this->stateFlags1 & PLAYER_STATE1_BOOMERANG_THROWN)) {
                    dLists = &gPlayerLeftHandOpenDLs[gSaveContext.linkAge];
                    sLeftHandType = PLAYER_MODELTYPE_LH_OPEN;
                } else if ((this->leftHandType == PLAYER_MODELTYPE_LH_OPEN) && (this->actor.speedXZ > 2.0f) &&
                           !(this->stateFlags1 & PLAYER_STATE1_IN_WATER)) {
                    dLists = &gPlayerLeftHandClosedDLs[gSaveContext.linkAge];
                    sLeftHandType = PLAYER_MODELTYPE_LH_CLOSED;
                }

                // Extended equipment: hide sword DL when ext sword draws its own model
                if (ExtEquip_ShouldHideSwordDL() &&
                    (sLeftHandType != PLAYER_MODELTYPE_LH_OPEN && sLeftHandType != PLAYER_MODELTYPE_LH_CLOSED &&
                     sLeftHandType != PLAYER_MODELTYPE_LH_BOOMERANG)) {
                    dLists = &gPlayerLeftHandOpenDLs[gSaveContext.linkAge];
                    sLeftHandType = PLAYER_MODELTYPE_LH_OPEN;
                }

                // Boss Remains: Odolwa hides Link's native sword to a plain closed fist (his own
                // sword is drawn on top in Player_PostLimbDrawGameplay); Goht disables the sword
                // entirely — empty closed fist whenever the hand would hold one. Mirrors MM 2ship.
                if ((BossRemains_IsOdolwaWorn() || BossRemains_IsGohtWorn()) &&
                    (sLeftHandType != PLAYER_MODELTYPE_LH_OPEN && sLeftHandType != PLAYER_MODELTYPE_LH_CLOSED &&
                     sLeftHandType != PLAYER_MODELTYPE_LH_BOOMERANG)) {
                    dLists = &gPlayerLeftHandClosedDLs[gSaveContext.linkAge];
                    sLeftHandType = PLAYER_MODELTYPE_LH_CLOSED;
                }
                *dList = Player_ResolveLimbDLForDummyOrLocal(dLists[sDListsLodOffset]);
            } else if (!pakHandled && limbIndex == PLAYER_LIMB_R_HAND) {
                Gfx** dLists = this->rightHandDLists;

                if (sRightHandType == PLAYER_MODELTYPE_RH_SHIELD) {
                    // Boss Remains: Odolwa's remains also hide the native hand-held shield — his own
                    // shield is drawn in Player_PostLimbDrawGameplay (R_HAND). Mirrors MM 2ship.
                    if ((ExtEquip_GetShieldDLOverride() != NULL) || BossRemains_IsOdolwaWorn()) {
                        // Shield of Ikana: show open hand, custom shield drawn in PostLimbDraw
                        dLists = &sPlayerRightHandOpenDLs[gSaveContext.linkAge];
                        sRightHandType = PLAYER_MODELTYPE_RH_OPEN;
                    } else {
                        dLists += this->currentShield * 4;
                    }
                } else if (ItemEquip_HoldsClosedFist()) {
                    // Slate / Rod of Seasons in hand: gripped like the Hookshot. The shield keeps
                    // precedence above so its row offset is never skipped. Skijer's NEI
                    dLists = &sPlayerRightHandClosedDLs[gSaveContext.linkAge];
                    sRightHandType = PLAYER_MODELTYPE_RH_CLOSED;
                } else if (ItemEquip_HoldsEmptyHand()) {
                    // Recall aim / Ultrahand carry: both take the HOOKSHOT group for its reaching
                    // pose, and this is what keeps the hookshot itself out of the hand. Skijer's NEI
                    dLists = &sPlayerRightHandOpenDLs[gSaveContext.linkAge];
                    sRightHandType = PLAYER_MODELTYPE_RH_OPEN;
                } else if ((this->rightHandType == PLAYER_MODELTYPE_RH_OPEN) && (this->actor.speedXZ > 2.0f) &&
                           !(this->stateFlags1 & PLAYER_STATE1_IN_WATER)) {
                    dLists = &sPlayerRightHandClosedDLs[gSaveContext.linkAge];
                    sRightHandType = PLAYER_MODELTYPE_RH_CLOSED;
                }

                *dList = Player_ResolveLimbDLForDummyOrLocal(dLists[sDListsLodOffset]);
            } else if (!pakHandled && limbIndex == PLAYER_LIMB_SHEATH) {
                Gfx** dLists = this->sheathDLists;

                if ((this->sheathType == PLAYER_MODELTYPE_SHEATH_18) ||
                    (this->sheathType == PLAYER_MODELTYPE_SHEATH_19)) {
                    if (ExtEquip_GetShieldDLOverride() != NULL) {
                        dLists = &sSheathDLs[gSaveContext.linkAge];
                    } else {
                        dLists += this->currentShield * 4;
                        if (!LINK_IS_ADULT && (this->currentShield < PLAYER_SHIELD_HYLIAN) &&
                            (gSaveContext.equips.buttonItems[0] != ITEM_SWORD_KOKIRI)) {
                            dLists += PLAYER_SHIELD_MAX * 4;
                        }
                    }
                } else if (!CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0) ||
                           (CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0) &&
                            ((gSaveContext.equips.buttonItems[0] != ITEM_SWORD_MASTER &&
                              gSaveContext.equips.buttonItems[0] != ITEM_SWORD_BGS) &&
                             this->currentShield == PLAYER_SHIELD_DEKU))) {
                    if (!LINK_IS_ADULT &&
                        ((this->sheathType == PLAYER_MODELTYPE_SHEATH_16) ||
                         (this->sheathType == PLAYER_MODELTYPE_SHEATH_17)) &&
                        (gSaveContext.equips.buttonItems[0] != ITEM_SWORD_KOKIRI)) {
                        dLists = &sSheathWithSwordDLs[PLAYER_SHIELD_MAX * 4];
                    }
                }

                if (dLists[sDListsLodOffset] != NULL) {
                    *dList = Player_ResolveLimbDLForDummyOrLocal(dLists[sDListsLodOffset]);
                } else {
                    *dList = NULL;
                }

            } else if (!pakHandled && limbIndex == PLAYER_LIMB_WAIST) {

                if (!Player_IsCustomLinkModel()) {
                    *dList = Player_ResolveLimbDLForDummyOrLocal(
                        this->waistDLists[sDListsLodOffset]); // NOTE: This needs to be disabled when using custom
                                                              // characters - they're not going to have LODs anyways...
                }
            }
        } // close pakHandled block

        // Hide Link's held-weapon DL for items that draw their own model (rods/Byrna/IK Axe).
        // After pak_loader so o2r equipment mods are caught too. Skijer's NEI
        if (!gerudoHandled && limbIndex == PLAYER_LIMB_L_HAND && this->actor.scale.y >= 0.0f) {
            // Ext-equipment pieces that draw their OWN weapon (Byrna cane, Trident
            // lance) have to suppress the NEI upgrade blades too — Razor / Gilded /
            // Great Fairy's are drawn by the else-branch below, which never consults
            // VB_PLAYER_SHOULD_HIDE_HELD_WEAPON.
            //
            // ⚠️ Setting hideLH alone was NOT enough and read as "the fix does
            // nothing": the L_HAND limb block ~90 lines up ALREADY forced the hand
            // open for the same reason and left sLeftHandType == LH_OPEN behind. So
            // the `sLeftHandType != LH_OPEN` guard below was false, control fell into
            // the else, and WeaponUpgrade_ApplyHeldSwordDL drew the Gilded Sword on
            // an open hand. The earlier hide SUCCEEDING is what routed us here.
            //
            // Hence the separate flag: when ext equipment owns the weapon the upgrade
            // blade must not be drawn at all, whatever sLeftHandType already says.
            // Skijer's NEI
            u8 extOwnsWeapon = ExtEquip_ShouldHideSwordDL();
            u8 hideLH = extOwnsWeapon || GameInteractor_Should(VB_PLAYER_SHOULD_HIDE_HELD_WEAPON, false, this);
            if (hideLH && sLeftHandType != PLAYER_MODELTYPE_LH_OPEN && sLeftHandType != PLAYER_MODELTYPE_LH_CLOSED &&
                sLeftHandType != PLAYER_MODELTYPE_LH_BOOMERANG) {
                Gfx** openDLs = &gPlayerLeftHandOpenDLs[gSaveContext.linkAge];
                *dList = Player_ResolveLimbDLForDummyOrLocal(openDLs[sDListsLodOffset]);
                sLeftHandType = PLAYER_MODELTYPE_LH_OPEN;
            } else if (!extOwnsWeapon) {
                // NEI progressive sword upgrades: keep an OOT open hand and draw the MM Razor /
                // Gilded / Great Fairy's Sword pieces (loaded from o2r) on top — pak_loader-style
                // (sword then hand), supporting mods. No-op unless the upgraded sword is wielded.
                Gfx** openDLs = &gPlayerLeftHandOpenDLs[gSaveContext.linkAge];
                void* ootHand = Player_ResolveLimbDLForDummyOrLocal(openDLs[sDListsLodOffset]);
                if (WeaponUpgrade_ApplyHeldSwordDL(dList, ootHand, this, sPlayerBodyEnvColor.r, sPlayerBodyEnvColor.g,
                                                   sPlayerBodyEnvColor.b)) {
                    sLeftHandType = PLAYER_MODELTYPE_LH_OPEN;
                } else if (sLeftHandType == PLAYER_MODELTYPE_LH_OPEN) {
                    // No upgraded sword to draw, so ootHand was resolved and then dropped:
                    // *dList kept whatever vanilla picked earlier, which is LINK's hand even
                    // when a custom skin is active. That is why Kafei whistled with an open
                    // Link hand on the left while his right hand was correct - the right
                    // hand's branches all assign, this one only assigned on a hit.
                    // Only for an OPEN hand: any other type means *dList is holding
                    // something and must not be replaced by an empty palm.
                    *dList = ootHand;
                }
            }
        }

        // Twilight clawshot mode: R-hand hookshot DL = OOT closed hand + MM hookshot body. Skijer's NEI
        if (!gerudoHandled && limbIndex == PLAYER_LIMB_R_HAND && this->actor.scale.y >= 0.0f &&
            sRightHandType == PLAYER_MODELTYPE_RH_HOOKSHOT) {
            extern void TwilightUpgrade_ApplyClawshotHandDL(Gfx * *dList, void* ootHand);
            void* ootHand = (sDListsLodOffset == 0) ? gLinkAdultRightHandClosedNearDL : gLinkAdultRightHandClosedFarDL;
            TwilightUpgrade_ApplyClawshotHandDL(dList, ootHand);
        }
    }

    GameInteractor_Should(VB_PLAYER_OVERRIDE_LIMB_DRAW, true, limbIndex, dList, thisx, play);

    // PAK Loader equipment mix must outrank CustomEquipment's VB_PLAYER_OVERRIDE_LIMB_DRAW
    // hook. Upstream #6708 added that hook at the END of this function — after our equipment
    // override at the top — so a per-slot pak selection got silently repainted by any active
    // equipment o2r mod. Re-apply the pak's DL here, but only when it actually provides one for
    // this limb: limbs without a per-slot/equipment-pack override keep the hook's result, so
    // o2r equipment still shows where there's no mix selection (the two coexist, as before).
    if (PakLoader_HasActiveModel() && (limbIndex == PLAYER_LIMB_L_HAND || limbIndex == PLAYER_LIMB_R_HAND ||
                                       limbIndex == PLAYER_LIMB_SHEATH || limbIndex == PLAYER_LIMB_WAIST)) {
        Gfx* pakReDL = PakLoader_GetEquipDL(this, limbIndex);
        if (pakReDL == PAK_DL_STUB) {
            *dList = NULL;
        } else if (pakReDL != NULL) {
            *dList = pakReDL;
        }
    }

    if (GameInteractor_InvisibleLinkActive()) {
        this->actor.shape.shadowDraw = NULL;
        *dList = NULL;
    }

    return false;
}

