/* overlay-extra aabd5a413be2 da39a3ee5e6b da39a3ee5e6b */
// Link-Span: o host tirou o corpo do laço do Actor_DrawAll para esta função; aqui entram as duas mudanças
// que o fork fez nesse corpo (lanterna de Poe como lente e o som contínuo das formas do MM).
static void Actor_DrawListEntry(PlayState* play, Actor* actor, s32 listIndex, Actor** invisibleActors,
                                s32* invisibleActorCounter) {
    OPEN_DISPS(play->state.gfxCtx);

    char* actorName = ActorDB_Retrieve(actor->id)->name;

    gDPNoOpString(POLY_OPA_DISP++, actorName, listIndex);
    gDPNoOpString(POLY_XLU_DISP++, actorName, listIndex);

    HREG(66) = listIndex;

    if ((HREG(64) != 1) || ((HREG(65) != -1) && (HREG(65) != HREG(66))) || (HREG(68) == 0)) {
        SkinMatrix_Vec3fMtxFMultXYZW(&play->viewProjectionMtxF, &actor->world.pos, &actor->projectedPos,
                                     &actor->projectedW);
    }

    if ((HREG(64) != 1) || ((HREG(65) != -1) && (HREG(65) != HREG(66))) || (HREG(69) == 0)) {
        if (actor->sfx != 0) {
            // Suppress continuous item SFX on player when in MM form. Skijer's NEI
            extern u8 TransformMasks_IsTransformed(void);
            if (!(actor->id == ACTOR_PLAYER && TransformMasks_IsTransformed() && (actor->sfx & 0xF800) == 0x1800)) {
                Actor_UpdateFlaggedAudio(actor);
            }
        }
    }

    // #region SOH [Enhancement] Extended culling updates
    bool shipShouldDraw = false;
    bool shipShouldUpdate = false;
    if ((HREG(64) != 1) || ((HREG(65) != -1) && (HREG(65) != HREG(66))) || (HREG(70) == 0)) {
        if (CVarGetInteger(CVAR_ENHANCEMENT("DisableDrawDistance"), 1) > 1 ||
            CVarGetInteger(CVAR_ENHANCEMENT("WidescreenActorCulling"), 0)) {
            Ship_CalcShouldDrawAndUpdate(play, actor, &actor->projectedPos, actor->projectedW, &shipShouldDraw,
                                         &shipShouldUpdate);

            if (shipShouldUpdate) {
                actor->flags |= ACTOR_FLAG_INSIDE_CULLING_VOLUME;
            } else {
                actor->flags &= ~ACTOR_FLAG_INSIDE_CULLING_VOLUME;
            }
        } else {
            if (Actor_CullingCheck(play, actor)) {
                actor->flags |= ACTOR_FLAG_INSIDE_CULLING_VOLUME;
            } else {
                actor->flags &= ~ACTOR_FLAG_INSIDE_CULLING_VOLUME;
            }
        }
    }

    actor->isDrawn = false;

    if ((HREG(64) != 1) || ((HREG(65) != -1) && (HREG(65) != HREG(66))) || (HREG(71) == 0)) {
        if ((actor->init == NULL) && (actor->draw != NULL) &&
            ((actor->flags & (ACTOR_FLAG_DRAW_CULLING_DISABLED | ACTOR_FLAG_INSIDE_CULLING_VOLUME)) ||
             shipShouldDraw)) {
            // #endregion
            if ((actor->flags & ACTOR_FLAG_REACT_TO_LENS) &&
                ((play->roomCtx.curRoom.lensMode == LENS_MODE_HIDE_ACTORS) || play->actorCtx.lensActive ||
                 play->actorCtx.lensFromLantern || (actor->room != play->roomCtx.curRoom.num))) {
                assert(*invisibleActorCounter < INVISIBLE_ACTOR_MAX);
                invisibleActors[*invisibleActorCounter] = actor;
                (*invisibleActorCounter)++;
            } else {
                if ((HREG(64) != 1) || ((HREG(65) != -1) && (HREG(65) != HREG(66))) || (HREG(72) == 0)) {
                    Actor_Draw(play, actor);
                    actor->isDrawn = true;
                }
            }
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
}
