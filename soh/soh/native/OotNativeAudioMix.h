#pragma once

#include <cstdint>

#include <shiplua/native/NativeHooks.h>

namespace ShipLuaHost {
// oot.audio.mix (oot_audio.h). O ponto é declarado no registro do jogo; a cada mudança na lista de hooks a thread
// do jogo publica uma cópia imutável para a thread de áudio. Install e Reset na thread do jogo.
uint64_t InstallOotAudioMix(ShipLua::NativeHookRegistry& registry);
void ResetOotAudioMix();
// Thread de áudio. Barato: há hook publicado?
bool OotAudioMixActive();
// Thread de áudio. Roda os hooks sobre `frames` quadros estéreo s16 em `samples`; false sem hook ou fora do limite.
bool MixOotAudio(int16_t* samples, uint32_t frames);
// Thread do jogo: desativa no registro os hooks que falharam três vezes seguidas na thread de áudio.
void PumpOotAudioMix();
} // namespace ShipLuaHost
