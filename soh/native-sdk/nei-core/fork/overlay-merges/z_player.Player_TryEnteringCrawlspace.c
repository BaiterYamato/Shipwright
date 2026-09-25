/* overlay-merge 9ba68adbf93d 84494dd8604b 9b27b4c1f701 */
s32 Player_TryEnteringCrawlspace(Player* this, PlayState* play, u32 interactWallFlags) {
    if (!LINK_IS_ADULT && !(this->stateFlags1 & PLAYER_STATE1_IN_WATER) && (interactWallFlags & WALL_FLAG_CRAWLSPACE)) {
        CollisionPoly* wallPoly;
        Vec3f wallVertices[3];
        f32 xVertex1;
        f32 xVertex2;
        f32 zVertex1;
        f32 zVertex2;
        s32 i;

        // Minish tiny mode walks straight through crawlspaces — never enter the crawl state
        if (TransformMasks_IsTransformed() || MinishTiny_IsActive()) {
            return false;
        }

        if (!GameInteractor_Should(VB_CRAWL, true)) {
            return false;
        }

        wallPoly = this->actor.wallPoly;
        CollisionPoly_GetVerticesByBgId(wallPoly, this->actor.wallBgId, &play->colCtx, wallVertices);

        // Determines min and max vertices for x & z (edges of the crawlspace hole)
        xVertex1 = xVertex2 = wallVertices[0].x;
        zVertex1 = zVertex2 = wallVertices[0].z;
        for (i = 1; i < 3; i++) {
            if (xVertex1 > wallVertices[i].x) {
                // Update x min
                xVertex1 = wallVertices[i].x;
            } else if (xVertex2 < wallVertices[i].x) {
                // Update x max
                xVertex2 = wallVertices[i].x;
            }

            if (zVertex1 > wallVertices[i].z) {
                // Update z min
                zVertex1 = wallVertices[i].z;
            } else if (zVertex2 < wallVertices[i].z) {
                // Update z max
                zVertex2 = wallVertices[i].z;
            }
        }

        // XZ Center of the crawlspace hole
        xVertex1 = (xVertex1 + xVertex2) * 0.5f;
        zVertex1 = (zVertex1 + zVertex2) * 0.5f;

        // Perpendicular (sideways) XZ-Distance from player pos to crawlspace line
        // Uses y-component of crossproduct formula for the distance from a point to a line
        xVertex2 = ((this->actor.world.pos.x - xVertex1) * COLPOLY_GET_NORMAL(wallPoly->normal.z)) -
                   ((this->actor.world.pos.z - zVertex1) * COLPOLY_GET_NORMAL(wallPoly->normal.x));

        if (fabsf(xVertex2) < 8.0f) {
            // Give do-action prompt to "Enter on A" for the crawlspace
            this->stateFlags2 |= PLAYER_STATE2_DO_ACTION_ENTER;

            if (CHECK_BTN_ALL(sControlInput->press.button, BTN_A)) {
                // Enter Crawlspace
                f32 wallPolyNormX = COLPOLY_GET_NORMAL(wallPoly->normal.x);
                f32 wallPolyNormZ = COLPOLY_GET_NORMAL(wallPoly->normal.z);
                f32 distToInteractWall = this->distToInteractWall;

                Player_SetupWaitForPutAway(play, this, func_8083A40C);
                this->stateFlags2 |= PLAYER_STATE2_CRAWLING;
                this->actor.shape.rot.y = this->yaw = this->actor.wallYaw + 0x8000;
                this->actor.world.pos.x = xVertex1 + (distToInteractWall * wallPolyNormX);
                this->actor.world.pos.z = zVertex1 + (distToInteractWall * wallPolyNormZ);
                func_80832224(this);
                this->actor.prevPos = this->actor.world.pos;
                if (GameInteractor_Should(VB_CRAWL_SPEED_ENTER, true)) {
                    Player_AnimPlayOnce(play, this, &gPlayerAnim_link_child_tunnel_start);
                }
                Player_StartAnimMovement(play, this, 0x9D);
                return true;
            }
        }
    }

    return false;
}