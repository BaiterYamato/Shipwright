#include "soh/resource/importer/scenecommand/SetAlternateHeadersFactory.h"
#include "soh/resource/type/scenecommand/SetAlternateHeaders.h"
#include "soh/resource/logging/SceneCommandLoggers.h"
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <tinyxml2.h>
#include "soh/resource/importer/SceneFactory.h"

namespace SOH {
std::shared_ptr<Ship::IResource>
SetAlternateHeadersFactory::ReadResource(std::shared_ptr<Ship::ResourceInitData> initData,
                                         std::shared_ptr<Ship::BinaryReader> reader) {
    auto setAlternateHeaders = std::make_shared<SetAlternateHeaders>(initData);

    ReadCommandId(setAlternateHeaders, reader);

    setAlternateHeaders->numHeaders = reader->ReadUInt32();
    setAlternateHeaders->headers.reserve(setAlternateHeaders->numHeaders);
    for (uint32_t i = 0; i < setAlternateHeaders->numHeaders; i++) {
        auto headerName = reader->ReadString();
        if (!headerName.empty()) {
            setAlternateHeaders->headers.push_back(std::static_pointer_cast<Scene>(
                Ship::Context::GetRawInstance()->GetResourceManager()->LoadResourceProcess(headerName.c_str())));
            setAlternateHeaders->headerFileNames.push_back(headerName);
        } else {
            setAlternateHeaders->headers.push_back(nullptr);
        }
    }

    if (CVarGetInteger(CVAR_DEVELOPER_TOOLS("ResourceLogging"), 0)) {
        LogAlternateHeadersAsXML(setAlternateHeaders);
    }

    return setAlternateHeaders;
}

std::shared_ptr<Ship::IResource>
SetAlternateHeadersFactoryXML::ReadResource(std::shared_ptr<Ship::ResourceInitData> initData,
                                            tinyxml2::XMLElement* reader) {
    auto setAlternateHeaders = std::make_shared<SetAlternateHeaders>(initData);

    setAlternateHeaders->cmdId = SceneCommandID::SetAlternateHeaders;

    auto child = reader->FirstChildElement();

    // SOH [Link-Span] OOT-CORE-008: um <AlternateHeader> por cabeçalho, na ordem. Com Path, carrega o
    // recurso; com comandos filhos, monta o cabeçalho em linha; vazio, fica nulo (o jogo cai no setup 0).
    // O laço antigo dependia de numHeaders, que ainda era 0 aqui, e não carregava nenhum cabeçalho.
    while (child != nullptr) {
        std::string childName = child->Name();
        if (childName == "AlternateHeader") {
            const char* path = child->Attribute("Path");
            const std::string headerName = path ? path : "";
            if (!headerName.empty()) {
                setAlternateHeaders->headers.push_back(std::static_pointer_cast<Scene>(
                    Ship::Context::GetRawInstance()->GetResourceManager()->LoadResourceProcess(headerName.c_str())));
                setAlternateHeaders->headerFileNames.push_back(headerName);
            } else if (child->FirstChildElement() != nullptr) {
                auto header = std::make_shared<Scene>(initData);
                ResourceFactoryXMLSceneV0::ParseSceneCommandList(header, child);
                setAlternateHeaders->headers.push_back(header);
                setAlternateHeaders->headerFileNames.push_back(initData->Path + "#" +
                                                               std::to_string(setAlternateHeaders->headers.size()));
            } else {
                setAlternateHeaders->headers.push_back(nullptr);
                setAlternateHeaders->headerFileNames.push_back("");
            }
        }

        child = child->NextSiblingElement();
    }

    setAlternateHeaders->numHeaders = static_cast<u32>(setAlternateHeaders->headers.size());

    return setAlternateHeaders;
}
} // namespace SOH
