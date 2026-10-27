#!/usr/bin/env python3
"""Sanity-check the generated banner.cgfx without a 3DS.

Rebuilds the CGFX in memory with the same code path as convert_banner.py,
confirms the bytes are identical to the checked-in binary, then prints the
parts the HOME Menu depends on (CMDL/SOBJ names, vertex buffer sizes, material
textures, TXOB format) and dumps the decoded texture for eyeballing.

Usage:
    python sys/3ds/verify_banner.py sys/3ds/banner.glb sys/3ds/banner.cgfx
"""

from __future__ import annotations

import sys
from pathlib import Path

import convert_banner
import etc1

BUDGET = 512 * 1024
# Not a real Home Menu limit: this was once suspected when the larger builds
# froze the console, but a 217952-byte build locked up as well and the shipped
# 232544-byte one runs, so the size is reported for information only.
VERTEX_BUDGET = 256 * 1024

# Same 8x8 tile permutation pycgfx uses when it writes RGBA4/RGB565 textures.
SWIZZLE_MAP = [
    0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15,
    0x02, 0x03, 0x06, 0x07, 0x12, 0x13, 0x16, 0x17,
    0x08, 0x09, 0x0C, 0x0D, 0x18, 0x19, 0x1C, 0x1D,
    0x0A, 0x0B, 0x0E, 0x0F, 0x1A, 0x1B, 0x1E, 0x1F,
    0x20, 0x21, 0x24, 0x25, 0x30, 0x31, 0x34, 0x35,
    0x22, 0x23, 0x26, 0x27, 0x32, 0x33, 0x36, 0x37,
    0x28, 0x29, 0x2C, 0x2D, 0x38, 0x39, 0x3C, 0x3D,
    0x2A, 0x2B, 0x2E, 0x2F, 0x3A, 0x3B, 0x3E, 0x3F,
]


