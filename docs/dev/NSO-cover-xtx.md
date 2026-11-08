# NSO covers: `.xtx.z` (NX texture) and plain PNG

Status words: PROVEN = reproduced by code on real files; LIKELY = consistent with data, not run on a console;
UNKNOWN = no sample or no test. Code and tests: `scratch/nso/cover-xtx/`.

## 1. Which cover format each NSO app uses (official apps unpacked and checked)

The `titlesdb` entry holds only path strings (`"cover": "/titles/<id>/<id>.png"`, `"details_screen": ...`).
The extension in the path decides the format.

| System | Official app (PROVEN, unpacked romfs) | Third-party mods | Hub should write |
|---|---|---|---|
| NES | `.xtx.z` (cover `<code>.xtx.z`, details `<code>00.xtx.z`) | - | `.xtx.z` (section 3) |
| SNES | `.xtx.z` (`<id>.xtx.z`, `<id>-details.xtx.z`) | - | `.xtx.z` |
| Game Boy / GBC | `.png` (`<id>.png`, `<id>-details.png`) | - | `.png` |
| GBA | `.png` | - | `.png` |
| N64 | `.png` | 54-title mod: `.png` | `.png` |
| Genesis | official NSZ not unpacked (UNKNOWN) | Zouloo mod 77 titles: `.xtx.z`; Ckau mod 280 titles: `.png` (all 280, plus its "standard boxarts" set) | `.png` (LIKELY, see below), `.xtx.z` as fallback |

Genesis PNG argument: both Ckau roots and the N64 mod contain only `atmosphere/contents/<tid>/romfs/...` (no `exefs`,
no IPS). A romfs-only mod cannot add a PNG decoder, so the Genesis app must decode PNG itself. This is LIKELY, not
run by us on a console. Both formats are in the field for Genesis.

### Image sizes (PROVEN from files)
| System | cover | details |
|---|---|---|
| NES (official xtx) | 355..359 x 512 (portrait) | 400x300 (20/20) |
| SNES (official xtx) | 512 x 357 (19 of 20, landscape) and 283 x 512 | 400x300 (20/20) |
| GB / GBA (official png) | 512x512, or 512 x 326..511 and 509..511 x 512; RGBA (type 6) and RGB (type 2) both | 400x300 (also 240x160 once) |
| N64 (official png) | 512 x 357..415; RGB and RGBA | 400x300 (408x306 once) |
| Genesis Zouloo xtx | 191..384 x 264..512 | 400x300 (77/77) |
| Genesis Ckau png | 341x512 RGB | 320x240 (275), 400x300 (4), 320x224 (1) |

The largest side is 512 in every set; the app scales to its layout. Recommendation: scale the cover so the larger side
is 512 (keep aspect); details 400x300 (letterbox if the source is not 4:3). Write 8-bit RGB or RGBA PNG (stb_image_write).
JPEG, palette and 16-bit PNG: UNKNOWN, do not use. The Hub tree already has `stb_image.h`, `stb_image_write.h` and
`stb_image_resize2.h` (`sphaira/source/image.cpp`).

## 2. XTX container (PROVEN: 80/80 official NES+SNES files and 153/154 Zouloo files are byte-exact)

Little endian. `.xtx.z` = zlib stream of the XTX file (official: `78 9C`, Zouloo: `78 DA`). Every file has 3 blocks.
The DFvN/HBvN layout and format 0x25 (RGBA8) also match the public XTX tools (LIKELY, from memory, not re-checked).

```
0x000  "DFvN"  u32 hdrSize=0x10  u32 major=1  u32 minor=1            file header (16)
0x010  block A  "HBvN" header (0x24)  dataSize=0x78  dataOffset=0x24  type=2 (texture info)  gIdx=0 inc=0
0x034  texture info (0x78 bytes)
0x0AC  block B  "HBvN" header (0x24)  dataSize=D     dataOffset=0x154 type=3 (pixel data)    gIdx=1 inc=0
0x0D0  zero pad up to 0x200            (block B data starts at blockB + dataOffset = 0x200)
0x200  swizzled pixels, all mips back to back, D bytes
0x200+D  block C "HBvN" header: dataSize=0x18 dataOffset=0x24 type=5 (end)  gIdx=2 inc=0
         + 24 bytes payload (6 x u32): 0,0,1,1,0,0
```
Block header (36 bytes): `"HBvN"`, u32 0x24, u64 dataSize, u64 dataOffset (from block start), u32 type, u32 gIdx, u32 inc.

