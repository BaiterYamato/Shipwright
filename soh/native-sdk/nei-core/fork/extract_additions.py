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
    return apagar_if0("".join(saida))


def apagar_if0(mask):
    """Apaga (com espaços, mantendo as quebras) o ramo morto de `#if 0`, até o #else/#elif/#endif dele."""
    linhas = mask.split("\n")
    profundidade, morto_em = 0, None
    for k, linha in enumerate(linhas):
        diretiva = re.match(r"\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", linha)
        if diretiva:
            nome = diretiva.group(1)
            if nome in ("if", "ifdef", "ifndef"):
                profundidade += 1
                if morto_em is None and nome == "if" and diretiva.group(2).strip() == "0":
                    morto_em = profundidade
                    linhas[k] = " " * len(linha)
                    continue
            elif nome in ("elif", "else") and morto_em == profundidade:
                morto_em = None
            elif nome == "endif":
                if morto_em == profundidade:
                    morto_em = None
                    linhas[k] = " " * len(linha)
                    profundidade -= 1
                    continue
                profundidade -= 1
        if morto_em is not None:
            linhas[k] = " " * len(linha)
    return "\n".join(linhas)


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
    # Outras diretivas (#include no meio do arquivo, #if) não declaram nada.
    if limpo.startswith("#"):
        return None
    # Um bloco que abre no topo e tem uma lista de parametros no prefixo e funcao.
    abre = mascara.find("{")
    if abre >= 0:
        nome = nome_funcao(mascara[:abre])
        if nome:
            return Declaracao(nome, "funcao", inicio, fim, texto, mascara)
    # Prototipos tambem precisam estar no mapa: eles sao imports potenciais. Nome todo em maiusculas sem tipo na
    # frente e invocacao de macro no topo (SHIP_SAVESTATE_DEFINE(BossTw, ...)), nao prototipo.
    nome = nome_funcao(mascara.rstrip().rstrip(";"))
    if nome and nome.isupper() and mascara.strip().startswith(nome):
        return None
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
        # Invocação de macro no topo sem ; (SHIP_SAVESTATE_DEFINE(BossTw, ...)): não declara nada que a extração
        # use, e sem ; ela grudaria na declaração seguinte.
        macro = re.match(r"[A-Z_][A-Z0-9_]*\s*\(", mask[ini:])
        if macro:
            nivel, j = 0, ini + macro.end() - 1
            while j < n:
                nivel += {"(": 1, ")": -1}.get(mask[j], 0)
                j += 1
                if nivel == 0:
                    break
            resto = mask[j:j + 200].lstrip(" \t")
            if resto.startswith(("\n", "\r")):
                pos = j
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
            # Cada enumerador (PLAYER_ITEM_CHG_MAX) leva ao enum inteiro, que é quem o define.
            if d.tipo == "tipo" and re.match(r"\s*(?:typedef\s+)?enum\b", d.mascara) and "{" in d.mascara:
                corpo = d.mascara[d.mascara.find("{") + 1:d.mascara.rfind("}")]
                for item in corpo.split(","):
                    m = re.match(r"\s*([A-Za-z_]\w*)", item)
                    if m and m.group(1) != d.nome:
                        resultado.append(Declaracao(m.group(1), "tipo", d.inicio, d.fim, d.texto, d.mascara))
        pos = max(fim, ini + 1)
    return resultado


def mapa(declaracoes):
    """Nome -> declaração. Função fica com a definição (o protótipo vem antes); o resto fica com a primeira, que
    nos fontes do jogo é o ramo `#if defined(MODDING) || defined(_MSC_VER)` (sEyeTextures[2][8], não o do #else)."""
    m = {}
    for d in declaracoes:
        atual = m.get(d.nome)
        if atual is None or (d.tipo == "funcao" and "{" in d.mascara and "{" not in atual.mascara):
            m[d.nome] = d
    return m


def remover_static(texto):
    return re.sub(r"\bstatic\s+", "", texto, count=1)


def cabecalho_funcao(d, sem_static=False):
    abre = d.mascara.find("{")
    cab = (d.texto if abre < 0 else d.texto[:abre]).rstrip().rstrip(";").rstrip()
    if sem_static:
        cab = remover_static(cab)
        cab = re.sub(r'\bextern\s+"C"\s*', "", cab)
    return cab + ";"


def elementos_inicializador(mask):
    """Quantos elementos tem o `{ ... }` de primeiro nível (texto já mascarado). None se não der para contar: sem
    chaves, ou com diretiva de pré-processador no meio."""
    abre = mask.find("{")
    if abre < 0 or "#" in mask:
        return None
    nivel, n, tem = 0, 0, False
    for c in mask[abre:]:
        if c in "{(":
            nivel += 1
            if nivel == 1:
                continue
        elif c in "})":
            nivel -= 1
            if nivel == 0:
                return n + (1 if tem else 0)
        if nivel == 1 and c == ",":
            n, tem = n + 1, False
        elif not c.isspace():
            tem = True
    return None