def decode_rgba4(data: bytes, width: int, height: int):
    from PIL import Image

    im = Image.new("RGBA", (width, height))
    pixels = im.load()
    for ty in range(height // 8):
        for tx in range(width // 8):
            tile = (ty * (width // 8) + tx) * 64 * 2
            for i in range(64):
                off = tile + SWIZZLE_MAP[i] * 2
                b0, b1 = data[off], data[off + 1]
                # PICA200 RGBA4 is little-endian ABGR: low nibble = A,
                # then B, G, R in the high nibble of the second byte.
                pixels[tx * 8 + (i % 8), ty * 8 + (i // 8)] = (
                    (b1 >> 4) * 17,
                    (b1 & 0x0F) * 17,
                    (b0 >> 4) * 17,
                    (b0 & 0x0F) * 17,
                )
    # convert_banner.py flips the image before packing it.
    return im.transpose(Image.Transpose.FLIP_TOP_BOTTOM)


def main() -> int:
    glb = Path(sys.argv[1] if len(sys.argv) > 1 else "sys/3ds/banner.glb")
    cgfx_path = Path(sys.argv[2] if len(sys.argv) > 2 else "sys/3ds/banner.cgfx")
    pycgfx_root = Path(".tools/pycgfx")

    pycgfx = convert_banner._load_pycgfx(pycgfx_root)
    from cgfx.primitives import InterleavedVertexStream, VertexStream, VertexAttributeUsage
    from cgfx.txob import TextureFormat

    format_names = {int(v): v.name for v in TextureFormat}

    cgfx = convert_banner.build_cgfx(glb, pycgfx)
    rebuilt = pycgfx.write(cgfx)

    on_disk = cgfx_path.read_bytes()
    print(f"file        {cgfx_path} ({len(on_disk)} bytes, budget {BUDGET})")
    print(f"budget      {'OK' if len(on_disk) <= BUDGET else 'OVER'} "
          f"({BUDGET - len(on_disk)} bytes spare)")
    print(f"rebuild     {'identical' if rebuilt == on_disk else 'DIFFERS'}")
    if rebuilt != on_disk:
        print("ERROR: convert_banner.py no longer reproduces the checked-in CGFX")
        return 1

    problems = []
    if cgfx.data.models.len() != 1:
        problems.append(f"expected 1 CMDL, found {cgfx.data.models.len()}")

    for model_name in cgfx.data.models:
        model = cgfx.data.models[model_name]
        print(f"\nCMDL        {model_name!r}")
        if model_name != "COMMON":
            problems.append(f"CMDL is named {model_name!r}, HOME Menu wants 'COMMON'")
        skeleton = getattr(model, "skeleton", None)
        # pycgfx always emits a skeleton; what matters is that no shape is
        # skinned (the HOME Menu rejects soft skinning) and that the meshes sit
        # under a "COMMON" bone, as in the banners known to work on hardware.
        if skeleton:
            bones = skeleton.bones
            print(f"skeleton    {bones.len()} bones")
            for bone_name in bones:
                bone = bones[bone_name]
                parent = getattr(bone, "parent", None)
                print(f"  bone      {bone_name!r} parent="
                      f"{parent.name if parent is not None else None!r}")
            if "COMMON" not in bones:
                problems.append("skeleton has no 'COMMON' bone above the meshes")
        else:
            print("skeleton    none")

        print(f"meshes      {len(model.meshes)}")
        for mesh in model.meshes.data.contents:
            print(f"  SOBJ      {mesh.name!r} shape={mesh.shape_index} "
                  f"material={mesh.material_index} node={mesh.mesh_node_name!r}")

        print(f"shapes      {len(model.shapes)}")
        vertex_bytes = 0
        for shape in model.shapes.data.contents:
            print(f"  shape     {shape.name!r} attrs={len(shape.vertex_attributes)}")
            shape_verts = 0
            for attr in shape.vertex_attributes.data.contents:
                vertex_bytes += len(attr.vertex_stream_data)
                if isinstance(attr, InterleavedVertexStream):
                    streams = len(attr.vertex_streams)
                    shape_verts = max(
                        shape_verts,
                        len(attr.vertex_stream_data) // attr.vertex_data_entry_size,
                    )
                    print(f"    interleaved data={len(attr.vertex_stream_data)} "
                          f"streams={streams} stride={attr.vertex_data_entry_size}")
                elif isinstance(attr, VertexStream):
                    verts = len(attr.vertex_stream_data) // (
                        4 * max(1, attr.components_count)
                    )
                    print(f"    stream    usage={attr.usage.name} "
                          f"components={attr.components_count} "
                          f"data={len(attr.vertex_stream_data)} verts={verts}")
                    if len(attr.vertex_stream_data) > 0x10000:
                        problems.append(
                            f"{shape.name} {attr.usage.name} stream is "
                            f"{len(attr.vertex_stream_data)} bytes "
                            "(PICA200 vertex buffer limit is 65536)"
                        )
                else:
                    print(f"    other     {type(attr).__name__}")
                if getattr(attr, "usage", None) in (
                    VertexAttributeUsage.BoneIndex,
                    VertexAttributeUsage.BoneWeight,
                ):
                    problems.append(f"{shape.name} has {attr.usage.name} vertices")

            for pset in shape.primitive_sets.data.contents:
                if pset.skinning_mode:
                    problems.append(f"{shape.name} uses skinning_mode={pset.skinning_mode}")
                for prim in pset.primitives.data.contents:
                    for stream in prim.index_streams.data.contents:
                        face = stream.face_data
                        width = 2 if int(stream.data_type) == 5123 else 1
                        top = max(
                            (int.from_bytes(face[i:i + width], "little")
                             for i in range(0, len(face), width)),
                            default=0,
                        )
                        print(f"    indices   {len(face)} bytes "
                              f"mode={stream.primitive_mode} "
                              f"type={int(stream.data_type)} max={top}")
                        if shape_verts and top >= shape_verts:
                            problems.append(
                                f"{shape.name} index {top} is past its "
                                f"{shape_verts} vertices"
                            )

        print(f"vertices    {vertex_bytes} bytes "
              f"(reference {VERTEX_BUDGET}, {VERTEX_BUDGET - vertex_bytes} spare)")

        print(f"materials   {model.materials.len()}")
        for mat_name in model.materials:
            mat = model.materials[mat_name]
            print(f"  MTOB      {mat_name!r}")
            print(f"    cull      {mat.rasterization.command.param} "
                  f"(2 = back-face)")
            print(f"    diffuse   {mat.material_color.diffuse.values()}")
            print(f"    specular  {mat.material_color.specular[0].values()}")
            print(f"    constant0 {mat.material_color.constant[0].values()}")
            print(f"    tev coords={mat.used_texture_coordinates_count}")
            for slot, mapper in enumerate(mat.texture_mappers):
                if mapper is None:
                    continue
                # pycgfx wraps the TXOB in a ReferenceTexture.
                tex = getattr(mapper.txob, "txob", mapper.txob)
                fmt = int(tex.hw_format)
                print(f"    texture{slot}  {tex.name!r} {tex.width}x{tex.height} "
                      f"{format_names.get(fmt, fmt)}")
                if fmt != int(TextureFormat.ETC1) and fmt != int(TextureFormat.RGBA4):
                    problems.append(
                        f"{mat_name} texture{slot} uses "
                        f"{format_names.get(fmt, fmt)}; the banners known to "
                        "work use RGBA4 or ETC1"
                    )
            if (mat.material_color.specular[0].r == 0
                    and mat.material_color.constant[0].r == 0):
                problems.append(
                    f"{mat_name} has its specular stage muted (--matte); "
                    "the model will look much darker than the reference banners"
                )
            if not any(m is not None for m in mat.texture_mappers):
                problems.append(f"{mat_name} has no texture bound")

    print(f"\nLIGHTS      {cgfx.data.lights.len()}")
    for light_name in cgfx.data.lights:
        light = cgfx.data.lights[light_name]
        print(f"  CFLT      {light_name!r}")
        print(f"    ambient  {light.ambient.values()}")
        print(f"    diffuse  {light.diffuse.values()}")

    print(f"\nTEXTURES    {cgfx.data.textures.len()}")
    for tex_name in cgfx.data.textures:
        tex = cgfx.data.textures[tex_name]
        fmt = int(tex.hw_format)
        data = tex.pixel_based_image.data
        print(f"  TXOB      {tex_name!r} {tex.width}x{tex.height} "
              f"{format_names.get(fmt, fmt)} {len(data)} bytes "
              f"mips={tex.mipmap_level_count}")
        if fmt == int(TextureFormat.ETC1):
            needed = tex.width * tex.height // 2
        elif fmt == int(TextureFormat.ETC1A4):
            needed = tex.width * tex.height
        else:
            needed = int(tex.width * tex.height
                         * TextureFormat(fmt).bytes_per_pixel())
        if len(data) != needed:
            problems.append(
                f"{tex_name} payload is {len(data)} bytes, "
                f"{format_names.get(fmt, fmt)} needs {needed}"
            )
        out = Path("build3ds") / f"banner_texture_{tex.width}.png"
        out.parent.mkdir(parents=True, exist_ok=True)
        if fmt == int(TextureFormat.ETC1):
            etc1.decode_image(data, tex.width, tex.height).save(out)
        elif fmt == int(TextureFormat.RGBA4):
            decode_rgba4(data, tex.width, tex.height).save(out)
        else:
            print(f"    decoded   (no decoder for {format_names.get(fmt, fmt)})")
            continue
        print(f"    decoded   {out}")

    print()
    if problems:
        for p in problems:
            print(f"PROBLEM: {p}")
        return 1
    print("OK: banner.cgfx matches the 3DS banner rules checked here")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
