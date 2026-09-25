"""Modelos do fork NEI escritos em C (arrays de textura, vértice e display list do fast64) viram recursos (NEI-007).

O fork compila alguns modelos direto no código, como arrays C, em vez de pô-los no archive. O plano pede que nenhum
modelo fique compilado assim só para contornar o resource manager, e os assets do fork não podem ir na DLL (não
têm licença). Este módulo faz as duas pontas:

- `resources(...)`, usado pelo tools/build-nei-assets.py: converte cada array em recurso do LUS no formato que o
  próprio fork usa nos modelos exportados (textura binária OTEX v1, vértices em XML, display list binária ODLT com
  as referências por hash de caminho), para entrar no nei-assets-core.o2r;
- `c_definitions(...)`, usado pelo sync.py: troca o array por `const char nome[] = "__OTR__caminho"`, o idioma do
  SoH, e o código do fork passa a pedir o recurso ao resource manager.

Símbolo que já existe com o mesmo nome nos assets exportados do fork (o fork exportou vários desses modelos para o
archive e deixou a cópia em C) aponta para o recurso existente e não é convertido.

As macros gs* saem do gbi.h do libultraship pelo pcpp (pip install pcpp), com F3DEX_GBI_2, e as expressões são
avaliadas aqui; os ponteiros para outros arrays viram os comandos de 128 bits G_VTX/G_SETTIMG/G_DL_OTR_HASH.
"""
import os
import re
import struct

# Arquivos do fork com modelos em C (caminho no fork, relativo a soh/mods). O nome da pasta vira a pasta do recurso
# convertido: objects/object_nei_cmodels/<pasta>/<símbolo>.
MODEL_FILES = [
    "items/objects/shovel_DL/gDampeShovelDL_mesh_001.c",
    "items/objects/shovel_hole_DL/model.inc.c",
    "items/objects/ball_and_chainDL/model.inc.c",
    "items/objects/beetle_giveDL/model.inc.c",
    "items/objects/desire_sensor_giveDL/model.inc.c",
    "items/objects/fire_rodDL/model.inc.c",
    "items/objects/ice_rodDL/model.inc.c",
    "items/objects/light_rodDL/Cylinder_002.c",
    "items/objects/magic_spell_giveDL/model.inc.c",
    "items/objects/shovel_giveDL/model.inc.c",
    "equipment/objects/ikaxe_DL/model.inc.c",
]
CONVERTED_ROOT = "objects/object_nei_cmodels"

ARRAY = re.compile(r"^[ \t]*(?:static\s+)?(?:const\s+)?(?:ALIGN_ASSET\(\d+\)\s+)?(u64|u32|u16|u8|Vtx|Gfx)\s+(\w+)"
                   r"\s*\[[^\]]*\]\s*=\s*\{(.*?)^\};", re.S | re.M)


PATH_REF = re.compile(r"^[ \t]*(?:static\s+)?(?:const\s+)?(?:ALIGN_ASSET\(\d+\)\s+)?char\s+(\w+)\s*\[\s*\]\s*=\s*(\w+)\s*;",
                      re.M)


def parse_arrays(text):
    return [(m.group(1), m.group(2), m.group(3)) for m in ARRAY.finditer(text)]


def parse_path_refs(text):
    """{símbolo: caminho} das texturas que o modelo já pede ao archive (char x[] = dx; com #define dx "__OTR__...")."""
    defines = dict(re.findall(r'#\s*define\s+(\w+)\s+"__OTR__([^"]+)"', text))
    return {m.group(1): defines[m.group(2)] for m in PATH_REF.finditer(text) if m.group(2) in defines}


ASSET_PREFIX = "soh/assets/custom/"


def archive_path(rel):
    """Caminho do recurso dentro do .o2r, como o soh-o2r-packer grava (PNG com formato perde o sufixo)."""
    name = os.path.basename(rel)
    if rel.endswith(".png") and name.count(".") >= 2:
        return rel[: -len(".png")].rsplit(".", 1)[0]
    return rel


