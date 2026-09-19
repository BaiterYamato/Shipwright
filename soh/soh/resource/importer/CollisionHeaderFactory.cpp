#include <tinyxml2.h>

#include "soh/resource/importer/CollisionHeaderFactory.h"
#include "soh/resource/type/CollisionHeader.h"
#include "soh/unbound/CollisionVertexWords.h"
#include "spdlog/spdlog.h"
#include <tinyxml2.h>
#include <cstddef>
#include <cstring>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include "z64bgcheck.h"

namespace SOH {
// SOH [Unbound] The mirrors are cast to the game structs by z_scene_otr.cpp; the game helpers own the bit layout.
static_assert(sizeof(SurfaceType) == sizeof(::SurfaceType), "SOH::SurfaceType must mirror ::SurfaceType");
static_assert(sizeof(WaterBox) == sizeof(::WaterBox), "SOH::WaterBox must mirror ::WaterBox");
static_assert(offsetof(SurfaceType, lightSetting) == offsetof(::SurfaceType, lightSetting) &&
                  offsetof(SurfaceType, isWallDamage) == offsetof(::SurfaceType, isWallDamage),
              "SOH::SurfaceType field order must mirror ::SurfaceType");
static_assert(offsetof(WaterBox, room) == offsetof(::WaterBox, room),
              "SOH::WaterBox field order must mirror ::WaterBox");
// SOH [Unbound] The widened structs too: a mirror that lags a widening reads every field after it as garbage.
static_assert(sizeof(CollisionPoly) == sizeof(::CollisionPoly) &&
                  offsetof(CollisionPoly, dist) == offsetof(::CollisionPoly, dist),
              "SOH::CollisionPoly must mirror ::CollisionPoly");
static_assert(sizeof(CamData) == sizeof(::CamData), "SOH::CamData must mirror ::CamData");
static_assert(sizeof(CollisionHeaderData) == sizeof(::CollisionHeader) &&
                  offsetof(CollisionHeaderData, vtxList) == offsetof(::CollisionHeader, vtxList) &&
                  offsetof(CollisionHeaderData, waterBoxes) == offsetof(::CollisionHeader, waterBoxes) &&
                  offsetof(CollisionHeaderData, cameraDataListLen) == offsetof(::CollisionHeader, cameraDataListLen),
              "SOH::CollisionHeaderData must mirror ::CollisionHeader");

SurfaceType UnpackSurfaceType(uint32_t data0, uint32_t data1) {
    ::SurfaceType game = SurfaceType_Unpack(data0, data1);
    SurfaceType out;
    std::memcpy(&out, &game, sizeof(out));
    return out;
}

void UnpackWaterBoxProperties(WaterBox& waterBox, uint32_t properties) {
    WaterBox_UnpackProperties(reinterpret_cast<::WaterBox*>(&waterBox), properties);
}
} // namespace SOH

