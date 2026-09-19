"""Gera thunks MASM e a tabela C de imports do fork NEI para o host SoH.

Uso: python gen_imports.py --obj modulo.obj [--obj outro.obj] --symbols soh.symbols
       --out-asm imports.asm --out-c imports.c [--data-prefix nei_host_] [--report relatorio.txt]
"""
import argparse
import os
import struct
import sys


IMAGE_SYM_CLASS_EXTERNAL = 2
IMAGE_SYM_CLASS_WEAK_EXTERNAL = 105
# GUID {D1BAA1C7-BAEE-4BA9-AF20-FAF66AA4DCB8}, na ordem little-endian do COFF.
BIGOBJ_CLASS_ID = bytes.fromhex("C7A1BAD1EEBAA94BAF20FAF66AA4DCB8")


def fail(message):
    """Interrompe por uma entrada COFF ou soh.symbols malformada."""
    raise ValueError(message)


def read_c_string(data, offset, where):
    if offset < 4 or offset >= len(data):
        fail(f"{where}: offset de nome longo inválido: {offset}")
    end = data.find(b"\0", offset)
    if end < 0:
        fail(f"{where}: nome longo sem terminador")
    return data[offset:end].decode("utf-8", "replace")


def coff_symbols(path):
    """Devolve (definidos, pedidos) de um COFF x64 normal ou /bigobj."""
    with open(path, "rb") as file:
        data = file.read()
    if len(data) < 20:
        fail(f"{path}: cabeçalho COFF truncado")

    sig1, sig2 = struct.unpack_from("<HH", data)
    bigobj = (sig1, sig2) == (0, 0xFFFF)
    if bigobj:
        if len(data) < 56:
            fail(f"{path}: cabeçalho bigobj truncado")
        version, machine = struct.unpack_from("<HH", data, 4)
        class_id = data[12:28]
        if version < 2 or class_id != BIGOBJ_CLASS_ID:
            fail(f"{path}: assinatura bigobj inválida")
        symbol_offset, symbol_count = struct.unpack_from("<II", data, 48)
        symbol_size = 20
    else:
        machine, _, _, symbol_offset, symbol_count, _, _ = struct.unpack_from("<HHIIIHH", data)
        symbol_size = 18
    if machine != 0x8664:
        fail(f"{path}: não é COFF x64 (Machine 0x{machine:04x})")
    symbol_end = symbol_offset + symbol_count * symbol_size
    if symbol_offset == 0 or symbol_end + 4 > len(data):
        fail(f"{path}: tabela de símbolos truncada")
    string_size, = struct.unpack_from("<I", data, symbol_end)
    if string_size < 4 or symbol_end + string_size > len(data):
        fail(f"{path}: tabela de strings truncada")
    strings = data[symbol_end:symbol_end + string_size]

    defined, requested = set(), set()
    index = 0
    while index < symbol_count:
        offset = symbol_offset + index * symbol_size
        record = data[offset:offset + symbol_size]
        if len(record) != symbol_size:
            fail(f"{path}: registro de símbolo truncado")
        name_field = record[:8]
        if name_field[:4] == b"\0\0\0\0":
            string_offset, = struct.unpack_from("<I", name_field, 4)
            name = read_c_string(strings, string_offset, path)
        else:
            name = name_field.split(b"\0", 1)[0].decode("utf-8", "replace")
        value, = struct.unpack_from("<I", record, 8)
        if bigobj:
            section, = struct.unpack_from("<i", record, 12)
            storage_class, auxiliary = record[18], record[19]
        else:
            section, = struct.unpack_from("<h", record, 12)
            storage_class, auxiliary = record[16], record[17]
        if index + 1 + auxiliary > symbol_count:
            fail(f"{path}: símbolos auxiliares ultrapassam a tabela")
        if name:
            # Seção 0 com valor > 0 é COMMON: definição provisória do C (`T gX;` sem extern), o linker aloca.
            if storage_class == IMAGE_SYM_CLASS_EXTERNAL and (section > 0 or (section == 0 and value > 0)):
                defined.add(name)
            if ((storage_class == IMAGE_SYM_CLASS_EXTERNAL and section == 0 and value == 0)
                    or storage_class == IMAGE_SYM_CLASS_WEAK_EXTERNAL):
                requested.add(name)
        index += 1 + auxiliary
    return defined, requested


