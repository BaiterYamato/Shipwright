#pragma once

#ifdef _WIN32
#include "SunShadow.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <span>
#include <wrl/client.h>

namespace Fast {
struct WorldCutoutGeometry;
}

namespace ShaderPacks {

struct GeometryShadow {
    bool Initialize(ID3D11Device& device, decltype(&D3DCompile) compile);
    bool Draw(ID3D11DeviceContext& ctx, std::span<const std::array<float, 3>> triangles,
              const Fast::WorldCutoutGeometry& cutouts,
              const std::array<float, 3>& focus, const std::array<float, 4>& sun, SunShadowMap::Constants& constants);
    ID3D11ShaderResourceView* View() const { return view.Get(); }

  private:
    void DrawCutouts(ID3D11DeviceContext& ctx, const Fast::WorldCutoutGeometry& cutouts,
                     const SunShadowMap::Constants& constants);

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
