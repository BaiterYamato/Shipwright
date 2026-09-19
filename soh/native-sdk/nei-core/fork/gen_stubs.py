"""Gera stubs C para símbolos ausentes de um fork.

Uso:
    python gen_stubs.py --names nomes.txt --search fork/soh --search compat \
        --out stubs.c --report stubs.txt [--exclude pular.txt]

Os diretórios de busca são lidos, nunca alterados. O arquivo gerado pressupõe
que os tipos normais do jogo já foram incluídos à força (por exemplo, global.h).
"""
import argparse
import os
import re
import sys


EXTENSOES_HEADER = {".h", ".hpp"}
EXTENSOES_FONTE = {".c", ".cpp"}
TOKENS_CPP = re.compile(
    r"\b(namespace|class|template|typename|std|constexpr|using|public|private|protected|"
    r"virtual|override|decltype|auto)\b|::|\bnew\b|\bdelete\b"
)
TIPOS_ESCALARES = {
    "_Bool", "bool", "char", "signed", "unsigned", "short", "int", "long", "float", "double",
    "size_t", "ptrdiff_t", "intptr_t", "uintptr_t", "int8_t", "uint8_t", "int16_t", "uint16_t",
    "int32_t", "uint32_t", "int64_t", "uint64_t", "s8", "u8", "s16", "u16", "s32", "u32",
    "s64", "u64", "f32", "f64", "s128", "u128",
}
# Tipos que global.h e os includes forçados do host já oferecem. Ponteiros para
# tipos incompletos não precisam de include, mas estes nomes também podem surgir
# por valor nas declarações antigas do jogo.
TIPOS_DO_HOST = {
    "Actor", "ActorContext", "PlayState", "GameState", "GraphicsContext", "Gfx", "Mtx", "MtxF",
    "Vec2f", "Vec3f", "Vec3s", "Vec3i", "Color_RGBA8", "Color_RGB8", "CollisionPoly", "CollisionContext",
    "SaveContext", "Player", "Input", "LightNode", "LightInfo", "AnimationHeader", "SkeletonHeader",
    "FlexSkeletonHeader", "SkelAnime", "CollisionHeader", "CollisionCheckContext", "DynaPolyActor",
    "CutsceneContext", "Camera", "Path", "Vtx", "TexturePtr", "OSTime", "OSMesgQueue",
}
QUALIFICADORES = {"const", "volatile", "restrict", "register", "_Atomic", "struct", "union", "enum"}
ARMAZENAMENTO = re.compile(r"^\s*(?:(?:extern|static|inline|__inline|__forceinline)\s+)+")


def ler_nomes(caminho):
    """Lê uma lista, ignorando comentários e repetidos sem mudar a ordem."""
    vistos, nomes = set(), []
    with open(caminho, encoding="utf-8", errors="replace") as arquivo:
        for linha in arquivo:
            nome = linha.split("#", 1)[0].strip()
            if nome and nome not in vistos:
                vistos.add(nome)
                nomes.append(nome)
    return nomes