namespace SOH {

// SOH [Unbound] The legacy (N64) poly layout packs a 13-bit vertex index and 3 flag bits into
// each u16. The in-memory CollisionPoly is 32-bit with the flags in bits 29-31; unpack here so
// every existing archive keeps loading. See unbound-docs/SPEC.md §8 (and collision.md for the why).
static uint32_t UnpackLegacyVtxWord(uint16_t packed) {
    return (uint32_t)(packed & 0x1FFF) | ((uint32_t)(packed >> 13) << 29);
}

static uint32_t PackVtxWord(uint32_t index, uint32_t flags3) {
    return (index & 0x1FFFFFFFu) | ((flags3 & 7u) << 29);
}

// SOH [Link-Span] OOT-CORE-008: um CollisionHeader XML pode trazer vértices e polígonos num arquivo
// binário (o collision.bin do SPEC.md §4.4.1 do Unbound): little-endian, sem cabeçalho, vértices
// { s32 x, y, z } e depois polígonos { u16 type, u16 pad, u32 vA, vB, vC, s16 nx, ny, nz, s16 pad, s32 dist }.
namespace {
constexpr size_t kBulkVertexBytes = 12;
constexpr size_t kBulkPolyBytes = 28;

uint32_t LoadU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint16_t LoadU16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

bool ReadCollisionBulk(CollisionHeader& col, const std::string& docPath, const char* binPath, uint32_t numVertices,
                       uint32_t numPolys) {
    auto file = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->LoadFile(binPath);
    if (file == nullptr || !file->IsLoaded || file->Buffer == nullptr) {
        SPDLOG_ERROR("{}: collision bulk {} missing", docPath, binPath);
        return false;
    }
    const size_t need = (size_t)numVertices * kBulkVertexBytes + (size_t)numPolys * kBulkPolyBytes;
    if (file->Buffer->size() < need) {
        SPDLOG_ERROR("{}: collision bulk {} is shorter than its declared counts", docPath, binPath);
        return false;
    }
    const uint8_t* p = reinterpret_cast<const uint8_t*>(file->Buffer->data());
    col.vertices.reserve(col.vertices.size() + numVertices);
    for (uint32_t i = 0; i < numVertices; i++, p += kBulkVertexBytes) {
        Vec3i v;
        v.x = (int32_t)LoadU32(p);
        v.y = (int32_t)LoadU32(p + 4);
        v.z = (int32_t)LoadU32(p + 8);
        col.vertices.push_back(v);
    }
    col.polygons.reserve(col.polygons.size() + numPolys);
    for (uint32_t i = 0; i < numPolys; i++, p += kBulkPolyBytes) {
        CollisionPoly poly{};
        poly.type = LoadU16(p);
        poly.flags_vIA = LoadU32(p + 4);
        poly.flags_vIB = LoadU32(p + 8);
        poly.vIC = LoadU32(p + 12);
        poly.normal.x = (int16_t)LoadU16(p + 16);
        poly.normal.y = (int16_t)LoadU16(p + 18);
        poly.normal.z = (int16_t)LoadU16(p + 20);
        poly.dist = (int32_t)LoadU32(p + 24);
        col.polygons.push_back(poly);
    }
    return true;
}
} // namespace

std::shared_ptr<Ship::IResource>
ResourceFactoryBinaryCollisionHeaderV0::ReadResource(std::shared_ptr<Ship::File> file,
                                                     std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto collisionHeader = std::make_shared<CollisionHeader>(initData);
    auto reader = std::get<std::shared_ptr<Ship::BinaryReader>>(file->Reader);

    collisionHeader->collisionHeaderData.minBounds.x = reader->ReadInt16();
    collisionHeader->collisionHeaderData.minBounds.y = reader->ReadInt16();
    collisionHeader->collisionHeaderData.minBounds.z = reader->ReadInt16();

    collisionHeader->collisionHeaderData.maxBounds.x = reader->ReadInt16();
    collisionHeader->collisionHeaderData.maxBounds.y = reader->ReadInt16();
    collisionHeader->collisionHeaderData.maxBounds.z = reader->ReadInt16();

    collisionHeader->collisionHeaderData.numVertices = reader->ReadInt32();
    collisionHeader->vertices.reserve(collisionHeader->collisionHeaderData.numVertices);
    for (uint32_t i = 0; i < collisionHeader->collisionHeaderData.numVertices; i++) {
        Vec3i vtx; // SOH [Unbound] collision vertices are s32; the vanilla source is s16, so this is exact
        vtx.x = reader->ReadInt16();
        vtx.y = reader->ReadInt16();
        vtx.z = reader->ReadInt16();
        collisionHeader->vertices.push_back(vtx);
    }
    collisionHeader->collisionHeaderData.vtxList = collisionHeader->vertices.data();

    collisionHeader->collisionHeaderData.numPolygons = reader->ReadUInt32();
    collisionHeader->polygons.reserve(collisionHeader->collisionHeaderData.numPolygons);
    for (uint32_t i = 0; i < collisionHeader->collisionHeaderData.numPolygons; i++) {
        CollisionPoly polygon;

        polygon.type = reader->ReadUInt16();

        polygon.flags_vIA = UnpackLegacyVtxWord(reader->ReadUInt16());
        polygon.flags_vIB = UnpackLegacyVtxWord(reader->ReadUInt16());
        polygon.vIC = UnpackLegacyVtxWord(reader->ReadUInt16());

        polygon.normal.x = reader->ReadUInt16();
        polygon.normal.y = reader->ReadUInt16();
        polygon.normal.z = reader->ReadUInt16();

        polygon.dist = reader->ReadInt16(); // SOH [Unbound] dist is s32 now; the u16 read no longer wraps negative

        collisionHeader->polygons.push_back(polygon);
    }
    collisionHeader->collisionHeaderData.polyList = collisionHeader->polygons.data();

    collisionHeader->surfaceTypesCount = reader->ReadUInt32();
    collisionHeader->surfaceTypes.reserve(collisionHeader->surfaceTypesCount);
    for (uint32_t i = 0; i < collisionHeader->surfaceTypesCount; i++) {
        uint32_t data1 = reader->ReadUInt32();
        uint32_t data0 = reader->ReadUInt32();

        collisionHeader->surfaceTypes.push_back(UnpackSurfaceType(data0, data1)); // SOH [Unbound]
    }
    collisionHeader->collisionHeaderData.surfaceTypeList = collisionHeader->surfaceTypes.data();

    collisionHeader->camDataCount = reader->ReadUInt32();
    collisionHeader->camData.reserve(collisionHeader->camDataCount);
    collisionHeader->camPosDataIndices.reserve(collisionHeader->camDataCount);
    for (uint32_t i = 0; i < collisionHeader->camDataCount; i++) {
        CamData camDataEntry;
        camDataEntry.cameraSType = reader->ReadUInt16();
        camDataEntry.numCameras = reader->ReadInt16();
        collisionHeader->camData.push_back(camDataEntry);

        int32_t camPosDataIdx = reader->ReadInt32();
        collisionHeader->camPosDataIndices.push_back(camPosDataIdx);
    }

    collisionHeader->camPosCount = reader->ReadInt32();
    collisionHeader->camPosData.reserve(collisionHeader->camPosCount);
    for (int32_t i = 0; i < collisionHeader->camPosCount; i++) {
        Vec3s pos;
        pos.x = reader->ReadInt16();
        pos.y = reader->ReadInt16();
        pos.z = reader->ReadInt16();
        collisionHeader->camPosData.push_back(pos);
    }

    Vec3s zero;
    zero.x = 0;
    zero.y = 0;
    zero.z = 0;
    collisionHeader->camPosDataZero = zero;

    for (size_t i = 0; i < collisionHeader->camDataCount; i++) {
        int32_t idx = collisionHeader->camPosDataIndices[i];

        if (collisionHeader->camPosCount > 0) {
            collisionHeader->camData[i].camPosData = &collisionHeader->camPosData[idx];
        } else {
            collisionHeader->camData[i].camPosData = &collisionHeader->camPosDataZero;
        }
    }
    collisionHeader->collisionHeaderData.cameraDataList = collisionHeader->camData.data();
    collisionHeader->collisionHeaderData.cameraDataListLen = collisionHeader->camDataCount;

    collisionHeader->collisionHeaderData.numWaterBoxes = reader->ReadInt32();
    collisionHeader->waterBoxes.reserve(collisionHeader->collisionHeaderData.numWaterBoxes);
    for (int32_t i = 0; i < collisionHeader->collisionHeaderData.numWaterBoxes; i++) {
        WaterBox waterBox;
        waterBox.xMin = reader->ReadInt16();
        waterBox.ySurface = reader->ReadInt16();
        waterBox.zMin = reader->ReadInt16();
        waterBox.xLength = reader->ReadInt16();
        waterBox.zLength = reader->ReadInt16();
        UnpackWaterBoxProperties(waterBox, reader->ReadUInt32()); // SOH [Unbound]

        collisionHeader->waterBoxes.push_back(waterBox);
    }
    collisionHeader->collisionHeaderData.waterBoxes = collisionHeader->waterBoxes.data();

    return collisionHeader;
}

std::shared_ptr<Ship::IResource>
ResourceFactoryXMLCollisionHeaderV0::ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto collisionHeader = std::make_shared<CollisionHeader>(initData);

