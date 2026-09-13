// Lado do OoT da ponte. Este arquivo inclui os headers do SoH e NUNCA os do MM
// — ver MmAudioBridge.h para o motivo.

#include "soh/mmaudio/mmseq/MmAudioBridge.h"

#include <string>

#include <spdlog/spdlog.h>
#include <z64.h>

#include "z64audio.h"

#include "soh/ResourceManagerHelpers.h"

namespace {
// Mesmo prefixo que o MmCrossWorldArchive aplica ao montar o mm.o2r.
constexpr const char* kMmNamespace = "mm/";
} // namespace

extern "C" void* MmBridge_LoadSoundFont(const char* pathWithinMm) {
    if (pathWithinMm == nullptr) {
        return nullptr;
    }
    // O factory deriva o namespace das amostras do caminho do próprio recurso
    // (ver AudioSoundFontFactory.cpp), então basta pedir pelo caminho prefixado.
    const std::string full = kMmNamespace + std::string(pathWithinMm);
    return ResourceMgr_LoadAudioSoundFontByName(full.c_str());
}

extern "C" void* MmBridge_LoadSequence(const char* pathWithinMm) {
    if (pathWithinMm == nullptr) {
        return nullptr;
    }
    const std::string full = kMmNamespace + std::string(pathWithinMm);
    SequenceData* seq = ResourceMgr_LoadSeqPtrByName(full.c_str());

    // Diagnóstico: o teste de 25/07 mostrou seqDataSize = 4184423824 (~4 GB) e
    // os primeiros bytes zerados. Logar do LADO DO OOT, antes de qualquer cast
    // para os tipos do MM, isola "o recurso veio errado" de "meu cast está
    // errado" — duas hipóteses que o log anterior não separava.
    if (seq == nullptr) {
        SPDLOG_WARN("ShipLua/mmaudio: sequÃªncia '{}' -> NULO", full);
    } else {
        SPDLOG_INFO("ShipLua/mmaudio: sequÃªncia '{}' -> seqData={} size={} numFonts={} font0={}", full,
                    (const void*)seq->seqData, seq->seqDataSize, seq->numFonts, seq->fonts[0]);
        if (seq->seqData != nullptr && seq->seqDataSize > 0 && seq->seqDataSize < (1u << 24)) {
            const unsigned char* b = (const unsigned char*)seq->seqData;
            SPDLOG_INFO("ShipLua/mmaudio: 1os bytes: {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}", b[0],
                        b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
        }
    }
    return seq;
}
