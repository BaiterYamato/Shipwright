/* overlay-merge 2eca46e9c921 e5accf7ab3d1 a36b775525e3 */
void Actor_DrawAll(PlayState* play, ActorContext* actorCtx) {
    s32 invisibleActorCounter;
    Actor* invisibleActors[INVISIBLE_ACTOR_MAX];
    ActorListEntry* actorListEntry;
    Actor* actor;
    s32 i;

    invisibleActorCounter = 0;

    OPEN_DISPS(play->state.gfxCtx);

    // CEL-003: o estado de render dos mods é lido uma vez por frame e reusado no fechamento, para o
    // colchete nunca ficar desbalanceado. Sem mod as consultas devolvem zero e nada é emitido.
    const s32 toonActors = LinkSpan_RenderToonActorsEnabled();
    const s32 shadowReceivers = LinkSpan_RenderHasShadowReceivers();
    const s32 renderStateActive = LinkSpan_RenderStateActive();
    if (toonActors) {
        gSPToon(POLY_OPA_DISP++, true);
        gSPToon(POLY_XLU_DISP++, true);
    }

    // Alguns pisos são atores (ponte levadiça, plataformas). A lista de ids vem do mod; o host não fixa
    // política. Desenhados aqui, antes das luzes do mundo e da descarga das sombras, entram no depth buffer
    // como o cenário; o laço principal os pula, e cada um continua desenhado uma vez só.
    if (shadowReceivers) {
        static const u8 receiverCategories[] = { ACTORCAT_BG, ACTORCAT_PROP, ACTORCAT_SWITCH };
        for (i = 0; i < ARRAY_COUNT(receiverCategories); i++) {
            actorListEntry = &actorCtx->actorLists[receiverCategories[i]];
            for (actor = actorListEntry->head; actor != NULL; actor = actor->next) {
                if (LinkSpan_RenderIsShadowReceiver(actor->id)) {
                    Actor_DrawListEntry(play, actor, receiverCategories[i], invisibleActors, &invisibleActorCounter);
                }
            }
        }
    }

    LinkSpan_RenderWorldLights(play);
    if (shadowReceivers) {
        gSPToonShadowFlush(POLY_OPA_DISP++);
    }

    actorListEntry = &actorCtx->actorLists[0];

    for (i = 0; i < ARRAY_COUNT(actorCtx->actorLists); i++, actorListEntry++) {
        actor = actorListEntry->head;

        while (actor != NULL) {
            // Receptores já passaram pelo pré-passe, que só percorre BG, PROP e SWITCH; um id de outra categoria
            // registrado como receptor segue no laço normal em vez de sumir.
            if (shadowReceivers && (i == ACTORCAT_BG || i == ACTORCAT_PROP || i == ACTORCAT_SWITCH) &&
                LinkSpan_RenderIsShadowReceiver(actor->id)) {
                actor = actor->next;
                continue;
            }

            Actor_DrawListEntry(play, actor, i, invisibleActors, &invisibleActorCounter);

            actor = actor->next;
        }
    }

    if (toonActors) {
        gSPToon(POLY_OPA_DISP++, false);
        gSPToon(POLY_XLU_DISP++, false);
    } else if (renderStateActive) {
        // Sem o colchete toon não há borda de objeto depois do último ator: marca a borda para a captura de
        // sombra armada por um mod não vazar para a geometria iluminada seguinte (efeitos, XLU).
        gSPToonShadow(POLY_OPA_DISP++, 0, 0, 0, 0.0f);
    }

    if ((HREG(64) != 1) || (HREG(73) != 0)) {
        Effect_DrawAll(play->state.gfxCtx);
    }

    if ((HREG(64) != 1) || (HREG(74) != 0)) {
        EffectSs_DrawAll(play);
    }

    if ((HREG(64) != 1) || (HREG(72) != 0)) {
        // Skijer's NEI: lensFromLantern is the Poe-fire lantern's own lens; it is
        // independent of the Lens of Truth (no magic, no lensActive) so both can be on.
        if (play->actorCtx.lensActive || play->actorCtx.lensFromLantern) {
            // CEL-003: atores da lente desenham depois do fechamento do colchete; reabre em volta deles.
            if (toonActors) {
                gSPToon(POLY_OPA_DISP++, true);
                gSPToon(POLY_XLU_DISP++, true);
            }
            Actor_DrawLensActors(play, invisibleActorCounter, invisibleActors);
            if (toonActors) {
                gSPToon(POLY_OPA_DISP++, false);
                gSPToon(POLY_XLU_DISP++, false);
            }
            if ((play->csCtx.state != CS_STATE_IDLE) || Player_InCsMode(play)) {
                Actor_DisableLens(play);
            }
        }
    }

    Actor_DrawFaroresWindPointer(play);

    if (IREG(32) == 0) {
        Lights_DrawGlow(play);
    }

    if ((HREG(64) != 1) || (HREG(75) != 0)) {
        TitleCard_Draw(play, &actorCtx->titleCtx);
    }

    if ((HREG(64) != 1) || (HREG(76) != 0)) {
        CollisionCheck_DrawCollision(play, &play->colChkCtx);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}