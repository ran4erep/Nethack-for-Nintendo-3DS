#!/usr/bin/env python3
"""Turn a glTF/GLB model into the compact mesh used by the 3DS banner.

The HOME Menu CGFX renderer has two hard constraints:

* the whole CGFX must stay below 512 KiB, and
* every per-attribute vertex stream should stay below 64 KiB (the reference
  banners use small submeshes, and oversized streams can make the real HOME
  Menu stutter or fail to draw the mesh).

The source model (knight.glb) is a gltfpack export with quantised attributes,
a node scale of 5.8e-5, and three 2048x2048 textures.  Its UV map cannot be
repainted: unrelated parts of the mesh share the same texels, so a colour
painted for the ground also lands on the armour.  This script:

* bakes the node transforms into the vertices,
* bakes KHR_texture_transform into TEXCOORD_0 (pycgfx does not understand it),
* drops the metallic-roughness and normal maps,
* splits the mesh into detail regions (armour steel and shadowed steel, sword
  blade and brass crossguard, leather boots with mud, cloak outside and lining,
  near-black crevices, stone, earth and grass) and four baked brightness steps
  per region.  All of that colour lives in one palette texture, and the whole
  banner uses a single material: a dozen materials made the real HOME Menu hang,
  so the palette carries the colours instead of the material list,
* generates its own UVs (each vertex is projected along the axis it faces most
  and lands in the palette tile its triangle chose);
  --no-paint uses the photographic stone texture with the model's own UVs
  instead,
* centres the model at X=0/Z=0 with a chosen height (optionally turning it
  around its vertical axis so its front faces the banner camera),
* splits every region into submeshes that fit the 64 KiB vertex-stream budget,
  and
* hangs every submesh from a single root node named ``COMMON`` so the CGFX
  skeleton is ``Scene root -> COMMON -> meshes``, like the banners that are
  known to work on real hardware.

Output is a plain glTF 2.0 GLB with float attributes, which pycgfx can read
without any of its quantisation assumptions.
"""

from __future__ import annotations

import argparse
import base64
import io
import json
import math
import struct
from pathlib import Path

import numpy as np
from PIL import Image

GLB_MAGIC = 0x46546C67
JSON_CHUNK = 0x4E4F534A
BIN_CHUNK = 0x004E4942

COMPONENT_DTYPES = {
    5120: np.int8,
    5121: np.uint8,
    5122: np.int16,
    5123: np.uint16,
    5125: np.uint32,
    5126: np.float32,
}
COMPONENT_MAX = {
    5120: 127.0,
    5121: 255.0,
    5122: 32767.0,
    5123: 65535.0,
    5125: 4294967295.0,
    5126: 1.0,
}
TYPE_COMPONENTS = {
    "SCALAR": 1,
    "VEC2": 2,
    "VEC3": 3,
    "VEC4": 4,
    "MAT2": 4,
    "MAT3": 9,
    "MAT4": 16,
}


def load_glb(path: Path):
    data = path.read_bytes()
    if len(data) < 12:
        raise ValueError(f"{path}: file is too short to be a GLB")
    magic, version, length = struct.unpack_from("<III", data, 0)
    if magic != GLB_MAGIC:
        raise ValueError(f"{path}: not a binary glTF file")
    if version != 2:
        raise ValueError(f"{path}: unsupported GLB version {version}")
    length = min(length, len(data))

    json_data = None
    bin_data = b""
    offset = 12
    while offset + 8 <= length:
        chunk_len, chunk_type = struct.unpack_from("<II", data, offset)
        offset += 8
        chunk = data[offset : offset + chunk_len]
        offset += chunk_len
        if chunk_type == JSON_CHUNK:
            json_data = chunk
        elif chunk_type == BIN_CHUNK:
            bin_data = chunk
    if json_data is None:
        raise ValueError(f"{path}: missing JSON chunk")
    doc = json.loads(json_data.rstrip(b" \0").decode("utf-8"))
    return doc, bin_data


def write_glb(doc, bin_data: bytes, path: Path):
    js = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    js += b" " * (-len(js) % 4)
    bin_data += b"\0" * (-len(bin_data) % 4)
    total = 12 + 8 + len(js) + 8 + len(bin_data)
    out = bytearray()
    out += struct.pack("<III", GLB_MAGIC, 2, total)
    out += struct.pack("<II", len(js), JSON_CHUNK) + js
    out += struct.pack("<II", len(bin_data), BIN_CHUNK) + bin_data
    path.write_bytes(out)
    return total


def accessor_array(doc, bin_data: bytes, index: int) -> np.ndarray:
    acc = doc["accessors"][index]
    if "bufferView" not in acc:
        raise ValueError(f"accessor {index}: sparse accessors are not supported")
    view = doc["bufferViews"][acc["bufferView"]]
    component = acc["componentType"]
    if component not in COMPONENT_DTYPES:
        raise ValueError(f"accessor {index}: unsupported componentType {component}")
    dtype = np.dtype(COMPONENT_DTYPES[component]).newbyteorder("<")
    ncomp = TYPE_COMPONENTS[acc["type"]]
    item_size = dtype.itemsize
    stride = view.get("byteStride", item_size * ncomp)
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    end = start + stride * (acc["count"] - 1) + item_size * ncomp
    if end > len(bin_data):
        raise ValueError(f"accessor {index}: data extends past the BIN chunk")
    raw = bin_data[start:end]
    arr = np.ndarray(
        shape=(acc["count"], ncomp),
        dtype=dtype,
        buffer=raw,
        offset=0,
        strides=(stride, item_size),
    ).copy()
    return arr


