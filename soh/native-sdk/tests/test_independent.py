"""DLL independente com layouts reais; não substitui o playtest do jogo."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile
import zipfile


def run(*command):
    result = subprocess.run([str(arg) for arg in command], capture_output=True,
                            text=True, encoding="utf-8", errors="replace", timeout=180)
    if result.returncode:
        raise RuntimeError(f"{command}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--example", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cmake", required=True)
    # O mesmo gerador do build do host: o runner do CI usa Ninja com o ambiente do MSVC e não tem o VS 2022.
    parser.add_argument("--generator", default="Visual Studio 17 2022")
    parser.add_argument("--make-program", default="")
    args = parser.parse_args()
    if args.generator.startswith("Visual Studio"):
        generator = ["-G", args.generator, "-A", "x64"]
    else:
        generator = ["-G", args.generator, "-DCMAKE_BUILD_TYPE=Release"]
        if args.make_program:
            generator.append(f"-DCMAKE_MAKE_PROGRAM={args.make_program}")
    before = hashlib.sha256(args.host.read_bytes()).hexdigest()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="oot-independent-") as temporary:
        build = Path(temporary) / "build"
        for velocity in (7, 10):
            run(args.cmake, "-S", args.example, "-B", build, *generator,
                f"-DOOT_NATIVE_SDK={args.sdk}", f"-DJUMP_VELOCITY={velocity}.0")
            run(args.cmake, "--build", build, "--config", "Release")
            print(run(args.probe, build / "mod", velocity).strip())
            package = args.output / f"dynamic-movement-remake-{velocity}.zip"
            with zipfile.ZipFile(package, "w", zipfile.ZIP_DEFLATED) as archive:
                for relative in ("manifest.toml", "main.lua", "provider/dynamic_movement_remake.dll"):
                    archive.write(build / "mod" / relative, relative)
            if hashlib.sha256(args.host.read_bytes()).hexdigest() != before:
                raise AssertionError("O executável do host mudou ao compilar o mod")
    print(f"Dois providers modificaram Player; host SHA-256 inalterado: {before}")


if __name__ == "__main__":
    main()
