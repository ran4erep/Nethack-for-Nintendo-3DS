/* NetHack 5.0	3dspng.c */
/* Nintendo 3DS tileset loader: self-contained PNG reader.
 *
 * devkitPro ships no libpng/zlib for the ARM target, so win/share/pngtiles.c
 * cannot be compiled for the 3DS.  Instead this file supplies the
 * read_png_tiles() entry point that win/share/tileset.c calls when USE_PNG is
 * defined.  It decodes exactly the subset the tile renderer needs:
 *
 *   - 8-bit samples, non-interlaced, no compression method other than 0
 *   - colour type 3 (palette), 2 (RGB), 6 (RGBA), 0 (grey), 4 (grey+alpha)
 *
 * The image is returned through the usual struct TileSetImage fields: pixels
 * hold the colours and indexes hold per-pixel palette indexes.  For non-palette
 * images an index buffer is synthesised (0 for fully transparent pixels, 1
 * otherwise) because the 3DS cache in 3dstiles.c keys transparency off index 0.
 *
 * DEFLATE is inflated with the public-domain "puff" reference decoder by Mark
 * Adler (zlib, contrib/puff), vendored in puff.c/puff.h next to this file.
 *
 * NetHack may be freely redistributed.  See license for details.
 */

#include "config.h"
#include "tileset.h"
#include "puff.h"

#ifdef NINTENDO_3DS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Upper bound for the on-disk file and for the inflated scanline buffer.  The
   bundled PixelHack tileset is ~2.4 MB raw; keep generous headroom. */
#define PNG_MAX_FILE (32UL * 1024UL * 1024UL)

/* Big-endian 32-bit read from a PNG chunk header/field. */
static unsigned long
png_u32(const unsigned char *p)
{
    return ((unsigned long) p[0] << 24) | ((unsigned long) p[1] << 16)
           | ((unsigned long) p[2] << 8) | (unsigned long) p[3];
}

/* PNG filter type 4 predictor. */
static int
png_paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);

    if (pa <= pb && pa <= pc)
        return a;
    if (pb <= pc)
        return b;
    return c;
}

static unsigned char *
png_slurp(const char *filename, size_t *out_len)
{
    FILE *fp = fopen(filename, "rb");
    unsigned char *buf;
    long len;
    size_t got;

    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0 || (len = ftell(fp)) <= 0
        || (unsigned long) len > PNG_MAX_FILE) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    buf = (unsigned char *) alloc((unsigned int) len);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    got = fread(buf, 1, (size_t) len, fp);
    fclose(fp);
    if (got != (size_t) len) {
        free(buf);
        return NULL;
    }
    *out_len = got;
    return buf;
}

