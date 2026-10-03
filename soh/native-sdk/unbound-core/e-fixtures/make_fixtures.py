#!/usr/bin/env python3
"""Gera assets/ das fixtures da fase E do Unbound (UNBOUND-007 a 011).

Uso: python make_fixtures.py <oot-unbound.o2r> <pasta-assets>

As cenas copiadas (casa do Link, Dodongo's Cavern, luzes do Hyrule Field) saem da base convertida da
máquina de quem roda o script: dados do jogo nunca entram no repositório nem no pacote publicado. A cena
ampla (colisão, atores) e o título são sintéticos.
"""
import copy
import json
import re
import struct
import sys
import zipfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

TITLE_CARD = "textures/linkspan_e/title_card"
SIGN_ACTOR = 0x0141   # En_Kanban: textId = params | 0x300
SIGN_OBJECT = 0x012F  # object_kanban
HEART_ACTOR = 0x0015  # En_Item00
HEART_PARAMS = 3      # coração de recuperação
HEART_OBJECT = 0x00B7  # object_gi_heart
WIDE_SPAWN = 45000    # além do alcance s16 (±32 760)
WIDE_EXTENT = 60000
WIDE_GRID = 150       # 151² vértices (> 8 191) e 45 000 polígonos (> 32 767)


def main():
    base = zipfile.ZipFile(sys.argv[1])
    out = Path(sys.argv[2])

    def doc(path):
        return json.loads(base.read(path))

    def put(path, value):
        target = out / path
        target.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(value, (bytes, bytearray)):
            target.write_bytes(value)
        else:
            target.write_text(json.dumps(value, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")

    registry = {}
    put(TITLE_CARD, title_card("LINK-SPAN UNBOUND"))

    # UNBOUND-007/010: casa do Link como cena própria, exits por nome e duas placas na frente do spawn.
    house = doc("scenes/link_home/scene.json")
    house["rooms"] = {"0": "scenes/linkspan_e/house/rooms/0.json"}
    for setup in house["setups"].values():
        if "exits" in setup:
            setup["exits"] = {key: "ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN" for key in setup["exits"]}
    put("scenes/linkspan_e/house/scene.json", house)
    room = doc("scenes/link_home/rooms/0.json")
    # Spawn 1 é a porta (Link olha para +z); os polígonos de saída ficam logo atrás dele, em z < -136.
    spawn = house["setups"]["0"]["spawns"]["1"]["pos"]
    for setup in room["setups"].values():
        objects = setup.setdefault("objects", {})
        objects[str(len(objects))] = SIGN_OBJECT
        actors = setup.setdefault("actors", {})
        # A placa nova fica logo à frente, a substituída ao lado.
        actors["linkspan_sign_new"] = {"id": SIGN_ACTOR, "pos": [spawn[0], 0, spawn[2] + 60],
                                       "rot": [0, 0x8000 - 0x10000, 0], "params": 0x00FE}
        actors["linkspan_sign_replaced"] = {"id": SIGN_ACTOR, "pos": [spawn[0] + 80, 0, spawn[2] + 60],
                                            "rot": [0, 0x8000 - 0x10000, 0], "params": 0x0001}
    put("scenes/linkspan_e/house/rooms/0.json", room)
    registry["linkspan_e/house"] = {
        "name": "Link-Span E: casa JSON",
        "scene": "scenes/linkspan_e/house/scene.json",
        "drawConfig": 0,
        "titleCardTexture": TITLE_CARD,
        "entrances": {"main": {"spawn": 1, "showTitleCard": True}},
    }
    put("text/eng/messages.json", {
        "$schema": "unbound/text/1",
        "messages": {
            "0x03FE": {"box": 0, "ypos": 0,
                       "text": "\u0005AUnbound\u0005@: esta mensagem\u0001veio de text/eng/messages.json."},
            "0x0301": {"box": 0, "ypos": 0,
                       "text": "\u0008Placa \u0005Dsubstituida\u0005@ pelo JSON\u0001do Link-Span.\u0009"},
            "0x0345": None,
        },
    })

    # UNBOUND-008/009: chão sintético de 45 000 polígonos até ±60 000, Link em x=z=45 000 e 600 atores.
    field = doc("scenes/spot00/scene.json")["setups"]["0"]
    lighting = copy.deepcopy(field["lighting"])
    for entry in lighting.values():
        entry.update({"fogStart": 30000, "fogEnd": 60000, "drawDistance": 60000})
    put("scenes/linkspan_e/wide/scene.json", {
        "$schema": "unbound/scene/1",
        "collision": "scenes/linkspan_e/wide/collision.json",
        "rooms": {"0": "scenes/linkspan_e/wide/rooms/0.json"},
        "setups": {"0": {
            "sound": field["sound"],
            "cameraSettings": field["cameraSettings"],
            "specialObjects": field["specialObjects"],
            "skybox": field["skybox"],
            "lighting": lighting,
            "spawns": {"0": {"id": 0, "pos": [WIDE_SPAWN, 0, WIDE_SPAWN], "rot": [0, 0, 0],
                             "params": field["spawns"]["0"]["params"]}},
            "entrances": {"0": {"spawn": 0, "room": 0}},
            "exits": {},
        }},
    })
    field_room = doc("scenes/spot00/rooms/0.json")["setups"]["0"]
    wide_room = {key: copy.deepcopy(value) for key, value in field_room.items() if key != "actors"}
    # Coração da lista de atores usa o modelo 3D: sem object_gi_heart na sala ele existe mas não desenha.
    objects = wide_room.setdefault("objects", {})
    objects[str(len(objects))] = HEART_OBJECT
    wide_room["actors"] = {}
    for i in range(600):
        column, row = i % 30, i // 30
        # A coluna 15 cai em x = WIDE_SPAWN: andando para a frente o jogador coleta essa fileira.
        wide_room["actors"][str(i)] = {"id": HEART_ACTOR, "rot": [0, 0, 0], "params": HEART_PARAMS,
                                       "pos": [WIDE_SPAWN - 900 + column * 60, 0, WIDE_SPAWN + 200 + row * 60]}
    put("scenes/linkspan_e/wide/rooms/0.json", {"$schema": "unbound/room/1", "setups": {"0": wide_room}})
    vertices, polys = floor_grid(WIDE_GRID, WIDE_EXTENT)
    put("scenes/linkspan_e/wide/collision.bin", vertices + polys)
    field_collision = doc("scenes/spot00/collision.json")
    camera = dict(field_collision["cameras"]["0"], count=0, positionIndex=None)
    put("scenes/linkspan_e/wide/collision.json", {
        "$schema": "unbound/collision/3",
        "bounds": {"min": [-WIDE_EXTENT, -10, -WIDE_EXTENT], "max": [WIDE_EXTENT, 10, WIDE_EXTENT]},
        "bulk": {"file": "scenes/linkspan_e/wide/collision.bin", "vertices": (WIDE_GRID + 1) ** 2,
                 "polys": 2 * WIDE_GRID * WIDE_GRID},
        "surfaceTypes": {"0": {"camera": 0, "exit": 0, "floorType": 0, "wallFlags": 0, "wallType": 0,
                               "floorProperty": 0, "isSoft": 0, "isHorseBlocked": 0, "material": 0,
                               "floorEffect": 0, "lightSetting": 0, "echo": 0, "canHookshot": 0,
                               "conveyorSpeed": 0, "conveyorDirection": 0, "isWallDamage": 0}},
        "cameras": {"0": camera},
        "cameraPositions": {},
        "waterBoxes": {},
    })
    registry["linkspan_e/wide"] = {
        "name": "Link-Span E: mundo amplo",
        "scene": "scenes/linkspan_e/wide/scene.json",
        "drawConfig": 0,
        "entrances": {"main": {"spawn": 0}},
    }

    # UNBOUND-011: Dodongo's Cavern sem o draw config vanilla; os segmentos vêm de materialAnims.
    lava = [f"scenes/nonmq/ddan_scene/gDCLavaFloor{i}Tex" for i in range(1, 9)]
    entrance = ["scenes/nonmq/ddan_scene/gDCDayEntranceTex", "scenes/nonmq/ddan_scene/gDCNightEntranceTex"]
    common = {
        "0": {"segment": 8, "pass": "opa", "type": "texCycle", "textures": entrance, "frames": [0]},
        "1": {"segment": 9, "pass": "opa", "type": "texCycle", "textures": lava,
              "frames": [i // 2 for i in range(16)]},
        "2": {"segment": 9, "pass": "xlu", "type": "twoTexScroll",
              "layers": [{"xStep": 1, "yStep": 0, "width": 64, "height": 32},
                         {"xStep": 0, "yStep": -1, "width": 64, "height": 32}]},
        "3": {"segment": 10, "pass": "opa", "type": "texScroll",
              "layers": [{"xStep": 0, "yStep": -1, "width": 32, "height": 32}]},
    }
    eyes = {
        "cavern": {
            "4": {"segment": 11, "pass": "opa", "type": "color", "length": 60, "keyFrames": [0, 30],
                  "primColors": [[255, 255, 255, 255, 0], [255, 255, 255, 255, 0]],
                  "envColors": [[255, 255, 255, 0], [255, 255, 255, 255]]},
            "5": {"segment": 12, "pass": "opa", "type": "colorLerp", "length": 60, "keyFrames": [0, 30, 60],
                  "primColors": [[255, 255, 255, 255, 0]] * 3,
                  "envColors": [[255, 255, 255, 0], [255, 255, 255, 255], [255, 255, 255, 0]]},
        },
        "cavern_b": {
            "4": {"segment": 11, "pass": "opa", "type": "colorNonLinear", "length": 90,
                  "keyFrames": [0, 30, 60, 90],
                  "primColors": [[255, 255, 255, 255, 0]] * 4,
                  "envColors": [[255, 255, 255, 0], [255, 120, 0, 255], [255, 255, 255, 60],
                                [255, 255, 255, 0]]},
            "5": {"segment": 12, "pass": "opa", "type": "color", "length": 1, "keyFrames": [0],
                  "primColors": [[255, 255, 255, 255, 0]], "envColors": [[255, 60, 0, 255]]},
        },
    }
    # A textura da entrada (segmento 8) é o que se vê do spawn: cavern fica no dia, cavern_b alterna dia e
    # noite a cada 10 frames. O spawn vira para a saída para ela ficar na tela.
    entrance_frames = {"cavern": [0], "cavern_b": [0] * 10 + [1] * 10}
    cavern = doc("scenes/ddan/scene.json")
    for name, eye in eyes.items():
        scene = copy.deepcopy(cavern)
        scene["setups"]["0"]["spawns"]["0"]["rot"] = [0, 0, 0]
        anims = {**copy.deepcopy(common), **eye}
        anims["0"]["frames"] = entrance_frames[name]
        scene["setups"]["0"]["materialAnims"] = anims
        put(f"scenes/linkspan_e/{name}/scene.json", scene)
        registry[f"linkspan_e/{name}"] = {
            "name": f"Link-Span E: {name}",
            "scene": f"scenes/linkspan_e/{name}/scene.json",
            "drawConfig": 0,
            "entrances": {"main": {"spawn": 0}},
        }

    # UNBOUND-011: sala sintética com um quad por receita, cada um lendo o segmento dela (8 a 13).
    make_lab(put, registry, field, doc("scenes/spot00/rooms/0.json")["setups"]["0"], field_collision)
    # UNBOUND-008/009: 300 salas, 300 entradas de malha, 200 objetos, 100 portas, 60 caixas DynaPoly e água na sala 299.
    make_many(put, registry, field, doc("scenes/spot00/rooms/0.json")["setups"]["0"], field_collision,
              object_ids(Path(__file__).resolve().parents[4] / "soh/include/tables/object_table.h"))
    # UNBOUND-021: recursos próprios nos extremos (piso no limite do mundo, índices de colisão acima dos tetos
    # vanilla e uma malha de 81 920 unidades numa display list só).
    make_edge(put, registry, field, doc("scenes/spot00/rooms/0.json")["setups"]["0"], field_collision)
    make_span(put, registry, field, doc("scenes/spot00/rooms/0.json")["setups"]["0"], field_collision)
    make_exits(put, registry, field, doc("scenes/spot00/rooms/0.json")["setups"]["0"], field_collision)
    make_r02(put, registry, field, doc("scenes/spot00/rooms/0.json")["setups"]["0"], field_collision)
    make_m12(put, registry, field, doc("scenes/spot00/rooms/0.json")["setups"]["0"], field_collision)

    put("unbound/scenes.json", registry)
    print(f"fixtures: {len(registry)} cenas em {out}")


LAB_DL = "scenes/linkspan_e/lab/quads"
LAB_TEXTURES = {
    "stripes": lambda x, y: (31, 28, 4) if (x // 4) % 2 == 0 else (6, 3, 12),
    "grid": lambda x, y: (31, 31, 31) if ((x // 8) + (y // 8)) % 2 == 0 else (20, 20, 20),
    "red": lambda x, y: (31, 4, 4) if ((x // 8) + (y // 8)) % 2 == 0 else (18, 0, 0),
    "blue": lambda x, y: (4, 8, 31) if ((x // 8) + (y // 8)) % 2 == 0 else (0, 0, 18),
}
# (segmento, textura do quad ou None para a textura do próprio segmento). As faixas só variam em x: o
# segmento 9 rola a camada 0 em y e precisa da grade para a rolagem aparecer.
LAB_QUADS = [(8, "stripes"), (9, "grid"), (10, "grid"), (11, "grid"), (12, "grid"), (13, None)]


def rgba16_texture(pixel):
    """Textura RGBA16 32x32 no formato OTEX (dados big-endian, como os do jogo)."""
    header = bytes.fromhex("00000000" "5845544f" "00000000" "efbeadde" "efbeadde").ljust(0x40, b"\0")
    data = bytearray()
    for y in range(32):
        for x in range(32):
            r, g, b = pixel(x, y)
            data += struct.pack(">H", (r << 11) | (g << 6) | (b << 1) | 1)
    return header + struct.pack("<IIII", 2, 32, 32, len(data)) + bytes(data)


def xml_tile(tile, line):
    return (f'<SetTile Format="G_IM_FMT_RGBA" Size="G_IM_SIZ_16b" Line="{line}" TMem="0" Tile="{tile}" '
            'Palette="0" Cms0="G_TX_WRAP" Cms1="G_TX_NOMIRROR" Cmt0="G_TX_WRAP" Cmt1="G_TX_NOMIRROR" '
            'MaskS="5" MaskT="5" ShiftS="0" ShiftT="0"/>')


def lab_display_list():
    """Display list XML dos seis quads: textura, depois a chamada do segmento (scroll ou cor), depois os vértices."""
    lines = ['<DisplayList Version="0">', "<PipeSync/>",
             '<ClearGeometryMode G_LIGHTING="1" G_CULL_BACK="1" G_CULL_FRONT="1" G_TEXTURE_GEN="1" '
             'G_TEXTURE_GEN_LINEAR="1" G_FOG="1"/>',
             '<SetGeometryMode G_ZBUFFER="1" G_SHADE="1" G_SHADING_SMOOTH="1"/>',
             '<SetCycleType G_CYC_1CYCLE="1"/>',
             '<SetRenderMode Mode1="G_RM_AA_ZB_OPA_SURF" Mode2="G_RM_AA_ZB_OPA_SURF2"/>',
             '<SetCombineLERP A0="G_CCMUX_TEXEL0" B0="G_CCMUX_0" C0="G_CCMUX_PRIMITIVE" D0="G_CCMUX_0" '
             'Aa0="G_ACMUX_0" Ab0="G_ACMUX_0" Ac0="G_ACMUX_0" Ad0="G_ACMUX_1" A1="G_CCMUX_TEXEL0" B1="G_CCMUX_0" '
             'C1="G_CCMUX_PRIMITIVE" D1="G_CCMUX_0" Aa1="G_ACMUX_0" Ab1="G_ACMUX_0" Ac1="G_ACMUX_0" Ad1="G_ACMUX_1"/>',
             '<Texture S="65535" T="65535" Level="0" Tile="0" On="1"/>']
    for index, (segment, texture) in enumerate(LAB_QUADS):
        lines.append('<SetPrimColor M="0" L="0" R="255" G="255" B="255" A="255"/>')
        source = f"textures/linkspan_e/{texture}" if texture else f">0x{segment:02X}000000"
        lines += [f'<SetTextureImage Path="{source}" Format="G_IM_FMT_RGBA" Size="G_IM_SIZ_16b" Width="1"/>',
                  xml_tile(7, 0), "<LoadSync/>", '<LoadBlock Tile="7" Uls="0" Ult="0" Lrs="1023" Dxt="256"/>',
                  "<PipeSync/>", xml_tile(0, 8), '<SetTileSize T="0" Uls="0" Ult="0" Lrs="124" Lrt="124"/>']
        if texture:
            lines.append(f'<CallDisplayList Path=">0x{segment:02X}000000"/>')
        lines += [f'<LoadVertices Path="scenes/linkspan_e/lab/v{index}" Count="4" VertexBufferIndex="0" '
                  'VertexOffset="0"/>',
                  '<Triangle1 V00="0" V01="1" V02="2" Flag0="0"/>', '<Triangle1 V00="0" V01="2" V02="3" Flag0="0"/>']
    lines += ["<EndDisplayList/>", "</DisplayList>"]
    return "\n".join(lines).encode()


def lab_vertices(index):
    """Quad de 120x120 em pé em z=250, lado a lado ao longo de x; a textura repete duas vezes."""
    x0 = -420 + index * 140
    corners = [(x0, 10, 0, 2048), (x0 + 120, 10, 2048, 2048), (x0 + 120, 130, 2048, 0), (x0, 130, 0, 0)]
    rows = [f'<Vtx X="{x}" Y="{y}" Z="250" S="{s}" T="{t}" R="255" G="255" B="255" A="255"/>'
            for x, y, s, t in corners]
    return ("\n".join(['<Vertex Version="0">'] + rows + ["</Vertex>"])).encode()


def lab_material_anims():
    # UNBOUND-014: um quarto de texel por frame, visivelmente mais lento que um passo inteiro.
    layer = {"xStep": 0, "yStep": 0, "xSpeed": 0.25, "width": 32, "height": 32}
    return {
        "0": {"segment": 8, "pass": "opa", "type": "texScroll", "layers": [layer]},
        "1": {"segment": 9, "pass": "opa", "type": "twoTexScroll",
              "layers": [{"xStep": 0, "yStep": 2, "width": 32, "height": 32}, layer]},
        "2": {"segment": 10, "pass": "opa", "type": "color", "length": 40, "keyFrames": [0, 20],
              "primColors": [[255, 40, 40, 255, 0], [40, 255, 40, 255, 0]],
              "envColors": [[0, 0, 0, 255], [0, 0, 0, 255]]},
        "3": {"segment": 11, "pass": "opa", "type": "colorLerp", "length": 60, "keyFrames": [0, 30, 60],
              "primColors": [[255, 40, 40, 255, 0], [40, 40, 255, 255, 0], [255, 40, 40, 255, 0]],
              "envColors": [[0, 0, 0, 255], [0, 0, 0, 255], [0, 0, 0, 255]]},
        "4": {"segment": 12, "pass": "opa", "type": "colorNonLinear", "length": 60, "keyFrames": [0, 20, 40, 60],
              "primColors": [[255, 255, 40, 255, 0], [40, 255, 255, 255, 0], [255, 40, 255, 255, 0],
                             [255, 255, 40, 255, 0]],
              "envColors": [[0, 0, 0, 255]] * 4},
        "5": {"segment": 13, "pass": "opa", "type": "texCycle",
              "textures": ["textures/linkspan_e/red", "textures/linkspan_e/blue"], "frames": [0] * 10 + [1] * 10},
    }


def outdoor_setup(field, spawn, rot_y, room):
    return {
        "sound": field["sound"],
        "cameraSettings": field["cameraSettings"],
        "specialObjects": field["specialObjects"],
        "skybox": field["skybox"],
        "lighting": copy.deepcopy(field["lighting"]),
        "spawns": {"0": {"id": 0, "pos": spawn, "rot": [0, rot_y, 0], "params": field["spawns"]["0"]["params"]}},
        "entrances": {"0": {"spawn": 0, "room": room}},
        "exits": {},
    }


def flat_collision(field_collision, water_boxes, *, bulk_file="scenes/linkspan_e/wide/collision.bin",
                   vertices=(WIDE_GRID + 1) ** 2, polys=2 * WIDE_GRID * WIDE_GRID, exit_surface=False):
    camera = dict(field_collision["cameras"]["0"], count=0, positionIndex=None)
    surfaces = {"0": {"camera": 0, "exit": 0, "floorType": 0, "wallFlags": 0, "wallType": 0,
                      "floorProperty": 0, "isSoft": 0, "isHorseBlocked": 0, "material": 0,
                      "floorEffect": 0, "lightSetting": 0, "echo": 0, "canHookshot": 0,
                      "conveyorSpeed": 0, "conveyorDirection": 0, "isWallDamage": 0}}
    # O valor da superfície é o índice da lista de exits mais um: 0 não tem saída; 1 usa exits[0].
    if exit_surface:
        surfaces["1"] = {**surfaces["0"], "exit": 1}
    return {
        "$schema": "unbound/collision/3",
        "bounds": {"min": [-WIDE_EXTENT, -10, -WIDE_EXTENT], "max": [WIDE_EXTENT, 10, WIDE_EXTENT]},
        "bulk": {"file": bulk_file, "vertices": vertices, "polys": polys},
        "surfaceTypes": surfaces,
        "cameras": {"0": camera},
        "cameraPositions": {},
        "waterBoxes": water_boxes,
    }


def base_room(field_room):
    return {key: copy.deepcopy(value) for key, value in field_room.items() if key not in ("actors", "objects", "mesh")}


LAB_EXIT_Z = (560, 760)


def lab_floor(with_exit):
    """Piso pequeno do lab; a faixa z=560..760 recebe a superfície de saída 1 quando solicitada."""
    xs = (-1200, -400, 400, 1200)
    zs = (-600, 0, 400, LAB_EXIT_Z[0], LAB_EXIT_Z[1], 1200)
    vertices = bytearray()
    for z in zs:
        for x in xs:
            vertices += struct.pack("<iii", x, 0, z)
    polys = bytearray()
    columns = len(xs)
    for row in range(len(zs) - 1):
        for column in range(columns - 1):
            a = row * columns + column
            b = a + columns
            c = a + 1
            d = b + 1
            surface = 1 if with_exit and (zs[row], zs[row + 1]) == LAB_EXIT_Z else 0
            for va, vb, vc in ((a, b, c), (c, b, d)):
                polys += struct.pack("<HHIIIhhhhi", surface, 0, va, vb, vc, 0, 32767, 0, 0, 0)
    return bytes(vertices + polys), len(xs) * len(zs), 2 * (len(xs) - 1) * (len(zs) - 1)


def make_lab(put, registry, field, field_room, field_collision):
    for name, pixel in LAB_TEXTURES.items():
        put(f"textures/linkspan_e/{name}", rgba16_texture(pixel))
    put(LAB_DL, lab_display_list())
    for index in range(len(LAB_QUADS)):
        put(f"scenes/linkspan_e/lab/v{index}", lab_vertices(index))
    setup = outdoor_setup(field, [0, 0, 0], 0, 0)
    setup["materialAnims"] = lab_material_anims()
    # Saída montada: a faixa em z=560..760 fica em linha reta à frente do spawn (que olha para +z).
    setup["exits"] = {"0": "linkspan_e/lab_b/main"}
    put("scenes/linkspan_e/lab/scene.json", {
        "$schema": "unbound/scene/1",
        "collision": "scenes/linkspan_e/lab/collision.json",
        "rooms": {"0": "scenes/linkspan_e/lab/rooms/0.json"},
        "setups": {"0": setup},
    })
    floor, vertices, polys = lab_floor(with_exit=True)
    put("scenes/linkspan_e/lab/collision.bin", floor)
    put("scenes/linkspan_e/lab/collision.json", flat_collision(
        field_collision, {}, bulk_file="scenes/linkspan_e/lab/collision.bin", vertices=vertices, polys=polys,
        exit_surface=True))
    room = base_room(field_room)
    room.update({"mesh": {"type": 0, "entries": {"0": {"opa": LAB_DL, "xlu": None}}}, "objects": {}, "actors": {}})
    put("scenes/linkspan_e/lab/rooms/0.json", {"$schema": "unbound/room/1", "setups": {"0": room}})
    registry["linkspan_e/lab"] = {"name": "Link-Span E: materiais", "scene": "scenes/linkspan_e/lab/scene.json",
                                  "drawConfig": 0, "horse": {"pos": [0, 0, -120], "angle": 0},
                                  "entrances": {"main": {"spawn": 0}}}

    # Destino da saída: mantém piso simples e registro de Epona para a comprovação da transição montada.
    lab_b_setup = outdoor_setup(field, [0, 0, 0], 0, 0)
    lab_b_setup["materialAnims"] = lab_material_anims()
    put("scenes/linkspan_e/lab_b/scene.json", {
        "$schema": "unbound/scene/1",
        "collision": "scenes/linkspan_e/lab_b/collision.json",
        "rooms": {"0": "scenes/linkspan_e/lab_b/rooms/0.json"},
        "setups": {"0": lab_b_setup},
    })
    floor, vertices, polys = lab_floor(with_exit=False)
    put("scenes/linkspan_e/lab_b/collision.bin", floor)
    put("scenes/linkspan_e/lab_b/collision.json", flat_collision(
        field_collision, {}, bulk_file="scenes/linkspan_e/lab_b/collision.bin", vertices=vertices, polys=polys))
    room = base_room(field_room)
    # Mesma malha do lab (segmentos 8 a 13 vêm das materialAnims acima), para a chegada ter referência visual.
    room.update({"mesh": {"type": 0, "entries": {"0": {"opa": LAB_DL, "xlu": None}}}, "objects": {}, "actors": {}})
    put("scenes/linkspan_e/lab_b/rooms/0.json", {"$schema": "unbound/room/1", "setups": {"0": room}})
    registry["linkspan_e/lab_b"] = {"name": "Link-Span E: chegada montada",
                                    "scene": "scenes/linkspan_e/lab_b/scene.json", "drawConfig": 0,
                                    "horse": {"pos": [0, 0, -120], "angle": 0},
                                    "entrances": {"main": {"spawn": 0}}}


MANY_ROOMS = 300
MANY_ROOM = 299
DOOR_ACTOR = 0x0009
CRATE_ACTOR = 0x01A0
CRATE_OBJECT = 0x0170


def object_ids(table):
    """Ids de objeto definidos (sem os keeps, que a cena já carrega como especiais)."""
    ids = []
    for line in table.read_text(encoding="utf-8").splitlines():
        match = re.match(r"/\* 0x([0-9A-Fa-f]+) \*/ DEFINE_OBJECT\(", line.strip())
        if match:
            ids.append(int(match.group(1), 16))
    return [i for i in ids if i > 0x0003 and i not in (0x0014, 0x0015, HEART_OBJECT, CRATE_OBJECT)]


def make_many(put, registry, field, field_room, field_collision, objects):
    spawn = [0, 0, -400]
    setup = outdoor_setup(field, spawn, 0, MANY_ROOM)
    setup["transitionActors"] = {
        str(i): {"front": {"room": MANY_ROOM, "effects": 0}, "back": {"room": MANY_ROOM, "effects": 0},
                 "id": DOOR_ACTOR, "pos": [700 + (i % 10) * 120, 0, -800 + (i // 10) * 120], "rotY": 0,
                 "params": 0}
        for i in range(100)}
    # A malha reusa a DL do lab, que lê os segmentos 8 a 13: sem as animações, o 0x0D fica sem textura.
    setup["materialAnims"] = lab_material_anims()
    room_path = "scenes/linkspan_e/many/room.json"
    put("scenes/linkspan_e/many/scene.json", {
        "$schema": "unbound/scene/1",
        "collision": "scenes/linkspan_e/many/collision.json",
        "rooms": {str(i): room_path for i in range(MANY_ROOMS)},
        "setups": {"0": setup},
    })
    # Água funda (superfície 100 acima do chão) à frente do spawn, só na sala 299.
    put("scenes/linkspan_e/many/collision.json", flat_collision(field_collision, {
        "0": {"xMin": -300, "ySurface": 100, "zMin": 0, "xLength": 600, "zLength": 600, "camera": 0,
              "lightSetting": 0, "room": MANY_ROOM, "notSwimmable": False}}))
    room = base_room(field_room)
    room["mesh"] = {"type": 0, "entries": {str(i): {"opa": LAB_DL, "xlu": None} for i in range(300)}}
    listed = objects[:198] + [CRATE_OBJECT, HEART_OBJECT]
    room["objects"] = {str(i): object_id for i, object_id in enumerate(listed)}
    actors = {"heart": {"id": HEART_ACTOR, "pos": [0, 0, -250], "rot": [0, 0, 0], "params": HEART_PARAMS}}
    for i in range(60):
        actors[f"crate{i}"] = {"id": CRATE_ACTOR, "pos": [-900 + (i % 6) * 120, 0, -800 + (i // 6) * 120],
                               "rot": [0, 0, 0], "params": 0}
    room["actors"] = actors
    put(room_path, {"$schema": "unbound/room/1", "setups": {"0": room}})
    registry["linkspan_e/many"] = {"name": "Link-Span E: limites", "scene": "scenes/linkspan_e/many/scene.json",
                                   "drawConfig": 0, "entrances": {"main": {"spawn": 0}}}


# UNBOUND-021 (Prelude, prioridade 5 da matriz de limites): cenas só com recursos nossos.
# BGCHECK_XYZ_ABSMAX. O BgCheck só registra a posição como inválida (osSyncPrintf, desligado no build) e a colisão
# continua; o que muda de fato é o EffectSs (poeira, faíscas, respingos), apagado com |c| > 1 048 576.
EDGE_LIMIT = 1048576
EDGE_SPAWN = 1048500   # 76 unidades antes do limite, em x e em z
EDGE_FAR = 1048800     # o piso passa do limite
EDGE_STRIP = 1048000   # a faixa do spawn vai daqui até EDGE_FAR
EDGE_NEAR = 1040000
EDGE_VERTICES = 8193   # um além do índice de 13 bits do vanilla (0x1FFF): o último vértice é o 8192
EDGE_POLYS = 32769     # um além de s16: o último polígono é o 32768
SPAN_EXTENT = 40960    # malha de -40 960 a +40 960 (81 920 > 65 535, o alcance de um vértice s16)
SPAN_SPAWN = 40000
SPAN_WIDTH = 600


def axis_range(sign, near, far):
    """Faixa [near, far] do lado `sign` do eixo, em ordem crescente."""
    return (near, far) if sign > 0 else (-far, -near)


def quad_polys(a, b, c, d):
    """Dois triângulos de piso (normal +y) no padrão do floor_grid: a=(x0,z0) b=(x0,z1) c=(x1,z0) d=(x1,z1)."""
    return [struct.pack("<HHIIIhhhhi", 0, 0, va, vb, vc, 0, 32767, 0, 0, 0) for va, vb, vc in ((a, b, c), (c, b, d))]


def floor_vertices(xs, zs, repeats_s, repeats_t):
    """Quad de piso em y=0 como recurso Vertex XML; a textura de 32 texels repete repeats_s x repeats_t vezes."""
    s1, t1 = 1024 * repeats_s, 1024 * repeats_t
    corners = [(xs[0], zs[0], 0, 0), (xs[1], zs[0], s1, 0), (xs[1], zs[1], s1, t1), (xs[0], zs[1], 0, t1)]
    rows = [f'<Vtx X="{x}" Y="0" Z="{z}" S="{s}" T="{t}" R="255" G="255" B="255" A="255"/>'
            for x, z, s, t in corners]
    return ("\n".join(['<Vertex Version="0">'] + rows + ["</Vertex>"])).encode()


def wall_vertices(xs, zs, height):
    """Faixa vertical de `height` unidades entre (xs[0], zs[0]) e (xs[1], zs[1])."""
    corners = [(xs[0], 0, zs[0], 0, 1024), (xs[1], 0, zs[1], 1024 * 31, 1024),
               (xs[1], height, zs[1], 1024 * 31, 0), (xs[0], height, zs[0], 0, 0)]
    rows = [f'<Vtx X="{x}" Y="{y}" Z="{z}" S="{s}" T="{t}" R="255" G="255" B="255" A="255"/>'
            for x, y, z, s, t in corners]
    return ("\n".join(['<Vertex Version="0">'] + rows + ["</Vertex>"])).encode()


def quads_display_list(parts):
    """DL XML opaca, sem culling: para cada parte, a textura RGBA16 32x32 e o quad de 4 vértices do recurso."""
    lines = ['<DisplayList Version="0">', "<PipeSync/>",
             '<ClearGeometryMode G_LIGHTING="1" G_CULL_BACK="1" G_CULL_FRONT="1" G_TEXTURE_GEN="1" '
             'G_TEXTURE_GEN_LINEAR="1" G_FOG="1"/>',
             '<SetGeometryMode G_ZBUFFER="1" G_SHADE="1" G_SHADING_SMOOTH="1"/>',
             '<SetCycleType G_CYC_1CYCLE="1"/>',
             '<SetRenderMode Mode1="G_RM_AA_ZB_OPA_SURF" Mode2="G_RM_AA_ZB_OPA_SURF2"/>',
             '<SetCombineLERP A0="G_CCMUX_TEXEL0" B0="G_CCMUX_0" C0="G_CCMUX_PRIMITIVE" D0="G_CCMUX_0" '
             'Aa0="G_ACMUX_0" Ab0="G_ACMUX_0" Ac0="G_ACMUX_0" Ad0="G_ACMUX_1" A1="G_CCMUX_TEXEL0" B1="G_CCMUX_0" '
             'C1="G_CCMUX_PRIMITIVE" D1="G_CCMUX_0" Aa1="G_ACMUX_0" Ab1="G_ACMUX_0" Ac1="G_ACMUX_0" Ad1="G_ACMUX_1"/>',
             '<Texture S="65535" T="65535" Level="0" Tile="0" On="1"/>',
             '<SetPrimColor M="0" L="0" R="255" G="255" B="255" A="255"/>']
    for texture, vertices in parts:
        lines += [f'<SetTextureImage Path="textures/linkspan_e/{texture}" Format="G_IM_FMT_RGBA" Size="G_IM_SIZ_16b" '
                  'Width="1"/>',
                  xml_tile(7, 0), "<LoadSync/>", '<LoadBlock Tile="7" Uls="0" Ult="0" Lrs="1023" Dxt="256"/>',
                  "<PipeSync/>", xml_tile(0, 8), '<SetTileSize T="0" Uls="0" Ult="0" Lrs="124" Lrt="124"/>',
                  f'<LoadVertices Path="{vertices}" Count="4" VertexBufferIndex="0" VertexOffset="0"/>',
                  '<Triangle1 V00="0" V01="1" V02="2" Flag0="0"/>', '<Triangle1 V00="0" V01="2" V02="3" Flag0="0"/>']
    lines += ["<EndDisplayList/>", "</DisplayList>"]
    return "\n".join(lines).encode()


def edge_collision(sx, sz):
    """collision.bin do canto (sx, sz) do mundo com exatamente EDGE_VERTICES vértices e EDGE_POLYS polígonos.

    O vértice 0 fica reservado; o enchimento (grade 91x89 em x de EDGE_NEAR a EDGE_STRIP, repetida até 32 767
    polígonos, e 89 vértices sem uso) vem depois. A faixa do spawn, de EDGE_STRIP a EDGE_FAR em x, usa só os vértices
    8189 a 8192 e os polígonos 32767 e 32768. O chão sob o spawn é o polígono 32768, com o vértice 8192 no vIA, o
    índice que passa pela máscara de 13 bits do vanilla (COLPOLY_VTX_INDEX). Com a máscara, 8192 viraria o vértice 0,
    que repete outro vértice do mesmo triângulo: o triângulo degenera e o spawn fica sem chão (conferido abaixo).
    Com o índice de polígono em s16, o 32768 não seria alcançado."""
    fill_x = axis_range(sx, EDGE_NEAR, EDGE_STRIP)
    strip_x = axis_range(sx, EDGE_STRIP, EDGE_FAR)
    zs = axis_range(sz, EDGE_NEAR, EDGE_FAR)
    columns, rows = 91, 89
    points = [None]  # vértice 0: cópia de um vértice do triângulo do spawn, definida mais abaixo
    for row in range(rows):
        for column in range(columns):
            points.append((fill_x[0] + (fill_x[1] - fill_x[0]) * column // (columns - 1),
                           zs[0] + (zs[1] - zs[0]) * row // (rows - 1)))
    points += [(fill_x[0], zs[0])] * (EDGE_VERTICES - 4 - len(points))
    fill = []
    for row in range(rows - 1):
        for column in range(columns - 1):
            a = 1 + row * columns + column
            fill.append((a, a + columns, a + 1))
            fill.append((a + 1, a + columns, a + columns + 1))
    polys = [fill[i % len(fill)] for i in range(EDGE_POLYS - 2)]
    # O triângulo da faixa que fica sob o spawn é o último polígono, e o vértice só dele é o último vértice.
    corners = {"a": (strip_x[0], zs[0]), "b": (strip_x[0], zs[1]), "c": (strip_x[1], zs[0]), "d": (strip_x[1], zs[1])}
    spawn = (sx * EDGE_SPAWN, sz * EDGE_SPAWN)

    def side(p, q, r):
        return (p[0] - r[0]) * (q[1] - r[1]) - (q[0] - r[0]) * (p[1] - r[1])

    def strictly_inside(v1, v2, v3):
        signs = (side(spawn, v1, v2), side(spawn, v2, v3), side(spawn, v3, v1))
        return all(s > 0 for s in signs) or all(s < 0 for s in signs)

    spawn_tri, other_tri = ("abc", "cbd") if strictly_inside(*(corners[k] for k in "abc")) else ("cbd", "abc")
    order = [(set(other_tri) - set(spawn_tri)).pop(), "b", "c", (set(spawn_tri) - set(other_tri)).pop()]
    index = {key: EDGE_VERTICES - 4 + i for i, key in enumerate(order)}
    points += [corners[key] for key in order]
    # Rotação cíclica (mantém o winding) que põe o 8192 no vIA.
    last = [index[k] for k in spawn_tri]
    turn = last.index(EDGE_VERTICES - 1)
    last = last[turn:] + last[:turn]
    points[0] = points[last[1]]
    polys += [tuple(index[k] for k in other_tri), tuple(last)]

    def floor_under_spawn(tris):
        return [i for i, tri in enumerate(tris) if strictly_inside(*(points[v] for v in tri))]

    assert len(points) == EDGE_VERTICES and len(polys) == EDGE_POLYS
    assert floor_under_spawn(polys) == [EDGE_POLYS - 1]
    # Mutação: a máscara de 13 bits do vanilla no vIA e no vIB tira o chão do spawn.
    masked = [(a & 0x1FFF, b & 0x1FFF, c) for a, b, c in polys]
    assert floor_under_spawn(masked) == []
    vertices = b"".join(struct.pack("<iii", x, 0, z) for x, z in points)
    return vertices + b"".join(struct.pack("<HHIIIhhhhi", 0, 0, a, b, c, 0, 32767, 0, 0, 0) for a, b, c in polys)


def make_edge(put, registry, field, field_room, field_collision):
    """M01/M13/M14: piso no extremo do mundo, nos cantos (+x, -z) e (-x, +z). O spawn fica 76 unidades antes do
    limite nos dois eixos, olhando para fora em x; o piso continua além do limite (a colisão vale até o fim dele)."""
    for name, pixel in LAB_TEXTURES.items():
        put(f"textures/linkspan_e/{name}", rgba16_texture(pixel))
    for name, sx, sz in (("edge_pos", 1, -1), ("edge_neg", -1, 1)):
        folder = f"scenes/linkspan_e/{name}"
        fill_x = axis_range(sx, EDGE_NEAR, EDGE_STRIP)
        strip_x = axis_range(sx, EDGE_STRIP, EDGE_FAR)
        zs = axis_range(sz, EDGE_NEAR, EDGE_FAR)
        xs = (min(fill_x + strip_x), max(fill_x + strip_x))
        limit_x = sx * EDGE_LIMIT
        limit_z = sz * EDGE_LIMIT
        put(f"{folder}/v_fill", floor_vertices(fill_x, zs, 31, 31))
        put(f"{folder}/v_strip", floor_vertices(strip_x, zs, 3, 31))
        # Marcas do limite: faixa azul em x = ±1 048 576 e em z = ±1 048 576, onde os efeitos EffectSs somem.
        put(f"{folder}/v_limit_x", wall_vertices((limit_x, limit_x), zs, 40))
        put(f"{folder}/v_limit_z", wall_vertices(xs, (limit_z, limit_z), 40))
        put(f"{folder}/floor", quads_display_list([("grid", f"{folder}/v_fill"), ("red", f"{folder}/v_strip"),
                                                   ("blue", f"{folder}/v_limit_x"), ("blue", f"{folder}/v_limit_z")]))
        spawn = [sx * EDGE_SPAWN, 0, sz * EDGE_SPAWN]
        setup = outdoor_setup(field, spawn, 16384 * sx, 0)
        put(f"{folder}/scene.json", {
            "$schema": "unbound/scene/1",
            "collision": f"{folder}/collision.json",
            "rooms": {"0": f"{folder}/rooms/0.json"},
            "setups": {"0": setup},
        })
        put(f"{folder}/collision.bin", edge_collision(sx, sz))
        collision = flat_collision(field_collision, {}, bulk_file=f"{folder}/collision.bin",
                                   vertices=EDGE_VERTICES, polys=EDGE_POLYS)
        collision["bounds"] = {"min": [xs[0], -10, zs[0]], "max": [xs[1], 10, zs[1]]}
        put(f"{folder}/collision.json", collision)
        room = base_room(field_room)
        room.update({"mesh": {"type": 0, "entries": {"0": {"opa": f"{folder}/floor", "xlu": None}}},
                     "objects": {}, "actors": {}})
        put(f"{folder}/rooms/0.json", {"$schema": "unbound/room/1", "setups": {"0": room}})
        registry[f"linkspan_e/{name}"] = {"name": f"Link-Span E: limite do mundo ({name})",
                                          "scene": f"{folder}/scene.json", "drawConfig": 0,
                                          "entrances": {"main": {"spawn": 0}}}


def make_span(put, registry, field, field_room, field_collision):
    """M02: uma display list com um quad de 81 920 unidades (vértices em x = ±40 960) e o piso de colisão igual.
    Com vértice s16, x = 40 960 viraria -24 576 e o trecho sob os spawns, em x = ±40 000, não seria desenhado."""
    folder = "scenes/linkspan_e/span"
    xs = (-SPAN_EXTENT, SPAN_EXTENT)
    zs = (-SPAN_WIDTH, SPAN_WIDTH)
    put(f"{folder}/v_floor", floor_vertices(xs, zs, 31, 1))
    put(f"{folder}/floor", quads_display_list([("stripes", f"{folder}/v_floor")]))
    setup = outdoor_setup(field, [SPAN_SPAWN, 0, 0], -16384, 0)
    # Segundo spawn na outra ponta, olhando para +x.
    setup["spawns"]["1"] = dict(setup["spawns"]["0"], pos=[-SPAN_SPAWN, 0, 0], rot=[0, 16384, 0])
    setup["entrances"]["1"] = {"spawn": 1, "room": 0}
    put(f"{folder}/scene.json", {
        "$schema": "unbound/scene/1",
        "collision": f"{folder}/collision.json",
        "rooms": {"0": f"{folder}/rooms/0.json"},
        "setups": {"0": setup},
    })
    vertices = b"".join(struct.pack("<iii", x, 0, z) for x, z in ((xs[0], zs[0]), (xs[0], zs[1]),
                                                                  (xs[1], zs[0]), (xs[1], zs[1])))
    put(f"{folder}/collision.bin", vertices + b"".join(quad_polys(0, 1, 2, 3)))
    collision = flat_collision(field_collision, {}, bulk_file=f"{folder}/collision.bin", vertices=4, polys=2)
    collision["bounds"] = {"min": [xs[0], -10, zs[0]], "max": [xs[1], 10, zs[1]]}
    put(f"{folder}/collision.json", collision)
    room = base_room(field_room)
    room.update({"mesh": {"type": 0, "entries": {"0": {"opa": f"{folder}/floor", "xlu": None}}},
                 "objects": {}, "actors": {}})
    put(f"{folder}/rooms/0.json", {"$schema": "unbound/room/1", "setups": {"0": room}})
    registry["linkspan_e/span"] = {"name": "Link-Span E: malha de 81 920 unidades", "scene": f"{folder}/scene.json",
                                   "drawConfig": 0, "entrances": {"main": {"spawn": 0}, "oeste": {"spawn": 1}}}


EXITS_COUNT = 33   # um além do teto de 5 bits do vanilla (o valor 31 da superfície é o último que cabe)


def make_exits(put, registry, field, field_room, field_collision):
    """M16: 33 saídas por nome e a faixa de saída do piso com exit = 33, que usa exits[32] (o valor da superfície é o
    índice mais um). exits[32] leva à span (x = +40 000); as outras 32 levam à casa. Com o campo de 5 bits do vanilla,
    33 viraria 1 (exits[0]) e a chegada seria na casa. A faixa vermelha à frente do spawn é a saída."""
    folder = "scenes/linkspan_e/exits"
    xs = (-1200, 1200)
    put(f"{folder}/v_floor", floor_vertices(xs, (-600, LAB_EXIT_Z[0]), 8, 4))
    put(f"{folder}/v_exit", floor_vertices(xs, LAB_EXIT_Z, 8, 1))
    put(f"{folder}/v_after", floor_vertices(xs, (LAB_EXIT_Z[1], 1200), 8, 2))
    put(f"{folder}/floor", quads_display_list([("grid", f"{folder}/v_floor"), ("red", f"{folder}/v_exit"),
                                               ("grid", f"{folder}/v_after")]))
    setup = outdoor_setup(field, [0, 0, 0], 0, 0)
    setup["exits"] = {str(i): "linkspan_e/house/main" for i in range(EXITS_COUNT - 1)}
    setup["exits"][str(EXITS_COUNT - 1)] = "linkspan_e/span/main"
    put(f"{folder}/scene.json", {
        "$schema": "unbound/scene/1",
        "collision": f"{folder}/collision.json",
        "rooms": {"0": f"{folder}/rooms/0.json"},
        "setups": {"0": setup},
    })
    floor, vertices, polys = lab_floor(with_exit=True)
    put(f"{folder}/collision.bin", floor)
    collision = flat_collision(field_collision, {}, bulk_file=f"{folder}/collision.bin", vertices=vertices,
                               polys=polys, exit_surface=True)
    collision["surfaceTypes"]["1"]["exit"] = EXITS_COUNT
    put(f"{folder}/collision.json", collision)
    room = base_room(field_room)
    room.update({"mesh": {"type": 0, "entries": {"0": {"opa": f"{folder}/floor", "xlu": None}}},
                 "objects": {}, "actors": {}})
    put(f"{folder}/rooms/0.json", {"$schema": "unbound/room/1", "setups": {"0": room}})
    registry["linkspan_e/exits"] = {"name": "Link-Span E: 33 saídas", "scene": f"{folder}/scene.json",
                                    "drawConfig": 0, "entrances": {"main": {"spawn": 0}}}


# R02: o LoadVertices XML vira G_VTX_OTR_FILEPATH. A fábrica junta VertexBufferIndex e VertexOffset num OR sem
# máscara ((buffer << 16) | offset) e o interpretador lê a origem em 16 bits (w1 & 0xFFFF) e o destino em w1 >> 16.
# Com o buffer 1 em todas as cargas, o bit 16 do offset 65 536 cai no próprio destino e só a origem dá a volta.
R02_VERTICES = 65540
R02_QUADS = (  # offset no recurso, centro (x, y) do quad em z = 400, cor primitiva da carga (None: nenhuma carga)
    (0, (0, 330), None),               # W: só aparece (verde) se a origem do C der a volta para 0
    (4, (-240, 150), (255, 0, 0)),     # A
    (65532, (0, 150), (0, 0, 255)),    # B
    (65536, (240, 150), (0, 255, 0)),  # C
)
FIXTURE_Z = 400


def vertical_quad_rows(x, y, z, half):
    corners = [(x - half, y - half, 0, 1024), (x + half, y - half, 1024, 1024), (x + half, y + half, 1024, 0),
               (x - half, y + half, 0, 0)]
    return [f'<Vtx X="{vx}" Y="{vy}" Z="{z}" S="{s}" T="{t}" R="255" G="255" B="255" A="255"/>'
            for vx, vy, s, t in corners]


def vertex_resource(rows):
    return ("\n".join(['<Vertex Version="0">'] + rows + ["</Vertex>"])).encode()


def prim_quads_display_list(loads, buffer_index=0):
    """DL XML opaca sem textura: cada carga (recurso, offset, cor) desenha um quad na cor primitiva, sem depender da
    cor dos vértices lidos."""
    lines = ['<DisplayList Version="0">', "<PipeSync/>",
             '<ClearGeometryMode G_LIGHTING="1" G_CULL_BACK="1" G_CULL_FRONT="1" G_TEXTURE_GEN="1" '
             'G_TEXTURE_GEN_LINEAR="1" G_FOG="1"/>',
             '<SetGeometryMode G_ZBUFFER="1" G_SHADE="1" G_SHADING_SMOOTH="1"/>',
             '<SetCycleType G_CYC_1CYCLE="1"/>',
             '<SetRenderMode Mode1="G_RM_AA_ZB_OPA_SURF" Mode2="G_RM_AA_ZB_OPA_SURF2"/>',
             '<Texture S="0" T="0" Level="0" Tile="0" On="0"/>',
             '<SetCombineLERP A0="G_CCMUX_0" B0="G_CCMUX_0" C0="G_CCMUX_0" D0="G_CCMUX_PRIMITIVE" '
             'Aa0="G_ACMUX_0" Ab0="G_ACMUX_0" Ac0="G_ACMUX_0" Ad0="G_ACMUX_1" A1="G_CCMUX_0" B1="G_CCMUX_0" '
             'C1="G_CCMUX_0" D1="G_CCMUX_PRIMITIVE" Aa1="G_ACMUX_0" Ab1="G_ACMUX_0" Ac1="G_ACMUX_0" Ad1="G_ACMUX_1"/>']
    v = buffer_index
    for path, offset, (r, g, b) in loads:
        lines += ["<PipeSync/>", f'<SetPrimColor M="0" L="0" R="{r}" G="{g}" B="{b}" A="255"/>',
                  f'<LoadVertices Path="{path}" Count="4" VertexBufferIndex="{v}" VertexOffset="{offset}"/>',
                  f'<Triangle1 V00="{v}" V01="{v + 1}" V02="{v + 2}" Flag0="0"/>',
                  f'<Triangle1 V00="{v}" V01="{v + 2}" V02="{v + 3}" Flag0="0"/>']
    lines += ["<EndDisplayList/>", "</DisplayList>"]
    return "\n".join(lines).encode()


def r02_vertex_rows():
    # Enchimento abaixo do piso e sem repetir linhas: o validador de pacote recusa entrada com razão de compressão
    # acima de 200:1, e 65 536 linhas iguais passariam disso.
    rows = [f'<Vtx X="{i % 1000}" Y="-2000" Z="{i // 1000}" S="0" T="0" R="255" G="255" B="255" A="255"/>'
            for i in range(R02_VERTICES)]
    for offset, (x, y), _ in R02_QUADS:
        rows[offset:offset + 4] = vertical_quad_rows(x, y, FIXTURE_Z, 75)
    assert len(rows) == R02_VERTICES
    return rows


def r02_decode(buffer_index, offset):
    """(destino, origem) que o interpretador tira do w1 montado pela fábrica."""
    word = (buffer_index << 16) | offset
    return word >> 16, word & 0xFFFF


def small_scene(put, registry, field, field_room, field_collision, key, name, mesh):
    """Cena de uma sala sobre o piso do lab (sem saída); spawn em z = -500 olhando para +z."""
    folder = f"scenes/linkspan_e/{key}"
    setup = outdoor_setup(field, [0, 0, -500], 0, 0)
    put(f"{folder}/scene.json", {
        "$schema": "unbound/scene/1",
        "collision": f"{folder}/collision.json",
        "rooms": {"0": f"{folder}/rooms/0.json"},
        "setups": {"0": setup},
    })
    floor, vertices, polys = lab_floor(with_exit=False)
    put(f"{folder}/collision.bin", floor)
    put(f"{folder}/collision.json", flat_collision(field_collision, {}, bulk_file=f"{folder}/collision.bin",
                                                   vertices=vertices, polys=polys))
    room = base_room(field_room)
    room.update({"mesh": mesh, "objects": {}, "actors": {}})
    put(f"{folder}/rooms/0.json", {"$schema": "unbound/room/1", "setups": {"0": room}})
    registry[f"linkspan_e/{key}"] = {"name": name, "scene": f"{folder}/scene.json", "drawConfig": 0,
                                     "entrances": {"main": {"spawn": 0}}}


def make_r02(put, registry, field, field_room, field_collision):
    """R02: um recurso Vertex XML de 65 540 vértices e uma DL com três cargas: A (offset 4, vermelho), B (65 532,
    azul) e C (65 536, verde). No host atual a origem do C dá a volta para 0 e o verde sai em W, acima do B, com o
    lugar do C vazio; corrigido o transporte, o verde sai no C e o W não aparece."""
    folder = "scenes/linkspan_e/r02"
    put(f"{folder}/vertices", vertex_resource(r02_vertex_rows()))
    loads = [(f"{folder}/vertices", offset, color) for offset, _, color in R02_QUADS if color]
    assert [r02_decode(1, offset) for _, offset, _ in loads] == [(1, 4), (1, 65532), (1, 0)]
    put(f"{folder}/quads", prim_quads_display_list(loads, buffer_index=1))
    put(f"{folder}/v_floor", floor_vertices((-1200, 1200), (-600, 1200), 8, 6))
    put(f"{folder}/floor", quads_display_list([("grid", f"{folder}/v_floor")]))
    mesh = {"type": 0, "entries": {"0": {"opa": f"{folder}/floor", "xlu": None},
                                   "1": {"opa": f"{folder}/quads", "xlu": None}}}
    small_scene(put, registry, field, field_room, field_collision, "r02", "Link-Span E: 65 540 vértices", mesh)


M12_SORT_MAX = 1024  # SHAPE_SORT_MAX do z_room.c: a malha tipo 2 só considera as primeiras entradas


def make_m12(put, registry, field, field_room, field_collision):
    """M12: malha tipo 2 com 1 024 e 1 025 entradas. A entrada 0 é o piso, as do meio são uma DL vazia e as duas
    últimas são um quad amarelo (x = -150) e um magenta (x = +150; olhando para +z, o +x fica à esquerda). Com
    1 024 os dois aparecem; com 1 025 a entrada 1 024 (magenta) fica fora do corte, que vem antes do teste de
    distância. As do meio ficam atrás da câmera: contam no corte, mas não entram na lista ordenada de desenho."""
    folder = "scenes/linkspan_e/m12"
    put(f"{folder}/v_floor", floor_vertices((-1200, 1200), (-600, 1200), 8, 6))
    put(f"{folder}/floor", quads_display_list([("grid", f"{folder}/v_floor")]))
    put(f"{folder}/empty", b'<DisplayList Version="0">\n<EndDisplayList/>\n</DisplayList>')
    marks = {"yellow": (-150, (255, 255, 0)), "magenta": (150, (255, 0, 255))}
    for mark, (x, color) in marks.items():
        put(f"{folder}/v_{mark}", vertex_resource(vertical_quad_rows(x, 150, FIXTURE_Z, 60)))
        put(f"{folder}/{mark}", prim_quads_display_list([(f"{folder}/v_{mark}", 0, color)]))
    for count in (M12_SORT_MAX, M12_SORT_MAX + 1):
        entries = {str(i): {"pos": [0, 0, -30000], "radius": 1, "opa": f"{folder}/empty", "xlu": None}
                   for i in range(count)}
        entries["0"] = {"pos": [0, 0, 0], "radius": 3000, "opa": f"{folder}/floor", "xlu": None}
        for index, mark in ((count - 2, "yellow"), (count - 1, "magenta")):
            entries[str(index)] = {"pos": [marks[mark][0], 150, FIXTURE_Z], "radius": 120,
                                   "opa": f"{folder}/{mark}", "xlu": None}
        small_scene(put, registry, field, field_room, field_collision, f"m12_{count}",
                    f"Link-Span E: malha tipo 2 com {count} entradas", {"type": 2, "entries": entries})


def title_card(text):
    """Textura I8 144x24 no formato OTEX do SoH (o dos títulos vanilla)."""
    image = Image.new("L", (144, 24), 0)
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default(size=14)
    left, top, right, bottom = draw.textbbox((0, 0), text, font=font)
    draw.text(((144 - (right - left)) // 2 - left, (24 - (bottom - top)) // 2 - top), text, fill=255, font=font)
    header = bytes.fromhex("00000000" "5845544f" "00000000" "efbeadde" "efbeadde").ljust(0x40, b"\0")
    data = image.tobytes()
    return header + struct.pack("<IIII", 8, 144, 24, len(data)) + data


def floor_grid(cells, extent):
    """Chão plano em y=0 (collision.bin, SPEC §4.4.1): vértices s32 e polígonos de 28 bytes, normal +y."""
    step = 2 * extent // cells
    vertices = bytearray()
    for row in range(cells + 1):
        for column in range(cells + 1):
            vertices += struct.pack("<iii", -extent + column * step, 0, -extent + row * step)
    polys = bytearray()
    for row in range(cells):
        for column in range(cells):
            a = row * (cells + 1) + column  # (x0, z0)
            b = a + cells + 1                # (x0, z1)
            c = a + 1                        # (x1, z0)
            d = b + 1                        # (x1, z1)
            for va, vb, vc in ((a, b, c), (c, b, d)):
                polys += struct.pack("<HHIIIhhhhi", 0, 0, va, vb, vc, 0, 32767, 0, 0, 0)
    return bytes(vertices), bytes(polys)


if __name__ == "__main__":
    main()
