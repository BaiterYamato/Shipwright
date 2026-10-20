"""Confere o catálogo público dentro do soh.exe contra o soh.symbols (RFC 0029). Sai com 77 sem soh.symbols."""
import argparse
import hashlib
from pathlib import Path
import struct
import sys


def sections(image):
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    if image[pe:pe + 4] != b"PE\0\0":
        raise SystemExit("não é PE")
    count = struct.unpack_from("<H", image, pe + 6)[0]
    optional = struct.unpack_from("<H", image, pe + 20)[0]
    base = struct.unpack_from("<Q", image, pe + 24 + 24)[0]
    table = pe + 24 + optional
    result = []
    for index in range(count):
        offset = table + index * 40
        size, rva, raw_size, raw = struct.unpack_from("<IIII", image, offset + 8)
        result.append((rva, max(size, raw_size), raw))
    return base, result


def read(image, layout, rva, size):
    for start, length, raw in layout:
        if start <= rva < start + length:
            return image[raw + rva - start:raw + rva - start + size]
    raise SystemExit(f"RVA {rva:#x} fora das seções")


def read_symbols(path):
    """nome -> RVAs, das linhas `rva\tfolded|-\tarquivo\tnome` de symdump.cpp."""
    names = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines()[2:]:
        parts = line.split("\t")
        if len(parts) != 4:
            continue
        names.setdefault(parts[3], []).append(int(parts[0], 16))
    return names


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--symbols", type=Path, required=True)
    args = parser.parse_args()
    if not args.symbols.exists():
        print(f"sem {args.symbols}: rode o alvo soh_symbols")
        return 77
    image = args.exe.read_bytes()
    # Mesmo cabeçalho que o escape hatch confere (symdump.cpp:209-210, OotNativeEscapeWin.cpp:142).
    header = args.symbols.read_text(encoding="utf-8", errors="replace").splitlines()[:2]
    if header != ["linkspan-symbols 1", "sha256 " + hashlib.sha256(image).hexdigest()]:
        print(f"{args.symbols} não é do {args.exe.name}: cabeçalho ou sha256 diferente")
        return 1
    base, layout = sections(image)
    symbols = read_symbols(args.symbols)
    table_rva = symbols["LinkSpan_PublicSymbols"][0]
    count = struct.unpack("<Q", read(image, layout, symbols["LinkSpan_PublicSymbolCount"][0], 8))[0]
    previous, errors, functions = b"", [], 0
    for index in range(count):
        name_va, address_va, kind = struct.unpack("<QQB", read(image, layout, table_rva + index * 24, 17))
        name = read(image, layout, name_va - base, 128).split(b"\0", 1)[0]
        if name <= previous:
            errors.append(f"fora de ordem: {name!r} depois de {previous!r}")
        previous = name
        rvas = symbols.get(name.decode())
        if not rvas:
            errors.append(f"{name.decode()}: ausente do soh.symbols")
        elif address_va - base not in rvas:
            errors.append(f"{name.decode()}: tabela {address_va - base:#x}, símbolos {[hex(r) for r in rvas]}")
        functions += kind == 0
    for error in errors[:50]:
        print(error)
    print(f"{count} entradas ({functions} funções, {count - functions} variáveis); {len(errors)} divergências")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
