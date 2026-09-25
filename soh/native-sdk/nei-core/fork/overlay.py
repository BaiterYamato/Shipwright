"""Funções do host que o fork NEI modificou e que a DLL sobrepõe (NEI-HOST-001).

Para cada fonte do host em soh/hookable_sources.txt (compilados sem inline e fora do LTCG), compara o arquivo no
commit do fork com o da base dele e escolhe:
  - as funções que o fork mudou;
  - as variáveis que o fork mudou (tabelas de ação, custos de magia...), que vão para a DLL junto;
  - as funções do host que usam uma dessas variáveis, para lerem a cópia da DLL e não a tabela velha do host.
Quando o host também mudou a função desde a base (upstream), a versão copiada é a mescla de três vias
(git merge-file host base fork); com conflito, a função fica de fora e o relatório diz qual.

Saída: um arquivo "fork mesclado" por fonte (extract_additions.py --fork-file) e as sementes com > na frente.
"""
import hashlib
import os
import re
import subprocess
import tempfile

from extract_additions import declaracoes_topo, ids, ler_git, mapa

HERE = os.path.dirname(os.path.abspath(__file__))


def normal(texto):
    return re.sub(r"\s+", " ", texto).strip()


def ler_excluir():
    """overlay-exclude.txt: `nome  # motivo` fica fora da sobreposição (nem cópia, nem desvio); `~nome  # motivo` é
    copiado, mas o desvio é de outro lugar (kaleido_glue.cpp). Devolve (excluir, sem_desvio)."""
    excluir, sem_desvio = {}, set()
    for linha in open(os.path.join(HERE, "overlay-exclude.txt"), encoding="utf-8"):
        nome, _, motivo = linha.partition("#")
        nome = nome.strip()
        if nome.startswith("arquivo "):  # arquivo inteiro fora: `arquivo z_file_choose.c  # motivo`
            excluir[nome] = motivo.strip()
        elif nome.startswith("~"):
            sem_desvio.add(nome[1:])
        elif nome:
            excluir[nome] = motivo.strip()
    return excluir, sem_desvio


def assinatura(host, base, fork):
    """Hash das três versões: uma resolução manual só vale para as versões em que foi feita."""
    return " ".join(hashlib.sha1(normal(x).encode("utf-8")).hexdigest()[:12] for x in (host, base, fork))


def resolucao(arquivo, nome, host, base, fork):
    """overlay-merges/<arquivo>.<nome>.c: a mescla feita à mão de uma função com conflito. A primeira linha é
    `/* overlay-merge <assinatura> */`; se o host, a base ou o fork mudarem, ela fica velha e volta a ser conflito.
    Devolve (texto ou None, motivo)."""
    stem = os.path.splitext(os.path.basename(arquivo))[0]
    caminho = os.path.join(HERE, "overlay-merges", "%s.%s.c" % (stem, nome))
    if not os.path.exists(caminho):
        return None, "sem resolução"
    with open(caminho, encoding="utf-8") as f:
        primeira, _, texto = f.read().partition("\n")
    if primeira.strip() != "/* overlay-merge %s */" % assinatura(host, base, fork):
        return None, "resolução velha"
    return texto, "resolvida"


def mesclar(host, base, fork):
    """Mescla de três vias de um trecho; devolve (texto, conflitos)."""
    with tempfile.TemporaryDirectory() as pasta:
        caminhos = []
        for nome, texto in (("host", host), ("base", base), ("fork", fork)):
            caminho = os.path.join(pasta, nome)
            with open(caminho, "w", encoding="utf-8", newline="\n") as f:
                f.write(texto)
            caminhos.append(caminho)
        r = subprocess.run(["git", "merge-file", "-p", *caminhos], capture_output=True)
        return r.stdout.decode("utf-8", errors="replace"), r.returncode


def funcoes(texto):
    return mapa(d for d in declaracoes_topo(texto) if d.tipo == "funcao" and "{" in d.mascara)


def variaveis(texto):
    return mapa(d for d in declaracoes_topo(texto) if d.tipo == "variavel" and not d.texto.lstrip().startswith("#"))


