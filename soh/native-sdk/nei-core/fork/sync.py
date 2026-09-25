"""Prepara o código do fork NEI (skijer/Not-Enough-Items) para compilar dentro da DLL do linkspan.nei.

O fonte não é copiado para o repositório: sai do commit fixo FORK_COMMIT (precisa estar no git do host:
`git fetch https://github.com/skijer/Not-Enough-Items c29262b`), recebe os patches de patches/ e ganha a
camada de compatibilidade de gen_compat.py. Saída em <out>/fork (fonte) e <out>/compat (/FI e sombras).

Também gera <out>/compat/nei_host_data.h: cada variável global declarada `extern` nos headers do host que
existe como [data] no soh.symbols vira `#define gX (*nei_host_gX)`. Incluído antes do global.h, transforma a
declaração do host num ponteiro que gen_imports.py manda resolver no init da DLL.

Por fim extrai as funções que o fork acrescentou em arquivos do host (extract.txt -> <out>/extracted) e gera os
stubs dos subsistemas que ficaram de fora (stub-names.txt -> <out>/nei_stubs.c).

Uso: python sync.py <repo-do-host> <out> <soh.symbols>
"""
import os
import re
import shutil
import subprocess
import sys

FORK_COMMIT = "c29262b"
FORK_BASE = "783139310"  # develop do Shipwright em que o fork se baseia
TREES = ["soh/mods", "soh/expansions/NEI", "soh/expansions/sw97", "soh/expansions/trirod", "soh/soh/NEI"]
HERE = os.path.dirname(os.path.abspath(__file__))


def git(repo, *args, binary=False):
    result = subprocess.run(["git", "-C", repo, *args], capture_output=True, check=True)
    return result.stdout if binary else result.stdout.decode("utf-8", errors="replace")


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


# Nomes que também aparecem como variável local em macros do host (OPEN_DISPS declara __gfxCtx).
DATA_EXCLUDE = {"__gfxCtx"}


def host_data_macros(repo, symbols):
    data = set()
    for line in open(symbols, encoding="utf-8", errors="replace"):
        parts = line.rstrip("\n").split("\t")
        # "[data]" no LTCG, "[data]<arquivo>" nos fontes do host compilados fora dele (hookable_sources.txt).
        if len(parts) == 4 and parts[2].startswith("[data]") and parts[1] == "-":
            data.add(parts[3])
    names = set()
    declaracoes = {}
    plain = re.compile(r"^\s*extern\s+(?!\"C\")[^;()=]*?\b([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)*;")
    pointer = re.compile(r"^\s*extern\s+(?!\"C\")[^;=]*\(\s*\*\s*([A-Za-z_]\w*)\s*\)\s*\(")
    for folder in ("soh/include", "soh/soh", "soh/src"):
        for root, _, files in os.walk(os.path.join(repo, folder)):
            for file in files:
                if not file.endswith(".h"):
                    continue
                for line in open(os.path.join(root, file), encoding="utf-8", errors="replace"):
                    if "##" in line or line.rstrip().endswith("\\") or "std::" in line:
                        continue
                    m = plain.match(line) or pointer.match(line)
                    if m and m.group(1) in data and m.group(1) not in DATA_EXCLUDE:
                        names.add(m.group(1))
                        declaracoes.setdefault(m.group(1), line.strip())
    lines = ["// Gerado por sync.py: variáveis globais do host lidas por ponteiro resolvido no init da DLL.",
             "// Incluído antes do global.h: `extern T gX;` vira `extern T (*nei_host_gX);`.", "#pragma once"]
    lines += [f"#define {n} (*nei_host_{n})" for n in sorted(names)]
    return "\n".join(lines) + "\n", declaracoes


