"""Sessão de jogo, evidências e restauração pontual das CVars do Sail."""
import ctypes
from ctypes import wintypes as wt
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import socket
import subprocess
import time

from .agent_client import AgentClient, ProtocolError
from .capture import close_window
from .sail import Sail

DEFAULT_EVIDENCE = Path(r"D:\Desenvolvimento\ship-lua-worktrees\logs\playtest")
SAIL_VALUES = {"Enabled": 1, "Host": "127.0.0.1", "Port": 43384}


def atomic_json(path, value):
    path = Path(path)
    temporary = path.with_name(path.name + "." + secrets.token_hex(6) + ".tmp")
    try:
        temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _read_json(path):
    if not path.exists():
        return {}
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(data, dict):
        raise ValueError("configuração precisa ser um objeto JSON")
    return data


def sail_settings(path, original=None):
    data = _read_json(path)
    node = data
    ancestors = []
    for key in ("CVars", "gRemote", "Sail"):
        if key not in node:
            node[key] = {}
        if not isinstance(node[key], dict):
            raise ValueError("estrutura de CVars inválida")
        ancestors.append((node, key))
        node = node[key]
    if original is None:
        original = {key: {"present": key in node, "value": node.get(key)} for key in SAIL_VALUES}
        node.update(SAIL_VALUES)
    else:
        for key, old in original.items():
            if old["present"]:
                node[key] = old["value"]
            else:
                node.pop(key, None)
        for parent, key in reversed(ancestors):
            if not parent[key]:
                del parent[key]
    atomic_json(path, data)
    return original


class AttachedProcess:
    def __init__(self, pid):
        if os.name != "nt":
            raise OSError("attach exige Windows")
        self.pid = pid
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
        self.kernel.OpenProcess.restype = wt.HANDLE
        self.kernel.GetExitCodeProcess.argtypes = [wt.HANDLE, ctypes.POINTER(wt.DWORD)]
        self.kernel.GetExitCodeProcess.restype = wt.BOOL
        self.kernel.CloseHandle.argtypes = [wt.HANDLE]
        self.kernel.CloseHandle.restype = wt.BOOL
        self.handle = self.kernel.OpenProcess(0x1000, False, pid)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())

    def poll(self):
        code = wt.DWORD()
        if not self.kernel.GetExitCodeProcess(self.handle, ctypes.byref(code)):
            raise ctypes.WinError(ctypes.get_last_error())
        return None if code.value == 259 else code.value

    def dispose(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


class Session:
    def __init__(self, directory, metadata, process, client, sail=None):
        self.directory, self.metadata, self.process, self.client, self.sail = directory, metadata, process, client, sail

    def record(self, tool, arguments):
        with (self.directory / "commands.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps({"time": datetime.now().astimezone().isoformat(), "tool": tool,
                                     "arguments": arguments}, ensure_ascii=False) + "\n")

    def read_log(self, lines=100, pattern=None):
        if not isinstance(lines, int) or not 1 <= lines <= 10000:
            raise ValueError("lines precisa estar em 1..10000")
        paths = [Path(self.metadata["log"]), self.directory / "host-stdout.log"]
        result = []
        from collections import deque
        for path in paths:
            if path.exists():
                with path.open(encoding="utf-8", errors="replace") as stream:
                    if path == paths[0] and path.stat().st_size >= self.metadata.get("log_offset", 0):
                        stream.seek(self.metadata.get("log_offset", 0))
                    tail = deque((line.rstrip("\r\n") for line in stream if pattern is None or pattern in line), maxlen=lines)
                result.extend(tail)
        return result[-lines:]

    def info(self):
        return {"pid": self.process.pid, "sha256": self.metadata["sha256"], "log": self.metadata["log"],
                "session": str(self.directory), "sail": self.metadata["console"]}

    def close(self):
        self.record("game_close", {})
        if self.process.poll() is None:
            close_window(self.process.pid)
            deadline = time.monotonic() + 60
            while self.process.poll() is None:
                if time.monotonic() >= deadline:
                    raise TimeoutError("jogo não fechou em 60 s; sessão e configuração preservadas para recuperação")
                time.sleep(0.1)
        self.client.close()
        if self.sail is not None:
            self.sail.close()
        if self.metadata.get("sail_original") is not None:
            sail_settings(Path(self.metadata["runtime"]) / "shipofharkinian.json", self.metadata["sail_original"])
        log = Path(self.metadata["log"])
        errors = []
        for path in (log, self.directory / "host-stdout.log"):
            if path.exists():
                with path.open("rb") as stream:
                    if path == log and path.stat().st_size >= self.metadata.get("log_offset", 0):
                        stream.seek(self.metadata.get("log_offset", 0))
                    data = stream.read()
                if path == log:
                    (self.directory / "game-final.log").write_bytes(data)
                errors.extend(line for line in data.decode("utf-8", errors="replace").splitlines()
                              if re.search(r"\[(?:error|critical)\]", line, re.IGNORECASE))
        code = self.process.poll()
        self.metadata["closed"] = True
        # O token só é útil enquanto a sessão estiver aberta.
        self.metadata.pop("token", None)
        atomic_json(self.directory / "session.json", self.metadata)
        if isinstance(self.process, AttachedProcess):
            self.process.dispose()
        return {"exit_code": code, "errors": errors, "session": str(self.directory)}


