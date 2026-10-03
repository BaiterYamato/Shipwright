#!/usr/bin/env python3
"""Varredura sem dependencias de conteudo protegido em releases Link-Span.

Aceita diretorios, arquivos comuns e archives ZIP (inclusive ZIPs aninhados).
O programa nunca extrai arquivos para a arvore analisada; archives aninhados sao
copiados em arquivos temporarios e lidos em streaming.
"""
from __future__ import print_function

import argparse
import fnmatch
import hashlib
import json
import os
import shutil
import sys
import tempfile
import zipfile

ROM_EXTENSIONS = {".z64", ".n64", ".v64", ".rom"}
ROM_MAGICS = {
    b"\x80\x37\x12\x40",  # .z64 / big endian
    b"\x37\x80\x40\x12",  # .n64 / little endian
    b"\x12\x40\x37\x80",  # .v64 / byte-swapped
}
SAVE_EXTENSIONS = {".sav", ".sra", ".fla", ".eep"}
DUMP_EXTENSIONS = {".dmp", ".dump", ".mdmp"}
CHUNK_SIZE = 1024 * 1024


class Scanner(object):
    def __init__(self, max_file_bytes, allow_large, max_depth):
        self.max_file_bytes = max_file_bytes
        self.allow_large = allow_large
        self.max_depth = max_depth
        self.findings = []
        self.scanned_files = 0
        self.scanned_archives = 0
        self.scanned_bytes = 0
        self.errors = []

    def add(self, kind, path, detail):
        self.findings.append({"kind": kind, "path": path, "detail": detail})

    def is_allowed_large(self, path):
        # ! separa containers virtuais (outer.zip!inner.zip!entry) e deve
        # participar da mesma validação de segmentos que uma barra.
        normalized = path.replace("\\", "/").replace("!", "/")
        return any(fnmatch.fnmatchcase(normalized.lower(), pat.lower()) for pat in self.allow_large)

    def check_name_and_size(self, path, size):
        normalized = path.replace("\\", "/").replace("!", "/")
        parts = [part for part in normalized.split("/") if part]
        base = parts[-1].lower() if parts else ""
        ext = os.path.splitext(base)[1]
        if ext in ROM_EXTENSIONS:
            self.add("rom_extension", path, "extensao proibida: %s" % ext)
        if base in {"oot.o2r", "mm.o2r"}:
            self.add("protected_archive", path, "archive derivado protegido: %s" % base)
        if ext in SAVE_EXTENSIONS or base.startswith("file") and base.endswith(".sav"):
            self.add("save_file", path, "arquivo de save")
        if base == "save" or any(part.lower() == "save" for part in parts[:-1]):
            self.add("save_directory", path, "arquivo dentro da pasta Save")
        if ext in DUMP_EXTENSIONS or any(part.lower() in {"dump", "dumps"} for part in parts):
            self.add("dump", path, "dump ou arquivo dentro de pasta dump")
        if size > self.max_file_bytes and not self.is_allowed_large(normalized):
            self.add(
                "large_file",
                path,
                "%d bytes excede o limite de %d bytes (use --allow-large)" % (size, self.max_file_bytes),
            )

    def scan_stream(self, stream, path, size, depth):
        self.scanned_files += 1
        self.scanned_bytes += size
        self.check_name_and_size(path, size)
        head = stream.read(4)
        if head in ROM_MAGICS:
            self.add("rom_magic", path, "cabecalho N64 0x80371240 em uma das tres ordens")
        is_zip = head.startswith(b"PK\x03\x04") or head.startswith(b"PK\x05\x06") or head.startswith(b"PK\x07\x08")
        if not is_zip:
            return
        if depth >= self.max_depth:
            self.add("nested_zip_depth", path, "ZIP aninhado excede --max-depth=%d" % self.max_depth)
            return
        # zipfile precisa de seek; manter a copia fora da arvore de entrada evita escrita nela.
        fd, temp_name = tempfile.mkstemp(prefix="linkspan-protected-scan-", suffix=".zip")
        try:
            with os.fdopen(fd, "wb") as output:
                output.write(head)
                shutil.copyfileobj(stream, output, CHUNK_SIZE)
            try:
                with zipfile.ZipFile(temp_name, "r") as archive:
                    self.scan_zip(archive, path, depth + 1)
            except (zipfile.BadZipFile, OSError, RuntimeError) as exc:
                self.errors.append({"path": path, "error": "ZIP ilegivel: %s" % exc})
        finally:
            try:
                os.unlink(temp_name)
            except OSError:
                pass

    def scan_zip(self, archive, container_path, depth):
        self.scanned_archives += 1
        for entry in archive.infolist():
            entry_path = container_path + "!" + entry.filename.replace("\\", "/")
            if entry.is_dir():
                # Diretorios Save e dumps tambem sao evidencias, mesmo vazios.
                self.check_name_and_size(entry_path.rstrip("/"), 0)
                continue
            try:
                with archive.open(entry, "r") as source:
                    self.scan_stream(source, entry_path, entry.file_size, depth)
            except (OSError, RuntimeError, zipfile.BadZipFile) as exc:
                self.errors.append({"path": entry_path, "error": "entrada ZIP ilegivel: %s" % exc})

    def scan_path(self, path):
        if os.path.isdir(path):
            self.check_name_and_size(path, 0)
            for root, dirs, files in os.walk(path):
                dirs.sort(key=str.lower)
                for name in dirs:
                    self.check_name_and_size(os.path.join(root, name), 0)
                for name in sorted(files, key=str.lower):
                    full_path = os.path.join(root, name)
                    try:
                        with open(full_path, "rb") as source:
                            self.scan_stream(source, full_path, os.path.getsize(full_path), 0)
                    except OSError as exc:
                        self.errors.append({"path": full_path, "error": str(exc)})
            return
        if os.path.isfile(path):
            try:
                with open(path, "rb") as source:
                    self.scan_stream(source, path, os.path.getsize(path), 0)
            except OSError as exc:
                self.errors.append({"path": path, "error": str(exc)})
            return
        self.errors.append({"path": path, "error": "caminho inexistente ou nao regular"})

    def report(self, inputs):
        return {
            "schemaVersion": 1,
            "inputs": inputs,
            "maxFileBytes": self.max_file_bytes,
            "allowLarge": self.allow_large,
            "maxDepth": self.max_depth,
            "summary": {
                "scannedFiles": self.scanned_files,
                "scannedArchives": self.scanned_archives,
                "scannedBytes": self.scanned_bytes,
                "findings": len(self.findings),
                "errors": len(self.errors),
                "passed": not self.findings and not self.errors,
            },
            "findings": self.findings,
            "errors": self.errors,
        }


