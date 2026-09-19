"""Gera a camada de compatibilidade do fork NEI sobre os headers do host, sem mudar o layout do host.

- Enumeradores que só o fork tem viram #define com o valor calculado no header do fork (VB_ numa faixa livre).
- Declarações de topo inteiramente novas (protótipos, macros, strings de asset, tipos novos) são copiadas:
  as de soh/include e soh_assets.h para nei_compat.h (incluído por /FI); as dos outros headers para uma
  "sombra" no mesmo caminho relativo, que inclui o header do host e acrescenta o do fork.
- Declarações do host que o fork alterou (structs com campos novos) não entram: vão para o relatório, porque mudar
  o layout do host dentro da DLL quebraria a memória compartilhada.
Uso: python gen_compat.py <repo> <fork-commit> <base-commit> <saida nei_compat.h> <relatorio.txt>
"""
import os
import re
import subprocess
import sys

repo, fork, base, out, report_path = sys.argv[1:6]
shadow_root = os.path.dirname(os.path.abspath(out))
CPP_TOKENS = re.compile(r"\b(namespace|class|template|typename|std::|constexpr|using|public:|private:|operator)\b|::|&\s*\w+\s*[,)]")
FORCED = ("soh/include/", "soh/assets/soh_assets.h")


def git(*args):
    return subprocess.run(["git", "-C", repo, *args], capture_output=True, text=True, encoding="utf-8",
                          errors="replace").stdout


