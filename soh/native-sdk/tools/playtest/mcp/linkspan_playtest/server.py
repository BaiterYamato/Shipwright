"""Ferramentas MCP em stdio; stdout fica reservado ao protocolo MCP."""
import atexit
import re
import threading
import time

from mcp.server.fastmcp import FastMCP, Image
from . import launcher
from .capture import capture_window
from .conditions import wait_for as wait_for_state

mcp = FastMCP("linkspan-playtest")
_session = None
_lock = threading.RLock()


def _active():
    if _session is None:
        raise RuntimeError("use game_launch ou game_attach primeiro")
    if _session.process.poll() is not None:
        raise RuntimeError("o jogo encerrou; use game_close para finalizar as evidências")
    return _session


@mcp.tool()
def game_launch(runtime: str, console: bool = False) -> dict:
    """Abre o soh.exe do runtime e espera o agente, sem precisar de foco."""
    global _session
    with _lock:
        if _session is not None:
            raise RuntimeError("já existe uma sessão; finalize com game_close")
        _session = launcher.launch(runtime, console)
        return _session.info()


@mcp.tool()
def game_attach() -> dict:
    """Retoma a sessão local indicada por LINKSPAN_PLAYTEST_SESSION ou a última aberta."""
    global _session
    with _lock:
        if _session is not None:
            raise RuntimeError("já existe uma sessão")
        _session = launcher.attach()
        return _session.info()


@mcp.tool()
def game_close() -> dict:
    """Envia WM_CLOSE, aguarda até 60 s e restaura as CVars alteradas."""
    global _session
    with _lock:
        if _session is None:
            raise RuntimeError("nenhuma sessão aberta")
        result = _session.close()
        _session = None
        return result


@mcp.tool()
def game_state() -> dict:
    """Retorna o retrato do último callback do jogo; valores não aplicáveis são null."""
    with _lock:
        session = _active()
        session.record("game_state", {})
        return session.client.state()


@mcp.tool()
def input(steps: list[dict], wait: bool = True) -> dict:
    """Enfileira botões/sticks por frame. wait aguarda o frame posterior ao último passo."""
    with _lock:
        session = _active()
        session.record("input", {"steps": steps, "wait": wait})
        queued = session.client.input(steps)
        if wait:
            current = session.client.state().get("frame", 0)
            timeout = max(30, (queued["end_frame"] - current) / 10 + 30)
            try:
                queued["state"] = wait_for_state(session.client, f'frame > {queued["end_frame"]}', timeout)
            except Exception:
                try:
                    session.client.cancel()
                except Exception:
                    pass
                raise
        return queued


@mcp.tool()
def wait_for(condition: str, timeout: float = 30) -> dict:
    """Espera comparações sobre o retrato, unidas por and, sem executar código."""
    with _lock:
        session = _active()
        session.record("wait_for", {"condition": condition, "timeout": timeout})
        return wait_for_state(session.client, condition, timeout)


@mcp.tool()
def screenshot(label: str = "capture") -> list:
    """Captura a janela em PNG; texto da resposta informa se o fallback usou foco."""
    with _lock:
        session = _active()
        if not re.fullmatch(r"[A-Za-z0-9_-]{1,80}", label):
            raise ValueError("label aceita 1..80 letras ASCII, números, _ ou -")
        session.record("screenshot", {"label": label})
        path = session.directory / f"{time.time_ns()}-{label}.png"
        result = capture_window(session.process.pid, path)
        session.record("screenshot_result", {key: value for key, value in result.items() if key != "png"})
        return [f'path: {path}; focus_used: {str(result["focus_used"]).lower()}', Image(data=result["png"], format="png")]


@mcp.tool()
def console(command: str) -> dict:
    """Executa um comando pelo Sail; exige game_launch(console=true)."""
    with _lock:
        session = _active()
        if session.sail is None:
            raise RuntimeError("Sail desativado nesta sessão")
        session.record("console", {"command": command})
        return session.sail.command(command)


@mcp.tool()
def log(lines: int = 100, pattern: str | None = None) -> dict:
    """Lê o fim do log, com filtro opcional de texto literal."""
    with _lock:
        if _session is None:
            raise RuntimeError("nenhuma sessão aberta")
        _session.record("log", {"lines": lines, "pattern": pattern})
        return {"lines": _session.read_log(lines, pattern)}


def _disconnect():
    if _session is not None:
        _session.client.close()
        if _session.sail is not None:
            _session.sail.close()


def main():
    atexit.register(_disconnect)
    mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
