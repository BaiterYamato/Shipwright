// Gera soh.symbols a partir do soh.exe e do soh.pdb ao lado dele, para o escape hatch (RFC 0023).
//
// Formato (UTF-8, uma entrada por linha):
//   linkspan-symbols 1
//   sha256 <hex do executável>
//   <rva hex>\t<flags>\t<arquivo-fonte>\t<nome>
// flags: "-" ou "folded" quando outro nome ocupa o mesmo RVA (/OPT:ICF).
// Só entram funções com fonte em soh/src ou soh/soh; templates e std ficam de fora.
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

bool GameSource(const std::string& path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(c == '/' ? '\\' : std::tolower(c));
    });
    return lower.find("\\soh\\src\\") != std::string::npos || lower.find("\\soh\\soh\\") != std::string::npos;
}

BOOL CALLBACK Collect(PSYMBOL_INFO symbol, ULONG, PVOID user) {
    auto& context = *static_cast<Context*>(user);
    if (symbol->Tag != 5 /* SymTagFunction */ || symbol->NameLen == 0) {
        return TRUE;
    }
    const std::string name(symbol->Name, symbol->NameLen);
    if (name.find('<') != std::string::npos || name.rfind("std::", 0) == 0) {
        return TRUE;
    }
    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof(line);
    DWORD displacement = 0;
    if (!SymGetLineFromAddr64(context.process, symbol->Address, &displacement, &line) || !line.FileName ||
        !GameSource(line.FileName)) {
        return TRUE;
    }
    context.entries.push_back(
        {symbol->Address - context.base, std::filesystem::path(line.FileName).filename().string(), name});
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
    Context context{GetCurrentProcess(), 0, {}};
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
    std::printf("%zu funções, sha256 %s\n", context.entries.size(), fingerprint.c_str());
    return 0;
}
