#!/usr/bin/env python3
"""Gera as camadas de prova da UNBOUND-020, 022 e 024: documentos Unbound nos tetos da matriz do Prelude.

Uso: make-unbound-limit-fixtures.py <pasta-de-saida>   (grava UB020-A.o2r, UB022-A.o2r, UB024-A.o2r, UB025-A.o2r e UB028-A.o2r)

Os documentos ficam fora de scenes/ e do registro, entao o jogo nao os carrega: so a checagem do game.ready
(UNBOUND-019) os mescla e transcodifica, e o tempo dela sai no resumo (ms=). Sozinha em mods/, o esperado e
"referencias: documentos=3 conferidas=3 ausentes=0 recusados=0 notas=5":
  custom/ub020/cena.json     32 769 salas (uma acima do indice s16)            -> nota de salas
  custom/ub020/sala.json     65 536 atores, 1 025 objetos e 33 luzes           -> nota de atores, de objetos e de luzes
                             (desde a 0.6.9 a sala grava so os primeiros 8 192 atores)
  custom/ub020/colisao.json  65 535 surface types e water boxes (no teto) e uma posicao de camera fora de s16
                                                                               -> nota de camera
  custom/ub020/colisao.bin   bulk vazio, para a referencia resolver
  (desde a 0.6.6 a colisao tem uma camera: sem ela, as superficies com camera 0 dariam mais uma nota)
UB022-A (limite do mundo, BGCHECK_XYZ_ABSMAX = 1 048 576; alem dele o jogo apaga os EffectSs): sozinha em mods/,
o esperado e "referencias: documentos=3 conferidas=3 ausentes=0 recusados=0 notas=4":
  custom/ub022/cena.json     spawn em x = 1 048 577 e transition actor em y = -1 048 600   -> duas notas
  custom/ub022/sala.json     ator em z = -1 048 576,5 e outro em x = 1 048 576 (no limite)  -> uma nota
  custom/ub022/colisao.json  bounds ate x = 1 048 800                                      -> uma nota
UB024-A (cameras da colisao, 0.6.6): sozinha em mods/, o esperado e
"referencias: documentos=2 conferidas=2 ausentes=0 recusados=0 notas=6":
  custom/ub024/colisao.json  3 cameras; superficies com camera 3 e -1, water boxes com camera 5, 0 e -1 (0 e -1
                             sao "sem camera"), camera com positionIndex 4 e count 3 sobre 6 posicoes e camera
                             com count 3 sem positionIndex                                 -> quatro notas
  custom/ub024/grande.json   32 770 cameras, superficie com camera 32 768 (estado s16) e camera com count -1
                                                                                           -> duas notas
UB025-A (grafo cena <-> colisao e salas, 0.6.7): sozinha em mods/, o esperado sao cinco notas de grafo, uma lacuna
("grafo incompleto"), uma referencia ausente e as notas da spot04 vanilla em herdadas=:
  custom/ub025/cena.json     2 salas; setup 0 com 2 saidas; porta com room 5 e camera 3  -> nota de room de porta
                             e nota de camera de porta (a colisao tem 2 cameras)
  custom/ub025/colisao.json  superficie com exit 3 e water box na room 7                   -> nota de exit e de agua
  custom/ub025/lacuna.json   colisao que nenhum archive tem                                -> lacuna e ausente
  scenes/spot04/scene.json   delta: porta nova (setup 0) com room 40 numa cena de 3 salas  -> nota de room de porta;
                             os setups de cutscene vanilla (exits alem da lista) nao viram nota
UB028-A (minimapa e mapa-mundi, 0.6.8): sozinha em mods/, o esperado sao tres notas novas:
  scenes/ydan/scene.json         delta: salas 12 e 13 (14 de 13 do minimapa)        -> textura da dungeon seguinte na 13
  scenes/ice_doukutu/scene.json  delta: sala 12 (13 de 12; fim da lista)            -> textura alem da lista na 12
  custom/ub028/cena.json         worldMapArea -1, 23 e 22 nos setups 0, 1 e 2       -> nota de 2 setups (22 vale)
So JSON e bytes escritos aqui: nenhum dado do jogo entra no arquivo.
"""
import json
import os
import sys
import zipfile

MANIFEST = {"format": "unbound", "formatVersion": 2, "requires": {"formatVersion": 2}}


def positional(count, item):
    return {str(i): item for i in range(count)}


