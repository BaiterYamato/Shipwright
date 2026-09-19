// SOH [Link-Span] OOT-CORE-008: <SetAnimatedMaterialList> em XML. Cada filho é uma entrada:
//
//   <TexScroll Segment="8" Pass="3" Type="0|1"> <Layer XStep YStep Width Height/> (1 ou 2) </TexScroll>
//   <Color Segment Pass Type="2|3|4" Length="64">
//       <KeyFrame Frame PrimR PrimG PrimB PrimA LodFrac [EnvR EnvG EnvB EnvA]/> ... </Color>
//   <TexCycle Segment Pass> <Texture Path/> ... <Frame Index/> ... </TexCycle>
//
// Pass usa os bits ANIM_MAT_PASS_* (1 opa, 2 xlu, 3 os dois). Uma entrada fora das regras do SPEC é
// descartada com erro no log; as demais ficam.
#include "soh/resource/importer/scenecommand/SetAnimatedMaterialListFactory.h"
#include "soh/resource/type/scenecommand/SetAnimatedMaterialList.h"

#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <tinyxml2.h>

namespace SOH {
namespace {

struct EntryError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

u8 ReadSegment(tinyxml2::XMLElement* e) {
    const int segment = e->IntAttribute("Segment", -1);
    if (segment < ANIM_MAT_SEGMENT_MIN || segment > ANIM_MAT_SEGMENT_MAX) {
        throw EntryError("Segment " + std::to_string(segment) + " is outside 8..13");
    }
    return static_cast<u8>(segment);
}

u8 ReadPass(tinyxml2::XMLElement* e) {
    const unsigned pass = e->UnsignedAttribute("Pass", ANIM_MAT_PASS_OPA | ANIM_MAT_PASS_XLU);
    if (pass == 0 || pass > (ANIM_MAT_PASS_OPA | ANIM_MAT_PASS_XLU)) {
        throw EntryError("Pass " + std::to_string(pass) + " is not 1, 2 or 3");
    }
    return static_cast<u8>(pass);
}

void AddScroll(SetAnimatedMaterialList& list, tinyxml2::XMLElement* e) {
    const int type = e->IntAttribute("Type", ANIM_MAT_TEX_SCROLL);
    if (type != ANIM_MAT_TEX_SCROLL && type != ANIM_MAT_TWO_TEX_SCROLL) {
        throw EntryError("TexScroll Type " + std::to_string(type) + " is not 0 or 1");
    }
    const size_t want = type == ANIM_MAT_TWO_TEX_SCROLL ? 2 : 1;
    SetAnimatedMaterialList::ScrollStorage storage{};
    size_t count = 0;
    for (auto* layer = e->FirstChildElement("Layer"); layer != nullptr; layer = layer->NextSiblingElement("Layer")) {
        if (count < 2) {
            auto& p = storage.layers[count];
            p.xStep = static_cast<s8>(layer->IntAttribute("XStep"));
            p.yStep = static_cast<s8>(layer->IntAttribute("YStep"));
            p.width = static_cast<u8>(layer->UnsignedAttribute("Width"));
            p.height = static_cast<u8>(layer->UnsignedAttribute("Height"));
        }
        ++count;
    }
    if (count != want) {
        throw EntryError("TexScroll has " + std::to_string(count) + " layers; this type takes " + std::to_string(want));
    }
    list.AddScroll(ReadSegment(e), ReadPass(e), static_cast<u8>(type), storage);
}

void AddColor(SetAnimatedMaterialList& list, tinyxml2::XMLElement* e) {
    const int type = e->IntAttribute("Type", ANIM_MAT_COLOR);
    if (type < ANIM_MAT_COLOR || type > ANIM_MAT_COLOR_NON_LINEAR) {
        throw EntryError("Color Type " + std::to_string(type) + " is not 2, 3 or 4");
    }
    const int64_t length = e->Int64Attribute("Length");
    if (length < 1 || length > UINT16_MAX) {
        throw EntryError("Length " + std::to_string(length) + " is outside 1..65535");
    }
    SetAnimatedMaterialList::ColorStorage storage{};
    storage.params.keyFrameLength = static_cast<u16>(length);
    bool anyEnv = false;
    bool allEnv = true;
    for (auto* key = e->FirstChildElement("KeyFrame"); key != nullptr; key = key->NextSiblingElement("KeyFrame")) {
        const u16 frame = static_cast<u16>(key->UnsignedAttribute("Frame"));
        if (!storage.keyFrames.empty() && frame <= storage.keyFrames.back()) {
            throw EntryError("key frames must be ascending");
        }
        storage.keyFrames.push_back(frame);
        storage.primColors.push_back({ static_cast<u8>(key->UnsignedAttribute("PrimR")),
                                       static_cast<u8>(key->UnsignedAttribute("PrimG")),
                                       static_cast<u8>(key->UnsignedAttribute("PrimB")),
                                       static_cast<u8>(key->UnsignedAttribute("PrimA")),
                                       static_cast<u8>(key->UnsignedAttribute("LodFrac")) });
        const bool hasEnv = key->FindAttribute("EnvR") != nullptr;
        anyEnv = anyEnv || hasEnv;
        allEnv = allEnv && hasEnv;
        storage.envColors.push_back({ static_cast<u8>(key->UnsignedAttribute("EnvR")),
                                      static_cast<u8>(key->UnsignedAttribute("EnvG")),
                                      static_cast<u8>(key->UnsignedAttribute("EnvB")),
                                      static_cast<u8>(key->UnsignedAttribute("EnvA")) });
    }
    if (storage.keyFrames.empty() || storage.keyFrames.size() > ANIM_MAT_MAX_KEY_FRAMES ||
        storage.keyFrames[0] != 0) {
        throw EntryError("key frames need 1.." + std::to_string(ANIM_MAT_MAX_KEY_FRAMES) + " entries starting at 0");
    }
    if (anyEnv != allEnv) {
        throw EntryError("env colors must be on every key frame or on none");
    }
    if (!anyEnv) {
        storage.envColors.clear();
    }
    list.AddColor(ReadSegment(e), ReadPass(e), static_cast<u8>(type), std::move(storage));
}

void AddCycle(SetAnimatedMaterialList& list, tinyxml2::XMLElement* e) {
    SetAnimatedMaterialList::CycleStorage storage{};
    for (auto* texture = e->FirstChildElement("Texture"); texture != nullptr;
         texture = texture->NextSiblingElement("Texture")) {
        const char* path = texture->Attribute("Path");
        if (path == nullptr) {
            throw EntryError("a Texture has no Path");
        }
        storage.texturePaths.push_back(std::string("__OTR__") + path);
    }
    if (storage.texturePaths.size() > UINT16_MAX + 1) {
        throw EntryError("more than 65536 textures");
    }
    for (auto* frame = e->FirstChildElement("Frame"); frame != nullptr; frame = frame->NextSiblingElement("Frame")) {
        const int64_t index = frame->Int64Attribute("Index", -1);
        if (index < 0 || index >= static_cast<int64_t>(storage.texturePaths.size())) {
            throw EntryError("frame index " + std::to_string(index) + " is not a texture index");
        }
        storage.frames.push_back(static_cast<u16>(index));
    }
    if (storage.frames.empty() || storage.frames.size() > UINT16_MAX) {
        throw EntryError("frames need 1..65535 entries");
    }
    list.AddCycle(ReadSegment(e), ReadPass(e), std::move(storage));
}

} // namespace

std::shared_ptr<Ship::IResource>
SetAnimatedMaterialListFactoryXML::ReadResource(std::shared_ptr<Ship::ResourceInitData> initData,
                                                tinyxml2::XMLElement* reader) {
    auto list = std::make_shared<SetAnimatedMaterialList>(initData);
    list->cmdId = SceneCommandID::SetAnimatedMaterialList;
    size_t index = 0;
    for (auto* child = reader->FirstChildElement(); child != nullptr; child = child->NextSiblingElement(), ++index) {
        const std::string name = child->Name();
        try {
            if (name == "TexScroll") {
                AddScroll(*list, child);
            } else if (name == "Color") {
                AddColor(*list, child);
            } else if (name == "TexCycle") {
                AddCycle(*list, child);
            } else {
                throw EntryError("unknown entry <" + name + ">");
            }
        } catch (const EntryError& error) {
            SPDLOG_ERROR("{}: animated material {}: {}; entry dropped", initData->Path, index, error.what());
        }
    }
    return list;
}

} // namespace SOH
