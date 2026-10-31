#!/usr/bin/env python3
"""Copia para uma pasta só o código C/C++ que entra na DLL do NEI, para a auditoria de código do perfil oot.

Uso:
  python tools/nei-code-closure.py --nei-core soh/native-sdk/nei-core --build build/nei-core-native/nei-fork \
      --out build/nei-code

Raízes: as fontes do objeto nei_fork e da linkspan_nei_core no CMakeLists.txt, o extracted/ e as resoluções do
overlay. A partir delas segue os #include que resolvem no fork, no glue ou no build; header do SDK do host fica de
fora. Na saída, o fork vai em soh/ (o mesmo caminho do commit, que o build-nei-assets.py classifica por componente),
o glue em nei-core/ e o gerado em build/. Lexical: não pré-processa, então ramo #if e função não chamada entram.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import sys

INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.M)


def roots(nei_core, build):
    cmake = (nei_core / "CMakeLists.txt").read_text(encoding="utf-8")
    block = re.search(r"add_library\(nei_fork OBJECT(.*?)\)\s*target_include", cmake, re.S)
    final = re.search(r"add_library\(linkspan_nei_core SHARED(.*?)\)\s*#", cmake, re.S)
    if not block or not final:
        raise ValueError("CMakeLists.txt sem add_library(nei_fork OBJECT) ou add_library(linkspan_nei_core SHARED)")
    out = []
    for token in re.findall(r'"([^"\n]+\.(?:c|cpp))"', block[1]):
        token = token.replace("${fork_src}", str(nei_core / "fork")).replace("${nei_fork_dir}", str(build))
        out.append(Path(token))
    out += [nei_core / name for name in re.findall(r"(?<![\w/$])([\w/]+\.cpp)\b", final[1])]
    out += sorted((build / "extracted").glob("*.c"))
    out += sorted((nei_core / "fork" / "overlay-merges").glob("*.c"))
    out += sorted((nei_core / "fork" / "overlay-extra").glob("*.c"))
    return out


def closure(nei_core, build):
    fork = build / "fork"
    dirs = [nei_core / "fork", build, build / "compat", build / "compat" / "src", fork / "soh", fork / "soh" / "mods",
            fork / "soh" / "include", fork / "soh" / "assets", fork / "soh" / "src"]
    files, pending, missing = {}, roots(nei_core, build), []
    while pending:
        path = pending.pop().resolve()
        if path in files:
            continue
        if not path.is_file():
            raise FileNotFoundError("fonte raiz ausente: %s" % path)
        text = path.read_text(encoding="utf-8-sig", errors="replace")
        files[path] = text
        for name in INCLUDE.findall(text):
            found = next((c.resolve() for c in [path.parent / name] + [d / name for d in dirs] if c.is_file()), None)
            if found and inside(found, nei_core, build):
                pending.append(found)
            elif found:
                continue  # header do próprio host (soh/soh/...): não é código do NEI
            elif name.endswith((".c", ".cpp", ".inc")):
                missing.append("%s: %s" % (path, name))
    if missing:
        raise FileNotFoundError("include de implementação não resolvido:\n  " + "\n  ".join(sorted(missing)))
    return files


def inside(path, nei_core, build):
    return any(path.is_relative_to(base) for base in (nei_core, build))


def staged(path, nei_core, build):
    for base, prefix in ((build / "fork", ""), (nei_core, "nei-core/"), (build, "build/")):
        try:
            return prefix + path.relative_to(base.resolve()).as_posix()
        except ValueError:
            pass
    raise ValueError("fora do fork, do glue e do build: %s" % path)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--nei-core", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True, help="pasta nei-fork do build (saída do sync.py)")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)
    nei_core, build = args.nei_core.resolve(), args.build.resolve()
    try:
        files = closure(nei_core, build)
        names = {path: staged(path, nei_core, build) for path in files}
    except (OSError, ValueError) as error:
        print("erro: %s" % error, file=sys.stderr)
        return 2
    shutil.rmtree(args.out, ignore_errors=True)
    for path, name in sorted(names.items(), key=lambda item: item[1]):
        target = args.out / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    (args.out / "closure.txt").write_text("".join(n + "\n" for n in sorted(names.values())), encoding="utf-8")
    print("fecho do NEI: %d arquivos em %s" % (len(names), args.out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
