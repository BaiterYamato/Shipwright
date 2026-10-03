#pragma once

// Documentos da SPEC.md §4 (já mesclados) para o XML que as fábricas XML do host leem: <Room>
// com os comandos de cena, <CollisionHeader> e <Path>. Puro; os testes comparam o XML gerado.
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "unbound_format.h"

namespace LinkSpanUnbound {

struct TranscodeContext {
    std::string path; // caminho do documento, para as mensagens
    // Nome de entrada (ENTR_* ou "<cena>/<entrada>") para índice; < 0 se não existe.
    std::function<int32_t(const std::string&)> resolveEntrance;
    std::vector<std::string> notes; // entradas descartadas e outros avisos para o log
    std::function<int32_t(const std::string&)> resolveActor;
    // Recursos que o XML manda o host carregar (campo, caminho), na ordem do documento; entradas descartadas não
    // entram. A checagem de referências do framework confere cada um no VFS antes do gameplay (UNBOUND-019).
    std::vector<std::pair<std::string, std::string>> references;
};

// scene.json (room=false) ou rooms/<n>.json (room=true). DocumentError = documento recusado.
std::string TranscodeScene(const Json& doc, bool room, TranscodeContext& context);
std::string TranscodeCollision(const Json& doc, TranscodeContext& context);
std::string TranscodePaths(const Json& doc, TranscodeContext& context);

std::string EscapeXml(const std::string& text);

} // namespace LinkSpanUnbound
