#include "WorldPass.h"
#include "PassSource.h"
#include "SunShadow.h"

#ifdef _WIN32
// libultraship always builds its DX11 backend on Windows. Its definition is private to that target.
#ifndef ENABLE_DX11
#define ENABLE_DX11
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "GeometryShadow.h"
#include "soh/Enhancements/Graphics/WaterRipple/WaterRipple.h"
#include "soh/Enhancements/Graphics/Decals/ImpactDecals.h"
#include "soh/Enhancements/Graphics/Weather/WetSurface.h"
#include <wrl/client.h>
#include <fast/Fast3dWindow.h>
#include <fast/backends/gfx_direct3d_common.h>
#include <fast/world_postprocess.h>
#include <ship/Context.h>
#include <ship/config/ConsoleVariable.h>
#include <spdlog/spdlog.h>
#include <d3d11shader.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>

extern "C" {
#include "z64.h"
extern PlayState* gPlayState;
extern SaveContext gSaveContext;
void Matrix_MtxToMtxF(Mtx* source, MtxF* destination);
}

namespace {

using Microsoft::WRL::ComPtr;
using ShaderPacks::MaxPassInputs;
using ShaderPacks::SceneColorInput;
using ShaderPacks::SceneDepthInput;
using ShaderPacks::SceneFogShadowInput;
using ShaderPacks::SceneGeometryShadowInput;
using ShaderPacks::ScenePointShadowInput;
using ShaderPacks::SceneSunShadowInput;
using ShaderPacks::SunShadowMap;
using ShaderPacks::SunShadowMesh;
using ShaderPacks::UnboundInput;

struct CameraConstants {
    float projection[4][4];
    float view[4][4];
    float info[4]; // Near, far, interpolation fraction, replaced-matrix flags.
};
static_assert(sizeof(CameraConstants) == 9 * 16);

using SceneVector = std::array<float, 4>;
constexpr size_t TransmissionTriangleOffset = 1 + 2 * ShaderPacks::MaxTransmissionMaterials;
using TransmissionConstants =
    std::array<SceneVector, TransmissionTriangleOffset + 3 * Fast::MaxWorldTransmissionTriangles>;
struct PointShadowConstants {
    SceneVector source;
    SceneVector light;
};
static_assert(sizeof(PointShadowConstants) == 2 * 16);
constexpr size_t MaxPointLights = ShaderPacks::GeometryShadow::MaxPointLights;
constexpr size_t MaxSceneLights = 8;
constexpr size_t MaxWaterBoxes = 8;
bool UsesSolarGeometry(uint16_t scene) {
    return scene == SCENE_KOKIRI_FOREST || scene == SCENE_LOST_WOODS || scene == SCENE_GRAVEYARD ||
           scene == SCENE_HYRULE_FIELD;
}

struct SceneConstants {
    SceneVector info; // Scene, room, render time in seconds, day fraction.
    SceneVector sun;
    SceneVector counts; // Lights, water boxes, lights selected for point shadows.
    SceneVector sunColor;
    std::array<SceneVector, MaxSceneLights> lightPositions;
    std::array<SceneVector, MaxSceneLights> lightColors;
    std::array<SceneVector, MaxWaterBoxes> waterBounds;
    std::array<SceneVector, MaxWaterBoxes> waterPlanes;
};
static_assert(sizeof(SceneConstants) == 36 * 16);

void CaptureScene(const PlayState& play, SceneConstants& scene,
                  const ShaderPacks::TransmissionMaterials* shadowMaterials, size_t pointLightCapacity) {
    scene.info = { static_cast<float>(play.sceneNum), static_cast<float>(play.roomCtx.curRoom.num),
                   static_cast<float>(play.gameplayFrames), gSaveContext.dayTime / 65536.0f };
    scene.sun = { play.envCtx.sunPos.x, play.envCtx.sunPos.y, play.envCtx.sunPos.z, 0.0f };
    const auto& direct = play.envCtx.dirLight1.params.dir;
    scene.sunColor = { direct.color[0] / 255.0f, direct.color[1] / 255.0f, direct.color[2] / 255.0f, 0.0f };

    struct PointLight {
        SceneVector position;
        SceneVector color;
        float score;
        float shadowScore = 0.8f * 0.8f;
    };
    std::array<PointLight, 64> candidates{};
    size_t count = 0;
    const auto* node = play.lightCtx.listHead;
    for (size_t visited = 0; node && visited < candidates.size(); ++visited, node = node->next) {
        if (!node->info || (node->info->type != LIGHT_POINT_GLOW && node->info->type != LIGHT_POINT_NOGLOW))
            continue;
        const auto& light = node->info->params.point;
        if (light.radius <= 0 || !(light.color[0] || light.color[1] || light.color[2]) || !std::isfinite(light.x) ||
            !std::isfinite(light.y) || !std::isfinite(light.z))
            continue;
        const float x = light.x - play.view.eye.x;
        const float y = light.y - play.view.eye.y;
        const float z = light.z - play.view.eye.z;
        const float radius = std::min<float>(light.radius, 1024.0f);
        candidates[count++] = { { light.x, light.y, light.z, radius },
                                { light.color[0] / 255.0f, light.color[1] / 255.0f, light.color[2] / 255.0f, 0.0f },
                                (x * x + y * y + z * z) / (radius * radius) };
    }
    const size_t visible = std::min(count, MaxSceneLights);
    size_t reserved = 0;
    if (shadowMaterials) {
        constexpr float cutoff = 0.8f * 0.8f;
        for (size_t i = 0; i < count; ++i) {
            auto& candidate = candidates[i];
            const auto& light = candidate.position;
            if (light[3] <= ShaderPacks::GeometryShadow::PointNear)
                continue;
            for (const auto& material : *shadowMaterials) {
                if (material.scene != play.sceneNum || material.tint[3] <= 0.0f)
                    continue;
                const auto& anchor = material.light;
                const float x = light[0] - anchor[0], y = light[1] - anchor[1], z = light[2] - anchor[2];
                const float score = (x * x + y * y + z * z) / (anchor[3] * anchor[3]);
                candidate.shadowScore = std::min(candidate.shadowScore, score);
            }
        }
        const auto selectedEnd = candidates.begin() + std::min(visible, pointLightCapacity);
        std::partial_sort(candidates.begin(), selectedEnd, candidates.begin() + count,
                          [](const auto& a, const auto& b) { return a.shadowScore < b.shadowScore; });
        reserved = std::find_if(candidates.begin(), selectedEnd,
                                [cutoff](const auto& light) { return light.shadowScore >= cutoff; }) -
                   candidates.begin();
    }
    std::partial_sort(candidates.begin() + reserved, candidates.begin() + visible, candidates.begin() + count,
                      [](const auto& a, const auto& b) { return a.score < b.score; });
    for (size_t i = 0; i < visible; ++i) {
        scene.lightPositions[i] = candidates[i].position;
        scene.lightColors[i] = candidates[i].color;
    }
    scene.counts[0] = static_cast<float>(visible);
    scene.counts[2] = static_cast<float>(reserved);

    const auto* collision = play.colCtx.colHeader;
    if (!collision || !collision->waterBoxes)
        return;
    size_t waterCount = 0;
    for (size_t i = 0; i < collision->numWaterBoxes && waterCount < MaxWaterBoxes; ++i) {
        const auto& box = collision->waterBoxes[i];
        if ((box.room != -1 && box.room != play.roomCtx.curRoom.num) || box.notSwimmable || box.xLength <= 0 ||
            box.zLength <= 0)
            continue;
        const float x = static_cast<float>(box.xMin), z = static_cast<float>(box.zMin);
        scene.waterBounds[waterCount] = { x, z, x + box.xLength, z + box.zLength };
        scene.waterPlanes[waterCount++] = { static_cast<float>(box.ySurface), 0.0f, 0.0f, 0.0f };
    }
    scene.counts[1] = static_cast<float>(waterCount);
}

struct WorldSnapshot {
    uint16_t sceneId = 0;
    uint32_t ticket = 0;
    const void* projectionKey = nullptr;
    const void* viewKey = nullptr;
    CameraConstants camera{};
    SceneConstants scene{};
    std::shared_ptr<const SunShadowMesh> shadowMesh;
    std::array<float, 3> geometryFocus{};
    bool hasGeometryFocus = false;
    int fogSunTest = 0;
    unsigned sunCascades = 3;
};

// Display lists carry a ticket. Matrix addresses are only lookup keys during replay.
std::mutex worldSnapshotsMutex;
std::array<WorldSnapshot, 32> worldSnapshots{};
uint32_t worldTicket = 0;

uint32_t QueueWorldSnapshot(GraphicsContext* gfxCtx, bool captureCamera, bool captureScene, bool captureShadow,
                            bool captureGeometryShadow, bool lightsFog,
                            const ShaderPacks::TransmissionMaterials* shadowMaterials, size_t pointLightCapacity) {
    auto* play = gPlayState;
    if (!play || play->state.gfxCtx != gfxCtx)
        return 0;
    const auto& view = play->view;
    WorldSnapshot snapshot{};
    snapshot.sceneId = play->sceneNum;
    if (captureCamera) {
        if (!view.projectionPtr || !view.viewingPtr)
            return 0;
        snapshot.projectionKey = view.projectionPtr;
        snapshot.viewKey = view.viewingPtr;
        MtxF projection, viewing;
        Matrix_MtxToMtxF(view.projectionPtr, &projection);
        Matrix_MtxToMtxF(view.viewingPtr, &viewing);
        std::memcpy(snapshot.camera.projection, projection.mf, sizeof(projection.mf));
        std::memcpy(snapshot.camera.view, viewing.mf, sizeof(viewing.mf));
        snapshot.camera.info[0] = view.zNear;
        snapshot.camera.info[1] = view.zFar;
    }
    if (captureScene || captureGeometryShadow || shadowMaterials) {
        CaptureScene(*play, snapshot.scene, shadowMaterials, pointLightCapacity);
    }
    if (lightsFog) {
        snapshot.fogSunTest =
            Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_DEVELOPER_TOOLS("FogSunTest"), 0);
    }
    if (captureShadow)
        snapshot.shadowMesh = ShaderPacks::CaptureSunShadowMesh(*play);
    if (captureGeometryShadow && UsesSolarGeometry(snapshot.sceneId)) {
        snapshot.sunCascades = static_cast<unsigned>(std::clamp(
            Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_DEVELOPER_TOOLS("SunCascades"), 3),
            1, 3));
        if (const auto* player = play->actorCtx.actorLists[ACTORCAT_PLAYER].head) {
            const auto& position = player->world.pos;
            snapshot.geometryFocus = { position.x, position.y, position.z };
            snapshot.hasGeometryFocus = true;
        }
    }

    const std::lock_guard lock(worldSnapshotsMutex);
    if (++worldTicket == 0)
        ++worldTicket;
    snapshot.ticket = worldTicket;
    worldSnapshots[worldTicket % worldSnapshots.size()] = snapshot;
    return worldTicket;
}

