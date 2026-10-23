import sys
import zipfile

FILES = {
    "textures/res_probe/a": b"LINKSPAN-RES-A",
    "textures/res_probe/b": b"LINKSPAN-RES-B",
    "objects/gameplay_keep/gPlayerAnim_link_normal_wait": b"LINKSPAN-RES-C",
}

with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_STORED) as archive:
    for name, data in FILES.items():
        archive.writestr(name, data)