def fork_asset_paths(repo, base, commit):
    """Caminhos no archive dos assets que o fork acrescentou (a mesma lista que o build-nei-assets.py empacota)."""
    import subprocess
    names = subprocess.run(["git", "-C", repo, "diff", "--name-only", "--diff-filter=A", "-z", base, commit, "--",
                            ASSET_PREFIX], capture_output=True, check=True).stdout.decode("utf-8")
    return sorted(archive_path(n[len(ASSET_PREFIX):]) for n in names.split("\0") if n)


def folder_of(model_file):
    return model_file.split("/")[-2]


def plan(texts, asset_paths):
    """{símbolo: caminho do recurso} e o conjunto de símbolos que precisam de conversão."""
    by_name = {}
    for path in asset_paths:
        by_name.setdefault(path.rsplit("/", 1)[-1], []).append(path)
    mapping, convert = {}, set()
    for model_file, text in texts.items():
        mapping.update(parse_path_refs(text))
        for ctype, name, _ in parse_arrays(text):
            found = by_name.get(name, [])
            if len(found) == 1:
                mapping[name] = found[0]
            else:
                mapping[name] = "%s/%s/%s" % (CONVERTED_ROOT, folder_of(model_file), name)
                convert.add(name)
    return mapping, convert


def c_definitions(texts, mapping):
    """Definições C que substituem os arrays: o endereço passa a ser o caminho do recurso."""
    lines = ["/* Gerado por cmodels.py: os modelos em C do fork NEI apontam para recursos do archive (NEI-007). */",
             "#include <stdint.h>", ""]
    seen = set()
    for model_file, text in texts.items():
        lines.append("/* %s */" % model_file)
        for ctype, name, _ in parse_arrays(text):
            if name in seen:
                continue
            seen.add(name)
            lines.append('__declspec(align(8)) const char %s[] = "__OTR__%s";' % (name, mapping[name]))
        lines.append("")
    return "\n".join(lines) + "\n"


def header_declarations(header_text, names):
    """extern Gfx x[]; -> extern const char x[]; nos headers dos modelos."""
    pattern = re.compile(r"extern\s+(?:const\s+)?(?:u64|u32|u16|u8|Vtx|Gfx)\s+(\w+)\s*\[\s*\w*\s*\]\s*;")
    return pattern.sub(lambda m: "extern const char %s[];" % m.group(1) if m.group(1) in names else m.group(0),
                       header_text)


# ---------------------------------------------------------------------------------------------------------------------
# Conversão para recursos


class Ref:
    def __init__(self, name, offset=0):
        self.name, self.offset = name, offset

    def __add__(self, other):
        return Ref(self.name, self.offset + int(other))

    __radd__ = __add__


TOKEN = re.compile(r"\s*(0[xX][0-9a-fA-F]+|\d+)[uUlL]*|\s*([A-Za-z_]\w*)|\s*(<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^~!?:(),<>])")
CAST = re.compile(r"\(\s*(?:const\s+)?(?:unsigned\s+int|unsigned\s+long\s+long|unsigned\s+long|unsigned|signed|int|long|"
                  r"short|char|bool|float|u8|u16|u32|u64|s8|s16|s32|s64|uint8_t|uint16_t|uint32_t|uint64_t|int8_t|"
                  r"int16_t|int32_t|int64_t|uintptr_t|intptr_t|f32|Gfx|Vtx)\s*\*?\s*\)")


