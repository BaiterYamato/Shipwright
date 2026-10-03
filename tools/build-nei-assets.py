"""Gera, no computador de quem joga, os arquivos de assets do Not Enough Items (NEI-007).

Os assets do fork NEI (skijer/Not-Enough-Items) não têm licença declarada, então não entram em nenhum pacote do
Link-Span. Este script tira do commit congelado do fork os arquivos que ele acrescentou em soh/assets/custom, separa
por componente (núcleo, formas e expansões, que são componentes opcionais do NEI-015), empacota cada componente
num .o2r com o soh-o2r-packer do host e escreve o manifesto com origem, licença, hash e associação de cada arquivo.

O caminho dos recursos continua o do fork (objects/object_nei_shovel/...), porque o código do fork e as display
lists binárias apontam para ele por nome e por hash. O que o prefixo mod/skijer.nei/ garantiria (não sobrescrever
nada do jogo) o validador prova direto: nenhum caminho de componente existe no oot.o2r nem no soh.o2r.

O validador também confere as referências internas: cada display list binária só pode apontar (por hash CRC64 do
caminho) para textura, vértice, matriz ou display list que exista nos componentes ou nos arquivos base, e cada
caminho "__OTR__..." que o código do fork compilado na DLL usa precisa existir em algum lugar.

Uso:
  python tools/build-nei-assets.py --repo . --packer build/x64/Release/soh-o2r-packer.exe \
      --base <oot.o2r> --base <soh.o2r> --fork-source build/nei-core-native/nei-fork/fork --out build/nei-assets

Precisa do commit do fork no git do repositório (git fetch https://github.com/skijer/Not-Enough-Items c29262b).
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import zipfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "soh", "native-sdk", "nei-core", "fork"))
import cmodels  # noqa: E402

FORK_COMMIT = "c29262b"
FORK_COMMIT_FULL = "c29262b76ead6f00786885ae4be68916cbee77f5"
FORK_BASE = "783139310"
PREFIX = cmodels.ASSET_PREFIX
VERSION = "0.3.6"
SPINNER_TEXTURE = "objects/object_nei_spinner/sSpinnerTex"
SPINNER_REPAIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "soh", "native-sdk",
                              "nei-core", "assets", "spinner-bronze.otex")

# Componente por prefixo de caminho (dentro de soh/assets/custom). O primeiro que casar vale. Tudo que não casar
# é do núcleo: modelos dos itens, ícones, nomes, animações do Link e o HUD da Cane.
COMPONENTS = [
    ("objects/forms/kafei/", "form.kafei"),
    ("objects/forms/gerudo/", "form.gerudo"),
    ("objects/forms/keaton/", "form.keaton"),
    ("objects/forms/rito/", "form.rito"),
    ("objects/forms/garo/", "form.garo"),
    ("objects/forms/", "form.other"),
    ("objects/object_jso/", "form.garo"),
    ("textures/pikachu/", "expansion.ssbb"),
    ("objects/object_nei_pokeball/", "expansion.ssbb"),
    ("textures/mario_hp/", "expansion.sm64"),
    ("objects/object_nei_mario_mask/", "expansion.sm64"),
    ("prop_hunt/", "network.harpoon"),
]

# Associação legível de cada arquivo a item, ator ou sistema, pelo caminho. Vai para o manifesto.
ASSOCIATIONS = [
    (r"^objects/object_nei_([a-z0-9_]+?)_?/", lambda m: "item:" + m.group(1)),
    (r"^textures/icon_item_custom/gItemIcon(\w+?)Tex", lambda m: "icone:" + m.group(1)),
    (r"^textures/item_name_custom/gItemName(\w+?)Tex", lambda m: "nome:" + m.group(1)),
    (r"^objects/forms/(\w+)/", lambda m: "forma:" + m.group(1)),
    (r"^misc/link_animetion/", lambda m: "animacao:link"),
    (r"^textures/trirod/", lambda m: "item:trirod"),
    (r"^textures/four_sword/", lambda m: "equipamento:four_sword"),
    (r"^textures/buttons/", lambda m: "hud:botoes"),
    (r"^textures/season", lambda m: "item:rod_of_seasons"),
    (r"^map_select/", lambda m: "ui:map_select"),
    (r"^objects/(object_\w+)/", lambda m: "ator:" + m.group(1)),
    (r"^objects/(gameplay_\w+)/", lambda m: "keep:" + m.group(1)),
    (r"^scenes/", lambda m: "cena:especial"),
    (r"^fonts/", lambda m: "fonte:nomes"),
]

LICENSE = {
    "status": "sem licença declarada",
    "redistributable": False,
    "note": "O fork não tem LICENSE nem créditos por arquivo (inventário NEI-001 §8). Uso local apenas.",
}

# Opcodes de 128 bits do gbi do libultraship que carregam o CRC64 de um caminho na segunda metade.
HASH_OPCODES = {0x20: "textura", 0x31: "display list", 0x32: "vertices", 0x36: "matriz", 0x42: "movemem"}
OTHER_WIDE = {0x33, 0x35}  # G_MARKER e G_BRANCH_Z_OTR: 128 bits, sem caminho
DL_TYPE = 0x4F444C54  # 'ODLT'
END_OPCODES = {0xDF, 0xB8}  # G_ENDDL do F3DEX2 e do F3DEX


def crc64_table():
    poly = 0x42F0E1EBA9EA3693
    table = []
    for i in range(256):
        crc = i << 56
        for _ in range(8):
            crc = ((crc << 1) ^ poly) if crc & (1 << 63) else (crc << 1)
            crc &= 0xFFFFFFFFFFFFFFFF
        table.append(crc)
    return table


TABLE = crc64_table()


def crc64(text):
    crc = 0xFFFFFFFFFFFFFFFF
    for byte in text.encode("utf-8"):
        crc = TABLE[((crc >> 56) ^ byte) & 0xFF] ^ ((crc << 8) & 0xFFFFFFFFFFFFFFFF)
    return crc


def git(repo, *args, binary=False):
    result = subprocess.run(["git", "-C", repo, *args], capture_output=True, check=True)
    return result.stdout if binary else result.stdout.decode("utf-8", errors="replace")


def read_blobs(repo, commit, paths):
    """Lê vários arquivos de um commit numa chamada só (git cat-file --batch)."""
    request = "".join("%s:%s\n" % (commit, p) for p in paths).encode("utf-8")
    raw = subprocess.run(["git", "-C", repo, "cat-file", "--batch"], input=request, capture_output=True,
                         check=True).stdout
    out = {}
    pos = 0
    for path in paths:
        end = raw.index(b"\n", pos)
        header = raw[pos:end].split(b" ")
        if header[-1] == b"missing":
            raise SystemExit("ausente no commit: " + path)
        size = int(header[2])
        out[path] = raw[end + 1:end + 1 + size]
        pos = end + 1 + size + 1
    return out


def component_of(rel):
    for prefix, name in COMPONENTS:
        if rel.startswith(prefix):
            return name
    return "core"


def association_of(rel):
    for pattern, make in ASSOCIATIONS:
        match = re.match(pattern, rel)
        if match:
            return make(match)
    return "outro"


archive_path = cmodels.archive_path


def fork_files(repo):
    names = git(repo, "diff", "--name-only", "--diff-filter=A", "-z", FORK_BASE, FORK_COMMIT, "--", PREFIX)
    return sorted(n for n in names.split("\0") if n)


def base_paths(archives):
    paths = set()
    for archive in archives:
        with zipfile.ZipFile(archive) as z:
            for info in z.infolist():
                if not info.is_dir():
                    paths.add(info.filename.replace("\\", "/"))
    return paths


def dl_references(data):
    """Devolve [(tipo, hash)] das referências por caminho de uma display list binária, ou None se não for DL."""
    if len(data) < 0x48 or data[0] != 0:
        return None
    if struct.unpack_from("<I", data, 4)[0] != DL_TYPE:
        return None
    offset = 0x40 + 1
    while offset % 8:
        offset += 1
    refs = []
    while offset + 8 <= len(data):
        w0, w1 = struct.unpack_from("<II", data, offset)
        opcode = w0 >> 24
        offset += 8
        if opcode in HASH_OPCODES or opcode in OTHER_WIDE:
            if offset + 8 > len(data):
                break
            h0, h1 = struct.unpack_from("<II", data, offset)
            offset += 8
            if opcode in HASH_OPCODES:
                refs.append((HASH_OPCODES[opcode], (h0 << 32) | h1))
        if opcode in END_OPCODES:
            break
    return refs


# Componente de quem usa um caminho, pelo arquivo do fork. Uma ausência só pesa no núcleo quando algum usuário é
# do núcleo; formas, MM, expansões, rede e o bundle trutefel-enemies.o2r são componentes opcionais (NEI-015).
USER_COMPONENTS = [
    ("soh/mods/transformation_masks/", "forms"),
    ("soh/mods/mm_sources/", "mm"),
    ("soh/mods/pak_loader/", "forms"),
    ("soh/mods/o2r_loader/", "forms"),
    ("soh/mods/actors/trutefel/", "bundle.trutefel-enemies"),
    ("soh/expansions/sm64/", "expansion.sm64"),
    ("soh/expansions/ssbb/", "expansion.ssbb"),
    ("soh/expansions/sw97/", "expansion.sw97"),
    ("soh/expansions/trirod/", "expansion.trirod"),
    ("soh/soh/Network/", "network"),
]

# Referências da expansão MM presentes no código congelado do fork, mesmo quando o OoT é o único host instalado.
# O gerador do núcleo não deve falhar por elas; caminhos novos fora desta lista continuam sendo erro.
MM_OPTIONAL_PREFIXES = (
    "icon_item_static_yar/", "item_name_static/",
    "objects/gameplay_keep/gDekuFlower", "objects/gameplay_keep/gElegyShell",
    "objects/gameplay_keep/gGoldDekuFlower", "objects/gameplay_keep/gPinkDekuFlower",
    "objects/gameplay_keep/gRazorSword", "objects/object_gi_bigbomb/",
    "objects/object_gi_reserve00/", "objects/object_gi_reserve01/",
    "objects/object_gi_reserve_b_00/", "objects/object_gi_reserve_b_01/",
    "objects/object_gi_reserve_c_00/", "objects/object_link_child/gLinkHuman",
    "objects/object_mask_bu_san/", "objects/object_mkk/", "objects/object_pst/",
    "objects/object_sek/", "parameter_static/gPictoBox",
)


def user_component(path):
    for prefix, name in USER_COMPONENTS:
        if path.startswith(prefix):
            return name
    return "core"


def code_references(source):
    refs = {}
    pattern = re.compile(r'"__OTR__([^"]+)"')
    for root, _, files in os.walk(source):
        for file in files:
            if not file.endswith((".c", ".h", ".cpp", ".inc", ".inc.c")):
                continue
            path = os.path.join(root, file)
            with open(path, encoding="utf-8", errors="replace") as f:
                for match in pattern.finditer(f.read()):
                    refs.setdefault(match.group(1), set()).add(os.path.relpath(path, source).replace("\\", "/"))
    return refs


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", default=".")
    parser.add_argument("--packer", required=True)
    parser.add_argument("--base", action="append", default=[], help="oot.o2r, soh.o2r (conferência)")
    parser.add_argument("--mm", action="append", default=[],
                        help="mm.o2r do usuário: caminhos dele contam como dependência do adapter do MM")
    parser.add_argument("--fork-source", help="fonte do fork preparado pelo sync.py (referências __OTR__)")
    parser.add_argument("--out", default="build/nei-assets")
    parser.add_argument("--gbi-include", help="include do libultraship (gbi.h) para os modelos em C; padrão: "
                        "<repo>/libultraship/include")
    args = parser.parse_args()

    repo = os.path.abspath(args.repo)
    out = os.path.abspath(args.out)
    stage_root = os.path.join(out, "stage")
    shutil.rmtree(stage_root, ignore_errors=True)
    os.makedirs(stage_root, exist_ok=True)

    files = fork_files(repo)
    if not files:
        sys.exit("nenhum arquivo do fork: falta o commit %s no git do repositório?" % FORK_COMMIT)

    manifest = {
        "format": "linkspan.nei.assets/1",
        "version": VERSION,
        "source": {
            "repository": "https://github.com/skijer/Not-Enough-Items",
            "commit": FORK_COMMIT_FULL,
            "base": FORK_BASE,
            "tree": PREFIX,
        },
        "license": LICENSE,
        "components": {},
        "files": [],
    }
    by_component = {}
    blobs = {}
    contents = read_blobs(repo, FORK_COMMIT, files)
    packer = os.path.abspath(args.packer)
    for rel_full in files:
        rel = rel_full[len(PREFIX):]
        data = contents[rel_full]
        repaired = rel == SPINNER_TEXTURE
        if repaired:
            # O fork contém uma textura arco-íris de teste. Preservar o modelo e trocar só os pixels
            # pelo material de bronze local. Mesmo container OTEX, formato RGBA16 e dimensões 32x32.
            with open(SPINNER_REPAIR, "rb") as f:
                data = f.read()
            source = contents[rel_full]
            if len(data) != len(source) or data[:92] != source[:92] or len(data) != 92 + 32 * 32 * 2:
                raise SystemExit("spinner-bronze.otex incompatível com sSpinnerTex RGBA16 32x32")
        component = component_of(rel)
        target = os.path.join(stage_root, component, rel)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "wb") as f:
            f.write(data)
        entry = {
            "path": archive_path(rel),
            "source": "soh/native-sdk/nei-core/assets/spinner-bronze.otex" if repaired else rel,
            "sha256": hashlib.sha256(data).hexdigest(),
            "bytes": len(data),
            "component": component,
            "association": association_of(rel),
        }
        if repaired:
            entry["replaces_source"] = rel_full
            entry["repair"] = "textura de bronze gerada localmente; comportamento e modelo do fork preservados"
            entry["license"] = "material original gerado para o Link-Span"
        manifest["files"].append(entry)
        by_component.setdefault(component, []).append(entry)
        blobs[entry["path"]] = data

    # Modelos que o fork escreveu em C viram recursos do núcleo (cmodels.py; o sync.py troca os arrays por caminhos).
    model_sources = read_blobs(repo, FORK_COMMIT, ["soh/mods/" + p for p in cmodels.MODEL_FILES])
    texts = {p: model_sources["soh/mods/" + p].decode("utf-8") for p in cmodels.MODEL_FILES}
    mapping, convert = cmodels.plan(texts, [entry["path"] for entry in manifest["files"]])
    gbi_include = os.path.abspath(args.gbi_include or os.path.join(repo, "libultraship", "include"))
    source_of = {name: model_file for model_file, text in texts.items() for _, name, _ in cmodels.parse_arrays(text)}
    for path, data in sorted(cmodels.resources(texts, mapping, convert, gbi_include).items()):
        target = os.path.join(stage_root, "core", path)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "wb") as f:
            f.write(data)
        entry = {
            "path": path,
            "source": "soh/mods/%s#%s" % (source_of[path.rsplit("/", 1)[-1]], path.rsplit("/", 1)[-1]),
            "sha256": hashlib.sha256(data).hexdigest(),
            "bytes": len(data),
            "component": "core",
            "association": "modelo-c:" + path.split("/")[2],
            "converted": True,
        }
        manifest["files"].append(entry)
        by_component["core"].append(entry)
        blobs[path] = data

    for component, entries in sorted(by_component.items()):
        archive = os.path.join(out, "nei-assets-%s.o2r" % component)
        subprocess.run([packer, os.path.join(stage_root, component), archive, VERSION], check=True)
        with open(archive, "rb") as f:
            digest = hashlib.sha256(f.read()).hexdigest()
        manifest["components"][component] = {
            "archive": os.path.basename(archive),
            "sha256": digest,
            "files": len(entries),
            "optional": component != "core",
        }

    # Conferências.
    report = {"overrides": [], "dangling": [], "code_missing": [], "stats": {}}
    own = {entry["path"] for entry in manifest["files"]}
    base = base_paths(args.base) if args.base else set()
    report["overrides"] = sorted(own & base)
    known = {crc64(p): p for p in own | base}
    dl_count = 0
    ref_count = 0
    xml_attr = re.compile(rb'\b(\w*Path)="([^"]*)"')
    for path, data in blobs.items():
        if data[:1] == b"<":
            refs = [(m.group(1).decode(), m.group(2).decode("utf-8", "replace")) for m in xml_attr.finditer(data)]
            if not refs:
                continue
            dl_count += 1
            for kind, target in refs:
                ref_count += 1
                target = target[len("__OTR__"):] if target.startswith("__OTR__") else target
                if target.startswith((">", "&gt;", "0x")):
                    continue  # endereço de segmento (matriz do limb, paleta), não caminho
                if target and target not in own and target not in base:
                    report["dangling"].append({"recurso": path, "componente": component_of(path), "tipo": kind,
                                                "caminho": target})
            continue
        refs = dl_references(data)
        if refs is None:
            continue
        dl_count += 1
        for kind, value in refs:
            ref_count += 1
            if value not in known:
                report["dangling"].append({"recurso": path, "componente": component_of(path), "tipo": kind,
                                            "hash": "%016x" % value})
    if args.fork_source:
        code = code_references(args.fork_source)
        mm = base_paths(args.mm) if args.mm else set()
        report["code_mm"] = []
        report["code_optional"] = []
        for path, users in sorted(code.items()):
            if path in own or path in base:
                continue
            if not re.fullmatch(r"[\w./-]+", path) or path.endswith(("/", "_")) or "..." in path:
                continue  # prefixo montado em tempo de execução ou texto de exemplo, não caminho
            owners = sorted({user_component(u) for u in users})
            item = {"path": path, "usado_em": sorted(users), "componentes": owners}
            if path in mm or path.startswith(MM_OPTIONAL_PREFIXES):
                report["code_mm"].append(item)
            elif "core" in owners:
                report["code_missing"].append(item)
            else:
                report["code_optional"].append(item)
        report["stats"]["code_refs"] = len(code)
    report["stats"].update({"files": len(own), "display_lists": dl_count, "dl_refs": ref_count,
                            "base_paths": len(base)})

    shutil.rmtree(stage_root, ignore_errors=True)
    with open(os.path.join(out, "nei-assets-manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=1)
    with open(os.path.join(out, "nei-assets-report.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=1)

    print("arquivos: %d em %d componentes" % (len(own), len(by_component)))
    for component, info in sorted(manifest["components"].items()):
        print("  %-18s %5d  %s" % (component, info["files"], info["sha256"][:8]))
    print("display lists: %d, referências: %d, sem destino: %d" % (dl_count, ref_count, len(report["dangling"])))
    print("sobrescrevem o jogo: %d" % len(report["overrides"]))
    if args.fork_source:
        print("caminhos __OTR__ do código: %d, do mm.o2r: %d, de componente opcional ausente: %d, "
              "do núcleo ausentes: %d" % (report["stats"]["code_refs"], len(report["code_mm"]),
                                         len(report["code_optional"]), len(report["code_missing"])))
        for item in report["code_missing"]:
            print("  ausente no núcleo: %s (%s)" % (item["path"], ", ".join(item["usado_em"][:2])))
    core_dangling = [d for d in report["dangling"] if d["componente"] == "core"]
    for item in report["dangling"]:
        print("  sem destino (%s): %s -> %s" % (item["componente"], item["recurso"], item.get("caminho", item.get("hash"))))
    return 1 if report["overrides"] or core_dangling or report.get("code_missing") else 0


if __name__ == "__main__":
    sys.exit(main())