def sem_comentarios(texto):
    """Remove comentários sem deslocar linhas, útil para relatórios e scanner.

    Uma passada só, na ordem do texto: um /* dentro de comentário de linha (ou de string) não abre bloco.
    """
    def trocar(m):
        s = m.group(0)
        return s if s[0] in "\"'" else "\n" * s.count("\n")
    return re.sub(r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'", trocar, texto, flags=re.S)


def sem_ramos_cpp(texto):
    """Descarta apenas o ramo garantidamente C++ de condicionais __cplusplus.

    O padrão extern \"C\" em volta de uma declaração comum é removido, não a
    declaração. Outras condicionais ficam: escolher um ramo delas exigiria
    executar o pré-processador do fork.
    """
    linhas, resultado, pilha = sem_comentarios(texto).splitlines(), [], []
    ativo = True
    continua_macro = False
    blocos_extern_c = 0
    for linha in linhas:
        s = linha.strip()
        if continua_macro:
            resultado.append("")
            continua_macro = linha.rstrip().endswith("\\")
            continue
        inicio_cpp = re.match(r"#\s*if\s+(?:defined\s*\(?\s*__cplusplus\s*\)?|__cplusplus)\b", s)
        inicio_cpp = inicio_cpp or re.match(r"#\s*ifdef\s+__cplusplus\b", s)
        inicio_not_cpp = re.match(r"#\s*ifndef\s+__cplusplus\b", s)
        if inicio_cpp or inicio_not_cpp:
            ramo_c = bool(inicio_not_cpp)
            pilha.append((ativo, ramo_c, True))
            ativo = ativo and ramo_c
            resultado.append("")
            continue
        if re.match(r"#\s*if", s):
            pilha.append((ativo, True, False))
            resultado.append("")
            continue
        if re.match(r"#\s*elif", s) and pilha:
            pai, ramo_c, eh_cpp = pilha[-1]
            if eh_cpp:
                ramo_c = not bool(re.search(r"__cplusplus", s))
                pilha[-1] = (pai, ramo_c, True)
                ativo = pai and ramo_c
            resultado.append("")
            continue
        if re.match(r"#\s*else\b", s) and pilha:
            pai, ramo_c, eh_cpp = pilha[-1]
            if eh_cpp:
                ramo_c = not ramo_c
                pilha[-1] = (pai, ramo_c, True)
                ativo = pai and ramo_c
            resultado.append("")
            continue
        if re.match(r"#\s*endif\b", s) and pilha:
            pai, _, _ = pilha.pop()
            ativo = pai
            resultado.append("")
            continue
        if not ativo:
            resultado.append("")
            continue
        if s.startswith("#"):
            resultado.append("")
            continua_macro = linha.rstrip().endswith("\\")
            continue
        if re.match(r'^\s*extern\s+"C"\s*\{\s*$', linha):
            blocos_extern_c += 1
            resultado.append("")
            continue
        if blocos_extern_c and s == "}":
            blocos_extern_c -= 1
            resultado.append("")
            continue
        # Pode estar em linha única. Blocos extern "C" envolvidos por
        # __cplusplus já foram descartados acima; não remova chaves soltas,
        # pois elas também fecham funções C normais.
        linha = re.sub(r'\bextern\s+"C"\s*', "", linha)
        resultado.append(linha)
    return "\n".join(resultado) + "\n"


def posicao_linha(texto, posicao):
    return texto.count("\n", 0, posicao) + 1


def declaracoes_topo(texto):
    """Devolve (linha, texto, tem_corpo) para sentenças C de nível superior."""
    texto = sem_ramos_cpp(texto)
    resultado = []
    inicio = None
    parenteses = colchetes = chaves = 0
    i, n = 0, len(texto)
    while i < n:
        c = texto[i]
        if inicio is None:
            if not c.isspace() and c != '#':
                inicio = i
            i += 1
            continue
        if c == '(':
            parenteses += 1
        elif c == ')':
            parenteses = max(0, parenteses - 1)
        elif c == '[':
            colchetes += 1
        elif c == ']':
            colchetes = max(0, colchetes - 1)
        elif c == '{' and parenteses == 0 and colchetes == 0:
            cabeca = texto[inicio:i].strip()
            if cabeca:
                resultado.append((posicao_linha(texto, inicio), cabeca, True))
            # Pula o corpo/initializer completo. Se for typedef struct, isto
            # impede que seus membros sejam confundidos com declarações globais.
            chaves = 1
            i += 1
            while i < n and chaves:
                if texto[i] == '{':
                    chaves += 1
                elif texto[i] == '}':
                    chaves -= 1
                i += 1
            while i < n and texto[i].isspace():
                i += 1
            if i < n and texto[i] == ';':
                i += 1
            inicio = None
            parenteses = colchetes = chaves = 0
            continue
        elif c == ';' and parenteses == 0 and colchetes == 0:
            resultado.append((posicao_linha(texto, inicio), texto[inicio:i + 1].strip(), False))
            inicio = None
        i += 1
    return resultado


def limpar_armazenamento(texto):
    texto = texto.strip()
    anterior = None
    while texto != anterior:
        anterior = texto
        texto = ARMAZENAMENTO.sub("", texto)
    return texto.strip()


def normalizar(texto):
    return re.sub(r"\s+", " ", texto).strip()


def separar_parametros(texto):
    """Separa vírgulas sem dividir arrays, ponteiros de função ou macros."""
    if not texto.strip() or texto.strip() == "void":
        return []
    partes, inicio, nivel = [], 0, 0
    for i, c in enumerate(texto):
        if c in "([":
            nivel += 1
        elif c in ")]":
            nivel = max(0, nivel - 1)
        elif c == ',' and nivel == 0:
            partes.append(texto[inicio:i].strip())
            inicio = i + 1
    partes.append(texto[inicio:].strip())
    return partes


def parametro_com_nome(parametro, indice):
    """Acrescenta pN a uma declaração de parâmetro que não tem identificador."""
    parametro = parametro.strip()
    if not parametro or parametro == "void" or parametro == "...":
        return parametro
    if re.search(r"\(\s*\*\s*[A-Za-z_]\w*\s*\)", parametro):
        return parametro
    if re.search(r"\(\s*\*\s*\)", parametro):
        return re.sub(r"\(\s*\*\s*\)", "(*p%d)" % indice, parametro, count=1)
    # Um nome de parâmetro fica ao final, antes de um possível sufixo de array.
    m = re.search(r"\b([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)?$", parametro)
    if not m:
        return parametro + " p%d" % indice
    antes = parametro[:m.start()].rstrip()
    ultimo = m.group(1)
    if not antes or ultimo in TIPOS_ESCALARES or ultimo in QUALIFICADORES or re.search(r"\b(?:struct|union|enum)\s*$", antes):
        return parametro + " p%d" % indice
    # const MeuTipo* não termina em identificador; o ramo acima já o nomeia.
    return parametro


def assinatura_funcao(nome, declaracao):
    """Converte uma declaração simples em (retorno, parâmetros) ou None."""
    texto = declaracao.strip().rstrip(';').strip()
    texto = limpar_armazenamento(texto)
    if TOKENS_CPP.search(texto):
        return None
    padrao = re.compile(r"^(?P<ret>.*?)\b" + re.escape(nome) + r"\s*\((?P<args>.*)\)\s*(?:__attribute__\s*\(\(.*\)\))?$", re.S)
    achado = padrao.match(texto)
    if not achado or not achado.group("ret").strip():
        return None
    retorno = achado.group("ret").strip()
    # Funções ponteiro e macros de C++ não são o contrato simples pedido.
    if retorno.endswith("*") and "(" in retorno:
        return None
    parametros = [parametro_com_nome(p, i) for i, p in enumerate(separar_parametros(achado.group("args")))]
    return retorno, parametros


def assinatura_variavel(nome, declaracao, tem_corpo):
    """Converte extern ou definição de variável em (tipo, dimensões) ou None."""
    texto = limpar_armazenamento(declaracao.strip().rstrip(';').strip())
    if TOKENS_CPP.search(texto) or '(' in texto or not re.search(r"\b" + re.escape(nome) + r"\b", texto):
        return None
    padrao = re.compile(r"^(?P<tipo>.*?)\b" + re.escape(nome) + r"\b(?P<dims>(?:\s*\[[^\]]*\])*)\s*(?:=\s*)?$", re.S)
    achado = padrao.match(texto)
    if not achado or not achado.group("tipo").strip():
        return None
    tipo = achado.group("tipo").strip()
    # Sem extern, só é aceito se o scanner viu um initializer/corpo: evita
    # transformar uma declaração local incompleta em global.
    if not tem_corpo and not re.match(r"^\s*extern\b", declaracao):
        return None
    # Display list precisa só do G_ENDDL; outro array de tamanho desconhecido (textura, tabela) ganha 4 KB
    # zerados, para quem lê além do primeiro elemento não sair da variável.
    vazio = "[1]" if eh_display_list(tipo) else "[0x1000]"
    dims = re.sub(r"\[\s*\]", vazio, achado.group("dims"))
    return tipo, dims


def eh_display_list(tipo):
    return re.sub(r"\bconst\b", "", tipo).strip() == "Gfx"


def coletar_tipos(headers):
    """Mapeia tipos declarados nos headers e marca aliases escalares/enums."""
    donos, escalares, enums = {}, set(TIPOS_ESCALARES), set()
    for caminho in headers:
        try:
            with open(caminho, encoding="utf-8", errors="replace") as arquivo:
                texto = sem_comentarios(arquivo.read())
        except OSError:
            continue
        for nome in re.findall(r"\b(?:typedef\s+)?(?:struct|union)\s+([A-Za-z_]\w*)", texto):
            donos.setdefault(nome, caminho)
        for nome in re.findall(r"\b(?:typedef\s+)?enum\s+(?:class\s+)?([A-Za-z_]\w*)", texto):
            donos.setdefault(nome, caminho)
            enums.add(nome)
        for nome in re.findall(r"\}\s*([A-Za-z_]\w*)\s*;", texto):
            # Pode ser alias de struct ou enum; incluir o header é seguro.
            donos.setdefault(nome, caminho)
        for base, nome in re.findall(r"\btypedef\s+([^;{}]+?)\s+([A-Za-z_]\w*)\s*;", texto, re.S):
            donos.setdefault(nome, caminho)
            palavras = set(re.findall(r"\b[A-Za-z_]\w*\b", base))
            if palavras & TIPOS_ESCALARES or "enum" in palavras:
                escalares.add(nome)
                if "enum" in palavras:
                    enums.add(nome)
    return donos, escalares, enums


def tipos_usados(texto):
    return set(re.findall(r"\b[A-Za-z_]\w*\b", texto))


def retorno_escalar(retorno, escalares, enums):
    sem_macro = re.sub(r"\b[A-Za-z_]\w*\s*\(([^()]*)\)", r" \1 ", retorno)
    palavras = tipos_usados(sem_macro)
    return bool(palavras & escalares or palavras & enums)


def tipo_de_parametro(parametro):
    """Remove o nome de parâmetro para comparar assinaturas, quando houver."""
    parametro = normalizar(parametro)
    parametro = re.sub(r"\(\s*\*\s*[A-Za-z_]\w*\s*\)", "(*)", parametro)
    if "(*)" in parametro:
        return parametro
    m = re.search(r"\b([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?$", parametro)
    if not m:
        return parametro
    antes, ultimo = parametro[:m.start()].rstrip(), m.group(1)
    if antes and ultimo not in TIPOS_ESCALARES and ultimo not in QUALIFICADORES and not re.search(
            r"\b(?:struct|union|enum)\s*$", antes):
        return antes
    return parametro


def chave_declaracao(achado):
    """Chave sem nomes de parâmetros: não relata diferença apenas cosmética."""
    categoria, dados = achado[0], achado[1]
    if categoria == "variavel":
        return categoria + ":" + normalizar(dados[0]) + normalizar(dados[1])
    retorno, parametros = dados
    return categoria + ":" + normalizar(retorno) + "(" + ",".join(
        tipo_de_parametro(parametro) for parametro in parametros) + ")"


def corpo_funcao(retorno, escalares, enums):
    if '*' in retorno:
        return "    return NULL;"
    if re.search(r"\bvoid\b", retorno) and not retorno_escalar(retorno, escalares, enums):
        return ""
    if retorno_escalar(retorno, escalares, enums):
        return "    return 0;"
    return "    %s r = { 0 };\n    return r;" % retorno


def caminhos_de_busca(diretorios):
    headers, fontes = [], []
    for diretorio in diretorios:
        for raiz, subdirs, arquivos in os.walk(diretorio):
            subdirs.sort()
            for nome in sorted(arquivos):
                caminho = os.path.abspath(os.path.join(raiz, nome))
                extensao = os.path.splitext(nome)[1].lower()
                if extensao in EXTENSOES_HEADER:
                    headers.append(caminho)
                elif extensao in EXTENSOES_FONTE:
                    fontes.append(caminho)
    return sorted(set(headers)), sorted(set(fontes))


def relativo_include(caminho, diretorios):
    candidatos = []
    for diretorio in diretorios:
        try:
            relativo = os.path.relpath(caminho, diretorio)
        except ValueError:
            continue
        if not relativo.startswith(".." + os.sep) and relativo != "..":
            candidatos.append(relativo.replace(os.sep, "/"))
    return min(candidatos, key=lambda p: (p.count("/"), p)) if candidatos else os.path.basename(caminho)


def achar_declaracoes(nome, arquivos):
    """Procura headers primeiro; fontes só complementam o que não veio deles."""
    encontrados = []
    palavra = re.compile(r"\b" + re.escape(nome) + r"\b")
    for caminho in arquivos:
        try:
            with open(caminho, encoding="utf-8", errors="replace") as arquivo:
                texto = arquivo.read()
        except OSError:
            continue
        if not palavra.search(texto):
            continue
        for linha, declaracao, tem_corpo in declaracoes_topo(texto):
            if not palavra.search(declaracao):
                continue
            funcao = assinatura_funcao(nome, declaracao)
            if funcao:
                encontrados.append(("funcao", funcao, caminho, linha, normalizar(declaracao)))
                continue
            variavel = assinatura_variavel(nome, declaracao, tem_corpo)
            if variavel:
                encontrados.append(("variavel", variavel, caminho, linha, normalizar(declaracao)))
    return encontrados


def gerar(args):
    nomes = ler_nomes(args.names)
    excluidos = set(ler_nomes(args.exclude)) if args.exclude else set()
    nomes = [nome for nome in nomes if nome not in excluidos]
    diretorios = [os.path.abspath(d) for d in args.search]
    headers, fontes = caminhos_de_busca(diretorios)
    donos, escalares, enums = coletar_tipos(headers)
    relatorio, stubs, includes = [], [], []
    usados = set()
    sem_declaracao = []
    conflitos = []
    for nome in nomes:
        achados = achar_declaracoes(nome, headers)
        if not achados:
            achados = achar_declaracoes(nome, fontes)
        if not achados:
            sem_declaracao.append(nome)
            continue
        escolhido = achados[0]
        chave_escolhida = chave_declaracao(escolhido)
        diferentes = [a for a in achados[1:] if chave_declaracao(a) != chave_escolhida]
        if diferentes:
            lugares = ", ".join("%s:%d" % (a[2], a[3]) for a in diferentes)
            conflitos.append("%s: usada %s:%d; diferente em %s" % (nome, escolhido[2], escolhido[3], lugares))
        categoria, dados, caminho, linha, _ = escolhido
        if nome in usados:
            continue
        usados.add(nome)
        assinatura = (dados[0] + " " + " ".join(dados[1])) if categoria == "funcao" else dados[0]
        # Dimensão de array com macro (gX[N_MAX]): o header da própria declaração traz o valor.
        if categoria == "variavel" and re.search(r"[A-Za-z_]", dados[1]) and os.path.splitext(caminho)[1] in EXTENSOES_HEADER:
            include = relativo_include(caminho, diretorios)
            if include not in includes:
                includes.append(include)
                relatorio.append("include do fork: %s (dimensão de %s)" % (include, nome))
        for tipo in tipos_usados(assinatura):
            dono = donos.get(tipo)
            if dono and tipo not in TIPOS_DO_HOST and tipo not in TIPOS_ESCALARES:
                include = relativo_include(dono, diretorios)
                if include not in includes:
                    includes.append(include)
                    relatorio.append("include do fork: %s (tipo %s)" % (include, tipo))
        stubs.append((nome, categoria, dados, caminho, linha))
    linhas = [
        "// Gerado por gen_stubs.py: stubs de recurso ausente do fork NEI.",
        "// Tipos normais do jogo chegam pelos includes forçados do host.",
    ]
    if includes:
        linhas += ["", *['#include "%s"' % include for include in includes]]
    for nome, categoria, dados, caminho, _ in stubs:
        linhas += ["", "// %s: %s" % (nome, caminho.replace(os.sep, "/"))]
        if categoria == "funcao":
            retorno, parametros = dados
            linhas.append("%s %s(%s) {" % (retorno, nome, ", ".join(parametros) if parametros else "void"))
            corpo = corpo_funcao(retorno, escalares, enums)
            if corpo:
                linhas.extend(corpo.splitlines())
            linhas.append("}")
        else:
            tipo, dims = dados
            # Display list zerada não termina: o renderer seguiria lendo. Stub desenha nada e para.
            valor = "{ gsSPEndDisplayList() }" if eh_display_list(tipo) and dims else "{ 0 }"
            linhas.append("%s %s%s = %s;" % (tipo, nome, dims, valor))
    if sem_declaracao:
        relatorio.append("sem declaração: " + ", ".join(sem_declaracao))
    relatorio.extend("declaração divergente: " + item for item in conflitos)
    n_funcoes = sum(1 for _, cat, *_ in stubs if cat == "funcao")
    n_variaveis = sum(1 for _, cat, *_ in stubs if cat == "variavel")
    resumo = [
        "Resumo: %d stubs de função; %d stubs de variável; %d sem declaração; %d includes de header do fork."
        % (n_funcoes, n_variaveis, len(sem_declaracao), len(includes)),
        "Sem declaração: " + (", ".join(sem_declaracao) if sem_declaracao else "nenhum"),
        "Includes do fork: " + (", ".join(includes) if includes else "nenhum"),
    ]
    with open(args.out, "w", encoding="utf-8", newline="\n") as arquivo:
        arquivo.write("\n".join(linhas) + "\n")
    with open(args.report, "w", encoding="utf-8", newline="\n") as arquivo:
        arquivo.write("\n".join(resumo + ([""] + relatorio if relatorio else [])) + "\n")
    print("\n".join(resumo))
    return n_funcoes, n_variaveis, sem_declaracao, includes


def argumentos():
    parser = argparse.ArgumentParser(description="Gera stubs C de símbolos ausentes do fork.")
    parser.add_argument("--names", required=True, help="arquivo de nomes, um por linha")
    parser.add_argument("--search", required=True, action="append", help="diretório de busca recursiva")
    parser.add_argument("--out", required=True, help="arquivo C gerado")
    parser.add_argument("--report", required=True, help="relatório textual")
    parser.add_argument("--exclude", help="arquivo opcional de nomes a pular")
    return parser.parse_args()


if __name__ == "__main__":
    try:
        gerar(argumentos())
    except (OSError, ValueError) as erro:
        print("erro: %s" % erro, file=sys.stderr)
        sys.exit(2)
