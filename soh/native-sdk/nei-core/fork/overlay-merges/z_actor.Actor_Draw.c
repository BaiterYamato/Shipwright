/* overlay-merge 5d18d0994f9c 846e03dd22ca 9682d3716f49 */
void Actor_Draw(PlayState* play, Actor* actor) {
    FaultClient faultClient;
    Lights* lights;

    Fault_AddClient(&faultClient, Actor_FaultPrint, actor, "Actor_draw");

    FrameInterpolation_RecordOpenChild(actor, 0);
    OPEN_DISPS(play->state.gfxCtx);

    lights = LightContext_NewLights(&play->lightCtx, play->state.gfxCtx);

    Lights_BindAll(lights, play->lightCtx.listHead,
                   (actor->flags & ACTOR_FLAG_IGNORE_POINTLIGHTS) ? NULL : &actor->world.pos);
    Lights_Draw(lights, play->state.gfxCtx);

    // CEL-003: mesma posição do fork, depois de escolher as luzes do ator e
    // antes do draw. Sem hook registrado a função só consulta a flag do ponto.
    LinkSpan_RenderActorDraw(actor, play);

    FrameInterpolation_RecordActorPosRotMatrix();
    if (actor->flags & ACTOR_FLAG_IGNORE_QUAKE) {
        Matrix_SetTranslateRotateYXZ(
            actor->world.pos.x + play->mainCamera.skyboxOffset.x,
            actor->world.pos.y + (f32)((actor->shape.yOffset * actor->scale.y) + play->mainCamera.skyboxOffset.y),
            actor->world.pos.z + play->mainCamera.skyboxOffset.z, &actor->shape.rot);
    } else {
        Matrix_SetTranslateRotateYXZ(actor->world.pos.x, actor->world.pos.y + (actor->shape.yOffset * actor->scale.y),
                                     actor->world.pos.z, &actor->shape.rot);
    }

    Matrix_Scale(actor->scale.x, actor->scale.y, actor->scale.z, MTXMODE_APPLY);
    Actor_SetObjectDependency(play, actor);

    gSPSegment(POLY_OPA_DISP++, 0x06, play->objectCtx.status[actor->objBankIndex].segment);
    gSPSegment(POLY_XLU_DISP++, 0x06, play->objectCtx.status[actor->objBankIndex].segment);

    if (actor->colorFilterTimer != 0) {
        Color_RGBA8 color = { 0, 0, 0, 255 };

        if (actor->colorFilterParams & 0x8000) {
            color.r = color.g = color.b = ((actor->colorFilterParams & 0x1F00) >> 5) | 7;
        } else if (actor->colorFilterParams & 0x4000) {
            color.r = ((actor->colorFilterParams & 0x1F00) >> 5) | 7;
        } else {
            color.b = ((actor->colorFilterParams & 0x1F00) >> 5) | 7;
        }

        if (actor->colorFilterParams & 0x2000) {
            func_80026860(play, &color, actor->colorFilterTimer, actor->colorFilterParams & 0xFF);
        } else {
            func_80026400(play, &color, actor->colorFilterTimer, actor->colorFilterParams & 0xFF);
        }
    }

    {
        // Phantom Hourglass: the recall drains everything but Link and its target to grey. Skijer's NEI
        // (Link-Span: o cinza envolve o LinkSpan_ActorDraw do host, que chama actor->draw.)
        extern u8 Hourglass_ShouldDrawGray(Actor * actor);
        extern void Hourglass_PushGray(PlayState * play);
        extern void Hourglass_PopGray(PlayState * play);
        u8 recallGray = Hourglass_ShouldDrawGray(actor);

        if (recallGray) {
            Hourglass_PushGray(play);
        }
        LinkSpan_ActorDraw(actor, play);
        if (recallGray) {
            Hourglass_PopGray(play);
        }
    }

    if (actor->colorFilterTimer != 0) {
        if (actor->colorFilterParams & 0x2000) {
            func_80026A6C(play);
        } else {
            func_80026608(play);
        }
    }

    if (actor->shape.shadowDraw != NULL) {
        actor->shape.shadowDraw(actor, lights, play);
    }

    // VB_ACTOR_POST_DRAW: subscribers (e.g. Harpoon's Triforce Thief carrier
    // indicator) can draw extra geometry attached to this actor after its
    // own draw + shadow pass.
    GameInteractor_Should(VB_ACTOR_POST_DRAW, true, play, actor);

    CLOSE_DISPS(play->state.gfxCtx);
    FrameInterpolation_RecordCloseChild();

    Fault_RemoveClient(&faultClient);
}