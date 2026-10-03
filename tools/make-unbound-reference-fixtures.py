#!/usr/bin/env python3
"""Gera a camada de prova da UNBOUND-019: um .o2r Unbound com referencias que o oot.o2r nao tem.

Uso: make-unbound-reference-fixtures.py <pasta-de-saida>

Sozinha em mods/, o game.ready deve registrar
"referencias: documentos=4 ... ausentes=3 recusados=1" e estes avisos:
  scenes/spot04/rooms/0.json   delta que troca a display list da sala 0 por um nome com outro offset, como o de
                               uma cena feita sobre outra versao da ROM -> mesh.entries.opa ausente
  scenes/spot04/rooms/1.json   delta valido (echo); as referencias vem da base e existem -> nenhum aviso
  custom/ub019/cena.json       cena nova: sala da base (existe), colisao e musica que ninguem traz
                               -> collision e sound.song ausentes
  custom/ub019/quebrada.json   cena sem setup "0" -> recusada antes do gameplay
So JSON parcial escrito aqui: nenhum dado do jogo entra no arquivo.
"""
import json
import os
import sys
import zipfile

MANIFEST = {"format": "unbound", "formatVersion": 2, "requires": {"formatVersion": 2}}

LAYERS = {
    "UB019-A.o2r": {
        "scenes/spot04/rooms/0.json": {
            "$schema": "unbound/room/1",
            "setups": {"0": {"mesh": {"entries": {"0": {"opa": "scenes/shared/spot04_scene/spot04_room_0DL_00BD7C"}}}}},
        },
        "scenes/spot04/rooms/1.json": {"$schema": "unbound/room/1", "setups": {"0": {"echo": 3}}},
        "custom/ub019/cena.json": {
            "$schema": "unbound/scene/1",
            "collision": "custom/ub019/collision.json",
            "rooms": {"0": "scenes/spot04/rooms/0.json"},
            "setups": {"0": {"sound": {"seq": 2, "song": "custom/music/ub019-tema"}}},
        },
        "custom/ub019/quebrada.json": {"$schema": "unbound/scene/1", "setups": {"1": {"sound": {"seq": 2}}}},
    },
}


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    os.makedirs(argv[1], exist_ok=True)
    for name, files in LAYERS.items():
        path = os.path.join(argv[1], name)
        with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as archive:
            archive.writestr("unbound.json", json.dumps(MANIFEST, indent=2))
            for inner, doc in files.items():
                archive.writestr(inner, json.dumps(doc, indent=2))
        print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