def as_float(arr: np.ndarray, component: int, normalized: bool) -> np.ndarray:
    if component == 5126:
        return arr.astype(np.float64)
    values = arr.astype(np.float64)
    if normalized:
        values /= COMPONENT_MAX[component]
        if component in (5120, 5122):
            values = np.maximum(values, -1.0)
    return values


def node_matrix(node: dict) -> np.ndarray:
    if "matrix" in node:
        return np.array(node["matrix"], dtype=np.float64).reshape(4, 4).T
    tx, ty, tz = node.get("translation", (0.0, 0.0, 0.0))
    qx, qy, qz, qw = node.get("rotation", (0.0, 0.0, 0.0, 1.0))
    sx, sy, sz = node.get("scale", (1.0, 1.0, 1.0))
    xx, yy, zz = qx * qx, qy * qy, qz * qz
    xy, xz, yz = qx * qy, qx * qz, qy * qz
    wx, wy, wz = qw * qx, qw * qy, qw * qz
    rot = np.array(
        [
            [1 - 2 * (yy + zz), 2 * (xy - wz), 2 * (xz + wy)],
            [2 * (xy + wz), 1 - 2 * (xx + zz), 2 * (yz - wx)],
            [2 * (xz - wy), 2 * (yz + wx), 1 - 2 * (xx + yy)],
        ],
        dtype=np.float64,
    )
    mat = np.eye(4)
    mat[:3, :3] = rot * np.array([sx, sy, sz])
    mat[:3, 3] = (tx, ty, tz)
    return mat


def walk_nodes(doc, node_id: int, parent: np.ndarray):
    node = doc["nodes"][node_id]
    world = parent @ node_matrix(node)
    if "mesh" in node:
        yield node["mesh"], world
    for child in node.get("children", ()):
        yield from walk_nodes(doc, child, world)


def image_bytes(doc, bin_data: bytes, image_index: int, source_dir: Path) -> bytes:
    image = doc["images"][image_index]
    if "bufferView" in image:
        view = doc["bufferViews"][image["bufferView"]]
        start = view.get("byteOffset", 0)
        return bin_data[start : start + view["byteLength"]]
    uri = image.get("uri")
    if not uri:
        raise ValueError(f"image {image_index}: no bufferView or uri")
    if uri.startswith("data:"):
        _, payload = uri.split(",", 1)
        return base64.b64decode(payload)
    return (source_dir / uri).read_bytes()


def uv_transform(doc, material: dict) -> dict:
    base = (material.get("pbrMetallicRoughness") or {}).get("baseColorTexture")
    if not base:
        return {}
    return (base.get("extensions") or {}).get("KHR_texture_transform") or {}


def bake_uv(uv: np.ndarray, transform: dict) -> np.ndarray:
    if not transform:
        return uv
    offset = transform.get("offset", (0.0, 0.0))
    scale = transform.get("scale", (1.0, 1.0))
    rotation = float(transform.get("rotation", 0.0))
    u = uv[:, 0] * scale[0]
    v = uv[:, 1] * scale[1]
    if rotation:
        c, s = math.cos(rotation), math.sin(rotation)
        u, v = c * u - s * v, s * u + c * v
    uv = np.stack([u + offset[0], v + offset[1]], axis=1)
    # The transformed range is exactly 0..1 for this model; clamping removes
    # float rounding at the borders.  The sampler uses CLAMP_TO_EDGE.
    return np.clip(uv, 0.0, 1.0)


