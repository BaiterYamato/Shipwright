#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace water {

struct Point {
    float x{}, z{};
};
struct Gradient {
    float x{}, z{};
};
struct Velocity {
    float x{}, z{};
};

enum class Solver { DampedWave, ShallowWater };
enum class FlowComponent { X, Z };

struct Config {
    std::size_t side{ 128 };
    float cellSize{ 16.0f }; // unidades de mundo
    float planeY{};          // uma waterbox/plano por instância
    float stepSeconds{ 1.0f / 60.0f };
    float waveSpeed{ 180.0f }; // unidades de mundo / segundo
    float damping{ 1.5f };     // gamma em h_tt + 2 gamma h_t = c² lap(h)
    float edgeDamping{ 8.0f }; // gamma adicional na borda
    std::size_t edgeCells{ 8 };
    float maxImpulseSpeed{ 240.0f };             // limite da velocidade injetada, não da onda
    Solver solver{ Solver::DampedWave };         // mantém o comportamento R034 por padrão
    float gravity{ 980.0f };                     // unidades de mundo / segundo² (R035)
    float restDepth{ 180.0f * 180.0f / 980.0f }; // c = sqrt(gravity * restDepth)
    Point backgroundCurrent{};                   // velocidade base constante XZ (R042)
};

// Legado: dois buffers h(t), h(t-dt), alocados somente no construtor.
// CFL: (waveSpeed * stepSeconds / cellSize)^2 <= 0.49 (< 1/2 em 2D).
// Bordas: vizinhos exteriores com altura zero + faixa de amortecimento.
// Ordem de eventos e número de passos iguais => resultado determinístico no
// mesmo executável/ambiente FP; não promete identidade entre compiladores/CPUs.
// ShallowWater: equações LINEARIZADAS em torno de restDepth, altura no centro,
// u/v nas faces. Paredes com fluxo normal zero; transporte upwind conservativo.
// CFL: dt/dx * (sqrt(2*g*H) + |Ux| + |Uz|) <= 0.9; sem substeps adaptativos.
// Instância de uso serial; nenhuma função operacional aloca memória.
class Field {
  public:
    explicit Field(Config config = {}, Point focus = {});
    Field(const Field&) = delete;
    Field& operator=(const Field&) = delete;
    Field(Field&&) = delete;
    Field& operator=(Field&&) = delete;

    // Reancora em múltiplos inteiros de cellSize; conserva os DOIS tempos
    // sobrepostos (ou altura e faces interiores no modo raso; novas paredes zeram
    // o fluxo normal). Recorte/teleporte não conservam volume da janela.
    bool Follow(float focusX, float focusZ) noexcept;
    void Reset() noexcept; // zera tempos; mantém origem e configuração
    void Step() noexcept;  // exatamente um passo fixo

    // Impulso é delta de velocidade vertical (unidades/segundo), com kernel
    // compacto (1 - d²/r²)^2. Raio mínimo: uma célula. Não desloca h instantaneamente.
    // ShallowWater converte o kick em fluxo convergente/divergente nas faces,
    // sem fonte de massa; limita também cada velocidade perturbada a maxImpulseSpeed.
    bool Inject(float x, float z, float radius, float impulse) noexcept;
    // Cápsula varrida entre posições: cada célula recebe no máximo um impulso,
    // sem lacunas e sem depender de subdivisões. Segmento nulo equivale a Inject.
    bool InjectMoving(Point previous, Point current, float radius, float impulse) noexcept;

    // Altura relativa ao plano Y e gradiente dh/dx, dh/dz; zero fora da janela.
    // O gradiente interpola bilinearmente derivadas centrais dos nós, como a GPU.
    float SampleHeight(float x, float z) const noexcept;
    Gradient SampleGradient(float x, float z) const noexcept;
    // Velocidade horizontal total (base + média das faces), bilinear nos centros.
    // Zero fora da janela e no solver legado.
    Velocity SampleVelocity(float x, float z) const noexcept;

    // RGB float intercalado: [h, dh/dx, dh/dz], X rápido, linhas em +Z.
    // Exige pelo menos side²*3 floats; span curto retorna false sem escrever.
    bool PackTexture(std::span<float> rgb) const noexcept;
    // Caminho diretamente publicável em DXGI_FORMAT_R16G16B16A16_FLOAT:
    // converte RGB para half IEEE, arredonda ties-to-even, A=0; side²*4 uint16_t.
    // Satura em +/-65504 (limite de half), sem alocação temporária.
    bool PackTextureRGBA16(std::span<std::uint16_t> rgba) const noexcept;
    // Mesmo RGB bit a bit; A = velocidade total X (padrão) ou Z, em unidades/s.
    // Legado continua A=0. O chamador/pack deve combinar a componente escolhida.
    bool PackTextureRGBA16Flow(std::span<std::uint16_t> rgba,
                               FlowComponent component = FlowComponent::X) const noexcept;
    // uv = worldXZ * xy + zw; nós coincidem com centros de texel (meio texel).
    std::array<float, 4> WorldToUV() const noexcept;

    std::size_t Side() const noexcept {
        return config_.side;
    }
    float CellSize() const noexcept {
        return config_.cellSize;
    }
    float PlaneY() const noexcept {
        return config_.planeY;
    }
    float StepSeconds() const noexcept {
        return config_.stepSeconds;
    }
    float WorldX(std::size_t x) const noexcept;
    float WorldZ(std::size_t z) const noexcept;
    std::uint64_t Steps() const noexcept {
        return steps_;
    }
    // Energia discreta positiva (velocidade e diferenças espaciais entre tempos).
    // No raso inclui g*h² e H*(u²+v²), com correção de tempo escalonado.
    // Sem corrente/força/Follow, não cresce salvo erro FP. Exclui a energia da base.
    double Energy() const noexcept;

  private:
    std::size_t Index(std::size_t x, std::size_t z) const noexcept {
        return z * config_.side + x;
    }
    float HeightAt(int x, int z) const noexcept;
    Gradient GradientAt(int x, int z) const noexcept;
    bool GridPosition(float x, float z, double& gx, double& gz) const noexcept;
    void ShiftBuffer(std::vector<float>& buffer, int dx, int dz, std::size_t width, std::size_t rows) noexcept;
    void ClearState() noexcept;
    void CloseFaces() noexcept;
    void StepShallow() noexcept;
    bool InjectShallow(Point previous, Point current, float radius, float impulse) noexcept;
    void Advect(const std::vector<float>& source, std::vector<float>& target, std::size_t width,
                std::size_t rows) noexcept;
    float DampingAt(int x, int z) const noexcept;
    Velocity VelocityAt(std::size_t x, std::size_t z) const noexcept;
    bool PackRGBA16(std::span<std::uint16_t> rgba, bool flow, FlowComponent component) const noexcept;

    Config config_;
    std::vector<float> height_, previous_;
    // Faces X: (side+1)*side; faces Z: side*(side+1). Scratch só no modo raso.
    std::vector<float> velocityX_, velocityZ_, scratchX_, scratchZ_;
    double originX_{}, originZ_{};
    float lambdaSquared_{};
    std::uint64_t steps_{};
};

} // namespace water