bool ReadWorldSnapshot(const Fast::Interpreter& interpreter, uint32_t ticket, WorldSnapshot& snapshot) {
    if (!ticket)
        return false;
    {
        const std::lock_guard lock(worldSnapshotsMutex);
        snapshot = worldSnapshots[ticket % worldSnapshots.size()];
    }
    if (snapshot.ticket != ticket)
        return false;

    if (snapshot.projectionKey) {
        auto& camera = snapshot.camera;
        const bool projected = Fast::TryReadInterpolatedMatrix(interpreter, snapshot.projectionKey, camera.projection);
        const bool viewed = Fast::TryReadInterpolatedMatrix(interpreter, snapshot.viewKey, camera.view);
        camera.info[2] = interpreter.mInterpolationT;
        camera.info[3] = (viewed ? 1.0f : 0.0f) + (projected ? 2.0f : 0.0f);
    }
    snapshot.scene.info[2] = std::max(0.0f, (snapshot.scene.info[2] - 1.0f + interpreter.mInterpolationT) / 20.0f);
    return true;
}

struct PixelPass {
    ComPtr<ID3D11PixelShader> shader;
    unsigned divisor;
    std::array<int, MaxPassInputs> inputs;
    unsigned width = 0, height = 0;
    bool persistent = false;
};

struct PassTarget {
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11ShaderResourceView> view;
    UINT width;
    UINT height;
};

bool CreatePassTarget(ID3D11Device& device, DXGI_FORMAT format, PassTarget& target) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = target.width;
    desc.Height = target.height;
    desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Format = format;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    return SUCCEEDED(device.CreateTexture2D(&desc, nullptr, texture.GetAddressOf())) &&
           SUCCEEDED(device.CreateRenderTargetView(texture.Get(), nullptr, target.target.GetAddressOf())) &&
           SUCCEEDED(device.CreateShaderResourceView(texture.Get(), nullptr, target.view.GetAddressOf()));
}

struct WorldPass {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Buffer> cameraBuffer;
    ComPtr<ID3D11Buffer> sceneBuffer;
    ComPtr<ID3D11Buffer> sunShadowBuffer;
    ComPtr<ID3D11Buffer> transmissionBuffer;
    ComPtr<ID3D11Buffer> pointShadowBuffer;
    ComPtr<ID3D11Buffer> weatherBuffer;
    ComPtr<ID3D11Buffer> decalsBuffer;
    std::shared_ptr<const ShaderPacks::TransmissionMaterials> transmissionMaterials;
    TransmissionConstants transmissionConstants{};
    std::array<PointShadowConstants, MaxPointLights> pointShadowConstants{};
    ComPtr<ID3D11ShaderResourceView> sunShadowView;
    std::unique_ptr<ShaderPacks::GeometryShadow> geometryShadow;
    std::unique_ptr<ShaderPacks::GeometryShadow> pointShadow;
    std::shared_ptr<const SunShadowMesh> shadowMesh;
    ShaderPacks::SolarCascades::Constants shadowConstants{};
    std::vector<PixelPass> passes;
    std::vector<PassTarget> targets;
    std::vector<PassTarget> historyTargets;
    ShaderPacks::PackHistory history;
    bool hasHistory = false;
    ComPtr<ID3D11Buffer> historyBuffer;

    PassTarget& Output(size_t i) {
        return passes[i].persistent && history.WriteIndex() == 1 ? historyTargets[i] : targets[i];
    }
    PassTarget& Previous(size_t i) {
        return history.ReadIndex() == 1 ? historyTargets[i] : targets[i];
    }
    ComPtr<ID3D11Texture2D> scene;
    ComPtr<ID3D11ShaderResourceView> sceneView;
    D3D11_TEXTURE2D_DESC sceneDesc{};
    bool usesDepth = false;
    bool usesSunShadow = false;
    bool lightsFog = false;
    ComPtr<ID3D11PixelShader> resolveDepth;
    ComPtr<ID3D11DepthStencilView> depthAttachment;
    ComPtr<ID3D11ShaderResourceView> depthSource;
    PassTarget resolvedDepth{};
    std::string name;
    bool usesWaterRipple = false;
    ComPtr<ID3D11Texture2D> rippleTexture;
    ComPtr<ID3D11ShaderResourceView> rippleView;
    ComPtr<ID3D11Buffer> rippleBuffer;
    std::vector<uint16_t> rippleTexels;
    uint64_t rippleVersion = 0;
    bool drawn = false;
    bool reportedInterpolatedCamera = false;
    bool reportedTargetFailure = false;
    bool reportedDepthFailure = false;
    bool reportedGeometryShadow = false;
    bool reportedTransmission = false;
    size_t pointLightCapacity = 0;
    size_t sunCascadeCapacity = 0;
    size_t reportedPointCount = 0;

    void PrepareTransmission(uint16_t sceneId) {
        transmissionConstants = {};
        size_t materialCount = 0, triangleCount = 0;
        const auto geometry = Fast::ReadWorldTransmission();
        for (const auto& material : *transmissionMaterials) {
            if (material.scene != sceneId)
                continue;
            for (const auto& triangle : geometry) {
                if (triangle.texture != material.texture)
                    continue;
                const size_t offset = TransmissionTriangleOffset + 3 * triangleCount++;
                for (size_t i = 0; i < 3; ++i) {
                    const auto& vertex = triangle.vertices[i];
                    transmissionConstants[offset + i] = { vertex[0], vertex[1], vertex[2],
                                                          static_cast<float>(materialCount) };
                }
            }
            transmissionConstants[1 + 2 * materialCount] = material.tint;
            transmissionConstants[2 + 2 * materialCount] = material.light;
            ++materialCount;
        }
        transmissionConstants[0] = { static_cast<float>(materialCount), static_cast<float>(triangleCount), 0, 0 };
        if (triangleCount && !reportedTransmission) {
            const auto& vertex = transmissionConstants[TransmissionTriangleOffset];
            SPDLOG_INFO(
                "Shader pack DX11 transmissão: {} (cena {}, {} triângulos, primeiro vértice {:.1f}, {:.1f}, {:.1f})",
                name, sceneId, triangleCount, vertex[0], vertex[1], vertex[2]);
            reportedTransmission = true;
        }
    }

