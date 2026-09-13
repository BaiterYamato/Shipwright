#include "soh/resource/importer/AnimationFactory.h"
#include "soh/resource/type/Animation.h"
#include <ship/resource/ResourceManager.h>
#include "spdlog/spdlog.h"
#include <ship/Context.h>
#include <string_view>

namespace SOH {
namespace {

// LinkAnimationHeader stores its keyframe segment as a separate resource path.
// Archives produced for a standalone game use an unqualified path such as
// "misc/link_animetion/...".  A cross-world resource keeps that internal path,
// even though its header is exposed to OoT as "mm/...".  The unqualified path
// can collide with OoT's own link-animation archive, so prefer the MM
// namespace when the owner animation came from the mounted MM archive.
std::string ResolveCrossWorldLinkAnimationSegmentPath(const std::string& ownerPath, const std::string& segmentPath) {
    constexpr std::string_view kOtrPrefix = "__OTR__";
    constexpr std::string_view kMmPrefix = "mm/";

    std::string_view owner = ownerPath;
    if (owner.starts_with(kOtrPrefix)) {
        owner.remove_prefix(kOtrPrefix.size());
    }
    if (!owner.starts_with(kMmPrefix)) {
        return {};
    }

    std::string_view segment = segmentPath;
    if (segment.starts_with(kOtrPrefix)) {
        segment.remove_prefix(kOtrPrefix.size());
    }
    if (segment.starts_with(kMmPrefix)) {
        return {};
    }

    return std::string(kOtrPrefix) + std::string(kMmPrefix) + std::string(segment);
}

} // namespace

std::shared_ptr<Ship::IResource>
ResourceFactoryBinaryAnimationV0::ReadResource(std::shared_ptr<Ship::File> file,
                                               std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto animation = std::make_shared<Animation>(initData);
    auto reader = std::get<std::shared_ptr<Ship::BinaryReader>>(file->Reader);

    AnimationType animType = (AnimationType)reader->ReadUInt32();
    animation->type = animType;

    if (animType == AnimationType::Normal) {
        // Set frame count
        animation->animationData.animationHeader.common.frameCount = reader->ReadInt16();

        // Populate frame data
        uint32_t rotValuesCnt = reader->ReadUInt32();
        animation->rotationValues.reserve(rotValuesCnt);
        for (uint32_t i = 0; i < rotValuesCnt; i++) {
            animation->rotationValues.push_back(reader->ReadUInt16());
        }
        animation->animationData.animationHeader.frameData = (int16_t*)animation->rotationValues.data();

        // Populate joint indices
        uint32_t rotIndCnt = reader->ReadUInt32();
        animation->rotationIndices.reserve(rotIndCnt);
        for (size_t i = 0; i < rotIndCnt; i++) {
            uint16_t x = reader->ReadUInt16();
            uint16_t y = reader->ReadUInt16();
            uint16_t z = reader->ReadUInt16();
            animation->rotationIndices.push_back(RotationIndex(x, y, z));
        }
        animation->animationData.animationHeader.jointIndices = (JointIndex*)animation->rotationIndices.data();

        // Set static index max
        animation->animationData.animationHeader.staticIndexMax = reader->ReadInt16();
    } else if (animType == AnimationType::Curve) {
        // Read frame count (unused in this animation type)
        reader->ReadInt16();

        // Set refIndex
        uint32_t refArrCnt = reader->ReadUInt32();
        animation->refIndexArr.reserve(refArrCnt);
        for (uint32_t i = 0; i < refArrCnt; i++) {
            animation->refIndexArr.push_back(reader->ReadUByte());
        }
        animation->animationData.transformUpdateIndex.refIndex = animation->refIndexArr.data();

        // Populate transform data
        uint32_t transformDataCnt = reader->ReadUInt32();
        animation->transformDataArr.reserve(transformDataCnt);
        for (uint32_t i = 0; i < transformDataCnt; i++) {
            TransformData data;
            data.unk_00 = reader->ReadUInt16();
            data.unk_02 = reader->ReadInt16();
            data.unk_04 = reader->ReadInt16();
            data.unk_06 = reader->ReadInt16();
            data.unk_08 = reader->ReadFloat();

            animation->transformDataArr.push_back(data);
        }
        animation->animationData.transformUpdateIndex.transformData = animation->transformDataArr.data();

        // Populate copy values
        uint32_t copyValuesCnt = reader->ReadUInt32();
        animation->copyValuesArr.reserve(copyValuesCnt);
        for (uint32_t i = 0; i < copyValuesCnt; i++) {
            animation->copyValuesArr.push_back(reader->ReadInt16());
        }
        animation->animationData.transformUpdateIndex.copyValues = animation->copyValuesArr.data();
    } else if (animType == AnimationType::Link) {
        // Initialize segment to nullptr (important for alt asset fallback)
        animation->animationData.linkAnimationHeader.segment = nullptr;

        // Read the frame count
        animation->animationData.linkAnimationHeader.common.frameCount = reader->ReadInt16();

        // Read the segment pointer (always 32 bit, doesn't adjust for system pointer size)
        std::string path = reader->ReadString();
        std::shared_ptr<Animation> animData = nullptr;
        std::string crossWorldPath;
        if (initData != nullptr) {
            crossWorldPath = ResolveCrossWorldLinkAnimationSegmentPath(initData->Path, path);
        }

        // MM Link-animation headers retain their original, unqualified keyframe path after being mounted below
        // mm/. Resolve the matching cross-world blob first: a same-named OoT blob may otherwise be accepted
        // silently and deform the skeleton.
        if (!crossWorldPath.empty()) {
            animData = std::static_pointer_cast<Animation>(
                Ship::Context::GetRawInstance()->GetResourceManager()->LoadResourceProcess(crossWorldPath.c_str()));
        }
        if (animData == nullptr) {
            animData = std::static_pointer_cast<Animation>(
                Ship::Context::GetRawInstance()->GetResourceManager()->LoadResourceProcess(path.c_str()));
        }

        // If direct load failed and alt assets are enabled, try with alt/ prefix
        if (animData == nullptr && Ship::Context::GetRawInstance()->GetResourceManager()->IsAltAssetsEnabled()) {
            std::string altPath = path;
            if (altPath.find("__OTR__") == 0) {
                altPath = altPath.substr(7); // Strip __OTR__
            }
            altPath = "alt/" + altPath;
            animData = std::static_pointer_cast<Animation>(
                Ship::Context::GetRawInstance()->GetResourceManager()->LoadResourceProcess(altPath.c_str()));
        }

        if (animData != nullptr) {
            animation->animationData.linkAnimationHeader.segment = animData->GetPointer();
        } else {
            SPDLOG_WARN("Animation data segment not found: {}", path);
        }
    } else if (animType == AnimationType::Legacy) {
        SPDLOG_DEBUG("BEYTAH ANIMATION?!");
    }

    return animation;
}
} // namespace SOH
