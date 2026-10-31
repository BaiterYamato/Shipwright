import copy
import hashlib
import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest

from linkspan_playtest.agent_client import AgentClient, AgentError
from linkspan_playtest.vfs import (MAX_BYTES, query_vfs, save_vfs_snapshot, finalize_vfs_snapshots,
                                   validate_paths, validate_result)
from test_agent_client import FakeAgent


def observed(paths=("test/layers.json",), status=0):
    layers = [{"archive": "base.o2r", "sha256": hashlib.sha256(b"base\x00").hexdigest()},
              {"archive": "override.shipmod", "sha256": hashlib.sha256(b"override").hexdigest()}]
    return {"status": "observed", "frame": 17, "bytes_hashed": 13, "complete": status == 0,
            "instrumentation": {"observer": "linkspan.tool.playtest/vfs_layers", "revision": "a" * 64,
                                "agent_version": "0.2.1", "layout_id": "abcd", "resources_version": 2,
                                "layer_order": "low-to-high"},
            "observations": [{"path": p, "hostStatus": status, "hasFile": status == 0,
                              "complete": status == 0, "layers": copy.deepcopy(layers) if status == 0 else []}
                             for p in paths]}


class VfsTests(unittest.TestCase):
    def setUp(self):
        self.agent = FakeAgent()
        self.client = AgentClient(self.agent.port, "test-only", timeout=0.3)
        self.agent.job_result = observed()

    def tearDown(self):
        self.client.close()
        self.agent.close()

    def test_wire_ticket_order_and_forwarding(self):
        self.agent.pending_polls = 1
        result = query_vfs(self.client, ["test/layers.json"], 2048)
        self.assertEqual([l["archive"] for l in result["observations"][0]["layers"]],
                         ["base.o2r", "override.shipmod"])
        requests = [r for r in self.agent.requests if r["op"] == "vfs_layers"]
        self.assertEqual(len(requests), 1)
        self.assertEqual(requests[0]["max_bytes"], 2048)

    def test_unavailable_or_missing_is_not_complete(self):
        self.agent.job_result = observed(status=2)
        result = query_vfs(self.client, ["test/layers.json"])
        self.assertFalse(result["complete"])
        self.assertEqual(result["observations"][0]["hostStatus"], 2)
        self.assertEqual(result["observations"][0]["layers"], [])

    def test_limit_diagnostic_and_no_replay_after_drop(self):
        self.agent.job_result = observed(status=4)
        self.assertFalse(query_vfs(self.client, ["test/layers.json"])["complete"])
        self.agent.drop_job = True
        with self.assertRaises((AgentError, ConnectionError, OSError)):
            query_vfs(self.client, ["test/layers.json"])
        self.assertEqual(self.agent.jobs, 2)

    def test_validation_before_wire(self):
        for paths in ([], ["x"] * 9, [""], ["a\0b"], [1], ["x", "x"], ["é" * 513]):
            with self.subTest(paths=str(paths)[:40]), self.assertRaises(ValueError):
                query_vfs(self.client, paths)
        for budget in (0, MAX_BYTES + 1, True, 1.5):
            with self.assertRaises(ValueError):
                query_vfs(self.client, ["x"], budget)
        validate_paths(["é" * 512], MAX_BYTES)
        self.assertEqual(self.agent.jobs, 0)

    def test_reject_corrupt_partial_or_wrong_observer(self):
        mutations = [lambda r: r["observations"].clear(),
                     lambda r: r["observations"][0].update(path="other"),
                     lambda r: r["instrumentation"].update(revision="unknown"),
                     lambda r: r["instrumentation"].update(layer_order="high-to-low"),
                     lambda r: r["observations"][0]["layers"][0].update(sha256=""),
                     lambda r: r["observations"][0].update(hostStatus=True),
                     lambda r: r["observations"][0].update(layers=[]),
                     lambda r: r.update(bytes_hashed=MAX_BYTES + 1),
                     lambda r: r.update(complete=False),
                     lambda r: r["observations"][0].update(layers=r["observations"][0]["layers"] * 17)]
        for mutate in mutations:
            result = observed()
            mutate(result)
            with self.assertRaises(ValueError):
                validate_result(result, ["test/layers.json"])


class SnapshotTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.agent = FakeAgent()
        self.client = AgentClient(self.agent.port, "test-only", timeout=0.3)
        self.agent.job_result = observed()
        self.session = SimpleNamespace(directory=self.directory, metadata={"sha256": "b" * 64},
                                       process=SimpleNamespace(poll=lambda: None), client=self.client)

    def tearDown(self):
        self.client.close()
        self.agent.close()
        self.temp.cleanup()

    def close_session(self):
        self.session.metadata["closed"] = True
        self.session.process.poll = lambda: 0
        (self.directory / "game-final.log").write_bytes(b"ready\nunload\nclosed\n")

    def test_two_phases_final_log_and_repeatable_finalization(self):
        ready = save_vfs_snapshot(self.session, "alias-A-B", "after-ready", ["test/layers.json"])
        self.assertEqual(ready["status"], "pending_post_log")
        self.assertIsNone(json.loads(Path(ready["path"]).read_text())["postLogSha256"])
        self.agent.job_result = observed(status=2)
        save_vfs_snapshot(self.session, "alias-A-B", "after-unload", ["test/layers.json"])
        with self.assertRaises(RuntimeError):
            finalize_vfs_snapshots(self.session)
        self.close_session()
        finals = finalize_vfs_snapshots(self.session)
        document = json.loads(Path(finals[0]).read_text())
        self.assertEqual(document["schema"], "rc11-vfs-1")
        self.assertEqual(document["exeSha256"], "b" * 64)
        self.assertFalse(document["complete"])
        self.assertEqual([o["phase"] for o in document["observations"]], ["after-ready", "after-unload"])
        post = self.directory / document["postLogFile"]
        self.assertEqual(document["postLogSha256"], hashlib.sha256(post.read_bytes()).hexdigest())
        self.assertEqual(finalize_vfs_snapshots(self.session), finals)

    def test_invalid_case_phase_duplicate_and_observer_change(self):
        for case, phase in (("../escape", "after-ready"), ("ok", "ready")):
            with self.assertRaises(ValueError):
                save_vfs_snapshot(self.session, case, phase, ["test/layers.json"])
        save_vfs_snapshot(self.session, "ok", "after-ready", ["test/layers.json"])
        with self.assertRaisesRegex(ValueError, "já observado"):
            save_vfs_snapshot(self.session, "ok", "after-ready", ["test/layers.json"])
        self.agent.job_result["instrumentation"]["revision"] = "c" * 64
        with self.assertRaisesRegex(ValueError, "mudou"):
            save_vfs_snapshot(self.session, "ok", "after-unload", ["test/layers.json"])
        draft = next(self.directory.glob("*.pending.json"))
        self.assertEqual(len(json.loads(draft.read_text())["observations"]), 1)

    def test_missing_log_or_sha_does_not_invent_evidence(self):
        self.session.metadata["sha256"] = None
        with self.assertRaises(ValueError):
            save_vfs_snapshot(self.session, "ok", "after-ready", ["test/layers.json"])
        self.session.metadata["sha256"] = "b" * 64
        save_vfs_snapshot(self.session, "ok", "after-ready", ["test/layers.json"])
        self.session.process.poll = lambda: 0
        self.session.metadata["closed"] = True
        with self.assertRaises(FileNotFoundError):
            finalize_vfs_snapshots(self.session)
        self.assertFalse((self.directory / "ok-rc11-vfs-1.json").exists())

    def test_finalized_case_and_tampered_log_are_preserved(self):
        save_vfs_snapshot(self.session, "ok", "after-ready", ["test/layers.json"])
        self.close_session()
        final = Path(finalize_vfs_snapshots(self.session)[0])
        before = final.read_bytes()
        with self.assertRaises(ValueError):
            save_vfs_snapshot(self.session, "ok", "after-unload", ["test/layers.json"])
        (self.directory / "ok-rc11-vfs-1.post.log").write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "diverge"):
            finalize_vfs_snapshots(self.session)
        self.assertEqual(final.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
