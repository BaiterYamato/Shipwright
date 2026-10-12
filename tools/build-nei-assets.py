"""Gera os arquivos de assets do Not Enough Items (NEI-007), que vão dentro do .shipmod do NEI (NEI-BUNDLE-001).

Os assets vêm do fork NEI (skijer/Shipwright, branch Not-Enough-Items), que não declara licença; os direitos seguem com
skijer e colaboradores, e o NOTICE do pacote diz a origem. Este script tira do commit congelado do fork os arquivos
que ele acrescentou em soh/assets/custom, separa por componente (núcleo, formas e expansões, que são componentes
opcionais do NEI-015), empacota cada componente num .o2r com o soh-o2r-packer do host e escreve o manifesto com
origem, licença, hash e associação de cada arquivo.

O caminho dos recursos continua o do fork (objects/object_nei_shovel/...), porque o código do fork e as display
lists binárias apontam para ele por nome e por hash. O que o prefixo mod/skijer.nei/ garantiria (não sobrescrever
nada do jogo) o validador prova direto: nenhum caminho de componente existe no oot.o2r nem no soh.o2r.

Textura do fork com os mesmos pixels de uma textura do jogo (oot.o2r, mm.o2r) não vai no .o2r: as display lists
passam a apontar para o caminho do jogo, que o jogador já tem (NEI-BUNDLE-001). Assim o pacote não leva dado do jogo
copiado. O manifesto registra cada troca e contra quais arquivos a conferência foi feita.

O validador também confere as referências internas: cada display list binária só pode apontar (por hash CRC64 do
caminho) para textura, vértice, matriz ou display list que exista nos componentes ou nos arquivos base, e cada
caminho "__OTR__..." que o código do fork compilado na DLL usa precisa existir em algum lugar.

Uso:
  python tools/build-nei-assets.py --repo . --packer build/x64/Release/soh-o2r-packer.exe \
      --base <oot.o2r> --base <soh.o2r> --fork-source build/nei-core-native/nei-fork/fork --out build/nei-assets

Precisa do commit do fork no git do repositório (git fetch https://github.com/skijer/Shipwright c29262b76ead6f00786885ae4be68916cbee77f5).
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
VERSION = "0.3.8"
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
    "bundled": "dentro do .shipmod do NEI, em assets/ (NEI-BUNDLE-001)",
    "note": "O fork não tem LICENSE nem créditos por arquivo (inventário NEI-001 §8). Os direitos seguem com skijer e "
            "colaboradores.",
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


def texture_key(data):
    """sha256 dos pixels de um recurso OTEX v0 ou v1; None se não for textura ou se for pequena demais para contar.

    Tipo e dimensões do cabeçalho ficam fora: algumas texturas do fork, reexportadas de PNG, declaram outros, mas o
    formato que vale é o da display list que as carrega, e os bytes são os mesmos."""
    if len(data) < 0x50 or data[4:8] != b"XETO":
        return None
    version = struct.unpack_from("<I", data, 8)[0]
    offset = 0x4C if version == 0 else 0x58  # o v1 acrescenta flags e as duas escalas antes do tamanho
    if offset + 4 > len(data):
        return None
    size = struct.unpack_from("<I", data, offset)[0]
    pixels = data[offset + 4:offset + 4 + size]
    if len(pixels) != size or size < 32:
        return None
    return hashlib.sha256(pixels).hexdigest()


def game_textures(archives):
    """Índice das texturas do jogo: chave de texture_key -> (arquivo, caminho). O primeiro arquivo listado vence."""
    index = {}
    for archive in archives:
        with zipfile.ZipFile(archive) as z:
            for info in z.infolist():
                if info.is_dir() or info.file_size < 0x50:
                    continue
                key = texture_key(z.read(info))
                if key:
                    index.setdefault(key, (os.path.basename(archive), info.filename.replace("\\", "/")))
    return index


def rewrite_dl(data, by_hash):
    """Troca, numa display list binária, os CRC64 de caminho que estão em by_hash. Devolve os bytes novos ou None."""
    if len(data) < 0x48 or data[0] != 0 or struct.unpack_from("<I", data, 4)[0] != DL_TYPE:
        return None
    out = bytearray(data)
    changed = False
    offset = 0x40 + 1
    while offset % 8:
        offset += 1
    while offset + 8 <= len(out):
        w0 = struct.unpack_from("<I", out, offset)[0]
        opcode = w0 >> 24
        offset += 8
        if opcode in HASH_OPCODES or opcode in OTHER_WIDE:
            if offset + 8 > len(out):
                break
            h0, h1 = struct.unpack_from("<II", out, offset)
            target = by_hash.get((h0 << 32) | h1) if opcode in HASH_OPCODES else None
            if target is not None:
                struct.pack_into("<II", out, offset, target >> 32, target & 0xFFFFFFFF)
                changed = True
            offset += 8
        if opcode in END_OPCODES:
            break
    return bytes(out) if changed else None


def rewrite_xml(data, by_path):
    """Troca, num recurso XML, os atributos *Path que apontam para um caminho de by_path."""
    if data[:1] != b"<":
        return None

    def swap(match):
        value = match.group(2).decode("utf-8", "replace")
        prefix = "__OTR__" if value.startswith("__OTR__") else ""
        target = by_path.get(value[len(prefix):])
        if target is None:
            return match.group(0)
        return match.group(1) + b'="' + (prefix + target).encode("utf-8") + b'"'

    new = re.sub(rb'\b(\w*Path)="([^"]*)"', swap, data)
    return new if new != data else None


def strip_port_version(archive):
    """Tira o portVersion que o soh-o2r-packer grava na raiz (com a versão passada, aqui a do gerador).

    O NEI monta estes .o2r depois do soh.o2r, e no VFS o portVersion deles encobria o do host: o Unbound lê dali a
    major do SoH e, com 0.3.x, reconvertia a base sem o sohMajor. Asset de mod não declara versão do port.
    """
    with zipfile.ZipFile(archive) as z:
        infos = z.infolist()
        kept = [(info, z.read(info)) for info in infos if info.filename != "portVersion"]
    if len(kept) == len(infos):
        return
    temp = archive + ".tmp"
    with zipfile.ZipFile(temp, "w") as z:
        for info, data in kept:
            z.writestr(info, data)
    os.replace(temp, archive)


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
            "repository": "https://github.com/skijer/Shipwright/tree/Not-Enough-Items",
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
    stage_of = {}
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
        stage_of[entry["path"]] = target

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
        stage_of[path] = target

    # Texturas com os pixels de uma do jogo: referência ao caminho do jogo no lugar da cópia.
    game_archives = list(args.base) + list(args.mm)
    replaced = {}
    index = {}
    if game_archives:
        index = game_textures(game_archives)
        for path, data in blobs.items():
            key = texture_key(data)
            if key and key in index:
                replaced[path] = index[key]
    manifest["game_data"] = {
        "checked_against": sorted({os.path.basename(a) for a in game_archives}),
        "replaced": [{"path": p, "target": t, "archive": a} for p, (a, t) in sorted(replaced.items())],
    }
    if replaced:
        by_hash = {crc64(p): crc64(t) for p, (_, t) in replaced.items()}
        by_path = {p: t for p, (_, t) in replaced.items()}
        for path in replaced:
            os.remove(stage_of[path])
            del blobs[path]
        manifest["files"] = [e for e in manifest["files"] if e["path"] not in replaced]
        for component in by_component:
            by_component[component] = [e for e in by_component[component] if e["path"] not in replaced]
        entries = {e["path"]: e for e in manifest["files"]}
        for path, data in list(blobs.items()):
            new = rewrite_dl(data, by_hash) or rewrite_xml(data, by_path)
            if new is None:
                continue
            with open(stage_of[path], "wb") as f:
                f.write(new)
            blobs[path] = new
            entries[path]["sha256"] = hashlib.sha256(new).hexdigest()
            entries[path]["bytes"] = len(new)
            entries[path]["game_refs"] = True

    for component, entries in sorted(by_component.items()):
        archive = os.path.join(out, "nei-assets-%s.o2r" % component)
        subprocess.run([packer, os.path.join(stage_root, component), archive, VERSION], check=True)
        strip_port_version(archive)
        with open(archive, "rb") as f:
            digest = hashlib.sha256(f.read()).hexdigest()
        # O fork guarda parte das texturas em PNG, que só vira OTEX no packer: a troca acima não as vê. Esta conta,
        # no .o2r pronto, diz quantas cópias de textura do jogo sobraram; o package-nei-core.ps1 recusa o componente.
        remaining = []
        if index:
            with zipfile.ZipFile(archive) as z:
                for info in z.infolist():
                    if not info.is_dir() and info.file_size >= 0x50:
                        key = texture_key(z.read(info))
                        # O soh.o2r é do próprio SoH, não dado da ROM: cópia dele não conta.
                        if key and key in index and index[key][0] != "soh.o2r":
                            remaining.append({"path": info.filename, "game": index[key][1], "archive": index[key][0]})
        manifest["components"][component] = {
            "archive": os.path.basename(archive),
            "sha256": digest,
            "files": len(entries),
            "optional": component != "core",
            "game_copies": remaining,
        }

    # Conferências.
    report = {"overrides": [], "dangling": [], "code_missing": [], "stats": {}}
    own = {entry["path"] for entry in manifest["files"]}
    base = base_paths(args.base) if args.base else set()
    report["overrides"] = sorted(own & base)
    targets = {t for _, t in replaced.values()}
    known = {crc64(p): p for p in own | base | targets}
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
                if target and target not in own and target not in base and target not in targets:
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
        report["code_replaced"] = sorted(p for p in code if p in replaced)
    report["stats"].update({"files": len(own), "display_lists": dl_count, "dl_refs": ref_count,
                            "base_paths": len(base)})
    report["port_version"] = []
    for info in manifest["components"].values():
        with zipfile.ZipFile(os.path.join(out, info["archive"])) as z:
            if "portVersion" in z.namelist():
                report["port_version"].append(info["archive"])

    shutil.rmtree(stage_root, ignore_errors=True)
    with open(os.path.join(out, "nei-assets-manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=1)
    with open(os.path.join(out, "nei-assets-report.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=1)

    print("arquivos: %d em %d componentes" % (len(own), len(by_component)))
    for component, info in sorted(manifest["components"].items()):
        copies = len(info["game_copies"])
        print("  %-18s %5d  %s%s" % (component, info["files"], info["sha256"][:8],
                                     "  cópias do jogo: %d" % copies if copies else ""))
    print("display lists: %d, referências: %d, sem destino: %d" % (dl_count, ref_count, len(report["dangling"])))
    print("sobrescrevem o jogo: %d" % len(report["overrides"]))
    print("texturas iguais às do jogo, trocadas por referência: %d (%s)" % (
        len(replaced), ", ".join(manifest["game_data"]["checked_against"]) or "sem conferência"))
    if args.fork_source:
        print("caminhos __OTR__ do código: %d, do mm.o2r: %d, de componente opcional ausente: %d, "
              "do núcleo ausentes: %d" % (report["stats"]["code_refs"], len(report["code_mm"]),
                                         len(report["code_optional"]), len(report["code_missing"])))
        for item in report["code_missing"]:
            print("  ausente no núcleo: %s (%s)" % (item["path"], ", ".join(item["usado_em"][:2])))
    core_dangling = [d for d in report["dangling"] if d["componente"] == "core"]
    for item in report["dangling"]:
        print("  sem destino (%s): %s -> %s" % (item["componente"], item["recurso"], item.get("caminho", item.get("hash"))))
    for path in report.get("code_replaced", []):
        print("  o código do fork pede pelo nome uma textura trocada: %s" % path)
    for archive in report["port_version"]:
        print("  portVersion na raiz (encobre o do soh.o2r): %s" % archive)
    return 1 if (report["overrides"] or core_dangling or report.get("code_missing") or
                 report.get("code_replaced") or report["port_version"]) else 0


if __name__ == "__main__":
    sys.exit(main())
