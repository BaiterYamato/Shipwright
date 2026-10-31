"""Observação do VFS e recibos RC11; nenhum hash é inventado para arquivos ausentes."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tempfile

MAX_PATHS = 8
MAX_PATH = 1024
MAX_BYTES = 8 * 1024 * 1024
MAX_LAYERS = 32
PHASES = ("after-ready", "after-unload")
OBSERVER = "linkspan.tool.playtest/vfs_layers"


def validate_paths(paths, max_bytes=MAX_BYTES):
    if not isinstance(paths, list) or not 1 <= len(paths) <= MAX_PATHS:
        raise ValueError("paths precisa conter 1..8 caminhos")
    if type(max_bytes) is not int or not 1 <= max_bytes <= MAX_BYTES:
        raise ValueError("max_bytes precisa estar em 1..8388608")
    seen = set()
    for path in paths:
        if not isinstance(path, str) or not path or "\0" in path or len(path.encode("utf-8")) > MAX_PATH:
            raise ValueError("caminho precisa ter 1..1024 bytes UTF-8, sem NUL")
        if path in seen:
            raise ValueError("caminhos duplicados")
        seen.add(path)


def _sha(value):
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def validate_result(result, paths, max_bytes=MAX_BYTES):
    """Rejeita listas truncadas, hashes ausentes e respostas de outra revisão/protocolo."""
    if not isinstance(result, dict) or result.get("status") != "observed":
        raise ValueError("resultado VFS inválido")
    instrument = result.get("instrumentation", {})
    if (not isinstance(instrument, dict) or instrument.get("observer") != OBSERVER or not _sha(instrument.get("revision")) or
            instrument.get("layer_order") != "low-to-high" or instrument.get("resources_version") != 2 or
            not isinstance(instrument.get("layout_id"), str) or not instrument["layout_id"] or
            instrument.get("agent_version") != "0.2.1"):
        raise ValueError("identidade/revisão do observador VFS inválida")
    if type(result.get("frame")) is not int or result["frame"] < 0:
        raise ValueError("frame VFS inválido")
    if type(result.get("bytes_hashed")) is not int or not 0 <= result["bytes_hashed"] <= max_bytes:
        raise ValueError("limite de bytes VFS violado")
    observations = result.get("observations")
    if not isinstance(observations, list) or len(observations) != len(paths):
        raise ValueError("observações VFS incompletas")
    count = 0
    for path, observation in zip(paths, observations):
        if not isinstance(observation, dict) or observation.get("path") != path:
            raise ValueError("caminhos VFS divergentes")
        status, complete = observation.get("hostStatus"), observation.get("complete")
        if (type(status) is not int or not 0 <= status <= 4 or type(complete) is not bool or
                complete != (status == 0) or type(observation.get("hasFile")) is not bool):
            raise ValueError("status VFS inválido")
        layers = observation.get("layers")
        if not isinstance(layers, list) or (complete and not layers) or (not complete and layers):
            raise ValueError("camadas VFS parciais ou ausentes")
        count += len(layers)
        for layer in layers:
            if (not isinstance(layer, dict) or not isinstance(layer.get("archive"), str) or not layer["archive"] or
                    "\0" in layer["archive"] or len(layer["archive"].encode("utf-8")) > 1024 or
                    not _sha(layer.get("sha256"))):
                raise ValueError("archive/SHA-256 de camada inválido")
    if count > MAX_LAYERS or type(result.get("complete")) is not bool or result["complete"] != all(
            item["complete"] for item in observations):
        raise ValueError("completude/limite de camadas VFS inválido")
    return result


def query_vfs(client, paths, max_bytes=MAX_BYTES):
    validate_paths(paths, max_bytes)
    return validate_result(client.execute("vfs_layers", paths=paths, max_bytes=max_bytes), paths, max_bytes)


def _atomic_json(path, value):
    # Temporário exclusivo no mesmo volume, substituição atômica.
    fd, temporary = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2, allow_nan=False)
            stream.write("\n")
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def _case(case):
    if not isinstance(case, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,80}", case):
        raise ValueError("case aceita 1..80 letras ASCII, números, _ ou -")
    return case


def save_vfs_snapshot(session, case, phase, paths, max_bytes=MAX_BYTES):
    """Grava observações agora; game_close sela o log e o contrato final rc11-vfs-1.

    phase é declaração do chamador: esta ferramenta não dispara ready nem unload.
    postLogSha256 fica null no rascunho; ele nunca é retornado como evidência final.
    """
    _case(case)
    if phase not in PHASES:
        raise ValueError("phase precisa ser after-ready ou after-unload")
    validate_paths(paths, max_bytes)
    digest = session.metadata.get("sha256")
    if not _sha(digest):
        raise ValueError("SHA-256 do exe não está disponível na sessão")
    pending = session.directory / f"{case}-rc11-vfs-1.pending.json"
    if (session.directory / f"{case}-rc11-vfs-1.json").exists():
        raise ValueError("caso já finalizado")
    previous = json.loads(pending.read_text(encoding="utf-8")) if pending.exists() else None
    if previous:
        existing = {(o["phase"], o["path"]) for o in previous["observations"]}
        if any((phase, path) in existing for path in paths):
            raise ValueError("fase/caminho já observado; use outro case para repetir")
    result = query_vfs(session.client, paths, max_bytes)
    instrument = dict(result["instrumentation"])
    instrument["mcp_revision"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    instrument["phase_source"] = "caller"
    document = previous or {"schema": "rc11-vfs-1", "case": case, "instrumentation": instrument,
                            "exeSha256": digest, "postLogSha256": None, "observations": [],
                            "complete": True}
    if document["instrumentation"] != instrument or document["exeSha256"] != digest:
        raise ValueError("observador/exe mudou entre fases")
    for observation in result["observations"]:
        document["observations"].append({**observation, "phase": phase, "frame": result["frame"]})
    document["complete"] = document["complete"] and result["complete"]
    _atomic_json(pending, document)
    return {"status": "pending_post_log", "path": str(pending), "complete": document["complete"],
            "observations": len(document["observations"])}


def finalize_vfs_snapshots(session):
    """Após close (ou offline com sessão encerrada), vincula ao game-final.log real.

    Rascunhos ficam preservados e permitem repetir a finalização em caso de falha de I/O.
    """
    pending = sorted(session.directory.glob("*-rc11-vfs-1.pending.json"))
    if not pending:
        return []
    if session.process.poll() is None or not session.metadata.get("closed"):
        raise RuntimeError("finalização VFS exige jogo e sessão encerrados")
    log = session.directory / "game-final.log"
    with log.open("rb") as stream:
        log_sha = hashlib.file_digest(stream, "sha256").hexdigest()
    finalized = []
    for draft in pending:
        document = json.loads(draft.read_text(encoding="utf-8"))
        case = _case(document["case"])
        final = session.directory / f"{case}-rc11-vfs-1.json"
        frozen_log = session.directory / f"{case}-rc11-vfs-1.post.log"
        if not frozen_log.exists():
            # Arquivo temporário exclusivo; jamais publica uma cópia parcial.
            fd, temporary = tempfile.mkstemp(dir=session.directory, suffix=".log.tmp")
            try:
                with os.fdopen(fd, "wb") as output, log.open("rb") as source:
                    shutil.copyfileobj(source, output)
                os.replace(temporary, frozen_log)
            finally:
                if os.path.exists(temporary):
                    os.unlink(temporary)
        with frozen_log.open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != log_sha:
                raise ValueError("cópia do log final diverge")
        document["postLogSha256"] = log_sha
        document["postLogFile"] = frozen_log.name
        document["exitCode"] = session.process.poll()
        if final.exists() and json.loads(final.read_text(encoding="utf-8")) != document:
            raise ValueError("snapshot final já existe com conteúdo diferente")
        _atomic_json(final, document)
        finalized.append(str(final))
    return finalized
