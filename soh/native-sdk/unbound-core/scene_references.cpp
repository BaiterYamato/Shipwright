#include "scene_references.h"

#include <set>

#include "unbound_format.h"

namespace LinkSpanUnbound {

bool DocumentKindFor(const std::string& type, int version, DocumentKind& kind) {
    static constexpr struct {
        const char* type;
        int version;
        DocumentKind kind;
    } kTypes[] = {
        { "unbound/scene", 1, DocumentKind::Scene },
        { "unbound/room", 1, DocumentKind::Room },
        { "unbound/collision", 3, DocumentKind::Collision },
        { "unbound/paths", 1, DocumentKind::Paths },
    };
    for (const auto& entry : kTypes) {
        if (type == entry.type && version == entry.version) {
            kind = entry.kind;
            return true;
        }
    }
    return false;
}

const char* GameVersionName(uint32_t version) {
    static constexpr struct {
        uint32_t crc;
        const char* name;
    } kVersions[] = {
        { 0xEC7011B7, "NTSC-US 1.0" },    { 0xD43DA81F, "NTSC-US 1.1" },    { 0x693BA2AE, "NTSC-US 1.2" },
        { 0xB044B569, "PAL 1.0" },        { 0xB2055FBD, "PAL 1.1" },        { 0xF7F52DB8, "NTSC-JP GC CE" },
        { 0xF611F4BA, "NTSC-JP GC" },     { 0xF3DD35BA, "NTSC-US GC" },     { 0x09465AC3, "PAL GC" },
        { 0xF43B45BA, "NTSC-JP MQ" },     { 0xF034001A, "NTSC-US MQ" },     { 0x1D4136F3, "PAL MQ" },
        { 0x871E1C92, "PAL GC debug 1" }, { 0x87121EFE, "PAL GC debug 2" }, { 0x917D18F6, "PAL GC MQ debug" },
        { 0x3D81FB3E, "iQue TW" },        { 0xB1E1E07B, "iQue CN" },
    };
    for (const auto& entry : kVersions) {
        if (entry.crc == version) {
            return entry.name;
        }
    }
    return "";
}

int RegisteredVersion(const std::string& type) {
    for (const int version : { 1, 3 }) {
        DocumentKind kind;
        if (DocumentKindFor(type, version, kind)) {
            return version;
        }
    }
    return 0;
}

bool IsRawFileReference(const std::string& field) {
    return field == "bulk.file";
}

std::string ResourceLookupPath(const std::string& field, const std::string& path) {
    constexpr char kPrefix[] = "__OTR__";
    if (IsRawFileReference(field)) {
        return path;
    }
    return path.rfind(kPrefix, 0) == 0 ? path.substr(sizeof(kPrefix) - 1) : path;
}

ReferenceReport CollectReferences(const std::vector<LayerDocument>& layers, TranscodeContext context) {
    ReferenceReport report;
    MergedDocument merged;
    const bool mergedOk = MergeLayers(layers, true, merged);
    report.notes = merged.notes;
    report.schema = merged.schema;
    DocumentKind kind = DocumentKind::Scene;
    if (!mergedOk) {
        return report;
    }
    if (!DocumentKindFor(merged.type, merged.version, kind)) {
        // Tipo do framework com versão que ele não registra: o host não acha fábrica e o documento não carrega.
        const int registered = RegisteredVersion(merged.type);
        if (registered) {
            report.typed = true;
            report.error = "$schema " + merged.schema + ": o framework registra " + merged.type + "/" +
                           std::to_string(registered) + " e o host não carrega outra versão";
        }
        return report;
    }
    report.typed = true;
    context.references.clear();
    context.notes.clear();
    try {
        switch (kind) {
            case DocumentKind::Scene:
                TranscodeScene(merged.doc, false, context);
                break;
            case DocumentKind::Room:
                TranscodeScene(merged.doc, true, context);
                break;
            case DocumentKind::Collision:
                TranscodeCollision(merged.doc, context);
                break;
            case DocumentKind::Paths:
                TranscodePaths(merged.doc, context);
                break;
        }
        report.accepted = true;
    } catch (const DocumentError& error) {
        report.error = error.what();
    } catch (const std::exception& error) {
        report.error = std::string("falha: ") + error.what();
    }
    report.notes.insert(report.notes.end(), context.notes.begin(), context.notes.end());
    if (!report.accepted) {
        return report;
    }
    // Salas e colisão aparecem uma vez por setup; o mesmo recurso em dois campos conta nos dois.
    std::set<std::pair<std::string, std::string>> seen;
    for (auto& reference : context.references) {
        if (seen.insert(reference).second) {
            report.references.push_back(std::move(reference));
        }
    }
    return report;
}

} // namespace LinkSpanUnbound