class Expr:
    """Avaliador de expressão C inteira (depois do pré-processador): precedência do C, ternário, MIN/MAX."""

    BINARY = [["||"], ["&&"], ["|"], ["^"], ["&"], ["==", "!="], ["<", ">", "<=", ">="], ["<<", ">>"], ["+", "-"],
              ["*", "/", "%"]]

    def __init__(self, text, symbols):
        text = CAST.sub(" ", text.replace("sizeof(Vtx)", "16"))
        text = re.sub(r"&\s*(\w+)\s*\[([^\]]+)\]", r"(\1 + (\2))", text)  # &v[3] -> v + 3 (em elementos)
        self.tokens, pos = [], 0
        while pos < len(text):
            if text[pos:].strip() == "":
                break
            m = TOKEN.match(text, pos)
            if not m:
                raise ValueError("token inválido em: " + text[pos:pos + 40])
            self.tokens.append(m.group(1) or m.group(2) or m.group(3))
            pos = m.end()
        self.symbols, self.i = symbols, 0

    def peek(self):
        return self.tokens[self.i] if self.i < len(self.tokens) else None

    def take(self, expected=None):
        tok = self.peek()
        if expected is not None and tok != expected:
            raise ValueError("esperava %s, veio %s" % (expected, tok))
        self.i += 1
        return tok

    def parse(self):
        value = self.ternary()
        if self.peek() is not None:
            raise ValueError("sobrou: " + " ".join(self.tokens[self.i:]))
        return value

    def ternary(self):
        cond = self.binary(0)
        if self.peek() == "?":
            self.take()
            a = self.ternary()
            self.take(":")
            b = self.ternary()
            return a if cond else b
        return cond

    def binary(self, level):
        if level == len(self.BINARY):
            return self.unary()
        left = self.binary(level + 1)
        while self.peek() in self.BINARY[level]:
            op = self.take()
            right = self.binary(level + 1)
            left = self.apply(op, left, right)
        return left

    @staticmethod
    def apply(op, a, b):
        if isinstance(a, Ref) or isinstance(b, Ref):
            if op == "+":
                return a + b
            if op == "-" and isinstance(a, Ref) and not isinstance(b, Ref):
                return Ref(a.name, a.offset - b)
            raise ValueError("operação %s com ponteiro" % op)
        if op == "/":
            return int(a / b)
        if op == "%":
            return a - int(a / b) * b
        return {"||": lambda: int(bool(a) or bool(b)), "&&": lambda: int(bool(a) and bool(b)), "|": lambda: a | b,
                "^": lambda: a ^ b, "&": lambda: a & b, "==": lambda: int(a == b), "!=": lambda: int(a != b),
                "<": lambda: int(a < b), ">": lambda: int(a > b), "<=": lambda: int(a <= b),
                ">=": lambda: int(a >= b), "<<": lambda: (a << b) & 0xFFFFFFFFFFFFFFFF, ">>": lambda: a >> b,
                "+": lambda: a + b, "-": lambda: a - b, "*": lambda: a * b}[op]()

    def unary(self):
        tok = self.peek()
        if tok in ("-", "~", "!", "+"):
            self.take()
            value = self.unary()
            return {"-": -value, "~": ~value, "!": int(not value), "+": value}[tok] if not isinstance(value, Ref) \
                else value
        return self.primary()

    def primary(self):
        tok = self.take()
        if tok == "(":
            value = self.ternary()
            self.take(")")
            return value
        if tok is None:
            raise ValueError("expressão vazia")
        if tok[0].isdigit():
            return int(tok, 0)
        if tok in ("MIN", "MAX"):
            self.take("(")
            a = self.ternary()
            self.take(",")
            b = self.ternary()
            self.take(")")
            return min(a, b) if tok == "MIN" else max(a, b)
        if tok in self.symbols:
            return Ref(tok)
        if tok in ("true", "false"):
            return int(tok == "true")
        raise ValueError("identificador sem valor: " + tok)


def split_top(text, sep=","):
    parts, depth, cur = [], 0, []
    for ch in text:
        if ch in "({[":
            depth += 1
        elif ch in ")}]":
            depth -= 1
        if ch == sep and depth == 0:
            parts.append("".join(cur).strip())
            cur = []
            continue
        cur.append(ch)
    if "".join(cur).strip():
        parts.append("".join(cur).strip())
    return parts


def brace_groups(text):
    """'{a, b}, {c, d}' -> [['a', 'b'], ['c', 'd']]."""
    groups, depth, start = [], 0, None
    for i, ch in enumerate(text):
        if ch == "{":
            if depth == 0:
                start = i + 1
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                groups.append(split_top(text[start:i]))
    return groups