    void PreparePointShadow(ID3D11DeviceContext& ctx, const SceneConstants& world) {
        pointShadowConstants = {};
        const auto count = std::min(pointLightCapacity, static_cast<size_t>(world.counts[2]));
        if (transmissionConstants[0][1] < 1.0f || !count) {
            reportedPointCount = 0;
            return;
        }
        const std::span sources(world.lightPositions.data(), count);
        const auto geometry = Fast::ReadWorldGeometry();
        if (!pointShadow->DrawPoint(ctx, geometry, Fast::ReadWorldCutouts(), sources))
            return;
        using Shadow = ShaderPacks::GeometryShadow;
        for (size_t i = 0; i < count; ++i) {
            pointShadowConstants[i] = {
                sources[i], { static_cast<float>(i + 1), Shadow::PointNear, Shadow::PointFaceSize, Shadow::PointScale }
            };
        }
        if (reportedPointCount != count) {
            SPDLOG_INFO("Shader pack DX11 sombras pontuais: {} ({} fontes, {} faces, {} triângulos opacos)", name,
                        count, 6 * count, geometry.size() / 3);
            reportedPointCount = count;
        }
    }

    void PrepareSunShadow(const std::shared_ptr<const SunShadowMesh>& mesh) {
        if (shadowMesh == mesh)
            return;
        shadowMesh = mesh;
        sunShadowView.Reset();
        shadowConstants = {};
        if (!mesh)
            return;

        auto map = ShaderPacks::BakeSunShadow(*mesh);
        if (map.depth.empty()) {
            SPDLOG_WARN("Shader pack DX11 sem mapa de sombras estáticas dentro do orçamento: {}", name);
            return;
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = SunShadowMap::Size;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32_FLOAT;
        desc.SampleDesc = { 1, 0 };
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA pixels{ map.depth.data(), SunShadowMap::Size * sizeof(float), 0 };
        ComPtr<ID3D11Texture2D> texture;
        if (FAILED(device->CreateTexture2D(&desc, &pixels, texture.GetAddressOf())) ||
            FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, sunShadowView.GetAddressOf()))) {
            SPDLOG_WARN("Shader pack DX11 não criou o mapa de sombras estáticas: {}", name);
            return;
        }
        std::copy(map.constants.begin(), map.constants.end(), shadowConstants.begin());
        shadowConstants[14] = { 1, 1, 0, 0 };
        SPDLOG_INFO("Shader pack DX11 mapa de sombras estáticas: {} ({} triângulos, {}x{})", name,
                    mesh->triangles.size(), SunShadowMap::Size, SunShadowMap::Size);
    }

    bool PrepareTargets(D3D11_TEXTURE2D_DESC desc) {
        if (scene && sceneDesc.Width == desc.Width && sceneDesc.Height == desc.Height &&
            sceneDesc.Format == desc.Format)
            return true;

        std::vector<PassTarget> nextTargets, nextHistoryTargets;
        // Also reserve room for resolved depth if MSAA is enabled at this resolution later.
        uint64_t bytes = static_cast<uint64_t>(desc.Width) * desc.Height * (usesDepth ? 8 : 4);
        if (usesSunShadow || geometryShadow) {
            const auto tiles = geometryShadow ? geometryShadow->SolarCapacity() : 1;
            bytes += static_cast<uint64_t>(tiles) * SunShadowMap::Size * SunShadowMap::Size * sizeof(float);
        }
        // Histórico pequeno (exposição, médias) fica em FP32: em FP16 o ulp de um log2 entre 4 e 8
        // é 0,0039, e a adaptação lenta (passo < meio ulp por quadro) trava antes do alvo.
        // Histórico de tela cheia continua FP16 por custo de banda.
        std::vector<DXGI_FORMAT> formats;
        for (size_t i = 0; i + 1 < passes.size(); ++i) {
            const auto divisor = passes[i].divisor;
            const auto width = passes[i].width ? passes[i].width : (desc.Width + divisor - 1) / divisor;
            const auto height = passes[i].height ? passes[i].height : (desc.Height + divisor - 1) / divisor;
            const bool precise = passes[i].persistent && static_cast<uint64_t>(width) * height <= 64u * 64u;
            formats.push_back(precise ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT);
            bytes += static_cast<uint64_t>(width) * height * (precise ? 16 : 8) * (passes[i].persistent ? 2 : 1);
            nextTargets.push_back({ {}, {}, width, height });
            nextHistoryTargets.push_back(passes[i].persistent ? PassTarget{ {}, {}, width, height } : PassTarget{});
        }
        if (bytes > 256u * 1024u * 1024u)
            return false;

        desc.SampleDesc = { 1, 0 };
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> view;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf())) ||
            FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf())))
            return false;

        for (size_t i = 0; i < nextTargets.size(); ++i) {
            if (!CreatePassTarget(*device.Get(), formats[i], nextTargets[i]))
                return false;
        }

        for (size_t i = 0; i < nextHistoryTargets.size(); ++i) {
            if (nextHistoryTargets[i].width && !CreatePassTarget(*device.Get(), formats[i], nextHistoryTargets[i])) {
                return false;
            }
        }
        scene = std::move(texture);
        sceneView = std::move(view);
        sceneDesc = desc;
        targets = std::move(nextTargets);
        historyTargets = std::move(nextHistoryTargets);
        history.Reset();
        reportedTargetFailure = false;
        return true;
    }

    bool PrepareDepth(ID3D11DepthStencilView* attachment, const D3D11_TEXTURE2D_DESC& color) {
        if (!attachment)
            return false;
        if (depthAttachment.Get() == attachment)
            return true;

        ComPtr<ID3D11Resource> resource;
        ComPtr<ID3D11Texture2D> texture;
        attachment->GetResource(resource.GetAddressOf());
        if (FAILED(resource.As(&texture)))
            return false;

        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        if (desc.Width != color.Width || desc.Height != color.Height || desc.ArraySize != 1 || desc.MipLevels != 1 ||
            desc.SampleDesc.Count != color.SampleDesc.Count || !(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)) {
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
        switch (desc.Format) {
            case DXGI_FORMAT_R32G8X24_TYPELESS:
                viewDesc.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
                break;
            case DXGI_FORMAT_R24G8_TYPELESS:
                viewDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
                break;
            default:
                return false;
        }
        const bool multisampled = desc.SampleDesc.Count > 1;
        viewDesc.ViewDimension = multisampled ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
        if (!multisampled)
            viewDesc.Texture2D.MipLevels = 1;

        ComPtr<ID3D11ShaderResourceView> view;
        if (FAILED(device->CreateShaderResourceView(texture.Get(), &viewDesc, view.GetAddressOf())))
            return false;
        PassTarget resolved{ {}, {}, desc.Width, desc.Height };
        if (multisampled && (!resolveDepth || !CreatePassTarget(*device.Get(), DXGI_FORMAT_R32_FLOAT, resolved))) {
            return false;
        }

        depthAttachment = attachment;
        depthSource = std::move(view);
        resolvedDepth = std::move(resolved);
        reportedDepthFailure = false;
        SPDLOG_INFO("Shader pack DX11 profundidade: {} ({}x{}, {} amostras, modo={})", name, desc.Width, desc.Height,
                    desc.SampleDesc.Count, multisampled ? "nearest" : "direct");
        return true;
    }

    // Textura do campo de onda em t4 e constantes em b5, fora dos quatro slots de entrada do manifesto.
    bool PrepareWaterRipple(ID3D11DeviceContext& ctx) {
        if (!rippleView) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = desc.Height = WaterRipple::Side;
            desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
            desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_BUFFER_DESC buffer{};
            buffer.ByteWidth = sizeof(WaterRipple::Frame);
            buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            const std::vector<uint16_t> zero(size_t{ WaterRipple::Side } * WaterRipple::Side * 4, 0);
            const D3D11_SUBRESOURCE_DATA initial{ zero.data(), WaterRipple::Side * 8, 0 };
            if (FAILED(device->CreateTexture2D(&desc, &initial, rippleTexture.GetAddressOf())) ||
                FAILED(device->CreateShaderResourceView(rippleTexture.Get(), nullptr, rippleView.GetAddressOf())) ||
                FAILED(device->CreateBuffer(&buffer, nullptr, rippleBuffer.GetAddressOf()))) {
                rippleView.Reset();
                usesWaterRipple = false;
                return false;
            }
        }
        WaterRipple::Frame frame{};
        if (WaterRipple::Read(rippleVersion, rippleTexels, frame)) {
            ctx.UpdateSubresource(rippleTexture.Get(), 0, nullptr, rippleTexels.data(), WaterRipple::Side * 8, 0);
        }
        ctx.UpdateSubresource(rippleBuffer.Get(), 0, nullptr, &frame, 0, 0);
        return true;
    }

    void Draw(ID3D11DeviceContext& ctx, const CameraConstants* camera, const SceneConstants* world, uint16_t sceneId) {
        ComPtr<ID3D11RenderTargetView> target;
        ComPtr<ID3D11DepthStencilView> depth;
        ctx.OMGetRenderTargets(1, target.GetAddressOf(), depth.GetAddressOf());
        if (!target)
            return;

        ComPtr<ID3D11Resource> resource;
        ComPtr<ID3D11Texture2D> color;
        target->GetResource(resource.GetAddressOf());
        if (FAILED(resource.As(&color)))
            return;

        D3D11_TEXTURE2D_DESC desc{};
        color->GetDesc(&desc);
        if (desc.ArraySize != 1 || desc.MipLevels != 1)
            return;
        if (!PrepareTargets(desc)) {
            if (!reportedTargetFailure) {
                SPDLOG_WARN("Shader pack DX11 sem alvos intermediários: {} ({}x{})", name, desc.Width, desc.Height);
                reportedTargetFailure = true;
            }
            return;
        }
        if (usesDepth && !PrepareDepth(depth.Get(), desc)) {
            if (!reportedDepthFailure) {
                SPDLOG_WARN("Shader pack DX11 sem profundidade acessível: {}", name);
                reportedDepthFailure = true;
            }
            return;
        }

        const bool historyEnabled = hasHistory && Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
                                                      CVAR_DEVELOPER_TOOLS("PackHistory"), 1);
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        history.Begin(sceneId, desc.Width, desc.Height, historyEnabled, seconds);
        if (hasHistory && !history.Valid()) {
            const float zero[4]{};
            for (size_t i = 0; i < historyTargets.size(); ++i) {
                if (!passes[i].persistent)
                    continue;
                ctx.ClearRenderTargetView(targets[i].target.Get(), zero);
                ctx.ClearRenderTargetView(historyTargets[i].target.Get(), zero);
            }
        }

        ComPtr<ID3D11Buffer> oldHistoryBuffer;
        if (historyBuffer) {
            const SceneVector frame{ history.Delta(), history.Valid() ? 1.0f : 0.0f, 0.0f, 0.0f };
            ctx.PSGetConstantBuffers(8, 1, oldHistoryBuffer.GetAddressOf());
            ctx.UpdateSubresource(historyBuffer.Get(), 0, nullptr, frame.data(), 0, 0);
            ctx.PSSetConstantBuffers(8, 1, historyBuffer.GetAddressOf());
        }
        ComPtr<ID3D11VertexShader> oldVertex;
        ComPtr<ID3D11PixelShader> oldPixel;
        ComPtr<ID3D11InputLayout> oldLayout;
        std::array<ID3D11ShaderResourceView*, MaxPassInputs> oldSources{};
        ComPtr<ID3D11SamplerState> oldSampler;
        ComPtr<ID3D11Buffer> oldCameraBuffer;
        ComPtr<ID3D11Buffer> oldSceneBuffer;
        ComPtr<ID3D11Buffer> oldSunShadowBuffer;
        ComPtr<ID3D11Buffer> oldTransmissionBuffer;
        ComPtr<ID3D11Buffer> oldPointShadowBuffer;
        ComPtr<ID3D11Buffer> oldWeatherBuffer;
        ComPtr<ID3D11Buffer> oldDecalsBuffer;
        ComPtr<ID3D11RasterizerState> oldRaster;
        ComPtr<ID3D11BlendState> oldBlend;
        D3D11_PRIMITIVE_TOPOLOGY oldTopology{};
        D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        UINT viewportCount = ARRAYSIZE(oldViewports), sampleMask = 0;
        float blendFactor[4]{};
        ctx.VSGetShader(oldVertex.GetAddressOf(), nullptr, nullptr);
        ctx.PSGetShader(oldPixel.GetAddressOf(), nullptr, nullptr);
        ctx.IAGetInputLayout(oldLayout.GetAddressOf());
        ctx.IAGetPrimitiveTopology(&oldTopology);
        ctx.PSGetShaderResources(0, static_cast<UINT>(oldSources.size()), oldSources.data());
        ctx.PSGetSamplers(0, 1, oldSampler.GetAddressOf());
        if (camera) {
            ctx.PSGetConstantBuffers(0, 1, oldCameraBuffer.GetAddressOf());
            ctx.UpdateSubresource(cameraBuffer.Get(), 0, nullptr, camera, 0, 0);
            ctx.PSSetConstantBuffers(0, 1, cameraBuffer.GetAddressOf());
        }
        if (world) {
            ctx.PSGetConstantBuffers(1, 1, oldSceneBuffer.GetAddressOf());
            ctx.UpdateSubresource(sceneBuffer.Get(), 0, nullptr, world, 0, 0);
            ctx.PSSetConstantBuffers(1, 1, sceneBuffer.GetAddressOf());
        }
        if (sunShadowBuffer) {
            ctx.PSGetConstantBuffers(2, 1, oldSunShadowBuffer.GetAddressOf());
            ctx.UpdateSubresource(sunShadowBuffer.Get(), 0, nullptr, shadowConstants.data(), 0, 0);
            ctx.PSSetConstantBuffers(2, 1, sunShadowBuffer.GetAddressOf());
        }
        if (transmissionBuffer) {
            ctx.PSGetConstantBuffers(3, 1, oldTransmissionBuffer.GetAddressOf());
            ctx.UpdateSubresource(transmissionBuffer.Get(), 0, nullptr, transmissionConstants.data(), 0, 0);
            ctx.PSSetConstantBuffers(3, 1, transmissionBuffer.GetAddressOf());
        }
        if (pointShadowBuffer) {
            ctx.PSGetConstantBuffers(4, 1, oldPointShadowBuffer.GetAddressOf());
            ctx.UpdateSubresource(pointShadowBuffer.Get(), 0, nullptr, pointShadowConstants.data(), 0, 0);
            ctx.PSSetConstantBuffers(4, 1, pointShadowBuffer.GetAddressOf());
        }
        if (weatherBuffer) {
            const auto weather = WetSurface::Read();
            ctx.PSGetConstantBuffers(6, 1, oldWeatherBuffer.GetAddressOf());
            ctx.UpdateSubresource(weatherBuffer.Get(), 0, nullptr, &weather, 0, 0);
            ctx.PSSetConstantBuffers(6, 1, weatherBuffer.GetAddressOf());
        }
        if (decalsBuffer) {
            const auto decals = ImpactDecals::Read();
            ctx.PSGetConstantBuffers(7, 1, oldDecalsBuffer.GetAddressOf());
            ctx.UpdateSubresource(decalsBuffer.Get(), 0, nullptr, &decals, 0, 0);
            ctx.PSSetConstantBuffers(7, 1, decalsBuffer.GetAddressOf());
        }
        ComPtr<ID3D11Buffer> oldRippleBuffer;
        ComPtr<ID3D11ShaderResourceView> oldRippleView;
        const bool ripple = usesWaterRipple && PrepareWaterRipple(ctx);
        if (ripple) {
            ctx.PSGetConstantBuffers(5, 1, oldRippleBuffer.GetAddressOf());
            ctx.PSGetShaderResources(4, 1, oldRippleView.GetAddressOf());
            ctx.PSSetConstantBuffers(5, 1, rippleBuffer.GetAddressOf());
            ctx.PSSetShaderResources(4, 1, rippleView.GetAddressOf());
        }
        ctx.RSGetState(oldRaster.GetAddressOf());
        ctx.RSGetViewports(&viewportCount, oldViewports);
        ctx.OMGetBlendState(oldBlend.GetAddressOf(), blendFactor, &sampleMask);

        ctx.OMSetRenderTargets(0, nullptr, nullptr);
        if (desc.SampleDesc.Count > 1)
            ctx.ResolveSubresource(scene.Get(), 0, color.Get(), 0, desc.Format);
        else
            ctx.CopyResource(scene.Get(), color.Get());

        ctx.OMSetBlendState(nullptr, nullptr, ~0u);
        ctx.RSSetState(raster.Get());
        ctx.IASetInputLayout(nullptr);
        ctx.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx.VSSetShader(vertex.Get(), nullptr, 0);
        ctx.PSSetSamplers(0, 1, sampler.GetAddressOf());

        const std::array<ID3D11ShaderResourceView*, MaxPassInputs> unbound{};
        auto* depthInput = depthSource.Get();
        if (resolvedDepth.target) {
            const D3D11_VIEWPORT viewport{
                0, 0, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0, 1
            };
            ctx.OMSetRenderTargets(1, resolvedDepth.target.GetAddressOf(), nullptr);
            ctx.RSSetViewports(1, &viewport);
            ctx.PSSetShader(resolveDepth.Get(), nullptr, 0);
            ctx.PSSetShaderResources(0, 1, depthSource.GetAddressOf());
            ctx.Draw(3, 0);
            ctx.PSSetShaderResources(0, static_cast<UINT>(unbound.size()), unbound.data());
            depthInput = resolvedDepth.view.Get();
        }
        for (size_t i = 0; i < passes.size(); ++i) {
            const auto& pass = passes[i];
            const bool final = i == targets.size();
            const auto width = final ? desc.Width : Output(i).width;
            const auto height = final ? desc.Height : Output(i).height;
            auto* output = final ? target.Get() : Output(i).target.Get();
            const D3D11_VIEWPORT viewport{ 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
            std::array<ID3D11ShaderResourceView*, MaxPassInputs> inputs{};
            for (size_t slot = 0; slot < inputs.size(); ++slot) {
                const auto source = pass.inputs[slot];
                if (source == SceneColorInput)
                    inputs[slot] = sceneView.Get();
                else if (source == SceneDepthInput)
                    inputs[slot] = depthInput;
                else if (source == SceneSunShadowInput)
                    inputs[slot] = sunShadowView.Get();
                else if (source == ScenePointShadowInput && pointShadowConstants[0].light[0] > 0.0f) {
                    inputs[slot] = pointShadow->View();
                } else if ((source == SceneGeometryShadowInput || source == SceneFogShadowInput) &&
                           shadowConstants[3][0] > 0.0f) {
                    inputs[slot] = geometryShadow->View();
                } else if (ShaderPacks::IsHistoryInput(source)) {
                    if (history.Valid())
                        inputs[slot] = Previous(ShaderPacks::HistoryInputIndex(source)).view.Get();
                } else if (source > 0)
                    inputs[slot] = Output(static_cast<size_t>(source - 1)).view.Get();
            }
            ctx.OMSetRenderTargets(1, &output, nullptr);
            ctx.RSSetViewports(1, &viewport);
            ctx.PSSetShader(pass.shader.Get(), nullptr, 0);
            ctx.PSSetShaderResources(0, static_cast<UINT>(inputs.size()), inputs.data());
            ctx.Draw(3, 0);
            ctx.PSSetShaderResources(0, static_cast<UINT>(unbound.size()), unbound.data());
        }

        history.Commit();
        if (historyBuffer)
            ctx.PSSetConstantBuffers(8, 1, oldHistoryBuffer.GetAddressOf());

        // Keep the Fast3D state cache valid for the rest of the display list.
        ctx.OMSetRenderTargets(1, target.GetAddressOf(), depth.Get());
        ctx.PSSetShaderResources(0, static_cast<UINT>(oldSources.size()), oldSources.data());
        for (auto* source : oldSources) {
            if (source)
                source->Release();
        }
        ctx.PSSetSamplers(0, 1, oldSampler.GetAddressOf());
        if (camera)
            ctx.PSSetConstantBuffers(0, 1, oldCameraBuffer.GetAddressOf());
        if (world)
            ctx.PSSetConstantBuffers(1, 1, oldSceneBuffer.GetAddressOf());
        if (sunShadowBuffer)
            ctx.PSSetConstantBuffers(2, 1, oldSunShadowBuffer.GetAddressOf());
        if (transmissionBuffer)
            ctx.PSSetConstantBuffers(3, 1, oldTransmissionBuffer.GetAddressOf());
        if (pointShadowBuffer)
            ctx.PSSetConstantBuffers(4, 1, oldPointShadowBuffer.GetAddressOf());
        if (weatherBuffer)
            ctx.PSSetConstantBuffers(6, 1, oldWeatherBuffer.GetAddressOf());
        if (decalsBuffer)
            ctx.PSSetConstantBuffers(7, 1, oldDecalsBuffer.GetAddressOf());
        if (ripple) {
            ctx.PSSetConstantBuffers(5, 1, oldRippleBuffer.GetAddressOf());
            ctx.PSSetShaderResources(4, 1, oldRippleView.GetAddressOf());
        }
        ctx.PSSetShader(oldPixel.Get(), nullptr, 0);
        ctx.VSSetShader(oldVertex.Get(), nullptr, 0);
        ctx.IASetPrimitiveTopology(oldTopology);
        ctx.IASetInputLayout(oldLayout.Get());
        ctx.RSSetViewports(viewportCount, oldViewports);
        ctx.RSSetState(oldRaster.Get());
        ctx.OMSetBlendState(oldBlend.Get(), blendFactor, sampleMask);

        if (!drawn) {
            SPDLOG_INFO("Shader pack DX11 aplicado: {} ({}x{}, MSAA {}x, {} passes)", name, desc.Width, desc.Height,
                        desc.SampleDesc.Count, passes.size());
            drawn = true;
        }
        if (camera && !reportedInterpolatedCamera && camera->info[2] > 0.0f && camera->info[2] < 1.0f &&
            camera->info[3] > 0.0f) {
            SPDLOG_INFO("Shader pack DX11 câmera interpolada: {} (fração={:.3f}, matrizes={})", name, camera->info[2],
                        static_cast<int>(camera->info[3]));
            reportedInterpolatedCamera = true;
        }
    }
};

