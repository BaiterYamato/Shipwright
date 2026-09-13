// Instância do contexto e shims de heap/load do interpretador do MM.
//
// Ver MmAudioContext.h para o porquê de cada shim. Resumo: seqplayer.c e
// playback.c usam 12 símbolos de heap.c/load.c, todos rasos, e portar os 3.975
// linhas dos dois arquivos seria duplicar o que o LUS já resolve.

#include "soh/mmaudio/mmseq/MmAudioContext.h"
#include "soh/mmaudio/mmseq/MmAudioBridge.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace mmsfx {

AudioContext gAudioCtx;
AudioCustomSeqFunction gAudioCustomSeqFunction = nullptr;

// O recurso já veio pronto do ResourceMgr antes de qualquer coisa tocar, então
// "está carregado?" é sempre sim. Devolver 0 aqui faria o interpretador ficar
// esperando um carregamento assíncrono que nunca acontece.
s32 AudioLoad_IsFontLoadComplete(s32 fontId) {
    (void)fontId;
    return 1;
}

s32 AudioLoad_IsSeqLoadComplete(s32 seqId) {
    (void)seqId;
    return 1;
}

// Marcadores do estado de carregamento do MM. Sem tabela de carregamento aqui,
// não há o que marcar.
void AudioLoad_SetFontLoadStatus(s32 fontId, s32 status) {
    (void)fontId;
    (void)status;
}

void AudioLoad_SetSeqLoadStatus(s32 seqId, s32 status) {
    (void)seqId;
    (void)status;
}

// Busca nos caches do heap do N64. Não temos esse heap: quem resolve recurso
// por id é o carregador em MmSoundFont.cpp. Devolver nullptr faz o chamador
// seguir pelo caminho de "não está em cache", que é o correto.
AutoPtr AudioHeap_SearchCaches(s32 tableType, s32 cache, s32 id) {
    (void)tableType;
    (void)cache;
    (void)id;
    return AutoPtr{ nullptr };
}

AutoPtr AudioHeap_AllocZeroed(AudioAllocPool* pool, u32 size) {
    (void)pool;
    return AutoPtr{ std::calloc(1, size) };
}

// No N64 esta alocação vinha de uma região alinhada para DMA. Aqui não há DMA;
// calloc comum serve.
AutoPtr AudioHeap_AllocDmaMemory(AudioAllocPool* pool, u32 size) {
    (void)pool;
    return AutoPtr{ std::calloc(1, size) };
}

// De thread.c. O gerador do MM é um LCG simples; qualquer sequência serve, já
// que só alimenta variação de nota.
u32 AudioThread_NextRandom(void) {
    static u32 state = 0x12345678;
    state = state * 1103515245 + 12345;
    return state;
}

// Sem editor de áudio do 2S2H: nunca há substituição de sequência.
s32 AudioEditor_GetReplacementSeq(s32 seqId) {
    return seqId;
}

// Sem as CVars do 2S2H: sempre o padrão que o chamador pediu.
f32 CVarGetFloat(const char* name, f32 defaultValue) {
    (void)name;
    return defaultValue;
}

// ---------------------------------------------------------------------------
// Carregamento assíncrono do N64. Aqui não existe: o recurso chega pronto pelo
// ResourceMgr antes de qualquer nota tocar. Marcar isDone=1 imediatamente é o
// que faz o interpretador seguir em vez de esperar para sempre.
// ---------------------------------------------------------------------------

s32 AudioLoad_SyncInitSeqPlayer(s32 playerIndex, s32 seqId, s32 arg2) {
    (void)playerIndex;
    (void)arg2;
    return seqId;
}

s32 AudioLoad_SlowLoadSample(s32 fontId, s32 instId, s8* isDone) {
    (void)fontId;
    (void)instId;
    if (isDone != nullptr) {
        *isDone = 1;
    }
    return 0;
}

s32 AudioLoad_SlowLoadSeq(s32 seqId, u8* ramAddr, s8* isDone) {
    (void)seqId;
    (void)ramAddr;
    if (isDone != nullptr) {
        *isDone = 1;
    }
    return 0;
}

