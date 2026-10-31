"""Planta da colisão: rasterizador limitado, sem dependências além da stdlib."""
import json
import math
from .capture import png_rgb


# Fonte bitmap 3x5; coordenadas/ids continuam legíveis sem fontes do sistema/Pillow.
_GLYPHS = dict(zip("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-+.:/=?()_ ", (
    "111101101101111", "010110010010111", "111001111100111", "111001111001111",
    "101101111001001", "111100111001111", "111100111101111", "111001010010010",
    "111101111101111", "111101111001111", "010101111101101", "110101110101110",
    "111100100100111", "110101101101110", "111100110100111", "111100110100100",
    "111100101101111", "101101111101101", "111010010010111", "001001001101111",
    "101101110101101", "100100100100111", "101111111101101", "101111111111101",
    "111101101101111", "111101111100100", "111101101111001", "110101110101101",
    "111100111001111", "111010010010010", "101101101101111", "101101101101010",
    "101101111111101", "101101010101101", "101101010010010", "111001010100111",
    "000000111000000", "000010111010000", "000000000000010", "000010000010000",
    "001001010100100", "000111000111000", "110001010000010", "010100100100010",
    "010001001001010", "000000000000111", "000000000000000")))


def collect_overview(fetch, arguments, max_pages=32):
    """Agrega páginas sem misturar epochs. Nunca repete operação após erro de socket."""
    if type(max_pages) is not int or not 1 <= max_pages <= 64:
        raise ValueError("max_pages precisa estar em 1..64")
    args = dict(arguments)
    merged = None
    first_frame = None
    for page_number in range(max_pages):
        page = fetch(**args)
        if not isinstance(page, dict) or "scene_epoch" not in page or "next_offsets" not in page:
            raise RuntimeError("scene_overview indisponível/incompatível; atualize o agente v2 com esta operação")
        if merged is None:
            merged = dict(page)
            for key in ("polygons", "water_boxes", "transitions", "actors", "rooms"):
                merged[key] = list(page[key])
            first_frame = page["frame"]
        else:
            if page["scene_epoch"] != merged["scene_epoch"] or page["scene"] != merged["scene"]:
                raise RuntimeError("scene_epoch mudou; descarte as páginas e reinicie scene_overview")
            for key in ("polygons", "water_boxes", "transitions", "actors", "rooms"):
                # Atores são ordenados por distância e podem mudar entre callbacks.
                merged[key].extend(page[key])
            merged["invalid_polygons"] += page.get("invalid_polygons", 0)
            merged["actors_truncated"] |= page.get("actors_truncated", False)
        merged["next_offsets"] = page["next_offsets"]
        merged["frame"] = page["frame"]
        merged["pages"] = page_number + 1
        if not page["has_more"]:
            break
        args["scene_epoch"] = page["scene_epoch"]
        # Fixar a faixa da primeira página mesmo se Link subir/descer durante a consulta.
        args["y_min"], args["y_max"] = merged["y_min"], merged["y_max"]
        for key, value in page["next_offsets"].items():
            # Cursors esgotados não podem reiniciar em zero.
            args[key] = 4294967295 if value is None else value
    merged["partial"] = any(v is not None for v in merged["next_offsets"].values())
    merged["first_frame"] = first_frame
    # Room ids repeated by the bounded scene_map page are metadata, not spatial regions.
    merged["rooms"] = list({r["id"]: r for r in merged["rooms"]}.values())
    return merged


def _slice(vertices, ymin, ymax):
    """Sutherland-Hodgman em Y: não projetar andares fora da faixa selecionada."""
    for height, sign in ((ymin, 1), (ymax, -1)):
        result = []
        if not vertices:
            return []
        for previous, current in zip(vertices[-1:] + vertices[:-1], vertices):
            pin = (previous[1] - height) * sign >= 0
            cin = (current[1] - height) * sign >= 0
            if pin != cin:
                t = (height - previous[1]) / (current[1] - previous[1])
                result.append([previous[k] + t * (current[k] - previous[k]) for k in range(3)])
            if cin:
                result.append(current)
        vertices = result
    return vertices