def declaracao_externa_variavel(d):
    """Converte definicao fork em extern, mantendo os sufixos de array."""
    mask = d.mascara
    igual = mask.find("=")
    limite = len(d.texto) if igual < 0 else igual
    base = d.texto[:limite].rstrip()
    base = re.sub(r"\bstatic\s+", "", base, count=1).strip()
    base = base.rstrip().rstrip(";").rstrip()
    # `static u16 sItemButtons[] = { ... }` vira `extern u16 sItemButtons[N];`: sem o N o array fica incompleto, o
    # ARRAY_COUNT dele dá 0 no MSVC (só aviso) e o laço dos botões do Player_ProcessItemButtons nunca rodava.
    vazio = re.search(r"\[\s*\]", base)
    if vazio and igual >= 0:
        n = elementos_inicializador(mask[igual + 1:])
        if n:
            base = base[:vazio.start()] + "[%d]" % n + base[vazio.end():]
    # No fork a declaracao pode ja ser extern (variavel definida em outra unidade dele).
    if re.match(r"\bextern\b", base):
        return base + ";"
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


def embutir_dados(repo, arquivo, texto, ler):
    """Troca o #include de um arquivo de dados do host (não .h) pelo texto dele, lido por `ler`."""
    pasta = os.path.dirname(arquivo.replace("\\", "/"))

    def troca(m):
        inc = m.group(1)
        if inc.endswith(".h"):
            return m.group(0)
        for alvo in (os.path.normpath(os.path.join(pasta, inc)), os.path.join("soh", "src", inc)):
            alvo = alvo.replace("\\", "/")
            if alvo.startswith("soh/src/") and os.path.isfile(os.path.join(repo, *alvo.split("/"))):
                return "/* embutido de %s */\n%s\n" % (alvo, ler(alvo).rstrip("\n"))
        return m.group(0)
    return re.sub(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"[ \t]*$', troca, texto, flags=re.M)


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
    ap.add_argument("--exclui", action="append", default=[],
                    help="#include do fork que nao deve ser copiado, por caminho (repetivel). Serve para o "
                         "include de unity de um .c que ja tem unidade propria na DLL")
    ap.add_argument("--sobrepoe", action="append", default=[],
                    help="nome que o host tambem tem, mas cuja versao do fork deve ser copiada; a DLL desvia a do host")
    ap.add_argument("--symbols", help="soh.symbols do host: funcao ausente dele nao tem endereco para resolver, "
                                      "entao e copiada do fork em vez de virar import")
    ap.add_argument("--fork-file", help="le a versao do fork deste arquivo em vez de git show (fonte ja mesclada "
                                        "com o host pelo overlay.py)")
    ap.add_argument("--tabela", action="store_true",
                    help="fonte do host fora do LTCG (hookable_sources.txt): gera a tabela de desvios das "
                         "sobrepostas, qualifica static repetido pelo arquivo e troca funcao do host usada como "
                         "valor pelo endereco dela no host")
    ap.add_argument("--sem-desvio", action="append", default=[],
                    help="sobreposta que fica fora da tabela de desvios (o desvio é instalado em outro lugar)")
    a = ap.parse_args()
    repo = os.path.abspath(a.repo)
    sobrepoe = set(a.sobrepoe)
    resolviveis = None
    repeticoes = {}
    if a.symbols:
        resolviveis = set()
        with open(a.symbols, encoding="utf-8", errors="replace") as f:
            for linha in f:
                partes = linha.rstrip("\n").split("\t")
                if len(partes) == 4:
                    resolviveis.add(partes[3])
                    repeticoes[partes[3]] = repeticoes.get(partes[3], 0) + 1
    inlinadas = set()
    stem = os.path.splitext(os.path.basename(a.file))[0]

    def qualificado(nome):
        """Static repetido no soh.symbols só resolve com o arquivo: gen_imports.py lê nei_q_<arquivo>__<nome>."""
        if a.tabela and repeticoes.get(nome, 0) > 1:
            return "nei_q_%s__%s" % (stem, nome)
        return nome

    def e_import(nome, d):
        """O host ter o nome nao basta para ele virar import.

        Duas excecoes: o nome esta na lista de sobreposicao (a DLL desvia a versao do host, entao
        precisa da versao do fork no proprio modulo) ou o LTCG inlinou a funcao e ela nao aparece
        no soh.symbols, caso em que nao ha endereco para o escape hatch resolver."""
        if nome in sobrepoe:
            return False
        if resolviveis is not None and d.tipo == "funcao" and nome not in resolviveis:
            inlinadas.add(nome)
            return False
        # Static de dado que o compilador não deixou no soh.symbols (D_808987A0, só lido): a cópia da DLL tem o
        # mesmo inicializador. Só nos fontes fora do LTCG, onde todo static com armazenamento tem símbolo.
        if (a.tabela and resolviveis is not None and d.tipo == "variavel" and nome not in resolviveis
                and nome in hmap and re.match(r"\s*static\b", hmap[nome].mascara)):
            inlinadas.add(nome)
            return False
        return True
    if a.sem_host:
        host = ""
    else:
        caminho_host = os.path.join(repo, *a.file.replace("/", "\\").split("\\"))
        with open(caminho_host, encoding="utf-8", errors="replace") as f:
            host = f.read()
    if a.fork_file:
        with open(a.fork_file, encoding="utf-8", errors="replace") as f:
            fork = f.read()
    else:
        fork = ler_git(repo, "show", "%s:%s" % (a.fork, a.file))
    if a.tabela:
        # O arquivo de dados que o fonte inclui (z_camera_data.inc, *_colchk.c) é da mesma unidade no host: as
        # variáveis dele têm endereço no soh.symbols e seguem a regra de import como as do próprio fonte. Copiado
        # como #include, a DLL ganharia outra câmera: D_8015BD7C nulo, gDbgCamEnabled definido como ponteiro nulo.
        def ler_host(caminho):
            with open(os.path.join(repo, *caminho.split("/")), encoding="utf-8", errors="replace") as f:
                return f.read()

        def ler_fork(caminho):
            try:
                return ler_git(repo, "show", "%s:%s" % (a.fork, caminho))
            except Exception:
                return ler_host(caminho)
        host = embutir_dados(repo, a.file, host, ler_host)
        fork = embutir_dados(repo, a.file, fork, ler_fork)
    fd, hd = declaracoes_topo(fork), declaracoes_topo(host)
    fmap, hmap = mapa(fd), mapa(hd)
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
        if nome in hmap and e_import(nome, d):
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
        # Variável importada só precisa dos tipos da declaração; o inicializador fica no host.
        texto_deps = d.texto
        if d.tipo == "variavel" and nome in imports_var and d.mascara.find("=") >= 0:
            texto_deps = d.texto[:d.mascara.find("=")]
        for ident in ids(texto_deps):
            if ident == nome:
                continue
            # O nome só do host (sPlayerFocusOffsetFromHead, que o upstream renomeou e a mescla usa) é import.
            outro = fmap.get(ident) or (hmap.get(ident) if hmap.get(ident) and hmap[ident].tipo in ("funcao", "variavel")
                                        else None)
            if outro:
                if ident in hmap and e_import(ident, outro):
                    if outro.tipo == "funcao": imports_func.add(ident)
                    elif outro.tipo == "variavel":
                        imports_var.add(ident)
                        fila.append(ident)  # pelos tipos da declaração (ItemChangeInfo, PLAYER_ITEM_CHG_MAX)
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
    # A declaração do import é a do host: é o armazenamento que existe (o fork pode ter outra forma, ou duas
    # num #if, como o sEyeTextures).
    def decl_var(n):
        for d in (hmap.get(n), fmap.get(n)):
            if d is not None and d.tipo == "variavel":
                return d
        return None
    impv_d = [decl_var(n) for n in imports_var if decl_var(n) is not None]
    divergencias = []
    for d in tipos_d:
        if d.nome in hmap and d.texto.strip() != hmap[d.nome].texto.strip():
            divergencias.append(d.nome)
    # Ordenacao do arquivo, inclusive quando o mapa foi percorrido por conjuntos.
    impf_d.sort(key=lambda d: hmap[d.nome].inicio)
    impv_d.sort(key=lambda d: d.inicio)
    includes = [] if a.sem_includes else INCLUDE.findall(fork)
    includes = [linha for linha in includes if not any(x in linha for x in a.exclui)]
    # A saida nao fica ao lado do fonte: include curto ("z_ator.h") vira caminho a partir de soh/src.
    pasta = os.path.dirname(a.file.replace("\\", "/"))
    if pasta.startswith("soh/src/"):
        def relocar(m):
            # Só o header que existe mesmo ao lado do fonte (no host) vira caminho a partir de soh/src; global.h e
            # vt.h vêm do include path. Relativo que sai para a árvore do fork (../../../../mods/...) vira caminho a
            # partir de soh/, que a DLL tem no include path (nei-fork/fork/soh).
            alvo = os.path.normpath(os.path.join(pasta, m.group(1))).replace("\\", "/")
            if os.path.isfile(os.path.join(repo, *alvo.split("/"))) and alvo.startswith("soh/src/"):
                return '#include "%s"' % alvo[len("soh/src/"):]
            if alvo.startswith(("soh/mods/", "soh/expansions/")):
                return '#include "%s"' % alvo[len("soh/"):]
            return m.group(0)
        includes = [re.sub(r'#\s*include\s*"([^"]+)"', relocar, linha) for linha in includes]
    includes += ['#include "%s"' % h for h in a.inclui]
    linhas = ["/* Gerado por extract_additions.py; fonte: %s; fork: %s. */" % (a.file, a.fork), ""]
    linhas += includes + ([""] if includes else [])
    if tipos_d:
        linhas += [d.texto.rstrip() for d in tipos_d] + [""]
    if impf_d or impv_d:
        linhas.append("/* Imports do executavel host. */")
        for d in impf_d:
            if qualificado(d.nome) != d.nome:
                linhas.append("#define %s %s" % (d.nome, qualificado(d.nome)))
            linhas.append(cabecalho_funcao(d, sem_static=True))
        for d in impv_d:
            linhas.append("#define %s (*nei_host_%s)" % (d.nome, qualificado(d.nome)))
            linhas.append(declaracao_externa_variavel(d))
        linhas.append("")
    funcoes = [d for d in selecionadas if d.tipo == "funcao"]
    enderecos = set()
    if a.tabela:
        # Função do host usada como valor (actionFunc, comparação, callback) precisa do endereço do host: o jogo
        # compara `this->actionFunc == Player_Action_X` com o endereço dele, e a cópia da DLL ou o thunk do import
        # dariam outro. A chamada direta continua indo para a cópia ou para o thunk.
        do_host = {n for n, d in hmap.items() if d.tipo == "funcao"}
        valor = re.compile(r"(&\s*)?\b([A-Za-z_]\w*)\b(?!\s*\()")
        for d in funcoes:
            abre = d.mascara.find("{")
            if abre < 0:
                continue
            corpo_m, corpo = d.mascara[abre:], d.texto[abre:]
            partes, pos = [], 0
            for m in valor.finditer(corpo_m):
                nome = m.group(2)
                if nome not in do_host or nome == d.nome:
                    continue
                if corpo_m[:m.start()].rstrip().endswith(("->", ".")):
                    continue
                simbolo = "nei_fnaddr_" + qualificado(nome)
                enderecos.add(simbolo)
                # `&Func` é o endereço; em `a && Func` o & é do operador e fica.
                inicio = m.start(2) if m.group(1) and corpo_m[m.start() - 1:m.start()] == "&" else m.start()
                partes.append(corpo[pos:inicio])
                partes.append(simbolo)
                pos = m.end()
            if partes:
                partes.append(corpo[pos:])
                d.texto = d.texto[:abre] + "".join(partes)
    if enderecos:
        linhas.append("/* Endereços no host das funções usadas como valor (gen_imports.py, nei_fnaddr_). */")
        linhas += ["extern void* %s;" % s for s in sorted(enderecos)]
        linhas.append("")
    if funcoes:
        linhas.append("/* Prototipos locais para permitir qualquer ordem de definicao. */")
        linhas += [cabecalho_funcao(d) for d in funcoes]
        linhas.append("")
    # Tabela do fork sobreposta (sItemActions...) fica visível às outras unidades da DLL, que a declaram extern.
    linhas += [(remover_static(d.texto) if a.tabela and d.tipo == "variavel" and d.nome in sobrepoe else d.texto).rstrip()
               for d in selecionadas] + [""]
    desvios = [d for d in funcoes if d.nome in sobrepoe and "{" in d.mascara and d.nome not in a.sem_desvio]
    if a.tabela:
        linhas.append("/* Desvios: a função do host passa a entrar na cópia do fork (fork/overlay_glue.cpp). */")
        linhas.append('#include "overlay_table.h"')
        linhas.append("const NeiOverlayEntry nei_overlay_%s[] = {" % stem)
        linhas += ['    { "%s", (void*)&%s },' % ("%s.c!%s" % (stem, d.nome) if qualificado(d.nome) != d.nome
                                                  else d.nome, d.nome) for d in desvios] or ["    { 0, 0 },"]
        linhas.append("};")
        linhas.append("const unsigned nei_overlay_%s_count = %d;" % (stem, len(desvios)))
        linhas.append("")
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
           "Sobrepostas (o host tem, a DLL desvia): %s" % (", ".join(sorted(sobrepoe & escolhidas)) or "nenhuma"),
           "Copiadas por ausencia no soh.symbols (LTCG inlinou): %s" % (", ".join(sorted(inlinadas)) or "nenhuma"),
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
