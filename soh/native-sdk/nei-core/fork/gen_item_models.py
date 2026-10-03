"""Tabela do modelo de get-item de cada item do fork NEI (NEI-007/008), para o registro linkspan.nei.items.

No fork, o modelo que gira na mão do Link ao receber um item é desenhado pela função da coluna drawFunc da linha
do item em sNeiItems[] (extended_player.c), que mora no draw.cpp do randomizer do host e quase sempre é
`DrawCustomItemDiamond(play, <display list>, <escala>)`. O registro do Link-Span desenha uma display list do VFS
com escala e camada, então este script lê as duas coisas do commit congelado e resolve o símbolo da display list
para o caminho do recurso:

1. `#define d<símbolo> "__OTR__..."` no soh_assets.h (ou em qualquer header do fork);
2. `Gfx <símbolo>[] = { gsSPDisplayList(<outro>), gsSPEndDisplayList() }` no draw.cpp: segue o <outro>;
3. um recurso dos assets do fork com esse nome de arquivo (o modelo em C tem o mesmo nome no .o2r).

Saída: um .c com `gNeiItemModels[]`, compilado na DLL. Item sem função de desenho ou sem caminho resolvido fica
fora da tabela e usa o modelo provisório; o relatório diz quais.

Uso: python gen_item_models.py <repo> <commit> <out.c> <relatorio.txt>
"""
import re
import subprocess
import sys


# Itens cujo modelo o fork não liga pela coluna drawFunc. Caminho do recurso, escala e camada escolhidos no port.
OVERRIDES = {
    # O arquivo NEI atual inclui a pena em object_nei_rocs_feather; o caminho antigo do RG não existe nele.
    "ITEM_ROCS_FEATHER_SKIJER": ("objects/object_nei_rocs_feather/rocs_feather_dl", "1.0", "TRANSLUCENT",
                                 "Randomizer_DrawRocsFeather"),
    # drawFunc NULL na linha, mas o fork tem Randomizer_DrawRocsCape (gNeiRocsCapeDL x0.6).
    "ITEM_ROCS_CAPE": ("objects/object_nei_rocs_cape/rocs_cape_mesh_dl", "0.6", "OPAQUE", "Randomizer_DrawRocsCape"),
    # No fork os dois são o cubo verde provisório (DEFINE_GREEN_CUBE_ITEM(Dominionrod)); o modelo de verdade
    # existe nos assets. Escala na ordem da dos bastões (fire rod 0.2).
    "ITEM_DOMINION_ROD": ("objects/object_nei_dominion_rod/gNeiDominionRodDL", "0.2", "OPAQUE", "cubo verde no fork"),
    "ITEM_ELEMENTAL_WAND": ("objects/object_nei_wand_sand_rod/gNeiSandRodDL", "0.2", "OPAQUE", "cubo verde no fork"),
}


def git(repo, *args):
    return subprocess.run(["git", "-C", repo, *args], capture_output=True, check=True).stdout.decode(
        "utf-8", errors="replace")


def split_top(text):
    """Separa por vírgula de nível zero, respeitando strings e parênteses."""
    parts, depth, cur, quote, esc = [], 0, [], False, False
    for ch in text:
        if quote:
            cur.append(ch)
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                quote = False
            continue
        if ch == '"':
            quote = True
        elif ch in "({[":
            depth += 1
        elif ch in ")}]":
            depth -= 1
        elif ch == "," and depth == 0:
            parts.append("".join(cur).strip())
            cur = []
            continue
        cur.append(ch)
    if "".join(cur).strip():
        parts.append("".join(cur).strip())
    return parts


def rows_of(source):
    start = source.index("static const NeiItem sNeiItems[] = {")
    body = source[start + len("static const NeiItem sNeiItems[] = {"):source.index("\n};", start)]
    body = re.sub(r"//[^\n]*", "", body)
    rows, depth, cur, quote, esc = [], 0, [], False, False
    for ch in body:
        if quote:
            cur.append(ch)
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                quote = False
            continue
        if ch == '"':
            quote = True
        if ch == "{":
            depth += 1
            if depth == 1:
                cur = []
                continue
        elif ch == "}":
            depth -= 1
            if depth == 0:
                rows.append(split_top("".join(cur)))
                continue
        if depth >= 1:
            cur.append(ch)
    return rows