def read_symbols(path):
    """Lê soh.symbols e indexa todas as entradas pelo nome."""
    with open(path, encoding="utf-8", errors="strict") as file:
        lines = file.read().splitlines()
    if len(lines) < 2 or lines[0] != "linkspan-symbols 1":
        fail(f"{path}: cabeçalho linkspan-symbols 1 ausente")
    sha_parts = lines[1].split()
    if len(sha_parts) != 2 or sha_parts[0] != "sha256" or len(sha_parts[1]) != 64:
        fail(f"{path}: linha sha256 inválida")
    try:
        int(sha_parts[1], 16)
    except ValueError:
        fail(f"{path}: sha256 não hexadecimal")
    by_name = {}
    for number, line in enumerate(lines[2:], 3):
        if not line:
            continue
        parts = line.split("\t", 3)
        if len(parts) != 4:
            fail(f"{path}:{number}: entrada inválida")
        rva, status, source, name = parts
        try:
            int(rva, 16)
        except ValueError:
            fail(f"{path}:{number}: RVA inválida")
        if status not in ("-", "folded") or not source or not name:
            fail(f"{path}:{number}: status, arquivo ou nome inválido")
        by_name.setdefault(name, []).append((status, source))
    return sha_parts[1], by_name


def ignored_name(name):
    return (name.startswith("__imp_") or "@" in name or "?" in name or "$" in name
            or name.startswith(("__", "_RTC", "_guard", "_CRT", "__security")))


def c_string(value):
    """Literal C por bytes UTF-8, sem depender da página de código do compilador."""
    return '"' + "".join(f"\\x{byte:02x}" for byte in value.encode("utf-8")) + '"'


def write_asm(path, functions):
    lines = ["; Gerado por gen_imports.py. Não editar.", "EXTERN nei_host_functions:QWORD", "_TEXT SEGMENT"]
    for index, name in enumerate(functions):
        lines += [f"PUBLIC {name}", f"{name} PROC", f"    jmp QWORD PTR [nei_host_functions + {8 * index}]",
                  f"{name} ENDP"]
    lines += ["_TEXT ENDS", "END", ""]
    with open(path, "w", encoding="utf-8", newline="\n") as file:
        file.write("\n".join(lines))


def array_initializer(values):
    return ", ".join(values) if values else "NULL"


