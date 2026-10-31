"""Fixtures inteiramente sintéticas: ZIPs/OTEX/ODLT fabricados, nenhum dado de jogo."""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch
import zipfile

SCRIPT = Path(__file__).resolve().parents[1] / 'build-nei-assets.py'
PACKAGE = SCRIPT.with_name('package-nei-core.ps1')
SPEC = importlib.util.spec_from_file_location('nei_assets', SCRIPT)
A = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(A)


def texture(value):
    data = bytearray(0x50)
    data[4:8] = b'XETO'
    struct.pack_into('<I', data, 0x4c, 32)
    return bytes(data) + bytes([value]) * 32


def dl(target):
    data = bytearray(0x48)
    data[4:8] = b'TLDO'
    return bytes(data) + struct.pack('<IIIIII', 0x20000000, 0, A.crc64(target) >> 32,
                                     A.crc64(target) & 0xffffffff, 0xdf000000, 0)


def zip_write(path, entries):
    with zipfile.ZipFile(path, 'w') as archive:
        for name, data in entries.items():
            archive.writestr(name, data)


class Fixtures(unittest.TestCase):
    def setUp(self):
        # Forçar temporários dentro da entrega, inclusive no Windows.
        self.tmp = tempfile.TemporaryDirectory(dir=SCRIPT.parent)
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.oot = self.root / 'oot.o2r'
        self.soh = self.root / 'soh.o2r'
        zip_write(self.oot, {'objects/base/texture': texture(17)})
        zip_write(self.soh, {'host/resource': b'host sintetico'})
        self.bases = [str(self.oot), str(self.soh)]

    def make_manifest(self, core=None, ssbb=None):
        components = {}
        files = []
        for component, entries in [('core', core or {'objects/model/dl': dl('objects/base/texture')}),
                                    ('expansion.ssbb', ssbb or {'objects/object_nei_pokeball/model': b'poke sintetico'})]:
            path = self.root / ('nei-assets-%s.o2r' % component)
            zip_write(path, entries)
            components[component] = dict(archive=path.name, sha256=A.file_record(path)['sha256'],
                                         files=len(entries), game_copies=[], packed_files=A.archive_records(path))
            files += [dict(path=name, component=component, sha256=hashlib.sha256(data).hexdigest())
                      for name, data in entries.items()]
        return dict(version=A.VERSION, profile='oot', bases=A.base_records(self.bases), components=components,
                    files=files, excluded_components=[dict(component='expansion.sm64', reason='MM')],
                    game_data=dict(checked_against=['oot.o2r', 'soh.o2r'], replaced=[]),
                    code_audit=dict(policy='conservative-source-strings/1', reachable_mm=[], missing=[], replaced=[]))

    def save(self, manifest):
        (self.root / 'nei-assets-manifest.json').write_text(json.dumps(manifest), encoding='utf-8')


