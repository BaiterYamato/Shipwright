/* Carga de recurso por nome com a guarda de nulo do fork NEI.
 *
 * O fork mudou ResourceMgr_LoadTexOrDListByName no host para devolver NULL quando o recurso não existe, e o código
 * dele usa isso como teste de existência: o kaleido pergunta por texturas do mm.o2r (gPauseQuestStatus01Tex,
 * ícones de equipamento) antes de desenhá-las. No host do Link-Span ela derruba o jogo ao ler o recurso nulo, e
 * abrir o pause caía no IResource::GetInitData. O nei_host_overrides.h troca as chamadas do fork por esta, que
 * pergunta antes ao VFS (NeiAssets_HasFile, assets.cpp). O fork também guardou o ResourceMgr_LoadIfDListByName,
 * mas só o código de formas, que não entra na DLL, o chama.
 *
 * O arquivo guarda também o desvio das caixas de texto (fim do arquivo). */
#undef ResourceMgr_LoadTexOrDListByName
#undef ResourceMgr_FileExists
#undef Message_StartTextbox
#undef Message_ContinueTextbox

char* ResourceMgr_LoadTexOrDListByName(const char* filePath);
int NeiAssets_HasFile(const char* path);

u8 NeiResource_FileExists(const char* filePath) {
    return NeiAssets_HasFile(filePath) ? 1 : 0;
}

char* NeiResource_LoadTexOrDListByName(const char* filePath) {
    if (!NeiAssets_HasFile(filePath)) {
        return NULL;
    }
    return ResourceMgr_LoadTexOrDListByName(filePath);
}

/* Textos que o fork monta por hook C++ (OnOpenText) na hora de abrir a caixa: o text.cpp grava o do id no host antes
 * (NEI-011). Os ids que não são do fork passam direto. */
void Message_StartTextbox(PlayState* play, u16 textId, Actor* actor);
void Message_ContinueTextbox(PlayState* play, u16 textId);
int NeiText_Prepare(uint16_t id);

void NeiMessage_StartTextbox(PlayState* play, u16 textId, Actor* actor) {
    NeiText_Prepare(textId);
    Message_StartTextbox(play, textId, actor);
}

void NeiMessage_ContinueTextbox(PlayState* play, u16 textId) {
    NeiText_Prepare(textId);
    Message_ContinueTextbox(play, textId);
}
