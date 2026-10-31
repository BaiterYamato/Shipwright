import json
from pathlib import Path
import socket
import sys
import threading
import unittest
from unittest.mock import patch, Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from linkspan_playtest.agent_client import AgentClient, AgentError, ProtocolError


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
        self.protocol = 2
        self.drop_job = False
        self.jobs = 0
        self.pending_polls = 0
        self.job_result = {"status": "grounded", "position": {"x": 1, "y": 2, "z": 3}}
        self.requests = []
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
                        self.requests.append({k: v for k, v in request.items() if k != "token"})
                        if op == "hello":
                            if request["token"] != "test-only":
                                reply.update(ok=False, error="token inválido")
                            elif self.owner not in (None, connection) and not request.get("force"):
                                reply.update(ok=False, error="busy")
                            else:
                                self.owner = connection
                                reply.update(lease="lease", agent_version="0.2.0", layout_id="abcd")
                                if self.protocol is not None:
                                    reply["protocol_version"] = self.protocol
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
                        elif op == "result":
                            if self.pending_polls:
                                self.pending_polls -= 1
                                reply.update(ticket=1, status="pending", result=None)
                            else:
                                reply.update(ticket=1, status="done", result=self.job_result)
                        elif op in ("scene_map", "actors", "teleport", "camera_state", "camera_set", "walk_to"):
                            self.jobs += 1
                            if self.drop_job:
                                self.owner = None
                                return
                            reply.update(ticket=1, status="pending")
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

    def test_v2_handshake_rejects_old_and_future_agents(self):
        for version in (None, 1, 3, True, 2.0):
            self.agent.protocol = version
            with self.assertRaisesRegex(ProtocolError, "MCP exige v2"):
                self.client.hello()
            self.assertIsNone(self.client.lease)
            # Cada tentativa desconecta; aguarda a limpeza do fake antes da próxima.
            for _ in range(100):
                with self.agent.lock:
                    if self.agent.owner is None:
                        break
                threading.Event().wait(0.005)

    def test_v2_jobs_poll_without_replaying(self):
        self.agent.pending_polls = 2
        result = self.client.execute("teleport", x=1, y=2, z=3, yaw=90, timeout=1)
        self.assertEqual(result["status"], "grounded")
        self.assertEqual(self.agent.jobs, 1)
        hello = next(r for r in self.agent.requests if r["op"] == "hello")
        self.assertEqual(hello["protocol_version"], 2)
        self.assertEqual(sum(r["op"] == "result" for r in self.agent.requests), 3)

    def test_v2_mutation_lost_reply_is_not_replayed(self):
        self.agent.drop_job = True
        with self.assertRaises(ConnectionError):
            self.client.execute("teleport", x=1, y=2, z=3)
        self.assertEqual(self.agent.jobs, 1)

    def test_v2_job_error_cancels(self):
        self.agent.job_result = {"status": "error", "error": "serviço indisponível"}
        with self.assertRaisesRegex(AgentError, "serviço"):
            self.client.execute("scene_map")
        self.assertGreaterEqual(self.agent.cancelled, 1)

    def test_v2_finite_timeout(self):
        for value in (True, 0, -1, 121, float("nan"), float("inf")):
            with self.assertRaises(ValueError):
                self.client.execute("walk_to", x=1, z=2, timeout=value)
        self.assertEqual(self.agent.jobs, 0)

    def test_v2_stalled_frames_cancel_without_replay(self):
        client = AgentClient(self.agent.port, "test-only")
        client._socket = Mock()
        responses = [{"ticket": 1}, {"ticket": 1, "status": "pending", "result": None}]
        try:
            with patch.object(client, "request", side_effect=responses) as request, \
                    patch.object(client, "cancel") as cancel, \
                    patch("linkspan_playtest.agent_client.time.monotonic", side_effect=[0, 10]):
                with self.assertRaisesRegex(TimeoutError, "cancelada"):
                    client.execute("walk_to", x=1, z=2, timeout=0.1)
                self.assertEqual(request.call_count, 2)
                cancel.assert_called_once()
        finally:
            client.close()

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
