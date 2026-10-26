"""Mostra a mescla de três vias de uma função do overlay (host atual, base do fork, fork) e a primeira linha que a
resolução em overlay-merges/<arquivo>.<função>.c precisa ter.

Uso: python overlay_conflito.py <repo-do-host> <arquivo do host> <função> [saida.c]
     python overlay_conflito.py . soh/src/overlays/actors/ovl_player_actor/z_player.c Player_UpdateCommon
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import overlay  # noqa: E402
from sync import FORK_BASE, FORK_COMMIT  # noqa: E402


def main():
    repo, arquivo, nome = sys.argv[1], sys.argv[2], sys.argv[3]
    base_t = overlay.ler_git(repo, "show", "%s:%s" % (FORK_BASE, arquivo))
    fork_t = overlay.ler_git(repo, "show", "%s:%s" % (FORK_COMMIT, arquivo))
    with open(os.path.join(repo, *arquivo.split("/")), encoding="utf-8", errors="replace") as f:
        host_t = f.read()
    host, base, fork = (overlay.funcoes(t)[nome].texto for t in (host_t, base_t, fork_t))
    texto, conflitos = overlay.mesclar(host, base, fork)
    if len(sys.argv) > 4:
        with open(sys.argv[4], "w", encoding="utf-8", newline="\n") as f:
            f.write(texto)
    else:
        sys.stdout.reconfigure(encoding="utf-8")
        print(texto)
    print("/* overlay-merge %s */" % overlay.assinatura(host, base, fork))
    print("blocos em conflito: %d" % conflitos)


if __name__ == "__main__":
    main()
