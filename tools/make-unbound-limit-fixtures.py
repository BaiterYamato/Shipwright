#!/usr/bin/env python3
"""Gera a camada de prova da UNBOUND-020: documentos Unbound nos tetos da matriz do Prelude.

Uso: make-unbound-limit-fixtures.py <pasta-de-saida>

Os documentos ficam fora de scenes/ e do registro, entao o jogo nao os carrega: so a checagem do game.ready
(UNBOUND-019) os mescla e transcodifica, e o tempo dela sai no resumo (ms=). Sozinha em mods/, o esperado e
"referencias: documentos=3 conferidas=3 ausentes=0 recusados=0 notas=4":
  custom/ub020/cena.json     32 769 salas (uma acima do indice s16)            -> nota de salas
  custom/ub020/sala.json     65 536 atores e 1 025 objetos                     -> nota de atores e de objetos
  custom/ub020/colisao.json  65 535 surface types e water boxes (no teto) e uma posicao de camera fora de s16
                                                                               -> nota de camera
  custom/ub020/colisao.bin   bulk vazio, para a referencia resolver
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


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    os.makedirs(argv[1], exist_ok=True)
    path = os.path.join(argv[1], "UB020-A.o2r")
    with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as archive:
        archive.writestr("unbound.json", json.dumps(MANIFEST, indent=2))
        for inner, doc in documents().items():
            archive.writestr(inner, json.dumps(doc, separators=(",", ":")))
        archive.writestr("custom/ub020/colisao.bin", b"")
    print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
