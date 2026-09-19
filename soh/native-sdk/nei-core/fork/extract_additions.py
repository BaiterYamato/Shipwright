#!/usr/bin/env python3
"""Extrai adicoes de um arquivo C do fork para uma unidade de compilacao NEI.

Uso:
  python extract_additions.py --repo REPO --file CAMINHO --seed FUNCAO [--seed FUNCAO ...] --out SAIDA.c --report RELATORIO.txt

O arquivo do host e somente lido; o fork e lido por ``git show``.  O resultado
nao tenta substituir headers: simbolos ja existentes no host viram imports.
"""
import argparse
import os
import re
import subprocess
import sys
from dataclasses import dataclass


IDENT = re.compile(r"\b[A-Za-z_]\w*\b")
DEFINE = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)", re.M)
INCLUDE = re.compile(r"^\s*#\s*include\b.*$", re.M)
KEYWORDS = frozenset("""auto break case char const continue default do double else enum extern float for goto if int long
register return short signed sizeof static struct switch typedef union unsigned void volatile while inline restrict
_Bool _Complex _Imaginary alignas alignof atomic bool class constexpr decltype namespace noexcept nullptr
public private protected template this throw try typename using virtual wchar_t BAD_RETURN""".split())


def mascarar(texto):
    """Mascara comentario, string e caractere, conservando tamanho e quebras."""
    saida = list(texto)
    i, n = 0, len(texto)
    while i < n:
        if texto.startswith("//", i):
            j = texto.find("\n", i)
            if j < 0:
                j = n
            for k in range(i, j):
                saida[k] = " "
            i = j
        elif texto.startswith("/*", i):
            j = texto.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if saida[k] != "\n":
                    saida[k] = " "
            i = j
        elif texto[i] in "\"'":
            aspas = texto[i]
            saida[i] = " "
            i += 1
            while i < n:
                c = texto[i]
                if c != "\n":
                    saida[i] = " "
                if c == "\\" and i + 1 < n:
                    i += 1
                    if texto[i] != "\n":
                        saida[i] = " "
                elif c == aspas:
                    i += 1
                    break
                i += 1
        else:
            i += 1
    return "".join(saida)


def fim_linha(texto, pos):
    p = texto.find("\n", pos)
    return len(texto) if p < 0 else p


def pular_espaco_e_pp(mask, pos):
    n = len(mask)
    while pos < n and mask[pos].isspace():
        pos += 1
    return pos


def nome_funcao(prefixo):
    """Retorna o identificador cuja lista de parametros termina no abre-chave."""
    candidatos = list(re.finditer(r"\b([A-Za-z_]\w*)\s*\(", prefixo))
    for m in reversed(candidatos):
        nivel, j = 1, m.end()
        while j < len(prefixo) and nivel:
            if prefixo[j] == "(":
                nivel += 1
            elif prefixo[j] == ")":
                nivel -= 1
            j += 1
        if nivel == 0 and not prefixo[j:].strip():
            return m.group(1)
    return None


def nome_variavel(mask):
    """Heuristica para declaradores simples de variaveis de topo do codigo do jogo."""
    antes = mask.split("=", 1)[0]
    ponteiros = re.findall(r"\(\s*\*\s*([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*\)", antes)
    if ponteiros:
        return ponteiros[-1]
    antes = re.sub(r"\[[^\]]*\]", " ", antes)
    nomes = IDENT.findall(antes)
    nomes = [x for x in nomes if x not in KEYWORDS]
    return nomes[-1] if nomes else None


@dataclass
class Declaracao:
    nome: str
    tipo: str
    inicio: int
    fim: int
    texto: str
    mascara: str