    auto reader = std::get<std::shared_ptr<tinyxml2::XMLDocument>>(file->Reader)->FirstChildElement();
    auto child = reader->FirstChildElement();

    collisionHeader->collisionHeaderData.minBounds.x = reader->IntAttribute("MinBoundsX");
    collisionHeader->collisionHeaderData.minBounds.y = reader->IntAttribute("MinBoundsY");
    collisionHeader->collisionHeaderData.minBounds.z = reader->IntAttribute("MinBoundsZ");

    collisionHeader->collisionHeaderData.maxBounds.x = reader->IntAttribute("MaxBoundsX");
    collisionHeader->collisionHeaderData.maxBounds.y = reader->IntAttribute("MaxBoundsY");
    collisionHeader->collisionHeaderData.maxBounds.z = reader->IntAttribute("MaxBoundsZ");

    Vec3s zero;
    zero.x = 0;
    zero.y = 0;
    zero.z = 0;
    collisionHeader->camPosDataZero = zero;

    if (const char* bulk = reader->Attribute("BulkFile")) {
        if (!ReadCollisionBulk(*collisionHeader, initData->Path, bulk, reader->UnsignedAttribute("BulkVertices"),
                               reader->UnsignedAttribute("BulkPolys"))) {
            return nullptr;
        }
    }

