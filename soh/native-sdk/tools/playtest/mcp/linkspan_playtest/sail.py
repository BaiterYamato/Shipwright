"""Servidor local do Sail; o jogo é o cliente TCP."""
import json
import socket
import threading
import time


class Sail:
    def __init__(self, port=43384, timeout=5.0):
        self.timeout = timeout
        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
                self._listener.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            self._listener.bind(("127.0.0.1", port))
            self._listener.listen(1)
            self._listener.settimeout(timeout)
        except Exception:
            self._listener.close()
            raise
        self.port = self._listener.getsockname()[1]
        self._connection = None
        self._buffer = bytearray()
        self._sequence = 0
        self._lock = threading.Lock()

    def command(self, text):
        if not isinstance(text, str) or not text or len(text) > 16384 or "\0" in text:
            raise ValueError("comando inválido")
        with self._lock:
            try:
                deadline = time.monotonic() + self.timeout
                if self._connection is None:
                    self._connection, _ = self._listener.accept()
                self._sequence += 1
                request_id = self._sequence
                self._connection.settimeout(max(0.001, deadline - time.monotonic()))
                self._connection.sendall(json.dumps({"id": request_id, "type": "command", "command": text}).encode() + b"\0")
                while True:
                    while b"\0" not in self._buffer:
                        self._connection.settimeout(max(0.001, deadline - time.monotonic()))
                        data = self._connection.recv(8192)
                        if not data:
                            raise ConnectionError("Sail desconectado")
                        self._buffer.extend(data)
                        if len(self._buffer) > 256 * 1024:
                            raise RuntimeError("mensagem Sail excede o limite")
                    line, _, rest = self._buffer.partition(b"\0")
                    self._buffer = bytearray(rest)
                    reply = json.loads(line)
                    if isinstance(reply, dict) and reply.get("type") == "result" and reply.get("id") == request_id:
                        if reply.get("status") != "success":
                            raise RuntimeError("Sail recusou o comando: " + str(reply.get("status")))
                        return reply
                    if time.monotonic() >= deadline:
                        raise TimeoutError("tempo esgotado aguardando o Sail")
            except Exception:
                if self._connection is not None:
                    self._connection.close()
                self._connection = None
                self._buffer.clear()
                raise

    def close(self):
        with self._lock:
            if self._connection is not None:
                self._connection.close()
                self._connection = None
            self._listener.close()
