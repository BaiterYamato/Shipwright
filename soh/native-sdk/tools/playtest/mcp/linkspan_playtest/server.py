"""Ferramentas MCP em stdio; stdout fica reservado ao protocolo MCP."""
import atexit
import re
import threading
import time

from mcp.server.fastmcp import FastMCP, Image
from . import launcher
from .capture import capture_window
from .overview import collect_overview, render_overview, overview_summary
from .conditions import wait_for as wait_for_state
from .vfs import query_vfs, save_vfs_snapshot, finalize_vfs_snapshots, MAX_BYTES

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
        result["vfs_snapshots"] = finalize_vfs_snapshots(_session)
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
def vfs_layers(paths: list[str], max_bytes: int = MAX_BYTES) -> dict:
    """Camadas de 1..8 caminhos VFS: archive e SHA-256 dos bytes, da menor à maior prioridade.

    Até 8 MiB somados e 32 camadas por chamada. complete=false preserva falhas/ausência ambígua.
    Sem repetição automática; executa uma vez na thread do jogo, mesmo no título.
    """
    with _lock:
        session = _active()
        session.record("vfs_layers", {"paths": paths, "max_bytes": max_bytes})
        result = query_vfs(session.client, paths, max_bytes)
        session.record("vfs_layers_result", result)
        return result


@mcp.tool()
def game_vfs_snapshot(case: str, phase: str, paths: list[str], max_bytes: int = MAX_BYTES) -> dict:
    """Grava uma fase RC11. Espere ready/unload real antes de declarar a fase.

    Retorna pending_post_log; game_close sela o rc11-vfs-1 com SHA do log posterior ao unload.
    complete=false é diagnóstico e não prova um gate aprovado.
    """
    with _lock:
        session = _active()
        session.record("game_vfs_snapshot", {"case": case, "phase": phase, "paths": paths, "max_bytes": max_bytes})
        result = save_vfs_snapshot(session, case, phase, paths, max_bytes)
        session.record("game_vfs_snapshot_result", result)
        return result


def _v2(op, **arguments):
    with _lock:
        session = _active()
        session.record(op, arguments)
        result = session.client.execute(op, **arguments)
        session.record(op + "_result", result)
        return result


@mcp.tool()
def scene_map(offset: int = 0, limit: int = 128) -> dict:
    """Mapa vivo: spawns, entradas, exits, salas e planos EnHoll. null indica lista indisponível.

    Paginação simultânea de cada lista; avance offset em limit enquanto has_more.
    complete=false: o host não oferece linkspan.oot.scene_query v1 (spawns, entradas e exits).
    """
    return _v2("scene_map", offset=offset, limit=limit)


@mcp.tool()
def scene_overview(y_min: float | None = None, y_max: float | None = None,
                   bbox: list[float] | None = None, include_actors: bool = False,
                   include_ceiling: bool = False, stride: int = 1, limit: int = 128,
                   max_pages: int = 32, size: int = 960, offset: int = 0,
                   water_offset: int = 0, transition_offset: int = 0, actor_offset: int = 0,
                   room_offset: int = 0, scene_epoch: int | None = None) -> list:
    """Planta PNG da colisão estática + resumo. X à direita, +Z abaixo; yaw em graus.

    bbox=[x_min,z_min,x_max,z_max]; Y padrão Link-200..Link+300, fixado na primeira página.
    Até 64 páginas de 1..128 elementos por lista; partial e next_offsets permitem continuar.
    Ao continuar, forneça scene_epoch, os cursores e a mesma faixa Y/bbox/stride.
    Chão/água sobrepostos mostram a camada mais alta. DynaPoly e contornos de sala ausentes.
    Não calcula rota nem garante transitabilidade. Reduza a faixa Y em mapas com vários andares.
    """
    if type(size) is not int or not 384 <= size <= 1280:
        raise ValueError("size precisa estar em 384..1280 pixels")
    arguments = dict(include_actors=include_actors, include_ceiling=include_ceiling, stride=stride,
                     limit=limit, offset=offset, water_offset=water_offset,
                     transition_offset=transition_offset, actor_offset=actor_offset, room_offset=room_offset)
    for key, value in (("y_min", y_min), ("y_max", y_max), ("bbox", bbox), ("scene_epoch", scene_epoch)):
        if value is not None:
            arguments[key] = value
    with _lock:
        session = _active()
        scene = collect_overview(lambda **args: _v2("scene_overview", **args), arguments, max_pages)
        png = render_overview(scene, size)
        path = session.directory / f"{time.time_ns()}-scene-overview.png"
        path.write_bytes(png)
        summary = overview_summary(scene)
        session.record("scene_overview_render", {"path": str(path), "summary": summary})
        return [f"path: {path}; {summary}", Image(data=png, format="png")]


@mcp.tool()
def actors(radius: float | None = None, category: int | None = None, offset: int = 0, limit: int = 128) -> dict:
    """Atores vivos, ordenados por distância 3D do Link. Categoria 0..11; npc=4, prop=6, door=10.

    Nomes vêm da tabela pública do SDK; tipos dinâmicos podem ter name=null.
    """
    arguments = {"offset": offset, "limit": limit}
    if radius is not None:
        arguments["radius"] = radius
    if category is not None:
        arguments["category"] = category
    return _v2("actors", **arguments)


@mcp.tool()
def teleport(x: float, y: float, z: float, yaw: float | None = None, timeout: float = 3) -> dict:
    """Teleporta no chão, zera velocidade e confirma bgcheck. Yaw em graus (0=+Z, 90=+X).

    Recusa cutscene, caminhada automática, Player ocupado ou destino sem chão seguro.
    status=grounded confirma; refused/timeout/scene_changed nunca significam sucesso.
    """
    arguments = {"x": x, "y": y, "z": z, "timeout": timeout}
    if yaw is not None:
        arguments["yaw"] = yaw
    return _v2("teleport", **arguments)


@mcp.tool()
def camera_state() -> dict:
    """Eye, at, yaw/pitch em graus e input_yaw real usado pelo controle; modo e frame."""
    return _v2("camera_state")


@mcp.tool()
def camera_set(mode: str = "free", yaw: float | None = None, pitch: float | None = None,
               distance: float | None = None, eye: list[float] | None = None, at: list[float] | None = None) -> dict:
    """Câmera com posse exclusiva: yaw+pitch+distance ou eye+at ([x,y,z]); ângulos em graus.

    Yaw 0 olha +Z, 90 olha +X; pitch negativo olha para baixo. Sem at, orbita Link+40Y.
    mode=normal libera a câmera. Posse cai em troca de cena, cutscene ou desconexão.
    """
    arguments = {"mode": mode}
    for name, value in (("yaw", yaw), ("pitch", pitch), ("distance", distance), ("eye", eye), ("at", at)):
        if value is not None:
            arguments[name] = value
    return _v2("camera_set", **arguments)


@mcp.tool()
def walk_to(x: float, z: float, tolerance: float = 20, timeout: float = 15) -> dict:
    """Anda em linha reta compensando input_yaw a cada pad read; para em parede, queda ou falta de progresso.

    Não calcula rotas. Devolve status, posição, distância inicial/restante e progresso.
    Recusa Player sem controle e interrompe ao trocar cena/Player; aceita troca de sala.
    """
    return _v2("walk_to", x=x, z=z, tolerance=tolerance, timeout=timeout)


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
