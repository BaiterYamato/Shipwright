#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "json_merge.h"

namespace LinkSpanUnbound {

// Mesma ordem de campos do ActorEntry do jogo.
struct RoomActor {
    int16_t id = 0;
    int16_t pos[3]{};
    int16_t rot[3]{};
    int16_t params = 0;
};

// Caminho do recurso da sala ("scenes/shared/spot00_scene/spot00_room_0", com ou sem
// "nonmq"/"mq") para o documento do SPEC §4.1 ("scenes/spot00/rooms/0.json"; Master Quest
// ganha "_mq"). Vazio quando o caminho não tem uma pasta "<cena>_scene".
std::string RoomDocumentPath(const std::string& roomPath, int32_t room);

struct RoomActorsResult {
    std::vector<RoomActor> actors;
    uint32_t layersUsed = 0;
    // Camadas puladas (JSON inválido) e entradas ignoradas; o resto segue.
    std::vector<std::string> notes;
};

// Mescla as camadas unbound/room/1 (SPEC §3) sobre a lista vanilla da camada de cena
// `setup`, que entra como a camada mais baixa com as chaves "0".."n-1", e devolve
// setups.<setup>.actors na ordem do motor ($order, depois ordem de chave). Retorna false
// quando o documento mesclado é rejeitado ($schema ausente ou de outra versão, $order em
// lista posicional); nesse caso a lista vanilla deve ficar como está.
bool ApplyRoomActorLayers(const std::vector<RoomActor>& vanilla, int32_t setup,
                          const std::vector<LayerDocument>& layers, RoomActorsResult& output, std::string& error);

} // namespace LinkSpanUnbound