def documents():
    return {
        "custom/ub020/cena.json": {
            "$schema": "unbound/scene/1",
            "collision": "custom/ub020/colisao.json",
            "rooms": positional(32769, "custom/ub020/sala.json"),
            "setups": {"0": {}},
        },
        "custom/ub020/sala.json": {
            "$schema": "unbound/room/1",
            "setups": {"0": {
                "actors": positional(65536, {"id": 16, "pos": [0, 0, 0]}),
                "objects": positional(1025, 1),
                # UNBOUND-030: 33 luzes, uma além das 32 vagas do pool de luzes do host (z_lights.c).
                "lights": positional(33, {"type": 0, "pos": [0, 0, 0], "color": [255, 255, 255], "glow": 0,
                                          "radius": 100}),
            }},
        },
        "custom/ub020/colisao.json": {
            "$schema": "unbound/collision/3",
            "bounds": {"min": [0, 0, 0], "max": [1, 1, 1]},
            "bulk": {"file": "custom/ub020/colisao.bin", "vertices": 0, "polys": 0},
            "surfaceTypes": positional(65535, {}),
            "waterBoxes": positional(65535, {}),
            "cameras": {"0": {"sType": 1, "count": 0, "positionIndex": None}},
            "cameraPositions": {"0": [32767, 0, 0], "1": [40000, 0, 0]},
        },
    }


def camera_documents():
    camera = {"sType": 1, "count": 0, "positionIndex": None}
    return {
        "custom/ub024/colisao.json": {
            "$schema": "unbound/collision/3",
            "bounds": {"min": [0, 0, 0], "max": [1, 1, 1]},
            "bulk": {"file": "custom/ub024/colisao.bin", "vertices": 0, "polys": 0},
            "surfaceTypes": {"0": {"camera": 0}, "1": {"camera": 3}, "2": {"camera": -1}},
            "cameras": {"0": camera, "1": {"sType": 1, "count": 3, "positionIndex": 4},
                        "2": {"sType": 1, "count": 3, "positionIndex": None}},
            "cameraPositions": positional(6, [0, 0, 0]),
            # Água com câmera 0 ou -1 é "sem câmera" no jogo: só a 5 dá nota.
            "waterBoxes": {str(i): {"xMin": 0, "ySurface": 0, "zMin": 0, "xLength": 1, "zLength": 1, "camera": c}
                           for i, c in enumerate((5, 0, -1))},
        },
        "custom/ub024/grande.json": {
            "$schema": "unbound/collision/3",
            "bounds": {"min": [0, 0, 0], "max": [1, 1, 1]},
            "bulk": {"file": "custom/ub024/colisao.bin", "vertices": 0, "polys": 0},
            "surfaceTypes": {"0": {"camera": 32767}, "1": {"camera": 32768}},
            "cameras": dict(positional(32770, camera), **{"0": {"sType": 1, "count": -1, "positionIndex": None}}),
        },
    }


def world_limit_documents():
    return {
        "custom/ub022/cena.json": {
            "$schema": "unbound/scene/1",
            "collision": "custom/ub022/colisao.json",
            "rooms": {"0": "custom/ub022/sala.json"},
            "setups": {"0": {
                "spawns": {"0": {"id": 0, "pos": [1048577, 0, 0], "rot": [0, 0, 0], "params": 0}},
                "entrances": {"0": {"spawn": 0, "room": 0}},
                "transitionActors": {"0": {"id": 9, "pos": [0, -1048600, 0], "front": {"room": 0},
                                           "back": {"room": 0}}},
            }},
        },
        "custom/ub022/sala.json": {
            "$schema": "unbound/room/1",
            "setups": {"0": {"actors": {"0": {"id": 16, "pos": [0, 0, -1048576.5]},
                                        "1": {"id": 16, "pos": [1048576, 0, 0]}}}},
        },
        "custom/ub022/colisao.json": {
            "$schema": "unbound/collision/3",
            "bounds": {"min": [1040000, -10, -10], "max": [1048800, 10, 10]},
            "bulk": {"file": "custom/ub022/colisao.bin", "vertices": 0, "polys": 0},
        },
    }