boolean
read_png_tiles(const char *filename, struct TileSetImage *image)
{
    static const unsigned char sig[8] =
        { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    unsigned char *file = NULL, *idat = NULL, *raw = NULL;
    size_t filelen = 0, idatlen = 0;
    unsigned long width = 0, height = 0;
    int bitdepth = 0, colortype = 0, compression = 0, method = 0;
    int interlace = 0, channels = 0, bpp = 0, have_plte = 0;
    struct Pixel pal[256];
    size_t pos, rowbytes, rawlen, npix;
    unsigned long destlen, srclen;
    unsigned x, y;
    int i, err;
    boolean ok = FALSE;

    memset(pal, 0, sizeof pal);
    for (i = 0; i < 256; i++)
        pal[i].a = 255;

    image->width = image->height = 0;
    image->pixels = NULL;
    image->indexes = NULL;
    image->image_desc = NULL;
    image->tile_width = image->tile_height = 0;

    file = png_slurp(filename, &filelen);
    if (!file || filelen < 8 || memcmp(file, sig, 8) != 0)
        goto done;

    /* IDAT is a subset of the whole file, so one buffer of that size works. */
    idat = (unsigned char *) alloc((unsigned int) filelen);
    if (!idat)
        goto done;

    /* Walk the chunk list. */
    pos = 8;
    while (pos + 12 <= filelen) {
        unsigned long clen = png_u32(file + pos);
        const unsigned char *ctype = file + pos + 4;
        const unsigned char *cdata = file + pos + 8;

        if (clen > (unsigned long) (filelen - pos - 12))
            goto done; /* truncated, or a length that overruns the file */

        if (!memcmp(ctype, "IHDR", 4)) {
            if (clen != 13)
                goto done;
            width = png_u32(cdata);
            height = png_u32(cdata + 4);
            bitdepth = cdata[8];
            colortype = cdata[9];
            compression = cdata[10];
            method = cdata[11];
            interlace = cdata[12];
        } else if (!memcmp(ctype, "PLTE", 4)) {
            unsigned n = (unsigned) (clen / 3);

            if (n > 256)
                n = 256;
            for (i = 0; i < (int) n; i++) {
                pal[i].r = cdata[3 * i];
                pal[i].g = cdata[3 * i + 1];
                pal[i].b = cdata[3 * i + 2];
                pal[i].a = 255;
            }
            have_plte = 1;
        } else if (!memcmp(ctype, "tRNS", 4)) {
            unsigned n = (unsigned) clen;

            if (n > 256)
                n = 256;
            for (i = 0; i < (int) n; i++)
                pal[i].a = cdata[i];
        } else if (!memcmp(ctype, "IDAT", 4)) {
            if (idatlen + (size_t) clen > filelen)
                goto done;
            memcpy(idat + idatlen, cdata, (size_t) clen);
            idatlen += (size_t) clen;
        } else if (!memcmp(ctype, "IEND", 4)) {
            break;
        }
        pos += 12 + (size_t) clen;
    }

    if (width == 0 || height == 0 || idatlen < 2)
        goto done;
    if (bitdepth != 8 || compression != 0 || method != 0 || interlace != 0)
        goto done; /* only the plain 8-bit, non-interlaced subset */
    switch (colortype) {
    case 0: channels = 1; break; /* grey */
    case 2: channels = 3; break; /* truecolour */
    case 3: channels = 1; break; /* palette */
    case 4: channels = 2; break; /* grey + alpha */
    case 6: channels = 4; break; /* truecolour + alpha */
    default:
        goto done;
    }
    if (colortype == 3 && !have_plte)
        goto done;
    bpp = channels;

    if (width > 8192 || height > 8192)
        goto done;
    rowbytes = (size_t) width * (size_t) bpp;
    rawlen = (rowbytes + 1) * (size_t) height;
    if (rawlen / (rowbytes + 1) != (size_t) height) /* overflow */
        goto done;
    if (rawlen > PNG_MAX_FILE)
        goto done;
    npix = (size_t) width * (size_t) height;

    raw = (unsigned char *) alloc((unsigned int) rawlen);
    if (!raw)
        goto done;

    /* PNG IDAT is a zlib stream: skip the 2-byte header and let puff() decode
       the raw DEFLATE data that follows (it stops at the final block and the
       Adler-32 trailer, which puff() does not need). */
    destlen = (unsigned long) rawlen;
    srclen = (unsigned long) (idatlen - 2);
    err = puff(raw, &destlen, idat + 2, &srclen);
    if (err != 0 || destlen != (unsigned long) rawlen)
        goto done;

    /* Undo the per-scanline filters, in place. */
    for (y = 0; y < height; y++) {
        unsigned char *cur = raw + (size_t) y * (rowbytes + 1) + 1;
        unsigned char *prev = (y > 0)
                                  ? raw + (size_t) (y - 1) * (rowbytes + 1) + 1
                                  : NULL;
        int ft = raw[(size_t) y * (rowbytes + 1)];

        for (x = 0; x < (unsigned) rowbytes; x++) {
            int a = (x >= (unsigned) bpp) ? cur[x - bpp] : 0;
            int b = prev ? prev[x] : 0;
            int c = (prev && x >= (unsigned) bpp) ? prev[x - bpp] : 0;

            switch (ft) {
            case 0: /* none */
                break;
            case 1: /* sub */
                cur[x] = (unsigned char) (cur[x] + a);
                break;
            case 2: /* up */
                cur[x] = (unsigned char) (cur[x] + b);
                break;
            case 3: /* average */
                cur[x] = (unsigned char) (cur[x] + ((a + b) >> 1));
                break;
            case 4: /* paeth */
                cur[x] = (unsigned char) (cur[x] + png_paeth(a, b, c));
                break;
            default:
                goto done;
            }
        }
    }

    image->width = width;
    image->height = height;
    image->pixels =
        (struct Pixel *) alloc((unsigned int) (npix * sizeof(struct Pixel)));
    image->indexes = (unsigned char *) alloc((unsigned int) npix);
    if (!image->pixels || !image->indexes)
        goto done;
    for (i = 0; i < 256; i++)
        image->palette[i] = pal[i];

    for (y = 0; y < height; y++) {
        const unsigned char *row = raw + (size_t) y * (rowbytes + 1) + 1;
        size_t base = (size_t) y * width;

        for (x = 0; x < width; x++) {
            size_t di = base + x;
            const unsigned char *sp = row + (size_t) x * bpp;

            switch (colortype) {
            case 3:
                image->indexes[di] = sp[0];
                image->pixels[di] = pal[sp[0]];
                break;
            case 2:
                image->pixels[di].r = sp[0];
                image->pixels[di].g = sp[1];
                image->pixels[di].b = sp[2];
                image->pixels[di].a = 255;
                image->indexes[di] = 1;
                break;
            case 6:
                image->pixels[di].r = sp[0];
                image->pixels[di].g = sp[1];
                image->pixels[di].b = sp[2];
                image->pixels[di].a = sp[3];
                image->indexes[di] = (sp[3] == 0) ? 0 : 1;
                break;
            case 4:
                image->pixels[di].r = sp[0];
                image->pixels[di].g = sp[0];
                image->pixels[di].b = sp[0];
                image->pixels[di].a = sp[1];
                image->indexes[di] = (sp[1] == 0) ? 0 : 1;
                break;
            default: /* 0: grey */
                image->pixels[di].r = sp[0];
                image->pixels[di].g = sp[0];
                image->pixels[di].b = sp[0];
                image->pixels[di].a = 255;
                image->indexes[di] = 1;
                break;
            }
        }
    }

    ok = TRUE;

done:
    if (file)
        free(file);
    if (idat)
        free(idat);
    if (raw)
        free(raw);
    if (!ok) {
        if (image->pixels) {
            free(image->pixels);
            image->pixels = NULL;
        }
        if (image->indexes) {
            free(image->indexes);
            image->indexes = NULL;
        }
    }
    return ok;
}

#endif /* NINTENDO_3DS */
