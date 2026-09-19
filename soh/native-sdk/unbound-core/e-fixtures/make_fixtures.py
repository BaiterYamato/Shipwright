#!/usr/bin/env python3
"""Gera assets/ das fixtures da fase E do Unbound (UNBOUND-007 a 011).

Uso: python make_fixtures.py <oot-unbound.o2r> <pasta-assets>

As cenas copiadas (casa do Link, Dodongo's Cavern, luzes do Hyrule Field) saem da base convertida da
máquina de quem roda o script: dados do jogo nunca entram no repositório nem no pacote publicado. A cena
ampla (colisão, atores) e o título são sintéticos.
"""
import copy
import json
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

    put("unbound/scenes.json", registry)
    print(f"fixtures: {len(registry)} cenas em {out}")


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