std::atomic<std::shared_ptr<WorldPass>> activePass;

bool MatchWorldMaterial(uint16_t scene, std::string_view texture) {
    const auto pass = activePass.load();
    if (!pass || !pass->transmissionMaterials)
        return false;
    return std::any_of(
        pass->transmissionMaterials->begin(), pass->transmissionMaterials->end(),
        [scene, texture](const auto& material) { return material.scene == scene && material.texture == texture; });
}

void DrawWorld(Fast::Interpreter& interpreter, uint32_t ticket) {
    auto pass = activePass.load();
    auto* dx11 = dynamic_cast<Fast::GfxRenderingAPIDX11*>(interpreter.GetCurrentRenderingAPI());
    if (!pass || !dx11 || pass->device.Get() != dx11->mDevice.Get())
        return;
    WorldSnapshot snapshot{};
    if ((pass->cameraBuffer || pass->sceneBuffer || pass->usesSunShadow || pass->geometryShadow ||
         pass->transmissionBuffer || pass->hasHistory) &&
        !ReadWorldSnapshot(interpreter, ticket, snapshot))
        return;
    if (pass->usesSunShadow)
        pass->PrepareSunShadow(snapshot.shadowMesh);
    if (pass->transmissionBuffer)
        pass->PrepareTransmission(snapshot.sceneId);
    if (pass->pointShadow)
        pass->PreparePointShadow(*dx11->mContext.Get(), snapshot.scene);
    if (pass->geometryShadow) {
        pass->shadowConstants = {};
        const auto triangles = Fast::ReadWorldGeometry();
        const auto cutouts = Fast::ReadWorldCutouts();
        ShaderPacks::SolarCascades::Camera camera{};
        std::memcpy(camera.projection, snapshot.camera.projection, sizeof(camera.projection));
        std::memcpy(camera.view, snapshot.camera.view, sizeof(camera.view));
        camera.nearDepth = snapshot.camera.info[0];
        camera.farDepth = snapshot.camera.info[1];
        if (snapshot.hasGeometryFocus &&
            pass->geometryShadow->DrawCascades(*dx11->mContext.Get(), triangles, cutouts, camera,
                                               snapshot.geometryFocus, snapshot.scene.sun, snapshot.sunCascades,
                                               pass->shadowConstants) &&
            !pass->reportedGeometryShadow) {
            size_t cutoutTriangles = 0;
            for (const auto& batch : cutouts.batches)
                cutoutTriangles += batch.count / 3;
            SPDLOG_INFO("Shader pack DX11 mapa de geometria: {} ({} opacos, {} com recorte em {} lotes, {}x{})",
                        pass->name, triangles.size() / 3, cutoutTriangles, cutouts.batches.size(),
                        SunShadowMap::Size * pass->geometryShadow->SolarCapacity(), SunShadowMap::Size);
            pass->reportedGeometryShadow = true;
        }
    }
    pass->Draw(*dx11->mContext.Get(), pass->cameraBuffer ? &snapshot.camera : nullptr,
               pass->sceneBuffer ? &snapshot.scene : nullptr, snapshot.sceneId);
    if (pass->lightsFog && snapshot.fogSunTest != 2 && pass->shadowConstants[3][0] > 0.0f) {
        const auto day = snapshot.scene.info[3];
        const auto fade = [](float value) {
            value = std::clamp(value, 0.0f, 1.0f);
            return value * value * (3.0f - 2.0f * value);
        };
        const float daylight = fade((day - 0.25f) / 0.09f) * fade((0.75f - day) / 0.09f);
        if (daylight > 0.0f) {
            auto tint = snapshot.scene.sunColor;
            tint[3] = 0.58f * daylight;
            SunShadowMap::Constants legacy{};
            std::copy_n(pass->shadowConstants.begin(), legacy.size(), legacy.begin());
            Fast::worldFogSun = { dx11->mContext.Get(), pass->geometryShadow->View(), legacy, tint,
                                  snapshot.fogSunTest == 1 };
        }
    }
}