def classificar(texto, mascara, inicio, fim):
    limpo = mascara.strip()
    if not limpo:
        return None
    dm = DEFINE.match(mascara)
    if dm:
        return Declaracao(dm.group(1), "define", inicio, fim, texto, mascara)
    # Um bloco que abre no topo e tem uma lista de parametros no prefixo e funcao.
    abre = mascara.find("{")
    if abre >= 0:
        nome = nome_funcao(mascara[:abre])
        if nome:
            return Declaracao(nome, "funcao", inicio, fim, texto, mascara)
    # Prototipos tambem precisam estar no mapa: eles sao imports potenciais.
    nome = nome_funcao(mascara.rstrip().rstrip(";"))
    if nome:
        return Declaracao(nome, "funcao", inicio, fim, texto, mascara)
    if re.match(r"\s*typedef\b", mascara):
        nomes = IDENT.findall(mascara.rstrip().rstrip(";"))
        nomes = [x for x in nomes if x not in KEYWORDS]
        if nomes:
            return Declaracao(nomes[-1], "tipo", inicio, fim, texto, mascara)
    # Tags nomeadas sao dependencias possiveis mesmo sem typedef.
    tag = re.match(r"\s*(?:typedef\s+)?(?:struct|enum|union)\s+([A-Za-z_]\w*)", mascara)
    if tag:
        return Declaracao(tag.group(1), "tipo", inicio, fim, texto, mascara)
    nome = nome_variavel(mascara)
    if nome:
        return Declaracao(nome, "variavel", inicio, fim, texto, mascara)
    return None


def declaracoes_topo(texto):
    """Recorta declaracoes de topo sem confundir chaves de strings/comentarios."""
    mask = mascarar(texto)
    resultado, pos, n = [], 0, len(mask)
    blocos = 0  # namespace { / extern "C" { abertos: o conteudo continua sendo topo
    while True:
        ini = pular_espaco_e_pp(mask, pos)
        if ini >= n:
            break
        # Com a string mascarada, extern "C" { aparece como extern seguido de espacos e chave.
        bloco = re.match(r"(?:namespace\b[\w\s:]*|extern\s*)\{", mask[ini:])
        if bloco:
            blocos += 1
            pos = ini + bloco.end()
            continue
        if mask[ini] == "}" and blocos:
            blocos -= 1
            pos = ini + 1
            continue
        if mask[ini] == "#":
            fim = fim_linha(mask, ini)
            while fim > ini and mask[fim - 1] == "\\" and fim < n:
                fim = fim_linha(mask, fim + 1)
            d = classificar(texto[ini:fim], mask[ini:fim], ini, fim)
            if d:
                resultado.append(d)
            pos = fim + 1
            continue
        chaves = parenteses = colchetes = 0
        e_funcao = False
        i, fim = ini, n
        while i < n:
            c = mask[i]
            if c == "(":
                parenteses += 1
            elif c == ")":
                parenteses = max(0, parenteses - 1)
            elif c == "[":
                colchetes += 1
            elif c == "]":
                colchetes = max(0, colchetes - 1)
            elif c == "{":
                if chaves == 0:
                    e_funcao = nome_funcao(mask[ini:i]) is not None
                chaves += 1
            elif c == "}":
                chaves = max(0, chaves - 1)
                # Funcao nao termina em ponto e virgula; tipos/variaveis sim.
                if chaves == 0 and e_funcao:
                    fim = i + 1
                    break
            elif c == ";" and not chaves and not parenteses and not colchetes:
                fim = i + 1
                break
            i += 1
        d = classificar(texto[ini:fim], mask[ini:fim], ini, fim)
        if d:
            resultado.append(d)
            # Em ``typedef struct Tag { ... } Alias;``, Tag e Alias podem ser
            # usados separadamente; ambos apontam para o mesmo trecho fonte.
            tag = re.match(r"\s*(?:typedef\s+)?(?:struct|enum|union)\s+([A-Za-z_]\w*)", d.mascara)
            if d.tipo == "tipo" and tag and tag.group(1) != d.nome:
                resultado.append(Declaracao(tag.group(1), "tipo", d.inicio, d.fim, d.texto, d.mascara))
        pos = max(fim, ini + 1)
    return resultado


def remover_static(texto):
    return re.sub(r"\bstatic\s+", "", texto, count=1)


def cabecalho_funcao(d, sem_static=False):
    abre = d.mascara.find("{")
    cab = (d.texto if abre < 0 else d.texto[:abre]).rstrip().rstrip(";").rstrip()
    if sem_static:
        cab = remover_static(cab)
        cab = re.sub(r'\bextern\s+"C"\s*', "", cab)
    return cab + ";"


