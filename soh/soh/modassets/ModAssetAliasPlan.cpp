#include "ModAssetAliasPlan.h"
#include "ship/utils/StrHash64.h"
#include <algorithm>
#include <stdexcept>

namespace SohModAssets {
std::shared_ptr<ModAssetArchive> ReopenArchive(const ModAssetArchive& source, const Hashes& aliases) {
    // GetPath não é const no LUS; só a identidade física é consultada aqui.
    auto& view = const_cast<ModAssetArchive&>(source);
    auto next = std::make_shared<ModAssetArchive>(view.GetPath(), source.Prefix(), aliases, true);
    next->Load();
    return next;
}

AliasPlan PlanAliases(const Archives& snapshot, const Archives& privateArchives, const Archives& rejected,
                      const Reopen& reopen) {
    AliasPlan plan;
    const auto contains = [](const Archives& archives, const auto& archive) {
        return std::find(archives.begin(), archives.end(), archive) != archives.end();
    };
    std::unordered_set<Ship::Archive*> privateSet, rejectedSet;
    for (const auto& archive : rejected)
        rejectedSet.insert(archive.get());
    Hashes reserved;
    std::vector<std::shared_ptr<ModAssetArchive>> survivors;
    for (const auto& archive : privateArchives) {
        if (!contains(snapshot, archive))
            continue; // Pode ter sido promovido durante Init.
        privateSet.insert(archive.get());
        if (!rejectedSet.contains(archive.get())) {
            const auto wrapper = std::dynamic_pointer_cast<ModAssetArchive>(archive);
            if (!wrapper)
                throw std::runtime_error("Archive privado sem wrapper de assets");
            survivors.push_back(wrapper);
            for (const auto& [hash, name] : wrapper->OriginalFiles())
                reserved.insert(CRC64((wrapper->Prefix() + name).c_str()));
        }
    }
    // Nomes normais e privados têm precedência sobre todos os aliases recalculados.
    for (const auto& archive : snapshot)
        if (!privateSet.contains(archive.get()) && !rejectedSet.contains(archive.get()))
            for (const auto& [hash, name] : *archive->ListFiles())
                reserved.insert(hash);
    std::unordered_map<Ship::Archive*, std::shared_ptr<Ship::Archive>> replacements;
    for (const auto& wrapper : survivors) {
        if (!reopen)
            break; // PlanRetirement: sem redistribuição.
        Hashes aliases;
        for (const auto& [hash, name] : wrapper->OriginalFiles())
            if (ModAssetArchive::IsRenderPath(name) && reserved.insert(hash).second)
                aliases.insert(hash); // Primeiro wrapper na ordem original ganha.
        if (aliases != wrapper->Aliases()) {
            auto next = reopen(*wrapper, aliases);
            if (!next || !next->IsLoaded() || next->OriginalFiles() != wrapper->OriginalFiles() ||
                next->Aliases() != aliases || next->Prefix() != wrapper->Prefix() ||
                next->GetPath() != wrapper->GetPath())
                throw std::runtime_error("Archive mudou ou falhou ao preparar aliases");
            replacements.emplace(wrapper.get(), std::move(next));
            ++plan.replacements;
        }
    }
    for (const auto& archive : snapshot) {
        if (rejectedSet.contains(archive.get()))
            continue;
        auto found = replacements.find(archive.get());
        if (privateSet.contains(archive.get()))
            plan.privatePositions.push_back(plan.desired.size());
        plan.desired.push_back(found == replacements.end() ? archive : found->second);
    }
    // Camadas aposentadas também invalidam caches com owner/parent explícitos.
    for (const auto& archive : snapshot)
        if (rejectedSet.contains(archive.get()) || replacements.contains(archive.get()))
            for (const auto& [hash, name] : *archive->ListFiles())
                plan.affected.insert(name);
    // Diferença de vencedor e caminho literal: A→B também é invalidação, mesmo com presença igual.
    using Winners = std::unordered_map<uint64_t, std::pair<Ship::Archive*, std::string>>;
    const auto winners = [](const Archives& archives) {
        Winners result;
        for (const auto& archive : archives)
            for (const auto& [hash, name] : *archive->ListFiles())
                result[hash] = { archive.get(), name };
        return result;
    };
    const auto before = winners(snapshot), after = winners(plan.desired);
    for (const auto& [hash, origin] : before) {
        const auto found = after.find(hash);
        if (found == after.end() || found->second != origin) {
            plan.affected.insert(origin.second);
            if (found != after.end())
                plan.affected.insert(found->second.second);
        }
    }
    for (const auto& [hash, origin] : after)
        if (!before.contains(hash))
            plan.affected.insert(origin.second);
    return plan;
}

AliasPlan PlanRetirement(const Archives& snapshot, const Archives& privateArchives, const Archives& rejected) {
    return PlanAliases(snapshot, privateArchives, rejected, {});
}
} // namespace SohModAssets
