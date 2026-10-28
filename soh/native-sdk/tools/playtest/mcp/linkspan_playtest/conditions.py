"""Gramática pequena de comparações; não executa código recebido."""
import json
import math
import operator
import re
import time

FIELDS = frozenset(("frame", "game_state", "scene", "room", "entrance", "age", "health", "max_health",
                    "magic", "rupees", "pause", "textbox", "queue_size", "sail",
                    "position.x", "position.y", "position.z", "rotation.x", "rotation.y", "rotation.z"))
CLAUSE = re.compile(r'\s*([a-z_][a-z_0-9.]*)\s*(==|!=|<=|>=|<|>)\s*'
                    r'("(?:[^"\\\x00-\x1f]|\\["\\/bfnrt]|\\u[0-9a-fA-F]{4})*"|true|false|null|-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?)')
OPS = {"==": operator.eq, "!=": operator.ne, "<": operator.lt, "<=": operator.le, ">": operator.gt, ">=": operator.ge}


def parse(text):
    if not isinstance(text, str) or not text.strip() or len(text) > 4096:
        raise ValueError("condição vazia ou longa demais")
    clauses, pos = [], 0
    while True:
        match = CLAUSE.match(text, pos)
        if match is None or match[1] not in FIELDS:
            raise ValueError("condição inválida ou campo desconhecido")
        value = json.loads(match[3])
        if isinstance(value, float) and not math.isfinite(value):
            raise ValueError("número fora da faixa")
        clauses.append((match[1].split("."), match[2], value))
        pos = match.end()
        if not text[pos:].strip():
            break
        separator = re.match(r"\s+and\s+", text[pos:])
        if separator is None:
            raise ValueError("somente comparações unidas por and são aceitas")
        pos += separator.end()

    def condition(snapshot):
        for path, op, expected in clauses:
            actual = snapshot
            for key in path:
                if not isinstance(actual, dict) or key not in actual:
                    return False
                actual = actual[key]
            if actual is None or expected is None:
                if op not in ("==", "!=") or not OPS[op](actual, expected):
                    return False
                continue
            numeric = type(actual) in (int, float) and type(expected) in (int, float)
            if not numeric and type(actual) is not type(expected):
                return False
            if op not in ("==", "!=") and (type(actual) is bool or not numeric):
                return False
            if not OPS[op](actual, expected):
                return False
        return True
    return condition


def wait_for(client, condition, timeout=30.0, interval=0.02):
    if type(timeout) not in (int, float) or not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("timeout precisa ser um número positivo e finito")
    predicate = parse(condition)
    deadline = time.monotonic() + timeout
    while True:
        snapshot = client.state()
        if predicate(snapshot):
            return snapshot
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(f"condição não atingida: {condition}")
        time.sleep(min(interval, remaining))
