#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "GeometryShadow.h"
#include <fast/world_postprocess.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace ShaderPacks {
namespace {

using Microsoft::WRL::ComPtr;
using Point = std::array<float, 3>;
static_assert(SunShadowMap::Size == SolarCascades::TileSize);
constexpr float Span = 2600.0f;
constexpr float DepthSpan = 6400.0f;
constexpr std::string_view VertexShader = R"(
cbuffer LightSpace : register(b0) { float4 Data[4]; }
float4 main(float3 position : POSITION) : SV_Position {
    float4 p = float4(position, 1.0);
    return float4(dot(p, Data[0]), dot(p, Data[1]), dot(p, Data[2]), dot(p, Data[3]));
}
)";

float Dot(const Point& a, const Point& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Point Normalize(Point v) {
    const float length = std::sqrt(Dot(v, v));
    for (auto& coordinate : v)
        coordinate /= length;
    return v;
}

Point Cross(const Point& a, const Point& b) {
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

std::array<float, 4> Plane(const Point& axis, const Point& focus, float texel) {
    const float snapped = std::round(Dot(axis, focus) / texel) * texel;
    return { axis[0] / Span, axis[1] / Span, axis[2] / Span, 0.5f - snapped / Span };
}

} // namespace

bool GeometryShadow::CreateTarget(ID3D11Device& device, UINT width, UINT height) {
    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.MipLevels = textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    textureDesc.SampleDesc = { 1, 0 };
    textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11DepthStencilView> nextDepth;
    ComPtr<ID3D11ShaderResourceView> nextView;
    if (FAILED(device.CreateTexture2D(&textureDesc, nullptr, texture.GetAddressOf())) ||
        FAILED(device.CreateDepthStencilView(texture.Get(), &dsv, nextDepth.GetAddressOf())) ||
        FAILED(device.CreateShaderResourceView(texture.Get(), &srv, nextView.GetAddressOf())))
        return false;
    depth = std::move(nextDepth);
    view = std::move(nextView);
    return true;
}

bool GeometryShadow::ResizeSolar(ID3D11Device& device, unsigned tileSize) {
    tileSize = SolarCascades::ClampTileSize(tileSize);
    if (pointMode || !shader)
        return false;
    if (tileSize == solarTile)
        return true;
    if (!CreateTarget(device, tileSize * solarCapacity, tileSize))
        return false;
    solarTile = tileSize;
    return true;
}

bool GeometryShadow::Initialize(ID3D11Device& device, decltype(&D3DCompile) compile, unsigned pointLights,
                                unsigned requestedSolarCapacity, unsigned requestedTileSize) {
    if (pointLights > MaxPointLights ||
        (requestedSolarCapacity != 1 && requestedSolarCapacity != SolarCascades::MaxCascades))
        return false;
    solarCapacity = pointLights ? 1 : requestedSolarCapacity;
    pointMode = pointLights != 0;
    solarTile = pointMode ? SunShadowMap::Size : SolarCascades::ClampTileSize(requestedTileSize);
    ComPtr<ID3DBlob> code;
    if (FAILED(compile(VertexShader.data(), VertexShader.size(), "scene.geometryshadow", nullptr, nullptr, "main",
                       "vs_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, code.GetAddressOf(), nullptr)))
        return false;
    const auto* bytecode = code->GetBufferPointer();
    const auto size = code->GetBufferSize();
    const D3D11_INPUT_ELEMENT_DESC input{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA,
                                          0 };
    D3D11_RASTERIZER_DESC rasterDesc{};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.DepthClipEnable = TRUE;
    rasterDesc.ScissorEnable = TRUE;
    D3D11_DEPTH_STENCIL_DESC depthDesc{};
    depthDesc.DepthEnable = TRUE;
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthDesc.DepthFunc = D3D11_COMPARISON_GREATER;
    D3D11_BUFFER_DESC vertexDesc{};
    vertexDesc.ByteWidth = Fast::MaxWorldGeometryVertices * sizeof(Point);
    vertexDesc.Usage = D3D11_USAGE_DYNAMIC;
    vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vertexDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    D3D11_BUFFER_DESC constantDesc{};
    constantDesc.ByteWidth = sizeof(SunShadowMap::Constants);
    constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    const UINT targetWidth = pointLights ? PointFaceSize * 3 : solarTile * solarCapacity;
    const UINT targetHeight = pointLights ? PointFaceSize * 2 * pointLights : solarTile;
    D3D11_BUFFER_DESC cutoutDesc = vertexDesc;
    cutoutDesc.ByteWidth = Fast::MaxWorldCutoutFloats * sizeof(float);
    D3D11_BUFFER_DESC filterDesc = constantDesc;
    filterDesc.ByteWidth = sizeof(Fast::WorldCutoutBatch::filter);
    return SUCCEEDED(device.CreateVertexShader(bytecode, size, nullptr, shader.GetAddressOf())) &&
           SUCCEEDED(device.CreateInputLayout(&input, 1, bytecode, size, inputLayout.GetAddressOf())) &&
           SUCCEEDED(device.CreateRasterizerState(&rasterDesc, raster.GetAddressOf())) &&
           SUCCEEDED(device.CreateDepthStencilState(&depthDesc, depthState.GetAddressOf())) &&
           SUCCEEDED(device.CreateBuffer(&vertexDesc, nullptr, vertices.GetAddressOf())) &&
           SUCCEEDED(device.CreateBuffer(&constantDesc, nullptr, light.GetAddressOf())) &&
           SUCCEEDED(device.CreateBuffer(&cutoutDesc, nullptr, cutoutVertices.GetAddressOf())) &&
           SUCCEEDED(device.CreateBuffer(&filterDesc, nullptr, cutoutFilter.GetAddressOf())) &&
           CreateTarget(device, targetWidth, targetHeight);
}

void GeometryShadow::DrawCutouts(ID3D11DeviceContext& ctx, const Fast::WorldCutoutGeometry& cutouts,
                                 const SunShadowMap::Constants& clip) {
    if (cutouts.batches.empty() || cutouts.vertices.size() > Fast::MaxWorldCutoutFloats)
        return;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx.Map(cutoutVertices.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    auto* data = static_cast<float*>(mapped.pData);
    std::memcpy(data, cutouts.vertices.data(), cutouts.vertices.size_bytes());
    for (const auto& batch : cutouts.batches) {
        for (unsigned i = 0; i < batch.count; ++i) {
            auto* p = data + batch.offset + i * batch.stride;
            const Point world{ p[0], p[1], p[2] };
            for (size_t axis = 0; axis < clip.size(); ++axis) {
                const auto& row = clip[axis];
                p[axis] = world[0] * row[0] + world[1] * row[1] + world[2] * row[2] + row[3];
            }
        }
    }
    ctx.Unmap(cutoutVertices.Get(), 0);

    std::array<ID3D11ShaderResourceView*, Fast::WorldCutoutTextureSlots> previousViews{};
    std::array<ID3D11SamplerState*, Fast::WorldCutoutTextureSlots> previousSamplers{};
    ComPtr<ID3D11Buffer> previousFilter;
    const auto slots = static_cast<UINT>(previousViews.size());
    ctx.PSGetShaderResources(0, slots, previousViews.data());
    ctx.PSGetSamplers(0, slots, previousSamplers.data());
    ctx.PSGetConstantBuffers(1, 1, previousFilter.GetAddressOf());
    ctx.PSSetConstantBuffers(1, 1, cutoutFilter.GetAddressOf());
    for (const auto& batch : cutouts.batches) {
        std::array<ID3D11ShaderResourceView*, Fast::WorldCutoutTextureSlots> views{};
        std::array<ID3D11SamplerState*, Fast::WorldCutoutTextureSlots> samplers{};
        for (unsigned i = 0; i < slots; ++i) {
            views[i] = batch.views[i].Get();
            samplers[i] = batch.samplers[i].Get();
        }
        const UINT stride = batch.stride * sizeof(float), offset = batch.offset * sizeof(float);
        ctx.IASetVertexBuffers(0, 1, cutoutVertices.GetAddressOf(), &stride, &offset);
        ctx.IASetInputLayout(batch.layout.Get());
        ctx.VSSetShader(batch.vertex.Get(), nullptr, 0);
        ctx.PSSetShader(batch.pixel.Get(), nullptr, 0);
        ctx.PSSetShaderResources(0, slots, views.data());
        ctx.PSSetSamplers(0, slots, samplers.data());
        ctx.UpdateSubresource(cutoutFilter.Get(), 0, nullptr, batch.filter.data(), 0, 0);
        // Foliage uses the game's three-point sampler at s8/s9. Restore these immediately
        // so existing cutout batches and the main pipeline retain their original bindings.
        std::array<ID3D11SamplerState*, 2> previousPoints{};
        if (batch.foliage) {
            ctx.PSGetSamplers(8, 2, previousPoints.data());
            const std::array<ID3D11SamplerState*, 2> points{ batch.pointSamplers[0].Get(), batch.pointSamplers[1].Get() };
            ctx.PSSetSamplers(8, 2, points.data());
        }
        ctx.Draw(batch.count, 0);
        if (batch.foliage) {
            ctx.PSSetSamplers(8, 2, previousPoints.data());
            for (auto* sampler : previousPoints) {
                if (sampler)
                    sampler->Release();
            }
        }
    }
    ctx.PSSetConstantBuffers(1, 1, previousFilter.GetAddressOf());
    ctx.PSSetShaderResources(0, slots, previousViews.data());
    ctx.PSSetSamplers(0, slots, previousSamplers.data());
    for (unsigned i = 0; i < slots; ++i) {
        if (previousViews[i])
            previousViews[i]->Release();
        if (previousSamplers[i])
            previousSamplers[i]->Release();
    }
}

bool GeometryShadow::Draw(ID3D11DeviceContext& ctx, std::span<const Point> triangles,
                          const Fast::WorldCutoutGeometry& cutouts, const Point& focus, const std::array<float, 4>& sun,
                          SunShadowMap::Constants& constants) {
    constants = {};
    if (!std::isfinite(sun[0]) || !std::isfinite(sun[1]) || !std::isfinite(sun[2]) || sun[1] < 1.0f ||
        !std::all_of(focus.begin(), focus.end(), [](float x) { return std::isfinite(x); }))
        return false;
    const Point sunDirection = Normalize({ sun[0], sun[1], sun[2] });
    const Point right = Normalize(Cross({ 0, 0, 1 }, sunDirection));
    const Point up = Cross(sunDirection, right);
    const float low = Dot(focus, sunDirection) - DepthSpan * 0.5f;
    const float texel = Span / solarTile;
    const auto u = Plane(right, focus, texel), v = Plane(up, focus, texel);
    const SunShadowMap::Constants clip{ {
        { 2.0f * u[0], 2.0f * u[1], 2.0f * u[2], 2.0f * u[3] - 1.0f },
        { -2.0f * v[0], -2.0f * v[1], -2.0f * v[2], 1.0f - 2.0f * v[3] },
        { sunDirection[0] / DepthSpan, sunDirection[1] / DepthSpan, sunDirection[2] / DepthSpan, -low / DepthSpan },
        { 0.0f, 0.0f, 0.0f, 1.0f },
    } };
    const auto tile = static_cast<float>(solarTile);
    const Projection projection{ clip, { 0, 0, tile, tile, 0, 1 } };
    if (!DrawProjected(ctx, triangles, cutouts, std::span(&projection, 1)))
        return false;
    constants = { u, v, { sunDirection[0], sunDirection[1], sunDirection[2], 0 }, { 1, texel, low, 1.0f / DepthSpan } };
    return true;
}

bool GeometryShadow::DrawCascades(ID3D11DeviceContext& ctx, std::span<const Point> triangles,
                                  const Fast::WorldCutoutGeometry& cutouts, const SolarCascades::Camera& camera,
                                  const Point& focus, const std::array<float, 4>& sun, unsigned count,
                                  SolarCascades::Constants& constants) {
    constants = {};
    count = std::clamp(count, 1u, solarCapacity);
    if (count == 1) {
        SunShadowMap::Constants legacy{};
        if (!Draw(ctx, triangles, cutouts, focus, sun, legacy))
            return false;
        std::copy(legacy.begin(), legacy.end(), constants.begin());
        constants[14] = { 1, static_cast<float>(solarCapacity), 0, 0 };
        return true;
    }
    if (!std::isfinite(sun[0]) || !std::isfinite(sun[1]) || !std::isfinite(sun[2]) || sun[1] < 1) {
        lastFailure = "sol abaixo do horizonte ou inválido";
        return false;
    }
    const auto fitted = SolarCascades::Build(camera, { sun[0], sun[1], sun[2] }, count, solarTile);
    if (fitted.count != count) {
        lastFailure = "cascatas não ajustaram à câmera";
        return false;
    }
    std::array<Projection, SolarCascades::MaxCascades> projections{};
    const auto tile = static_cast<float>(solarTile);
    for (unsigned i = 0; i < count; ++i)
        projections[i] = { fitted.cascades[i].clip, { i * tile, 0, tile, tile, 0, 1 } };
    if (!DrawProjected(ctx, triangles, cutouts, std::span(projections.data(), count)))
        return false;
    for (unsigned i = 0; i < count; ++i) {
        std::copy(fitted.cascades[i].sample.begin(), fitted.cascades[i].sample.end(), constants.begin() + 4 * i);
        constants[12][i] = static_cast<float>(fitted.ends[i]);
        constants[13][i] = static_cast<float>(fitted.blends[i]);
    }
    constants[12][3] = static_cast<float>(camera.nearDepth);
    constants[14] = { static_cast<float>(count), static_cast<float>(solarCapacity), 0, 0 };
    return true;
}

bool GeometryShadow::DrawPoint(ID3D11DeviceContext& ctx, std::span<const Point> triangles,
                               const Fast::WorldCutoutGeometry& cutouts,
                               std::span<const std::array<float, 4>> sources) {
    if (sources.empty() || sources.size() > MaxPointLights)
        return false;
    constexpr std::array<Point, 6> directions{
        { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } }
    };
    std::array<Projection, directions.size() * MaxPointLights> projections{};
    size_t tile = 0;
    for (const auto& source : sources) {
        const Point eye{ source[0], source[1], source[2] };
        const float farPlane = source[3];
        if (!(farPlane > PointNear))
            return false;
        const auto plane = [&eye](const Point& axis, float scale) -> std::array<float, 4> {
            return { axis[0] * scale, axis[1] * scale, axis[2] * scale, -Dot(axis, eye) * scale };
        };
        const float reverseScale = PointNear / (farPlane - PointNear);
        for (const auto& forward : directions) {
            const Point up = forward[1] == 0.0f ? Point{ 0, 1, 0 } : Point{ 0, 0, 1 };
            const auto right = Cross(up, forward);
            auto& [clip, viewport] = projections[tile];
            clip = { plane(right, PointScale), plane(up, PointScale), plane(forward, -reverseScale),
                     plane(forward, 1.0f) };
            clip[2][3] += farPlane * reverseScale;
            viewport = { static_cast<float>(tile % 3 * PointFaceSize),
                         static_cast<float>(tile / 3 * PointFaceSize),
                         PointFaceSize,
                         PointFaceSize,
                         0,
                         1 };
            ++tile;
        }
    }
    return DrawProjected(ctx, triangles, cutouts, std::span(projections.data(), tile));
}

bool GeometryShadow::DrawProjected(ID3D11DeviceContext& ctx, std::span<const Point> triangles,
                                   const Fast::WorldCutoutGeometry& cutouts, std::span<const Projection> projections) {
    if (triangles.empty() && cutouts.batches.empty()) {
        lastFailure = "sem geometria capturada";
        return false;
    }
    if (triangles.size() > Fast::MaxWorldGeometryVertices) {
        lastFailure = "geometria acima do limite";
        return false;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx.Map(vertices.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        lastFailure = "Map do buffer de vértices falhou";
        return false;
    }
    lastFailure = "";
    if (!triangles.empty())
        std::memcpy(mapped.pData, triangles.data(), triangles.size_bytes());
    ctx.Unmap(vertices.Get(), 0);

    std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets{};
    ComPtr<ID3D11DepthStencilView> previousDepth;
    ComPtr<ID3D11DepthStencilState> previousDepthState;
    ComPtr<ID3D11RasterizerState> previousRaster;
    ComPtr<ID3D11VertexShader> previousVS;
    ComPtr<ID3D11PixelShader> previousPS;
    ComPtr<ID3D11InputLayout> previousLayout;
    ComPtr<ID3D11Buffer> previousLight, previousVertices;
    D3D11_PRIMITIVE_TOPOLOGY topology{};
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT count = ARRAYSIZE(viewports), reference = 0, stride = 0, offset = 0;
    UINT scissorCount = ARRAYSIZE(scissors);
    ctx.OMGetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), previousDepth.GetAddressOf());
    ctx.OMGetDepthStencilState(previousDepthState.GetAddressOf(), &reference);
    ctx.RSGetState(previousRaster.GetAddressOf());
    ctx.RSGetViewports(&count, viewports);
    ctx.RSGetScissorRects(&scissorCount, scissors);
    ctx.VSGetShader(previousVS.GetAddressOf(), nullptr, nullptr);
    ctx.PSGetShader(previousPS.GetAddressOf(), nullptr, nullptr);
    ctx.VSGetConstantBuffers(0, 1, previousLight.GetAddressOf());
    ctx.IAGetInputLayout(previousLayout.GetAddressOf());
    ctx.IAGetPrimitiveTopology(&topology);
    ctx.IAGetVertexBuffers(0, 1, previousVertices.GetAddressOf(), &stride, &offset);

    const UINT vertexStride = sizeof(Point), vertexOffset = 0;
    ctx.OMSetRenderTargets(0, nullptr, depth.Get());
    ctx.OMSetDepthStencilState(depthState.Get(), 0);
    ctx.ClearDepthStencilView(depth.Get(), D3D11_CLEAR_DEPTH, 0, 0);
    ctx.RSSetState(raster.Get());
    ctx.VSSetConstantBuffers(0, 1, light.GetAddressOf());
    ctx.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for (const auto& [clip, viewport] : projections) {
        ctx.UpdateSubresource(light.Get(), 0, nullptr, clip.data(), 0, 0);
        ctx.RSSetViewports(1, &viewport);
        {
            const LONG x = static_cast<LONG>(viewport.TopLeftX), y = static_cast<LONG>(viewport.TopLeftY);
            const D3D11_RECT rect{ x, y, x + static_cast<LONG>(viewport.Width),
                                   y + static_cast<LONG>(viewport.Height) };
            ctx.RSSetScissorRects(1, &rect);
        }
        ctx.VSSetShader(shader.Get(), nullptr, 0);
        ctx.PSSetShader(nullptr, nullptr, 0);
        ctx.IASetInputLayout(inputLayout.Get());
        ctx.IASetVertexBuffers(0, 1, vertices.GetAddressOf(), &vertexStride, &vertexOffset);
        ctx.Draw(static_cast<UINT>(triangles.size()), 0);
        DrawCutouts(ctx, cutouts, clip);
    }

    ctx.OMSetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), previousDepth.Get());
    for (auto* target : targets) {
        if (target)
            target->Release();
    }
    ctx.OMSetDepthStencilState(previousDepthState.Get(), reference);
    ctx.RSSetState(previousRaster.Get());
    ctx.RSSetViewports(count, viewports);
    ctx.RSSetScissorRects(scissorCount, scissors);
    ctx.VSSetShader(previousVS.Get(), nullptr, 0);
    ctx.PSSetShader(previousPS.Get(), nullptr, 0);
    ctx.VSSetConstantBuffers(0, 1, previousLight.GetAddressOf());
    ctx.IASetInputLayout(previousLayout.Get());
    ctx.IASetPrimitiveTopology(topology);
    ctx.IASetVertexBuffers(0, 1, previousVertices.GetAddressOf(), &stride, &offset);
    return true;
}

} // namespace ShaderPacks
#endif
