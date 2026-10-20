"""Contrato de tools/gen-host-keep.py com uma árvore falsa do host (RFC 0029, matriz §5.3 item 7)."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BASE = Path(__file__).resolve().parents[1]
SCRIPT = BASE / 'gen-host-keep.py'
spec = importlib.util.spec_from_file_location('gen_host_keep', SCRIPT)
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)


class GeneratorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='gen-host-keep-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'root'
        self.write('soh/include/functions.h', '''
#ifndef FUNCTIONS_H
#define FUNCTIONS_H
/* void BlockGhost(void); */
// void LineGhost(void); /* does not open a block
#define Fake(x) MacroGhost(x)
typedef void (*Callback)(int);
void Multi(
    int a,
    int b);
void TakesCallback(void (*cb)(int), int);
BAD_RETURN(s32) WithMacro(void);
void Plain(void);
#ifdef __linux__
void LinuxOnly(void);
#endif
#ifndef __cplusplus
void COnly(void);
#endif
#ifdef __cplusplus
void CppOnly(void);
#endif
void Undefined(void);
void LinkExcluded(void);
#include "segment_symbols.h"
#endif
''')
        self.write('soh/include/segment_symbols.h', 'void SegmentGhost(void);')
        self.write('soh/include/variables.h', '''
#ifndef VARIABLES_H
#define VARIABLES_H
extern int gTable[4];
extern u8 gOpenArray[];
extern void (*gHandler)(int);
extern void (*gHandlers[3])(int);
extern int gMissing;
extern int gStatic;
extern SaveContext gSaveContext;
#endif
''')
        self.write('soh/src/api.c', '''
void Multi(int a, int b) {}
void TakesCallback(void (*cb)(int), int x) {}
BAD_RETURN(s32) WithMacro(void) { return 0; }
void Plain(void) {}
void COnly(void) {}
void CppOnly(void) {}
void LinuxOnly(void) {}
void LinkExcluded(void) {}
void PrivateHelper(void) {}
void SegmentGhost(void) {}
int gTable[4];
u8 gOpenArray[8];
void (*gHandler)(int);
void (*gHandlers[3])(int);
static int gStatic;
''')
        self.write('soh/soh/save.cpp', 'extern "C" {\nSaveContext gSaveContext;\n}\n')
        self.write('tools/host-keep-extra.txt', 'PrivateHelper\n')
        self.write('tools/host-keep-exclude.txt', 'LinkExcluded\n')
        self.write('tools/public-symbols-exclude.txt', 'gOpenArray  # tamanho desconhecido em C\n')

    def write(self, relative, text):
        target = self.root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding='utf-8')

    def cli(self, *args):
        return subprocess.run([sys.executable, '-X', 'utf8', str(SCRIPT), '--root', str(self.root), *map(str, args)],
                              capture_output=True, text=True, encoding='utf-8')

    def snapshot(self, root):
        return {str(p.relative_to(root)): p.read_bytes() for p in root.rglob('*') if p.is_file()}

    def test_catalog_and_report_contract(self):
        keep, extras, prototypes, public, report, count = gen.build(self.root)
        self.assertEqual(count, 14)  # 7 funções C + 7 variáveis; LinuxOnly é condicional.
        self.assertEqual([n for n, _ in public], ['COnly', 'Multi', 'Plain', 'TakesCallback',
                         'WithMacro', 'gHandler', 'gHandlers', 'gSaveContext', 'gTable'])
        self.assertEqual(dict(public), {**{n: gen.FUNCTION for n in public_names(public)[:5]},
                                       **{n: gen.VARIABLE for n in public_names(public)[5:]}})
        self.assertEqual({n: reason for n, _, _, reason in report}, {
            'LinkExcluded': 'fora do link do host (tools/host-keep-exclude.txt)',
            'LinuxOnly': 'declaração condicional de plataforma',
            'Undefined': 'sem definição não static em soh/src ou soh/soh',
            'gMissing': 'sem definição de armazenamento em soh/src ou soh/soh',
            'gOpenArray': 'tamanho desconhecido em C',
            'gStatic': 'sem definição de armazenamento em soh/src ou soh/soh'})
        self.assertEqual(extras, ['PrivateHelper'])
        self.assertIn('void PrivateHelper(void);', prototypes)
        self.assertEqual(keep, ['Multi', 'TakesCallback', 'WithMacro', 'Plain', 'COnly'])
        self.assertNotIn('CppOnly', gen.render_report(report, count, public))
        self.assertNotIn('PrivateHelper', gen.render_public(public))

    def test_repeat_is_byte_identical(self):
        self.assertEqual(self.cli().returncode, 0)
        before = self.snapshot(self.root)
        self.assertEqual(self.cli().returncode, 0)
        self.assertEqual(before, self.snapshot(self.root))

    def test_check_and_drift(self):
        self.assertEqual(self.cli().returncode, 0)
        self.assertEqual(self.cli('--check').returncode, 0)
        self.write('/'.join(gen.CATALOG), 'alterado\n')
        before = self.snapshot(self.root)
        result = self.cli('--check')
        self.assertEqual(result.returncode, 1)
        self.assertIn('OotNativePublicSymbols.c', result.stderr)
        self.assertEqual(before, self.snapshot(self.root))

    def test_check_accepts_crlf(self):
        self.assertEqual(self.cli().returncode, 0)
        for relative in (gen.KEEP, gen.CATALOG, gen.REPORT):
            target = self.root.joinpath(*relative)
            target.write_bytes(target.read_bytes().replace(b'\n', b'\r\n'))
        self.assertEqual(self.cli('--check').returncode, 0)

    def test_output_dir_leaves_root_untouched(self):
        before = self.snapshot(self.root)
        out = Path(self.temp.name) / 'out'
        result = self.cli('--output-dir', out)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(before, self.snapshot(self.root))
        self.assertEqual(len(self.snapshot(out)), 3)

    def test_first_ifndef_is_not_automatically_a_header_guard(self):
        active, platform = gen.active_for_c('#ifndef __linux__\nvoid PlatformOnly(void);\n#endif\n')
        self.assertEqual(gen.declarations(active), [])
        self.assertEqual(gen.declarations(platform), [('PlatformOnly', gen.FUNCTION)])


def public_names(public):
    return [n for n, _ in public]


if __name__ == '__main__':
    unittest.main()
