/* overlay-merge ec8c85f63ef2 2e40856d070f 31c5d797cc87 */
void CollisionCheck_ApplyDamage(PlayState* play, CollisionCheckContext* colChkCtx, Collider* collider,
                                ColliderInfo* info) {
    DamageTable* tbl;
    f32 damage;

    if (collider->actor == NULL || !(collider->acFlags & AC_HIT)) {
        return;
    }
    if (!(info->bumperFlags & BUMP_HIT) ||
        (info->bumperFlags & BUMP_NO_DAMAGE &&
         !(info->acHitInfo && info->acHitInfo->toucher.dmgFlags & DMG_UNBLOCKABLE))) {
        return;
    }

    assert(info->acHitInfo != NULL);
    tbl = collider->actor->colChkInfo.damageTable;
    if (tbl == NULL) {
        damage = (f32)info->acHitInfo->toucher.damage - info->bumper.defense;
        if (damage < 0) {
            damage = 0;
        }
    } else {
        s32 i;
        u32 flags = info->acHitInfo->toucher.dmgFlags;

        for (i = 0; i < 0x20; i++, flags >>= 1) {
            if (flags == 1) {
                break;
            }
        }

        damage = tbl->table[i] & 0xF;
        collider->actor->colChkInfo.damageEffect = tbl->table[i] >> 4 & 0xF;
    }
    // DMG_UNBLOCKABLE (Gigantamax Pikachu): bypass damage table, force minimum damage
    if (info->acHitInfo->toucher.dmgFlags & DMG_UNBLOCKABLE) {
        if (damage < 4)
            damage = 4; // Minimum 4 damage regardless of resistance
    }
    // Skijer's NEI: DMG_FIXED_DAMAGE (forms) uses toucher damage verbatim
    if (info->acHitInfo->toucher.dmgFlags & DMG_FIXED_DAMAGE) {
        damage = (f32)info->acHitInfo->toucher.damage;
    }
    if (!(collider->acFlags & AC_HARD)) {
        // HOST-SLIM-001 (RFC 0029): oot.actor.damage e hook.oot.actor.damage trocam o dano; sem hook, volta `damage`.
        collider->actor->colChkInfo.damage += LinkSpan_ActorDamage(play, collider, info, damage);
    }

    // Na conta estendida, cada contribuição já recebeu o coop antes do espelho (RFC 0031).
    if (NEI_PlayerDamageBoostActive() && !LinkSpan_ActorHasExtendedHealth(collider->actor)) {
        collider->actor->colChkInfo.damage *= GET_PLAYER(play)->ivanDamageMultiplier;
    }
}