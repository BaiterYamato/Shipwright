/* overlay-merge 4135ebc004ba 75a96dd67527 b61a7aa470c2 */
void FileChoose_UpdateQuestMenu(GameState* thisx) {
    FileChoose_UpdateStickDirectionPromptAnim(thisx);
    FileChooseContext* this = (FileChooseContext*)thisx;
    Input* input = &this->state.input[0];
    s8 i = 0;
    bool dpad = CVarGetInteger(CVAR_SETTING("DpadInText"), 0);
    void* defaultName;

    FileChoose_UpdateRandomizer();

    // #region SOH [Enhancement] - Hide Quest Modes
    // If the current quest type was hidden after being selected (i.e., CVar changed while on the quest menu), advance
    // to the next visible one.
    if (CountVisibleQuests() > 0) {
        while (IsQuestSkipped(this->questType[this->buttonIndex])) {
            this->questType[this->buttonIndex]++;
            if (this->questType[this->buttonIndex] > MAX_QUEST) {
                this->questType[this->buttonIndex] = MIN_QUEST;
            }
        }
    }
    // #endregion

    // #region SOH [Enhancement] - Hide Quest Modes
    if (CountVisibleQuests() > 1 && ABS(this->stickRelX) > 30 ||
        (dpad && CHECK_BTN_ANY(input->press.button, BTN_DLEFT | BTN_DRIGHT))) {
        // Cycle through quest types, skipping any that are hidden (i.e., Master Quest without O2R,
        // Randomizer/Boss Rush when their CVars are set).  Wraps around if past min/max.
        if (this->stickRelX > 30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DRIGHT))) {
            do {
                this->questType[this->buttonIndex]++;
                if (this->questType[this->buttonIndex] > MAX_QUEST) {
                    this->questType[this->buttonIndex] = MIN_QUEST;
                }
            } while (IsQuestSkipped(this->questType[this->buttonIndex]));
        } else if (this->stickRelX < -30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DLEFT))) {
            do {
                this->questType[this->buttonIndex]--;
                if (this->questType[this->buttonIndex] < MIN_QUEST) {
                    this->questType[this->buttonIndex] = MAX_QUEST;
                }
            } while (IsQuestSkipped(this->questType[this->buttonIndex]));
        }
        // #endregion

        Audio_PlaySfxGeneral(NA_SE_SY_FSEL_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);

        GameInteractor_ExecuteOnUpdateFileQuestSelection(this->questType[this->buttonIndex]);
    }

    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        gSaveContext.ship.quest.id = this->questType[this->buttonIndex];

        if (this->questType[this->buttonIndex] == QUEST_BOSSRUSH) {
            Audio_PlaySfxGeneral(NA_SE_SY_FSEL_DECIDE_L, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            this->prevConfigMode = this->configMode;
            this->configMode = CM_ROTATE_TO_BOSS_RUSH_MENU;
            return;
        } else if (this->questType[this->buttonIndex] == QUEST_RANDOMIZER ||
                   this->questType[this->buttonIndex] == QUEST_OOTXMM) {
            // COMBO reuses the randomizer settings sub-screen (Start / Generate / Load Combo Seed /
            // Open Settings); the actions branch on the quest type inside FileChoose_UpdateRandomizerMenu.
            if (this->questType[this->buttonIndex] == QUEST_OOTXMM) {
                FleetComboFS_RefreshFleets(); // scan the fleet/ folder once for the Load Combo Seed picker
                sComboFleetIdx = 0;
            }
            Audio_PlaySfxGeneral(NA_SE_SY_FSEL_DECIDE_L, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            this->prevConfigMode = this->configMode;
            this->configMode = CM_ROTATE_TO_RANDOMIZER_SETTINGS_MENU;
        } else {
            Audio_PlaySfxGeneral(NA_SE_SY_FSEL_DECIDE_L, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            osSyncPrintf("Selected Dungeon Quest: %d\n", IS_MASTER_QUEST);
            this->prevConfigMode = this->configMode;
            this->configMode = CM_ROTATE_TO_NAME_ENTRY;
            this->logoAlpha = 0;
            return;
        }
    }

    if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
        this->configMode = CM_QUEST_TO_MAIN;
        sLastFileChooseButtonIndex = -1;
        return;
    }
}