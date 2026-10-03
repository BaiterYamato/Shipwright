#include <vector>
#include <string>
#include <algorithm>
extern "C" unsigned char ResourceGetIsCustomByName(const char*);
extern "C" {
short NeiBackend_RegisterActor(const char*, unsigned, unsigned, unsigned,
    void(*)(void*,void*), void(*)(void*,void*), void(*)(void*,void*), void(*)(void*,void*));
}
#include "mods/season_scene.cpp"