def render_overview(scene, size=960):
    if type(size) is not int or not 384 <= size <= 1280:
        raise ValueError("size precisa estar em 384..1280 pixels")
    bbox = scene["bbox"]
    if (len(bbox) != 4 or not all(math.isfinite(v) and abs(v) <= 10000000 for v in bbox)
            or bbox[0] > bbox[2] or bbox[1] > bbox[3]):
        raise ValueError("bbox inválida no retrato de colisão")
    ymin, ymax = scene["y_min"], scene["y_max"]
    left, top, extent = 64, 72, size - 112
    scale = extent / max(bbox[2] - bbox[0], bbox[3] - bbox[1], 1)
    xpad = (extent - (bbox[2] - bbox[0]) * scale) / 2
    zpad = (extent - (bbox[3] - bbox[1]) * scale) / 2
    clip_l, clip_r = math.ceil(left + xpad), math.floor(left + xpad + (bbox[2] - bbox[0]) * scale)
    clip_t, clip_b = math.ceil(top + zpad), math.floor(top + zpad + (bbox[3] - bbox[1]) * scale)
    pixels = bytearray(bytes((246, 243, 234)) * size * size)
    depth = [-math.inf] * (size * size)
    def project(x, z):
        return left + xpad + (x - bbox[0]) * scale, top + zpad + (z - bbox[1]) * scale
    def pixel(x, y, color):
        if 0 <= x < size and 0 <= y < size:
            pos = (y * size + x) * 3
            pixels[pos:pos + 3] = bytes(color)
    def text(x, y, message, color=(35, 38, 43), zoom=2):
        for char in str(message).upper():
            glyph = _GLYPHS.get(char, _GLYPHS["?"])
            for k, value in enumerate(glyph):
                if value == "1":
                    for dy in range(zoom):
                        for dx in range(zoom):
                            pixel(int(x) + k % 3 * zoom + dx, int(y) + k // 3 * zoom + dy, color)
            x += 4 * zoom
    def line(a, b, color, width=1):
        # Clip projected lines before stepping, even when actors are far outside the bbox.
        dx, dy = b[0] - a[0], b[1] - a[1]
        lo, hi = 0., 1.
        for p, q in ((-dx, a[0] - clip_l), (dx, clip_r - a[0]),
                     (-dy, a[1] - clip_t), (dy, clip_b - a[1])):
            if p == 0:
                if q < 0:
                    return
            elif p < 0:
                lo = max(lo, q / p)
            else:
                hi = min(hi, q / p)
        if lo > hi:
            return
        start = (a[0] + lo * dx, a[1] + lo * dy)
        end = (a[0] + hi * dx, a[1] + hi * dy)
        steps = max(1, math.ceil(max(abs(end[0] - start[0]), abs(end[1] - start[1]))))
        for step in range(steps + 1):
            x = round(start[0] + (end[0] - start[0]) * step / steps)
            y = round(start[1] + (end[1] - start[1]) * step / steps)
            for yy in range(y - width // 2, y + width // 2 + 1):
                for xx in range(x - width // 2, x + width // 2 + 1):
                    pixel(xx, yy, color)
    work = 0
    def fill(vertices, water=False):
        nonlocal work
        for i in range(1, len(vertices) - 1):
            tri = [vertices[0], vertices[i], vertices[i + 1]]
            pts = [(*project(v[0], v[2]), v[1]) for v in tri]
            low = max(clip_t, math.ceil(min(p[1] for p in pts)))
            high = min(clip_b, math.floor(max(p[1] for p in pts)))
            for y in range(low, high + 1):
                crossings = []
                for a, b in zip(pts, pts[1:] + pts[:1]):
                    if a[1] == b[1]:
                        continue
                    t = (y - a[1]) / (b[1] - a[1])
                    if 0 <= t <= 1:
                        crossings.append((a[0] + t * (b[0] - a[0]), a[2] + t * (b[2] - a[2])))
                if len(crossings) < 2:
                    continue
                a, b = min(crossings), max(crossings)
                start, end = max(clip_l, math.ceil(a[0])), min(clip_r, math.floor(b[0]))
                work += max(0, end - start + 1)
                if work > 12000000:
                    raise ValueError("render excedeu 12 milhões de pixels de trabalho; reduza bbox/max_pages ou aumente stride")
                for x in range(start, end + 1):
                    h = a[1] + (b[1] - a[1]) * ((x - a[0]) / max(b[0] - a[0], 1e-9))
                    index = y * size + x
                    if h >= depth[index] - 0.01:
                        depth[index] = h
                        grey = round(85 + 140 * max(0, min(1, (h - ymin) / max(ymax - ymin, 1))))
                        pixel(x, y, (65, 149, 218) if water else (grey, grey, grey))
    polygons = [(p, _slice(p["vertices"], ymin, ymax)) for p in scene["polygons"]]
    for poly, vertices in polygons:
        if poly["kind"] == "floor":
            fill(vertices)
    for water in scene["water_boxes"]:
        h = water["y"]
        if ymin <= h <= ymax:
            fill([[water["x_min"], h, water["z_min"]], [water["x_max"], h, water["z_min"]],
                  [water["x_max"], h, water["z_max"]], [water["x_min"], h, water["z_max"]]], water=True)
    for poly, vertices in polygons:
        if poly["kind"] in ("wall", "ceiling"):
            for a, b in zip(vertices, vertices[1:] + vertices[:1]):
                line(project(a[0], a[2]), project(b[0], b[2]),
                     (24, 28, 33) if poly["kind"] == "wall" else (157, 128, 176), 3 if poly["kind"] == "wall" else 1)
    # Grid in world coordinates, X right, +Z down. At most ~10 ticks per axis.
    span = max(bbox[2] - bbox[0], bbox[3] - bbox[1], 1)
    unit = 10 ** math.floor(math.log10(span / 6))
    step = next(n * unit for n in (1, 2, 5, 10) if n * unit >= span / 6)
    for axis, start, end in ((0, bbox[0], bbox[2]), (1, bbox[1], bbox[3])):
        for i in range(math.ceil(start / step), math.floor(end / step) + 1):
            value = i * step
            a = project(value, bbox[1]) if axis == 0 else project(bbox[0], value)
            b = project(value, bbox[3]) if axis == 0 else project(bbox[2], value)
            # Sparse dots preserve the geometry underneath.
            for t in range(0, int(max(abs(b[0] - a[0]), abs(b[1] - a[1]))) + 1, 8):
                pixel(round(a[0] + (0 if axis == 0 else t)), round(a[1] + (t if axis == 0 else 0)), (113, 123, 129))
            text(a[0] - 10 if axis == 0 else 3, size - 28 if axis == 0 else a[1], f"{value:g}", zoom=1)
    def visible(pos):
        return (bbox[0] <= pos["x"] <= bbox[2] and bbox[1] <= pos["z"] <= bbox[3]
                and ymin <= pos["y"] <= ymax)
    magenta = (180, 42, 139)
    for transition in scene["transitions"]:
        p = transition["position"]
        if not visible(p):
            continue
        x, z = project(p["x"], p["z"])
        cross = transition["crossing"]
        nx, ny, nz = cross["normal"]
        if abs(ny) > .9:
            line((x - 5, z), (x + 5, z), magenta, 3)
            line((x, z - 5), (x, z + 5), magenta, 3)
            text(x + 8, z - 12, f"Y +R{cross.get('positive_room')} -R{cross.get('negative_room')}", magenta, 1)
        else:
            line((x - nz * 20, z + nx * 20), (x + nz * 20, z - nx * 20), magenta, 3)
            line((x, z), (x + nx * 14, z + nz * 14), magenta)
            # Unknown semantics use front/back explicitly, never invent a sign-to-room mapping.
            known = cross.get("negative_room") is not None and cross.get("positive_room") is not None
            text(x + nx * 24 + 3, z + nz * 24, f"+R{cross['positive_room']}" if known else f"F{transition['front_room']}", magenta, 1)
            text(x - nx * 24 + 3, z - nz * 24, f"-R{cross['negative_room']}" if known else f"B{transition['back_room']}", magenta, 1)
        text(x + 5, z + 7, f"T{transition['index']}", magenta, 1)
    for actor in scene["actors"]:
        p = actor["position"]
        if visible(p):
            x, z = project(p["x"], p["z"])
            line((x - 2, z), (x + 2, z), (204, 105, 21), 3)
            text(x + 5, z - 6, f"{actor['id']:X}R{actor['room']}", (133, 65, 10), 1)
    link = scene["link"]
    if visible(link["position"]):
        x, z = project(link["position"]["x"], link["position"]["z"])
        angle = math.radians(link["yaw"])
        dx, dz = math.sin(angle), math.cos(angle)
        tip = (x + dx * 17, z + dz * 17)
        line((x - dx * 8, z - dz * 8), tip, (13, 123, 53), 3)
        for sign in (-1, 1):
            line(tip, (x + dx * 6 + sign * dz * 7, z + dz * 6 - sign * dx * 7), (13, 123, 53), 3)
        text(x + 12, z - 15, "LINK", (13, 123, 53), 1)
    text(16, 12, f"CENA {scene['scene']} SALA {scene['room']} EPOCH {scene['scene_epoch']}")
    text(16, 29, f"Y {ymin:g}..{ymax:g} CINZA=ALTURA AGUA=AZUL PAREDE=PRETO", zoom=1)
    text(16, 41, "LINK=VERDE TRANSICAO=ROXO ATOR=LARANJA +X DIREITA +Z ABAIXO", zoom=1)
    text(16, 53, f"GRADE {step:g} UNIDADES  {'PARCIAL' if scene.get('partial') else 'ESTATICA'}  DYNAPOLY OMITIDO", zoom=1)
    line((left, top + extent), (left + min(extent / 3, step * scale), top + extent), (20, 25, 30), 3)
    text(left, top + extent + 7, f"ESCALA {min(extent / 3, step * scale) / scale:g} UNIDADES", zoom=1)
    return png_rgb(size, size, pixels)


def overview_summary(scene):
    kinds = {kind: sum(p["kind"] == kind for p in scene["polygons"]) for kind in ("floor", "wall", "ceiling")}
    return json.dumps({"scene": scene["scene"], "scene_epoch": scene["scene_epoch"], "bbox": scene["bbox"],
        "y_min": scene["y_min"], "y_max": scene["y_max"], "polygons_rendered": kinds,
        "source_counts": scene["counts"], "water_boxes": len(scene["water_boxes"]),
        "transitions": len(scene["transitions"]), "actors": len(scene["actors"]),
        "room_count": scene["room_count"], "room_ids": [r["id"] for r in scene["rooms"]],
        "located_rooms": sorted({r for t in scene["transitions"] for r in (t["front_room"], t["back_room"]) if r >= 0}),
        "pages": scene["pages"], "partial": scene["partial"], "stride": scene["stride"],
        "next_offsets": scene["next_offsets"], "frames": [scene["first_frame"], scene["frame"]],
        "invalid_polygons": scene["invalid_polygons"], "actors_truncated": scene["actors_truncated"],
        "warnings": scene["warnings"] + ["Camadas sobrepostas: o piso/água mais alto prevalece; consulte faixas Y menores.",
            "Página parcial/stride>1 pode omitir chão e paredes; não comprova rota transitável.",
            "Link/atores/transições podem ter mudado entre páginas; posições de Link são da primeira página."]}, ensure_ascii=False)
