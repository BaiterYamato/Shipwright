#!/usr/bin/env python3
"""Readapta resoluções NEI quando só a função do host mudou.

Uso: python readapt.py <repo-do-host> [resolucao.c ...] [--saida PASTA]
Sem arquivos explícitos, procura carimbos velhos em overlay-extra/ e overlay-merges/.
Requer Git, Python 3 e os módulos overlay.py, extract_additions.py e sync.py do fork.
"""
import argparse
import importlib
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile


CARIMBO = re.compile(r"/\* overlay-(extra|merge) ([0-9a-f]{12} [0-9a-f]{12} [0-9a-f]{12}) \*/")
NOME = re.compile(r"(.+)\.([A-Za-z_]\w*)\.c")
MARCADOR = re.compile(r"^(?:<<<<<<< |=======\s*$|>>>>>>> )", re.M)
PASTA_FORK = Path("soh/native-sdk/nei-core/fork")


class ErroReadapt(Exception):
    """Falha recuperável de uma entrada, sem sobrescrever sua resolução."""


def carregar_modulos(pasta):
    # O fork pode ser somente leitura: importar nunca deve criar __pycache__ nele.
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(pasta))
    return importlib.import_module("overlay"), importlib.import_module("sync")


def git(repo, *args, ausente_ok=False):
    r = subprocess.run(["git", "-c", "safe.directory=" + str(repo), "-C", str(repo), *args],
                       capture_output=True)
    if r.returncode:
        erro = r.stderr.decode("utf-8", errors="replace").strip()
        # Uma revisão pode registrar a remoção do arquivo; ela não serve de base.
        if ausente_ok and "does not exist in" in erro:
            return None
        raise ErroReadapt(erro or "Git falhou (código %d)" % r.returncode)
    return r.stdout.decode("utf-8", errors="replace").replace("\r\n", "\n")


def funcao(overlay, texto, nome, origem):
    f = overlay.funcoes(texto).get(nome)
    if f is None:
        raise ErroReadapt("função %s ausente em %s" % (nome, origem))
    return f.texto


def ler_resolucao(caminho):
    original = caminho.read_bytes()
    bom = original.startswith(b"\xef\xbb\xbf")
    texto = original.decode("utf-8-sig")
    quebra = re.search(r"\r\n|\n|\r", texto)
    eol = quebra.group() if quebra else "\n"
    texto = texto.replace("\r\n", "\n").replace("\r", "\n")
    primeira, separador, corpo = texto.partition("\n")
    m = CARIMBO.fullmatch(primeira.strip())
    if not m or not separador:
        raise ErroReadapt("primeira linha sem carimbo overlay-extra/overlay-merge válido")
    if not corpo.strip() or MARCADOR.search(corpo):
        raise ErroReadapt("resolução vazia ou com marcadores de conflito; revise à mão")
    return original, eol, bom, m.group(1), m.group(2), corpo


def localizar_fonte(overlay, repo, caminho):
    m = NOME.fullmatch(caminho.name)
    if not m:
        raise ErroReadapt("nome esperado: <arquivo>.<função>.c")
    stem, nome = m.groups()
    fontes = sorted(set(f for f in overlay.fontes_sobreponiveis(str(repo)) if Path(f).stem == stem))
    if len(fontes) != 1:
        raise ErroReadapt("fonte %s: esperado um caminho em soh/hookable_sources.txt; encontrados %d"
                          % (stem, len(fontes)))
    return fontes[0], nome


def excluida(overlay, arquivo, nome):
    """Fonte ou função fora do overlay (overlay-exclude.txt): a resolução não entra na DLL."""
    excluir, _ = overlay.ler_excluir()
    for chave in ("arquivo " + Path(arquivo).name, nome):
        if chave in excluir:
            return chave
    return None


def encontrar_base(overlay, repo, arquivo, nome, antiga, base, fork, limite):
    revisoes = git(repo, "log", "--format=%H", "-n", str(limite), "--", arquivo).splitlines()
    for revisao in revisoes:
        texto = git(repo, "show", "%s:%s" % (revisao, arquivo), ausente_ok=True)
        if texto is None:
            continue
        f = overlay.funcoes(texto).get(nome)
        if f is not None and overlay.assinatura(f.texto, base, fork) == antiga:
            return revisao, f.texto
    raise ErroReadapt("carimbo não encontrado em nenhuma das %d revisões examinadas (limite %d); "
                      "aumente --limite ou revise a resolução à mão" % (len(revisoes), limite))


def mesclar(host, base, adaptada):
    # -p só escreve em stdout. Os temporários ficam no temp do sistema, nunca no fork.
    with tempfile.TemporaryDirectory(prefix="readapt-") as pasta:
        caminhos = []
        for nome, texto in (("host", host), ("base", base), ("adaptada", adaptada)):
            caminho = Path(pasta) / nome
            caminho.write_bytes((texto.rstrip("\n") + "\n").encode("utf-8"))
            caminhos.append(str(caminho))
        r = subprocess.run(["git", "merge-file", "-p", "-L", "host atual", "-L", "host antigo",
                            "-L", "resolução antiga", *caminhos], capture_output=True)
        if r.returncode < 0 or r.returncode >= 128:
            raise ErroReadapt("git merge-file falhou: " + r.stderr.decode("utf-8", errors="replace").strip())
        texto = r.stdout.decode("utf-8").replace("\r\n", "\n")
        if not texto:
            raise ErroReadapt("git merge-file não devolveu conteúdo")
        return texto, r.returncode


