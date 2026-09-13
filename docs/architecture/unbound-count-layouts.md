# Unbound: fundação de contagens ampliadas

## Objetivo

O recorte `OOT-UNBOUND-003A` remove os primeiros limites estruturais herdados do N64 que impediam cenas e mods
maiores. Ele altera o host uma única vez para que pacotes externos possam usar capacidades maiores sem recompilar o
Shipwright para cada mod.

## Capacidades

| Estrutura | Antes | Agora |
| --- | ---: | ---: |
| Atores vivos (`ACTOR_NUMBER_MAX`) | 2.000 | 8.192 |
| Contador de atores vivos | 8 bits | 16 bits |
| Banco de objetos | 128 | 1.024 |
| Índice de objeto no `Actor` | 8 bits com sinal | 16 bits com sinal |
| Atores de setup, salas e atores de transição | 8 bits | 16 bits |
| Contagem de display lists de mesh tipo 0/2 | 8 bits | 32 bits |
| Meshes tipo 2 ordenadas por sala | 64 | 1.024 |

O importador binário continua lendo a contagem de mesh do formato OTR v0 como `u8`, mas agora preserva corretamente
valores entre 128 e 255. O importador XML e futuros produtores de recursos podem preencher a representação de 32 bits.
As contagens de atores, salas e transições vindas de recursos de 32 bits são limitadas explicitamente ao maior valor
representável no runtime, com diagnóstico, em vez de sofrer wrap silencioso.

Quando uma lista de objetos excede 1.024 entradas, o carregador mantém a contagem interna dentro do banco, registra o
número descartado e evita que um recurso inválido deixe o `ObjectContext` apontando além do array. Quando uma sala tem
mais de 1.024 meshes tipo 2, o renderizador registra o excesso e desenha somente as primeiras 1.024.

`Object_Spawn` também recusa uma inserção quando o banco está cheio antes de acessar o array, e `Object_IsLoaded`
recusa índices negativos ou fora da contagem ativa. O espaço em bytes reservado para os dados dos objetos continua
limitado pelo alocador da cena; ampliar essa memória e os índices privados `s8` mantidos por atores específicos pertence
aos próximos recortes.

## Verificação incorporada

`soh/soh/unbound/UnboundLayoutChecks.cpp` é compilado junto do host e contém `static_assert`s para impedir que uma
alteração futura volte silenciosamente aos tipos estreitos ou crie divergência entre o layout do recurso e o runtime.

## Limites que continuam para os próximos recortes

Este recorte ainda não amplia coordenadas de atores, caminhos e colisão, IDs de sala armazenados em `s8`, tabelas de
colisão dinâmica, índices privados de objeto mantidos por alguns atores ou o catálogo dinâmico de cenas e entradas.
Essas mudanças exigem auditoria de ponteiros e conversões para evitar truncamento e serão implementadas em etapas
separadas.
