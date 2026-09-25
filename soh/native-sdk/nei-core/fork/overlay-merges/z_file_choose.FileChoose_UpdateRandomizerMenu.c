/* overlay-merge c1c3a09aecf4 4f6210db9b84 13942a100c7b */
void FileChoose_UpdateRandomizerMenu(GameState* thisx) {
    FileChoose_UpdateStickDirectionPromptAnim(thisx);
    FileChooseContext* this = (FileChooseContext*)thisx;
    Input* input = &this->state.input[0];
    bool dpad = CVarGetInteger(CVAR_SETTING("DpadInText"), 0);
    // COMBO (OoTxMM) reuses this menu with a 4th option (Load Combo Seed); rando keeps its 3.
    bool isCombo = (this->questType[this->buttonIndex] == QUEST_OOTXMM);
    uint8_t lastOpt = isCombo ? CBO_OPEN_SETTINGS : RSM_OPEN_RANDOMIZER_SETTINGS;

    FileChoose_UpdateRandomizer();

    // The combo generator runs on its own thread, so treat it as "generating" too (grays options /
    // blocks input while it works).
    if (generating || FleetComboFS_IsBusy()) {
        return;
    }

    // Fade in elements after opening Randomizer options menu
    this->randomizerUIAlpha += 25;
    if (this->randomizerUIAlpha > 255) {
        this->randomizerUIAlpha = 255;
    }

    // Move menu selection up or down.
    if (ABS(this->stickRelY) > 30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DDOWN | BTN_DUP))) {
        // Move down
        if (this->stickRelY < -30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DDOWN))) {
            // When selecting past the last option, cycle back to the first option.
            if ((this->randomizerIndex + 1) > lastOpt) {
                this->randomizerIndex = RSM_START_RANDOMIZER;
            } else {
                this->randomizerIndex++;
            }
        } else if (this->stickRelY > 30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DUP))) {
            // When selecting past the first option, cycle back to the last option and offset the list to view it
            // properly.
            if ((this->randomizerIndex - 1) < RSM_START_RANDOMIZER) {
                this->randomizerIndex = lastOpt;
            } else {
                this->randomizerIndex--;
            }
        }

        GameInteractor_ExecuteOnUpdateFileRandomizerOptionSelection(this->randomizerIndex);

        Audio_PlaySfxGeneral(NA_SE_SY_FSEL_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }

    // COMBO: on the "Load Combo Seed" row, stick left/right picks which .fleet to load.
    if (isCombo && this->randomizerIndex == CBO_LOAD_SEED) {
        int cnt = FleetComboFS_FleetCount();
        if (cnt > 0 &&
            (ABS(this->stickRelX) > 30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DLEFT | BTN_DRIGHT)))) {
            if (this->stickRelX > 30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DRIGHT))) {
                sComboFleetIdx = (sComboFleetIdx + 1) % cnt;
            } else {
                sComboFleetIdx = (sComboFleetIdx + cnt - 1) % cnt;
            }
            Audio_PlaySoundGeneral(NA_SE_SY_FSEL_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
    }

    if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
        this->configMode = CM_RANDOMIZER_SETTINGS_MENU_TO_QUEST;
        return;
    }

    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        if (this->randomizerIndex == RSM_START_RANDOMIZER) {
            if (Randomizer_IsSeedGenerated() || Randomizer_IsSpoilerLoaded()) {
                SohFileSelect_ShowPresetModal();
                Audio_PlaySfxGeneral(NA_SE_SY_FSEL_DECIDE_L, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                     &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);

                this->prevConfigMode = this->configMode;
                this->configMode = CM_ROTATE_TO_NAME_ENTRY;
                CVarSetInteger(CVAR_GENERAL("OnFileSelectNameEntry"), 1);
            } else {
                Sfx_PlaySfxCentered(NA_SE_SY_OCARINA_ERROR);
            }
        } else if (this->randomizerIndex == RSM_GENERATE_RANDOMIZER) {
            if (isCombo) {
                FleetComboFS_Generate(); // combo seed generation (own thread; marks Rando ctx seed-generated on finish)
            } else {
                Randomizer_GenerateRandomizer();
            }
        } else if (this->randomizerIndex == RSM_OPEN_RANDOMIZER_SETTINGS) {
            // index 2: rando = Open Settings; COMBO = Load the picked .fleet SEED-ONLY (Start then
            // bakes it into this file-select slot).
            Audio_PlaySfxGeneral(NA_SE_SY_FSEL_DECIDE_L, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            if (isCombo) {
                FleetComboFS_LoadSeedIndex(sComboFleetIdx);
            } else {
                Randomizer_ShowRandomizerMenu();
            }
        } else if (isCombo && this->randomizerIndex == CBO_OPEN_SETTINGS) {
            // COMBO index 3: open the shared combo settings (knobs / advanced .fleet management).
            Audio_PlaySfxGeneral(NA_SE_SY_FSEL_DECIDE_L, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            FleetComboFS_OpenSettings();
        }
    }
}