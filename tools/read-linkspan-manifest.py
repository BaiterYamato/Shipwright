#!/usr/bin/env python3
"""Le o manifest.toml de um pacote Link-Span e imprime em JSON os campos que o construtor da release confere.

Uso: read-linkspan-manifest.py <manifest.toml>
     read-linkspan-manifest.py --check   (sai com 0 se ha leitor de TOML)

Regex sobre o TOML aprovava hash citado em comentario e ignorava chave entre aspas; aqui o arquivo passa pelo
tomllib e provider.host_fingerprints segue as mesmas regras do ManifestParser do core (1-32 SHA-256 em hex).
"""
import json
import string
import sys

try:
    import tomllib
except ImportError:  # Python < 3.11
    try:
        import tomli as tomllib
    except ImportError:
        tomllib = None

LAYOUT_KEYS = ('oot_layout_id', 'layout_id')


def fail(message):
    print(message, file=sys.stderr)
    return 2


def find_layout(data):
    for table in [data] + [value for value in data.values() if isinstance(value, dict)]:
        for key in LAYOUT_KEYS:
            if isinstance(table.get(key), str) and table[key]:
                return table[key]
    return None


def main(argv):
    if tomllib is None:
        return fail('sem leitor de TOML: use Python 3.11+ (tomllib) ou instale o pacote tomli')
    if argv[1:] == ['--check']:
        return 0
    if len(argv) != 2:
        return fail('uso: read-linkspan-manifest.py <manifest.toml>')
    try:
        with open(argv[1], 'rb') as handle:
            data = tomllib.load(handle)
    except (OSError, tomllib.TOMLDecodeError) as error:
        return fail(f'manifest.toml ilegivel: {error}')

    provider = data.get('provider', {})
    if not isinstance(provider, dict):
        return fail('provider deve ser uma tabela')
    fingerprints = []
    if 'host_fingerprints' in provider:
        values = provider['host_fingerprints']
        if not isinstance(values, list) or not 1 <= len(values) <= 32:
            return fail('provider.host_fingerprints deve ser uma lista de 1-32 SHA-256')
        for value in values:
            if not isinstance(value, str) or len(value) != 64 or any(c not in string.hexdigits for c in value):
                return fail(f'provider.host_fingerprints tem entrada que nao e SHA-256: {value!r}')
            fingerprints.append(value.lower())

    result = {
        'id': data.get('id') if isinstance(data.get('id'), str) else None,
        'version': data.get('version') if isinstance(data.get('version'), str) else None,
        'hostFingerprints': fingerprints,
        'layoutId': find_layout(data),
    }
    json.dump(result, sys.stdout)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