    while (child != nullptr) {
        std::string childName = child->Name();
        if (childName == "Vertex") {
            Vec3i vtx; // SOH [Unbound] collision vertices are s32; the attributes are integers already
            vtx.x = child->IntAttribute("X");
            vtx.y = child->IntAttribute("Y");
            vtx.z = child->IntAttribute("Z");
            collisionHeader->vertices.push_back(vtx);
        } else if (childName == "Polygon") {
            CollisionPoly polygon;

            polygon.type = child->UnsignedAttribute("Type");

            // SOH [Unbound] New form: VertexA/B/C are plain indices, flags live in XpFlags / Conveyor.
            // Legacy form (neither attribute present): VertexA/B carry the N64 packed u16 words.
            if (child->FindAttribute("XpFlags") != nullptr || child->FindAttribute("Conveyor") != nullptr) {
                polygon.flags_vIA =
                    PackVtxWord(child->UnsignedAttribute("VertexA"), child->UnsignedAttribute("XpFlags"));
                polygon.flags_vIB =
                    PackVtxWord(child->UnsignedAttribute("VertexB"), child->BoolAttribute("Conveyor") ? 1 : 0);
            } else {
                polygon.flags_vIA = UnpackLegacyVtxWord((uint16_t)child->UnsignedAttribute("VertexA"));
                polygon.flags_vIB = UnpackLegacyVtxWord((uint16_t)child->UnsignedAttribute("VertexB"));
            }
            polygon.vIC = child->FindAttribute("XpFlags") != nullptr || child->FindAttribute("Conveyor") != nullptr
                              ? child->UnsignedAttribute("VertexC")
                              : UnpackLegacyVtxWord((uint16_t)child->UnsignedAttribute("VertexC"));

            polygon.normal.x = child->IntAttribute("NormalX");
            polygon.normal.y = child->IntAttribute("NormalY");
            polygon.normal.z = child->IntAttribute("NormalZ");

            polygon.dist = child->IntAttribute("Dist");

            collisionHeader->polygons.push_back(polygon);
        } else if (childName == "PolygonType") {
            // SOH [Unbound]
            collisionHeader->surfaceTypes.push_back(
                UnpackSurfaceType(child->UnsignedAttribute("Data1"), child->UnsignedAttribute("Data2")));
        } else if (childName == "SurfaceType") {
            // SOH [Link-Span] OOT-CORE-008: campos já separados, sem os limites dos words empacotados.
            SurfaceType surface{};
            surface.camera = child->IntAttribute("Camera");
            surface.exit = child->IntAttribute("Exit");
            surface.lightSetting = child->IntAttribute("LightSetting");
            surface.floorType = (uint8_t)child->UnsignedAttribute("FloorType");
            surface.wallFlags = (uint8_t)child->UnsignedAttribute("WallFlags");
            surface.wallType = (uint8_t)child->UnsignedAttribute("WallType");
            surface.floorProperty = (uint8_t)child->UnsignedAttribute("FloorProperty");
            surface.isSoft = (uint8_t)child->UnsignedAttribute("IsSoft");
            surface.isHorseBlocked = (uint8_t)child->UnsignedAttribute("IsHorseBlocked");
            surface.material = (uint8_t)child->UnsignedAttribute("Material");
            surface.floorEffect = (uint8_t)child->UnsignedAttribute("FloorEffect");
            surface.echo = (uint8_t)child->UnsignedAttribute("Echo");
            surface.canHookshot = (uint8_t)child->UnsignedAttribute("CanHookshot");
            surface.conveyorSpeed = (uint8_t)child->UnsignedAttribute("ConveyorSpeed");
            surface.conveyorDirection = (uint8_t)child->UnsignedAttribute("ConveyorDirection");
            surface.isWallDamage = (uint8_t)child->UnsignedAttribute("IsWallDamage");
            collisionHeader->surfaceTypes.push_back(surface);
        } else if (childName == "CameraData") {
            CamData camDataEntry;
            camDataEntry.cameraSType = child->UnsignedAttribute("SType");
            camDataEntry.numCameras = child->IntAttribute("NumData");
            collisionHeader->camData.push_back(camDataEntry);

            int32_t camPosDataIdx = child->IntAttribute("CameraPosDataSeg");
            collisionHeader->camPosDataIndices.push_back(camPosDataIdx);
        } else if (childName == "CameraPositionData") {
            // each camera position data is made up of 3 Vec3s
            Vec3s pos;
            pos.x = child->IntAttribute("PosX");
            pos.y = child->IntAttribute("PosY");
            pos.z = child->IntAttribute("PosZ");
            collisionHeader->camPosData.push_back(pos);
            Vec3s rot;
            rot.x = child->IntAttribute("RotX");
            rot.y = child->IntAttribute("RotY");
            rot.z = child->IntAttribute("RotZ");
            collisionHeader->camPosData.push_back(rot);
            Vec3s other;
            other.x = child->IntAttribute("FOV");
            other.y = child->IntAttribute("JfifID");
            other.z = child->IntAttribute("Unknown");
            collisionHeader->camPosData.push_back(other);
        } else if (childName == "WaterBox") {
            WaterBox waterBox;
            waterBox.xMin = child->IntAttribute("XMin");
            waterBox.ySurface = child->IntAttribute("Ysurface");
            waterBox.zMin = child->IntAttribute("ZMin");
            waterBox.xLength = child->IntAttribute("XLength");
            waterBox.zLength = child->IntAttribute("ZLength");
            if (child->FindAttribute("Room") != nullptr) {
                // SOH [Link-Span] OOT-CORE-008: propriedades separadas (SPEC.md §4.4 do Unbound).
                waterBox.camera = child->IntAttribute("Camera");
                waterBox.lightSetting = child->IntAttribute("LightSetting");
                waterBox.room = child->IntAttribute("Room");
                waterBox.notSwimmable = child->BoolAttribute("NotSwimmable") ? 1 : 0;
            } else {
                UnpackWaterBoxProperties(waterBox, child->UnsignedAttribute("Properties")); // SOH [Unbound]
            }

            collisionHeader->waterBoxes.push_back(waterBox);
        }

        child = child->NextSiblingElement();
    }

