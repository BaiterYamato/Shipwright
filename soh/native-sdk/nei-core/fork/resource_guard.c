/* Carga de recurso por nome com a guarda de nulo do fork NEI.
 *
 * O fork mudou ResourceMgr_LoadTexOrDListByName no host para devolver NULL quando o recurso não existe, e o código
 * dele usa isso como teste de existência: o kaleido pergunta por texturas do mm.o2r (gPauseQuestStatus01Tex,
 * ícones de equipamento) antes de desenhá-las. No host do Link-Span ela derruba o jogo ao ler o recurso nulo, e
 * abrir o pause caía no IResource::GetInitData. O nei_host_overrides.h troca as chamadas do fork por esta, que
 * pergunta antes ao VFS (NeiAssets_HasFile, assets.cpp). O fork também guardou o ResourceMgr_LoadIfDListByName,
 * mas só o código de formas, que não entra na DLL, o chama. */
#undef ResourceMgr_LoadTexOrDListByName

char* ResourceMgr_LoadTexOrDListByName(const char* filePath);
int NeiAssets_HasFile(const char* path);

char* NeiResource_LoadTexOrDListByName(const char* filePath) {
    if (!NeiAssets_HasFile(filePath)) {
        return NULL;
    }
    return ResourceMgr_LoadTexOrDListByName(filePath);
}