class VerifyTests(Fixtures):
    def test_valid_and_cli(self):
        self.save(self.make_manifest())
        result = subprocess.run([sys.executable, '-B', str(SCRIPT), '--profile', 'oot', '--verify-assets',
                                 str(self.root), '--base', str(self.oot), '--base', str(self.soh)], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_no_mm_or_base_bypass(self):
        for bases, mm in [(self.bases, ['mm.o2r']), (['oot.o2r', 'mm.o2r'], []),
                          (['soh.o2r'], []), (['oot.o2r', 'oot.o2r'], [])]:
            with self.subTest(bases=bases, mm=mm), self.assertRaises(ValueError):
                A.validate_inputs('oot', bases, mm)
        A.validate_inputs('full', [], ['mm.o2r'])

    def test_mm_cli_refused_before_any_output(self):
        result = subprocess.run([sys.executable, '-B', str(SCRIPT), '--profile', 'oot', '--mm', 'mm.o2r',
                                 '--out', str(self.root / 'absent')], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'--mm', result.stderr)
        self.assertFalse((self.root / 'absent').exists())

    def test_missing_excluded_or_wrong_profile(self):
        for key, value in [('profile', 'full'), ('excluded_components', []), ('code_audit', {})]:
            manifest = self.make_manifest()
            manifest[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                A.verify_archives(self.root, manifest, self.bases)

    def test_stale_full_directory_is_refused(self):
        manifest = self.make_manifest()
        zip_write(self.root / 'nei-assets-expansion.sm64.o2r', {'fake': b'fake'})
        with self.assertRaisesRegex(ValueError, 'saída limpa'):
            A.verify_archives(self.root, manifest, self.bases)

    def test_reachable_mm_even_if_report_would_call_it_optional(self):
        for data in [dl('objects/object_mask_bu_san/MM'), b'<R><D Path="__OTR__mm/tex" /></R>',
                     b"<R><D TexturePath='__OTR__objects/object_mkk/MM' /></R>"]:
            manifest = self.make_manifest(core={'objects/model/dl': data})
            with self.subTest(data=data[:8]), self.assertRaisesRegex(ValueError, 'referência MM'):
                A.verify_archives(self.root, manifest, self.bases)

    def test_transitive_reference_to_excluded_component(self):
        manifest = self.make_manifest(core={'objects/model/a': dl('objects/model/b'),
                                            'objects/model/b': dl('objects/object_nei_mario_mask/model')})
        with self.assertRaisesRegex(ValueError, 'referência MM'):
            A.verify_archives(self.root, manifest, self.bases)

    def test_ssbb_unknown_reference_is_fatal(self):
        manifest = self.make_manifest(ssbb={'objects/object_nei_pokeball/model': dl('arbitrary/unavailable')})
        with self.assertRaisesRegex(ValueError, 'referência MM'):
            A.verify_archives(self.root, manifest, self.bases)

    def test_texture_copy_not_just_manifest_claim(self):
        manifest = self.make_manifest(core={'objects/model/tex': texture(17)})
        with self.assertRaisesRegex(ValueError, 'cópia de textura'):
            A.verify_archives(self.root, manifest, self.bases)

    def test_hash_archive_resource_and_base(self):
        for kind in ('archive', 'packed', 'base'):
            manifest = self.make_manifest()
            if kind == 'archive':
                manifest['components']['core']['sha256'] = '0' * 64
            elif kind == 'packed':
                manifest['components']['core']['packed_files'][0]['sha256'] = '0' * 64
            else:
                manifest['bases'][0]['sha256'] = '0' * 64
            with self.subTest(kind=kind), self.assertRaisesRegex(ValueError, 'SHA-256'):
                A.verify_archives(self.root, manifest, self.bases)

    def test_overrides_portversion_and_truncated_dl(self):
        for name, data in [('objects/base/texture', b'shadow'), ('portVersion', b'0.3.9'),
                           ('objects/model/dl', b'\0\0\0\0TLDO')]:
            manifest = self.make_manifest(core={name: data})
            with self.subTest(name=name), self.assertRaises(ValueError):
                A.verify_archives(self.root, manifest, self.bases)

    def test_code_audit_mm_is_fatal(self):
        manifest = self.make_manifest()
        manifest['code_audit']['reachable_mm'] = [{'path': 'mm/texture', 'componentes': ['core']}]
        with self.assertRaisesRegex(ValueError, 'código incluído'):
            A.verify_archives(self.root, manifest, self.bases)

    def test_replaced_must_describe_package_only(self):
        manifest = self.make_manifest()
        manifest['game_data']['replaced'] = [dict(path='objects/object_nei_mario_mask/tex',
                                                 target='objects/base/texture', archive='oot.o2r',
                                                 component='expansion.sm64')]
        with self.assertRaisesRegex(ValueError, 'componente excluído'):
            A.verify_archives(self.root, manifest, self.bases)


class GeneratorTests(Fixtures):
    def generate(self, profile='oot', extra_code='', packed_extra=None, inactive_mm=False):
        source = self.root / 'fork'
        file = source / 'soh/mods/items/test.c'
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text('const char *base = "__OTR__objects/base/texture";\n' + extra_code, encoding='utf-8')
        if inactive_mm:
            optional = source / 'soh/mods/mm_sources/optional.c'
            optional.parent.mkdir(parents=True)
            optional.write_text('char *path = "__OTR__objects/object_mkk/MM";', encoding='utf-8')
        entries = {'objects/model/tex': texture(17), 'objects/model/dl': dl('objects/model/tex'),
                   'objects/object_nei_mario_mask/tex': texture(93),
                   'objects/object_nei_pokeball/model': b'poke', 'objects/forms/garo/model': b'form'}
        blobs = {A.PREFIX + name: data for name, data in entries.items()}
        cm = types.SimpleNamespace(MODEL_FILES=[], plan=lambda *_: ({}, []), resources=lambda *_: {}, parse_arrays=lambda _: [])
        out = self.root / 'output'

        def pack(command, **kwargs):
            stage, dest = Path(command[1]), Path(command[2])
            data = {f.relative_to(stage).as_posix(): f.read_bytes() for f in stage.rglob('*') if f.is_file()}
            data['portVersion'] = b'synthetic version'
            if packed_extra and dest.name == 'nei-assets-core.o2r':
                data.update(packed_extra)
            zip_write(dest, data)

        argv = ['build', '--repo', str(self.root), '--packer', 'synthetic-packer',
                '--base', str(self.oot), '--base', str(self.soh), '--fork-source', str(source), '--out', str(out)]
        if profile is not None:
            argv += ['--profile', profile]
        with patch.object(sys, 'argv', argv), patch.object(A, 'fork_files', return_value=list(blobs)), \
                patch.object(A, 'read_blobs', side_effect=lambda _, __, paths: {p: blobs[p] for p in paths}), \
                patch.object(A.importlib, 'import_module', return_value=cm), patch.object(A.subprocess, 'run', side_effect=pack), \
                contextlib.redirect_stdout(io.StringIO()):
            status = A.main()
        return status, json.loads((out / 'nei-assets-manifest.json').read_text(encoding='utf-8')), out

    def test_oot_selects_rewrites_and_records_bases(self):
        status, manifest, out = self.generate()
        self.assertEqual(status, 0)
        self.assertEqual(set(manifest['components']), set(A.OOT_COMPONENTS))
        self.assertEqual({e['component'] for e in manifest['excluded_components']}, {'form.garo', 'expansion.sm64'})
        self.assertEqual(manifest['game_data']['replaced'][0]['component'], 'core')
        self.assertNotIn('mm.o2r', manifest['game_data']['checked_against'])
        A.verify_assets(out, 'oot', self.bases)

    def test_full_still_generates_all_components(self):
        status, manifest, _ = self.generate(None)
        self.assertEqual(status, 0)
        self.assertEqual(manifest['profile'], 'full')
        self.assertIn('expansion.sm64', manifest['components'])
        self.assertIn('form.garo', manifest['components'])

    def test_included_mm_code_is_fatal_and_recorded(self):
        status, manifest, _ = self.generate(extra_code='const char *bad = "__OTR__objects/object_mkk/MM";')
        self.assertEqual(status, 1)
        self.assertEqual(len(manifest['code_audit']['reachable_mm']), 1)

    def test_bare_mm_loader_path_is_fatal(self):
        status, manifest, _ = self.generate(extra_code='TransformMasks_LoadMmDL("objects/object_link_child/gLinkHumanMirrorShieldDL");')
        self.assertEqual(status, 1)
        self.assertEqual(len(manifest['code_audit']['reachable_mm']), 1)

    def test_arbitrary_otr_namespace_is_still_checked(self):
        status, manifest, _ = self.generate(extra_code='char *bad = "__OTR__misc/unavailable";')
        self.assertEqual(status, 1)
        self.assertEqual(manifest['code_audit']['missing'][0]['path'], 'misc/unavailable')

    def test_excluded_mm_code_stays_visible_without_blocking(self):
        status, manifest, out = self.generate(inactive_mm=True)
        self.assertEqual(status, 0)
        self.assertEqual(manifest['code_audit']['reachable_mm'], [])
        report = json.loads((out / 'nei-assets-report.json').read_text(encoding='utf-8'))
        self.assertEqual(report['code_mm'][0]['componentes'], ['mm'])

    def test_postpacker_new_game_copy_is_fatal(self):
        status, _, _ = self.generate(packed_extra={'objects/model/late_png_tex': texture(17)})
        self.assertEqual(status, 1)

    def test_postpacker_new_mm_reference_is_fatal(self):
        status, _, _ = self.generate(packed_extra={'objects/model/late_dl': dl('mm/late')})
        self.assertEqual(status, 1)


@unittest.skipUnless(shutil.which('powershell'), 'PowerShell não disponível')
class PowerShellTests(Fixtures):
    def ps(self, code):
        return subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-Command', code],
                              cwd=self.root, capture_output=True)

    def test_parser(self):
        path = str(PACKAGE).replace("'", "''")
        result = self.ps("$t=$null; $e=$null; [System.Management.Automation.Language.Parser]::ParseFile('" + path +
                         "',[ref]$t,[ref]$e) | Out-Null; if ($e.Count) {$e; exit 1}")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_actual_package_oot_and_full_selection(self):
        # Executa o empacotador real com fonte/DLL/exe/validator sintéticos. Só o helper ZIP é um stub.
        for profile in ('oot', 'full'):
            with self.subTest(profile=profile):
                manifest = self.make_manifest()
                manifest.update(format='linkspan.nei.assets/1', source={}, license={})
                if profile == 'full':
                    manifest['profile'] = 'full'
                    manifest['game_data']['checked_against'].append('mm.o2r')
                    archive = self.root / 'nei-assets-expansion.sm64.o2r'
                    zip_write(archive, {'objects/object_nei_mario_mask/fake': b'fake'})
                    manifest['components']['expansion.sm64'] = dict(archive=archive.name,
                        sha256=A.file_record(archive)['sha256'], game_copies=[])
                    manifest['game_data']['replaced'] = [dict(path='objects/forms/garo/fake', target='mm/fake',
                        archive='mm.o2r', component='form.garo')]
                self.save(manifest)
                scripts = self.root / 'tools'
                scripts.mkdir(exist_ok=True)
                shutil.copyfile(SCRIPT, scripts / SCRIPT.name)
                shutil.copyfile(PACKAGE, scripts / PACKAGE.name)
                (scripts / 'linkspan-zip.ps1').write_text('function New-LinkSpanZip { param($Source,$Destination)\n'
                    'Add-Type -AssemblyName System.IO.Compression.FileSystem\n'
                    'if (Test-Path $Destination) { Remove-Item -LiteralPath $Destination }\n'
                    '[IO.Compression.ZipFile]::CreateFromDirectory($Source,$Destination) }', encoding='utf-8-sig')
                source = self.root / 'soh/native-sdk/nei-core'
                for name in ('core-main.lua', 'include/linkspan/nei/nei_items.h', 'README.md', 'NOTICE.md',
                             'demo/main.lua', 'demo/nei_demo.cpp'):
                    file = source / name
                    file.parent.mkdir(parents=True, exist_ok=True)
                    file.write_text('synthetic', encoding='utf-8')
                license_file = self.root / 'docs/licensing/LICENSE-PACKAGE.txt'
                license_file.parent.mkdir(parents=True, exist_ok=True)
                license_file.write_text('synthetic', encoding='utf-8')
                (self.root / 'linkspan_nei_core.dll').write_bytes(b'synthetic DLL, not executable')
                (self.root / 'host.exe').write_bytes(b'synthetic EXE, not executable')
                fingerprint = A.file_record(self.root / 'host.exe')['sha256']
                (self.root / 'soh.symbols').write_text('linkspan-symbols 1\nsha256 ' + fingerprint, encoding='utf-8')
                (self.root / 'layout.h').write_text('LINKSPAN_OOT_LAYOUT_ID "' + 'a'*64 + '"', encoding='utf-8')
                (self.root / 'copyright').write_text('synthetic', encoding='utf-8')
                (self.root / 'validator.ps1').write_text('exit 0', encoding='utf-8')
                quote = lambda value: "'" + str(value).replace("'", "''") + "'"
                command = '& ./tools/package-nei-core.ps1 ' + ('-Profile oot ' if profile == 'oot' else '') + '-ProviderDirectory . ' + \
                    '-Validator ./validator.ps1 -LayoutHeader ./layout.h -JsonLicense ./copyright ' + \
                    '-HostExecutable ./host.exe -HostSymbols ./soh.symbols -AssetsDirectory . ' + \
                    '-OutputDirectory ./package -Python ' + quote(sys.executable) + \
                    ' -BaseArchive ./oot.o2r,./soh.o2r'
                result = self.ps(command)
                self.assertEqual(result.returncode, 0, (result.stdout + result.stderr).decode(errors='replace'))
                package = next((self.root / 'package').glob('*.shipmod'))
                with zipfile.ZipFile(package) as archive:
                    names = archive.namelist()
                    packed = json.loads(archive.read('assets/nei-assets-manifest.json'))
                self.assertEqual(packed['profile'], profile)
                self.assertEqual(packed['game_data']['replaced'], [])
                self.assertEqual('assets/nei-assets-expansion.sm64.o2r' in names, profile == 'full')
                self.assertEqual({e['component'] for e in packed['files']}, set(A.OOT_COMPONENTS))
                if profile == 'oot':
                    manifest['components']['core']['sha256'] = '0' * 64
                    self.save(manifest)
                    refused = self.ps(command)
                    self.assertNotEqual(refused.returncode, 0)


if __name__ == '__main__':
    unittest.main()
