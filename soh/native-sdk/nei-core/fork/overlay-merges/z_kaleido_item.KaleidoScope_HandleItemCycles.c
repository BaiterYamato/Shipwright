/* overlay-merge 98b4d1bd8db1 03534b34f1e7 98ac8251f68a */
void KaleidoScope_HandleItemCycles(PlayState* play) {
    // handle the mask select — only on the vanilla item page (0); on pages 1/2 the cell holds a
    // custom item / MM mask, so the wheel must not respond there (same as the bottle wheels).
    // Skijer's NEI. The IS_RANDO term is upstream's: in rando the wheel stays usable even when
    // CanMaskSelect() says no, and that has to keep working on page 0.
    if (ExtInv_GetCurrentPage() == 0)
        KaleidoScope_HandleItemCycleExtras(play, SLOT_TRADE_CHILD, IS_RANDO || CanMaskSelect(),
                                           IS_RANDO ? Randomizer_GetPrevChildTradeItem()
                                                    : (INV_CONTENT(ITEM_TRADE_CHILD) <= ITEM_MASK_KEATON ||
                                                               INV_CONTENT(ITEM_TRADE_CHILD) > ITEM_MASK_TRUTH
                                                           ? ITEM_MASK_TRUTH
                                                           : INV_CONTENT(ITEM_TRADE_CHILD) - 1),
                                           IS_RANDO ? Randomizer_GetNextChildTradeItem()
                                                    : (INV_CONTENT(ITEM_TRADE_CHILD) >= ITEM_MASK_TRUTH ||
                                                               INV_CONTENT(ITEM_TRADE_CHILD) < ITEM_MASK_KEATON
                                                           ? ITEM_MASK_KEATON
                                                           : INV_CONTENT(ITEM_TRADE_CHILD) + 1),
                                           true);

    // handle the adult trade select
    // Adult-trade wheel: the shared linear cycle wheel, fed with EVERY owned trade item — a held
    // vanilla/rando item folded in, plus the granted MM ones — so the one wheel cycles them all (A opens,
    // stick L/R cycles). Only on the vanilla item page (0) — on pages 1/2 the cell is a custom item /
    // MM mask, so the wheel must not respond there (same as the bottle wheels). Skijer's NEI
    if (ExtInv_GetCurrentPage() == 0) {
        u16 tradeCur = ExtInv_GetSlotItem(SLOT_TRADE_ADULT);
        TradeAdult_FoldCurrent(tradeCur);
        // The child chain's non-mask items (Weird Egg / Cucco / Zelda's Letter, trade indices 20-22)
        // live in SLOT_TRADE_CHILD, which this wheel never reads — so nothing was ever setting their
        // ownership bits and they could not reach the unified wheel (or MM). Fold them from their own
        // slot. Masks in that slot are ignored: TradeAdult_IndexOfItem returns -1 for them.
        // Skijer 2026-07-30
        TradeAdult_FoldCurrent(ExtInv_GetSlotItem(SLOT_TRADE_CHILD));
        if (tradeCur == ITEM_NONE && TradeAdult_OwnedCount() > 0) {
            tradeCur = TradeAdult_ItemId(TradeAdult_OwnedAt(0)); // show the first owned item in an empty slot
            ExtInv_SetSlotItem(SLOT_TRADE_ADULT, tradeCur);
        }
        KaleidoScope_HandleItemCycleExtras(play, SLOT_TRADE_ADULT, TradeAdult_OwnedCount() > 1,
                                           TradeAdult_PrevItem(tradeCur), TradeAdult_NextItem(tradeCur), true);
    }

    // Bottle Randomizer wheels A/B (Skijer's NEI). Wheel A holds bottleSlots[0..3], Wheel B holds
    // bottleSlots[4..7] (the bottle inventory edited in the save editor). Show a real bottle from
    // that wheel in the visible slot, then reuse the trade-slot cycler to swap among them. Only
    // syncs when the wheel actually has bottles, so vanilla bottles are untouched when the rando
    // isn't active. ONLY on page 1 — on pages 2/3 the visual bottle cells are different items
    // (custom items / MM masks), so the wheel must not run there.
    if (ExtInv_GetCurrentPage() == 0) {
        // Index-based selectors so every bottle (empty ones included) is reachable, always cyclable.
        Bottle_WheelHandle(play, BOTTLE_WHEEL_A, SLOT_BOTTLE_1);
        Bottle_WheelHandle(play, BOTTLE_WHEEL_B, SLOT_BOTTLE_2);
    }

    // Handle Nayru's Love/Roc's Feather
    KaleidoScope_HandleItemCycleExtras(play, SLOT_NAYRUS_LOVE, Randomizer_GetSettingValue(RSK_ROCS_FEATHER),
                                       Enhancement_GetPrevNayrusItem(), Enhancement_GetNextNayrusItem(), true);

    // Handle Farore's Wind/Rito Mask — same idea, page 0 only (on pages 1/2 that
    // grid position is a different item entirely). Skijer's NEI
    if (ExtInv_GetCurrentPage() == 0) {
        RitoItem_SyncCell(); // records what this file owns, seeds an empty cell
        KaleidoScope_HandleItemCycleExtras(play, SLOT_FARORES_WIND, RitoItem_CanCycle(), RitoItem_OtherItem(),
                                           RitoItem_OtherItem(), true);
    }

    // Handle Gust Jar element cycle
    GustJar_HandleElementCycle(play);

    // Handle Bow/Slingshot elemental-arrow wheel (hold-C on bow or slingshot)
    ArrowWheel_Handle(play);

    // Handle Lantern fire-type selector (press A on lantern → stick L/R picks
    // between captured types + Vacía, press A confirms / B cancels)
    Lantern_HandleKaleidoSelector(play);

    // Dual Cane cane-type toggle (A on the cell) — Somaria <-> Pacci.
    Cane_HandleKaleidoSelector(play);

    // Shovel <-> Dominion Rod (shared cell 46; also folds pre-re-layout saves). Skijer's NEI
    Shovel_HandleKaleidoSelector(play);

    // Elemental Wand rod selector (A on the cell) — six rods share the page-2 slot the Bomb Arrows
    // used to occupy. Skijer's NEI
    Wand_HandleKaleidoSelector(play);

    // Sheikah Slate rune selector (A on the cell) — four runes share the slate cell. Skijer's NEI
    Slate_HandleKaleidoSelector(play);

    // Twilight Upgrade mode toggles — A on hookshot/longshot (Clawshot) or
    // boomerang (Gale Boomerang) opens a 2-slot selector. Gated by the
    // corresponding twilightUpgrade bit so the toggles stay hidden until the
    // player obtains the upgrade.
    Clawshot_HandleKaleidoSelector(play);
    Gale_HandleKaleidoSelector(play);

    // Pictograph Box flip on the Lens of Truth slot (A → Lens ↔ Pictobox). Skijer's NEI
    Picto_HandleKaleidoSelector(play);

    // Power Keg flip on the Bomb slot (A → Bomb ↔ Power Keg). Skijer's NEI
    PowerKeg_HandleKaleidoSelector(play);

    // Gust Jar press-A element selector (Roc's Feather style — coexists with
    // the hold-C wheel below).
    GustJar_HandlePressASelector(play);

    // Bow / Slingshot press-A arrow selector (Roc's Feather style — coexists
    // with the hold-C arrow wheel below).
    ArrowWheel_HandlePressA(play);
}