def alinhar_redeclaracoes_de_array(raiz, declaracoes):
    """Faz a declaração local de um array do host usar o mesmo subscrito do header do host.

    O nei_host_data.h transforma `gX` em `(*nei_host_gX)`, então `extern void* gX[];` escrito dentro
    do fork vira um ponteiro para array **incompleto**, tipo diferente do `[158]` que o header do host
    declara, e o compilador recusa com C2369. Apagar a linha não serve: às vezes ela é a única
    declaração que aquele arquivo enxerga (o gEquipAgeReqs só aparece no z_kaleido_scope.h). Trocar
    pela declaração do próprio host resolve os dois casos, porque as duas passam a expandir igual."""
    padrao = re.compile(r"^([ \t]*)extern\s+[^;=(){}]*?\b(%s)\b\s*(?:\[[^\]]*\]\s*)+;[ \t]*$"
                        % "|".join(sorted(declaracoes)), re.M)

    def troca(m):
        host = declaracoes[m.group(2)]
        return m.group(1) + host if host != m.group(0).strip() else m.group(0)

    total = 0
    for root, _, files in os.walk(raiz):
        for file in files:
            if not file.endswith((".c", ".h", ".cpp", ".inc")):
                continue
            path = os.path.join(root, file)
            with open(path, encoding="utf-8", errors="replace") as f:
                texto = f.read()
            novo, n = padrao.subn(troca, texto)
            if novo != texto:
                with open(path, "w", encoding="utf-8", newline="") as f:
                    f.write(novo)
                total += n
    return total


def aplicar_substituicoes(out):
    """extracted-fixes.txt: troca exata de texto no código extraído (que não passa pelos patches, porque sai do
    commit do fork direto para <out>/extracted). Cada linha é `arquivo ::: original ::: novo`, e o original precisa
    aparecer exatamente uma vez; senão o sync falha, em vez de compilar a versão sem a troca."""
    total = 0
    for line in open(os.path.join(HERE, "extracted-fixes.txt"), encoding="utf-8"):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        name, original, novo = (part.strip() for part in line.split(" ::: "))
        path = os.path.join(out, "extracted", name)
        with open(path, encoding="utf-8", newline="") as f:
            texto = f.read()
        if texto.count(original) != 1:
            sys.exit(f"extracted-fixes: '{original}' aparece {texto.count(original)} vezes em {name}")
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(texto.replace(original, novo))
        total += 1
    return total