def expand_gfx(gfx_arrays, gbi_include):
    """{nome: [macro, ...]} -> {nome: [(w0, w1)]}, com w1 inteiro ou Ref."""
    import io
    from pcpp import Preprocessor

    source = ["#define F3DEX_GBI_2 1", "#define _LANGUAGE_C 1", '#include "libultraship/libultra/gbi.h"']
    for name, calls in gfx_arrays.items():
        source.append("@@ARRAY %s" % name)
        for call in calls:
            source.append(call.replace("\n", " ") + " @@CMD")
    pre = Preprocessor()
    pre.add_path(gbi_include)
    pre.passthru_unfound_includes = True
    pre.line_directive = None
    pre.parse("\n".join(source) + "\n", "cmodels")
    out = io.StringIO()
    pre.write(out)
    text = out.getvalue()
    result, current = {}, None
    for chunk in re.split(r"(@@ARRAY\s+\w+|@@CMD)", text[text.find("@@ARRAY"):]):
        m = re.match(r"@@ARRAY\s+(\w+)", chunk)
        if m:
            current = m.group(1)
            result[current] = []
            continue
        if chunk == "@@CMD" or current is None or not chunk.strip():
            continue
        result[current].append(chunk)
    return result


def gfx_words(expanded_calls, symbols):
    words = []
    for chunk in expanded_calls:
        for group in brace_groups(chunk):
            if len(group) != 2:
                raise ValueError("comando com %d palavras: %s" % (len(group), group))
            w0 = Expr(group[0], symbols).parse()
            w1 = Expr(group[1], symbols).parse()
            words.append((w0 & 0xFFFFFFFF, w1 if isinstance(w1, Ref) else w1 & 0xFFFFFFFF))
    return words


def crc64(text):
    poly = 0x42F0E1EBA9EA3693
    crc = 0xFFFFFFFFFFFFFFFF
    for byte in text.encode("utf-8"):
        crc ^= byte << 56
        for _ in range(8):
            crc = ((crc << 1) ^ poly) & 0xFFFFFFFFFFFFFFFF if crc & (1 << 63) else (crc << 1) & 0xFFFFFFFFFFFFFFFF
    return crc


def header(resource_type, version):
    data = bytearray(0x40)
    struct.pack_into("<IIQ", data, 4, resource_type, version, 0xDEADBEEFDEADBEEF)
    return bytes(data)


OP_VTX, OP_TRI1, OP_DL, OP_ENDDL, OP_SETTIMG = 0x01, 0x05, 0xDE, 0xDF, 0xFD
OP_SETTILE, OP_SETTILESIZE, OP_LOADTLUT = 0xF5, 0xF2, 0xF0
OP_VTX_HASH, OP_SETTIMG_HASH, OP_DL_HASH = 0x32, 0x20, 0x31
TEXTURE_TYPES = {(0, 2): 2, (0, 3): 1, (2, 0): 3, (2, 1): 4, (3, 0): 7, (3, 1): 8, (3, 2): 9, (4, 0): 5, (4, 1): 6}
BITS = {0: 4, 1: 8, 2: 16, 3: 32}