def strip_comments(text):
    # Uma passada só, na ordem do texto: um /* dentro de comentário de linha ou de string não abre bloco.
    def replace(m):
        s = m.group(0)
        return s if s[0] in "\"'" else "\n" * s.count("\n")
    return re.sub(r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'", replace, text, flags=re.S)


def enums(text):
    values = {}
    text = "\n".join(l for l in strip_comments(text).split("\n") if not l.strip().startswith("#"))
    text = re.sub(r"RANDO_ENUM_BEGIN\((\w+)\)", r"enum \1 {", text)
    text = re.sub(r"RANDO_ENUM_ITEM\((\w+)\s*,\s*([^)]+)\)", r"\1 = \2,", text)
    text = re.sub(r"RANDO_ENUM_ITEM\((\w+)\)", r"\1,", text)
    text = re.sub(r"RANDO_ENUM_END\((\w+)\)", "};", text)
    for body in re.findall(r"enum\s*(?:class\s+)?\w*\s*(?::\s*\w+\s*)?\{(.*?)\}", text, flags=re.S):
        current = -1
        for item in body.split(","):
            item = item.strip()
            if not item:
                continue
            m = re.match(r"^([A-Za-z_]\w*)\s*(?:=\s*(.+))?$", item, flags=re.S)
            if not m:
                current = None
                continue
            name, expr = m.group(1), m.group(2)
            if expr is not None:
                try:
                    expr = re.sub(r"(\d)[uUlL]+\b", r"\1", expr.strip())
                    current = int(eval(expr, {"__builtins__": {}}, dict(values)))
                except Exception:
                    current = None
            elif current is not None:
                current += 1
            if current is not None:
                values[name] = current
    return values


def added_line_numbers(path):
    numbers = set()
    for m in re.finditer(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", git("diff", "-U0", base, fork, "--", path), re.M):
        start, count = int(m.group(1)), int(m.group(2) or 1)
        numbers.update(range(start, start + count))
    return numbers


def drop_cplusplus_blocks(lines):
    """Troca por linha vazia o ramo __cplusplus de cada #if (mantém o ramo C) e remove extern "C"."""
    result, stack = [], []
    for line in lines:
        s = line.strip()
        if re.match(r"#\s*if(def\s+__cplusplus|\s+defined\s*\(?\s*__cplusplus)", s):
            stack.append("cpp")
            result.append("")
            continue
        if re.match(r"#\s*ifndef\s+__cplusplus", s):
            stack.append("c")
            result.append("")
            continue
        if re.match(r"#\s*if", s):
            stack.append("other")
            result.append("")
            continue
        if re.match(r"#\s*else", s) and stack:
            if stack[-1] in ("cpp", "c"):
                stack[-1] = "c" if stack[-1] == "cpp" else "cpp"
            result.append("")
            continue
        if re.match(r"#\s*endif", s) and stack:
            stack.pop()
            result.append("")
            continue
        if "cpp" in stack or re.match(r'extern\s+"C"\s*\{?\s*$', s):
            result.append("")
            continue
        result.append(line)
    return result


def statements(text):
    """Declarações de topo: (primeira linha, última linha, texto). Linhas de pré-processador são uma declaração."""
    lines = drop_cplusplus_blocks(strip_comments(text).split("\n"))
    out, depth, start, buf, head = [], 0, None, [], ""
    i = 0
    while i < len(lines):
        line = lines[i]
        number = i + 1
        if depth == 0 and not buf and line.strip().startswith("#"):
            first = number
            block = [line]
            while block[-1].rstrip().endswith("\\") and i + 1 < len(lines):
                i += 1
                block.append(lines[i])
            out.append((first, i + 1, "\n".join(block)))
            i += 1
            continue
        if not line.strip() and not buf:
            i += 1
            continue
        if start is None:
            start = number
        buf.append(line)
        done = False
        for ch in line:
            if ch == "{":
                if depth == 0:
                    head = "\n".join(buf)
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    before = head.split("{")[0]
                    is_function = ")" in before and "=" not in before and not re.search(r"\b(struct|enum|union)\b", before)
                    rest = line[line.rfind("}") + 1:].strip()
                    if is_function and not rest.startswith(";"):
                        done = True
            elif ch == ";" and depth == 0:
                done = True
        if done or (depth == 0 and buf and line.rstrip().endswith(";")):
            out.append((start, number, "\n".join(buf)))
            start, buf, head = None, [], ""
        i += 1
    return out


host_enums, fork_enums = {}, {}
compat, report, shadows = [], [], []
modified = [p for p in git("diff", "--name-only", "--diff-filter=M", base, fork, "--", "*.h", "*.hpp").split()
            if not p.startswith(("soh/mods/", "soh/expansions/"))]
for path in modified:
    fork_text = git("show", f"{fork}:{path}")
    fork_lines = fork_text.split("\n")
    try:
        with open(f"{repo}/{path}", encoding="utf-8", errors="replace") as f:
            host_text = f.read()
    except FileNotFoundError:
        host_text = git("show", f"{base}:{path}")
    host_enums.update(enums(host_text))
    fork_enums.update(enums(fork_text))
    added = added_line_numbers(path)
    block = []
    for first, last, text in statements(fork_text):
        span = set(range(first, last + 1))
        touched = span & added
        if not touched:
            continue
        stripped = text.strip()
        if stripped.startswith("#"):
            m = re.match(r"#\s*define\s+(\w+)", stripped)
            if m and span <= added:
                if re.search(r"^\s*#\s*define\s+" + m.group(1) + r"\b", host_text, flags=re.M):
                    report.append(f"{path}:{first} macro redefinida pelo fork, fica a do host: {stripped[:120]}")
                else:
                    block.append(stripped)
            continue
        if stripped.startswith(("DEFINE_HOOK", "RANDO_ENUM")):
            report.append(f"{path}:{first} macro de tabela, ignorada: {stripped.splitlines()[0][:100]}")
            continue
        if re.search(r"\benum\b", stripped.split("{")[0]) and "{" in stripped:
            m = re.search(r"\}\s*(\w+)\s*;\s*$", stripped)
            if m and span <= added and stripped.startswith("typedef"):
                block.append(f"typedef int {m.group(1)};")
            continue  # os enumeradores novos saem como #define
        nonblank = {n for n in span if fork_lines[n - 1].strip()}
        if not nonblank <= added:
            report.append(f"{path}:{first}-{last} alterada pelo fork (fica a do host):\n    "
                          + "\n    ".join(fork_lines[n - 1] for n in sorted(touched)))
            continue
        if CPP_TOKENS.search(stripped):
            report.append(f"{path}:{first} só C++, ignorada: {stripped.splitlines()[0][:100]}")
            continue
        proto = re.match(r"^[^=({]*?\b(\w+)\s*\([^{]*\)\s*;$", stripped, flags=re.S)
        if proto and re.search(r"\b" + proto.group(1) + r"\s*\(", strip_comments(host_text)):
            report.append(f"{path}:{first} assinatura diferente da do host, fica a do host: {stripped[:120]}")
            continue
        block.append(stripped)
    if not block:
        continue
    if path.startswith(FORCED):
        compat.append(f"\n// {path}")
        compat.extend(block)
        continue
    relative = path[len("soh/"):]
    shadow = os.path.join(shadow_root, relative)
    os.makedirs(os.path.dirname(shadow), exist_ok=True)
    with open(shadow, "w", encoding="utf-8", newline="\n") as f:
        f.write("// Sombra gerada por gen_compat.py: o header do host mais o que o fork NEI acrescentou.\n"
                f'#pragma once\n#include "{repo}/{path}"\n#ifdef __cplusplus\nextern "C" {{\n#endif\n'
                + "\n".join(block) + "\n#ifdef __cplusplus\n}\n#endif\n")
    shadows.append(relative)

# Nomes que os próprios headers do fork em soh/mods redefinem como macro ficam com a macro deles.
mods_macros = set()
for header in git("ls-tree", "-r", "--name-only", fork, "soh/mods").split():
    if header.endswith(".h"):
        mods_macros.update(re.findall(r"^\s*#\s*define\s+(\w+)", git("show", f"{fork}:{header}"), flags=re.M))
new = sorted((n, v) for n, v in fork_enums.items() if n not in host_enums and n not in mods_macros)
diff = sorted((n, host_enums[n], v) for n, v in fork_enums.items() if n in host_enums and host_enums[n] != v)
lines = ["// Gerado por gen_compat.py: acréscimos do fork NEI (skijer c29262b) aos headers do host.",
         "// Incluído por /FI nos fontes do fork. Nada aqui muda o layout do host.",
         "#pragma once", '#include "nei_host_data.h"', '#include "global.h"', '#include "align_asset_macro.h"', "",
         f"// {len(new)} enumeradores só do fork"]
# VB_ novos vão para uma faixa que o host não usa: o GameInteractor do host devolve o padrão para eles.
lines += [f"#define {name} {value + 0x40000 if name.startswith('VB_') else value:#x}" for name, value in new]
lines += ["", f"// {len(diff)} enumeradores com valor diferente no fork (vale o do host, exceto os _MAX):"]
lines += [f"//   {name}: host {h:#x}, fork {f:#x}" for name, h, f in diff]
for name, h, f in diff:
    if name.endswith("_MAX"):
        lines += [f"#undef {name}", f"#define {name} {f:#x}"]
lines += ["", "// Declarações novas do fork"] + compat
with open(out, "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(lines) + "\n")
with open(report_path, "w", encoding="utf-8", newline="\n") as f:
    f.write("\n\n".join(report) + "\n")
print(f"{len(shadows)} sombras; {len(modified)} headers, {len(new)} enumeradores novos, {len(diff)} diferentes, "
      f"{sum(1 for l in compat if not l.startswith(chr(10)))} declarações globais, {len(report)} no relatório")
