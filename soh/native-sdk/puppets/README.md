# Bonecos — casca nativa

Registra `oot.link_child_puppet` e `oot.mm_elegy_statue` pelo serviço
`linkspan.oot.actors`. O Link criança usa os recursos de OoT. A estátua exige
o serviço `linkspan.mm.audio` V1 do MM Base ativo e o recurso
`mm/objects/gameplay_keep/gElegyShellHumanDL`; sem ambos, seu init mata o ator
antes do primeiro desenho. Update e draw conferem novamente a disponibilidade.

O provider usa ABI 1.4 e o layout do SDK. Preserva o desenho do Link criança
com três símbolos privados, portanto também exige o SHA-256 do exe no manifesto.
Não instala patches nem linka contra o host, shiplua ou Lua.

Configure com `cmake -S soh/native-sdk/puppets -B build/puppets
-DOOT_NATIVE_SDK=<OotNativeSdk.cmake> -DHOST_EXECUTABLE=<soh.exe>` e compile
com `cmake --build build/puppets --config Release`.

A saída está em `build/puppets/mod`. Esta casca fica fora do RC e sem prova
em jogo enquanto os assets de MM estiverem de lado. O entrypoint não cria atores
automaticamente. MM Base deve carregar antes de criar a estátua; não é dependência
obrigatória do pacote, para permitir usar só o Link criança.

Lacuna do host atual: `ship.actor.spawn` não resolve nomes do registro nativo.
No Lua deste pacote, `ship.native.call("actor_ids", "")` devolve JSON com os dois
IDs registrados; o caminho genérico existente `oot.id.<id>` permite criá-los.
Exemplos externos que usam os nomes antigos precisam ser incorporados/migrados.
