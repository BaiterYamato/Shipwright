"""tools/nei-code-closure.py com uma árvore sintética: CMakeLists, fork, glue, build e um header do host."""
import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "nei-code-closure.py"
SPEC = importlib.util.spec_from_file_location("nei_code_closure", SCRIPT)
C = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(C)

CMAKE = """add_library(nei_fork OBJECT "${fork_src}/player_unit.c" "${nei_fork_dir}/fork/soh/mods/items/bottles.cpp"
    "${nei_fork_dir}/nei_stubs.c")
target_include_directories(nei_fork PRIVATE x)
add_library(linkspan_nei_core SHARED nei_core.cpp fork/fork_glue.cpp
    $<TARGET_OBJECTS:nei_fork>)
# fim
"""


class ClosureTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.host, self.core, self.build = root / "host", root / "host/soh/native-sdk/nei-core", root / "build"
        files = {
            self.core / "CMakeLists.txt": CMAKE,
            self.core / "nei_core.cpp": "int core;\n",
            self.core / "fork/fork_glue.cpp": "int glue;\n",
            self.core / "fork/player_unit.c": '#include "mods/extended.c"\n#include "../../../soh/Host.h"\n',
            self.core / "fork/overlay-merges/z_x.F.c": "int f;\n",
            self.host / "soh/soh/Host.h": "int host;\n",
            self.build / "fork/soh/mods/extended.c": '#include "extended.h"\n',
            self.build / "fork/soh/mods/extended.h": 'char *p = "__OTR__icon_item_static_yar/x";\n',
            self.build / "fork/soh/mods/items/bottles.cpp": "int b;\n",
            self.build / "fork/soh/mods/unused.c": "int unused;\n",
            self.build / "nei_stubs.c": "int stubs;\n",
            self.build / "extracted/z_kaleido.c": "int k;\n",
        }
        for path, text in files.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def run_tool(self, out):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return C.main(["--nei-core", str(self.core), "--build", str(self.build), "--out", str(out)])

    def test_stages_only_what_the_dll_compiles(self):
        out = Path(self.tmp.name) / "out"
        self.assertEqual(self.run_tool(out), 0)
        listed = (out / "closure.txt").read_text(encoding="utf-8").split()
        self.assertEqual(listed, sorted([
            "build/extracted/z_kaleido.c", "build/nei_stubs.c", "nei-core/fork/fork_glue.cpp",
            "nei-core/fork/overlay-merges/z_x.F.c", "nei-core/fork/player_unit.c", "nei-core/nei_core.cpp",
            "soh/mods/extended.c", "soh/mods/extended.h", "soh/mods/items/bottles.cpp"]))
        # O fork fica no caminho do commit (soh/...), que o build-nei-assets.py classifica por componente.
        self.assertIn("icon_item_static_yar", (out / "soh/mods/extended.h").read_text(encoding="utf-8"))
        self.assertFalse((out / "soh/mods/unused.c").exists())

    def test_unresolved_implementation_include_fails(self):
        (self.build / "fork/soh/mods/extended.c").write_text('#include "gone.c"\n', encoding="utf-8")
        self.assertEqual(self.run_tool(Path(self.tmp.name) / "out"), 2)


if __name__ == "__main__":
    unittest.main()
