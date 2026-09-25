/* overlay-merge 97f06aa79d78 878c804e3711 2a4af5667d07 */
void EnDntDemo_Judge(EnDntDemo* this, PlayState* play) {
    s16 delay;
    s16 reaction;
    s16 rand9;
    s16 maskIdx;
    s16 resultIdx;
    u8 ignore;
    s32 i;

    // Captain's Hat is an MM worn mask (player->currentMask stays NONE), so map
    // it onto PLAYER_MASK_SKULL here — same celebrate reaction, prize, flag and
    // rando check as showing the Skull Mask. Skijer's NEI
    s16 effMask = Player_GetMask(play);
    if (effMask == PLAYER_MASK_NONE && MmMaskWear_GetCurrent() == ITEM_MM_MASK_CAPTAIN) {
        effMask = PLAYER_MASK_SKULL;
    }

    if (this->leaderSignal != DNT_SIGNAL_NONE) {
        for (i = 0; i < 9; i++) {
            this->scrubs[i]->stageSignal = this->leaderSignal;
            this->scrubs[i]->action = this->action;
            this->scrubs[i]->stagePrize = DNT_PRIZE_NONE;
        }
        if (this->leader->isSolid) {
            this->leader->stageSignal = DNT_LEADER_SIGNAL_BURROW;
        }
        this->leaderSignal = DNT_SIGNAL_NONE;
        this->actionFunc = EnDntDemo_Results;
    } else if ((this->actor.xzDistToPlayer > 30.0f) || (effMask == 0)) {
        this->debugArrowTimer++;
        if (this->subCamera != SUBCAM_FREE) {
            this->subCamera = SUBCAM_FREE;
        }
        if (this->judgeTimer != 0) {
            for (i = 0; i < 9; i++) {
                this->scrubs[i]->stageSignal = DNT_SIGNAL_HIDE;
            }
            this->judgeTimer = 0;
        }
    } else {
        if ((effMask != 0) && (this->subCamera == SUBCAM_FREE)) {
            this->subCamera =
                OnePointCutscene_Init(play, 2220, -99, &this->scrubs[3]->actor, CAM_ID_MAIN); // was MAIN_CAM
        }
        this->debugArrowTimer = 0;
        if (this->judgeTimer == 40) {
            for (i = 0; i < 9; i++) {
                this->scrubs[i]->stageSignal = DNT_SIGNAL_LOOK;
            }
        }
        if (this->judgeTimer > 40) {
            // "gera gera" [onomatopoeia for loud giggling]
            osSyncPrintf(VT_FGCOL(RED) "☆☆☆☆☆ げらげら ☆☆☆☆☆ \n" VT_RST);
            func_800F436C(&this->actor.projectedPos, NA_SE_EV_CROWD - SFX_FLAG, 2.0f);
        }
        if (this->judgeTimer < 120) {
            this->judgeTimer++;
        } else {
            ignore = false;
            reaction = DNT_SIGNAL_NONE;
            delay = 0;
            switch (effMask) {
                case PLAYER_MASK_SKULL:
                    if (!Flags_GetItemGetInf(ITEMGETINF_OBTAINED_STICK_UPGRADE_FROM_STAGE)) {
                        reaction = DNT_SIGNAL_CELEBRATE;
                        this->prize = DNT_PRIZE_STICK;
                        Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_SARIA_THEME);
                        break;
                    }
                case PLAYER_MASK_TRUTH:
                    if (GameInteractor_Should(VB_DEKU_SCRUBS_REACT_TO_MASK_OF_TRUTH,
                                              !Flags_GetItemGetInf(ITEMGETINF_OBTAINED_NUT_UPGRADE_FROM_STAGE) &&
                                                  (effMask != PLAYER_MASK_SKULL))) {
                        Audio_PlaySfxGeneral(NA_SE_SY_TRE_BOX_APPEAR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                        this->prize = DNT_PRIZE_NUTS;
                        this->leader->stageSignal = DNT_LEADER_SIGNAL_UP;
                        reaction = DNT_SIGNAL_LOOK;
                        if (this->subCamera != SUBCAM_FREE) {
                            this->subCamera = SUBCAM_FREE;
                            reaction = DNT_SIGNAL_LOOK;
                            OnePointCutscene_Init(play, 2340, -99, &this->leader->actor, CAM_ID_MAIN);
                        }
                        break;
                    }
                case PLAYER_MASK_KEATON:
                case PLAYER_MASK_SPOOKY:
                case PLAYER_MASK_BUNNY:
                case PLAYER_MASK_GORON:
                case PLAYER_MASK_ZORA:
                case PLAYER_MASK_GERUDO:
                    rand9 = Rand_ZeroFloat(8.99f);
                    maskIdx = effMask;
                    maskIdx--;
                    if (rand9 == 8) {
                        ignore = true;
                        delay = 8;
                        reaction = DNT_SIGNAL_HIDE;
                        // "Special!"
                        osSyncPrintf(VT_FGCOL(GREEN) "☆☆☆☆☆ 特別！ ☆☆☆☆☆ \n" VT_RST);
                    } else {
                        if (maskIdx >= PLAYER_MASK_MAX - 1) {
                            // "This is dangerous!"
                            osSyncPrintf(VT_FGCOL(GREEN) "☆☆☆☆☆ ヤバいよこれ！ ☆☆☆☆☆ \n" VT_RST);
                            osSyncPrintf(VT_FGCOL(YELLOW) "☆☆☆☆☆ ヤバいよこれ！ ☆☆☆☆☆ \n" VT_RST);
                            osSyncPrintf(VT_FGCOL(PURPLE) "☆☆☆☆☆ ヤバいよこれ！ ☆☆☆☆☆ \n" VT_RST);
                            osSyncPrintf(VT_FGCOL(CYAN) "☆☆☆☆☆ ヤバいよこれ！ ☆☆☆☆☆ \n" VT_RST);
                            maskIdx = Rand_ZeroFloat(7.99f);
                        }

                        resultIdx = sResultTable[rand9][maskIdx];
                        reaction = sResultValues[resultIdx][0];
                        this->action = sResultValues[resultIdx][1];
                        switch (this->action) {
                            case DNT_ACTION_LOW_RUPEES:
                                Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_COURTYARD);
                                break;
                            case DNT_ACTION_ATTACK:
                                if (this->subCamera != SUBCAM_FREE) {
                                    this->subCamera = SUBCAM_FREE;
                                    OnePointCutscene_Init(play, 2350, -99, &this->scrubs[3]->actor, CAM_ID_MAIN);
                                }
                                Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_ENEMY | 0x800);
                                break;
                            case DNT_ACTION_DANCE:
                                Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_SHOP);
                                break;
                        }
                        osSyncPrintf("\n\n");
                        // "Each index 1"
                        osSyncPrintf(VT_FGCOL(GREEN) "☆☆☆☆☆ 各インデックス１ ☆☆☆☆☆ %d\n" VT_RST, rand9);
                        // "Each index 2"
                        osSyncPrintf(VT_FGCOL(GREEN) "☆☆☆☆☆ 各インデックス２ ☆☆☆☆☆ %d\n" VT_RST, maskIdx);
                        // "Each index 3"
                        osSyncPrintf(VT_FGCOL(GREEN) "☆☆☆☆☆ 各インデックス３ ☆☆☆☆☆ %d\n" VT_RST, resultIdx);
                        osSyncPrintf("\n");
                        // "What kind of evaluation?"
                        osSyncPrintf(VT_FGCOL(YELLOW) "☆☆☆☆☆ どういう評価？  ☆☆☆☆☆☆ %d\n" VT_RST, reaction);
                        // "What kind of action?"
                        osSyncPrintf(VT_FGCOL(PURPLE) "☆☆☆☆☆ どういうアクション？  ☆☆☆ %d\n" VT_RST, this->action);
                        osSyncPrintf("\n\n");
                        break;
                    }
            }
            if (reaction != DNT_SIGNAL_NONE) {
                for (i = 0; i < 9; i++) {
                    if (delay != 0) {
                        this->scrubs[i]->timer3 = delay * i;
                    }
                    this->scrubs[i]->action = this->action;
                    this->scrubs[i]->stageSignal = reaction;
                    this->scrubs[i]->ignore = ignore;
                    if (this->prize != DNT_PRIZE_NONE) {
                        this->scrubs[i]->timer1 = 300;
                        this->scrubs[i]->stagePrize = this->prize;
                        this->scrubs[i]->targetPos = this->leader->actor.world.pos;
                        if (this->prize == DNT_PRIZE_NUTS) {
                            this->leader->stageSignal = DNT_LEADER_SIGNAL_UP;
                        }
                        if (this->prize == DNT_PRIZE_STICK) {
                            this->leader->timer = 300;
                        }
                    }
                }
                this->actionFunc = EnDntDemo_Results;
            }
        }
    }
}