Texture info (offset from 0x34):
```
+0x00 u64 imageSize   official files: = D (total mip data size);  Zouloo PC tool: = W*H*4.  Both variants exist.
+0x08 u32 alignment   = 0x200
+0x0C u32 width       +0x10 u32 height   +0x14 u32 depth=1   +0x18 u32 target=1 (2D)
+0x1C u32 format=0x25 (NVN RGBA8 unorm)   +0x20 u32 mipCount=6   +0x24 u32 sliceSize = same value as imageSize
+0x28 u32 mipOffset[17]  (byte offset of each mip inside the pixel data; unused entries 0)
+0x6C u32 blockHeightLog2 = 4 (16 GOBs)   +0x70 u32 0x00010007   +0x74 u32 0
```
D = sum of the mip sizes. LIKELY (one Zouloo sample, 191x264): D is rounded up to a multiple of 0x400. For mip-5
height above 8 (H >= 288; true for 341x512, 512x357, 400x300) every mip size is already a multiple of 0x400, so the rule
never fires for the recommended sizes.

Pixel format: RGBA8, bytes R,G,B,A per pixel (PROVEN). No BC1/BC3, no lossy step.

### Block-linear swizzle (Tegra X1, 4 bytes per pixel) - PROVEN
GOB = 64 bytes x 8 rows = 512 bytes. For pixel (x,y), `xb = x*4`:
```
gob_in  = (xb%64)/32*256 + ((y%8)/2)*64 + ((xb%32)/16)*32 + (y%2)*16 + (xb%16)     // byte inside the GOB
wGob    = ceil(W*4/64)
off     = ((y/(8*bh)) * wGob + xb/64) * (512*bh) + ((y/8) % bh) * 512 + gob_in      // bytes off..off+3 = R,G,B,A
levelSize = wGob * ceil(H/(8*bh)) * bh * 512
```
Block height per mip: start at 16, `while (bh > 1 && bh/2 >= ceil(H_mip/8)) bh /= 2`. Mip i: `W_i = max(1, W>>i)`,
`H_i = max(1, H>>i)`. Mips are stored one after another: `mipOffset[i+1] = mipOffset[i] + levelSize(i)`.
Pad bytes (right of the image, below it) are zero in 233 of 234 files; the app ignores them.

## 3. Reference encoder (`xtx.py`, `png2xtx.py`)
`encode(img_RGBA)`: 6 mips (box filter), swizzle each level with the rule above, header as in section 2 (official
variant), `zlib.compress(level 9)`. A C++ port is a pixel loop (about 40 lines) plus `compress2()` from zlib.

Tests (all run from `scratch/nso/cover-xtx/`; they read the original files from the PC):
- `test_roundtrip.py <dirs>`: decode -> encode with the file's own mips equals the raw XTX byte for byte for 80/80 official
  NES+SNES files and 153/154 Zouloo files (the odd one, `M-6315_e.xtx.z`, differs only in 3890 non-zero pad bytes).
- `test_png_roundtrip.py <names>`: original -> mip-0 PNG -> `png2xtx` gives the same size, header, layout and mip-0
  pixels (3 covers of different sizes + 1 details, Zouloo set). Mips 1-5 differ: the original tools used another resampler
  (mean error about 1-2 levels vs. my box filter on an official file; the Zouloo tool's mip 4 even has an empty second GOB
  column). Mip content is not reproducible; whether the app samples mips 1-5 is UNKNOWN.
- zlib bytes: Python zlib levels 1/6/9 do not reproduce the stored `.z` bytes (another deflate encoder was used).
  Irrelevant for the format; a standard zlib stream is LIKELY accepted (not run on a console).
- Ckau mod has zero `.xtx.z` files (it is all PNG), so there is no Ckau xtx round trip to run.
- Official-app unpack used the PC tools; the NSP tickets hold the title key in the raw 16 bytes at 0x180 (no key printed
  or stored in the repo). `survey2.py` lists format/size of every image in a romfs tree.

## 4. Open points (need the console)
- UNKNOWN: `mipCount = 1` accepted? (all 234 samples have 6; keep 6).
- UNKNOWN: Genesis official cover format (NSZ not unpacked); PNG on Genesis is LIKELY via the Ckau mod.
- UNKNOWN: JPEG or palette PNG; sizes outside the ranges above.
- Nothing here needs a signing key: the Hub writes plain files into LayeredFS.
