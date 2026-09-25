/* overlay-merge bd780fa67430 41dde49e05bd 7673dbb4c109 */
s32 Player_HandleSlopes(PlayState* play, Player* this, CollisionPoly* floorPoly) {
    static LinkAnimationHeader* sSlopeSlideAnims[] = {
        &gPlayerAnim_link_normal_down_slope_slip,
        &gPlayerAnim_link_normal_up_slope_slip,
    };
    s32 pad;
    s16 playerVelYaw;
    Vec3f slopeNormal;
    s16 downwardSlopeYaw;
    f32 slopeSlowdownSpeed;
    f32 slopeSlowdownSpeedStep;
    s16 velYawToDownwardSlope;

    // Climb Boots: full traction — steep slopes neither force the slide nor slow the climb.
    // Roc's Boots on the water surface: the lake bottom's slope is not the floor Link stands on.
    if (!Player_InBlockingCsMode(play, this) && (Player_Action_SlideOnSlope != this->actionFunc) &&
        !ClimbBoots_HasGrip() && !RocBoots_OnWater() &&
        (SurfaceType_GetFloorEffect(&play->colCtx, floorPoly, this->actor.floorBgId) == FLOOR_EFFECT_1)) {
        // Get direction of movement relative to the downward direction of the slope
        playerVelYaw = Math_Atan2S(this->actor.velocity.z, this->actor.velocity.x);
        Player_GetSlopeDirection(floorPoly, &slopeNormal, &downwardSlopeYaw);
        velYawToDownwardSlope = downwardSlopeYaw - playerVelYaw;

        if (ABS(velYawToDownwardSlope) > 0x3E80) { // 87.9 degrees
            // moving parallel or upwards on the slope, player does not slip but does slow down
            slopeSlowdownSpeed = (1.0f - slopeNormal.y) * 40.0f;
            slopeSlowdownSpeedStep = (slopeSlowdownSpeed * slopeSlowdownSpeed) * 0.015f;

            if (slopeSlowdownSpeedStep < 1.2f) {
                slopeSlowdownSpeedStep = 1.2f;
            }

            // slows down speed as player is climbing a slope
            this->pushedYaw = downwardSlopeYaw;
            Math_StepToF(&this->pushedSpeed, slopeSlowdownSpeed, slopeSlowdownSpeedStep);
        } else {
            // moving downward on the slope, causing player to slip
            Player_SetupAction(play, this, Player_Action_SlideOnSlope, 0);
            func_80832564(play, this);

            if (sFloorShapePitch >= 0) {
                this->av1.actionVar1 = 1;
            }
            Player_AnimChangeLoopMorph(play, this, sSlopeSlideAnims[this->av1.actionVar1]);
            this->linearVelocity = sqrtf(SQ(this->actor.velocity.x) + SQ(this->actor.velocity.z));
            this->yaw = playerVelYaw;
            return true;
        }
    }

    return false;
}