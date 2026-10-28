import json
from pathlib import Path
import socket
import sys
import threading
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from linkspan_playtest.agent_client import AgentClient, AgentError


class FakeAgent:
    def __init__(self):
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(8)
        self.listener.settimeout(0.05)
        self.port = self.listener.getsockname()[1]
        self.stop = threading.Event()
        self.owner = None
        self.lock = threading.Lock()
        self.drop_state = False
        self.drop_input = False
        self.inputs = 0
        self.cancelled = 0
        self.workers = []
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.thread.start()

    def serve(self):
        while not self.stop.is_set():
            try:
                connection, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            worker = threading.Thread(target=self.handle, args=(connection,), daemon=True)
            self.workers.append(worker)
            worker.start()

    def handle(self, connection):
        connection.settimeout(0.05)
        buffer = bytearray()
        try:
            while not self.stop.is_set():
                try:
                    data = connection.recv(4096)
                except socket.timeout:
                    continue
                if not data:
                    return
                buffer.extend(data)
                while b"\n" in buffer:
                    line, _, buffer = buffer.partition(b"\n")
                    request = json.loads(line)
                    reply = {"id": request["id"], "ok": True}
                    with self.lock:
                        op = request["op"]
                        if op == "hello":
                            if request["token"] != "test-only":
                                reply.update(ok=False, error="token inválido")
                            elif self.owner not in (None, connection) and not request.get("force"):
                                reply.update(ok=False, error="busy")
                            else:
                                self.owner = connection
                                reply.update(lease="lease", agent_version="0.1.0", layout_id="abcd")
                        elif self.owner is not connection:
                            reply.update(ok=False, error="lease necessário")
                        elif op == "state":
                            if self.drop_state:
                                self.drop_state = False
                                self.owner = None
                                return
                            reply.update(frame=120, game_state="play", scene=85)
                        elif op == "input":
                            self.inputs += 1
                            if self.drop_input:
                                self.drop_input = False
                                self.owner = None
                                return
                            reply.update(queued=len(request["steps"]), start_frame=121, end_frame=122)
                        elif op == "cancel":
                            self.cancelled += 1
                            reply.update(cancelled=True)
                        elif op == "release":
                            self.owner = None
                    encoded = json.dumps(reply).encode() + b"\n"
                    connection.sendall(encoded[:4])
                    connection.sendall(encoded[4:])
        except OSError:
            pass
        finally:
            with self.lock:
                if self.owner is connection:
                    self.owner = None
                    self.cancelled += 1
            connection.close()

    def close(self):
        self.stop.set()
        self.listener.close()
        self.thread.join(1)
        for worker in self.workers:
            worker.join(1)


class AgentTests(unittest.TestCase):
    def setUp(self):
        self.agent = FakeAgent()
        self.client = AgentClient(self.agent.port, "test-only", timeout=0.3)

    def tearDown(self):
        self.client.close()
        self.agent.close()

    def test_protocol_and_input(self):
        self.assertEqual(self.client.hello()["layout_id"], "abcd")
        self.assertEqual(self.client.state()["scene"], 85)
        self.assertEqual(self.client.input([{"buttons": ["A"], "frames": 2}])["end_frame"], 122)
        self.assertTrue(self.client.cancel()["cancelled"])
        self.client.release()
        self.assertIsNone(self.client.lease)

    def test_bad_token(self):
        other = AgentClient(self.agent.port, "wrong", timeout=0.3)
        try:
            with self.assertRaisesRegex(AgentError, "token"):
                other.hello()
        finally:
            other.close()

    def test_busy_and_force(self):
        self.client.hello()
        other = AgentClient(self.agent.port, "test-only", timeout=0.3)
        try:
            with self.assertRaisesRegex(AgentError, "busy"):
                other.hello()
            other.hello(force=True)
            with self.assertRaisesRegex(AgentError, "lease"):
                self.client.state()
            self.assertEqual(other.state()["frame"], 120)
        finally:
            other.close()

    def test_state_reconnect(self):
        self.client.hello()
        self.agent.drop_state = True
        self.assertEqual(self.client.state()["frame"], 120)

    def test_input_is_never_replayed(self):
        self.client.hello()
        self.agent.drop_input = True
        with self.assertRaises(ConnectionError):
            self.client.input([{}])
        self.assertEqual(self.agent.inputs, 1)
        self.assertEqual(self.client.state()["scene"], 85)

    def test_response_id_mismatch(self):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            def peer():
                connection, _ = listener.accept()
                with connection:
                    connection.recv(8192)
                    connection.sendall(b'{"id":"wrong","ok":true,"lease":"x"}\n')
            worker = threading.Thread(target=peer)
            worker.start()
            client = AgentClient(listener.getsockname()[1], "test-only", timeout=0.3)
            try:
                with self.assertRaisesRegex(AgentError, "id"):
                    client.hello()
            finally:
                client.close()
                worker.join(1)


if __name__ == "__main__":
    unittest.main()
