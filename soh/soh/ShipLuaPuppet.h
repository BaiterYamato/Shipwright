#pragma once

// Puppet host do ShipLua: um ator leve que desenha o esqueleto do child Link
// (gLinkChildSkel) com a animação de espera. Pacotes .otr que substituem
// objects/object_link_child/* (ex.: Kafei) trocam a aparência do puppet sem
// código adicional. Ver plan-sdk §26 (primeiro caso de uso: Kafei Puppet).

namespace ShipLuaHost {

// Converte o ator recém-spawnado (host En_Item00) em puppet: troca update/draw
// e prepara esqueleto + buffers do objeto do child Link. Main-thread only.
bool ShipLuaPuppet_Attach(void* actor, void* play);

// Variante estática: desenha uma display list resolvida por caminho __OTR__
// (ex.: a estátua da Elegia transplantada do mm.o2r). Main-thread only.
bool ShipLuaPuppet_AttachStatue(void* actor);

// Libera o estado do puppet quando o ator morre ou a cena é destruída.
void ShipLuaPuppet_HandleActorDestroy(void* actor);
void ShipLuaPuppet_Reset();

} // namespace ShipLuaHost
