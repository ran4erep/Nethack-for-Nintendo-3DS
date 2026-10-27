#!/usr/bin/env python3
"""Convert the prepared banner GLB into a 3DS HOME Menu CGFX.

This is a thin wrapper around pycgfx (skyfloogle/pycgfx) with three banner
specific choices:

* the map is stored as RGBA4, the format every banner known to work on real
  hardware uses.  The painted banner puts all of its colour in one 128x128
  palette texture (32 KiB) and uses a single material; with --no-paint pass
  --texture-size 256, the largest RGBA4 texture that still fits the budget.
* the material is left as pycgfx writes it.  Its specular stage
  (specular colour 1,1,1 and TEV constant 0.1) is what the working banners
  use and what gives the stone its highlights; muting it (--matte) makes the
  model flatter and darker.
* the scene light is left exactly as pycgfx writes it: ambient = diffuse = 1
  with the light pointing along -Z, the same setting the hardware-tested
  banners use.  The painted banner bakes its own brightness steps into the
  palette, so the light does not have to be re-tuned (and a changed CFLT is one
  more thing that can surprise the HOME Menu).  --light-ambient / --light-diffuse
  are still there for experiments.

The input GLB is expected to be the output of build_banner_glb.py: float
attributes, one base-colour texture, no normal/metallic-roughness maps and no
skinning.
"""

from __future__ import annotations

import argparse
import io
import sys
from pathlib import Path
from types import ModuleType

from PIL import Image

# 3DS/PICA200 texture format number.
ETC1 = 12


def _load_pycgfx(root: Path) -> ModuleType:
    sys.path.insert(0, str(root.resolve()))
    import main as pycgfx  # noqa: E402

    return pycgfx


def build_cgfx(
    in_glb: Path,
    pycgfx: ModuleType,
    texture_size: int = 256,
    texture_format: str = "rgba4",
    matte: bool = False,
    ambient: float = 1.0,
    diffuse: float = 1.0,
):
    """Return the pycgfx CGFX object for ``in_glb``.

    Kept separate from :func:`main` so ``verify_banner.py`` can inspect the
    object graph that produced the binary.
    """
    from cgfx.mtob import ColorFloat  # noqa: E402
    from cgfx.txob import ImageTexture, TextureFormat  # noqa: E402

    import etc1 as etc1_codec  # noqa: E402

    def banner_texture(cgfx, gltf, image_id: int, normal: bool = False):
        image = gltf.model.images[image_id]
        name = image.name or image.uri or f"image{image_id}"
        if normal:
            name = f"NORM~{name}"
        if name in cgfx.data.textures:
            return cgfx.data.textures[name]

        if image.uri is not None:
            data = gltf.get_resource(image.uri).data
        else:
            data = pycgfx.gltf_get_bv_data(gltf, image.bufferView)

        im = Image.open(io.BytesIO(data)).convert("RGB")
        if im.size != (texture_size, texture_size):
            im = im.resize(
                (texture_size, texture_size), Image.Resampling.LANCZOS
            )
        # glTF UVs start at the top-left, the PICA200 sampler at the bottom-left.
        im = im.transpose(Image.Transpose.FLIP_TOP_BOTTOM)

        txob = ImageTexture()
        txob.name = name
        txob.width = txob.pixel_based_image.width = im.width
        txob.height = txob.pixel_based_image.height = im.height
        txob.mipmap_level_count = 1
        if texture_format == "etc1":
            txob.hw_format = TextureFormat.ETC1
            txob.pixel_based_image.data = etc1_codec.encode_image(im)
        else:
            txob.hw_format = TextureFormat.RGBA4
            txob.pixel_based_image.data = pycgfx.swizzler.swizzle(
                im.convert("RGBA"), TextureFormat.RGBA4
            )
        cgfx.data.textures.add(name, txob)
        return txob

    pycgfx.gltf_get_texture = banner_texture

    gltf = pycgfx.gltflib.GLTF.load(str(in_glb), load_file_resources=True)
    cgfx = pycgfx.convert_gltf(gltf)

    # pycgfx writes ambient = diffuse = 1 and a light pointing along -Z.  That
    # is the configuration the banners known to work on hardware use, so it is
    # left alone by default; the palette bakes the shading into the texture.
    # --light-ambient / --light-diffuse can still dim it for experiments.
    if ambient != 1.0 or diffuse != 1.0:
        for name in cgfx.data.lights:
            light = cgfx.data.lights[name]
            light.ambient = ColorFloat(ambient, ambient, ambient, 1.0)
            light.diffuse = ColorFloat(diffuse, diffuse, diffuse, 1.0)

    if matte:
        for model_name in cgfx.data.models:
            model = cgfx.data.models[model_name]
            # pycgfx keeps materials on the CMDL, not in cgfx.data.materials.
            for material_name in model.materials:
                material = model.materials[material_name]
                # The PBR conversion adds a specular stage; muting its colour
                # and its TEV constant removes the highlights and makes the
                # model noticeably darker, so this is off by default.
                material.material_color.specular[0] = ColorFloat(0, 0, 0, 0)
                material.material_color.constant[0] = ColorFloat(0, 0, 0, 1)

    return cgfx


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("in_glb", type=Path, help="prepared input GLB")
    parser.add_argument("out_cgfx", type=Path, help="output CGFX")
    parser.add_argument(
        "--pycgfx-root",
        type=Path,
        required=True,
        help="directory containing pycgfx's main.py and cgfx/ package",
    )
    parser.add_argument(
        "--texture-size",
        type=int,
        default=256,
        help="base-colour texture size, multiple of 8 (default: 256, the size the "
        "shipped statue banner uses)",
    )
    parser.add_argument(
        "--texture-format",
        choices=("etc1", "rgba4"),
        default="rgba4",
        help="base-colour texture format (default: rgba4)",
    )
    parser.add_argument(
        "--matte",
        action=argparse.BooleanOptionalAction,
        default=False,
        help="mute the PBR specular stage (flatter, darker; default: no)",
    )
    parser.add_argument(
        "--light-ambient",
        type=float,
        default=1.0,
        help="ambient term of the scene light (default: 1.0, i.e. pycgfx's own "
        "setting; lower it only for experiments)",
    )
    parser.add_argument(
        "--light-diffuse",
        type=float,
        default=1.0,
        help="diffuse term of the scene light (default: 1.0, i.e. pycgfx's own "
        "setting)",
    )
    args = parser.parse_args()

    if args.texture_size % 8:
        parser.error("--texture-size must be a multiple of 8")
    if args.texture_format == "rgba4" and args.texture_size > 256:
        parser.error("RGBA4 above 256x256 cannot fit the 512 KiB banner budget")

    pycgfx = _load_pycgfx(args.pycgfx_root)
    cgfx = build_cgfx(
        args.in_glb,
        pycgfx,
        texture_size=args.texture_size,
        texture_format=args.texture_format,
        matte=args.matte,
        ambient=args.light_ambient,
        diffuse=args.light_diffuse,
    )

    output = pycgfx.write(cgfx)
    args.out_cgfx.write_bytes(output)

    limit = 512 * 1024
    print(
        f"{args.out_cgfx}: {len(output)} bytes "
        f"({args.texture_size}x{args.texture_size} {args.texture_format.upper()}, "
        f"CMDL COMMON)"
    )
    if len(output) > limit:
        print(f"ERROR: CGFX is larger than {limit} bytes")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