def gravar(destino, dados, origem, original):
    """Publica o arquivo inteiro atomicamente, conferindo concorrência na origem."""
    if origem.read_bytes() != original:
        raise ErroReadapt("resolução alterada durante a execução; nada gravado")
    destino.parent.mkdir(parents=True, exist_ok=True)
    temporario = None
    try:
        with tempfile.NamedTemporaryFile(dir=destino.parent, prefix=".readapt-", delete=False) as f:
            temporario = Path(f.name)
            f.write(dados)
        os.chmod(temporario, stat.S_IMODE(origem.stat().st_mode))
        if origem.read_bytes() != original:
            raise ErroReadapt("resolução alterada durante a execução; nada gravado")
        os.replace(temporario, destino)
    finally:
        if temporario is not None:
            temporario.unlink(missing_ok=True)


def readaptar(args, caminho, overlay, sync):
    original, eol, bom, tipo, antiga, adaptada = ler_resolucao(caminho)
    arquivo, nome = localizar_fonte(overlay, args.repo, caminho)
    chave = excluida(overlay, arquivo, nome)
    if chave:
        print("%s: ignorada; '%s' está em overlay-exclude.txt" % (caminho.name, chave))
        return 0
    host = funcao(overlay, (args.repo / arquivo).read_text(encoding="utf-8"), nome, "host atual")
    base = fork = ""
    if tipo == "merge":
        base, fork = (funcao(overlay, git(args.repo, "show", "%s:%s" % (rev, arquivo)), nome, origem)
                      for rev, origem in ((sync.FORK_BASE, "FORK_BASE"), (sync.FORK_COMMIT, "FORK_COMMIT")))
    nova = overlay.assinatura(host, base, fork)
    if antiga == nova:
        if args.arquivos and not args.verificar:
            print("%s: em dia; assinatura %s; conflitos: 0" % (caminho.name, nova))
        return 0
    print("%s: velho; assinatura %s -> %s" % (caminho.name, antiga, nova))
    mudaram = [rotulo for rotulo, a, n in zip(("base", "fork"), antiga.split()[1:], nova.split()[1:]) if a != n]
    if args.verificar:
        if mudaram:
            print("  %s também mudou; readaptação automática bloqueada" % " e ".join(mudaram))
        return 1
    if mudaram:
        raise ErroReadapt("%s também mudou; não é possível readaptar variando só o host" % " e ".join(mudaram))
    revisao, host_antigo = encontrar_base(overlay, args.repo, arquivo, nome, antiga, base, fork, args.limite)
    texto, conflitos = mesclar(host, host_antigo, adaptada)
    destino = (args.saida / caminho.name) if args.saida else caminho
    if conflitos:
        # Fora de *.c: o overlay.py lê toda resolução .c da pasta, e um diagnóstico ali travaria o configure.
        destino = destino.with_suffix(".conflito.txt")
    if args.saida and destino.resolve() == caminho.resolve():
        raise ErroReadapt("--saida aponta para a resolução original; escolha outra pasta")
    if destino.resolve() == caminho.resolve() and conflitos:
        raise ErroReadapt("destino de conflito coincide com a origem")
    conteudo = "/* overlay-%s %s */\n%s" % (tipo, nova, texto)
    dados = (b"\xef\xbb\xbf" if bom else b"") + conteudo.replace("\n", eol).encode("utf-8")
    gravar(destino, dados, caminho, original)
    print("  revisão base: %s; conflitos: %d; saída: %s" % (revisao, conflitos, destino))
    return 1 if conflitos else 0


def positivo(valor):
    n = int(valor)
    if n <= 0:
        raise argparse.ArgumentTypeError("o limite deve ser positivo")
    return n


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repo", type=Path, help="repositório do host (árvore atual)")
    parser.add_argument("arquivos", nargs="*", type=Path, help="resoluções; caminhos relativos ao diretório atual")
    parser.add_argument("--saida", type=Path, help="grava somente nesta pasta, sem substituir as origens")
    parser.add_argument("--limite", type=positivo, default=200, help="máximo de revisões por fonte (padrão: 200)")
    parser.add_argument("--verificar", action="store_true", help="só lista carimbos velhos; retorna 1 se houver")
    parser.add_argument("--fork-dir", type=Path, help="pasta dos módulos e resoluções (padrão: fork do host)")
    args = parser.parse_args(argv)
    args.repo = args.repo.resolve()
    pasta = (args.fork_dir or args.repo / PASTA_FORK).resolve()
    try:
        overlay, sync = carregar_modulos(pasta)
        # Sem a lista de exclusões, uma resolução fora da DLL seria regravada como se valesse; o sync.py também a exige.
        if not (pasta / "overlay-exclude.txt").is_file():
            raise ErroReadapt("overlay-exclude.txt ausente em %s" % pasta)
        caminhos = sorted(set(p.resolve() for p in args.arquivos)) if args.arquivos else sorted(
            p for sub in ("overlay-extra", "overlay-merges") for p in (pasta / sub).glob("*.c"))
        # --saida usa nomes simples: não deixe duas entradas disputarem o mesmo arquivo.
        if args.saida and len({p.name for p in caminhos}) != len(caminhos):
            raise ErroReadapt("nomes de saída duplicados; processe as resoluções em pastas separadas")
        resultado = 0
        for caminho in caminhos:
            try:
                resultado = max(resultado, readaptar(args, caminho, overlay, sync))
            except (ErroReadapt, OSError, UnicodeError, RuntimeError) as erro:
                print("%s: erro: %s" % (caminho.name, erro), file=sys.stderr)
                resultado = 2
        if not caminhos:
            print("Nenhuma resolução encontrada em %s." % pasta)
        elif resultado == 0 and not args.arquivos:
            print("Nenhum carimbo velho.")
        return resultado
    except (ErroReadapt, OSError, ImportError, RuntimeError) as erro:
        print("erro: %s" % erro, file=sys.stderr)
        return 2


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    raise SystemExit(main())
