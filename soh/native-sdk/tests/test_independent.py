"""DLL independente com layouts reais; não substitui o playtest do jogo."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import tomllib
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
    sdk = args.sdk.resolve()
    example = args.example.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="oot-independent-", dir=args.output.resolve()) as temporary:
        build = Path(temporary) / "build"
        # Projeto descartável: troca somente a entrada da DLL por um observador que
        # inclui provider.cpp. A DLL e os pacotes normais continuam sem instrumentação.
        observer_source = Path(__file__).resolve().with_name("dmr_public_binding.cpp")
        observer_project = Path(temporary) / "observer"
        observer_project.mkdir()
        (observer_project / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.26)\n'
            'project(DmrPublicBinding LANGUAGES CXX)\n'
            f'add_subdirectory("{example.as_posix()}" dmr)\n'
            'get_target_property(dmr_sources dynamic_movement_remake SOURCES)\n'
            'list(REMOVE_ITEM dmr_sources provider.cpp)\n'
            f'list(APPEND dmr_sources "{observer_source.as_posix()}")\n'
            'set_property(TARGET dynamic_movement_remake PROPERTY SOURCES "${dmr_sources}")\n',
            encoding="utf-8")
        observer_build = Path(temporary) / "observer-build"
        for velocity in (7, 10):
            run(args.cmake, "-S", example, "-B", build, *generator,
                f"-DOOT_NATIVE_SDK={sdk}", f"-DJUMP_VELOCITY={velocity}.0")
            run(args.cmake, "--build", build, "--config", "Release")
            manifest = tomllib.loads((build / "mod" / "manifest.toml").read_text(encoding="utf-8"))
            provider = manifest["provider"]
            if (provider["abi_version"] != "1.4" or "host_fingerprints" in provider
                    or len(provider.get("host_layout_id", "")) != 64):
                raise AssertionError("DMR precisa de ABI 1.4 e layout, sem fingerprint")
            if list((build / "mod").rglob("soh.symbols")):
                raise AssertionError("Fixture independente não pode fornecer soh.symbols")
            print(run(args.probe, build / "mod", velocity).strip())
            # Os três testes existentes usam o provider e os headers reais do SDK.
            ctest = Path(shutil.which(args.cmake) or args.cmake).resolve().with_name("ctest.exe")
            run(ctest, "--test-dir", build, "-C", "Release", "--output-on-failure",
                "--no-tests=error", "-R", "^dynamic_movement_(icon|camera|menu)_tests$")
            run(args.cmake, "-S", observer_project, "-B", observer_build, *generator,
                f"-DOOT_NATIVE_SDK={sdk}", f"-DJUMP_VELOCITY={velocity}.0")
            run(args.cmake, "--build", observer_build, "--config", "Release",
                "--target", "dynamic_movement_remake")
            if list((observer_build / "mod").rglob("soh.symbols")):
                raise AssertionError("Fixture observada não pode fornecer soh.symbols")
            for scenario in ("public-ok", "no-layout", "bad-layout", "other-fingerprint"):
                print(run(args.probe, observer_build / "mod", velocity, scenario).strip())
            package = args.output / f"dynamic-movement-remake-{velocity}.zip"
            with zipfile.ZipFile(package, "w", zipfile.ZIP_DEFLATED) as archive:
                for relative in ("manifest.toml", "main.lua", "provider/dynamic_movement_remake.dll"):
                    archive.write(build / "mod" / relative, relative)
            if hashlib.sha256(args.host.read_bytes()).hexdigest() != before:
                raise AssertionError("O executável do host mudou ao compilar o mod")
    print(f"DMR real e observador: dois impulsos, catálogo/layout/fingerprint/câmera/ícones; host SHA-256 inalterado: {before}")


if __name__ == "__main__":
    main()
