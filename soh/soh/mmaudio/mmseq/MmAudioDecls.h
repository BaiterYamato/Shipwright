#pragma once

// Declarações cruzadas entre os arquivos de áudio portados do MM.
//
// GERADO por link-span/tools/port_mm_audio_file.py — NÃO editar à mão.
//
// Existe porque os arquivos do decomp se chamam entre si sem header comum: no
// MM as declarações vinham de global.h, que aqui não pode entrar. Extrair as
// assinaturas em vez de listá-las à mão evita que a lista envelheça em silêncio
// quando um arquivo for reportado.

#include "soh/mmaudio/mmseq/MmAudioTypes.h"

namespace mmsfx {

// ---- globais ----
extern EnvelopePoint gDefaultEnvelope[];
extern NoteSampleState gDefaultSampleState;
extern NoteSampleState gZeroedSampleState;
extern f32 gBendPitchOneOctaveFrequencies[];
extern f32 gBendPitchTwoSemitonesFrequencies[];
extern f32 gDefaultPanVolume[];
extern f32 gHeadsetPanVolume[];
extern f32 gPitchFrequencies[];
extern f32 gStereoPanVolume[];
extern s16 gInvalidAdpcmCodeBook[];
extern u16 gHaasEffectDelaySize[];
extern u8 gDefaultShortNoteGateTimeTable[];
extern u8 gDefaultShortNoteVelocityTable[];
extern u8 sSeqInstructionArgsTable[];

// ---- funções ----
AnyPtr AudioScript_AudioListPopBack(AudioListItem* list);
Drum* AudioPlayback_GetDrum(s32 fontId, s32 drumId);
Instrument* AudioPlayback_GetInstrumentInner(s32 fontId, s32 instId);
Note* AudioPlayback_AllocNote(SequenceLayer* layer);
Note* AudioPlayback_AllocNoteFromActive(NotePool* pool, SequenceLayer* layer);
Note* AudioPlayback_AllocNoteFromDecaying(NotePool* pool, SequenceLayer* layer);
Note* AudioPlayback_AllocNoteFromDisabled(NotePool* pool, SequenceLayer* layer);
Note* AudioPlayback_FindNodeWithPrioLessThan(AudioListItem* list, s32 limit);
SoundEffect* AudioPlayback_GetSoundEffect(s32 fontId, s32 sfxId);
TunedSample* AudioPlayback_GetInstrumentTunedSample(Instrument* instrument, s32 semitone);
f32 AudioEffects_UpdateAdsr(AdsrState* adsr);
f32 AudioEffects_UpdatePortamento(Portamento* portamento);
f32 AudioEffects_UpdateVibrato(VibratoState* vib);
s16 AudioEffects_GetVibratoPitchChange(VibratoState* vib);
s16 AudioScript_ScriptReadS16(SeqScriptState* state);
s32 AudioPlayback_BuildSyntheticWave(Note* note, SequenceLayer* layer, s32 waveId);
s32 AudioPlayback_SetFontInstrument(s32 instrumentType, s32 fontId, s32 index, void* value);
s32 AudioScript_HandleScriptFlowControl(SequencePlayer* seqPlayer, SeqScriptState* state, s32 cmd, s32 cmdArg);
s32 AudioScript_SeqChannelSetLayer(SequenceChannel* channel, s32 layerIndex);
s32 AudioScript_SeqLayerProcessScriptStep2(SequenceLayer* layer);
s32 AudioScript_SeqLayerProcessScriptStep3(SequenceLayer* layer, s32 cmd);
s32 AudioScript_SeqLayerProcessScriptStep4(SequenceLayer* layer, s32 cmd);
s32 AudioScript_SeqLayerProcessScriptStep5(SequenceLayer* layer, s32 sameTunedSample);
u16 AudioScript_GetScriptControlFlowArgument(SeqScriptState* state, u8 cmd);
u16 AudioScript_ScriptReadCompressedU16(SeqScriptState* state);
u8 AudioScript_GetInstrument(SequenceChannel* channel, u8 instId, Instrument** instOut, AdsrSettings* adsr);
u8 AudioScript_ScriptReadU8(SeqScriptState* state);
void AudioEffects_InitAdsr(AdsrState* adsr, EnvelopePoint* envelope, s16* volOut);
void AudioEffects_InitPortamento(Note* note);
void AudioEffects_InitVibrato(Note* note);
void AudioEffects_UpdatePortamentoAndVibrato(Note* note);
void AudioPlayback_AudioListPushFront(AudioListItem* list, AudioListItem* item);
void AudioPlayback_AudioListRemove(AudioListItem* item);
void AudioPlayback_InitNoteFreeList(void);
void AudioPlayback_InitNoteList(AudioListItem* list);
void AudioPlayback_InitNoteLists(NotePool* pool);
void AudioPlayback_InitSampleState(Note* note, NoteSampleState* sampleState, NoteSubAttributes* subAttrs);
void AudioPlayback_InitSyntheticWave(Note* note, SequenceLayer* layer);
void AudioPlayback_NoteDisable(Note* note);
void AudioPlayback_NoteInit(Note* note);
void AudioPlayback_NoteInitAll(void);
void AudioPlayback_NoteInitForLayer(Note* note, SequenceLayer* layer);
void AudioPlayback_NotePoolClear(NotePool* pool);
void AudioPlayback_NotePoolFill(NotePool* pool, s32 count);
void AudioPlayback_NoteReleaseAndTakeOwnership(Note* note, SequenceLayer* layer);
void AudioPlayback_NoteSetResamplingRate(NoteSampleState* sampleState, f32 resamplingRateInput);
void AudioPlayback_ProcessNotes(void);
void AudioPlayback_SeqLayerDecayRelease(SequenceLayer* layer, s32 target);
void AudioPlayback_SeqLayerNoteDecay(SequenceLayer* layer);
void AudioPlayback_SeqLayerNoteRelease(SequenceLayer* layer);
void AudioScript_AudioListPushBack(AudioListItem* list, AudioListItem* item);
void AudioScript_InitLayerFreelist(void);
void AudioScript_InitSequenceChannel(SequenceChannel* channel);
void AudioScript_InitSequencePlayer(SequencePlayer* seqPlayer);
void AudioScript_InitSequencePlayerChannels(s32 seqPlayerIndex);
void AudioScript_InitSequencePlayers(void);
void AudioScript_ProcessSequences(s32 arg0);
void AudioScript_ResetSequencePlayer(SequencePlayer* seqPlayer);
void AudioScript_SeqLayerDisable(SequenceLayer* layer);
void AudioScript_SeqLayerFree(SequenceChannel* channel, s32 layerIndex);
void AudioScript_SeqLayerProcessScript(SequenceLayer* layer);
void AudioScript_SeqLayerProcessScriptStep1(SequenceLayer* layer);
void AudioScript_SequenceChannelDisable(SequenceChannel* channel);
void AudioScript_SequenceChannelEnable(SequencePlayer* seqPlayer, u8 channelIndex, void* script);
void AudioScript_SequenceChannelProcessScript(SequenceChannel* channel);
void AudioScript_SequenceChannelProcessSound(SequenceChannel* channel, s32 recalculateVolume, s32 applyBend);
void AudioScript_SequenceChannelSetVolume(SequenceChannel* channel, u8 volume);
void AudioScript_SequencePlayerDisable(SequencePlayer* seqPlayer);
void AudioScript_SequencePlayerDisableAsFinished(SequencePlayer* seqPlayer);
void AudioScript_SequencePlayerDisableChannels(SequencePlayer* seqPlayer, u16 channelBitsUnused);
void AudioScript_SequencePlayerProcessSequence(SequencePlayer* seqPlayer);
void AudioScript_SequencePlayerProcessSound(SequencePlayer* seqPlayer);
void AudioScript_SequencePlayerSetupChannels(SequencePlayer* seqPlayer, u16 channelBits);
void AudioScript_SetChannelPriorities(SequenceChannel* channel, u8 priority);
void AudioScript_SetInstrument(SequenceChannel* channel, u8 instId);
void AudioScript_SkipForwardSequence(SequencePlayer* seqPlayer);
void func_801963E8(Note* note, SequenceLayer* layer);

} // namespace mmsfx