constexpr std::string_view fullscreenVertex = R"(
void main(uint vertex : SV_VertexID, out float4 position : SV_Position, out float2 uv : TEXCOORD0) {
    uv = float2(vertex == 1 ? 2.0 : 0.0, vertex == 2 ? 2.0 : 0.0);
    position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
}
)";

constexpr std::string_view resolveDepthShader = R"(
Texture2DMS<float> worldDepth : register(t0);

float main(float4 position : SV_Position) : SV_Target {
    uint width, height, samples;
    worldDepth.GetDimensions(width, height, samples);
    float nearest = 1.0;
    for (uint i = 0; i < samples; ++i) {
        nearest = min(nearest, worldDepth.Load(int2(position.xy), i));
    }
    return nearest;
}
)";

bool CheckCameraBuffer(ID3D11ShaderReflectionConstantBuffer& buffer) {
    D3D11_SHADER_BUFFER_DESC desc{};
    if (FAILED(buffer.GetDesc(&desc)) || desc.Size != sizeof(CameraConstants) || desc.Variables != 3)
        return false;
    for (UINT i = 0; i < desc.Variables; ++i) {
        const bool matrix = i < 2;
        auto* variable = buffer.GetVariableByIndex(i);
        D3D11_SHADER_VARIABLE_DESC value{};
        D3D11_SHADER_TYPE_DESC type{};
        if (FAILED(variable->GetDesc(&value)) || FAILED(variable->GetType()->GetDesc(&type)) ||
            value.StartOffset != 64 * i || value.Size != (matrix ? 64u : 16u) ||
            type.Class != (matrix ? D3D_SVC_MATRIX_ROWS : D3D_SVC_VECTOR) || type.Type != D3D_SVT_FLOAT ||
            type.Rows != (matrix ? 4u : 1u) || type.Columns != 4 || type.Elements != 0)
            return false;
    }
    return true;
}

