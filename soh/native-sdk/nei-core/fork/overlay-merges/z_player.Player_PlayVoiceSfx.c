/* overlay-merge ca8140ab41a7 7f3954336f05 605ad5069db0 */
void Player_PlayVoiceSfx(Player* this, u16 sfxId) {
    if (LinkSpan_PlayerVoiceSfx(this, &sfxId) != 0) {
        return;
    }
    if (!GameInteractor_Should(VB_PLAYER_VOICE_SFX, true, this, sfxId)) {
        return;
    }

    if (this->actor.category == ACTORCAT_PLAYER) {
        Player_PlaySfx(this, sfxId + this->ageProperties->unk_92);
    } else {
        func_800F4190(&this->actor.projectedPos, sfxId);
    }
}
