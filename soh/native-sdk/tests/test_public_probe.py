"""Compila o probe 1.4 só com o SDK do host, confere o layout no manifesto e carrega pela política real."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile


def run(*command):
    result = subprocess.run([str(arg) for arg in command], capture_output=True, text=True, encoding="utf-8",
                            errors="replace", timeout=300)
    if result.returncode:
        raise RuntimeError(f"{command}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True, help="oot_native_engine_tests")
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
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
    with tempfile.TemporaryDirectory(prefix="oot-public-probe-") as temporary:
        build = Path(temporary) / "build"
        run(args.cmake, "-S", args.source, "-B", build, *generator, f"-DOOT_NATIVE_SDK={args.sdk}")
        run(args.cmake, "--build", build, "--config", "Release")
        manifest = (build / "mod" / "manifest.toml").read_text(encoding="utf-8")
        if f'host_layout_id = "{layout.group(1)}"' not in manifest:
            raise AssertionError("manifesto do probe sem o layout do SDK")
        print(run(args.probe, build / "mod", "public").strip())
        args.output.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(args.output / "public-probe.zip", "w", zipfile.ZIP_DEFLATED) as archive:
            for relative in ("manifest.toml", "main.lua", "provider/public_probe.dll"):
                archive.write(build / "mod" / relative, relative)
    print(f"probe 1.4 com layout {layout.group(1)[:8]}: {args.output / 'public-probe.zip'}")


if __name__ == "__main__":
    main()
