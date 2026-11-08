"""Regenerates the expected-output fixtures for tests/test_nso_*.cpp from the PROVEN python reference
converters in scratch/nso/ (local research tree, not in git). Run:  python -I -X utf8 make_fixtures.py < /dev/null
The ROMs are synthetic (xorshift32 bytes, same generator as in the C++ tests); only small outputs are stored,
big ones as `name size crc32` lines in expect.txt.  Real data used: official NES/SNES v0 titlesdb files,
an N64 mod titlesdb (trimmed), and the .dtz payload of mod game N-4435_e."""
import glob
import importlib.util
import json
import os
import sys
import zlib

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SCR = os.path.abspath(os.path.join(HERE, '..', '..', '..', 'scratch', 'nso'))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.dirname(path))
    spec.loader.exec_module(m)
    return m


nes = load('nes_snes_conv', SCR + '/nes-snes/nsoconv.py')
gen = load('gen_conv', SCR + '/genesis-gb-gba/nsoconv.py')
n64 = load('n64_conv', SCR + '/n64-vb/n64_entry.py')
sys.path.insert(0, SCR + '/cover-xtx')
from xtx import encode  # noqa: E402


def xorshift(seed, n):
    x = seed
    out = bytearray()
    for _ in range(n):
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        out.append((x >> 16) & 0xFF)
    return bytearray(out)


def W(name, data):
    open(os.path.join(HERE, name), 'wb').write(data)


expect = []


def E(name, data):
    expect.append('%s %d %08x' % (name, len(data), zlib.crc32(data)))


# ---- SNES
snes = xorshift(1, 32768)
snes[0x7FDC:0x7FE0] = bytes([0x34, 0x12, 0xCB, 0xED])
E('sfrom', nes.snes_wrap(bytes(snes)))
# ---- NES (header bytes 8..15 junk)
nesrom = xorshift(2, 24592)
nesrom[0:8] = b'NES\x1a\x01\x01\x01\x00'
nesrom[8:16] = b'DiskDude'
E('nes', nes.nes_wrap(bytes(nesrom)))
# ---- Genesis SMD
g = xorshift(3, 65536)
g[0x100:0x104] = b'SEGA'
smd = gen.bin_to_smd(bytes(g))
assert gen.smd_to_bin(smd) == bytes(g)
E('smd_to_bin', bytes(g))
E('smd', smd)
W('smd.bin', smd)
# ---- N64
z = xorshift(4, 8192)
z[0:4] = bytes.fromhex('80371240')
z[0x10:0x14] = bytes.fromhex('AABBCCDD')
z[0x14:0x18] = bytes.fromhex('11223344')
z[0x3B:0x3F] = b'NXYE'
E('z64', bytes(z))
E('v64', n64.to_v64(bytes(z)))
E('n64', n64.to_n64(bytes(z)))
zp = bytearray(z)
zp[0x3E] = ord('P')
E('z64_pal', bytes(zp))

# ---- XTX covers (pattern is reproduced in the C++ test)
for (w, h) in ((48, 40), (37, 23)):
    img = np.zeros((h, w, 4), np.uint8)
    for y in range(h):
        for x in range(w):
            img[y, x] = ((x * 7 + y * 3) & 255, (x * y) & 255, (x ^ (y * 5)) & 255, 255 if (x + y) % 5 else (x * 13) & 255)
    W('xtx_%dx%d.bin' % (w, h), encode(img, official=True))

# ---- N64 cpio of the mod game N-4435_e
dz = glob.glob('D:/Downloads/switch/Nintendo 64*/54*/atmosphere/contents/0100C9A00ECE6000/romfs/titles/N-4435_e/N-4435_e.dtz')[0]
raw = zlib.decompress(open(dz, 'rb').read())
ents, _ = n64.n64cpio.parse(raw)
assert ents[0][8] == 1636711955 and ents[1][8] == 1636711992 and ents[0][0].startswith('Hybrid Heaven (USA)')
W('n64_4435.cpio', raw)
W('n64_4435.meta', ents[0][9])
W('n64_4435.cfg', ents[1][9])