bool CheckVectorArrayBuffer(ID3D11ShaderReflectionConstantBuffer& buffer, UINT count) {
    D3D11_SHADER_BUFFER_DESC desc{};
    if (FAILED(buffer.GetDesc(&desc)) || desc.Size != count * 16u || desc.Variables != 1)
        return false;
    D3D11_SHADER_TYPE_DESC type{};
    return SUCCEEDED(buffer.GetVariableByIndex(0)->GetType()->GetDesc(&type)) && type.Class == D3D_SVC_VECTOR &&
           type.Type == D3D_SVT_FLOAT && type.Rows == 1 && type.Columns == 4 && type.Elements == count;
}

class PackIncludes final : public ID3DInclude {
  public:
    explicit PackIncludes(const ShaderPacks::PassSource& pass) : pass(pass) {
    }

    HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE, LPCSTR file, LPCVOID parent, LPCVOID* data, UINT* bytes) override {
        if (!pass.includes || open.size() >= 16)
            return E_FAIL;
        const std::string_view name(file);
        if (name.empty() || name.size() > 160 || name.starts_with("//") ||
            name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-. /") !=
                name.npos) {
            return E_FAIL;
        }

        std::string_view parentFile = pass.file;
        if (parent && parent != pass.source.data()) {
            const auto active = open.find(parent);
            if (active == open.end())
                return E_FAIL;
            parentFile = active->second;
        }
        const std::string path = name.starts_with('/')
                                     ? "shaders/" + std::string(name.substr(1))
                                     : std::string(parentFile.substr(0, parentFile.rfind('/') + 1)) + std::string(name);
        const auto resolved = std::filesystem::path(path).lexically_normal().generic_string();
        if (!resolved.starts_with("shaders/"))
            return E_FAIL;
        const auto source = pass.includes->find(resolved);
        if (source == pass.includes->end() || open.contains(source->second.data()) ||
            expandedBytes + source->second.size() > 1024u * 1024u)
            return E_FAIL;

        *data = source->second.data();
        *bytes = static_cast<UINT>(source->second.size());
        expandedBytes += *bytes;
        open.emplace(*data, source->first);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Close(LPCVOID data) override {
        return open.erase(data) ? S_OK : E_FAIL;
    }

  private:
    const ShaderPacks::PassSource& pass;
    std::map<const void*, std::string_view> open;
    size_t expandedBytes = 0;
};

bool reflectedWaterRipple = false;

