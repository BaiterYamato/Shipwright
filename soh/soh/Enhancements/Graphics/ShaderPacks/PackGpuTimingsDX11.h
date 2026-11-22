#pragma once

#include "PackGpuTimings.h"
#include <fast/backends/gfx_direct3d_common.h>
#include <spdlog/spdlog.h>
#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ShaderPacks::PackGpu {
// Um slot por quadro de apresentacao (StartFrame), nao por ticket/replay.
// O backend possui somente o relogio compartilhado; seções e estatistica ficam aqui.
class DX11Timer {
  public:
    static constexpr std::size_t RingSize = 8;
    enum Section : std::size_t {
        Prepare, Sun, Point, FogPrepare, ComposeSetup, Pass0,
        FogComposite = Pass0 + 16, Total, BetweenPhases, FoliageDepth, SectionCount
    };

    static std::string Token(const std::string& value) {
        constexpr char hex[] = "0123456789ABCDEF";
        std::string result;
        for (unsigned char c : value) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '.' || c == '_' || c == '-') {
                result += static_cast<char>(c);
            } else {
                result += '%';
                result += hex[c >> 4];
                result += hex[c & 15];
            }
        }
        return result.empty() ? "%00" : result;
    }

    DX11Timer(Fast::GfxRenderingAPIDX11& api, const std::string& packName,
              const std::vector<std::string>& passNames) : api(api), pack(Token(packName)) {
        static std::atomic<uint64_t> nextRun{ 0 };
        run = ++nextRun;
        labels[Prepare] = "prepare";
        labels[Sun] = "sun_cascades";
        labels[Point] = "point_shadow";
        labels[FogPrepare] = "fog_prepare";
        labels[ComposeSetup] = "compose_setup";
        labels[FogComposite] = "fog_composite";
        labels[Total] = "total";
        labels[BetweenPhases] = "between_phases";
        labels[FoliageDepth] = "foliage_depth";
        for (std::size_t i = 0; i < passNames.size() && i < 16; ++i)
            labels[Pass0 + i] = "pass." + std::to_string(i) + "." + Token(passNames[i]);
    }

    // Configurar ANTES de colher: nenhuma janela mistura tamanho, MSAA ou formato.
    void Begin(const D3D11_TEXTURE2D_DESC& desc) {
        Finish(false); // Prepare anterior sem Compose: descartar, nunca deixar query aberta.
        if (width != desc.Width || height != desc.Height || msaa != desc.SampleDesc.Count ||
            quality != desc.SampleDesc.Quality || format != desc.Format ||
            diagnosticMode != api.FoliageDepthDiagnosticMode()) {
            width = desc.Width;
            height = desc.Height;
            msaa = desc.SampleDesc.Count;
            quality = desc.SampleDesc.Quality;
            format = desc.Format;
            diagnosticMode = api.FoliageDepthDiagnosticMode();
            foliageCounts.Reset();
            ++epoch;
            for (auto& window : windows)
                window.Reset();
            batches.fill(0);
        }
        const auto frame = api.GpuTimingFrame();
        // Mesmo quadro pode executar varias display lists/replays: medir so o primeiro.
        if (lastFrame == frame || failed)
            return;
        lastFrame = frame;
        auto& slot = slots[frame % RingSize];
        if (!Collect(slot))
            return; // GPU atrasada: perder esta amostra, sem sobrescrever queries em voo.
        if (!slot.timestamps[Total][0] && !Create(slot))
            return;
        slot.clock = api.AcquireGpuTimingClock();
        if (!slot.clock)
            return;
        slot.frame = frame;
        slot.epoch = epoch;
        slot.diagnosticMode = diagnosticMode;
        slot.started = slot.present = 0;
        slot.discard = false;
        active = static_cast<int>(frame % RingSize);
        Mark(Total, false);
        Mark(Prepare, false);
    }

    void Mark(std::size_t section, bool end) {
        if (active < 0)
            return;
        auto& slot = slots[active];
        if (slot.frame != api.GpuTimingFrame() || slot.clock->ended) {
            Finish(false);
            return;
        }
        const auto bit = uint32_t{ 1 } << section;
        // Uma secao so pode ter um intervalo por quadro.
        if ((!end && (slot.started & bit)) || (end && (!(slot.started & bit) || (slot.present & bit))))
            return;
        api.mContext->End(slot.timestamps[section][end ? 1 : 0].Get());
        if (end)
            slot.present |= bit;
        else
            slot.started |= bit;
    }

    void Finish(bool complete) {
        if (active < 0)
            return;
        if (complete) {
            // Mark pode invalidar uma transacao que atravessou StartFrame.
            Mark(Total, true);
            if (active < 0)
                return;
        }
        auto& slot = slots[active];
        slot.discard = !complete;
        slot.pending = true;
        active = -1;
    }

  private:
    using Query = Microsoft::WRL::ComPtr<ID3D11Query>;
    struct Slot {
        std::array<std::array<Query, 2>, SectionCount> timestamps{};
        std::shared_ptr<Fast::GfxRenderingAPIDX11::GpuTimingClock> clock;
        uint64_t frame = 0, epoch = 0;
        uint32_t started = 0, present = 0;
        bool pending = false, discard = false;
        unsigned diagnosticMode = 0;
    };
    Fast::GfxRenderingAPIDX11& api;
    std::array<Slot, RingSize> slots{};
    std::array<Window<>, SectionCount> windows{};
    std::array<std::string, SectionCount> labels{};
    std::array<uint64_t, SectionCount> batches{};
    std::string pack;
    uint64_t run = 0, epoch = 0, lastFrame = UINT64_MAX;
    UINT width = 0, height = 0, msaa = 0, quality = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    int active = -1;
    bool failed = false;
    unsigned diagnosticMode = 0;
    Fast::FoliageDepth::CountBatch foliageCounts{};

    bool Create(Slot& slot) {
        const D3D11_QUERY_DESC desc{ D3D11_QUERY_TIMESTAMP, 0 };
        for (std::size_t i = 0; i < SectionCount; ++i) {
            if (labels[i].empty() || i == FoliageDepth)
                continue;
            for (auto& query : slot.timestamps[i]) {
                const auto hr = api.mDevice->CreateQuery(&desc, query.ReleaseAndGetAddressOf());
                if (FAILED(hr)) {
                    failed = true; // Recuperar somente ao alternar CVar/recarregar pack.
                    SPDLOG_WARN("M03: timestamps do pack indisponiveis hr={:x}", static_cast<unsigned>(hr));
                    return false;
                }
            }
        }
        return true;
    }

    bool Collect(Slot& slot) {
        if (!slot.pending)
            return true;
        constexpr UINT flags = D3D11_ASYNC_GETDATA_DONOTFLUSH;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock{};
        const auto hr = api.mContext->GetData(slot.clock->disjoint.Get(), &clock, sizeof(clock), flags);
        if (hr == S_FALSE) {
            slot.discard = true;
            return false; // Nao esperar; manter lease ate poder reutilizar em seguranca.
        }
        if (FAILED(hr) || clock.Disjoint || !clock.Frequency || slot.epoch != epoch || slot.discard) {
            slot.pending = false;
            slot.clock.reset();
            return true;
        }
        std::array<std::array<UINT64, 2>, SectionCount> ticks{};
        for (std::size_t i = 0; i < SectionCount; ++i) {
            if (!(slot.present & (uint32_t{ 1 } << i)))
                continue;
            for (std::size_t edge = 0; edge < 2; ++edge) {
                const auto status = api.mContext->GetData(slot.timestamps[i][edge].Get(), &ticks[i][edge],
                                                         sizeof(UINT64), flags);
                if (status != S_OK) {
                    slot.discard = true;
                    // S_FALSE preserva os recursos ate o proximo giro; falha descarta.
                    if (FAILED(status)) {
                        slot.pending = false;
                        slot.clock.reset();
                    }
                    return !slot.pending;
                }
            }
            if (ticks[i][1] < ticks[i][0]) {
                slot.pending = false;
                slot.clock.reset();
                return true; // Rejeitar o quadro inteiro antes de atualizar qualquer janela.
            }
        }
        Fast::FoliageDepth::Sample foliage{};
        const bool haveFoliage = api.ReadFoliageDepthTiming(slot.frame, foliage) && foliage.valid &&
            foliage.mode == slot.diagnosticMode &&
            (!foliage.counts.draws || (foliage.width == width && foliage.height == height && foliage.msaa == msaa &&
                                     foliage.quality == quality && foliage.format == static_cast<uint32_t>(format)));
        for (std::size_t i = 0; i < SectionCount; ++i) {
            if (i == FoliageDepth ? !haveFoliage : !(slot.present & (uint32_t{ 1 } << i)))
                continue;
            auto& window = windows[i];
            if (i == FoliageDepth) {
                if (!window.AddMilliseconds(foliage.milliseconds))
                    continue;
                foliageCounts.Add(foliage.counts);
            } else {
                window.AddTicks(ticks[i][0], ticks[i][1], clock.Frequency, clock.Disjoint != FALSE);
            }
            if (window.Full()) {
                const auto stats = window.Read();
                if (i == FoliageDepth) {
                    SPDLOG_INFO("PackGpuTiming v=1 run={} epoch={} batch={} pack={} section={} n={} "
                                "width={} height={} msaa={} median_ms={:.6f} p95_ms={:.6f} max_ms={:.6f} "
                                "diagnostic_mode={} draws_total={} triangles_total={} draws_max={} triangles_max={}",
                                run, epoch, ++batches[i], pack, labels[i], stats.count,
                                width, height, msaa, stats.median, stats.p95, stats.maximum, diagnosticMode,
                                foliageCounts.total.draws, foliageCounts.total.triangles,
                                foliageCounts.maximum.draws, foliageCounts.maximum.triangles);
                    foliageCounts.Reset();
                } else {
                    SPDLOG_INFO("PackGpuTiming v=1 run={} epoch={} batch={} pack={} section={} n={} "
                                "width={} height={} msaa={} median_ms={:.6f} p95_ms={:.6f} max_ms={:.6f}",
                                run, epoch, ++batches[i], pack, labels[i], stats.count,
                                width, height, msaa, stats.median, stats.p95, stats.maximum);
                }
                window.Reset();
            }
        }
        slot.pending = false;
        slot.clock.reset();
        return true;
    }
};
} // namespace ShaderPacks::PackGpu
