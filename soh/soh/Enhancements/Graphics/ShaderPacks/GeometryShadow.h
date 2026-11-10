#pragma once

#ifdef _WIN32
#include "SunShadow.h"
#include "SolarCascades.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <span>
#include <wrl/client.h>

namespace Fast {
struct WorldCutoutGeometry;
}

namespace ShaderPacks {

struct GeometryShadow {
    static constexpr unsigned MaxPointLights = 2;
    static constexpr unsigned PointFaceSize = 512;
    static constexpr float PointNear = 2.0f;
    static constexpr float PointScale = (PointFaceSize - 2.0f) / PointFaceSize;
    bool Initialize(ID3D11Device& device, decltype(&D3DCompile) compile, unsigned pointLights = 0,
                    unsigned solarCapacity = 1);
    bool Draw(ID3D11DeviceContext& ctx, std::span<const std::array<float, 3>> triangles,
              const Fast::WorldCutoutGeometry& cutouts, const std::array<float, 3>& focus,
              const std::array<float, 4>& sun, SunShadowMap::Constants& constants);
    bool DrawCascades(ID3D11DeviceContext& ctx, std::span<const std::array<float, 3>> triangles,
                      const Fast::WorldCutoutGeometry& cutouts, const SolarCascades::Camera& camera,
                      const std::array<float, 3>& focus, const std::array<float, 4>& sun, unsigned count,
                      SolarCascades::Constants& constants);
    unsigned SolarCapacity() const {
        return solarCapacity;
    }
    bool DrawPoint(ID3D11DeviceContext& ctx, std::span<const std::array<float, 3>> triangles,
                   const Fast::WorldCutoutGeometry& cutouts, std::span<const std::array<float, 4>> sources);
    ID3D11ShaderResourceView* View() const {
        return view.Get();
    }

  private:
    unsigned solarCapacity = 1;
    struct Projection {
        SunShadowMap::Constants clip;
        D3D11_VIEWPORT viewport;
    };
    bool DrawProjected(ID3D11DeviceContext& ctx, std::span<const std::array<float, 3>> triangles,
                       const Fast::WorldCutoutGeometry& cutouts, std::span<const Projection> projections);
    void DrawCutouts(ID3D11DeviceContext& ctx, const Fast::WorldCutoutGeometry& cutouts,
                     const SunShadowMap::Constants& clip);

    Microsoft::WRL::ComPtr<ID3D11VertexShader> shader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertices;
    Microsoft::WRL::ComPtr<ID3D11Buffer> light;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cutoutVertices;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cutoutFilter;
};

} // namespace ShaderPacks
#endif
