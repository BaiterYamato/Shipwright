"""Gera soh/soh/native/OotNativeHostKeep.c: uma tabela com o endereço de cada função da API do jogo.

Com LTCG, uma função inlinada em todos os usos perde o corpo e o nome no PDB, e o escape hatch (soh.symbols)
não a encontra. Tomar o endereço força um corpo fora de linha com nome. Entram todos os protótipos de
soh/include/functions.h mais os nomes de tools/host-keep-extra.txt (funções fora de functions.h que mods
nativos usam; o protótipo é copiado da definição no fonte). tools/host-keep-exclude.txt lista protótipos de
functions.h sem definição no host.

Uso: python tools/gen-host-keep.py   (na raiz do host)
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "soh", "soh", "native", "OotNativeHostKeep.c")
SOURCES = [os.path.join(ROOT, "soh", "src"), os.path.join(ROOT, "soh", "soh")]


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def strip_comments(text):
    # Uma passada só, na ordem do texto: um /* dentro de comentário de linha ou de string não abre bloco.
    def replace(m):
        s = m.group(0)
        return s if s[0] in "\"'" else " "
    return re.sub(r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'", replace, text, flags=re.S)


def names_from(path):
    if not os.path.exists(path):
        return []
    return [l.split("#")[0].strip() for l in read(path).splitlines() if l.split("#")[0].strip()]


def functions_h_names():
    text = strip_comments(read(os.path.join(ROOT, "soh", "include", "functions.h")))
    text = re.sub(r"^\s*#.*$", "", text, flags=re.M)
    names = []
    for m in re.finditer(r"([A-Za-z_][\w\s\*\(\)]*?)\b(\w+)\s*\(([^;{}]*)\)\s*;", text):
        if "typedef" in m.group(1) or "(*" in m.group(0).split(m.group(2))[0]:
            continue
        names.append(m.group(2))
    return list(dict.fromkeys(names))


def defined_names():
    """Nomes com definição não static nos fontes do jogo (qualquer assinatura)."""
    pattern = re.compile(r"^(?:extern\s+\"C\"\s+)?(BAD_RETURN\(\w+\)\s+|[A-Za-z_][\w \*]*?)\b(\w+)\s*\(([^;{}]*)\)\s*\{",
                         re.M)
    names = set()
    for base in SOURCES:
        for folder, _, files in os.walk(base):
            for file in files:
                if file.endswith((".c", ".cpp")):
                    for m in pattern.finditer(strip_comments(read(os.path.join(folder, file)))):
                        if not m.group(1).lstrip().startswith(("static", "else", "return")):
                            names.add(m.group(2))
    return names


def definition_prototype(name):
    """Protótipo copiado da definição não static de name nos fontes do jogo."""
    pattern = re.compile(r"^(?:extern\s+\"C\"\s+)?((?:BAD_RETURN\(\w+\)\s+|[A-Za-z_][\w \*]*?))\b" + name
                         + r"\s*\(([^)]*)\)\s*\{", re.M)
    for base in SOURCES:
        for folder, _, files in os.walk(base):
            for file in files:
                if not file.endswith((".c", ".cpp")):
                    continue
                path = os.path.join(folder, file)
                text = read(path)
                if name not in text:
                    continue
                m = pattern.search(strip_comments(text))
                if m and not m.group(1).lstrip().startswith("static"):
                    return f"{m.group(1).strip()} {name}({m.group(2).strip() or 'void'});"
    return None


def main():
    tools = os.path.join(ROOT, "tools")
    exclude = set(names_from(os.path.join(tools, "host-keep-exclude.txt")))
    defined = defined_names()
    declared = functions_h_names()
    base = [n for n in declared if n not in exclude and n in defined]
    undefined = [n for n in declared if n not in defined]
    extras, prototypes = [], []
    for name in names_from(os.path.join(tools, "host-keep-extra.txt")):
        if name.startswith("include "):
            prototypes.append(f'#include "{name[len("include "):]}"')
            continue
        if name in base or name in exclude:
            continue
        prototype = definition_prototype(name)
        if not prototype:
            sys.exit(f"sem definição não static para {name}")
        extras.append(name)
        prototypes.append(prototype)
    names = base + extras
    lines = [
        "// Gerado por tools/gen-host-keep.py. Não editar à mão.",
        "// Com LTCG, uma função inlinada em todos os usos perde o corpo e o nome no PDB; o escape hatch",
        "// (soh.symbols) só resolve o que tem corpo nomeado. Esta tabela toma o endereço de cada função da API",
        "// do jogo para que ela sobreviva com nome. O /include impede o linker de descartar a tabela.",
        '#include "global.h"',
        "",
        "// Funções fora de functions.h (tools/host-keep-extra.txt).",
        *prototypes,
        "",
        '#pragma comment(linker, "/include:LinkSpan_HostKeep")',
        f"const void* const LinkSpan_HostKeep[{len(names)}] = {{",
        *[f"    (const void*)&{n}," for n in names],
        "};",
        "",
    ]
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"{len(base)} de functions.h ({len(undefined)} sem definição), {len(extras)} extras, {len(exclude)} excluídas"
          f" -> {os.path.relpath(OUT, ROOT)}")


main()
