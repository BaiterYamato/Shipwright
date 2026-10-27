import json
from pathlib import Path
import socket
import sys
import threading
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from linkspan_playtest.sail import Sail


class SailTests(unittest.TestCase):
    def test_framing_and_results(self):
        server = Sail(port=0, timeout=1)
        received, errors = [], []
        def game():
            try:
                with socket.create_connection(("127.0.0.1", server.port), timeout=1) as client:
                    buffer = bytearray()
                    for _ in range(2):
                        while b"\0" not in buffer:
                            buffer.extend(client.recv(4096))
                        line, _, buffer = buffer.partition(b"\0")
                        request = json.loads(line)
                        received.append(request)
                        event = b'{"type":"event"}\0'
                        result = json.dumps({"id": request["id"], "type": "result", "status": "success"}).encode() + b"\0"
                        client.sendall(event + result[:3])
                        client.sendall(result[3:])
            except Exception as error:
                errors.append(error)
        thread = threading.Thread(target=game)
        thread.start()
        try:
            self.assertEqual(server.command("shiplua_goto_file_select")["status"], "success")
            self.assertEqual(server.command("help")["id"], 2)
        finally:
            thread.join(2)
            server.close()
        self.assertFalse(errors)
        self.assertEqual(received[0]["type"], "command")
        self.assertEqual(received[0]["command"], "shiplua_goto_file_select")

    def test_failure(self):
        server = Sail(port=0, timeout=1)
        def game():
            with socket.create_connection(("127.0.0.1", server.port)) as client:
                request = json.loads(client.recv(4096).split(b"\0")[0])
                client.sendall(json.dumps({"type": "result", "id": request["id"], "status": "failure"}).encode() + b"\0")
        thread = threading.Thread(target=game)
        thread.start()
        try:
            with self.assertRaisesRegex(RuntimeError, "recusou"):
                server.command("bad_command")
        finally:
            thread.join(2)
            server.close()

    def test_invalid_command(self):
        server = Sail(port=0, timeout=0.02)
        try:
            for value in ("", "hello\0there"):
                with self.assertRaises(ValueError):
                    server.command(value)
            with self.assertRaises(TimeoutError):
                server.command("help")
        finally:
            server.close()


if __name__ == "__main__":
    unittest.main()