def planejar(repo, fork_commit, base_commit, arquivo, saida, excluir):
    """Devolve (caminho do fork mesclado, sementes, relatório) de um fonte do host."""
    if "arquivo " + os.path.basename(arquivo) in excluir:
        return None, [], {"arquivo": arquivo, "sobrepostas": [], "excluidas": ["(arquivo inteiro)"]}
    base_t = ler_git(repo, "show", "%s:%s" % (base_commit, arquivo))
    fork_t = ler_git(repo, "show", "%s:%s" % (fork_commit, arquivo))
    with open(os.path.join(repo, *arquivo.split("/")), encoding="utf-8", errors="replace") as f:
        host_t = f.read()
    bf, kf, hf = funcoes(base_t), funcoes(fork_t), funcoes(host_t)
    bv, kv = variaveis(base_t), variaveis(fork_t)
    mudadas = {n for n in kf if n in bf and normal(kf[n].texto) != normal(bf[n].texto)}
    dados = {n for n in kv if n in bv and normal(kv[n].texto) != normal(bv[n].texto)}
    # Global mudada pelo fork (gItemAgeReqs) fica a do host: o resto do jogo lê a do host pelo nome, e a cópia
    # da DLL só valeria para as funções copiadas. Só a static do arquivo vai para a DLL.
    # É a declaração do host que decide: o fork tirou o static de sItemActions para o extended_player.c ler.
    hv = variaveis(host_t)
    globais = {n for n in dados if n in hv and not re.match(r"\s*static\b", hv[n].mascara)}
    dados = {n for n in dados - globais if n not in excluir}
    leitoras = {n for n in kf if n in bf and n in hf and ids(kf[n].texto) & dados}
    rel = {"arquivo": arquivo, "mudadas": sorted(mudadas), "dados": sorted(dados), "globais_do_host": sorted(globais),
           "leitoras": sorted(leitoras - mudadas), "excluidas": [], "conflitos": [], "mescladas": [], "resolvidas": [], "adaptadas": [],
           "ausentes_no_host": []}
    escolhidas = set()
    for nome in sorted(mudadas | leitoras):
        if nome in excluir:
            rel["excluidas"].append(nome)
        elif nome not in hf:
            rel["ausentes_no_host"].append(nome)
        else:
            escolhidas.add(nome)
    # A mescla troca, no texto do fork, cada função escolhida que o host também mudou desde a base.
    trocas = []
    for nome in sorted(escolhidas):
        if normal(hf[nome].texto) == normal(bf[nome].texto):
            continue
        texto, conflitos = mesclar(hf[nome].texto, bf[nome].texto, kf[nome].texto)
        # A resolução à mão vale também sobre uma mescla limpa mas incoerente (o upstream renomeou o parâmetro
        # itemAction e o trecho novo do fork usa o nome antigo, actionParam).
        manual, motivo = resolucao(arquivo, nome, hf[nome].texto, bf[nome].texto, kf[nome].texto)
        if manual is not None:
            texto, conflitos = manual, 0
            rel["resolvidas"].append(nome)
        elif conflitos:
            escolhidas.discard(nome)
            rel["conflitos"].append("%s(%s)" % (nome, motivo))
            continue
        else:
            rel["mescladas"].append(nome)
        trocas.append((kf[nome].inicio, kf[nome].fim, texto))
    mesclado = fork_t
    for inicio, fim, texto in sorted(trocas, reverse=True):
        mesclado = mesclado[:inicio] + texto.rstrip("\n") + mesclado[fim:]
    # overlay-extra/<arquivo>.<nome>.c: função que só o host tem (o host refatorou o trecho que o fork mudou, como o
    # Actor_DrawListEntry do Link-Span), adaptada à mão com a mudança do fork. Vale enquanto a do host não mudar.
    stem = os.path.splitext(os.path.basename(arquivo))[0]
    pasta_extra = os.path.join(HERE, "overlay-extra")
    for nome_extra in sorted(os.listdir(pasta_extra)) if os.path.isdir(pasta_extra) else []:
        if not nome_extra.startswith(stem + ".") or not nome_extra.endswith(".c"):
            continue
        nome = nome_extra[len(stem) + 1:-2]
        with open(os.path.join(pasta_extra, nome_extra), encoding="utf-8") as f:
            primeira, _, texto = f.read().partition("\n")
        if nome not in hf or nome in kf:
            rel["conflitos"].append("%s(adaptação sem função só do host)" % nome)
        elif primeira.strip() != "/* overlay-extra %s */" % assinatura(hf[nome].texto, "", ""):
            rel["conflitos"].append("%s(adaptação velha)" % nome)
        else:
            mesclado += "\n\n" + texto.rstrip("\n") + "\n"
            escolhidas.add(nome)
            rel["adaptadas"].append(nome)
    destino = os.path.join(saida, *arquivo.split("/"))
    os.makedirs(os.path.dirname(destino), exist_ok=True)
    with open(destino, "w", encoding="utf-8", newline="\n") as f:
        f.write(mesclado)
    # Função que o fork acrescentou a este arquivo (EnButte_NetForceTransform, SwitchHook_PlayerNoClip) e que o
    # código dele chama de fora: vai junto, sem desvio (o host não tem).
    novas = sorted(n for n in kf if n not in bf and n not in hf and n not in excluir)
    rel["novas"] = novas
    sementes = [">" + n for n in sorted(escolhidas)] + [">" + n for n in sorted(dados)] + novas
    rel["sobrepostas"] = sorted(escolhidas)
    return destino, sementes, rel


def fontes_sobreponiveis(repo):
    caminho = os.path.join(repo, "soh", "hookable_sources.txt")
    return ["soh/" + l.strip() for l in open(caminho, encoding="utf-8")
            if l.strip() and not l.lstrip().startswith("#")]