def declaracao_externa_variavel(d):
    """Converte definicao fork em extern, mantendo os sufixos de array."""
    mask = d.mascara
    igual = mask.find("=")
    limite = len(d.texto) if igual < 0 else igual
    base = d.texto[:limite].rstrip()
    base = re.sub(r"\bstatic\s+", "", base, count=1).strip()
    base = base.rstrip().rstrip(";").rstrip()
    return "extern " + base + ";"


def ids(texto):
    return set(IDENT.findall(mascarar(texto))) - KEYWORDS


def is_static(d):
    return bool(re.search(r"\bstatic\b", d.mascara[:d.mascara.find("{") if "{" in d.mascara else len(d.mascara)]))


def ler_git(repo, *args):
    p = subprocess.run(["git", "-c", "safe.directory=" + os.path.abspath(repo), "-C", repo, *args],
                       text=True, encoding="utf-8", errors="replace", capture_output=True)
    if p.returncode:
        raise RuntimeError(p.stderr.strip() or "git falhou")
    return p.stdout


def main():
    ap = argparse.ArgumentParser(description="Extrai adicoes C de um fork NEI.")
    ap.add_argument("--repo", default=os.getcwd())
    ap.add_argument("--fork", default="c29262b")
    ap.add_argument("--file", required=True)
    ap.add_argument("--seed", action="append", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--report", required=True)
    ap.add_argument("--sem-includes", action="store_true",
                    help="nao copia os #include do fork (saida incluida numa unidade que ja os tem)")
    ap.add_argument("--sem-host", action="store_true", help="arquivo so do fork: nada vira import")
    ap.add_argument("--de-cpp", action="store_true",
                    help='fonte C++ com funcoes extern "C" em C puro: extern "C" vira extern, nullptr vira NULL')
    ap.add_argument("--inclui", action="append", default=[], help="#include extra na saida (repetivel)")
    a = ap.parse_args()
    repo = os.path.abspath(a.repo)
    if a.sem_host:
        host = ""
    else:
        caminho_host = os.path.join(repo, *a.file.replace("/", "\\").split("\\"))
        with open(caminho_host, encoding="utf-8", errors="replace") as f:
            host = f.read()
    fork = ler_git(repo, "show", "%s:%s" % (a.fork, a.file))
    fd, hd = declaracoes_topo(fork), declaracoes_topo(host)
    fmap = {d.nome: d for d in fd}
    hmap = {d.nome: d for d in hd}
    ausentes = []
    fila = list(a.seed)
    escolhidas, imports_func, imports_var, tipos = set(), set(), set(), set()
    vistos = set()
    while fila:
        nome = fila.pop()
        if nome in vistos:
            continue
        vistos.add(nome)
        d = fmap.get(nome)
        if not d:
            if nome not in hmap:
                ausentes.append(nome)
            continue
        if nome in hmap:
            if d.tipo == "funcao": imports_func.add(nome)
            elif d.tipo == "variavel": imports_var.add(nome)
            elif d.tipo in ("tipo", "define"):
                tipos.add(nome)
            else:
                continue
        elif d.tipo in ("funcao", "variavel"):
            escolhidas.add(nome)
        elif d.tipo in ("tipo", "define"):
            tipos.add(nome)
        for ident in ids(d.texto):
            if ident == nome:
                continue
            outro = fmap.get(ident)
            if outro:
                if ident in hmap:
                    if outro.tipo == "funcao": imports_func.add(ident)
                    elif outro.tipo == "variavel": imports_var.add(ident)
                    elif outro.tipo in ("tipo", "define"):
                        tipos.add(ident)
                        fila.append(ident)
                elif outro.tipo in ("funcao", "variavel", "tipo", "define"):
                    fila.append(ident)
            # So chamadas/macros desconhecidas entram no relatorio; nomes locais nao.
            elif ident not in hmap and (re.search(r"\b" + re.escape(ident) + r"\s*\(", mascarar(d.texto)) or ident.isupper()):
                ausentes.append(ident)
    # Prototipos originais nao sao definicoes a copiar: os prototipos gerados
    # abaixo ja fazem o forward-declare de cada definicao escolhida.
    selecionadas = [d for d in fd if d.nome in escolhidas and
                    (d.tipo != "funcao" or "{" in d.mascara)]
    tipos_d = []
    vistos_trechos = set()
    for d in fd:
        if d.nome in tipos and (d.inicio, d.fim) not in vistos_trechos:
            tipos_d.append(d)
            vistos_trechos.add((d.inicio, d.fim))
    impf_d = [hmap[n] for n in imports_func if n in hmap and hmap[n].tipo == "funcao"]
    impv_d = [fmap[n] for n in imports_var if n in fmap and fmap[n].tipo == "variavel"]
    divergencias = []
    for d in tipos_d:
        if d.nome in hmap and d.texto.strip() != hmap[d.nome].texto.strip():
            divergencias.append(d.nome)
    # Ordenacao do arquivo, inclusive quando o mapa foi percorrido por conjuntos.
    impf_d.sort(key=lambda d: hmap[d.nome].inicio)
    impv_d.sort(key=lambda d: d.inicio)
    includes = [] if a.sem_includes else INCLUDE.findall(fork)
    # A saida nao fica ao lado do fonte: include curto ("z_ator.h") vira caminho a partir de soh/src.
    pasta = os.path.dirname(a.file.replace("\\", "/"))
    if pasta.startswith("soh/src/"):
        includes = [re.sub(r'#\s*include\s*"([^"/]+)"', lambda m: '#include "%s/%s"' % (pasta[len("soh/src/"):], m.group(1)),
                           linha) for linha in includes]
    includes += ['#include "%s"' % h for h in a.inclui]
    linhas = ["/* Gerado por extract_additions.py; fonte: %s; fork: %s. */" % (a.file, a.fork), ""]
    linhas += includes + ([""] if includes else [])
    if tipos_d:
        linhas += [d.texto.rstrip() for d in tipos_d] + [""]
    if impf_d or impv_d:
        linhas.append("/* Imports do executavel host. */")
        linhas += [cabecalho_funcao(d, sem_static=True) for d in impf_d]
        for d in impv_d:
            linhas.append("#define %s (*nei_host_%s)" % (d.nome, d.nome))
            linhas.append(declaracao_externa_variavel(d))
        linhas.append("")
    funcoes = [d for d in selecionadas if d.tipo == "funcao"]
    if funcoes:
        linhas.append("/* Prototipos locais para permitir qualquer ordem de definicao. */")
        linhas += [cabecalho_funcao(d) for d in funcoes]
        linhas.append("")
    linhas += [d.texto.rstrip() for d in selecionadas] + [""]
    saida = "\n".join(linhas)
    if a.de_cpp:
        # extern vale em definicao de funcao C e mantem declaracoes de variavel como declaracoes.
        saida = re.sub(r'\bextern\s+"C"\s*', "extern ", saida)
        saida = re.sub(r"\bnullptr\b", "NULL", saida)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)) or ".", exist_ok=True)
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(saida)
    staticos = [d.nome for d in impf_d if is_static(d)]
    rel = ["Relatorio de extracao NEI", "Fonte: %s" % a.file, "Fork: %s" % a.fork, "",
           "Funcoes copiadas (%d): %s" % (len(funcoes), ", ".join(d.nome for d in funcoes) or "nenhuma"),
           "Imports de funcao do host (%d; static no host: %d): %s" % (len(impf_d), len(staticos), ", ".join(d.nome for d in impf_d) or "nenhum"),
           "Static no host: %s" % (", ".join(staticos) or "nenhum"),
           "Variaveis importadas do host (%d): %s" % (len(impv_d), ", ".join(d.nome for d in impv_d) or "nenhuma"),
           "Divergencias de tipos/macros: %s" % (", ".join(divergencias) or "nenhuma"),
           "Identificadores nao encontrados (provaveis headers): %s" % (", ".join(sorted(set(ausentes))) or "nenhum")]
    with open(a.report, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(rel) + "\n")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as e:
        print("erro: " + str(e), file=sys.stderr)
        sys.exit(2)
