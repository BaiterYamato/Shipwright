/* overlay-extra 3e19e039a3d1 da39a3ee5e6b da39a3ee5e6b */
static void LinkSpan_DrawEquipChoice(PlayState* play, u8 choice) {
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_42Opa(play->state.gfxCtx);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    for (u8 button = 1; button <= 3; ++button) {
        const s16 x = 112 + (button - 1) * 32;
        const s16 y = 176;
        const u8 selected = button == choice;
        const u8 item = gSaveContext.equips.buttonItems[button];
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, selected ? 255 : 180, selected ? 230 : 180,
                        selected ? 70 : 180, 255);
        gDPLoadTextureBlock(POLY_OPA_DISP++, gEquippedItemOutlineTex, G_IM_FMT_IA, G_IM_SIZ_8b, 32, 32, 0,
                               G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                               G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        gSPTextureRectangle(POLY_OPA_DISP++, (x - 4) << 2, (y - 4) << 2, (x + 28) << 2, (y + 28) << 2,
                            G_TX_RENDERTILE, 0, 0, 1024, 1024);
        if (item != ITEM_NONE && gItemIcons[item] != NULL) {
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255);
            gDPLoadTextureBlock(POLY_OPA_DISP++, gItemIcons[item], G_IM_FMT_RGBA, G_IM_SIZ_32b, 32, 32, 0,
                                G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                                G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            gSPTextureRectangle(POLY_OPA_DISP++, x << 2, y << 2, (x + 24) << 2, (y + 24) << 2,
                                G_TX_RENDERTILE, 0, 0, 1365, 1365);
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
}