def converter_modelos_c(repo, out):
    """Os modelos que o fork escreveu em C (cmodels.MODEL_FILES) viram caminhos de recurso: <out>/nei_cmodels.c
    define cada array como `const char x[] = "__OTR__..."`, o arquivo do modelo passa a só declarar os nomes e os
    headers da pasta trocam `extern Gfx x[]` por `extern const char x[]`. Os recursos saem do build-nei-assets.py,
    com o mesmo mapeamento (mesmo commit, mesma lista de assets)."""
    sys.path.insert(0, HERE)
    import cmodels
    mods = os.path.join(out, "fork", "soh", "mods")
    texts = {}
    for model_file in cmodels.MODEL_FILES:
        with open(os.path.join(mods, model_file), encoding="utf-8") as f:
            texts[model_file] = f.read()
    mapping, convert = cmodels.plan(texts, cmodels.fork_asset_paths(repo, FORK_BASE, FORK_COMMIT))
    with open(os.path.join(out, "nei_cmodels.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write(cmodels.c_definitions(texts, mapping))
    names = set()
    for model_file, text in texts.items():
        declared = [name for _, name, _ in cmodels.parse_arrays(text)]
        names.update(declared)
        with open(os.path.join(mods, model_file), "w", encoding="utf-8", newline="\n") as f:
            f.write("/* cmodels.py (sync.py): os arrays deste modelo viraram recursos do nei-assets-core.o2r e os\n"
                    "   caminhos ficam em nei_cmodels.c. */\n")
            f.write("".join("extern const char %s[];\n" % name for name in declared))
    headers = 0
    for folder in {os.path.dirname(p) for p in cmodels.MODEL_FILES}:
        for name in os.listdir(os.path.join(mods, folder)):
            if not name.endswith(".h"):
                continue
            path = os.path.join(mods, folder, name)
            with open(path, encoding="utf-8", newline="") as f:
                text = f.read()
            novo = cmodels.header_declarations(text, names)
            if novo != text:
                with open(path, "w", encoding="utf-8", newline="") as f:
                    f.write(novo)
                headers += 1
    # object_shovel.c guarda o endereço da display list num Gfx*; agora é o caminho, então precisa do cast.
    path = os.path.join(mods, "items", "objects", "object_shovel.c")
    with open(path, encoding="utf-8", newline="") as f:
        text = f.read()
    original = "Gfx* gShovelGiveDL = gDampeShovelDL_mesh_001_opaque_dl;"
    if text.count(original) != 1:
        sys.exit("cmodels: object_shovel.c mudou; revise o cast de gShovelGiveDL")
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text.replace(original, "Gfx* gShovelGiveDL = (Gfx*)gDampeShovelDL_mesh_001_opaque_dl;"))
    return len(names), len(convert), headers


ACTOR_EXPR = r"[A-Za-z_]\w*(?:(?:->|\.)\w+|\[[^\]\n]*\])*?"
ACTOR_SET = re.compile(r"(?<![\w.>])(" + ACTOR_EXPR + r")(->|\.)(update|draw|destroy)\s*=(?!=)\s*([^;\n]+);")
ACTOR_ALIVE = re.compile(r"(?<![\w.>])(" + ACTOR_EXPR + r")->update\s*(==|!=)\s*NULL\b")
ACTOR_SPAWN = re.compile(r"(?<![\w.>])(Actor_Spawn|Actor_SpawnAsChild)\s*\(")


def proteger_atores(out):
    """NEI-006: o código do fork que entra na DLL passa pela guarda de atores (fork/actor_guard.c). Troca de
    update/draw/destroy vira NeiActor_Set*, Actor_Spawn vira NeiActor_Spawned(Actor_Spawn(...)) e o teste de vida
    `p->update == NULL` vira !NeiActor_IsAlive(p), que não lê ator já liberado."""
    def campo(m):
        alvo = m.group(1) if m.group(2) == "->" else "&" + m.group(1)
        funcao = {"update": "NeiActor_SetUpdate", "draw": "NeiActor_SetDraw", "destroy": "NeiActor_SetDestroy"}
        return "%s(%s, %s);" % (funcao[m.group(3)], alvo, m.group(4).strip())

    def spawns(texto):
        partes, pos, total = [], 0, 0
        for m in ACTOR_SPAWN.finditer(texto):
            if m.start() < pos:
                continue
            inicio_linha = texto.rfind("\n", 0, m.start()) + 1
            if "//" in texto[inicio_linha:m.start()] or texto[inicio_linha:m.start()].lstrip().startswith(("*", "#")):
                continue
            nivel, i = 0, m.end() - 1
            while i < len(texto):
                if texto[i] == "(":
                    nivel += 1
                elif texto[i] == ")":
                    nivel -= 1
                    if nivel == 0:
                        break
                i += 1
            partes.append(texto[pos:m.start()])
            partes.append("NeiActor_Spawned(" + texto[m.start():i + 1] + ")")
            pos = i + 1
            total += 1
        partes.append(texto[pos:])
        return "".join(partes), total

    contagem = {"set": 0, "vivo": 0, "spawn": 0, "arquivos": 0}
    # As funções do host copiadas para a DLL (extracted/, overlay.py) só passam pela troca de update/draw/destroy,
    # que registra quem aponta para a DLL. O teste de vida e o spawn delas são a lógica do próprio jogo
    # (Actor_UpdateAll apaga o ator com update NULL): continuam como no host.
    raizes = [(os.path.join(out, "fork", "soh", "mods"), True), (os.path.join(out, "extracted"), False)]
    for raiz, completo in raizes:
        for pasta, _, arquivos in os.walk(raiz):
            if os.path.relpath(pasta, raiz).replace("\\", "/").startswith("mm_sources"):
                continue
            for nome in arquivos:
                if not nome.endswith((".c", ".h", ".inc")):
                    continue
                caminho = os.path.join(pasta, nome)
                with open(caminho, encoding="utf-8", errors="surrogateescape", newline="") as f:
                    texto = f.read()
                novo, n_set = ACTOR_SET.subn(campo, texto)
                n_vivo = n_spawn = 0
                if completo:
                    novo, n_vivo = ACTOR_ALIVE.subn(
                        lambda m: ("!" if m.group(2) == "==" else "") + "NeiActor_IsAlive(%s)" % m.group(1), novo)
                    novo, n_spawn = spawns(novo)
                if novo != texto:
                    with open(caminho, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
                        f.write(novo)
                    contagem["arquivos"] += 1
                    contagem["set"] += n_set
                    contagem["vivo"] += n_vivo
                    contagem["spawn"] += n_spawn
    return contagem


# Campos que o fork acrescentou a structs de atores do host -> tipo do bloco lateral (fork/nei_ext.h).
EXT_FIELDS = {"ivanFloating": "Player", "homingTarget": "EnMThunder", "isGerudoCone": "EnMThunder",
              "coneArmed": "EnMThunder", "coneWait": "EnMThunder", "coneYaw": "EnMThunder", "netForced": "EnButte"}
EXT_ACCESS = re.compile(r"(?<![\w.>])(\(\s*\(\s*\w+\s*\*\s*\)\s*\w+\s*\)|[A-Za-z_]\w*(?:(?:->|\.)\w+|\[[^\]\n]*\])*?)"
                        r"\s*->\s*(" + "|".join(EXT_FIELDS) + r")\b")


def campos_laterais(out):
    """`x->ivanFloating` nas funções do host copiadas vira `NEI_EXT(Player, x)->ivanFloating` (fork/nei_ext.h):
    o campo que o fork acrescentou à struct não existe no host."""
    total = 0
    for pasta, _, arquivos in os.walk(os.path.join(out, "extracted")):
        for nome in arquivos:
            if not nome.endswith(".c"):
                continue
            caminho = os.path.join(pasta, nome)
            with open(caminho, encoding="utf-8", newline="") as f:
                texto = f.read()
            novo, n = EXT_ACCESS.subn(lambda m: "NEI_EXT(%s, %s)->%s" % (EXT_FIELDS[m.group(2)], m.group(1), m.group(2)),
                                      texto)
            if n:
                with open(caminho, "w", encoding="utf-8", newline="") as f:
                    f.write('#include "nei_ext.h"\n' + novo)
                total += n
    return total


def extract(repo, out, symbols):
    """Funções que o fork acrescentou em arquivos do host (extract.txt) e funções do host que ele mudou (overlay.py,
    fontes de soh/hookable_sources.txt) -> <out>/extracted. Devolve (arquivos, sobrepostas, relatórios)."""
    sys.path.insert(0, HERE)
    import overlay
    entradas = {}  # caminho -> [unit, fork_only, sementes, includes extras]
    for line in open(os.path.join(HERE, "extract.txt"), encoding="utf-8"):
        line = line.split("#")[0].strip()
        if not line:
            continue
        path, rest = line.split(":", 1)
        seeds, _, extra_includes = rest.partition("|")
        unit, fork_only = path.startswith("@"), path.startswith("+")
        entradas[path.lstrip("@+").strip()] = [unit, fork_only, seeds.split(), extra_includes.split()]
    excluir, sem_desvio = overlay.ler_excluir()
    mesclados, relatorios = {}, []
    for path in overlay.fontes_sobreponiveis(repo):
        destino, sementes, rel = overlay.planejar(repo, FORK_COMMIT, FORK_BASE, path,
                                                  os.path.join(out, "overlay-src"), excluir)
        relatorios.append(rel)
        if not sementes:
            continue
        mesclados[path] = destino
        entrada = entradas.setdefault(path, [False, False, [], []])
        entrada[2] = sorted(set(entrada[2]) | set(sementes))
    count = 0
    for path, (unit, fork_only, seeds, extra_includes) in entradas.items():
        name = os.path.splitext(os.path.basename(path))[0]
        target = os.path.join(out, "extracted", "unit" if unit else "", name + ".c")
        args = [sys.executable, os.path.join(HERE, "extract_additions.py"), "--repo", repo, "--fork", FORK_COMMIT,
                "--file", path, "--out", target, "--report", os.path.join(out, "extracted", name + ".txt")]
        # Semente com > na frente é sobreposição: o host também tem a função, mas quem vale é a
        # versão do fork, porque a DLL desvia a do host para ela (fork/overlay_glue.cpp, kaleido_glue.cpp).
        for seed in seeds:
            args += ["--seed", seed.lstrip(">")]
            if seed.startswith(">"):
                args += ["--sobrepoe", seed[1:]]
        args += ["--symbols", symbols]
        if path in mesclados:
            args += ["--fork-file", mesclados[path], "--tabela"]
            args += [x for nome in sorted(sem_desvio) for x in ("--sem-desvio", nome)]
        # Header com - na frente é o contrário: um #include do fork que a saída não deve copiar.
        for header in extra_includes:
            args += (["--exclui", header[1:]] if header.startswith("-") else ["--inclui", header])
        if unit:
            args.append("--sem-includes")
        if fork_only:
            args += ["--sem-host", "--de-cpp", "--sem-includes"]
        subprocess.run(args, check=True)
        count += 1
    # Um só ponto de entrada para as tabelas de desvio de cada arquivo (fork/overlay_glue.cpp).
    stems = sorted(os.path.splitext(os.path.basename(p))[0] for p in mesclados)
    linhas = ["/* Gerado por sync.py: tabelas de desvio das funções do host que o fork mudou (overlay.py). */",
              '#include "overlay_table.h"', ""]
    for stem in stems:
        linhas += ["extern const NeiOverlayEntry nei_overlay_%s[];" % stem,
                   "extern const unsigned nei_overlay_%s_count;" % stem]
    linhas += ["", "typedef struct { const NeiOverlayEntry* entries; const unsigned* count; } NeiOverlayTable;",
               "static const NeiOverlayTable kTables[] = {"]
    linhas += ["    { nei_overlay_%s, &nei_overlay_%s_count }," % (s, s) for s in stems] or ["    { 0, 0 },"]
    linhas += ["};", "",
               "/* Entrada `index` de todas as tabelas, em sequência; 0 quando acabou. */",
               "const NeiOverlayEntry* NeiOverlay_Entry(unsigned index) {",
               "    for (unsigned t = 0; t < sizeof(kTables) / sizeof(kTables[0]); ++t) {",
               "        if (kTables[t].count == 0) {", "            continue;", "        }",
               "        if (index < *kTables[t].count) {", "            return &kTables[t].entries[index];", "        }",
               "        index -= *kTables[t].count;", "    }", "    return 0;", "}", ""]
    with open(os.path.join(out, "nei_overlays.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(linhas))
    sobrepostas = sum(len(r["sobrepostas"]) for r in relatorios)
    with open(os.path.join(out, "overlay-report.txt"), "w", encoding="utf-8", newline="\n") as f:
        for r in relatorios:
            f.write("%s\n" % r["arquivo"])
            for campo in ("sobrepostas", "dados", "globais_do_host", "leitoras", "mescladas", "resolvidas", "adaptadas",
                          "conflitos", "excluidas", "ausentes_no_host"):
                if r.get(campo):
                    f.write("  %s: %s\n" % (campo, " ".join(r[campo])))
    return count, sobrepostas, relatorios


def main():
    repo, out, symbols = sys.argv[1], os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3])
    stamp = os.path.join(out, "sync.stamp")
    inputs = sorted(os.path.join(HERE, "patches", p) for p in os.listdir(os.path.join(HERE, "patches")))
    inputs += [os.path.join(HERE, f) for f in ("gen_compat.py", "extract_additions.py", "extract.txt", "gen_stubs.py",
                                               "stub-names.txt", "extracted-fixes.txt", "gen_item_models.py",
                                               "cmodels.py", "overlay.py", "overlay-exclude.txt")]
    for pasta in ("overlay-merges", "overlay-extra"):
        inputs += sorted(os.path.join(HERE, pasta, p) for p in os.listdir(os.path.join(HERE, pasta)))
    inputs += [os.path.abspath(__file__), symbols, os.path.join(repo, "soh", "hookable_sources.txt")]
    signature = FORK_COMMIT + "\n" + "\n".join(f"{p} {os.path.getmtime(p)}" for p in inputs)
    if os.path.exists(stamp) and open(stamp, encoding="utf-8").read() == signature:
        print("nei fork: em dia")
        return
    for folder in ("fork", "compat", "extracted", "overlay-src"):
        shutil.rmtree(os.path.join(out, folder), ignore_errors=True)

    # Fonte do fork (soh/... -> fork/soh/...).
    files = git(repo, "ls-tree", "-r", "--name-only", FORK_COMMIT, "--", *TREES).split()
    for path in files:
        write(os.path.join(out, "fork", path), git(repo, "show", f"{FORK_COMMIT}:{path}", binary=True))
    # Headers que o fork acrescentou ao host (soh/soh/X.h -> compat/soh/X.h, junto das sombras).
    added = git(repo, "diff", "--name-only", "--diff-filter=A", FORK_BASE, FORK_COMMIT, "--",
                "soh/soh/*.h", "soh/soh/*.hpp").split()
    added = [p for p in added if not p.startswith("soh/soh/NEI/")]
    for path in added:
        write(os.path.join(out, "compat", path[len("soh/"):]),
              git(repo, "show", f"{FORK_COMMIT}:{path}", binary=True))

    # A saída costuma ficar dentro do worktree do host (build/), e git apply dentro de um repositório ignora em
    # silêncio caminhos fora do diretório atual. O teto na pasta-mãe faz o git tratar a saída como fora de repo.
    env = dict(os.environ, GIT_CEILING_DIRECTORIES=os.path.dirname(out))
    for patch in sorted(os.listdir(os.path.join(HERE, "patches"))):
        subprocess.run(["git", "apply", "--unsafe-paths", "--directory=fork", "-p1", "--whitespace=nowarn",
                        os.path.join(HERE, "patches", patch)], cwd=out, env=env, check=True)

    subprocess.run([sys.executable, os.path.join(HERE, "gen_compat.py"), repo, FORK_COMMIT, FORK_BASE,
                    os.path.join(out, "compat", "nei_compat.h"), os.path.join(out, "compat-report.txt")], check=True)
    macros, dados = host_data_macros(repo, symbols)
    with open(os.path.join(out, "compat", "nei_host_data.h"), "w", encoding="utf-8", newline="\n") as f:
        f.write(macros)
    extracted, sobrepostas, _ = extract(repo, out, symbols)
    substituicoes = aplicar_substituicoes(out)
    laterais = campos_laterais(out)
    subprocess.run([sys.executable, os.path.join(HERE, "gen_item_models.py"), repo, FORK_COMMIT,
                    os.path.join(out, "nei_item_models.c"), os.path.join(out, "nei_item_models.txt")], check=True)
    modelos_c = converter_modelos_c(repo, out)
    atores = proteger_atores(out)
    redeclaracoes = sum(alinhar_redeclaracoes_de_array(os.path.join(out, pasta), dados)
                        for pasta in ("fork", "extracted"))
    # Nome que a extração passou a definir de verdade (função nova do fork puxada pelo overlay.py) deixa de ser stub.
    definidos = set()
    for pasta, _, arquivos in os.walk(os.path.join(out, "extracted")):
        for nome in arquivos:
            if nome.endswith(".c"):
                with open(os.path.join(pasta, nome), encoding="utf-8", errors="replace") as f:
                    definidos.update(re.findall(r"^[A-Za-z_][^;{}()]*?\b(\w+)\s*\([^;{}]*\)\s*\{", f.read(), re.M))
    nomes_stub = os.path.join(out, "stub-names.efetivo.txt")
    with open(os.path.join(HERE, "stub-names.txt"), encoding="utf-8") as f, \
            open(nomes_stub, "w", encoding="utf-8", newline="\n") as g:
        g.writelines(l for l in f if l.strip() not in definidos)
    subprocess.run([sys.executable, os.path.join(HERE, "gen_stubs.py"), "--names", nomes_stub,
                    "--search", os.path.join(out, "fork", "soh"), "--search", os.path.join(out, "compat"),
                    "--out", os.path.join(out, "nei_stubs.c"), "--report", os.path.join(out, "nei_stubs.txt")],
                   check=True, stdout=subprocess.DEVNULL)
    with open(stamp, "w", encoding="utf-8") as f:
        f.write(signature)
    print(f"nei fork: {len(files)} arquivos do fork, {len(added)} headers novos, patches aplicados, "
          f"{len(dados)} variáveis do host por ponteiro, {redeclaracoes} redeclarações de array alinhadas, "
          f"{extracted} arquivos do host extraídos ({sobrepostas} funções do host sobrepostas), "
          f"{substituicoes} substituições, {laterais} campos laterais, "
          f"modelos em C: {modelos_c[0]} arrays ({modelos_c[1]} convertidos, {modelos_c[2]} headers), "
          f"atores: {atores['set']} trocas, {atores['vivo']} testes de vida e {atores['spawn']} spawns em "
          f"{atores['arquivos']} arquivos, stubs gerados")


main()
