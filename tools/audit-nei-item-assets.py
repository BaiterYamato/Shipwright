"""Confere modelos e ícones dos itens da página NEI contra os arquivos montados no OoT."""

import argparse
import re
import sys
import zipfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fork-source", required=True, type=Path)
    parser.add_argument("--models", required=True, type=Path)
    parser.add_argument("--archive", required=True, action="append", type=Path)
    args = parser.parse_args()

    inventory = (args.fork_source / "soh/mods/extended_inventory.c").read_text(encoding="utf-8")
    page = re.search(r"gPage2Items\[24\]\s*=\s*\{([^}]+)\}", inventory, re.S)
    if page is None:
        sys.exit("gPage2Items[24] não encontrado")
    page_entries = re.sub(r"//[^\n]*", "", page.group(1))
    items = [item for item in re.findall(r"ITEM_[A-Z0-9_]+", page_entries) if item != "ITEM_NONE"]
    items.append("ITEM_ROCS_CAPE")

    models_text = args.models.read_text(encoding="utf-8")
    models = dict(re.findall(r'\{\s*(ITEM_[A-Z0-9_]+),\s*"([^"]+)"', models_text))
    icons = dict(re.findall(r"\{\s*(ITEM_[A-Z0-9_]+),\s*\(void\*\)(gItemIcon\w+Tex)", inventory))
    available = set()
    for archive in args.archive:
        with zipfile.ZipFile(archive) as z:
            available.update(z.namelist())
    lantern_icons = re.findall(r"gItemIconLantern\w*Tex", inventory)

    missing = []
    optional = []
    for item in items:
        model = models.get(item)
        icon = icons.get(item)
        absent = []
        if model is None or model not in available:
            absent.append("modelo: " + str(model))
        if icon is not None and "textures/icon_item_custom/" + icon not in available:
            absent.append("ícone: " + icon)
        if item == "ITEM_LANTERN":
            absent.extend("ícone: " + name for name in set(lantern_icons)
                          if "textures/icon_item_custom/" + name not in available)
        if not absent:
            continue
        if item == "ITEM_POKEBALL":
            optional.append((item, absent))  # componente expansion.ssbb, não montado no OoT inicial
        else:
            missing.append((item, absent))

    print(f"itens cadastrados: {len(items)}; completos: {len(items) - len(missing) - len(optional)}; "
          f"opcionais sem assets: {len(optional)}; incompletos: {len(missing)}")
    for item, absent in optional + missing:
        print(item + ": " + ", ".join(absent))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
