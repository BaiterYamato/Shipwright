# Proveniência do port Unbound 0.9

O trabalho próprio do Link-Span neste pacote (DLL, Lua, ferramenta, header, schemas e documentação) está em
domínio público pela CC0 1.0 Universal (`LICENSE`). As partes adaptadas do Unbound, descritas abaixo, seguem com
roborich e colaboradores; o projeto não publica licença, e este pacote não concede nenhuma sobre elas. A DLL e a
ferramenta embutem nlohmann/json 3.12.0 (MIT), com o texto em `docs/LICENSES.md`.

O leitor de tipos em `actor_registry.cpp` foi adaptado de `ActorRegistry.cpp` e
`DeclaredActorType.h` do projeto [roborich/Shipwright, tag 9.2.3-unbound0.9](https://github.com/roborich/Shipwright/releases/tag/9.2.3-unbound0.9),
commit `cf7db7f7f9b65247347d54abea386669ce9c909f`.

O host Link-Span adapta `DeclaredActor.cpp` no serviço genérico `linkspan.oot.actor-models`.
O formato JSON e os comportamentos seguem `unbound-docs/SPEC.md`, §§4.3 e 7.2.

Não foi encontrado arquivo de licença na raiz dessa tag. Este registro preserva
a autoria e não concede uma licença de redistribuição. Os exemplos usam caminhos
de recursos; os modelos e texturas do jogo não são incluídos no pacote.
