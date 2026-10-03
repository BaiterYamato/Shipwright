#pragma once

// Resolução por versão (plano §10.5/§14.3, UNBOUND-019): um documento Unbound de mod aponta para recursos pelo
// nome (display list, imagem, colisão, sala, path, cutscene, textura animada, música), e esses nomes mudam entre
// famílias de ROM. Aqui o documento é mesclado e transcodificado como o TranscodeJson faria, e sai a lista dos
// recursos que o XML manda o host carregar; o framework confere cada um no VFS no game.ready. Puro.
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "json_merge.h"
#include "transcode.h"

namespace LinkSpanUnbound {

enum class DocumentKind { Scene, Room, Collision, Paths };

// Tipo e versão que o framework registra no host (RegisterJsonTypes): só esses o jogo transcodifica. false para
// qualquer outro tipo, inclusive versão diferente da registrada (o host recusa fora da faixa).
bool DocumentKindFor(const std::string& type, int version, DocumentKind& kind);
// Versão registrada de um tipo de documento do framework (unbound/scene, room, collision, paths), ou 0.
int RegisteredVersion(const std::string& type);

struct ReferenceReport {
    bool typed = false;    // $schema de um tipo do framework (versão registrada ou não); senão o resto fica vazio
    bool accepted = false; // o transcodificador aceitou; senão `error` tem o motivo, como o jogo o registraria
    std::string schema;    // $schema mesclado ("" se nenhuma camada serviu)
    std::string error;
    std::vector<std::pair<std::string, std::string>> references; // (campo, caminho), sem repetição, na ordem
    std::vector<std::string> notes; // camadas puladas na mescla e notas do transcodificador
    DocumentKind kind = DocumentKind::Scene;
    std::string path; // context.path
    Json document;    // só quando accepted: a mescla que foi transcodificada
};

// Nome da versão de origem do oot.o2r (CRC de soh/soh/GameVersions.h), ou "" se desconhecida.
const char* GameVersionName(uint32_t version);

// Referência que a fábrica lê como arquivo cru (ArchiveManager::LoadFile com o nome literal), sem o ResourceManager:
// sem tirar __OTR__ e sem alias .meta. Hoje só o bulk.file da colisão.
bool IsRawFileReference(const std::string& field);
// Caminho para conferir no VFS: o ResourceManager tira o prefixo __OTR__ antes de procurar; arquivo cru não.
std::string ResourceLookupPath(const std::string& field, const std::string& path);

// `layers` da menor para a maior prioridade, como read_file_layers entrega; `context` traz o caminho e os
// resolvedores de entrada e ator (as referências e notas dele são ignoradas).
ReferenceReport CollectReferences(const std::vector<LayerDocument>& layers, TranscodeContext context);

// Grafo entre documentos (UNBOUND-025): índices que a cena, as salas e a colisão usam para ler listas uns dos outros,
// e que o host não confere. Puro; não muda JSON nem XML.
// Os itens de uma nota: onde (documento e lista), o que (com o limite usado) e cada campo=valor fora.
struct GraphFindings {
    std::string where;
    std::string what;
    std::vector<std::string> items;
};
struct SceneGraph {
    std::vector<GraphFindings> findings; // por item, para comparar com a base
    std::vector<std::string> notes;      // uma por grupo de findings: o jogo leria fora da lista, ou sem efeito
    std::vector<std::string> gaps;       // o que não deu para conferir (dependência ausente ou recusada, exits de sala)
};
// Relatório já mesclado de um caminho (sem __OTR__); nullptr é desconhecido, nunca lista vazia. Os ponteiros
// precisam viver até o retorno.
using GraphLookup = std::function<const ReferenceReport*(const std::string&)>;
SceneGraph CollectSceneGraph(const ReferenceReport& scene, const GraphLookup& lookup);
// Tira de `graph` cada item que a cena só da base também dá (mesmo lugar, mesmo limite, mesmo campo=valor) e refaz
// as notas com o que sobra; devolve quantos itens saíram. Lacunas ficam: cobertura que faltou não vira herança.
size_t DropInheritedFindings(SceneGraph& graph, const SceneGraph& base);
// Só o que o grafo lê de uma sala (portas e se ela troca os exits) ou de uma colisão (quantas câmeras, room da
// água, exit da superfície), para o cache do game.ready não reter atores, geometria e o resto. As chaves e a ordem
// das listas ficam; o grafo sai igual. Cena volta inteira; paths, vazio.
Json CompactForGraph(const Json& document, DocumentKind kind);

} // namespace LinkSpanUnbound
