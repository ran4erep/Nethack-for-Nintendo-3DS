"""Minimal ETC1 encoder/decoder for the 3DS PICA200 texture layout.

The HOME Menu stores textures in CGFX files uncompressed.  ETC1 is the only
widely supported compressed colour format on the 3DS, and it costs four bits
per pixel instead of 16 or 24.  That makes a 512x512 ETC1 texture the same
size as a 256x256 RGBA4 texture, with four times as many texels.

The block layout and the 8x8 ``tile -> four 4x4 subtiles`` order follow the
PICA200 hardware (see the ETC1 decoder in Citra's texture_decode.cpp):

  * 8x8 tiles are stored row-major;
  * inside a tile the four 4x4 blocks are ordered
    (0,0), (1,0), (0,1), (1,1);
  * each block is a standard ETC1 64-bit word, stored little-endian.

Only RGB is needed for the banner's base-colour map, so the alpha variant
(ETC1A4) is intentionally not implemented.
"""

from __future__ import annotations

from PIL import Image
import numpy as np

# (modifier for index bit 0, modifier for index bit 1), indexed by ETC1 table.
MODIFIERS = (
    (2, 8),
    (5, 17),
    (9, 29),
    (13, 42),
    (18, 60),
    (24, 80),
    (33, 106),
    (47, 183),
)


def _clamp(v: int) -> int:
    return 0 if v < 0 else 255 if v > 255 else v


def _c4(v: int) -> int:
    return v * 17


def _c5(v: int) -> int:
    return (v << 3) | (v >> 2)


def _q4(v: float) -> int:
    return max(0, min(15, int(round(v / 17.0))))


def _q5(v: float) -> int:
    return max(0, min(31, int(round(v * 31.0 / 255.0))))


def _sign3(v: int) -> int:
    return v - 8 if v & 4 else v


def _eval_half(pixels, base, table):
    """Return (squared error, sub-index mask, negation mask) for one half.

    ``pixels`` is a list of ``(x, y, (r, g, b))`` and the masks are indexed by
    the ETC1 texel number ``4*x + y``.
    """
    m0, m1 = MODIFIERS[table]
    br, bg, bb = base
    error = 0
    sub_mask = 0
    neg_mask = 0
    for x, y, (r, g, b) in pixels:
        best = None
        best_sub = 0
        best_neg = 0
        for sub, m in enumerate((m0, m1)):
            for neg in (0, 1):
                mm = -m if neg else m
                dr = _clamp(br + mm) - r
                dg = _clamp(bg + mm) - g
                db = _clamp(bb + mm) - b
                e = dr * dr + dg * dg + db * db
                if best is None or e < best:
                    best = e
                    best_sub = sub
                    best_neg = neg
        error += best
        bit = 4 * x + y
        sub_mask |= best_sub << bit
        neg_mask |= best_neg << bit
    return error, sub_mask, neg_mask


def _average(pixels):
    n = len(pixels)
    return (
        sum(p[2][0] for p in pixels) / n,
        sum(p[2][1] for p in pixels) / n,
        sum(p[2][2] for p in pixels) / n,
    )


def encode_block(block) -> bytes:
    """Encode one 4x4 RGB block (``block[y][x]``) to 8 ETC1 bytes."""
    best = None  # (error, packed bytes)

    for flip in (0, 1):
        halves = ([], [])
        for y in range(4):
            for x in range(4):
                sx, sy = (y, x) if flip else (x, y)
                halves[0 if sx < 2 else 1].append((x, y, tuple(int(v) for v in block[y][x])))

        # --- individual mode: two independent 4-bit base colours -------------
        c1 = tuple(_q4(v) for v in _average(halves[0]))
        c2 = tuple(_q4(v) for v in _average(halves[1]))
        base1 = tuple(_c4(v) for v in c1)
        base2 = tuple(_c4(v) for v in c2)
        best1 = best2 = None
        for t in range(8):
            r1 = _eval_half(halves[0], base1, t)
            if best1 is None or r1[0] < best1[0]:
                best1 = (r1[0], t, r1[1], r1[2])
            r2 = _eval_half(halves[1], base2, t)
            if best2 is None or r2[0] < best2[0]:
                best2 = (r2[0], t, r2[1], r2[2])
        error = best1[0] + best2[0]
        if best is None or error < best[0]:
            best = (
                error,
                _pack_individual(
                    flip,
                    best1[1],
                    best2[1],
                    c1,
                    c2,
                    best1[2] | best2[2],
                    best1[3] | best2[3],
                ),
            )

        # --- differential mode: 5-bit base + 3-bit delta ----------------------
        c1_5 = list(_q5(v) for v in _average(halves[0]))
        c2_5 = list(_q5(v) for v in _average(halves[1]))
        delta = [max(-4, min(3, c2_5[i] - c1_5[i])) for i in range(3)]
        # Keep base + delta inside the 5-bit range.
        c1_5 = [max(max(0, -delta[i]), min(31, 31 - delta[i], c1_5[i])) for i in range(3)]
        c2_5 = [c1_5[i] + delta[i] for i in range(3)]
        base1 = tuple(_c5(v) for v in c1_5)
        base2 = tuple(_c5(v) for v in c2_5)
        best1 = best2 = None
        for t in range(8):
            r1 = _eval_half(halves[0], base1, t)
            if best1 is None or r1[0] < best1[0]:
                best1 = (r1[0], t, r1[1], r1[2])
            r2 = _eval_half(halves[1], base2, t)
            if best2 is None or r2[0] < best2[0]:
                best2 = (r2[0], t, r2[1], r2[2])
        error = best1[0] + best2[0]
        if error < best[0]:
            best = (
                error,
                _pack_differential(
                    flip,
                    best1[1],
                    best2[1],
                    c1_5,
                    delta,
                    best1[2] | best2[2],
                    best1[3] | best2[3],
                ),
            )

    return best[1]


