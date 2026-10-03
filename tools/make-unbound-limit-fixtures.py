#!/usr/bin/env python3
"""Gera as camadas de prova da UNBOUND-020 e da UNBOUND-022: documentos Unbound nos tetos da matriz do Prelude.

Uso: make-unbound-limit-fixtures.py <pasta-de-saida>   (grava UB020-A.o2r e UB022-A.o2r)

Os documentos ficam fora de scenes/ e do registro, entao o jogo nao os carrega: so a checagem do game.ready
(UNBOUND-019) os mescla e transcodifica, e o tempo dela sai no resumo (ms=). Sozinha em mods/, o esperado e
"referencias: documentos=3 conferidas=3 ausentes=0 recusados=0 notas=4":
  custom/ub020/cena.json     32 769 salas (uma acima do indice s16)            -> nota de salas
  custom/ub020/sala.json     65 536 atores e 1 025 objetos                     -> nota de atores e de objetos
  custom/ub020/colisao.json  65 535 surface types e water boxes (no teto) e uma posicao de camera fora de s16
                                                                               -> nota de camera
  custom/ub020/colisao.bin   bulk vazio, para a referencia resolver
UB022-A (limite do mundo, BGCHECK_XYZ_ABSMAX = 1 048 576; alem dele o jogo apaga os EffectSs): sozinha em mods/,
o esperado e "referencias: documentos=3 conferidas=3 ausentes=0 recusados=0 notas=4":
  custom/ub022/cena.json     spawn em x = 1 048 577 e transition actor em y = -1 048 600   -> duas notas
  custom/ub022/sala.json     ator em z = -1 048 576,5 e outro em x = 1 048 576 (no limite)  -> uma nota
  custom/ub022/colisao.json  bounds ate x = 1 048 800                                      -> uma nota
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
            }},
        },
        "custom/ub020/colisao.json": {
            "$schema": "unbound/collision/3",
            "bounds": {"min": [0, 0, 0], "max": [1, 1, 1]},
            "bulk": {"file": "custom/ub020/colisao.bin", "vertices": 0, "polys": 0},
            "surfaceTypes": positional(65535, {}),
            "waterBoxes": positional(65535, {}),
            "cameraPositions": {"0": [32767, 0, 0], "1": [40000, 0, 0]},
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
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
