[English](README.md) · **Português (Brasil)**

# Provider mínimo para a prova do SDK

Compile em Release conforme o guia de autor. Exporta `ShipNative_Query`, consulta
`linkspan.oot.engine` v1 e recusa layout ou sizeof incompatíveis. O manifesto pede
ABI 1.2; a versão do descriptor vem dos macros do header distribuído (`nativeProviderAbi` em `linkspan-sdk.json`). Não instala patches, não carrega assets
e não altera o jogo. `doctor` analisa o PE sem carregar a DLL.

Este probe é um rascunho para o gate externo. Não é uma suíte completa de
conformidade nativa: falta execução com o host e negativos de ABI, rollback,
hooks/unload, lifetime e conflito entre serviços. Não instalar junto de variantes
com o mesmo ID. A presença do ZIP nunca prova execução em jogo.
