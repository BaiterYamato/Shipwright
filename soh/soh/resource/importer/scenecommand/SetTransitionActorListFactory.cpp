#include <tinyxml2.h>
#include "src/code/linkspan_vanilla.h"

#include "soh/resource/importer/scenecommand/SetTransitionActorListFactory.h"
#include "soh/resource/type/scenecommand/SetTransitionActorList.h"
#include "soh/resource/logging/SceneCommandLoggers.h"

namespace SOH {
std::shared_ptr<Ship::IResource>
SetTransitionActorListFactory::ReadResource(std::shared_ptr<Ship::ResourceInitData> initData,
                                            std::shared_ptr<Ship::BinaryReader> reader) {
    auto setTransitionActorList = std::make_shared<SetTransitionActorList>(initData);

    ReadCommandId(setTransitionActorList, reader);

    setTransitionActorList->numTransitionActors = reader->ReadUInt32();
    setTransitionActorList->transitionActorList.reserve(setTransitionActorList->numTransitionActors);
    for (uint32_t i = 0; i < setTransitionActorList->numTransitionActors; i++) {
        TransitionActorEntry entry;

        entry.sides[0].room = reader->ReadInt8(); // SOH [Unbound] sign-extend: 0xFF means "no room"
        entry.sides[0].effects = reader->ReadUByte();
        entry.sides[1].room = reader->ReadInt8(); // SOH [Unbound] sign-extend: 0xFF means "no room"
        entry.sides[1].effects = reader->ReadUByte();
        entry.id = reader->ReadInt16();
        entry.pos.x = reader->ReadInt16();
        entry.pos.y = reader->ReadInt16();
        entry.pos.z = reader->ReadInt16();
        entry.rotY = reader->ReadInt16();
        entry.params = reader->ReadUInt16();

        setTransitionActorList->transitionActorList.push_back(entry);
    }

    if (CVarGetInteger(CVAR_DEVELOPER_TOOLS("ResourceLogging"), 0)) {
        LogTransitionActorListAsXML(setTransitionActorList);
    }

    return setTransitionActorList;
}

std::shared_ptr<Ship::IResource>
SetTransitionActorListFactoryXML::ReadResource(std::shared_ptr<Ship::ResourceInitData> initData,
                                               tinyxml2::XMLElement* reader) {
    auto setTransitionActorList = std::make_shared<SetTransitionActorList>(initData);

    setTransitionActorList->cmdId = SceneCommandID::SetTransitionActorList;

    auto child = reader->FirstChildElement();

    while (child != nullptr) {
        std::string childName = child->Name();
        if (childName == "TransitionActorEntry") {
            TransitionActorEntry entry;
            entry.sides[0].room = LinkSpan_EngineExtended() ? child->IntAttribute("FrontSideRoom")
                                                            : static_cast<s8>(child->IntAttribute("FrontSideRoom"));
            entry.sides[0].effects = child->IntAttribute("FrontSideEffects");
            entry.sides[1].room = LinkSpan_EngineExtended() ? child->IntAttribute("BackSideRoom")
                                                            : static_cast<s8>(child->IntAttribute("BackSideRoom"));
            entry.sides[1].effects = child->IntAttribute("BackSideEffects");
            entry.id = child->IntAttribute("Id");
            // SOH [Link-Span] HOST-SLIM-001 F4 (V7): XML conserva as conversoes s16 antigas sem mods.
            entry.pos.x = LinkSpan_EngineExtended() ? child->FloatAttribute("PosX")
                                                    : static_cast<s16>(child->IntAttribute("PosX"));
            entry.pos.y = LinkSpan_EngineExtended() ? child->FloatAttribute("PosY")
                                                    : static_cast<s16>(child->IntAttribute("PosY"));
            entry.pos.z = LinkSpan_EngineExtended() ? child->FloatAttribute("PosZ")
                                                    : static_cast<s16>(child->IntAttribute("PosZ"));
            entry.rotY = child->IntAttribute("RotY");
            entry.params = child->IntAttribute("Params");
            setTransitionActorList->transitionActorList.push_back(entry);
        }

        child = child->NextSiblingElement();
    }

    setTransitionActorList->numTransitionActors = static_cast<u32>(setTransitionActorList->transitionActorList.size());

    return setTransitionActorList;
}
} // namespace SOH
