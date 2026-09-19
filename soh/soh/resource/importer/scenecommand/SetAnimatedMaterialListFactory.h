#pragma once

#include "soh/resource/importer/scenecommand/SceneCommandFactory.h"

namespace SOH {
// SOH [Link-Span] OOT-CORE-008: forma XML do comando 0x1A (materialAnims do SPEC.md §4.2 do Unbound).
class SetAnimatedMaterialListFactoryXML final : public SceneCommandFactoryXMLV0 {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::ResourceInitData> initData,
                                                  tinyxml2::XMLElement* reader) override;
};
} // namespace SOH