def collect_geometry(doc, bin_data: bytes):
    scene_index = doc.get("scene", 0)
    roots = doc["scenes"][scene_index].get("nodes", ())
    positions = []
    normals = []
    uvs = []
    indices = []
    materials = []

    for root in roots:
        for mesh_index, world in walk_nodes(doc, root, np.eye(4)):
            mesh = doc["meshes"][mesh_index]
            linear = world[:3, :3]
            normal_matrix = np.linalg.inv(linear).T
            for primitive in mesh.get("primitives", ()):
                if primitive.get("mode", 4) != 4:
                    raise ValueError("only triangle primitives are supported")
                attrs = primitive["attributes"]
                if "POSITION" not in attrs:
                    raise ValueError("primitive has no POSITION attribute")
                pos_acc = doc["accessors"][attrs["POSITION"]]
                pos = as_float(
                    accessor_array(doc, bin_data, attrs["POSITION"]),
                    pos_acc["componentType"],
                    pos_acc.get("normalized", False),
                )
                pos_h = np.concatenate([pos, np.ones((len(pos), 1))], axis=1)
                pos = (world @ pos_h.T).T[:, :3]

                if "NORMAL" in attrs:
                    n_acc = doc["accessors"][attrs["NORMAL"]]
                    nrm = as_float(
                        accessor_array(doc, bin_data, attrs["NORMAL"]),
                        n_acc["componentType"],
                        n_acc.get("normalized", False),
                    )
                    nrm = (normal_matrix @ nrm.T).T
                    length = np.linalg.norm(nrm, axis=1, keepdims=True)
                    length[length == 0] = 1.0
                    nrm = nrm / length
                else:
                    nrm = np.zeros_like(pos)

                if "TEXCOORD_0" in attrs:
                    t_acc = doc["accessors"][attrs["TEXCOORD_0"]]
                    uv = as_float(
                        accessor_array(doc, bin_data, attrs["TEXCOORD_0"]),
                        t_acc["componentType"],
                        t_acc.get("normalized", False),
                    )
                else:
                    uv = np.zeros((len(pos), 2), dtype=np.float64)
                if primitive.get("material") is not None:
                    material = doc["materials"][primitive["material"]]
                    uv = bake_uv(uv, uv_transform(doc, material))
                    materials.append(primitive["material"])
                else:
                    materials.append(None)

                if "indices" in primitive:
                    idx = accessor_array(doc, bin_data, primitive["indices"]).ravel()
                    idx = idx.astype(np.uint32)
                else:
                    idx = np.arange(len(pos), dtype=np.uint32)
                if len(idx) % 3:
                    raise ValueError("primitive index count is not a multiple of 3")

                base = sum(len(p) for p in positions)
                positions.append(pos)
                normals.append(nrm)
                uvs.append(uv)
                indices.append(idx.reshape(-1, 3) + base)

    if not positions:
        raise ValueError("the scene contains no triangle meshes")
    return (
        np.concatenate(positions, axis=0),
        np.concatenate(normals, axis=0),
        np.concatenate(uvs, axis=0),
        np.concatenate(indices, axis=0),
        materials,
    )


def _weld_and_components(pos: np.ndarray, indices: np.ndarray):
    """Weld vertices by position and return the component id of every triangle."""
    key = np.round(pos * 200.0).astype(np.int64)
    _, inverse = np.unique(key, axis=0, return_inverse=True)
    parent = np.arange(len(inverse))

    def find(a: int) -> int:
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for tri in indices:
        for i, j in ((0, 1), (0, 2)):
            ra, rb = find(int(inverse[tri[i]])), find(int(inverse[tri[j]]))
            if ra != rb:
                parent[rb] = ra
    roots = np.array([find(i) for i in range(len(inverse))])
    return roots[inverse[indices[:, 0]]], roots


# The banner is drawn with a single material on purpose.  A CGFX with a dozen
# materials needs a dozen MTOB/shader programs, and the Home Menu's banner
# player hangs on that (which is exactly what happened with the 12-material
# version).  All of the colour therefore lives in one palette texture: a grid of
# "region x brightness" tiles, and every triangle points its UVs at the tile it
# wants.
#
# Regions are (name, base colour, pattern).  The colours are deliberately
# darker than they should look on screen: the Home Menu lights with
# ambient = diffuse = 1, so a face turned towards the light is drawn at roughly
# twice its base colour.  Steel at 0.72 was therefore clipped to pure white on
# every plate, which is exactly why the knight read as one flat colour.
REGIONS = [
    ("ArmorSteel", (0.46, 0.49, 0.54), "metal"),
    ("ArmorShadow", (0.34, 0.36, 0.41), "metal"),
    ("SwordBlade", (0.56, 0.60, 0.66), "blade"),
    ("SwordBrass", (0.52, 0.39, 0.14), "brass"),
    ("Leather", (0.24, 0.17, 0.12), "leather"),
    ("BootMud", (0.28, 0.21, 0.14), "soil"),
    ("CloakOuter", (0.16, 0.20, 0.34), "cloth"),
    ("CloakInner", (0.08, 0.09, 0.15), "cloth"),
    ("Crevice", (0.03, 0.03, 0.04), "cloth"),
    ("BaseStone", (0.42, 0.37, 0.31), "stone"),
    ("BaseEarth", (0.30, 0.23, 0.16), "soil"),
    ("BaseGrass", (0.19, 0.30, 0.13), "grass"),
]

REGION_INDEX = {name: i for i, (name, _, _) in enumerate(REGIONS)}

# Four brightness steps baked into the palette.  The Home Menu's light is flat
# and view dependent, so a little baked shading is what makes the armour read
# as separate plates rather than one continuous surface.
SHADES = (0.55, 0.70, 0.85, 1.00)
SHADE_LEVELS = len(SHADES)

# Submeshes are built per "group" of regions: with one material there is no
# reason to spend a node on every region, and a handful of submeshes is what the
# banners that work on hardware use.  Groups are indices into REGIONS.
REGION_GROUPS = (
    (0, 1, 2, 3),     # armour steel, shadowed steel, blade, brass
    (4, 5),           # leather boots and the mud on them
    (6, 7, 8),        # cloak, lining, crevices
    (9, 10),          # stone and earth mound
    (11,),            # grass
)

TILE_GRID = 8         # palette texture is an 8x8 grid of tiles
TILE_SIZE = 128       # RGBA4: 32 KiB

_REGION_TO_GROUP = np.zeros(len(REGIONS), dtype=np.int32)
for _group, _group_regions in enumerate(REGION_GROUPS):
    for _region in _group_regions:
        _REGION_TO_GROUP[_region] = _group


def group_of_region(region: np.ndarray) -> np.ndarray:
    """Submesh group index for every triangle, given its palette region."""
    return _REGION_TO_GROUP[region]