bool CheckPassInterface(decltype(&D3DReflect) reflect, ID3DBlob& bytecode, const ShaderPacks::PassSource& pass,
                        bool& usesCamera, bool& usesScene, bool& usesSunConstants, bool& usesTransmission,
                        size_t& pointLightCapacity, size_t& sunCascadeCapacity, bool& usesWeather, bool& usesDecals) {
    ComPtr<ID3D11ShaderReflection> shader;
    if (FAILED(reflect(bytecode.GetBufferPointer(), bytecode.GetBufferSize(), __uuidof(ID3D11ShaderReflection),
                       reinterpret_cast<void**>(shader.GetAddressOf()))))
        return false;
    D3D11_SHADER_DESC desc{};
    if (FAILED(shader->GetDesc(&desc)))
        return false;
    for (UINT i = 0; i < desc.InputParameters; ++i) {
        D3D11_SIGNATURE_PARAMETER_DESC input{};
        if (FAILED(shader->GetInputParameterDesc(i, &input)) || input.SemanticIndex != 0 ||
            input.ComponentType != D3D_REGISTER_COMPONENT_FLOAT32)
            return false;
        if (input.Register == 0 && input.SystemValueType == D3D_NAME_POSITION)
            continue;
        if (input.Register != 1 || (input.Mask & ~3u) || _stricmp(input.SemanticName, "TEXCOORD") != 0)
            return false;
    }
    for (UINT i = 0; i < desc.BoundResources; ++i) {
        D3D11_SHADER_INPUT_BIND_DESC binding{};
        if (FAILED(shader->GetResourceBindingDesc(i, &binding)) || binding.BindCount != 1)
            return false;
        if (binding.Type == D3D_SIT_SAMPLER && binding.BindPoint == 0 && !(binding.uFlags & D3D_SIF_COMPARISON_SAMPLER))
            continue;
        if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 0 &&
            std::strcmp(binding.Name, "LinkSpanCamera") == 0) {
            auto* buffer = shader->GetConstantBufferByName(binding.Name);
            if (!CheckCameraBuffer(*buffer))
                return false;
            usesCamera = true;
            continue;
        }
        if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 1 &&
            std::strcmp(binding.Name, "LinkSpanScene") == 0) {
            auto* buffer = shader->GetConstantBufferByName(binding.Name);
            if (!CheckVectorArrayBuffer(*buffer, sizeof(SceneConstants) / 16))
                return false;
            usesScene = true;
            continue;
        }
        if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 2 &&
            std::string_view(binding.Name) == "LinkSpanSunShadow") {
            auto* buffer = shader->GetConstantBufferByName(binding.Name);
            size_t capacity = 0;
            if (CheckVectorArrayBuffer(*buffer, ShaderPacks::SolarCascades::ConstantVectors)) {
                capacity = ShaderPacks::SolarCascades::MaxCascades;
            } else if (CheckVectorArrayBuffer(*buffer, sizeof(SunShadowMap::Constants) / 16)) {
                capacity = 1;
            } else {
                return false;
            }
            // Todos os passes compartilham o mesmo SRV. Um consumidor antigo
            // não sabe limitar PCF ao tile: o menor contrato determina o atlas.
            sunCascadeCapacity = sunCascadeCapacity ? std::min(sunCascadeCapacity, capacity) : capacity;
            usesSunConstants = true;
            continue;
        }
        if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 3 &&
            std::string_view(binding.Name) == "LinkSpanTransmission") {
            auto* buffer = shader->GetConstantBufferByName(binding.Name);
            if (!pass.transmissionMaterials || !CheckVectorArrayBuffer(*buffer, sizeof(TransmissionConstants) / 16))
                return false;
            usesTransmission = true;
            continue;
        }
        if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 4 &&
            std::string_view(binding.Name) == "LinkSpanPointShadow") {
            auto* buffer = shader->GetConstantBufferByName(binding.Name);
            if (std::find(pass.inputs.begin(), pass.inputs.end(), ScenePointShadowInput) == pass.inputs.end()) {
                return false;
            }
            constexpr UINT vectorsPerLight = sizeof(PointShadowConstants) / 16;
            if (CheckVectorArrayBuffer(*buffer, vectorsPerLight * MaxPointLights)) {
                pointLightCapacity = MaxPointLights;
            } else if (CheckVectorArrayBuffer(*buffer, vectorsPerLight)) {
                pointLightCapacity = std::max<size_t>(pointLightCapacity, 1);
            } else {
                return false;
            }
            continue;
        }
        if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 5 &&
            std::string_view(binding.Name) == "LinkSpanWaterRipple") {
            if (!CheckVectorArrayBuffer(*shader->GetConstantBufferByName(binding.Name),
                                        sizeof(WaterRipple::Frame) / 16))
                return false;
            reflectedWaterRipple = true;
            continue;
        }
        if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 8 &&
            std::string_view(binding.Name) == "LinkSpanHistory") {
            if (!CheckVectorArrayBuffer(*shader->GetConstantBufferByName(binding.Name), 1))
                return false;
            continue;
        }
        if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 6 &&
            std::string_view(binding.Name) == "LinkSpanWeather") {
            if (!CheckVectorArrayBuffer(*shader->GetConstantBufferByName(binding.Name), sizeof(WetSurface::Frame) / 16))
                return false;
            usesWeather = true;
            continue;
        }
        if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 7 &&
            std::string_view(binding.Name) == "LinkSpanDecals") {
            if (!CheckVectorArrayBuffer(*shader->GetConstantBufferByName(binding.Name),
                                        sizeof(ImpactDecals::Frame) / 16))
                return false;
            usesDecals = true;
            continue;
        }
        if (binding.Type == D3D_SIT_TEXTURE && binding.BindPoint == 4 &&
            std::string_view(binding.Name) == "LinkSpanWaterRippleField") {
            if (binding.Dimension != D3D_SRV_DIMENSION_TEXTURE2D || binding.ReturnType != D3D_RETURN_TYPE_FLOAT) {
                return false;
            }
            reflectedWaterRipple = true;
            continue;
        }
        if (binding.Type != D3D_SIT_TEXTURE || binding.Dimension != D3D_SRV_DIMENSION_TEXTURE2D ||
            binding.BindPoint >= pass.inputs.size())
            return false;
        const auto input = pass.inputs[binding.BindPoint];
        if (input == UnboundInput ||
            ((input == SceneDepthInput || input == SceneSunShadowInput || input == SceneGeometryShadowInput ||
              input == SceneFogShadowInput || input == ScenePointShadowInput) &&
             binding.ReturnType != D3D_RETURN_TYPE_FLOAT)) {
            return false;
        }
    }
    return true;
}

} // namespace
#endif

