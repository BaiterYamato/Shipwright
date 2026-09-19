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
    layer = {"xStep": 3, "yStep": 0, "width": 32, "height": 32}
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


def flat_collision(field_collision, water_boxes):
    camera = dict(field_collision["cameras"]["0"], count=0, positionIndex=None)
    return {
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
        "waterBoxes": water_boxes,
    }


def base_room(field_room):
    return {key: copy.deepcopy(value) for key, value in field_room.items() if key not in ("actors", "objects", "mesh")}


def make_lab(put, registry, field, field_room, field_collision):
    for name, pixel in LAB_TEXTURES.items():
        put(f"textures/linkspan_e/{name}", rgba16_texture(pixel))
    put(LAB_DL, lab_display_list())
    for index in range(len(LAB_QUADS)):
        put(f"scenes/linkspan_e/lab/v{index}", lab_vertices(index))
    setup = outdoor_setup(field, [0, 0, 0], 0, 0)
    setup["materialAnims"] = lab_material_anims()
    put("scenes/linkspan_e/lab/scene.json", {
        "$schema": "unbound/scene/1",
        "collision": "scenes/linkspan_e/lab/collision.json",
        "rooms": {"0": "scenes/linkspan_e/lab/rooms/0.json"},
        "setups": {"0": setup},
    })
    put("scenes/linkspan_e/lab/collision.json", flat_collision(field_collision, {}))
    room = base_room(field_room)
    room.update({"mesh": {"type": 0, "entries": {"0": {"opa": LAB_DL, "xlu": None}}}, "objects": {}, "actors": {}})
    put("scenes/linkspan_e/lab/rooms/0.json", {"$schema": "unbound/room/1", "setups": {"0": room}})
    registry["linkspan_e/lab"] = {"name": "Link-Span E: materiais", "scene": "scenes/linkspan_e/lab/scene.json",
                                  "drawConfig": 0, "entrances": {"main": {"spawn": 0}}}


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
