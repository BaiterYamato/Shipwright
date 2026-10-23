"""Valida fixture e contrato dos logs. Nenhum resultado daqui prova host/jogo."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import tempfile
import tomllib
import zipfile

FILES = {
    "textures/res_probe/a": b"LINKSPAN-RES-A",
    "textures/res_probe/b": b"LINKSPAN-RES-B",
    "objects/gameplay_keep/gPlayerAnim_link_normal_wait": b"LINKSPAN-RES-C",
}
NAMES = ["vanilla", "mount", "names", "bytes", "list", "duplicate", "unmount_quiet", "remount"]


def fixture(path):
    with zipfile.ZipFile(path) as z:
        entries = z.infolist()
        assert len(entries) == 3 and {e.filename for e in entries} == FILES.keys(), "índice inesperado/duplicado"
        for e in entries:
            assert e.compress_type == zipfile.ZIP_STORED, "fixture exige ZIP_STORED"
            assert z.read(e) == FILES[e.filename], f"bytes diferentes: {e.filename}"
    return {"kind": "synthetic_fixture", "files": 3, "sha256": hashlib.sha256(Path(path).read_bytes()).hexdigest()}


def log_report(text, require_unload=False):
    start = text.rfind("res-probe report stage=complete")
    assert start >= 0, "sem relatório final completo"
    end = text.find("res-probe report stage=", start + 1)
    report = text[start:end if end >= 0 else len(text)]
    assert "res-probe ok 8/8" in report and "res-probe fail" not in report, "resumo final falhou"
    checks = re.findall(r"res-probe check=(\d+) name=(\w+) status=(\w+)", report)
    assert checks == [(str(i + 1), name, "ok") for i, name in enumerate(NAMES)], "checks ausentes/repetidos/incorretos"
    timing = re.search(r"quiet_frames=(\d+) transform_calls=(\d+) left_handle=(\d+)", report)
    assert timing and int(timing[1]) >= 20 and int(timing[2]) > 0 and int(timing[3]) > 0, "janela/transform/remontagem ausente"
    mounts = re.findall(r"Link-Span montou 3 arquivos [^\r\n]*res-probe\.o2r[^\r\n]*sob 'mod/res-probe/' \(1 aliases aceitos, 1 recusados\)", text)
    assert len(mounts) >= 2, "faltam montagem inicial/remontagem do host"
    # O handle final distingue cleanup por owner do primeiro unmount explícito.
    final_handle = int(timing[3])
    unload_pattern = rf"Link-Span desmontou archive {final_handle} de '[^'\r\n]*res-probe\.o2r#linkspan-mount-{final_handle}'"
    unloaded = re.search(unload_pattern, text[start:]) is not None
    if require_unload:
        assert unloaded, "sem unmount do handle final depois do relatório"
    assert not re.search(r"res-probe ERRO|res-probe fail", text), "probe registrou erro"
    return {
        "kind": "runtime_log_claims_only",
        "checks": 8,
        "final_handle": final_handle,
        "owner_unmount_logged": unloaded,
        "does_not_prove": ["ausencia_de_crash", "dreno_concorrente", "cache_LUS", "DLL_unloaded", "animacao_vanilla"],
    }


def selftest():
    good = "Link-Span montou 3 arquivos de 'mods/res-probe/res-probe.o2r' sob 'mod/res-probe/' (1 aliases aceitos, 1 recusados)\n"
    good *= 2
    good += "res-probe report stage=complete\n"
    good += "".join(f"res-probe check={i+1} name={name} status=ok\n" for i, name in enumerate(NAMES))
    good += "res-probe quiet_frames=20 transform_calls=1 left_handle=9\nres-probe ok 8/8\n"
    good += "Link-Span desmontou archive 9 de 'mods/res-probe/res-probe.o2r#linkspan-mount-9'\n"
    assert log_report(good, True)["owner_unmount_logged"]
    negatives = [
        good.replace("status=ok", "status=fail", 1),
        good.replace("quiet_frames=20", "quiet_frames=0"),
        good.replace("left_handle=9", "left_handle=0"),
        good.replace("archive 9", "archive 8"),
        good.replace("#linkspan-mount-9", "#linkspan-mount-8"),
        good.replace("#linkspan-mount-9", ""),
        good.replace("#linkspan-mount-9", "#linkspan-mount-9-extra"),
        good.replace("res-probe ok 8/8", "res-probe fail 7/8"),
        good.replace("res-probe check=4 name=bytes status=ok\n", ""),
        "res-probe ok 8/8\n",
    ]
    for text in negatives:
        try:
            log_report(text, True)
        except AssertionError:
            continue
        raise AssertionError("validador aceitou log negativo")
    with tempfile.TemporaryDirectory(prefix="res-probe-", dir=Path(__file__).parent) as temporary:
        zpath = Path(temporary) / "synthetic.o2r"
        with zipfile.ZipFile(zpath, "w", zipfile.ZIP_STORED) as z:
            for name, data in FILES.items():
                z.writestr(name, data)
        fixture(zpath)
        with zipfile.ZipFile(zpath, "w") as z:
            z.writestr("textures/res_probe/a", b"ERRADO")
        try:
            fixture(zpath)
        except AssertionError:
            pass
        else:
            raise AssertionError("fixture negativa aceita")
    root = Path(__file__).parent
    manifest = tomllib.loads((root / "manifest.toml").read_text(encoding="utf-8"))
    assert manifest["id"] == "linkspan.test.res_probe" and manifest["provider"]["abi_version"] == "1.4"
    assert manifest["provider"]["win64"] == "provider/res_probe.dll"
    return {"kind": "offline_validator_selftest", "passed": True, "negative_logs": len(negatives), "host_tested": False}


def main():
    if sys.flags.optimize:
        print('{"passed": false, "error": "nao executar validador com Python -O"}')
        return 1
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("selftest")
    sub.add_parser("fixture").add_argument("path", type=Path)
    log = sub.add_parser("log")
    log.add_argument("path", type=Path)
    log.add_argument("--require-unload", action="store_true")
    args = parser.parse_args()
    try:
        result = selftest() if args.command == "selftest" else fixture(args.path) if args.command == "fixture" else log_report(
            args.path.read_text(encoding="utf-8", errors="replace"), args.require_unload)
    except (AssertionError, OSError, ValueError, zipfile.BadZipFile) as error:
        print(json.dumps({"passed": False, "error": str(error)}, ensure_ascii=False))
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