void AudioLoad_ScriptLoad(s32 tableType, s32 id, s8* isDone) {
    (void)tableType;
    (void)id;
    if (isDone != nullptr) {
        *isDone = 1;
    }
}

// Filtros passa-baixa/passa-alta do reverb. O caminho de SFX não usa reverb
// nesta fase; deixar o buffer zerado é neutro.
void AudioHeap_LoadFilter(s16* filter, s32 lowPassCutoff, s32 highPassCutoff) {
    (void)lowPassCutoff;
    (void)highPassCutoff;
    if (filter != nullptr) {
        std::memset(filter, 0, 8 * sizeof(s16));
    }
}

// ---------------------------------------------------------------------------
// Lado do MM da ponte com o ResourceMgr. O cast dos ponteiros opacos é seguro
// porque as structs de soundfont e sequência são binário-compatíveis entre os
// dois decomps — verificado campo a campo e travado por static_assert em
// MmAudioTypesCheck.cpp.
// ---------------------------------------------------------------------------

SoundFont* ResourceMgr_LoadAudioSoundFontByName(const char* path) {
    return reinterpret_cast<SoundFont*>(MmBridge_LoadSoundFont(path));
}

SequenceData ResourceMgr_LoadSeqByName(const char* path) {
    SequenceData* data = reinterpret_cast<SequenceData*>(MmBridge_LoadSequence(path));
    return (data != nullptr) ? *data : SequenceData{};
}

// Tabelas de caminho por id, sem o prefixo "mm/" — a ponte o acrescenta.
// No 2S2H são preenchidas na inicialização a partir do índice do archive; aqui
// os nomes são posicionais e previsíveis, então geramos direto.
namespace {
constexpr s32 kNumFonts = 41;      // audio/fonts/ do mm.o2r
constexpr s32 kNumSequences = 128; // audio/sequences/ do mm.o2r

// Armazenamento POR TABELA. A primeira versão usava `static` dentro da função
// geradora, então as duas chamadas compartilhavam os mesmos vetores: a segunda
// anexava aos dados da primeira e devolvia o mesmo ponteiro-base, fazendo
// gSequenceMap[0] apontar para "audio/fonts/Soundfont_0". O interpretador
// carregava um soundfont como se fosse script de sequência.
struct PathTable {
    std::vector<std::string> storage;
    std::vector<char*> pointers;
};

PathTable gFontPaths;
PathTable gSeqPaths;

void BuildPathTable(PathTable& table, const char* prefix, s32 count, char*** out) {
    table.storage.clear();
    table.pointers.clear();
    table.storage.reserve(count);
    table.pointers.reserve(count);

    // Duas passadas de propósito: push_back pode realocar o vetor de strings, e
    // um c_str() capturado antes disso vira ponteiro pendurado. Só depois de
    // todas as strings existirem é que os ponteiros são colhidos.
    for (s32 i = 0; i < count; i++) {
        table.storage.push_back(std::string(prefix) + std::to_string(i));
    }
    for (std::string& path : table.storage) {
        table.pointers.push_back(const_cast<char*>(path.c_str()));
    }
    *out = table.pointers.data();
}
} // namespace

char** gFontMap = nullptr;
char** gSequenceMap = nullptr;

void MmAudio_InitPathTables(void) {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    BuildPathTable(gFontPaths, "audio/fonts/Soundfont_", kNumFonts, &gFontMap);
    BuildPathTable(gSeqPaths, "audio/sequences/Sequence_", kNumSequences, &gSequenceMap);
}

// Do mixer do SoH, para amostras Opus. O mm.o2r não traz nenhuma (todas as 682
// são VADPCM), então este caminho nunca é alcançado — mas o decomp referencia a
// função e o linker exige o símbolo.
void aOPUSFree(struct OggOpusFile* opusFile) {
    (void)opusFile;
}

} // namespace mmsfx
