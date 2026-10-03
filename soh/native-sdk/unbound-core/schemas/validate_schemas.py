#!/usr/bin/env python3
"""Validate known Unbound 2 JSON documents with the schemas shipped beside this file."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from jsonschema import Draft202012Validator
from referencing import Registry, Resource


# Exemplos do próprio repositório, relativos a soh/native-sdk/unbound-core.
_UNBOUND_CORE = Path(__file__).resolve().parent.parent
DEFAULT_ROOTS = (
    _UNBOUND_CORE / "custom-actors-demo" / "assets",
    _UNBOUND_CORE / "scene-demo",
    _UNBOUND_CORE / "field-demo",
)


def strip_json_comments(text: str) -> str:
    """Remove // and /* */ comments without touching quoted JSON strings."""
    result: list[str] = []
    index = 0
    in_string = False
    escaped = False
    while index < len(text):
        char = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if in_string:
            result.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            index += 1
        elif char == '"':
            result.append(char)
            in_string = True
            index += 1
        elif char == "/" and following == "/":
            index += 2
            while index < len(text) and text[index] not in "\r\n":
                index += 1
        elif char == "/" and following == "*":
            index += 2
            while index + 1 < len(text) and text[index:index + 2] != "*/":
                if text[index] in "\r\n":
                    result.append(text[index])
                index += 1
            index += 2
        else:
            result.append(char)
            index += 1
    return "".join(result)


def document_kind(path: Path, root: Path) -> str | None:
    parts = path.relative_to(root).as_posix().split("/")
    if path.name == "unbound.json":
        return "manifest"
    if path.name == "scene.json" and "scenes" in parts:
        return "scene"
    if path.name == "collision.json" and "scenes" in parts:
        return "collision"
    if path.name == "messages.json" and len(parts) >= 3 and parts[-3] == "text":
        return "text"
    if path.name == "scenes.json" and parts[-2:] == ["unbound", "scenes.json"]:
        return "scene-registry"
    if "unbound" in parts and "actors" in parts:
        return "actor-type"
    if "scenes" in parts and "rooms" in parts:
        return "room"
    if "scenes" in parts and "paths" in parts:
        return "paths"
    return None


def load_validators(schema_dir: Path) -> dict[str, Draft202012Validator]:
    resources: list[tuple[str, Resource]] = []
    documents: dict[str, dict] = {}
    for schema_path in sorted(schema_dir.glob("*.schema.json")):
        contents = json.loads(schema_path.read_text(encoding="utf-8"))
        schema_id = contents["$id"]
        documents[schema_path.name.removesuffix(".schema.json")] = contents
        resources.append((schema_id, Resource.from_contents(contents)))
    registry = Registry().with_resources(resources)
    return {name: Draft202012Validator(schema, registry=registry) for name, schema in documents.items()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, action="append", help="Asset root to inspect; repeatable.")
    args = parser.parse_args()
    script_dir = Path(__file__).resolve().parent
    validators = load_validators(script_dir / "unbound")
    roots = tuple(args.root) if args.root else DEFAULT_ROOTS
    failures = 0
    validated = 0
    skipped = 0
    for root in roots:
        if not root.is_dir():
            print(f"FAIL missing root: {root}")
            failures += 1
            continue
        for path in sorted(root.rglob("*.json")):
            kind = document_kind(path, root)
            if kind is None:
                print(f"SKIP {path} (not an Unbound document path)")
                skipped += 1
                continue
            try:
                instance = json.loads(strip_json_comments(path.read_text(encoding="utf-8")))
            except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
                print(f"FAIL {path}: JSON parse: {error}")
                failures += 1
                continue
            errors = sorted(validators[kind].iter_errors(instance), key=lambda item: list(item.absolute_path))
            if errors:
                failures += 1
                for error in errors:
                    location = "/".join(map(str, error.absolute_path)) or "<root>"
                    print(f"FAIL {path} [{kind}] {location}: {error.message}")
            else:
                print(f"PASS {path} [{kind}]")
                validated += 1
    print(f"Summary: {validated} passed, {failures} failed, {skipped} skipped.")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
