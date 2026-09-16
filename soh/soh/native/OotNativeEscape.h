#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <shiplua/native/NativeProvider.h>

namespace ShipLuaHost {

// Tabela de soh.symbols (formato em soh/native-sdk/tools/symdump.cpp).
class OotSymbolTable {
  public:
    // Falha com texto malformado ou versão desconhecida; `error` explica.
    bool Parse(std::string_view text, std::string& error);
    const std::string& Fingerprint() const { return mFingerprint; }
    size_t Size() const { return mEntries.size(); }
    // "Funcao" único ou "arquivo.c!Funcao". Ausente, ambíguo ou RVA compartilhado (ICF) = UNSUPPORTED.
    ShipNativeStatus Resolve(std::string_view name, uint64_t& rva) const;

  private:
    struct Entry {
        uint64_t rva = 0;
        bool folded = false;
        std::string file;
        std::string name;
    };
    std::string mFingerprint;
    std::vector<Entry> mEntries;
    std::multimap<std::string, size_t, std::less<>> mByName;
};

// Implementação de plataforma, registrada pelo binding do jogo (Windows). Sem ela,
// a política não publica escape hatch.
using OotEscapeHatchFactory = std::shared_ptr<ShipLua::NativeEscapeHatch> (*)();
void SetOotEscapeHatchFactory(OotEscapeHatchFactory factory);
std::shared_ptr<ShipLua::NativeEscapeHatch> CreateOotEscapeHatch();

} // namespace ShipLuaHost
