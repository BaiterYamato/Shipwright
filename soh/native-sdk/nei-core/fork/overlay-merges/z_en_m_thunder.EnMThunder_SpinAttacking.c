/* overlay-merge 14f5d3fd368a 3dab3395c787 d210a9fb329b */
void EnMThunder_SpinAttacking(EnMThunder* this, PlayState* play) {
    Player* player = GET_PLAYER(play);

    // Gerudo: the wedge is THROWN, not worn — it leaves the blades on one exact frame of
    // the release swing, not on the frame the button comes up. That frame is source frame
    // GMHR_CHARGE_FAST_BEG of ForwardTumbleDelayedCrossFinish, which the clip builder
    // translates into an installed frame for us (the release is built at three rates, so
    // source and installed frames are not proportional). Until then the actor is frozen:
    // no growth, no collider, no draw, and the lifetime does not start ticking. The frame
    // cap is a safety net so a swing cut short (damage, a cutscene) can never strand it
    // waiting forever. Skijer's NEI
    if (this->isGerudoCone && !this->coneArmed) {
        s16 summonFrame = GerudoMhr_ChargeSummonFrame();
        f32 last = player->skelAnime.endFrame;
        f32 mark = (summonFrame > 0) ? (f32)summonFrame : (last * (2.0f / 3.0f));

        this->actor.scale.x = 0.0f;
        Actor_SetScale(&this->actor, 0.0f);
        this->spinAttackAlpha = 0.0f;
        if ((this->coneWait++ >= GERUDO_WEDGE_ARM_CAP) || ((mark > 0.0f) && (player->skelAnime.curFrame >= mark))) {
            this->coneArmed = 1;
        }
        if (Play_InCsMode(play)) {
            Actor_Kill(&this->actor);
        }
        return;
    }

    if (Math_StepToF(&this->spinAttackTimer, 0.0f, 1 / 16.0f)) {
        Actor_Kill(&this->actor);
    } else {
        Math_SmoothStepToF(&this->actor.scale.x, (s32)this->targetScale, 0.6f, 0.8f, 0.0f);
        Actor_SetScale(&this->actor, this->actor.scale.x);
        if (this->isGerudoCone) {
            // Her volume is the forward third of the ring, not the whole ring. Same
            // growth curve; the cylinder that stands in for the wedge is pushed out
            // along her release facing and is 1.5x the vanilla radius.
            // AT_HIT must be read BEFORE CollisionCheck_SetAT — sATResetFuncs clears the
            // flag every frame (same trap as the sword beam further down).
            f32 wedgeR = GERUDO_WEDGE_WORLD_RADIUS(this->actor.scale.x);
            if (this->collider.base.atFlags & AT_HIT) {
                GerudoMhr_AddChargeRage(this->collider.base.at);
            }
            this->collider.dim.radius = (s16)(wedgeR * 0.62f);
            this->collider.dim.height = 70;
            this->collider.dim.yShift = -35;
            Collider_UpdateCylinder(&this->actor, &this->collider);
            this->collider.dim.pos.x += (s16)(Math_SinS(this->coneYaw) * wedgeR * 0.55f);
            this->collider.dim.pos.z += (s16)(Math_CosS(this->coneYaw) * wedgeR * 0.55f);
        } else {
            this->collider.dim.radius = LinkSpan_S16F((this->actor.scale.x * 25.0f));
            Collider_UpdateCylinder(&this->actor, &this->collider);
        }
        CollisionCheck_SetAT(play, &play->colChkCtx, &this->collider.base);
    }

    if (this->followPlayerTimer > 0) {
        this->actor.world.pos.x = player->bodyPartsPos[0].x;
        this->actor.world.pos.z = player->bodyPartsPos[0].z;
        this->followPlayerTimer--;
    }

    if (this->spinAttackTimer > 0.6f) {
        this->spinAttackAlpha = 1.0f;
    } else {
        this->spinAttackAlpha = this->spinAttackTimer * (5.0f / 3.0f);
    }

    EnMThunder_UpdateSpinAttack(this, play);

    if (Play_InCsMode(play)) {
        Actor_Kill(&this->actor);
    }
}
