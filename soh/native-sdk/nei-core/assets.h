// Assets do NEI (NEI-007): os arquivos .o2r que tools/build-nei-assets.py gera, a partir do fork, no computador
// de quem joga. Não vêm no pacote (o fork não tem licença declarada); o coremod monta os que encontrar em
// <pasta do jogo>/nei-assets/ e segue com ícones e modelos provisórios quando não há nenhum.
#ifndef LINKSPAN_NEI_ASSETS_H
#define LINKSPAN_NEI_ASSETS_H

#include <string>

#include <shiplua/native/ship_native_abi.h>

namespace LinkSpanNei {

// Monta nei-assets-core.o2r e os componentes opcionais presentes. Chamado no Init, quando só os archives
// do jogo estão montados: os assets ficam abaixo dos mods que o SoH monta depois.
void MountAssets(const ShipNativeRuntime* runtime);
void UnmountAssets();
// Verdadeiro quando o componente (core, form.kafei, expansion.sm64...) está montado.
bool AssetComponentMounted(const char* component);
const std::string& AssetsStatus();

} // namespace LinkSpanNei

#endif
