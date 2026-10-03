extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <cstdio>
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    lua_State* lua = luaL_newstate(); luaL_openlibs(lua);
    lua_pushstring(lua, argv[2]); lua_setglobal(lua, "MIC_MAIN");
    const int result = luaL_dofile(lua, argv[1]);
    if (result) std::fprintf(stderr, "%s\n", lua_tostring(lua, -1));
    lua_close(lua); return result ? 1 : 0;
}
