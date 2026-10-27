#pragma once

// Mod Bonecos: um ator leve que desenha o esqueleto do child Link
// (gLinkChildSkel) com a animação de espera. Pacotes .otr que substituem
// objects/object_link_child/* (ex.: Kafei) trocam a aparência do puppet sem
// código adicional. Ver plan-sdk §26 (primeiro caso de uso: Kafei Puppet).

namespace ShipLuaHost {

// Prepara esqueleto e buffers do Link criança no init do tipo nativo. Só thread do jogo.
bool ShipLuaPuppet_Attach(void* actor, void* play);

// Variante estática: exige MM Base ativo e a display list montada. Só thread do jogo.
bool ShipLuaPuppet_AttachStatue(void* actor);

// Libera o estado do puppet quando o ator morre ou a cena é destruída.
void ShipLuaPuppet_HandleActorDestroy(void* actor);
void ShipLuaPuppet_Reset();

} // namespace ShipLuaHost

struct Actor;
struct PlayState;
extern "C" {
void ShipLuaPuppetUpdate(Actor* actor, PlayState* play);
void ShipLuaPuppetDraw(Actor* actor, PlayState* play);
void ShipLuaStatueDraw(Actor* actor, PlayState* play);
}
