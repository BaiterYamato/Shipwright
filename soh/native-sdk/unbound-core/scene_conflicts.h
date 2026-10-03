#pragma once

// Conflito entre mods (plano §10.5/§14.3): duas camadas de mod no mesmo caminho do VFS. Documento JSON que o
// Unbound mescla é comparado folha a folha: delta que mescla sem perda só é registrado; valor de um mod que outro
// sobrescreve vira aviso antes do gameplay, com os mods, exemplos de chave e quem vence. Qualquer outro arquivo o
// VFS troca inteiro, então conteúdo diferente é conflito. Puro, exceto ListArchiveNames, que lê do disco.
#include <cstddef>
#include <string>
#include <vector>

#include "json_merge.h"

namespace LinkSpanUnbound {

constexpr size_t kConflictKeyExamples = 3;
// Camadas de mod comparadas por documento (os pares crescem com o quadrado).
constexpr size_t kMaxComparedLayers = 16;

// Nomes das entradas de um ZIP, lidos do diretório central (sem descompactar nada). ZIP64, diretório central
// fora do buffer ou entrada truncada devolvem false com o motivo.
bool ListZipNames(const std::string& bytes, std::vector<std::string>& names, std::string& error);
// O mesmo para um arquivo no disco (lê só o fim e o diretório central) ou uma pasta montada como archive.
bool ListArchiveNames(const std::string& path, std::vector<std::string>& names, std::string& error);

// Entrada de camada que entra na comparação: arquivo (não pasta) e não o unbound.json, que não mescla (§6).
bool IsComparableEntry(const std::string& name);

// Nome curto do mod para o log: o caminho depois de "mods/" (pasta do mod e .o2r), ou o nome do arquivo.
std::string ArchiveLabel(const std::string& archive);

// A regra que o jogo aplica ao caminho, para a comparação ver o mesmo resultado.
enum class MergeRule {
    Scene,      // JSON tipado pelo conteúdo ('{' + $schema "unbound/..."): MergeLayers estrito e MergeJson
    Registry,   // unbound/scenes.json: MergeSchemaFreeDocuments (comentários, null remove, sem $replace)
    Text,       // text/<lang>/messages.json: BuildTextTable (MergeJson, sem exigir '{' no primeiro byte)
    WholeFile,  // o resto: o VFS entrega o arquivo da camada mais alta, sem merge
};
// `layers`: todas as camadas do caminho (base incluída), para achar o $schema que tipa o documento.
MergeRule RuleFor(const std::string& path, const std::vector<LayerDocument>& layers);

struct LayerConflict {
    std::string lower;              // archive da camada que perde
    std::string upper;              // archive que vence (montado depois)
    size_t lost = 0;                // folhas da camada de baixo que não sobrevivem ao merge (1 no arquivo inteiro)
    std::vector<std::string> keys;  // até kConflictKeyExamples caminhos, "setups.0.actors.a.pos"
    bool wholeFile = false;         // o arquivo de baixo some inteiro (MergeRule::WholeFile)
};

struct DocumentAnalysis {
    std::vector<LayerConflict> conflicts;  // um por par (baixo, cima) com perda
    std::vector<std::string> notes;        // camadas ilegíveis, puladas da comparação
    bool identical = false;                // WholeFile: todas as camadas de mod com o mesmo conteúdo
    size_t compared = 0;                   // camadas que entraram na comparação (menos de 2: nada foi comparado)
};

// `layers`: só as camadas de mod de um caminho, da menor para a maior prioridade (a base fica de fora).
// Uma folha da camada de baixo sobrevive se o merge com a de cima (pela regra do caminho) deixa o mesmo valor
// no mesmo caminho; null sobrevive se a chave continua ausente. Valor igual nas duas camadas não é conflito.
// WholeFile compara o conteúdo: cada camada diferente da mais alta perde o arquivo inteiro.
DocumentAnalysis AnalyzeDocument(const std::vector<LayerDocument>& layers, MergeRule rule = MergeRule::Scene);

} // namespace LinkSpanUnbound
