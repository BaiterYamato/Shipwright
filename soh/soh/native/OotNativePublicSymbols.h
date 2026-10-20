#ifndef LINKSPAN_OOT_PUBLIC_SYMBOLS_H
#define LINKSPAN_OOT_PUBLIC_SYMBOLS_H

#include <stddef.h>
#include <stdint.h>

/* Catálogo público da ABI 1.4 (RFC 0029), gerado por tools/gen-host-keep.py a partir das declarações diretas de
 * soh/include/functions.h e variables.h. Ordem de strcmp. Variável dá o endereço do armazenamento. */
#define LINKSPAN_PUBLIC_FUNCTION 0u
#define LINKSPAN_PUBLIC_VARIABLE 1u

typedef struct LinkSpanPublicSymbol {
    const char* name;
    const void* address;
    uint8_t kind;
} LinkSpanPublicSymbol;

#ifdef __cplusplus
extern "C" {
#endif
extern const LinkSpanPublicSymbol LinkSpan_PublicSymbols[];
extern const size_t LinkSpan_PublicSymbolCount;
#ifdef __cplusplus
}

#include <shiplua/native/NativeProvider.h>
#include <memory>
#include <string>

namespace ShipLuaHost {
// Busca binária por nome exato; nullptr se não houver.
const LinkSpanPublicSymbol* FindOotPublicSymbol(const LinkSpanPublicSymbol* table, size_t count, const char* name);
// Catálogo da política: layout do host e busca na tabela dada (os testes passam uma tabela própria).
std::shared_ptr<ShipLua::NativePublicSymbols> CreateOotPublicSymbols(std::string layoutId,
                                                                    const LinkSpanPublicSymbol* table, size_t count);
// O do soh.exe: LINKSPAN_OOT_LAYOUT_ID e LinkSpan_PublicSymbols.
std::shared_ptr<ShipLua::NativePublicSymbols> CreateOotPublicSymbols();
} // namespace ShipLuaHost
#endif

#endif