def launch(runtime, console=False):
    if os.name != "nt":
        raise OSError("o launcher do OoT exige Windows")
    runtime = Path(runtime).resolve()
    exe = runtime / "soh.exe"
    if not exe.is_file():
        raise FileNotFoundError(f"soh.exe ausente em {runtime}")
    agent = Path(os.environ.get("LINKSPAN_PLAYTEST_AGENT", str(Path(__file__).resolve().parents[2] / "agent" / "build" / "mod"))).resolve()
    if not (agent / "manifest.toml").is_file() or not (agent / "provider" / "linkspan_playtest.dll").is_file():
        raise FileNotFoundError("agente não compilado; defina LINKSPAN_PLAYTEST_AGENT para o pacote gerado")
    port = int(os.environ.get("LINKSPAN_PLAYTEST_PORT", "47811"))
    if not 1 <= port <= 65535:
        raise ValueError("porta fora de 1..65535")
    with socket.socket() as probe:
        if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        probe.bind(("127.0.0.1", port))
    root = Path(os.environ.get("LINKSPAN_PLAYTEST_EVIDENCE", str(DEFAULT_EVIDENCE))).resolve()
    directory = root / (datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + secrets.token_hex(3))
    directory.mkdir(parents=True)
    with exe.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    log_path = runtime / "logs" / "Ship of Harkinian.log"
    metadata = {"runtime": str(runtime), "sha256": digest, "log": str(log_path),
                "log_offset": log_path.stat().st_size if log_path.exists() else 0,
                "port": port, "token": secrets.token_hex(16), "console": bool(console), "closed": False,
                "sail_original": None}
    sail, process, client = None, None, None
    config = runtime / "shipofharkinian.json"
    try:
        if console:
            sail = Sail()
            # Persiste o journal antes de abrir o processo, para permitir recuperação por attach.
            current = _read_json(config)
            node = current.get("CVars", {}).get("gRemote", {}).get("Sail", {})
            metadata["sail_original"] = {key: {"present": key in node, "value": node.get(key)} for key in SAIL_VALUES}
            atomic_json(directory / "session.json", metadata)
            sail_settings(config)
        environment = os.environ.copy()
        environment.update(LINKSPAN_PLAYTEST_TOKEN=metadata["token"], LINKSPAN_PLAYTEST_PORT=str(port),
                           LINKSPAN_PLAYTEST_SAIL="1" if console else "0")
        with (directory / "host-stdout.log").open("wb") as output:
            process = subprocess.Popen([str(exe), "--linkspan-tool", str(agent)], cwd=runtime, env=environment,
                                       stdout=output, stderr=subprocess.STDOUT)
        metadata["pid"] = process.pid
        atomic_json(directory / "session.json", metadata)
        client = AgentClient(port, metadata["token"])
        deadline = time.monotonic() + 90
        while True:
            if process.poll() is not None:
                raise RuntimeError("jogo encerrou antes do hello")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("agente não respondeu hello em 90 s")
            client.timeout = min(5.0, remaining)
            try:
                client.hello()
                client.timeout = 5.0
                break
            except ProtocolError:
                raise
            except (OSError, RuntimeError, ValueError):
                if time.monotonic() >= deadline:
                    raise TimeoutError("agente não respondeu hello em 90 s") from None
                time.sleep(0.1)
        session = Session(directory, metadata, process, client, sail)
        session.record("game_launch", {"runtime": str(runtime), "console": console})
        return session
    except Exception as error:
        if client is not None:
            client.close()
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=10)
        if sail is not None:
            sail.close()
        if metadata["sail_original"] is not None:
            sail_settings(config, metadata["sail_original"])
        metadata.pop("token", None)
        metadata["closed"] = True
        atomic_json(directory / "session.json", metadata)
        if process is not None:
            failed = Session(directory, metadata, process, None)
            raise RuntimeError(f"{error}; evidências: {directory}; log: {failed.read_log(30)}") from None
        raise


def attach():
    root = Path(os.environ.get("LINKSPAN_PLAYTEST_EVIDENCE", str(DEFAULT_EVIDENCE))).resolve()
    chosen = os.environ.get("LINKSPAN_PLAYTEST_SESSION")
    if chosen:
        directory = Path(chosen).resolve()
    else:
        candidates = sorted(root.glob("*/session.json"), reverse=True)
        directory = next((path.parent for path in candidates if not _read_json(path).get("closed")), None)
        if directory is None:
            raise FileNotFoundError("nenhuma sessão aberta; defina LINKSPAN_PLAYTEST_SESSION")
    metadata = _read_json(directory / "session.json")
    if metadata.get("closed") or not metadata.get("token"):
        raise RuntimeError("sessão encerrada ou sem token")
    process = AttachedProcess(metadata["pid"])
    client, sail = AgentClient(metadata["port"], metadata["token"]), None
    try:
        if process.poll() is not None:
            raise RuntimeError("processo da sessão já encerrou")
        # Não toma uma sessão de outro controlador silenciosamente.
        client.hello()
        if metadata["console"]:
            sail = Sail()
        session = Session(directory, metadata, process, client, sail)
        session.record("game_attach", {})
        return session
    except Exception:
        client.close()
        process.dispose()
        if sail is not None:
            sail.close()
        raise
