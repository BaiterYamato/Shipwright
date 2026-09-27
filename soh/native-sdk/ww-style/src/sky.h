// Céu do Wind Waker (WWSkyEnv/WWSkyGradient/WWNightSky/WWClouds/WWWindWisps do fork): interface interna.
#pragma once

#include <cstdint>
#include <vector>

#include "ww_style.h"

namespace WWStyle {

// Clima lido do EnvironmentContext: nublado 0..1 (suavizado), tempestade 0..1 e a cor de névoa da cena.
struct SkyWeather {
    float cloudiness;
    float storm;
    uint8_t fogColor[3];
};

// As cinco cores agendadas do céu para a hora e o clima: sky = cúpula, kasumi = névoa fina do horizonte,
// usoUmi = abaixo da linha do horizonte, kumo/kumoCenter = borda e centro das nuvens.
struct SkyColors {
    uint8_t sky[3];
    uint8_t kasumi[3];
    uint8_t usoUmi[3];
    uint8_t kumo[3];
    uint8_t kumoCenter[3];
};

struct SkyWind {
    float x;
    float z;
    float power; // 0,3..0,9, a faixa do dKyw_get_wind_vecpow
};

// Textura RGBA32 das nuvens: gerada pela DLL ou trazida por um pacote de texturas (mesmo caminho do fork).
struct CloudTexture {
    const uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
};

// Lista de comandos de um desenho do céu. O renderer lê até o fim do frame; cada desenho tem a sua e a refaz
// no frame seguinte.
struct GfxList {
    std::vector<Gfx> cmds;
    Gfx* Begin(size_t capacity) {
        cmds.resize(capacity);
        return cmds.data();
    }
};

// Marca da última carga de cena vista por quem guarda estado transitório entre frames.
struct PlayStamp {
    const void* play = nullptr;
    int16_t scene = -1;
    uint32_t frames = 0;
};

// sky_env.cpp
// Verdadeiro numa carga nova: outro PlayState, outra cena ou o contador de frames recomeçado (a mesma cena
// recarregada). Atualiza a marca a cada chamada.
bool IsNewPlay(PlayStamp& stamp, const PlayState* play);
SkyWeather SampleWeather(PlayState* play);
float SunHeight(const SaveContext* save);
void SampleColors(const SaveContext* save, const SkyWeather& weather, SkyColors& out);
void NightColors(SkyColors& out);
SkyWind Wind(const PlayState* play);
float HorizonYForEye(const Settings& cfg, float eyeY);
bool SkyAvailable(const PlayState* play);
void SplitBegin(const Settings& cfg, Gfx*& g);
void SplitEnd(const Settings& cfg, Gfx*& g);
// Matriz de translação no filho de interpolação do céu (camera_epoch, como o skybox vanilla), ou nulo.
const void* SkyMatrix(Mod& mod, float x, float y, float z);
float SkyTicks(Mod& mod); // frames de 30 Hz neste frame de jogo (1,5 a 20 fps), a unidade do Wind Waker

// sky_gradient.cpp
void DrawSkyGradient(Mod& mod, PlayState* play);
void DrawNightSky(Mod& mod, PlayState* play);
void DrawFileSelectSky(Mod& mod, View* view);

// sky_clouds.cpp
void DrawClouds(Mod& mod, PlayState* play);
void DrawWindWisps(Mod& mod, PlayState* play);

} // namespace WWStyle