def text_report(report):
    summary = report["summary"]
    lines = [
        "SCAN PROTECTED CONTENT: %s" % ("OK" if summary["passed"] else "FALHOU"),
        "Arquivos: %(scannedFiles)d | ZIPs: %(scannedArchives)d | Bytes: %(scannedBytes)d" % summary,
        "Limite por arquivo: %d bytes" % report["maxFileBytes"],
    ]
    for finding in report["findings"]:
        lines.append("PROIBIDO [%s] %s -- %s" % (finding["kind"], finding["path"], finding["detail"]))
    for error in report["errors"]:
        lines.append("ERRO %s -- %s" % (error["path"], error["error"]))
    return "\n".join(lines) + "\n"


def write_text(path, content):
    if path:
        with open(path, "w", encoding="utf-8", newline="\n") as output:
            output.write(content)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", help="pastas ou arquivos a verificar")
    parser.add_argument("--max-file-bytes", type=int, default=64 * 1024 * 1024)
    parser.add_argument("--allow-large", action="append", default=[], metavar="GLOB")
    parser.add_argument("--max-depth", type=int, default=8)
    parser.add_argument("--json-out", help="caminho do relatorio JSON")
    parser.add_argument("--text-out", help="caminho do relatorio de texto")
    args = parser.parse_args(argv)
    if args.max_file_bytes < 0 or args.max_depth < 0:
        parser.error("--max-file-bytes e --max-depth devem ser >= 0")
    scanner = Scanner(args.max_file_bytes, args.allow_large, args.max_depth)
    for path in args.paths:
        scanner.scan_path(os.path.abspath(path))
    report = scanner.report([os.path.abspath(path) for path in args.paths])
    json_text = json.dumps(report, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    human_text = text_report(report)
    write_text(args.json_out, json_text)
    write_text(args.text_out, human_text)
    sys.stdout.write(json_text)
    sys.stderr.write(human_text)
    return 0 if report["summary"]["passed"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