def main():
    repo, commit, out, report = sys.argv[1:5]
    player = git(repo, "show", f"{commit}:soh/mods/extended_player.c")
    draw = git(repo, "show", f"{commit}:soh/soh/Enhancements/randomizer/draw.cpp")
    defines = {}
    grep = git(repo, "grep", "-h", "-E", r'#define\s+d\w+\s+"__OTR__', commit, "--", "soh/assets", "soh/mods")
    for m in re.finditer(r'#define\s+d(\w+)\s+"__OTR__([^"]+)"', grep):
        defines.setdefault(m.group(1), m.group(2))
    wrappers = {m.group(1): m.group(2) for m in re.finditer(
        r"Gfx\s+(\w+)\[\]\s*=\s*\{\s*gsSPDisplayList\(\(?(?:Gfx\*\))?\s*(\w+)\)\s*,\s*gsSPEndDisplayList\(\)", draw)}
    # Apelidos dos modelos em C (items/objects/*/header.h): #define g_fire_rod_give_dl Cylinder_001_opaque_dl.
    aliases = git(repo, "grep", "-h", "-E", r"^#define\s+\w+\s+\w+\s*$", commit, "--", "soh/mods/items/objects")
    for m in re.finditer(r"#define\s+(\w+)\s+(\w+)", aliases):
        wrappers.setdefault(m.group(1), m.group(2))
    assets = {}
    for path in git(repo, "diff", "--name-only", "-z", "--diff-filter=A", "783139310", commit, "--",
                    "soh/assets/custom/objects").split(chr(0)):
        if "/" not in path:
            continue
        assets.setdefault(path.rsplit("/", 1)[1], path[len("soh/assets/custom/"):])

    def resolve(symbol, seen=()):
        if symbol in defines:
            return defines[symbol]
        if symbol in wrappers and symbol not in seen:
            return resolve(wrappers[symbol], seen + (symbol,))
        return assets.get(symbol)

    entries, missing = [], []
    for row in rows_of(player):
        if len(row) < 9 or row[0] == "NEI_NO_ITEM":
            continue
        item, draw_fn = row[0], row[8]
        if item in OVERRIDES:
            path, scale, layer, why = OVERRIDES[item]
            entries.append((item, path, scale, layer, "port: " + why))
            continue
        if draw_fn in ("NULL", "0"):
            missing.append(f"{item}: sem função de desenho")
            continue
        m = re.search(r"void\s+%s\s*\([^)]*\)\s*\{(.*?)\n\}" % re.escape(draw_fn), draw, re.S)
        if not m:
            missing.append(f"{item}: {draw_fn} não está no draw.cpp")
            continue
        body = m.group(1)
        call = re.search(r"DrawCustomItemDiamond(?:Tint)?\(\s*play\s*,\s*(?:\(Gfx\*\)\s*)?(\w+)\s*,(.*?)\);", body, re.S)
        layer, scale, symbol = "OPAQUE", None, None
        if call:
            symbol = call.group(1)
            floats = re.findall(r"(\d+\.\d+)f", call.group(2))
            scale = floats[-1] if floats else None
        else:
            dl = re.search(r"gSPDisplayList\(POLY_(OPA|XLU)_DISP\+\+,\s*(?:\(Gfx\*\)\s*)?(\w+)\)", body)
            sc = re.search(r"Matrix_Scale\(\s*(\d+\.\d+)f", body)
            if dl:
                layer = "OPAQUE" if dl.group(1) == "OPA" else "TRANSLUCENT"
                symbol = dl.group(2)
                scale = sc.group(1) if sc else "1.0"
        path = resolve(symbol) if symbol else None
        if not path or not scale:
            missing.append(f"{item}: {draw_fn} -> {symbol or '?'} sem caminho resolvido")
            continue
        entries.append((item, path, scale, layer, draw_fn))

    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("/* Gerado por gen_item_models.py a partir do fork NEI. Não editar. */\n")
        f.write('#include "fork_models.h"\n\n')
        f.write("const NeiItemModel gNeiItemModels[] = {\n")
        for item, path, scale, layer, draw_fn in entries:
            f.write(f'    {{ {item}, "{path}", {scale}f, NEI_MODEL_LAYER_{layer} }}, /* {draw_fn} */\n')
        f.write("};\n")
        f.write("const unsigned int gNeiItemModelCount = sizeof(gNeiItemModels) / sizeof(gNeiItemModels[0]);\n")
    with open(report, "w", encoding="utf-8") as f:
        f.write(f"modelos: {len(entries)}\n")
        f.write("".join(f"  {item} -> {path} x{scale} ({layer})\n" for item, path, scale, layer, _ in entries))
        f.write(f"sem modelo: {len(missing)}\n")
        f.write("".join(f"  {m}\n" for m in missing))
    print(f"gen_item_models: {len(entries)} modelos, {len(missing)} sem modelo")


main()
