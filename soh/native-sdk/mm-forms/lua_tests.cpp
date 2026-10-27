extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <cstdio>
int main(int argc, char** argv) {
    if (argc != 3)
        return 1;
    lua_State* state = luaL_newstate();
    if (!state)
        return 2;
    luaL_openlibs(state);
    lua_pushstring(state, argv[2]);
    lua_setglobal(state, "main_path");
    const int status = luaL_dofile(state, argv[1]);
    if (status != LUA_OK)
        std::fprintf(stderr, "%s\n", lua_tostring(state, -1));
    lua_close(state);
    return status == LUA_OK ? 0 : 3;
}
