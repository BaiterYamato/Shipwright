"""Compila as cascas de mod (conteúdo fora do exe, sem prova em jogo) só com o SDK do host e confere o pacote."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def run(*command):
    result = subprocess.run([str(arg) for arg in command], capture_output=True, text=True, encoding="utf-8",
                            errors="replace", timeout=600)
    if result.returncode:
        raise RuntimeError(f"{command}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--host", type=Path, required=True, help="soh.exe do mesmo build")
    parser.add_argument("--shell", type=Path, action="append", required=True, help="pasta da casca; repita")
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--generator", default="Visual Studio 17 2022")
    parser.add_argument("--make-program", default="")
    args = parser.parse_args()
    if args.generator.startswith("Visual Studio"):
        generator = ["-G", args.generator, "-A", "x64"]
    else:
        generator = ["-G", args.generator, "-DCMAKE_BUILD_TYPE=Release"]
        if args.make_program:
            generator.append(f"-DCMAKE_MAKE_PROGRAM={args.make_program}")
    layout = re.search(r"set\(OOT_NATIVE_LAYOUT_ID \[==\[([0-9a-f]{64})\]==\]\)", args.sdk.read_text(encoding="utf-8"))
    if not layout:
        raise AssertionError("SDK sem OOT_NATIVE_LAYOUT_ID")
    for shell in args.shell:
        with tempfile.TemporaryDirectory(prefix=f"oot-shell-{shell.name}-") as temporary:
            build = Path(temporary) / "build"
            run(args.cmake, "-S", shell, "-B", build, *generator, f"-DOOT_NATIVE_SDK={args.sdk}",
                f"-DHOST_EXECUTABLE={args.host}")
            run(args.cmake, "--build", build, "--config", "Release")
            manifest = (build / "mod" / "manifest.toml").read_text(encoding="utf-8")
            if f'host_layout_id = "{layout.group(1)}"' not in manifest:
                raise AssertionError(f"{shell.name}: manifesto sem o layout do SDK")
            dlls = sorted((build / "mod" / "provider").glob("*.dll"))
            if not dlls:
                raise AssertionError(f"{shell.name}: pacote sem DLL em provider/")
            print(f"{shell.name}: {', '.join(dll.name for dll in dlls)} com layout {layout.group(1)[:8]}")


if __name__ == "__main__":
    main()