# ---- entries
T = 'Test "Game" \\ \t\u00a9 \u30fc'
PUB = 'Pub Co'
REL = '1994-05-06'
LC = '2031-02-03'
ent = {}
c, e = nes.snes_entry(3123, 'S-3123_e', T, PUB, REL, 2, True, LC)
ent['snes'] = e
c, e = nes.nes_entry('CLV-P-NABCE', 'CLV-P-NABCE', T, PUB, REL, 2, True, LC)
ent['nes'] = e
ent['n64'] = n64.make_entry('N-4435_e', T, PUB, REL, 2, False, 1, 80, False, 1, LC)
gb_dmg = bytearray(xorshift(5, 0x200)); gb_dmg[0x143] = 0
gb_cgb = bytearray(xorshift(6, 0x200)); gb_cgb[0x143] = 0x80
ent['genesis'] = gen.build_entry('genesis', b'', 'M-1234_e', T, PUB, '\u00a9' + PUB, REL, LC, players=2, simultaneous=True, rom_ext='gen')
ent['gb_dmg'] = gen.build_entry('gb', bytes(gb_dmg), 'D-4321_e', T, PUB, '\u00a9' + PUB, REL, LC, players=2)
ent['gb_cgb'] = gen.build_entry('gb', bytes(gb_cgb), 'C-4321_e', T, PUB, '\u00a9' + PUB, REL, LC, players=2)
gba = bytearray(xorshift(7, 0x200))
ent['gba'] = gen.build_entry('gba', bytes(gba), 'A-ABCD_e', T, PUB, '\u00a9' + PUB, REL, LC, players=2, simultaneous=True)
# Virtual Boy: no sample mod; same common fields as the official entries (LIKELY)
vb = dict(code='V-1234_e', copyright='\u00a9' + PUB, cover='/titles/V-1234_e/V-1234_e.png',
          details_screen='/titles/V-1234_e/V-1234_e-details.png', lcla6_release_date=LC, players_count=2,
          publisher=PUB, release_date=REL, rewind_interval=1.5, save_count=1, sort_publisher=PUB.lower(),
          sort_title=T.lower(), title=T, title_ko=T, rom='/titles/V-1234_e/V-1234_e.vb', volume=100,
          title_zhHans='\u30fc', title_zhHant='\u30fc')
ent['vb'] = {k: vb[k] for k in sorted(vb)}
for k, e in ent.items():
    W('entry_%s.json' % k, json.dumps(e, ensure_ascii=False, separators=(',', ':')).encode('utf8'))

# ---- titlesdb texts (real formatting)
WK = SCR + '/nes-snes/work'
for name, src in (('db_nes_v0.json', WK + '/nes_romfs_base/titles/lclassics.titlesdb'),
                  ('db_snes_v0.json', WK + '/snes_romfs_base/titles/lclassics.titlesdb')):
    b = open(src, 'rb').read()
    W(name, b)
    d = json.loads(b.decode('utf8'))['titles']
    codes = [x['code'] for x in d] if isinstance(d, list) else list(d)
    W(name + '.codes', ('\n'.join(codes) + '\n').encode())
up = open(WK + '/nes_romfs_upd/titles/lclassics.titlesdb', 'rb').read().decode('utf8')
dd = json.loads(up)
for ind in (2, 4, '\t'):
    if json.dumps(dd, indent=ind, ensure_ascii=False) == up:
        break
else:
    ind = None
print('nes upd indent', repr(ind))
small = {'titles': dict(list(dd['titles'].items())[:4])}
if ind is not None:
    t = json.dumps(small, indent=ind, ensure_ascii=False).encode('utf8')
else:
    t = json.dumps(small, indent=2, ensure_ascii=False).encode('utf8')
W('db_nes_upd_small.json', t)
W('db_nes_upd_small.json.codes', ('\n'.join(small['titles']) + '\n').encode())
mod = glob.glob('D:/Downloads/switch/Nintendo 64*/54*/atmosphere/contents/0100C9A00ECE6000/romfs/titles/lclassics.titlesdb')[0]
mraw = open(mod, 'rb').read()
md = json.loads(mraw.decode('utf8'))
assert json.dumps(md, separators=(',', ':'), ensure_ascii=False).encode('utf8') == mraw
msmall = {'titles': dict(list(md['titles'].items())[:4])}
W('db_n64_small.json', json.dumps(msmall, separators=(',', ':'), ensure_ascii=False).encode('utf8'))
W('db_n64_small.json.codes', ('\n'.join(msmall['titles']) + '\n').encode())

open(os.path.join(HERE, 'expect.txt'), 'w').write('\n'.join(expect) + '\n')
print('\n'.join(expect))
