# Provider nativo de atores OoT

`OOT-MODSDK-001` implementa o provider `shipwright-native` para a API genérica de atores do ShipLua 0.4.

## Contrato deste recorte

- somente a thread principal do jogo pode criar, consultar ou destruir atores;
- o mod informa um ID lógico allowlisted, nunca um `ActorId` numérico;
- nenhum `Actor*` sai do provider;
- cada ator recebe um `ShipLua::Handle` com slot, geração, geração de cena e ownership;
- o limite inicial é de 16 atores vivos por mod e 256 no host;
- a dependência de object precisa estar carregada antes do spawn;
- handles são invalidados quando o ator nativo é destruído, o mod é descarregado ou o `PlayState` termina;
- `ACTOR_PLAYER` é rejeitado mesmo que seja incluído acidentalmente na allowlist.

IDs iniciais:

| ID lógico | Ator OoT | Object obrigatório | Params fixos |
|---|---|---|---|
| `oot.en_dog` | `ACTOR_EN_DOG` | `OBJECT_DOG` | `0x8000` |
| `oot.en_torch2` | `ACTOR_EN_TORCH2` | `OBJECT_TORCH2` | `0` |

O provider registra as capabilities experimentais `actor.spawn`, `actor.destroy` e `actor.exists` com versão `0.1.0` e é publicado no `LuaApiHostContext`. Os bindings `ship.actor.spawn`, `ship.actor.destroy` e `ship.actor.exists` vêm do ShipLua 0.4 (`MODSDK-005`, PR `link-span#41`). O manifesto precisa declarar as capabilities, os grants `world.entities.create`, `world.entities.destroy` e `world.entities.read`, além de um limite `actors` maior que zero para spawn.

Posições chegam como números genéricos e são convertidas para `float` somente na chamada nativa. Rotações são expressas em graus pela API portátil e convertidas para o formato binário de ângulo do OoT no limite do host.

## Lifecycle

`ShipLuaBootstrap` registra hooks de `OnActorDestroy` e `OnPlayDestroy`. O primeiro invalida imediatamente o handle de um ator removido pelo próprio jogo. O segundo mata os atores ainda pertencentes a mods e avança a geração de cena antes da desmontagem do `PlayState`.

Para hot reload futuro, o host deve chamar `OotActorProvider::ReleaseMod(modId)` antes de destruir o `LuaRuntime` daquele mod.

## Validação

O teste `oot_actor_provider_tests` cobre:

- descritores das capabilities;
- allowlist de `En_Dog` e `En_Torch2`;
- bloqueio de `ACTOR_PLAYER`;
- dependência de object;
- ownership entre mods;
- handles inválidos e gerações obsoletas;
- limite por mod;
- cleanup por ator, mod e cena;
- rejeição fora da thread principal.

A validação integrada gera `x64/Release/soh.exe` em Release e executa 57/57 testes aplicáveis do host e do SDK embutido. O teste legado `prism`, que não produz executável nessa configuração do host, é excluído dessa contagem.
