/* overlay-merge b8e7def2d444 3ecb0187a922 54109b4f1182 */
void KaleidoScope_DrawItemCycles(PlayState* play) {
    // draw the mask select
    // mask-select overlay only on the vanilla item page (0) — pages 1/2 show custom items / masks. Skijer's NEI
    // IS_RANDO term from upstream, same reasoning as the input handler above.
    if (ExtInv_GetCurrentPage() == 0)
        KaleidoScope_DrawItemCycleExtras(play, SLOT_TRADE_CHILD, IS_RANDO || CanMaskSelect(),
                                         IS_RANDO ? Randomizer_GetPrevChildTradeItem()
                                                  : (INV_CONTENT(ITEM_TRADE_CHILD) <= ITEM_MASK_KEATON ||
                                                             INV_CONTENT(ITEM_TRADE_CHILD) > ITEM_MASK_TRUTH
                                                         ? ITEM_MASK_TRUTH
                                                         : INV_CONTENT(ITEM_TRADE_CHILD) - 1),
                                         IS_RANDO ? Randomizer_GetNextChildTradeItem()
                                                  : (INV_CONTENT(ITEM_TRADE_CHILD) >= ITEM_MASK_TRUTH ||
                                                             INV_CONTENT(ITEM_TRADE_CHILD) < ITEM_MASK_KEATON
                                                         ? ITEM_MASK_KEATON
                                                         : INV_CONTENT(ITEM_TRADE_CHILD) + 1));

    // draw the adult trade select — only on the vanilla item page (0), like the mask select above. Skijer's NEI
    if (ExtInv_GetCurrentPage() == 0) {
        u16 tradeCur = ExtInv_GetSlotItem(SLOT_TRADE_ADULT);
        KaleidoScope_DrawItemCycleExtras(play, SLOT_TRADE_ADULT, TradeAdult_OwnedCount() > 1,
                                         TradeAdult_PrevItem(tradeCur), TradeAdult_NextItem(tradeCur));
    }

    // Bottle Randomizer wheels A/B draw (Skijer's NEI) — page 1 only (see HandleItemCycles). Previews
    // are the prev/next SLOT (index-based) with forceShow, so the mini-icons + A indicator appear even
    // between identical EMPTY bottles.
    if (ExtInv_GetCurrentPage() == 0) {
        KaleidoScope_DrawItemCycleExtrasImpl(play, SLOT_BOTTLE_1, Bottle_WheelBottleCount(BOTTLE_WHEEL_A) > 1,
                                             Bottle_WheelPeek(BOTTLE_WHEEL_A, -1), Bottle_WheelPeek(BOTTLE_WHEEL_A, 1),
                                             true);
        KaleidoScope_DrawItemCycleExtrasImpl(play, SLOT_BOTTLE_2, Bottle_WheelBottleCount(BOTTLE_WHEEL_B) > 1,
                                             Bottle_WheelPeek(BOTTLE_WHEEL_B, -1), Bottle_WheelPeek(BOTTLE_WHEEL_B, 1),
                                             true);
    }

    // Draw Nayru's Love/Roc's Feather
    KaleidoScope_DrawItemCycleExtras(play, SLOT_NAYRUS_LOVE, Randomizer_GetSettingValue(RSK_ROCS_FEATHER),
                                     Enhancement_GetPrevNayrusItem(), Enhancement_GetNextNayrusItem());

    // Draw Farore's Wind/Rito Mask (see HandleItemCycles — page 0 only). Skijer's NEI
    if (ExtInv_GetCurrentPage() == 0) {
        KaleidoScope_DrawItemCycleExtras(play, SLOT_FARORES_WIND, RitoItem_CanCycle(), RitoItem_OtherItem(),
                                         RitoItem_OtherItem());
    }

    // Draw Gust Jar element indicator
    GustJar_DrawElementCycle(play);

    // Draw Bow/Slingshot elemental-arrow wheel overlay
    ArrowWheel_Draw(play);

    // Draw Lantern fire-type selector overlay (only when active)
    Lantern_DrawKaleidoSelector(play);

    // Dual Cane cane-type toggle overlay.
    Cane_DrawKaleidoSelector(play);

    // Shovel <-> Dominion Rod overlay (shared cell 46). Skijer's NEI
    Shovel_DrawKaleidoSelector(play);

    // Elemental Wand rod selector overlay. Skijer's NEI
    Wand_DrawKaleidoSelector(play);

    // Sheikah Slate rune selector overlay. Skijer's NEI
    Slate_DrawKaleidoSelector(play);

    // Draw Twilight Upgrade mode toggles (Clawshot + Gale Boomerang)
    Clawshot_DrawKaleidoSelector(play);
    Gale_DrawKaleidoSelector(play);

    // Pictograph Box flip overlay on the Lens of Truth slot. Skijer's NEI
    Picto_DrawKaleidoSelector(play);

    // Power Keg flip overlay on the Bomb slot. Skijer's NEI
    PowerKeg_DrawKaleidoSelector(play);

    // Draw Gust Jar press-A selector (Roc's Feather visual)
    GustJar_DrawPressASelector(play);

    // Draw Bow / Slingshot press-A selector (Roc's Feather visual)
    ArrowWheel_DrawPressA(play);
}