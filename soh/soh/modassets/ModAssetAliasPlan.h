#pragma once
#include "ModAssetArchive.h"
#include <functional>

namespace SohModAssets {
struct AliasPlan {
    Archives desired;
    std::vector<size_t> privatePositions;
    std::unordered_set<std::string> affected;
    size_t replacements = 0;
};
using Reopen = std::function<std::shared_ptr<ModAssetArchive>(const ModAssetArchive&, const Hashes&)>;
// Snapshot e privadas contêm ownership gerenciado. rejected usa identidade, nunca o caminho textual.
AliasPlan PlanAliases(const Archives& snapshot, const Archives& privateArchives, const Archives& rejected,
                      const Reopen& reopen);
// Só retira os rejeitados, sem reabrir wrappers: os sobreviventes ficam com os aliases que já têm. Caminho de
// reserva quando a redistribuição falha; publicado pelo mesmo lote, invalida o cache de todos os owners.
AliasPlan PlanRetirement(const Archives& snapshot, const Archives& privateArchives, const Archives& rejected);
std::shared_ptr<ModAssetArchive> ReopenArchive(const ModAssetArchive& source, const Hashes& aliases);
} // namespace SohModAssets