def write_c(path, sha256, functions, data_names, data_prefix):
    function_size = max(1, len(functions))
    data_size = max(1, len(data_names))
    lines = [
        f"// Gerado por gen_imports.py a partir de soh.symbols {sha256}. Não editar.",
        "#include <stddef.h>", "#include <stdint.h>",
        "typedef int (*NeiHostResolveFn)(void* context, const char* name, uintptr_t* address); /* 0 = ok */",
        f"void* nei_host_functions[{function_size}];",
        f"static const char* const kFunctionNames[{function_size}] = {{{array_initializer([c_string(name) for name in functions])}}};",
        f"static const size_t kFunctionCount = {len(functions)};",
    ]
    lines += [f"void* {data_prefix}{name};" for name in data_names]
    slots = [f"&{data_prefix}{name}" for name in data_names]
    lines += [
        f"static void** const kDataSlots[{data_size}] = {{{array_initializer(slots)}}};",
        f"static const char* const kDataNames[{data_size}] = {{{array_initializer([c_string(name) for name in data_names])}}};",
        f"static const size_t kDataCount = {len(data_names)};",
        f"const char nei_host_symbols_sha256[] = {c_string(sha256)};", "",
        "/* Resolve tudo; devolve 0 ou o índice+1 do primeiro nome que falhou, e *failed aponta o nome. */",
        "int nei_host_resolve(NeiHostResolveFn resolve, void* context, const char** failed) {",
        "    uintptr_t address;", "    size_t index;", "", "    if (failed != NULL) {", "        *failed = NULL;", "    }",
        "    for (index = 0; index < kFunctionCount; ++index) {",
        "        address = 0;",
        "        if (resolve(context, kFunctionNames[index], &address) != 0) {",
        "            if (failed != NULL) {", "                *failed = kFunctionNames[index];", "            }",
        "            return (int)(index + 1);", "        }",
        "        nei_host_functions[index] = (void*)address;", "    }",
        "    for (index = 0; index < kDataCount; ++index) {",
        "        address = 0;",
        "        if (resolve(context, kDataNames[index], &address) != 0) {",
        "            if (failed != NULL) {", "                *failed = kDataNames[index];", "            }",
        "            return (int)(kFunctionCount + index + 1);", "        }",
        "        *kDataSlots[index] = (void*)address;", "    }", "    return 0;", "}", "",
    ]
    with open(path, "w", encoding="utf-8", newline="\n") as file:
        file.write("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description="Gera imports NEI resolvidos por soh.symbols.")
    parser.add_argument("--obj", action="append", required=True, help="objeto COFF x64 da DLL (repetível)")
    parser.add_argument("--symbols", required=True, help="arquivo soh.symbols")
    parser.add_argument("--data-prefix", default="nei_host_", help="prefixo dos dados do host")
    parser.add_argument("--out-asm", required=True, help="saída MASM x64")
    parser.add_argument("--out-c", required=True, help="saída C")
    parser.add_argument("--report", help="relatório opcional")
    args = parser.parse_args()

    try:
        sha256, symbols = read_symbols(args.symbols)
        defined, requested = set(), set()
        for path in args.obj:
            one_defined, one_requested = coff_symbols(path)
            defined.update(one_defined)
            requested.update(one_requested)
    except (OSError, UnicodeError, ValueError) as exc:
        parser.error(str(exc))

    functions, data_names, ignored, errors = [], [], [], []
    for name in sorted(requested - defined):
        entries = symbols.get(name)
        if name.startswith(args.data_prefix):
            short_name = name[len(args.data_prefix):]
            entries = symbols.get(short_name)
            if (len(entries or []) == 1 and entries[0][0] != "folded" and entries[0][1] == "[data]"):
                data_names.append(short_name)
            else:
                errors.append((name, "dado ausente, repetido, folded ou não marcado [data]"))
            continue
        if ignored_name(name):
            ignored.append(name)
            continue
        function_entries = [entry for entry in (entries or []) if entry[1] != "[data]"]
        if not function_entries:
            ignored.append(name)
        elif len(entries) != 1:
            errors.append((name, "nome repetido em soh.symbols"))
        elif function_entries[0][0] == "folded":
            errors.append((name, "símbolo folded em soh.symbols"))
        else:
            functions.append(name)

    write_asm(args.out_asm, functions)
    write_c(args.out_c, sha256, functions, data_names, args.data_prefix)
    if args.report:
        report_lines = [f"funções: {len(functions)}", f"dados: {len(data_names)}", f"ignorados: {len(ignored)}",
                        f"erros: {len(errors)}", ""]
        report_lines += [f"ERRO {name}: {reason}" for name, reason in errors]
        report_lines += [f"IGNORADO {name}" for name in ignored]
        with open(args.report, "w", encoding="utf-8", newline="\n") as file:
            file.write("\n".join(report_lines) + "\n")
    print(f"funções: {len(functions)}; dados: {len(data_names)}; ignorados: {len(ignored)}; erros: {len(errors)}")
    for name, reason in errors:
        print(f"erro: {name}: {reason}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
