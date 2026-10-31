"""Cliente JSON por linha, com lease vinculado à conexão."""
import json
import socket
import threading
import time
import math

MAX_LINE = 256 * 1024
PROTOCOL_VERSION = 2


class AgentError(RuntimeError):
    pass


class ProtocolError(AgentError):
    """Incompatibilidade permanente: reconectar não atualiza a DLL."""


class AgentClient:
    def __init__(self, port, token, client="linkspan-playtest-mcp", timeout=5.0):
        if not 1 <= port <= 65535 or not token:
            raise ValueError("porta ou token inválido")
        self.port, self._token, self.name, self.timeout = port, token, client, timeout
        self._socket = None
        self._buffer = bytearray()
        self._sequence = 0
        self._lock = threading.RLock()
        self.lease = None

    def close(self):
        with self._lock:
            if self._socket is not None:
                self._socket.close()
            self._socket = None
            self._buffer.clear()
            self.lease = None

    def _exchange(self, op, **args):
        self._sequence += 1
        request_id = str(self._sequence)
        payload = json.dumps({"id": request_id, "op": op, **args}, separators=(",", ":"), allow_nan=False).encode() + b"\n"
        if len(payload) > MAX_LINE:
            raise ValueError("mensagem excede o limite")
        deadline = time.monotonic() + self.timeout
        self._socket.settimeout(self.timeout)
        self._socket.sendall(payload)
        while b"\n" not in self._buffer:
            self._socket.settimeout(max(0.001, deadline - time.monotonic()))
            data = self._socket.recv(8192)
            if not data:
                raise ConnectionError("agente fechou a conexão")
            self._buffer.extend(data)
            if len(self._buffer) > MAX_LINE:
                raise AgentError("resposta excede o limite")
            if time.monotonic() >= deadline:
                raise TimeoutError("tempo esgotado aguardando o agente")
        line, _, rest = self._buffer.partition(b"\n")
        self._buffer = bytearray(rest)
        reply = json.loads(line)
        if not isinstance(reply, dict) or reply.get("id") != request_id or type(reply.get("ok")) is not bool:
            raise AgentError("resposta inválida ou id divergente")
        if not reply["ok"]:
            raise AgentError(str(reply.get("error", "erro do agente")))
        return {key: value for key, value in reply.items() if key not in ("id", "ok")}

    def hello(self, force=False):
        with self._lock:
            self.close()
            try:
                self._socket = socket.create_connection(("127.0.0.1", self.port), self.timeout)
                result = self._exchange("hello", token=self._token, client=self.name, force=force,
                                        protocol_version=PROTOCOL_VERSION)
                if type(result.get("protocol_version")) is not int or result["protocol_version"] != PROTOCOL_VERSION:
                    raise ProtocolError("protocolo incompatível: MCP exige v2; recompile e carregue o agente 0.2.0 "
                                        f'(agente informou {result.get("protocol_version", "v1/ausente")})')
                self.lease = result["lease"]
                return result
            except Exception:
                self.close()
                raise

    def request(self, op, **args):
        with self._lock:
            if self._socket is None:
                self.hello()
            try:
                return self._exchange(op, **args)
            except (OSError, ConnectionError, ValueError, AgentError) as error:
                self.close()
                # Uma entrada pode ter sido aceita antes da queda; nunca a repete.
                if op == "state" and isinstance(error, (OSError, ConnectionError)):
                    self.hello()
                    try:
                        return self._exchange(op, **args)
                    except Exception:
                        self.close()
                        raise
                raise

    def state(self):
        return self.request("state")

    def input(self, steps):
        return self.request("input", steps=steps)

    def cancel(self):
        return self.request("cancel")

    def execute(self, op, **args):
        """Espera ticket da thread do jogo; nenhuma mutação é repetida após falha."""
        timeout = args.get("timeout", 3.0 if op == "teleport" else 15.0 if op == "walk_to" else 5.0)
        if type(timeout) not in (float, int) or not math.isfinite(timeout) or not 0.1 <= timeout <= 120:
            raise ValueError("timeout precisa estar em 0.1..120 segundos")
        with self._lock:
            accepted = self.request(op, **args)
            deadline = time.monotonic() + timeout + 5.0
            try:
                ticket = accepted["ticket"]
                while True:
                    result = self.request("result", ticket=ticket)
                    if result.get("status") == "done":
                        body = result.get("result")
                        if not isinstance(body, dict):
                            raise AgentError("resultado v2 inválido")
                        if body.get("status") == "error":
                            raise AgentError(body.get("error", "operação recusada"))
                        return body
                    if result.get("status") != "pending":
                        raise AgentError("status de ticket inválido")
                    if time.monotonic() >= deadline:
                        raise TimeoutError("jogo não avançou; operação cancelada sem repetir a mutação")
                    time.sleep(0.02)
            except BaseException:
                if self._socket is not None:
                    try:
                        self.cancel()
                    except Exception:
                        self.close()
                raise

    def release(self):
        try:
            if self._socket is not None:
                return self.request("release")
        finally:
            self.close()
