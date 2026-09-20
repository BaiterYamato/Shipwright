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
        if len(parts) == 4 and parts[2] == "[data]" and parts[1] == "-":
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


def extract(repo, out, symbols):
    """Funções que o fork acrescentou em arquivos do host (extract.txt) -> <out>/extracted."""
    count = 0
    for line in open(os.path.join(HERE, "extract.txt"), encoding="utf-8"):
        line = line.split("#")[0].strip()
        if not line:
            continue
        path, rest = line.split(":", 1)
        seeds, _, extra_includes = rest.partition("|")
        unit, fork_only = path.startswith("@"), path.startswith("+")
        path = path.lstrip("@+").strip()
        name = os.path.splitext(os.path.basename(path))[0]
        target = os.path.join(out, "extracted", "unit" if unit else "", name + ".c")
        args = [sys.executable, os.path.join(HERE, "extract_additions.py"), "--repo", repo, "--fork", FORK_COMMIT,
                "--file", path, "--out", target, "--report", os.path.join(out, "extracted", name + ".txt")]
        # Semente com > na frente é sobreposição: o host também tem a função, mas quem vale é a
        # versão do fork, porque a DLL desvia a do host para ela (fork/kaleido_glue.cpp).
        for seed in seeds.split():
            args += ["--seed", seed.lstrip(">")]
            if seed.startswith(">"):
                args += ["--sobrepoe", seed[1:]]
        args += ["--symbols", symbols]
        # Header com - na frente é o contrário: um #include do fork que a saída não deve copiar.
        for header in extra_includes.split():
            args += (["--exclui", header[1:]] if header.startswith("-") else ["--inclui", header])
        if unit:
            args.append("--sem-includes")
        if fork_only:
            args += ["--sem-host", "--de-cpp", "--sem-includes"]
        subprocess.run(args, check=True)
        count += 1
    return count


def main():
    repo, out, symbols = sys.argv[1], os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3])
    stamp = os.path.join(out, "sync.stamp")
    inputs = sorted(os.path.join(HERE, "patches", p) for p in os.listdir(os.path.join(HERE, "patches")))
    inputs += [os.path.join(HERE, f) for f in ("gen_compat.py", "extract_additions.py", "extract.txt", "gen_stubs.py",
                                               "stub-names.txt")]
    inputs += [os.path.abspath(__file__), symbols]
    signature = FORK_COMMIT + "\n" + "\n".join(f"{p} {os.path.getmtime(p)}" for p in inputs)
    if os.path.exists(stamp) and open(stamp, encoding="utf-8").read() == signature:
        print("nei fork: em dia")
        return
    for folder in ("fork", "compat", "extracted"):
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
    extracted = extract(repo, out, symbols)
    redeclaracoes = sum(alinhar_redeclaracoes_de_array(os.path.join(out, pasta), dados)
                        for pasta in ("fork", "extracted"))
    subprocess.run([sys.executable, os.path.join(HERE, "gen_stubs.py"), "--names", os.path.join(HERE, "stub-names.txt"),
                    "--search", os.path.join(out, "fork", "soh"), "--search", os.path.join(out, "compat"),
                    "--out", os.path.join(out, "nei_stubs.c"), "--report", os.path.join(out, "nei_stubs.txt")],
                   check=True, stdout=subprocess.DEVNULL)
    with open(stamp, "w", encoding="utf-8") as f:
        f.write(signature)
    print(f"nei fork: {len(files)} arquivos do fork, {len(added)} headers novos, patches aplicados, "
          f"{len(dados)} variáveis do host por ponteiro, {redeclaracoes} redeclarações de array alinhadas, "
          f"{extracted} arquivos do host extraídos, stubs gerados")


main()
