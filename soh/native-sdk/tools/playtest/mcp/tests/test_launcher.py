import json
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch, Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from linkspan_playtest import launcher
from linkspan_playtest.launcher import sail_settings


class SettingsTests(unittest.TestCase):
    def test_protocol_mismatch_does_not_retry_for_90_seconds(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            runtime, agent = root / "runtime", root / "agent"
            runtime.mkdir(); (agent / "provider").mkdir(parents=True)
            (runtime / "soh.exe").write_bytes(b"fake-exe")
            (agent / "manifest.toml").write_text("fake manifest")
            (agent / "provider/linkspan_playtest.dll").write_bytes(b"fake-dll")
            process = SimpleNamespace(pid=12, code=None)
            process.poll = lambda: process.code
            process.terminate = lambda: setattr(process, "code", 0)
            process.wait = lambda timeout: 0
            client = Mock()
            client.hello.side_effect = launcher.ProtocolError("MCP exige v2; recompile o agente")
            with patch.dict(launcher.os.environ, {"LINKSPAN_PLAYTEST_AGENT": str(agent),
                                                 "LINKSPAN_PLAYTEST_EVIDENCE": str(root / "evidence")}):
                with patch.object(launcher.subprocess, "Popen", return_value=process), \
                        patch.object(launcher, "AgentClient", return_value=client), \
                        patch.object(launcher.time, "sleep") as sleep:
                    with self.assertRaisesRegex(RuntimeError, "MCP exige v2"):
                        launcher.launch(str(runtime))
            self.assertEqual(client.hello.call_count, 1)
            sleep.assert_not_called()

    def test_restore_preserves_unrelated_edits(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "shipofharkinian.json"
            path.write_text(json.dumps({"CVars": {"gRemote": {"Sail": {"Enabled": 0, "Port": 12345, "Other": 8}}}}))
            original = sail_settings(path)
            current = json.loads(path.read_text())
            self.assertEqual(current["CVars"]["gRemote"]["Sail"]["Host"], "127.0.0.1")
            current["Window"] = {"Width": 1200}
            path.write_text(json.dumps(current))
            sail_settings(path, original)
            restored = json.loads(path.read_text())
            self.assertEqual(restored["CVars"]["gRemote"]["Sail"], {"Enabled": 0, "Port": 12345, "Other": 8})
            self.assertEqual(restored["Window"]["Width"], 1200)

    def test_missing_values_removed(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "shipofharkinian.json"
            original = sail_settings(path)
            sail_settings(path, original)
            self.assertEqual(json.loads(path.read_text()), {})

    def test_close_restores_config_and_removes_token(self):
        with tempfile.TemporaryDirectory() as folder:
            runtime = Path(folder)
            evidence = runtime / "evidence"
            evidence.mkdir()
            config = runtime / "shipofharkinian.json"
            original = sail_settings(config)
            log = runtime / "game.log"
            historical = b"[error] old session\n"
            log.write_bytes(historical + b"[info] new session\n[critical] current failure\n")
            (evidence / "host-stdout.log").write_text("[error] native failure\n")
            process = SimpleNamespace(pid=12, poll=lambda: 0)
            client = SimpleNamespace(close=lambda: None)
            metadata = {"runtime": str(runtime), "log": str(log), "log_offset": len(historical),
                        "sail_original": original, "token": "test-only", "closed": False}
            session = launcher.Session(evidence, metadata, process, client)
            result = session.close()
            self.assertEqual(result["exit_code"], 0)
            self.assertEqual(result["errors"], ["[critical] current failure", "[error] native failure"])
            self.assertEqual(json.loads(config.read_text()), {})
            self.assertNotIn("token", json.loads((evidence / "session.json").read_text()))
            self.assertNotIn(b"old session", (evidence / "game-final.log").read_bytes())

    def test_close_sends_window_message(self):
        with tempfile.TemporaryDirectory() as folder:
            process = SimpleNamespace(pid=12, code=None)
            process.poll = lambda: process.code
            session = launcher.Session(Path(folder), {"log": str(Path(folder) / "none.log"), "closed": False},
                                       process, SimpleNamespace(close=lambda: None))
            def close(pid):
                self.assertEqual(pid, 12)
                process.code = 0
            with patch.object(launcher, "close_window", close):
                self.assertEqual(session.close()["exit_code"], 0)

    def test_launch_failure_rolls_back_sail(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            runtime, agent = root / "runtime", root / "agent"
            runtime.mkdir()
            (agent / "provider").mkdir(parents=True)
            (runtime / "soh.exe").write_bytes(b"fake-exe")
            (agent / "manifest.toml").write_text("fake manifest")
            (agent / "provider/linkspan_playtest.dll").write_bytes(b"fake-dll")
            config = runtime / "shipofharkinian.json"
            config.write_text('{"CVars":{"gRemote":{"Sail":{"Enabled":0,"Port":123}}}}')
            before = json.loads(config.read_text())
            fake_sail = SimpleNamespace(close=lambda: None)
            fake_process = SimpleNamespace(pid=12, poll=lambda: 2)
            with patch.dict(launcher.os.environ, {"LINKSPAN_PLAYTEST_AGENT": str(agent),
                                                 "LINKSPAN_PLAYTEST_EVIDENCE": str(root / "evidence")}):
                with patch.object(launcher, "Sail", return_value=fake_sail), \
                        patch.object(launcher.subprocess, "Popen", return_value=fake_process):
                    with self.assertRaisesRegex(RuntimeError, "antes do hello"):
                        launcher.launch(str(runtime), console=True)
            self.assertEqual(json.loads(config.read_text()), before)
            metadata = json.loads(next((root / "evidence").glob("*/session.json")).read_text())
            self.assertTrue(metadata["closed"])
            self.assertNotIn("token", metadata)


if __name__ == "__main__":
    unittest.main()