    for (size_t i = 0; i < collisionHeader->camData.size(); i++) {
        int32_t idx = collisionHeader->camPosDataIndices[i];

        // SOH [Link-Span] OOT-CORE-008: índice negativo (sem posição) ou além da lista usa a posição zero.
        if (idx >= 0 && static_cast<size_t>(idx) < collisionHeader->camPosData.size()) {
            collisionHeader->camData[i].camPosData = &collisionHeader->camPosData[idx];
        } else {
            collisionHeader->camData[i].camPosData = &collisionHeader->camPosDataZero;
        }
    }

    if (collisionHeader->waterBoxes.size() > UINT16_MAX) {
        SPDLOG_ERROR("{}: {} water boxes (at most {})", initData->Path, collisionHeader->waterBoxes.size(), UINT16_MAX);
        return nullptr;
    }

    collisionHeader->collisionHeaderData.numVertices = static_cast<u32>(collisionHeader->vertices.size());
    collisionHeader->collisionHeaderData.numPolygons = static_cast<u32>(collisionHeader->polygons.size());
    collisionHeader->surfaceTypesCount = static_cast<uint32_t>(collisionHeader->surfaceTypes.size());
    collisionHeader->camDataCount = static_cast<uint32_t>(collisionHeader->camData.size());
    collisionHeader->camPosCount = static_cast<int32_t>(collisionHeader->camPosData.size());
    collisionHeader->collisionHeaderData.numWaterBoxes = static_cast<u16>(collisionHeader->waterBoxes.size());

    collisionHeader->collisionHeaderData.vtxList = collisionHeader->vertices.data();
    collisionHeader->collisionHeaderData.polyList = collisionHeader->polygons.data();
    collisionHeader->collisionHeaderData.surfaceTypeList = collisionHeader->surfaceTypes.data();
    collisionHeader->collisionHeaderData.cameraDataList = collisionHeader->camData.data();
    collisionHeader->collisionHeaderData.cameraDataListLen = collisionHeader->camDataCount;
    collisionHeader->collisionHeaderData.waterBoxes = collisionHeader->waterBoxes.data();

    return collisionHeader;
}
} // namespace SOH