def display_list(words, mapping, textures):
    """Display list binária (ODLT v0, F3DEX2) e anotação do formato das texturas que ela carrega."""
    body = bytearray([4] + [0] * 7)  # ucode_f3dex2, alinhado a 8
    last_timg = None
    for w0, w1 in words:
        op = w0 >> 24
        if isinstance(w1, Ref):
            path = mapping[w1.name]
            if op == OP_VTX:
                first, second = ((OP_VTX_HASH << 24) | (w0 & 0xFFFFFF), w1.offset * 16), path
            elif op == OP_SETTIMG:
                first, second = ((OP_SETTIMG_HASH << 24) | (w0 & 0xFFFFFF), 0), path
                last_timg = w1.name
            elif op == OP_DL:
                first, second = ((OP_DL_HASH << 24) | (w0 & 0xFFFFFF), 0), path
            else:
                raise ValueError("ponteiro no comando 0x%02X" % op)
            h = crc64(second)
            body += struct.pack("<IIII", first[0], first[1], h >> 32, h & 0xFFFFFFFF)
            continue
        if op == OP_SETTILE and last_timg and ((w1 >> 24) & 7) != 7:
            textures.setdefault(last_timg, {}).update(fmt=(w0 >> 21) & 7, siz=(w0 >> 19) & 3)
        elif op == OP_SETTILESIZE and last_timg and ((w1 >> 24) & 7) != 7:
            textures.setdefault(last_timg, {}).update(width=((w1 >> 12) & 0xFFF) // 4 + 1, height=(w1 & 0xFFF) // 4 + 1)
        elif op == OP_LOADTLUT and last_timg:
            textures.setdefault(last_timg, {}).update(fmt=0, siz=2, width=((w1 >> 14) & 0x3FF) + 1, height=1)
        body += struct.pack("<II", w0, w1)
        if op == OP_ENDDL:
            break
    return header(0x4F444C54, 0) + bytes(body)


def texture(data, info, name):
    kind = TEXTURE_TYPES.get((info.get("fmt"), info.get("siz")))
    width, height = info.get("width"), info.get("height")
    if kind is None or not width or not height:
        raise ValueError("textura %s sem formato ou tamanho nas display lists" % name)
    expected = width * height * BITS[info["siz"]] // 8
    if expected > len(data):
        raise ValueError("textura %s: %dx%d pede %d bytes, o array tem %d" % (name, width, height, expected, len(data)))
    payload = data[:expected]
    return header(0x4F544558, 1) + struct.pack("<IIIIffI", kind, width, height, 0, 1.0, 1.0, len(payload)) + payload


def vertices(body):
    """Vértices em {{{x, y, z}, flag, {s, t}, {r, g, b, a}}} (fast64) ou VTX(x, y, z, s, t, r, g, b, a) (macros.h)."""
    def s16(v):
        v = int(v, 0) & 0xFFFF
        return v - 0x10000 if v & 0x8000 else v

    out = ['<Vertex Version="0">']
    for entry in split_top(re.sub(r"/\*.*?\*/|//[^\n]*", "", body, flags=re.S)):
        numbers = re.findall(r"-?(?:0[xX][0-9a-fA-F]+|\d+)", entry)
        if entry.startswith("VTX"):
            numbers.insert(3, "0")
        if len(numbers) != 10:
            raise ValueError("vértice fora do formato: " + entry[:80])
        x, y, z, _, s, t = (s16(v) for v in numbers[:6])
        r, g, b, a = (int(v, 0) & 0xFF for v in numbers[6:])
        out.append('\t<Vtx X="%d" Y="%d" Z="%d" S="%d" T="%d" R="%d" G="%d" B="%d" A="%d"/>' % (x, y, z, s, t, r, g, b, a))
    out.append("</Vertex>")
    return ("\n".join(out) + "\n").encode("utf-8")


def raw_bytes(ctype, body):
    size = {"u64": 8, "u32": 4, "u16": 2, "u8": 1}[ctype]
    values = [int(v, 0) for v in re.findall(r"0[xX][0-9a-fA-F]+|\d+", re.sub(r"/\*.*?\*/|//[^\n]*", "", body, flags=re.S))]
    return b"".join(v.to_bytes(size, "big") for v in values)


def resources(texts, mapping, convert, gbi_include):
    """{caminho no archive: bytes} dos símbolos em `convert`."""
    arrays, refs = {}, set()
    for text in texts.values():
        refs.update(parse_path_refs(text))
        for ctype, name, body in parse_arrays(text):
            arrays[name] = (ctype, body)
    gfx = {name: split_top(re.sub(r"/\*.*?\*/|//[^\n]*", "", body, flags=re.S))
           for name, (ctype, body) in arrays.items() if ctype == "Gfx" and name in convert}
    expanded = expand_gfx(gfx, gbi_include)
    out, textures = {}, {}
    for name, calls in expanded.items():
        out[mapping[name]] = display_list(gfx_words(calls, set(arrays) | refs), mapping, textures)
    for name in convert:
        ctype, body = arrays[name]
        if ctype == "Vtx":
            out[mapping[name]] = vertices(body)
        elif ctype != "Gfx":
            out[mapping[name]] = texture(raw_bytes(ctype, body), textures.get(name, {}), name)
    return out


def read_model_texts(read):
    """read(caminho relativo a soh/mods) -> texto; devolve {arquivo: texto} dos MODEL_FILES."""
    return {path: read(path) for path in MODEL_FILES}
