/* overlay-merge 5b14b3744286 1a9a055084cd 7572914f432f */
void AudioOcarina_PlaybackSong(void) {
    u32 noteTimerStep;
    u32 nextNoteTimerStep = 0;

    if (sPlaybackState != 0) {
        if (sPlaybackStaffPos == 0) {
            noteTimerStep = 3;
        } else {
            noteTimerStep = sOcarinaUpdateTaskStart - sOcarinaPlaybackTaskStart;
        }

        if (noteTimerStep < sPlaybackNoteTimer) {
            sPlaybackNoteTimer -= noteTimerStep;
        } else {
            nextNoteTimerStep = noteTimerStep - sPlaybackNoteTimer;
            sPlaybackNoteTimer = 0;
        }

        if (sPlaybackNoteTimer == 0) {

            sPlaybackNoteTimer = sPlaybackSong[sPlaybackNotePos].length;

            if (sPlaybackNotePos == 1) {
                sPlaybackNoteTimer++;
            }

            if (sPlaybackNoteTimer == 0) {
                sPlaybackState--;
                if (sPlaybackState != 0) {
                    sPlaybackNotePos = 0;
                    sPlaybackStaffPos = 0;
                    sPlaybackPitch = OCARINA_PITCH_NONE;
                } else {
                    Audio_StopSfxById(NA_SE_OC_OCARINA);
                    GameInteractor_ExecuteOnOcarinaPlaybackNote(OCARINA_PITCH_NONE, 1.0f);
                }
                return;
            } else {
                sPlaybackNoteTimer -= nextNoteTimerStep;
            }

            // Update volume
            if (sNotePlaybackVolume != sPlaybackSong[sPlaybackNotePos].volume) {
                sNotePlaybackVolume = sPlaybackSong[sPlaybackNotePos].volume;
                sRelativeNotePlaybackVolume = sNotePlaybackVolume / 127.0f;
            }

            // Update vibrato
            if (sNotePlaybackVibrato != sPlaybackSong[sPlaybackNotePos].vibrato) {
                sNotePlaybackVibrato = sPlaybackSong[sPlaybackNotePos].vibrato;
                Audio_QueueCmdS8(0x6 << 24 | SEQ_PLAYER_SFX << 16 | 0xD06, sNotePlaybackVibrato);
            }

            // Update bend
            if (sNotePlaybackBend != sPlaybackSong[sPlaybackNotePos].bend) {
                sNotePlaybackBend = sPlaybackSong[sPlaybackNotePos].bend;
                sRelativeNotePlaybackBend = AudioOcarina_BendPitchTwoSemitones(sNotePlaybackBend);
            }

            // No changes in volume, vibrato, or bend between notes
            if ((sPlaybackSong[sPlaybackNotePos].volume == sPlaybackSong[sPlaybackNotePos - 1].volume &&
                 (sPlaybackSong[sPlaybackNotePos].vibrato == sPlaybackSong[sPlaybackNotePos - 1].vibrato) &&
                 (sPlaybackSong[sPlaybackNotePos].bend == sPlaybackSong[sPlaybackNotePos - 1].bend))) {
                sPlaybackPitch = 0xFE;
            }

            if (sPlaybackPitch != sPlaybackSong[sPlaybackNotePos].pitch) {
                u8 pitch = sPlaybackSong[sPlaybackNotePos].pitch;

                // As bFlat4 is exactly in the middle of notes B & A, a flag is
                // added to the pitch to resolve which button to map Bflat4 to
                if (pitch == OCARINA_PITCH_BFLAT4) {
                    sPlaybackPitch = pitch + sPlaybackSong[sPlaybackNotePos].bFlat4Flag;
                } else {
                    sPlaybackPitch = pitch;
                }

                if (sPlaybackPitch != OCARINA_PITCH_NONE) {
                    sPlaybackStaffPos++;
                    // Sets ocarina instrument Id to channelIndex io port 7, which is used
                    // as an index in seq 0 to get the true instrument Id
                    Audio_QueueCmdS8(0x6 << 24 | SEQ_PLAYER_SFX << 16 | 0xD07, sOcarinaInstrumentId - 1);
                    // Sets sPlaybackPitch to channelIndex io port 5
                    Audio_QueueCmdS8(0x6 << 24 | SEQ_PLAYER_SFX << 16 | 0xD05, sPlaybackPitch & 0x3F);
                    Audio_PlaySfxGeneral(NA_SE_OC_OCARINA, &gSfxDefaultPos, 4, &sRelativeNotePlaybackBend,
                                         &sRelativeNotePlaybackVolume, &gSfxDefaultReverb);
                    GameInteractor_ExecuteOnOcarinaPlaybackNote(sPlaybackPitch & 0x3F, sRelativeNotePlaybackBend);
                } else {
                    Audio_StopSfxById(NA_SE_OC_OCARINA);
                    GameInteractor_ExecuteOnOcarinaPlaybackNote(OCARINA_PITCH_NONE, 1.0f);
                }
            }
            sPlaybackNotePos++;
        }
    }
}