namespace ShaderPacks {

bool ActivateWorldPass(std::span<const PassSource> sources, std::string_view name, std::string& error) {
#ifdef _WIN32
    auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
    auto interpreter = window ? window->GetInterpreterWeak().lock() : nullptr;
    auto* renderer = interpreter ? interpreter->GetCurrentRenderingAPI() : nullptr;
    auto* dx11 = dynamic_cast<Fast::GfxRenderingAPIDX11*>(renderer);
    if (!dx11 || !dx11->mDevice) {
        error = "Selecione o DirectX 11 para ativar este pack.";
        return false;
    }
    const auto compiler = GetModuleHandleW(L"D3DCompiler_47.dll");
    const auto compile =
        compiler ? reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
    const auto reflect =
        compiler ? reinterpret_cast<decltype(&D3DReflect)>(GetProcAddress(compiler, "D3DReflect")) : nullptr;
    if (!compile || !reflect) {
        error = "Compilador DirectX 11 indisponível.";
        return false;
    }

    if (!ValidPassSources(sources)) {
        error = "Grafo de passes inválido.";
        return false;
    }
    auto next = std::make_shared<WorldPass>();
    next->device = dx11->mDevice;
    next->name = name;
    ComPtr<ID3DBlob> vs;
    bool usesCamera = false;
    bool usesScene = false;
    bool usesSunConstants = false;
    bool usesTransmission = false;
    bool usesGeometryShadow = false;
    bool usesWeather = false;
    bool usesDecals = false;
    const D3D_SHADER_MACRO capabilities[] = { { "LINKSPAN_HAS_WEATHER", "1" },
                                              { "LINKSPAN_HAS_DECALS", "1" },
                                              { nullptr, nullptr } };
    constexpr UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL2;
    reflectedWaterRipple = false;
    for (const auto& pass : sources) {
        ComPtr<ID3DBlob> ps, diagnostic;
        PackIncludes includes(pass);
        if (FAILED(compile(pass.source.data(), pass.source.size(), pass.file.c_str(), capabilities, &includes, "main",
                           "ps_4_0", flags, 0, ps.GetAddressOf(), diagnostic.GetAddressOf()))) {
            error = diagnostic ? static_cast<const char*>(diagnostic->GetBufferPointer())
                               : "Falha ao compilar HLSL: " + pass.file;
            return false;
        }
        if (!CheckPassInterface(reflect, *ps.Get(), pass, usesCamera, usesScene, usesSunConstants, usesTransmission,
                                next->pointLightCapacity, next->sunCascadeCapacity, usesWeather, usesDecals)) {
            error = "Assinatura ou recursos fora do contrato do passe: " + pass.file;
            return false;
        }
        PixelPass compiled{ {}, pass.divisor, pass.inputs, pass.width, pass.height, pass.persistent };
        next->hasHistory |= pass.persistent;
        if (FAILED(next->device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr,
                                                   compiled.shader.GetAddressOf()))) {
            error = "Falha ao criar o shader DirectX 11: " + pass.file;
            return false;
        }
        next->usesDepth |= std::find(pass.inputs.begin(), pass.inputs.end(), SceneDepthInput) != pass.inputs.end();
        next->usesSunShadow |=
            std::find(pass.inputs.begin(), pass.inputs.end(), SceneSunShadowInput) != pass.inputs.end();
        usesGeometryShadow |=
            std::find(pass.inputs.begin(), pass.inputs.end(), SceneGeometryShadowInput) != pass.inputs.end();
        next->lightsFog |= std::find(pass.inputs.begin(), pass.inputs.end(), SceneFogShadowInput) != pass.inputs.end();
        next->passes.push_back(std::move(compiled));
    }
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    raster.MultisampleEnable = TRUE;
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler.MaxAnisotropy = 1;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(compile(fullscreenVertex.data(), fullscreenVertex.size(), nullptr, nullptr, nullptr, "main", "vs_4_0",
                       flags, 0, vs.GetAddressOf(), nullptr)) ||
        FAILED(next->device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr,
                                                next->vertex.GetAddressOf())) ||
        FAILED(next->device->CreateRasterizerState(&raster, next->raster.GetAddressOf())) ||
        FAILED(next->device->CreateSamplerState(&sampler, next->sampler.GetAddressOf()))) {
        error = "Falha ao criar o passe DirectX 11.";
        return false;
    }
    if (next->usesDepth && dx11->mFeatureLevel >= D3D_FEATURE_LEVEL_10_1) {
        ComPtr<ID3DBlob> resolve;
        if (FAILED(compile(resolveDepthShader.data(), resolveDepthShader.size(), "scene.depth", nullptr, nullptr,
                           "main", "ps_4_1", flags, 0, resolve.GetAddressOf(), nullptr)) ||
            FAILED(next->device->CreatePixelShader(resolve->GetBufferPointer(), resolve->GetBufferSize(), nullptr,
                                                   next->resolveDepth.GetAddressOf()))) {
            error = "Falha ao criar a resolução MSAA da profundidade.";
            return false;
        }
    }
    if (usesCamera) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(CameraConstants);
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(next->device->CreateBuffer(&desc, nullptr, next->cameraBuffer.GetAddressOf()))) {
            error = "Falha ao criar os dados da câmera no DirectX 11.";
            return false;
        }
    }
    {
        D3D11_BUFFER_DESC historyDesc{};
        historyDesc.ByteWidth = sizeof(SceneVector);
        historyDesc.Usage = D3D11_USAGE_DEFAULT;
        historyDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(next->device->CreateBuffer(&historyDesc, nullptr, next->historyBuffer.GetAddressOf()))) {
            error = "Falha ao criar constantes de histórico.";
            return false;
        }
    }
    if (usesScene) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(SceneConstants);
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(next->device->CreateBuffer(&desc, nullptr, next->sceneBuffer.GetAddressOf()))) {
            error = "Falha ao criar os dados da cena no DirectX 11.";
            return false;
        }
    }
    usesGeometryShadow |= next->lightsFog;
    // O contrato WorldFogSun no Fast3D ainda consome somente o prefixo legado.
    if (next->lightsFog || !next->sunCascadeCapacity)
        next->sunCascadeCapacity = 1;
    if (next->usesSunShadow && usesGeometryShadow) {
        error = "O pack deve escolher um mapa de sombras para o buffer LinkSpanSunShadow.";
        return false;
    }
    if (usesGeometryShadow) {
        next->geometryShadow = std::make_unique<GeometryShadow>();
        if (!next->geometryShadow->Initialize(*next->device.Get(), compile, 0,
                                              static_cast<unsigned>(next->sunCascadeCapacity))) {
            error = "Falha ao criar o mapa de sombras da geometria no DirectX 11.";
            return false;
        }
    }
    if (usesSunConstants) {
        D3D11_BUFFER_DESC desc{};
        // Um buffer maior também é válido para shaders legados; necessário
        // para passes estendidos em um pack misto que negocie somente um tile.
        desc.ByteWidth = sizeof(SolarCascades::Constants);
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if ((!next->usesSunShadow && !usesGeometryShadow) ||
            FAILED(next->device->CreateBuffer(&desc, nullptr, next->sunShadowBuffer.GetAddressOf()))) {
            error = "LinkSpanSunShadow exige um mapa de sombras e um buffer DirectX 11 compatível.";
            return false;
        }
    }
    if (usesTransmission) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(TransmissionConstants);
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(next->device->CreateBuffer(&desc, nullptr, next->transmissionBuffer.GetAddressOf()))) {
            error = "Falha ao criar os dados de transmissão no DirectX 11.";
            return false;
        }
        next->transmissionMaterials = sources.front().transmissionMaterials;
    }
    if (next->pointLightCapacity) {
        if (!next->transmissionMaterials || next->transmissionMaterials->empty()) {
            error = "LinkSpanPointShadow exige materiais de transmissão.";
            return false;
        }
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(sizeof(PointShadowConstants) * next->pointLightCapacity);
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        next->pointShadow = std::make_unique<GeometryShadow>();
        if (!next->pointShadow->Initialize(*next->device.Get(), compile,
                                           static_cast<unsigned>(next->pointLightCapacity)) ||
            FAILED(next->device->CreateBuffer(&desc, nullptr, next->pointShadowBuffer.GetAddressOf()))) {
            error = "Falha ao criar o mapa de sombra da luz pontual no DirectX 11.";
            return false;
        }
    }
    if (usesWeather) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(WetSurface::Frame);
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(next->device->CreateBuffer(&desc, nullptr, next->weatherBuffer.GetAddressOf()))) {
            error = "Falha ao criar LinkSpanWeather no DirectX 11.";
            return false;
        }
    }
    if (usesDecals) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(ImpactDecals::Frame);
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        const ImpactDecals::Frame empty{};
        const D3D11_SUBRESOURCE_DATA initial{ &empty, 0, 0 };
        if (FAILED(next->device->CreateBuffer(&desc, &initial, next->decalsBuffer.GetAddressOf()))) {
            error = "Falha ao criar LinkSpanDecals no DirectX 11.";
            return false;
        }
    }
    Fast::SetWorldMaterialMatcher(MatchWorldMaterial);
    Fast::SetWorldPostProcess(DrawWorld);
    next->usesWaterRipple = reflectedWaterRipple;
    WaterRipple::SetWanted(reflectedWaterRipple);
    ImpactDecals::SetWanted(usesDecals);
    activePass.store(std::move(next));
    return true;
#else
    error = "Este build não inclui DirectX 11.";
    return false;
#endif
}

void DisableWorldPass() {
#ifdef _WIN32
    activePass.store(nullptr);
    WaterRipple::SetWanted(false);
    ImpactDecals::SetWanted(false);
#endif
}

} // namespace ShaderPacks

extern "C" void ShaderPacks_DrawWorldPass(struct GraphicsContext* gfxCtx) {
#ifdef _WIN32
    if (const auto pass = activePass.load()) {
        auto* command = gfxCtx->overlay.p++;
        command->words.w0 = FAST_GFX_WORLD_POSTPROCESS << 24;
        const bool camera =
            pass->cameraBuffer.Get() != nullptr || (pass->geometryShadow && pass->sunCascadeCapacity > 1);
        const bool scene = pass->sceneBuffer.Get() != nullptr;
        const bool shadow = pass->usesSunShadow;
        const bool geometry = pass->geometryShadow != nullptr;
        command->words.w1 = camera || scene || shadow || geometry || pass->transmissionBuffer || pass->hasHistory
                                ? QueueWorldSnapshot(gfxCtx, camera, scene, shadow, geometry, pass->lightsFog,
                                                     pass->pointShadow ? pass->transmissionMaterials.get() : nullptr,
                                                     pass->pointLightCapacity)
                                : 0;
    }
#endif
}

extern "C" void ShaderPacks_CaptureWorldGeometry(struct GraphicsContext* gfxCtx, int enable) {
#ifdef _WIN32
    const auto pass = activePass.load();
    const auto* play = gPlayState;
    if (!pass || !play)
        return;
    const auto scene = play->sceneNum;
    const bool materialScene = pass->transmissionMaterials &&
                               std::any_of(pass->transmissionMaterials->begin(), pass->transmissionMaterials->end(),
                                           [scene](const auto& material) { return material.scene == scene; });
    if ((pass->pointShadow && materialScene) || (pass->geometryShadow && UsesSolarGeometry(scene))) {
        auto* command = gfxCtx->polyOpa.p++;
        command->words.w0 =
            (FAST_GFX_WORLD_POSTPROCESS << 24) | (enable ? FAST_WORLD_GEOMETRY_BEGIN : FAST_WORLD_GEOMETRY_END);
        command->words.w1 = 0;
    }
    if (materialScene) {
        auto* command = gfxCtx->polyXlu.p++;
        command->words.w0 =
            (FAST_GFX_WORLD_POSTPROCESS << 24) | (enable ? FAST_WORLD_TRANSMISSION_BEGIN : FAST_WORLD_TRANSMISSION_END);
        command->words.w1 = scene;
    }
#endif
}