def _pack_individual(flip, t1, t2, c1, c2, sub, neg) -> bytes:
    low = sub | (neg << 16)
    high = flip | (t2 << 2) | (t1 << 5)
    high |= c2[2] << 8
    high |= c1[2] << 12
    high |= c2[1] << 16
    high |= c1[1] << 20
    high |= c2[0] << 24
    high |= c1[0] << 28
    return (low | (high << 32)).to_bytes(8, "little")


def _pack_differential(flip, t1, t2, c1, delta, sub, neg) -> bytes:
    low = sub | (neg << 16)
    high = flip | 2 | (t2 << 2) | (t1 << 5)
    high |= (delta[2] & 7) << 8
    high |= c1[2] << 11
    high |= (delta[1] & 7) << 16
    high |= c1[1] << 19
    high |= (delta[0] & 7) << 24
    high |= c1[0] << 27
    return (low | (high << 32)).to_bytes(8, "little")


def encode_image(im: Image.Image) -> bytes:
    """Encode an RGB image to a PICA200 ETC1 texture.

    The image dimensions must be multiples of eight (512x512 for the banner).
    """
    im = im.convert("RGB")
    w, h = im.size
    if w % 8 or h % 8:
        raise ValueError(f"ETC1 image size must be a multiple of 8, got {w}x{h}")
    pixels = np.asarray(im, dtype=np.int16)
    out = bytearray()
    for ty in range(0, h, 8):
        for tx in range(0, w, 8):
            tile = bytearray(32)
            for sy in range(2):
                for sx in range(2):
                    block = pixels[ty + sy * 4 : ty + sy * 4 + 4, tx + sx * 4 : tx + sx * 4 + 4]
                    offset = (sx + 2 * sy) * 8
                    tile[offset : offset + 8] = encode_block(block)
            out += tile
    return bytes(out)


def decode_block(data: bytes):
    """Decode one 8-byte ETC1 block to ``block[y][x] = (r, g, b)``."""
    value = int.from_bytes(data, "little")
    low = value & 0xFFFFFFFF
    high = value >> 32
    sub = low & 0xFFFF
    neg = (low >> 16) & 0xFFFF
    flip = high & 1
    differential = (high >> 1) & 1
    t2 = (high >> 2) & 7
    t1 = (high >> 5) & 7

    if differential:
        db = _sign3((high >> 8) & 7)
        b = (high >> 11) & 31
        dg = _sign3((high >> 16) & 7)
        g = (high >> 19) & 31
        dr = _sign3((high >> 24) & 7)
        r = (high >> 27) & 31
        c1 = (_c5(r), _c5(g), _c5(b))
        c2 = (_c5(r + dr), _c5(g + dg), _c5(b + db))
    else:
        b2 = (high >> 8) & 15
        b1 = (high >> 12) & 15
        g2 = (high >> 16) & 15
        g1 = (high >> 20) & 15
        r2 = (high >> 24) & 15
        r1 = (high >> 28) & 15
        c1 = (_c4(r1), _c4(g1), _c4(b1))
        c2 = (_c4(r2), _c4(g2), _c4(b2))

    block = [[None] * 4 for _ in range(4)]
    for y in range(4):
        for x in range(4):
            texel = 4 * x + y
            sx, sy = (y, x) if flip else (x, y)
            base = c1 if sx < 2 else c2
            table = t1 if sx < 2 else t2
            m = MODIFIERS[table][(sub >> texel) & 1]
            if (neg >> texel) & 1:
                m = -m
            block[y][x] = (
                _clamp(base[0] + m),
                _clamp(base[1] + m),
                _clamp(base[2] + m),
            )
    return block


def decode_image(data: bytes, width: int, height: int) -> Image.Image:
    """Decode a PICA200 ETC1 texture into an RGB image (for validation)."""
    im = Image.new("RGB", (width, height))
    px = im.load()
    offset = 0
    for ty in range(0, height, 8):
        for tx in range(0, width, 8):
            for sy in range(2):
                for sx in range(2):
                    block = decode_block(data[offset : offset + 8])
                    offset += 8
                    for y in range(4):
                        for x in range(4):
                            px[tx + sx * 4 + x, ty + sy * 4 + y] = block[y][x]
    return im
