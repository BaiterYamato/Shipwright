#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "json_merge.h"

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    constexpr const char* schema = "linkspan.unbound.actor-patch/v1";
    const std::vector<LinkSpanUnbound::LayerDocument> layers{
        { "base.zip",
          R"({"$schema":"linkspan.unbound.actor-patch/v1","actor":{"name":"guard","stats":{"health":3,"speed":1},"drops":["rupee"]},"enabled":true})",
          1 },
        { "override.zip",
          R"({"$schema":"linkspan.unbound.actor-patch/v1","actor":{"stats":{"health":8},"drops":["heart"]},"enabled":false})",
          2 },
    };
    LinkSpanUnbound::MergeResult result;
    std::string error;
    Check(LinkSpanUnbound::MergeDocuments(schema, layers, result, error), "merge válido deve funcionar");
    Check(result.layerCount == 2 && result.hash != 0, "resultado deve registrar camadas e hash");
    Check(
        result.json ==
            R"({"$schema":"linkspan.unbound.actor-patch/v1","actor":{"name":"guard","stats":{"health":8,"speed":1},"drops":["heart"]},"enabled":false})",
        "objetos devem mesclar recursivamente e arrays devem ser substituídos");

    auto wrong = layers;
    wrong[1].json = R"({"$schema":"outro/v1","actor":{"name":"wrong"}})";
    Check(!LinkSpanUnbound::MergeDocuments(schema, wrong, result, error) && error.starts_with("schema mismatch"),
          "schema divergente deve ser recusado");
    wrong[1].json = "{";
    Check(!LinkSpanUnbound::MergeDocuments(schema, wrong, result, error) && !error.empty(),
          "JSON inválido deve ser recusado sem exceção externa");
    std::cout << "unbound json merge: ok\n";
    return 0;
}
