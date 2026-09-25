// Gera soh.symbols a partir do soh.exe e do soh.pdb ao lado dele, para o escape hatch (RFC 0023).
//
// Formato (UTF-8, uma entrada por linha):
//   linkspan-symbols 1
//   sha256 <hex do executável>
//   <rva hex>\t<flags>\t<arquivo-fonte>\t<nome>
// flags: "-" ou "folded" quando outro nome ocupa o mesmo RVA (/OPT:ICF).
// Entram funções com fonte em soh/src, soh/soh ou libultraship/src e as variáveis globais e estáticas do jogo
// e do libultraship. O arquivo de uma variável é "[data]" no build com /GL, que não guarda a compiland; ela
// resolve pelo nome, quando único. Fora do LTCG (soh/hookable_sources.txt) é "[data]<arquivo>", e o static
// repetido resolve por "[data]<arquivo>!nome". Templates e std ficam de fora.
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <dbghelp.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

struct Entry {
    DWORD64 rva;
    std::string file;
    std::string name;
};

struct Context {
    HANDLE process;
    DWORD64 base;
    std::vector<Entry> entries;
    // compiland (minúsculas) -> arquivo-fonte, das funções do jogo
    std::map<std::string, std::string> dataSources;
};

std::string Sha256(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return {};
    }
    std::string result;
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        std::vector<char> buffer(1 << 20);
        bool ok = true;
        while (ok && input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto count = input.gcount();
            if (count > 0) {
                ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(count), 0) == 0;
            }
        }
        unsigned char digest[32];
        if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
            static constexpr char hex[] = "0123456789abcdef";
            for (const auto byte : digest) {
                result.push_back(hex[byte >> 4]);
                result.push_back(hex[byte & 0xF]);
            }
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
}

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(c == '/' ? '\\' : std::tolower(c));
    });
    return text;
}

bool GameSource(const std::string& path) {
    const std::string lower = Lower(path);
    return lower.find("\\soh\\src\\") != std::string::npos || lower.find("\\soh\\soh\\") != std::string::npos ||
           lower.find("\\libultraship\\src\\") != std::string::npos;
}

bool PlainName(const std::string& name) {
    return !name.empty() && name.find('<') == std::string::npos && name.find('`') == std::string::npos &&
           name.find('?') == std::string::npos && name.rfind("std::", 0) != 0;
}

// Nome da compiland (o .obj) que define o símbolo; é o pai léxico de variáveis estáticas e globais.
std::string Compiland(HANDLE process, DWORD64 base, ULONG index) {
    DWORD parent = 0;
    WCHAR* name = nullptr;
    if (!SymGetTypeInfo(process, base, index, TI_GET_LEXICALPARENT, &parent) ||
        !SymGetTypeInfo(process, base, parent, TI_GET_SYMNAME, &name) || !name) {
        return {};
    }
    const std::wstring wide(name);
    LocalFree(name);
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string text(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), text.data(), size, nullptr,
                        nullptr);
    return text;
}