def _tile_noise(cell: int, freq: int, seed: int) -> np.ndarray:
    """Seamless noise tile: a small random grid repeated and smoothed."""
    rng = np.random.default_rng(seed)
    freq = max(2, min(freq, cell))
    small = rng.random((freq, freq)).astype(np.float32)
    reps = int(np.ceil(cell / freq))
    big = np.tile(small, (reps, reps))[:cell, :cell]
    im = Image.fromarray((big * 255).astype(np.uint8))
    im = im.resize((cell, cell), Image.BICUBIC)
    a = np.asarray(im).astype(np.float32) / 255.0
    return (a - a.min()) / max(float(a.max() - a.min()), 1e-6)


def _tile_streaks(cell: int, seed: int) -> np.ndarray:
    """Brushed-metal streaks: noisy rows smeared horizontally."""
    rng = np.random.default_rng(seed)
    rows = rng.random(cell).astype(np.float32)
    rows = np.repeat(rows[:, None], cell, axis=1)
    grain = _tile_noise(cell, max(2, cell // 2), seed + 1)
    a = 0.65 * rows + 0.35 * grain
    return (a - a.min()) / max(float(a.max() - a.min()), 1e-6)


def _region_pattern(kind: str, cell: int, seed: int) -> np.ndarray:
    if kind in ("metal", "blade"):
        return _tile_streaks(cell, seed)
    if kind == "brass":
        return _tile_noise(cell, max(2, cell // 2), seed)
    if kind == "leather":
        return _tile_noise(cell, max(2, cell // 2), seed + 7)
    if kind == "cloth":
        return _tile_noise(cell, max(2, cell // 3), seed + 11)
    if kind in ("soil", "stone"):
        return _tile_noise(cell, max(2, cell // 3), seed + 13)
    return _tile_noise(cell, max(2, cell // 4), seed + 17)


def palette_texture(size: int = TILE_SIZE, grid: int = TILE_GRID) -> Image.Image:
    """Region x brightness palette, one tile per entry.

    The texture carries the colour of every part of the model, plus a neutral
    pattern (brushed metal, leather, cloth, soil, stone, grass).  Because each
    tile is its own little picture, a part can never bleed onto another even
    though the model's original UV map is unusable.
    """
    cell = size // grid
    rgb = np.zeros((size, size, 3), dtype=np.float32)
    for region_index, (name, colour, kind) in enumerate(REGIONS):
        base = np.array(colour, dtype=np.float32)
        for level, shade in enumerate(SHADES):
            tile = region_index * SHADE_LEVELS + level
            ty, tx = divmod(tile, grid)
            pattern = _region_pattern(kind, cell, 31 + region_index * 17 + level)
            modulation = 0.85 + 0.15 * pattern
            value = base[None, None, :] * shade * modulation[:, :, None]
            rgb[ty * cell:(ty + 1) * cell, tx * cell:(tx + 1) * cell] = value
    pixels = np.clip(rgb * 255.0 + 0.5, 0, 255).astype(np.uint8)
    return Image.fromarray(pixels)


def triplanar_uv(pos: np.ndarray, nrm: np.ndarray, tile: int) -> np.ndarray:
    """UVs into one palette tile, projected along each vertex's main axis.

    The model's own UV layout cannot be used: unrelated parts share the same
    texels, so nothing can be painted per part.  The banner generates UVs from
    the vertex positions instead.  Every vertex is projected onto the plane it
    faces most, which keeps a pattern continuous over a surface without any
    unwrapping; overlaps between parts are harmless because each part samples
    its own tile.
    """
    axis = np.argmax(np.abs(nrm), axis=1)
    u = np.where(axis == 1, pos[:, 0], np.where(axis == 2, pos[:, 0], pos[:, 2]))
    v = np.where(axis == 1, pos[:, 2], pos[:, 1])
    u = u / 2.6
    v = v / 2.6
    u -= np.floor(u)
    v -= np.floor(v)
    margin = 0.5 / TILE_SIZE
    span = 1.0 / TILE_GRID - 2.0 * margin
    tx, ty = tile % TILE_GRID, tile // TILE_GRID
    au = tx / TILE_GRID + margin + u * span
    # V grows downwards in the stored texture, exactly like the palette PNG, so
    # row ty is addressed from the top of V.
    av = ty / TILE_GRID + margin + v * span
    return np.stack([au, av], axis=1)


def _shadow_distance(tris: np.ndarray, face: np.ndarray, chunk: int = 128,
                     max_dist: float = 0.55) -> np.ndarray:
    """Shortest ray each triangle can cast along its normal before it hits something.

    This finds the tight recesses of the model (the helmet's visor slit, the
    gaps between plates, the space under the cloak) without any lighting
    information, so the darkening stays correct while the banner spins.
    """
    v0 = tris[:, 0]
    e1 = tris[:, 1] - v0
    e2 = tris[:, 2] - v0
    origin = tris.mean(axis=1) + face * 0.015
    n = len(tris)
    out = np.full(n, max_dist, dtype=np.float64)
    for start in range(0, n, chunk):
        stop = min(start + chunk, n)
        o = origin[start:stop][:, None, :]
        d = face[start:stop][:, None, :]
        pvec = np.cross(d, e2[None, :, :])
        det = np.einsum("ijk,jk->ij", pvec, e1)
        safe = np.abs(det) > 1e-9
        inv = np.where(safe, 1.0 / np.where(safe, det, 1.0), 0.0)
        tvec = o - v0[None, :, :]
        u = np.einsum("ijk,ijk->ij", tvec, pvec) * inv
        qvec = np.cross(tvec, e1[None, :, :])
        v = np.einsum("ijk,ijk->ij", d, qvec) * inv
        t = np.einsum("ijk,jk->ij", qvec, e2) * inv
        hit = (
            safe
            & (u >= 0.0)
            & (v >= 0.0)
            & (u + v <= 1.0)
            & (t > 0.01)
            & (t < max_dist)
        )
        rows = np.arange(stop - start)
        hit[rows, start + rows] = False
        t = np.where(hit, t, np.inf)
        out[start:stop] = np.minimum(out[start:stop], t.min(axis=1))
    return out


def region_tiles(pos: np.ndarray, normals: np.ndarray, indices: np.ndarray):
    """Assign every triangle a palette region and a baked brightness level.

    The source model's UV map cannot be painted: large parts of the mesh share
    the same texels (the same patch of texture is used by the ground and by the
    knight at once), so anything painted for the base also lands on the armour.
    Every triangle therefore points at its own tile of the palette texture.

    The split is purely geometric: mesh components find the grass tufts, face
    orientation separates the mound from the legs, the cape is the cloth that
    hangs behind and around the body, a thin shaft in front of the knight is the
    sword, and short ray casts find the recesses that should be dark.

    Returns (region, band): region is an index into REGIONS, band is an index
    into SHADES.
    """
    tris = pos[indices]
    centroid = tris.mean(axis=1)
    face = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
    face /= np.maximum(np.linalg.norm(face, axis=1, keepdims=True), 1e-12)

    tri_component, roots = _weld_and_components(pos, indices)
    counts = {r: int((roots == r).sum()) for r in np.unique(roots)}
    is_grass = np.array([counts[c] < 200 for c in tri_component])

    # Mound: the low part of the body whose faces point upwards.  A height cut
    # alone would climb up the legs, because the mound is sculpted around them.
    is_base = (~is_grass) & (
        ((centroid[:, 1] <= -2.9) & (np.abs(face[:, 1]) >= 0.45))
        | (centroid[:, 1] <= -4.6)
    )
    is_earth = is_base & (centroid[:, 1] <= -4.75)

    # Sword: the blade and crossguard stand in front of the arms (z > 3.95), so
    # a box around them cannot pick up the gauntlets that hold the grip.
    is_guard = (
        (~is_grass)
        & (~is_base)
        & (np.abs(centroid[:, 0]) < 1.05)
        & (centroid[:, 2] > 3.95)
        & (centroid[:, 1] > 2.1)
        & (centroid[:, 1] < 3.5)
    )
    is_blade = (
        (~is_grass)
        & (~is_base)
        & (np.abs(centroid[:, 0]) < 0.7)
        & (centroid[:, 2] > 4.0)
        & (centroid[:, 1] <= 2.1)
    )
    sword = is_guard | is_blade

    # Cloak: the cape that wraps around the knight's back and sides.  It is a
    # cloth sheet hanging outside the body, so it is found by where it sits
    # (behind the body) rather than by thickness, and its lining is simply the
    # side whose normal points back at the body.
    radial = np.stack([centroid[:, 0], np.zeros(len(centroid)), centroid[:, 2]], axis=1)
    radius = np.linalg.norm(radial, axis=1)
    radial /= np.maximum(radius[:, None], 1e-6)

    # Boots and greaves are leather, with mud where they meet the ground.  They
    # are claimed first so that the heel can never turn cloak-blue.
    legs = (
        (~is_grass)
        & (~is_base)
        & (~sword)
        & (centroid[:, 1] <= -2.45)
        & (radius < 3.0)
    )
    # The knight kneels on one knee, so the poleyn (the knee plate) sits as low as
    # the boot, and a height cut alone painted the whole knee as dirty leather.
    # The knee is the part in front of the leg axis; behind it is the boot.
    knee = legs & (centroid[:, 2] > 0.35)
    boot = legs & (~knee)
    is_mud = boot & (centroid[:, 1] <= -3.95)
    is_leather = boot & (~is_mud)

    is_cape = (
        (~is_grass)
        & (~is_base)
        & (~legs)
        & (~sword)
        & (centroid[:, 2] < -1.15)
        & (centroid[:, 1] > -4.3)
        & (centroid[:, 1] < 6.4)
        & (radius > 1.0)
    )
    is_lining = is_cape & (np.einsum("ij,ij->i", face, radial) < -0.1)
    # The cape hangs all the way down to the mound, and its lowest fold sits right
    # beside the kneeling boot.  A blue tip there reads as a blue shoe, so the hem
    # picks up the same dirt as the boots instead - which is also what a cloak
    # that drags over a rubble mound would look like.
    hem = is_cape & (centroid[:, 1] <= -3.55)

    knight = (~is_grass) & (~is_base) & (~is_cape) & (~sword) & (~legs)

    shadow = _shadow_distance(tris, face)
    deep = (shadow < 0.10) & (~is_grass) & (~is_base)
    dark = (shadow < 0.30) & knight & (~is_mud) & (~is_leather)
    # The helmet's visor slit: the only genuinely recessed band on the model,
    # the face opening under the crown.  It is dark in the source model's
    # texture, so it has to be darkened explicitly now that the banner uses its
    # own palette instead of that texture.
    head = (centroid[:, 1] > 6.2) & knight
    visor = (
        head
        & (face[:, 2] > 0.1)
        & (centroid[:, 1] >= 6.38)
        & (centroid[:, 1] < 6.82)
        & (centroid[:, 2] > 1.8)
        & (np.abs(centroid[:, 0]) < 1.3)
    )

    region = np.full(len(indices), REGION_INDEX["ArmorSteel"], dtype=np.int32)
    region[is_base] = REGION_INDEX["BaseStone"]
    region[is_earth] = REGION_INDEX["BaseEarth"]
    region[is_grass] = REGION_INDEX["BaseGrass"]
    region[is_cape] = REGION_INDEX["CloakOuter"]
    region[is_lining] = REGION_INDEX["CloakInner"]
    region[hem] = REGION_INDEX["BootMud"]
    region[is_guard] = REGION_INDEX["SwordBrass"]
    region[is_blade] = REGION_INDEX["SwordBlade"]
    region[is_leather] = REGION_INDEX["Leather"]
    region[is_mud] = REGION_INDEX["BootMud"]
    region[dark] = REGION_INDEX["ArmorShadow"]
    region[deep] = REGION_INDEX["Crevice"]
    region[visor] = REGION_INDEX["Crevice"]

    # Baked brightness: a fixed model-space light plus the recesses the ray cast
    # found.  This is what breaks a plate into light and dark faces, and it keeps
    # working even though the Home Menu's own light is flat.
    #
    # The light comes from above and to the knight's left on purpose.  A light
    # along the view axis would put every plate that faces the banner camera into
    # the same brightness step, so the front of the armour would read as one flat
    # surface again; an angled light steps the plates through all four shades.
    light = np.array([-0.42, 0.78, 0.46], dtype=np.float64)
    light /= np.linalg.norm(light)
    lit = 0.45 + 0.55 * np.clip(face @ light, 0.0, 1.0)
    lit = np.where(shadow < 0.30, lit * 0.80, lit)
    lit = np.where(shadow < 0.12, lit * 0.70, lit)
    band = np.clip(((lit - 0.42) / 0.58) * SHADE_LEVELS, 0, SHADE_LEVELS - 1)
    return region, band.astype(np.int32)


def base_color_image(
    doc,
    bin_data: bytes,
    source_dir: Path,
    size: int,
    fmt: str,
    quality: int,
    gamma: float = 1.0,
    brightness: float = 1.0,
):
    for material in doc.get("materials", ()):
        base = (material.get("pbrMetallicRoughness") or {}).get("baseColorTexture")
        if not base:
            continue
        texture = doc["textures"][base["index"]]
        source = texture.get("source")
        if source is None:
            # glTF-pipeline/gltfpack store WebP images behind an extension
            # instead of the core "source" field.
            extensions = texture.get("extensions") or {}
            source = (extensions.get("EXT_texture_webp") or {}).get("source")
        if source is None:
            continue
        im = Image.open(io.BytesIO(image_bytes(doc, bin_data, source, source_dir))).convert("RGB")
        if im.size != (size, size):
            im = im.resize((size, size), Image.LANCZOS)
        if gamma != 1.0 or brightness != 1.0:
            # The source albedo is a dark, dirty stone (mean sRGB ~0.39).
            # Under the HOME Menu's single directional light and with the
            # material's ambient colour at zero the statue reads almost black,
            # so the map is lifted here.  gamma < 1 opens the midtones without
            # clipping the highlights, brightness scales the whole range.
            arr = np.asarray(im).astype(np.float32) / 255.0
            arr = np.power(arr, gamma) * brightness
            im = Image.fromarray((np.clip(arr, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8))
        buf = io.BytesIO()
        if fmt == "webp":
            im.save(buf, format="WEBP", quality=quality, method=6)
            mime = "image/webp"
        else:
            im.save(buf, format="PNG", optimize=True)
            mime = "image/png"
        return buf.getvalue(), mime
    raise ValueError("the source model has no base-colour texture")


def centre_and_scale(pos: np.ndarray, height: float, y_rotate: float = 0.0):
    mins = pos.min(axis=0)
    maxs = pos.max(axis=0)
    centre = (mins + maxs) / 2.0
    span_y = maxs[1] - mins[1]
    if span_y <= 0:
        raise ValueError("the model has zero height")
    scale = height / span_y
    out = (pos - centre) * scale
    if y_rotate:
        # Turn the model around the vertical axis it will spin around in the
        # Home Menu.  The knight statue already faces +Z, i.e. the banner
        # camera, so it needs no rotation; this is here for models that face
        # away.
        c, s = math.cos(math.radians(y_rotate)), math.sin(math.radians(y_rotate))
        x, z = out[:, 0].copy(), out[:, 2].copy()
        out[:, 0] = c * x + s * z
        out[:, 2] = -s * x + c * z
    out[:, 1] += 1.0  # the banner camera looks at Y = 1
    return out


def split_triangles(indices: np.ndarray, max_vertices: int):
    chunks = []
    current = []
    used = set()
    for tri in indices:
        new = set(int(v) for v in tri) - used
        if current and len(used) + len(new) > max_vertices:
            chunks.append((np.array(current, dtype=np.uint32), sorted(used)))
            current = []
            used = set()
        current.append(tri)
        used.update(int(v) for v in tri)
    if current:
        chunks.append((np.array(current, dtype=np.uint32), sorted(used)))
    return chunks


def build_glb(
    positions: np.ndarray,
    normals: np.ndarray,
    indices: np.ndarray,
    image: bytes,
    image_mime: str,
    material: dict,
    tri_tile: np.ndarray,
    tri_group: np.ndarray,
    max_vertices: int,
    root_name: str = "COMMON",
    uvs: np.ndarray | None = None,
):
    """Assemble the banner GLB with exactly one material.

    The colours live in a palette texture, so a triangle only has to pick its
    tile.  Triangles are packed into a few submeshes (one per group of regions);
    inside a submesh each palette tile gets its own copy of the vertices it
    needs, so two tiles can never share a UV.
    """
    bin_data = bytearray()
    views = []
    accessors = []

    def add_view(data: bytes, target: int | None = None):
        while len(bin_data) % 4:
            bin_data.append(0)
        offset = len(bin_data)
        bin_data.extend(data)
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        views.append(view)
        return len(views) - 1

    def add_accessor(data: bytes, component: int, count: int, kind: str, target: int, extra=None):
        view = add_view(data, target)
        acc = {
            "bufferView": view,
            "componentType": component,
            "count": count,
            "type": kind,
        }
        if extra:
            acc.update(extra)
        accessors.append(acc)
        return len(accessors) - 1

    meshes = []
    nodes = []
    # Each submesh gets its own node and name: pycgfx names the generated SOBJ
    # after the mesh, and two primitives in one mesh would produce duplicate
    # SOBJ keys and a corrupt Patricia trie.
    for group in sorted(set(int(g) for g in tri_group)):
        selected = np.flatnonzero(tri_group == group)
        tiles = sorted(set(int(t) for t in tri_tile[selected]))

        pos_parts, nrm_parts, uv_parts, idx_parts = [], [], [], []
        offset = 0
        for tile in tiles:
            sub = selected[tri_tile[selected] == tile]
            used = np.unique(indices[sub])
            if uvs is not None:
                # --no-paint keeps the model's own UV map for the photographic
                # texture; the painted banner generates UVs into its palette.
                uv = uvs[used].astype("<f4")
            else:
                uv = triplanar_uv(positions[used], normals[used], tile).astype("<f4")
            pos = positions[used].astype("<f4")
            nrm = normals[used].astype("<f4")
            # The source model stores one corner per face and the tile loop
            # repeats every corner it needs, so the same (position, normal, uv)
            # row appears many times.  Merging those rows is lossless -- equal
            # attributes rasterise identically -- and it matters: the HOME Menu
            # gives a 3D banner 256 KiB of vertex data and hangs past that.
            rows = np.concatenate([pos, nrm, uv], axis=1)
            kept = np.unique(rows, axis=0, return_index=True, return_inverse=True)
            first = kept[1]
            remap = np.zeros(len(positions), dtype="<u4")
            remap[used] = kept[2]
            pos_parts.append(pos[first])
            nrm_parts.append(nrm[first])
            uv_parts.append(uv[first])
            idx_parts.append(
                np.array(
                    [[int(remap[int(v)]) + offset for v in tri] for tri in indices[sub]],
                    dtype="<u4",
                )
            )
            offset += len(first)

        group_pos = np.concatenate(pos_parts)
        group_nrm = np.concatenate(nrm_parts)
        group_uv = np.concatenate(uv_parts)
        group_idx = np.concatenate(idx_parts)

        for tris, used in split_triangles(group_idx, max_vertices):
            local = {int(old): new for new, old in enumerate(used)}
            local_pos = group_pos[used].astype("<f4")
            local_nrm = group_nrm[used].astype("<f4")
            local_uv = group_uv[used].astype("<f4")
            local_idx = np.array(
                [[local[int(v)] for v in tri] for tri in tris], dtype="<u2"
            )

            pos_acc = add_accessor(
                local_pos.tobytes(),
                5126,
                len(local_pos),
                "VEC3",
                34962,
                {
                    "min": local_pos.min(axis=0).tolist(),
                    "max": local_pos.max(axis=0).tolist(),
                },
            )
            nrm_acc = add_accessor(local_nrm.tobytes(), 5126, len(local_nrm), "VEC3", 34962)
            uv_acc = add_accessor(local_uv.tobytes(), 5126, len(local_uv), "VEC2", 34962)
            idx_acc = add_accessor(local_idx.tobytes(), 5123, local_idx.size, "SCALAR", 34963)
            name = f"KnightBanner_{len(meshes)}"
            meshes.append(
                {
                    "name": name,
                    "primitives": [
                        {
                            "attributes": {
                                "POSITION": pos_acc,
                                "NORMAL": nrm_acc,
                                "TEXCOORD_0": uv_acc,
                            },
                            "indices": idx_acc,
                            "material": 0,
                            "mode": 4,
                        }
                    ],
                }
            )
            nodes.append({"name": name, "mesh": len(meshes) - 1})

    # One explicit root bone above the meshes.  Every hardware-tested banner
    # CGFX has the same shape: "Scene root" -> "COMMON" -> mesh nodes, and the
    # HOME Menu renderer is happier walking that hierarchy than it is with
    # meshes attached directly to "Scene root".
    nodes.append({"name": root_name, "children": list(range(len(meshes)))})
    root_node = len(nodes) - 1

    image_view = add_view(image)
    doc = {
        "asset": {
            "version": "2.0",
            "generator": "NetHack 3DS banner builder",
        },
        "scene": 0,
        "scenes": [{"nodes": [root_node]}],
        "nodes": nodes,
        "meshes": meshes,
        "materials": [
            {
                "name": material["name"],
                "doubleSided": False,
                "alphaMode": "OPAQUE",
                "pbrMetallicRoughness": {
                    "baseColorFactor": [*material["color"], 1.0],
                    "baseColorTexture": {"index": 0},
                    "metallicFactor": 0.0,
                    "roughnessFactor": material["roughness"],
                },
            }
        ],
        "textures": [{"sampler": 0, "source": 0}],
        "samplers": [
            {
                "magFilter": 9729,
                "minFilter": 9729,
                "wrapS": 33071,
                "wrapT": 33071,
            }
        ],
        "images": [
            {
                "name": "KnightBannerPalette",
                "bufferView": image_view,
                "mimeType": image_mime,
            }
        ],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(bin_data)}],
    }
    return doc, bytes(bin_data), len(meshes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--texture-size",
        type=int,
        default=512,
        help="base-colour texture size with --no-paint (default: 512)",
    )
    parser.add_argument(
        "--height",
        type=float,
        default=14.0,
        help="model height in banner units (default: 14)",
    )
    parser.add_argument(
        "--y-rotate",
        type=float,
        default=0.0,
        help="rotate the model this many degrees around its vertical axis "
        "(default: 0; the Home Menu spins it around this axis)",
    )
    parser.add_argument(
        "--max-vertices",
        type=int,
        default=4096,
        help="maximum vertices per submesh (default: 4096; 64 KiB stream limit)",
    )
    parser.add_argument(
        "--root-name",
        default="COMMON",
        help="name of the single root node above the meshes (default: COMMON)",
    )
    parser.add_argument(
        "--image-format",
        choices=("png", "webp"),
        default="png",
        help="texture encoding with --no-paint (default: png)",
    )
    parser.add_argument(
        "--webp-quality",
        type=int,
        default=90,
        help="WebP quality when --image-format=webp (default: 90)",
    )
    parser.add_argument(
        "--paint",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="colour the mesh from a generated palette (armour, sword, leather, "
        "cloak, stone/earth/grass base) instead of using the source texture "
        "(default: yes)",
    )
    parser.add_argument(
        "--gamma",
        type=float,
        default=0.6,
        help="gamma applied to the source texture with --no-paint (default: 0.6)",
    )
    parser.add_argument(
        "--brightness",
        type=float,
        default=1.15,
        help="brightness multiplier with --no-paint (default: 1.15)",
    )
    args = parser.parse_args()

    doc, bin_data = load_glb(args.source)
    positions, normals, uvs, indices, _ = collect_geometry(doc, bin_data)
    positions = centre_and_scale(positions, args.height, args.y_rotate)

    if args.paint:
        # One material, and every colour comes from a tile of the palette
        # texture: the source UV layout is shared between unrelated parts, so
        # the banner generates its own UVs instead of painting that atlas.
        region, band = region_tiles(positions, normals, indices)
        tri_tile = region * SHADE_LEVELS + band
        tri_group = group_of_region(region)
        material = {"name": "KnightBanner", "color": (1.0, 1.0, 1.0), "roughness": 0.45}
        buf = io.BytesIO()
        palette_texture().save(buf, format="PNG", optimize=True)
        image = buf.getvalue()
        mime = "image/png"
    else:
        tri_tile = np.zeros(len(indices), dtype=np.int32)
        tri_group = np.zeros(len(indices), dtype=np.int32)
        material = {"name": "KnightMaterial", "color": (1.0, 1.0, 1.0), "roughness": 1.0}
        image, mime = base_color_image(
            doc,
            bin_data,
            args.source.parent,
            args.texture_size,
            args.image_format,
            args.webp_quality,
            args.gamma,
            args.brightness,
        )

    out_doc, out_bin, mesh_count = build_glb(
        positions,
        normals,
        indices,
        image,
        mime,
        material,
        tri_tile,
        tri_group,
        args.max_vertices,
        args.root_name,
        uvs=None if args.paint else uvs,
    )
    size = write_glb(out_doc, out_bin, args.output)
    detail = (
        f"1 material, {TILE_SIZE}x{TILE_SIZE} palette with "
        f"{len(REGIONS)} regions x {SHADE_LEVELS} shades"
        if args.paint
        else f"1 material, {args.texture_size}x{args.texture_size} {args.image_format}"
    )
    print(
        f"{args.output}: {size} bytes, {len(positions)} verts, "
        f"{len(indices)} tris in {mesh_count} submeshes, {detail}"
    )


if __name__ == "__main__":
    main()
