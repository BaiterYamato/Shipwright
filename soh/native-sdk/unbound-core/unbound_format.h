#pragma once

// Regras de valor (SPEC.md §2) e de merge por camada (§3) do formato 2 do Unbound. Puro: recebe as
// camadas já lidas, então os testes não precisam do jogo.
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "json_merge.h"

namespace LinkSpanUnbound {

using Json = nlohmann::ordered_json;

// Documento recusado (§ "rejected"): a mensagem vai para o log e o recurso não carrega.
struct DocumentError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct MergedDocument {
    Json doc;              // sem $schema, $replace nem nulls
    std::string schema;    // "$schema" da camada mais alta que o traz ("" se nenhuma)
    std::string type;      // parte antes da última barra
    int version = 0;       // número depois da última barra (0 se inválido)
    uint32_t layersUsed = 0;
    std::vector<std::string> notes; // camadas puladas e outros avisos
};

// Mescla as camadas da menor para a maior prioridade. Camada que não é JSON (ou não é objeto) é
// pulada com uma nota; `strictStart` exige '{' no primeiro byte (§2, documentos da §4). Aceita
// comentários // e /* */. Devolve false só se nenhuma camada serviu.
bool MergeLayers(const std::vector<LayerDocument>& layers, bool strictStart, MergedDocument& out);

// Json::parse(text, nullptr, false, ignoreComments) sem o custo quadrático do ordered_json: o parser padrão procura
// cada chave nova entre as já lidas, e uma lista posicional de 65 535 entradas levava segundos. Chave repetida fica
// na primeira posição com o último valor, como no nlohmann. Inválido devolve um valor descartado (is_discarded).
Json ParseJson(const std::string& text, bool ignoreComments = true);

// Mescla `overlay` sobre `base` (§3.1–3.4), sem remover as diretivas.
void MergeJson(Json& base, const Json& overlay);
// Tira $replace e nulls que sobraram (vale também para um documento de uma camada só).
void StripDirectives(Json& doc);

// Chaves de lista com chave (§3.5): $order primeiro, depois inteiros em ordem numérica, depois o
// resto em ordem de bytes. Chaves com '$' ficam de fora.
std::vector<std::string> ListKeys(const Json& list);
// Lista posicional: $order recusa o documento; buraco depois do merge recusa o documento.
std::vector<std::string> PositionalKeys(const Json& list, const std::string& what);
// As mesmas chaves com o valor de cada uma: list[chave] no ordered_json é uma busca linear, e percorrer uma lista
// grande por chave custava o quadrado do tamanho. Os ponteiros valem enquanto `list` não mudar.
using ListItem = std::pair<std::string, const Json*>;
std::vector<ListItem> ListItems(const Json& list);
std::vector<ListItem> PositionalItems(const Json& list, const std::string& what);

bool ParseIntString(const std::string& text, int64_t& out);
bool ParseNumberString(const std::string& text, double& out);
int64_t ToInt(const Json& value, int64_t fallback = 0);
double ToNumber(const Json& value, double fallback = 0.0);
bool HasNumber(const Json& obj, const char* key);
int64_t Field(const Json& obj, const char* key, int64_t fallback = 0);
double NumberField(const Json& obj, const char* key, double fallback = 0.0);
std::string PathField(const Json& obj, const char* key);
const Json& Sub(const Json& obj, const char* key);
const Json& SubArray(const Json& obj, const char* key);

struct Vec3 {
    double x = 0, y = 0, z = 0;
};
// Vetor de 3 números; com menos de 3 elementos o vetor inteiro é zero (§2).
Vec3 ReadVec3(const Json& value);

bool ParseSchema(const std::string& schema, std::string& type, int& version);

} // namespace LinkSpanUnbound
