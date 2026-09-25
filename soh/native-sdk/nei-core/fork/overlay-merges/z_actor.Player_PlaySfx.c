/* overlay-merge 910ecd993337 4ca110a4cc8c 2275be1e7605 */
void Player_PlaySfx(Actor* actor, u16 sfxId) {
    // Suppress OOT SFX when in MM transformation form.
    // MM forms play their own sounds via MmSfx_PlayAtPos / MmForm_PlaySfx.
    // Keep: floor/surface SFX (WALK, JUMP, LAND, SLIP), environmental, water, status effects.
    extern u8 TransformMasks_IsTransformed(void);
    extern u8 GerudoForm_IsActive(void);
    // Gerudo is the exception — soh.o2r doesn't ship MM combat SFX, so its
    // dual-scimitar combo plays vanilla OOT sword sounds (NA_SE_IT_SWORD_SWING,
    // etc.) directly. Without this carve-out the swing audio is silently
    // dropped by the NA_SE_IT_* block below. Same pattern as
    // Player_PlayVoiceSfx's Gerudo exception (z_player.c:1833).
    if (actor->id == ACTOR_PLAYER && TransformMasks_IsTransformed() && !GerudoForm_IsActive()) {
        // Block ALL item/weapon SFX (NA_SE_IT_* = 0x1800-0x18FF)
        if ((sfxId & 0xF800) == 0x1800) {
            return;
        }
        // Block ALL voice SFX (NA_SE_VO_LI_*)
        if (sfxId >= NA_SE_VO_LI_SWORD_N && sfxId <= NA_SE_VO_LI_ELECTRIC_SHOCK_LV_KID) {
            return;
        }
        // Block only combat SFX that MM handles via its own system.
        // Keep body sounds (BODY_HIT, DAMAGE) — they're form-neutral impacts.
        switch (sfxId) {
            case NA_SE_PL_THROW:
            case NA_SE_PL_CHANGE_ARMS:
            case NA_SE_PL_CATCH_BOOMERANG:
            case NA_SE_PL_KNOCK:
            case NA_SE_PL_SPARK:
                return;
        }
    }

    if (actor->id != ACTOR_PLAYER || sfxId < NA_SE_VO_LI_SWORD_N || sfxId > NA_SE_VO_LI_ELECTRIC_SHOCK_LV_KID) {
        Audio_PlaySfxGeneral(sfxId, &actor->projectedPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                             &gSfxDefaultReverb);
    } else {
        // Custom voice pack interception: a loaded pack may replace this Link
        // voice id with a sample from a .pak in mods/. If it handles the id we
        // skip the vanilla SFX so we don't double-play.
        extern u8 VoicePack_PlayIfMatch(u16 sfxId, Vec3f * pos);
        if (!VoicePack_PlayIfMatch(sfxId, &actor->projectedPos)) {
            freqMultiplier = CVarGetFloat(CVAR_AUDIO("LinkVoiceFreqMultiplier"), 1.0);
            if (freqMultiplier <= 0) {
                freqMultiplier = 1;
            }
            // Authentic behavior uses D_801333E0 for both freqScale and a4
            // Audio_PlaySoundGeneral(sfxId, &actor->projectedPos, 4, &D_801333E0 , &D_801333E0, &D_801333E8);
            Audio_PlaySoundGeneral(sfxId, &actor->projectedPos, 4, &freqMultiplier, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultReverb);
        }
    }

    if (actor->id == ACTOR_PLAYER) {
        GameInteractor_ExecuteOnPlayerSfx(sfxId);
    }
}