BOOL CALLBACK Collect(PSYMBOL_INFO symbol, ULONG, PVOID user) {
    auto& context = *static_cast<Context*>(user);
    if ((symbol->Tag != 5 /* SymTagFunction */ && symbol->Tag != 7 /* SymTagData */) || symbol->NameLen == 0) {
        return TRUE;
    }
    const std::string name(symbol->Name, symbol->NameLen);
    if (!PlainName(name)) {
        return TRUE;
    }
    if (symbol->Tag == 7) {
        // Só variáveis com endereço fixo no módulo; locais e parâmetros têm registrador ou frame.
        if (symbol->Flags & (SYMFLAG_REGREL | SYMFLAG_REGISTER | SYMFLAG_FRAMEREL | SYMFLAG_PARAMETER |
                             SYMFLAG_LOCAL | SYMFLAG_CONSTANT | SYMFLAG_TLSREL)) {
            return TRUE;
        }
        // Com /GL (LTCG) a compiland de uma variável do jogo e do libultraship é "* CIL *" (estática) ou
        // "* Linker *" (global), sem arquivo; bibliotecas de fora (SDL, glew) compilam sem /GL e ficam com o
        // .obj delas. Sem LTCG, o .obj da variável diz o arquivo pelas funções dele; a coluna fica
        // "[data]<arquivo>" para continuar separando dado de função, e "[data]<arquivo>!nome" resolve um
        // static repetido (sCylinderInit) dos fontes compilados fora do LTCG (soh/hookable_sources.txt).
        const std::string compiland = Compiland(context.process, context.base, symbol->Index);
        const auto source = context.dataSources.find(Lower(compiland));
        if (compiland == "* CIL *" || compiland == "* Linker *") {
            context.entries.push_back({symbol->Address - context.base, "[data]", name});
        } else if (source != context.dataSources.end()) {
            context.entries.push_back({symbol->Address - context.base, "[data]" + source->second, name});
        }
        return TRUE;
    }
    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof(line);
    DWORD displacement = 0;
    if (!SymGetLineFromAddr64(context.process, symbol->Address, &displacement, &line) || !line.FileName ||
        !GameSource(line.FileName)) {
        return TRUE;
    }
    const std::string file = std::filesystem::path(line.FileName).filename().string();
    context.entries.push_back({symbol->Address - context.base, file, name});
    // A compiland de uma função do jogo vale para as variáveis dela, que não têm linha de fonte. Funções
    // inline de header também apontam para a compiland; o nome certo é o do .c/.cpp.
    const std::string extension = Lower(std::filesystem::path(file).extension().string());
    const std::string compiland = Compiland(context.process, context.base, symbol->Index);
    if (!compiland.empty() && (extension == ".c" || extension == ".cpp")) {
        context.dataSources.emplace(Lower(compiland), file);
    }
    return TRUE;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "uso: linkspan_symdump <soh.exe> <saida.symbols>\n");
        return 2;
    }
    const std::filesystem::path exe = std::filesystem::absolute(argv[1]);
    const std::string fingerprint = Sha256(exe);
    if (fingerprint.empty()) {
        std::fprintf(stderr, "não foi possível ler %s\n", exe.string().c_str());
        return 1;
    }
    Context context{GetCurrentProcess(), 0, {}, {}};
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_EXACT_SYMBOLS | SYMOPT_FAIL_CRITICAL_ERRORS);
    if (!SymInitialize(context.process, exe.parent_path().string().c_str(), FALSE)) {
        std::fprintf(stderr, "SymInitialize falhou: %lu\n", GetLastError());
        return 1;
    }
    context.base = SymLoadModuleEx(context.process, nullptr, exe.string().c_str(), nullptr, 0x10000000, 0, nullptr, 0);
    IMAGEHLP_MODULE64 module{};
    module.SizeOfStruct = sizeof(module);
    if (!context.base || !SymGetModuleInfo64(context.process, context.base, &module) || module.SymType != SymPdb) {
        std::fprintf(stderr, "PDB correspondente a %s não encontrado\n", exe.string().c_str());
        SymCleanup(context.process);
        return 1;
    }
    // Duas passadas: a primeira aprende as compilands do jogo pelas funções; a segunda pega as variáveis delas.
    SymEnumSymbols(context.process, context.base, "*", Collect, &context);
    context.entries.clear();
    SymEnumSymbols(context.process, context.base, "*", Collect, &context);
    SymCleanup(context.process);

    std::sort(context.entries.begin(), context.entries.end(), [](const Entry& a, const Entry& b) {
        return a.rva != b.rva ? a.rva < b.rva : (a.file != b.file ? a.file < b.file : a.name < b.name);
    });
    context.entries.erase(std::unique(context.entries.begin(), context.entries.end(),
                                      [](const Entry& a, const Entry& b) {
                                          return a.rva == b.rva && a.file == b.file && a.name == b.name;
                                      }),
                          context.entries.end());
    std::map<DWORD64, int> perRva;
    for (const auto& entry : context.entries) {
        ++perRva[entry.rva];
    }

    const std::filesystem::path output = argv[2];
    const auto temporary = output.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << "linkspan-symbols 1\n" << "sha256 " << fingerprint << "\n";
        char rva[32];
        for (const auto& entry : context.entries) {
            std::snprintf(rva, sizeof(rva), "%llx", static_cast<unsigned long long>(entry.rva));
            out << rva << '\t' << (perRva[entry.rva] > 1 ? "folded" : "-") << '\t' << entry.file << '\t'
                << entry.name << '\n';
        }
        if (!out) {
            std::fprintf(stderr, "falha ao gravar %s\n", temporary.c_str());
            return 1;
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, output, error);
    if (error) {
        std::fprintf(stderr, "falha ao renomear para %s: %s\n", output.string().c_str(), error.message().c_str());
        return 1;
    }
    std::printf("%zu símbolos, sha256 %s\n", context.entries.size(), fingerprint.c_str());
    return 0;
}
