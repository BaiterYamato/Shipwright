"""Gera os arquivos do host que dependem da API do jogo (soh/include/functions.h e variables.h).

soh/soh/native/OotNativeHostKeep.c: retenção privada. Toma o endereço de cada função de functions.h com
definição no host, mais as de tools/host-keep-extra.txt, para o LTCG não apagar o corpo e o nome que o escape
hatch (soh.symbols) resolve. tools/host-keep-exclude.txt lista protótipos sem definição no link.

soh/soh/native/OotNativePublicSymbols.c: catálogo público da ABI 1.4 (RFC 0029). Nome -> endereço das funções e
variáveis declaradas diretamente em functions.h e variables.h, ativas num build C, com definição linkável no
host. host-keep-extra.txt nunca entra; tools/public-symbols-exclude.txt tira o que o linker recusa. Ordem de
strcmp, para busca binária.

tools/public-symbols-report.txt: toda declaração que ficou fora do catálogo, com o motivo.

Uso, na raiz do host:
  python tools/gen-host-keep.py           grava os três arquivos
  python tools/gen-host-keep.py --check   só compara; sai com 1 se algum estiver desatualizado
"""
import argparse
import os
import re
import sys

FUNCTION = "function"
VARIABLE = "variable"
KEEP = ("soh", "soh", "native", "OotNativeHostKeep.c")
CATALOG = ("soh", "soh", "native", "OotNativePublicSymbols.c")
REPORT = ("tools", "public-symbols-report.txt")


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def strip_comments(text):
    # Uma passada só, na ordem do texto: um /* dentro de comentário de linha ou de string não abre bloco. O
    # comentário de várias linhas vira o mesmo número de quebras, para as diretivas seguirem em linha própria.
    def replace(m):
        s = m.group(0)
        if s[0] in "\"'":
            return s
        return "\n" * s.count("\n") if "\n" in s else " "
    return re.sub(r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'", replace, text, flags=re.S)


def names_from(path):
    if not os.path.exists(path):
        return []
    return [l.split("#")[0].strip() for l in read(path).splitlines() if l.split("#")[0].strip()]


def reasons_from(path):
    """Linhas 'Nome  # motivo' de tools/public-symbols-exclude.txt."""
    result = {}
    if os.path.exists(path):
        for line in read(path).splitlines():
            name, _, reason = line.partition("#")
            if name.strip():
                result[name.strip()] = reason.strip() or "excluído à mão"
    return result


def active_for_c(text):
    """Devolve (ativo, plataforma) para um build C do catálogo.

    No texto ativo, diretivas e blocos inativos viram linhas vazias. Ativos: a guarda do header e
    #ifndef __cplusplus. Inativo: #ifdef __cplusplus. Qualquer outro #if, #ifdef ou #ifndef é de plataforma: o
    conteúdo sai do ativo e volta no segundo texto, para o relatório.
    """
    active, platform, stack = [], [], []
    # Só a dupla inicial #ifndef X / #define X identifica a guarda do header.
    guard = re.match(r"\s*#\s*ifndef\s+(\w+)[ \t]*\n\s*#\s*define\s+\1\b", text)
    guard_open = False
    for line in text.splitlines():
        directive = re.match(r"\s*#\s*(\w+)\s*(.*)", line)
        if directive:
            word, rest = directive.group(1), directive.group(2).strip()
            if word in ("if", "ifdef", "ifndef"):
                if word == "ifndef" and guard and rest == guard.group(1) and not stack and not guard_open:
                    guard_open = True
                    stack.append("active")
                elif word == "ifndef" and rest == "__cplusplus":
                    stack.append("active")
                elif word == "ifdef" and rest == "__cplusplus":
                    stack.append("inactive")
                else:
                    stack.append("platform")
            elif word in ("elif", "else") and stack:
                stack[-1] = {"active": "inactive", "inactive": "active"}.get(stack[-1], "platform")
            elif word == "endif" and stack:
                stack.pop()
            active.append("")
            continue
        if "platform" in stack:
            platform.append(line)
            active.append("")
        elif "inactive" in stack:
            active.append("")
        else:
            active.append(line)
    return "\n".join(active), "\n".join(platform)


def declarations(text):
    """(nome, tipo) de cada declaração de função ou variável, na ordem do texto, sem repetir nome."""
    text = re.sub(r'extern\s+"C"\s*\{?', " ", text).replace("{", " ").replace("}", " ")
    found, seen = [], set()
    for raw in text.split(";"):
        statement = " ".join(raw.split())
        if not statement or statement.startswith("typedef"):
            continue
        is_extern = statement.startswith("extern ")
        body = statement[len("extern "):] if is_extern else statement
        body = re.sub(r"BAD_RETURN\((\w+)\)", r"\1", body)
        paren = body.find("(")
        pointer = paren >= 0 and body[paren + 1:].lstrip().startswith("*")
        if is_extern and pointer:
            match = re.match(r"\(\s*\*\s*(\w+)", body[paren:])
            kind = VARIABLE
        elif is_extern and not body.endswith(")"):
            match = re.search(r"(\w+)\s*$", re.sub(r"(\s*\[[^\]]*\])+$", "", body))
            kind = VARIABLE
        elif paren >= 0 and not pointer and body.endswith(")"):
            match = re.search(r"(\w+)\s*$", body[:paren])
            kind = FUNCTION
        else:
            continue
        if match and match.group(1) not in seen:
            seen.add(match.group(1))
            found.append((match.group(1), kind))
    return found


def source_files(sources):
    for base in sources:
        for folder, _, files in os.walk(base):
            for file in sorted(files):
                # .inc: dados incluídos por um .c compilado (ex.: z_camera_data.inc). O linker é a prova final.
                if file.endswith((".c", ".cpp", ".inc")):
                    yield os.path.join(folder, file)


def defined_functions(sources):
    """Nomes com definição de função não static nos fontes do jogo (qualquer assinatura)."""
    pattern = re.compile(r"^(?:extern\s+\"C\"\s+)?(BAD_RETURN\(\w+\)\s+|[A-Za-z_][\w \*]*?)\b(\w+)\s*\(([^;{}]*)\)\s*\{",
                         re.M)
    names = set()
    for path in source_files(sources):
        for m in pattern.finditer(strip_comments(read(path))):
            if not m.group(1).lstrip().startswith(("static", "else", "return")):
                names.add(m.group(2))
    return names


def at_file_scope(text, position):
    """True se `position` está fora de toda chave, salvo as de `extern "C" {` (definição global em C++)."""
    stack = []
    for m in re.finditer(r'extern\s+"C"\s*\{|[{}]', text[:position]):
        if m.group(0) == "}":
            if stack:
                stack.pop()
        else:
            stack.append(m.group(0) != "{")
    return all(stack)


def defined_variables(sources, wanted):
    """Nomes de `wanted` com definição de armazenamento não static nos fontes. O linker é a prova final."""
    found, pending = set(), set(wanted)
    for path in source_files(sources):
        if not pending:
            break
        text = None
        for name in sorted(pending):
            raw = text if text is not None else read(path)
            if name not in raw:
                continue
            if text is None:
                text = strip_comments(raw)
            pattern = (r"^[ \t]*(?!static\b|extern\b|typedef\b|return\b)[A-Za-z_][^;{}()=\n]*?(?:\(\s*\*\s*)?\b"
                       + re.escape(name) + r"\s*(?:\[[^\]\n]*\]\s*)*(?:\)\s*\([^;{}]*\)\s*)?(?:=|;)")
            # Variável local com o mesmo nome não é armazenamento global (daria LNK2001).
            if any(at_file_scope(text, m.start()) for m in re.finditer(pattern, text, re.M)):
                found.add(name)
        pending -= found
    return found


def definition_prototype(sources, name):
    """Protótipo copiado da definição não static de name nos fontes do jogo."""
    pattern = re.compile(r"^(?:extern\s+\"C\"\s+)?((?:BAD_RETURN\(\w+\)\s+|[A-Za-z_][\w \*]*?))\b" + name
                         + r"\s*\(([^)]*)\)\s*\{", re.M)
    for path in source_files(sources):
        text = read(path)
        if name not in text:
            continue
        m = pattern.search(strip_comments(text))
        if m and not m.group(1).lstrip().startswith("static"):
            return f"{m.group(1).strip()} {name}({m.group(2).strip() or 'void'});"
    return None


def build(root):
    include = os.path.join(root, "soh", "include")
    tools = os.path.join(root, "tools")
    sources = [os.path.join(root, "soh", "src"), os.path.join(root, "soh", "soh")]
    declared, platform, names = [], [], set()
    for header in ("functions.h", "variables.h"):
        active, conditional = active_for_c(strip_comments(read(os.path.join(include, header))))
        for name, kind in declarations(active):
            if name not in names:
                names.add(name)
                declared.append((name, kind, header))
        platform += [(name, kind, header) for name, kind in declarations(conditional)]
    defined = defined_functions(sources)
    variables = defined_variables(sources, {name for name, kind, _ in declared if kind == VARIABLE})
    keep_exclude = set(names_from(os.path.join(tools, "host-keep-exclude.txt")))
    public_exclude = reasons_from(os.path.join(tools, "public-symbols-exclude.txt"))

    keep = [name for name, kind, header in declared
            if header == "functions.h" and kind == FUNCTION and name in defined and name not in keep_exclude]
    extras, prototypes = [], []
    for name in names_from(os.path.join(tools, "host-keep-extra.txt")):
        if name.startswith("include "):
            prototypes.append(f'#include "{name[len("include "):]}"')
            continue
        if name in keep or name in keep_exclude:
            continue
        prototype = definition_prototype(sources, name)
        if not prototype:
            raise SystemExit(f"sem definição não static para {name}")
        extras.append(name)
        prototypes.append(prototype)

    public, report = [], []
    for name, kind, header in declared:
        if name in public_exclude:
            reason = public_exclude[name]
        elif kind == FUNCTION and name in keep_exclude:
            reason = "fora do link do host (tools/host-keep-exclude.txt)"
        elif kind == FUNCTION and name not in defined:
            reason = "sem definição não static em soh/src ou soh/soh"
        elif kind == VARIABLE and name not in variables:
            reason = "sem definição de armazenamento em soh/src ou soh/soh"
        else:
            public.append((name, kind))
            continue
        report.append((name, kind, header, reason))
    for name, kind, header in platform:
        if name not in names:
            report.append((name, kind, header, "declaração condicional de plataforma"))
    public.sort(key=lambda item: item[0].encode())
    report.sort(key=lambda item: item[0].encode())
    return keep, extras, prototypes, public, report, len(declared)


def render_keep(keep, extras, prototypes):
    names = keep + extras
    return "\n".join([
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
    ])


def render_public(public):
    kinds = {FUNCTION: "LINKSPAN_PUBLIC_FUNCTION", VARIABLE: "LINKSPAN_PUBLIC_VARIABLE"}
    return "\n".join([
        "// Gerado por tools/gen-host-keep.py. Não editar à mão.",
        "// Catálogo público da ABI 1.4 (RFC 0029): funções e variáveis declaradas diretamente em",
        "// soh/include/functions.h e variables.h, com definição no host, em ordem de strcmp. Variável dá o endereço",
        "// do armazenamento. O que ficou fora está em tools/public-symbols-report.txt.",
        '#include "global.h"',
        '#include "OotNativePublicSymbols.h"',
        "",
        '#pragma comment(linker, "/include:LinkSpan_PublicSymbols")',
        '#pragma comment(linker, "/include:LinkSpan_PublicSymbolCount")',
        "const LinkSpanPublicSymbol LinkSpan_PublicSymbols[] = {",
        *[f'    {{ "{name}", (const void*)&{name}, {kinds[kind]} }},' for name, kind in public],
        "};",
        "const size_t LinkSpan_PublicSymbolCount = sizeof(LinkSpan_PublicSymbols) / sizeof(LinkSpan_PublicSymbols[0]);",
        "",
    ])


def render_report(report, declared, public):
    functions = sum(1 for _, kind in public if kind == FUNCTION)
    return "\n".join([
        "# Gerado por tools/gen-host-keep.py. Não editar à mão.",
        "# Declarações de soh/include/functions.h e variables.h fora do catálogo público (ABI 1.4), com o motivo.",
        f"# {declared} declaradas; no catálogo, {functions} funções e {len(public) - functions} variáveis; "
        f"{len(report)} fora.",
        "# nome\ttipo\theader\tmotivo",
        *[f"{name}\t{kind}\t{header}\t{reason}" for name, kind, header, reason in report],
        "",
    ])


def main(argv=None):
    parser = argparse.ArgumentParser(description="Gera a retenção privada e o catálogo público do host.")
    parser.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    parser.add_argument("--check", action="store_true", help="só compara; 1 se algum arquivo estiver desatualizado")
    parser.add_argument("--output-dir", help="grava os três arquivos nesta pasta, com o caminho relativo à raiz")
    args = parser.parse_args(argv)
    keep, extras, prototypes, public, report, declared = build(args.root)
    outputs = {
        KEEP: render_keep(keep, extras, prototypes),
        CATALOG: render_public(public),
        REPORT: render_report(report, declared, public),
    }
    stale = []
    for relative, content in outputs.items():
        current_path = os.path.join(args.root, *relative)
        current = read(current_path).replace("\r\n", "\n") if os.path.exists(current_path) else None
        if current != content:
            stale.append("/".join(relative))
        if args.check:
            continue
        target = os.path.join(args.output_dir or args.root, *relative)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "w", encoding="utf-8", newline="\n") as f:
            f.write(content)
    functions = sum(1 for _, kind in public if kind == FUNCTION)
    print(f"retenção: {len(keep)} de functions.h + {len(extras)} extras; catálogo: {functions} funções e "
          f"{len(public) - functions} variáveis de {declared} declarações; {len(report)} fora")
    if args.check and stale:
        print("desatualizado: " + ", ".join(stale) + " (rode python tools/gen-host-keep.py)", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
