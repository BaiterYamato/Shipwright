// Ajustes à mão do fork NEI ao host Link-Span. Incluído por /FI depois do nei_compat.h.
#pragma once

extern PlayState* gPlayState;

// O fork pôs lensFromLantern no primeiro byte do padding unk_04 do ActorContext; o host mantém o padding.
#define lensFromLantern unk_04[0]

// A tabela de BgActors do host cresce sob demanda (OOT-CORE-007); o fork percorre o tamanho fixo de 50.
#define BG_ACTOR_MAX (gPlayState->colCtx.dyna.bgActorMax)

// Nomes que o upstream 78dc6d970 trocou (mesma assinatura).
#define Audio_PlaySoundGeneral Audio_PlaySfxGeneral
#define Camera_ChangeMode Camera_RequestMode

// fork_items.c: id runtime do linkspan.oot.items no botão -> id do item no fork (patch 0002).
u8 NeiFork_ToLogicalItem(u8 runtimeId);
// fork_items.c: o contrário, para o kaleido do fork gravar no botão C o id que o host entende (extracted-fixes.txt).
u8 NeiFork_ToRuntimeItem(u8 logicalId);

// Statics do z_player.c do host que o extended_player.c do fork declara extern (no fork ele é incluído no
// z_player.c). Só leitura e dentro do tamanho vanilla; lidos pelo endereço resolvido no init da DLL.
#define sItemActions (*nei_host_sItemActions)
#define sActionModelGroups (*nei_host_sActionModelGroups)
#define sItemActionUpdateFuncs (*nei_host_sItemActionUpdateFuncs)
#define sItemActionInitFuncs (*nei_host_sItemActionInitFuncs)
