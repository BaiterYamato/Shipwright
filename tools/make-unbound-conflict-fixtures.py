#!/usr/bin/env python3
"""Gera as camadas de prova da UNBOUND-018: tres .o2r Unbound que mexem nos mesmos caminhos.

Uso: make-unbound-conflict-fixtures.py <pasta-de-saida>

Montadas em ordem de caminho (A < B < C), o game.ready deve registrar
"mods: camadas=3 arquivos em comum=6 mesclaveis=2 identicos=1 conflitos=3":
  scenes/spot04/scene.json    A e B mudam setups.0.sound.seq     -> conflito, vence B
  scenes/spot04/rooms/0.json  A e C trazem o mesmo echo          -> delta mesclavel
  scenes/spot04/rooms/1.json  A muda time.hour, B muda echo      -> delta mesclavel
  custom/ub018/templo.json    cena tipada fora de scenes/, A e B -> conflito, vence B
  textures/ub018/icone.bin    conteudo diferente em A e B        -> conflito de arquivo inteiro, vence B
  textures/ub018/igual.bin    mesmo conteudo em A e C            -> copia identica
Com so A e C: "arquivos em comum=2 mesclaveis=1 identicos=1 conflitos=0".
So JSON parcial e bytes escritos aqui: nenhum dado do jogo entra nos arquivos.
"""
import json
import os
import sys
import zipfile

MANIFEST = {"format": "unbound", "formatVersion": 2, "requires": {"formatVersion": 2}}

LAYERS = {
    "UB018-A.o2r": {
        "scenes/spot04/scene.json": {"$schema": "unbound/scene/1", "setups": {"0": {"sound": {"seq": 30}}}},
        "scenes/spot04/rooms/0.json": {"$schema": "unbound/room/1", "setups": {"0": {"echo": 5}}},
        "scenes/spot04/rooms/1.json": {"$schema": "unbound/room/1", "setups": {"0": {"time": {"hour": 12}}}},
        "custom/ub018/templo.json": {"$schema": "unbound/scene/1", "setups": {"0": {"sound": {"seq": 10}}}},
        "textures/ub018/icone.bin": b"UB018-A",
        "textures/ub018/igual.bin": b"UB018-igual",
    },
    "UB018-B.o2r": {
        "scenes/spot04/scene.json": {"$schema": "unbound/scene/1", "setups": {"0": {"sound": {"seq": 40}}}},
        "scenes/spot04/rooms/1.json": {"$schema": "unbound/room/1", "setups": {"0": {"echo": 2}}},
        "custom/ub018/templo.json": {"$schema": "unbound/scene/1", "setups": {"0": {"sound": {"seq": 11}}}},
        "textures/ub018/icone.bin": b"UB018-B",
    },
    "UB018-C.o2r": {
        "scenes/spot04/rooms/0.json": {"$schema": "unbound/room/1", "setups": {"0": {"echo": 5}}},
        "textures/ub018/igual.bin": b"UB018-igual",
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
                archive.writestr(inner, doc if isinstance(doc, bytes) else json.dumps(doc, indent=2))
        print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