def graph_documents():
    spawn = {"0": {"id": 0, "pos": [0, 0, 0], "rot": [0, 0, 0], "params": 0}}
    door = {"id": 9, "pos": [0, 0, 100], "rotY": 0, "params": 0,
            "front": {"room": 5, "effects": 3}, "back": {"room": 0, "effects": -1}}
    return {
        "custom/ub025/cena.json": {
            "$schema": "unbound/scene/1",
            "collision": "custom/ub025/colisao.json",
            "rooms": {"0": "custom/ub025/sala.json", "1": "custom/ub025/sala.json"},
            "setups": {"0": {"spawns": spawn, "entrances": {"0": {"spawn": 0, "room": 0}},
                             "exits": {"0": 0, "1": 0}, "transitionActors": {"0": door}}},
        },
        "custom/ub025/lacuna.json": {
            "$schema": "unbound/scene/1",
            "collision": "custom/ub025/nao-existe.json",
            "rooms": {"0": "custom/ub025/sala.json"},
            "setups": {"0": {"spawns": spawn, "entrances": {"0": {"spawn": 0, "room": 0}}}},
        },
        "custom/ub025/sala.json": {"$schema": "unbound/room/1", "setups": {"0": {}}},
        # Delta numa cena da base: a porta nova vira nota; as notas que a spot04 vanilla já dá ficam em herdadas=.
        "scenes/spot04/scene.json": {
            "$schema": "unbound/scene/1",
            "setups": {"0": {"transitionActors": {"2": {
                "id": 35, "pos": [0, 0, 0], "rotY": 0, "params": 319,
                "front": {"room": 40, "effects": 255}, "back": {"room": 0, "effects": 255}}}}},
        },
        "custom/ub025/colisao.json": {
            "$schema": "unbound/collision/3",
            "bounds": {"min": [0, 0, 0], "max": [1, 1, 1]},
            "bulk": {"file": "custom/ub025/colisao.bin", "vertices": 0, "polys": 0},
            "surfaceTypes": {"0": {"camera": 0, "exit": 0}, "1": {"camera": 0, "exit": 3}},
            "cameras": {"0": {"sType": 1, "count": 0, "positionIndex": None},
                        "1": {"sType": 1, "count": 0, "positionIndex": None}},
            "waterBoxes": {"0": {"xMin": 0, "ySurface": 0, "zMin": 0, "xLength": 1, "zLength": 1, "camera": 0,
                                 "room": 7}},
        },
    }


def map_documents():
    spawn = {"0": {"id": 0, "pos": [0, 0, 0], "rot": [0, 0, 0], "params": 0}}
    return {
        # Deltas em dungeons da base: salas que repetem uma sala vanilla, além da tabela do minimapa.
        "scenes/ydan/scene.json": {"$schema": "unbound/scene/1",
                                   "rooms": {"12": "scenes/ydan/rooms/0.json", "13": "scenes/ydan/rooms/0.json"}},
        "scenes/ice_doukutu/scene.json": {"$schema": "unbound/scene/1",
                                          "rooms": {"12": "scenes/ice_doukutu/rooms/0.json"}},
        "custom/ub028/cena.json": {
            "$schema": "unbound/scene/1",
            "collision": "custom/ub028/colisao.json",
            "rooms": {"0": "custom/ub028/sala.json"},
            "setups": {
                "0": {"spawns": spawn, "entrances": {"0": {"spawn": 0, "room": 0}},
                      "cameraSettings": {"cameraMovement": 0, "worldMapArea": -1}},
                "1": {"spawns": spawn, "entrances": {"0": {"spawn": 0, "room": 0}},
                      "cameraSettings": {"cameraMovement": 0, "worldMapArea": 23}},
                "2": {"spawns": spawn, "entrances": {"0": {"spawn": 0, "room": 0}},
                      "cameraSettings": {"cameraMovement": 0, "worldMapArea": 22}},
            },
        },
        "custom/ub028/sala.json": {"$schema": "unbound/room/1", "setups": {"0": {}}},
        "custom/ub028/colisao.json": {
            "$schema": "unbound/collision/3",
            "bounds": {"min": [0, 0, 0], "max": [1, 1, 1]},
            "bulk": {"file": "custom/ub028/colisao.bin", "vertices": 0, "polys": 0},
        },
    }


def write_layer(folder, name, docs, bulk):
    path = os.path.join(folder, name)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as archive:
        archive.writestr("unbound.json", json.dumps(MANIFEST, indent=2))
        for inner, doc in docs.items():
            archive.writestr(inner, json.dumps(doc, separators=(",", ":")))
        archive.writestr(bulk, b"")
    print(path)


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    os.makedirs(argv[1], exist_ok=True)
    write_layer(argv[1], "UB020-A.o2r", documents(), "custom/ub020/colisao.bin")
    write_layer(argv[1], "UB022-A.o2r", world_limit_documents(), "custom/ub022/colisao.bin")
    write_layer(argv[1], "UB024-A.o2r", camera_documents(), "custom/ub024/colisao.bin")
    write_layer(argv[1], "UB025-A.o2r", graph_documents(), "custom/ub025/colisao.bin")
    write_layer(argv[1], "UB028-A.o2r", map_documents(), "custom/ub028/colisao.bin")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
