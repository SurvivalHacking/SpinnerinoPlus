#!/usr/bin/env python3
"""
ROM converter for Mario Bros (Nintendo 1983) — GALAGONE arcade emulator
Generates header files from mario.zip ROM set (MAME format).

Hardware:
  Main CPU: Z80 @ 4MHz + Z80 DMA (I/O port 0x00)
  Sound CPU: I8035 (MCS-48 family, external 4K ROM)
  Video: 256x224 (portrait), 8x8 2bpp tiles (512), 16x16 3bpp sprites (256)
  Colors: 256-entry palette from 512-byte PROM

Memory map:
  0x0000-0x5FFF  ROM (24K)
  0x6000-0x67FF  RAM (2K)
  0x6800-0x6FFF  RAM (2K, battery-backed in original)
  0x7000-0x73FF  Sprite RAM (1K)
  0x7400-0x77FF  Video RAM (1K, tile codes only — color from tile_code>>5)
  0x7C00         IN0 read / walk sample write (discrete analog, ignored)
  0x7C80         IN1 read / walk sample write (discrete analog, ignored)
  0x7D00         Scroll Y write
  0x7E00         Sound command latch (read by I8035 via tune_r when P2 bit7=1)
  0x7E80-0x7E87  LS259 mainlatch write (gfx_bank, palette_bank, flip, NMI mask, DMA)
  0x7F00-0x7F07  LS259 sample triggers: 0=INT(death), 1-2=T0/T1, 3-6=port P1 bits, 7=skid
  0x7F80         DSW DIP switches read
  0xF000-0xFFFF  ROM (4K high)

MAME tile GFX layout:
  8x8 2bpp, planes {512*8*8, 0}, xoffs {0,1,2,3,4,5,6,7}
  Tile color = 8 + (tile_code >> 5) + 16 * palette_bank, granularity 8

MAME sprite GFX layout:
  16x16 3bpp, planes {2*256*16*16, 256*16*16, 0}
  xoffs {0..7, 256*16*8+0..256*16*8+7}, yoffs {0..15}*8
  charincrement = 16*8 = 128 bits
"""

import sys, os, zipfile

# ── Paths ──
SEARCH = [
    os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'),
    os.path.expanduser('~/Desktop'),
    os.path.expanduser('~/Desktop/GALAGONE MARCO'),
    '.',
]

def find_zip():
    for d in SEARCH:
        for name in ('mario.zip', 'mariobros.zip'):
            p = os.path.join(d, name)
            if os.path.exists(p):
                return p
    if len(sys.argv) > 1 and os.path.exists(sys.argv[1]):
        return sys.argv[1]
    return None

def load(z, name, expected=None):
    names_lc = {n.lower(): n for n in z.namelist()}
    for try_n in [name, names_lc.get(name.lower(), name)]:
        try:
            d = z.read(try_n)
            if expected and len(d) != expected:
                print(f"  WARN: {name} is {len(d)}B, expected {expected}")
            print(f"  {name}: {len(d)}B")
            return d
        except KeyError:
            continue
    print(f"  ERROR: {name} not found!")
    sys.exit(1)

def read_bit(data, pos):
    return (data[pos >> 3] >> (pos & 7)) & 1

def rgb565_swap(r, g, b):
    c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return ((c >> 8) & 0xFF) | ((c & 0xFF) << 8)

def decode_color(prom, idx):
    """Decode one palette entry from Mario Bros color PROM.
    MAME mario_decode_info: ALL components from SAME byte (offset 0).
    shifts: R=5, G=2, B=0.  masks: R=0x07, G=0x07, B=0x03.
    So: R = bits 7-5, G = bits 4-2, B = bits 1-0 of prom[idx].
    """
    v = prom[idx]
    r_raw = (v >> 5) & 0x07  # bits 7,6,5
    g_raw = (v >> 2) & 0x07  # bits 4,3,2
    b_raw = (v >> 0) & 0x03  # bits 1,0
    # Resistor network weights (Darlington for R/G, Emitter for B)
    r = 0x21 * (r_raw & 1) + 0x47 * ((r_raw >> 1) & 1) + 0x97 * ((r_raw >> 2) & 1)
    g = 0x21 * (g_raw & 1) + 0x47 * ((g_raw >> 1) & 1) + 0x97 * ((g_raw >> 2) & 1)
    b = 0x55 * (b_raw & 1) + 0xAA * ((b_raw >> 1) & 1)
    # MAME uses RES_NET_MONITOR_SANYO_EZV20 → inverted output for Nintendo monitor
    r = 255 - r
    g = 255 - g
    b = 255 - b
    return r, g, b

# ── Tile decode: 8x8 2bpp (MAME charlayout) ──
# planes: {512*8*8, 0}, xoffs: {0,1,2,3,4,5,6,7}, yoffs: {0*8..7*8}, step=64
def decode_tiles(gfx1):
    """512 tiles, each 8x8 2bpp."""
    half = len(gfx1) // 2  # 4096 bytes
    half_bits = half * 8    # 32768 bits = 512*8*8
    tiles = []
    num = 512
    for t in range(num):
        base = t * 64  # charincrement = 8*8 = 64 bits
        rows = []
        for y in range(8):
            packed = 0
            for x in range(8):
                bit_x = x
                b0 = read_bit(gfx1, base + half_bits + y * 8 + bit_x)  # planes[0] → bit 1 (MSB)
                b1 = read_bit(gfx1, base + 0          + y * 8 + bit_x)  # planes[1] → bit 0 (LSB)
                pixel = (b0 << 1) | b1  # MAME: planes[0] = MSB
                packed |= (pixel << (x * 2))
            rows.append(packed)
        tiles.append(rows)
    return tiles

# ── Sprite decode: 16x16 3bpp (MAME spritelayout) ──
# planes: {2*256*16*16, 256*16*16, 0} = {131072, 65536, 0} bits
# xoffs: {0..7, 256*16*8+0..256*16*8+7} = {0..7, 32768..32775}
# yoffs: {0*8, 1*8, ..., 15*8} = {0, 8, ..., 120}
# charincrement = 16*8 = 128 bits
def decode_sprites(gfx2):
    """256 sprites, each 16x16 3bpp.
    Returns list of sprites, each = list of 16 rows,
    each row = (left_word, right_word) tuple of 24-bit values in unsigned long.
    Left word = pixels 0-7 (pixel 0 at bits 0-2), right word = pixels 8-15.
    Same 2×unsigned long format as Green Beret (proven working on ESP32-P4).
    """
    plane_off = [2 * 256 * 16 * 16, 256 * 16 * 16, 0]  # [131072, 65536, 0] bits
    right_half = 256 * 16 * 8  # 32768 bits
    xoffs = list(range(8)) + [right_half + i for i in range(8)]
    yoffs = [i * 8 for i in range(16)]
    step = 16 * 8  # 128 bits per sprite (charincrement)
    num = 256

    sprites = []
    for s in range(num):
        base = s * step
        rows = []
        for y in range(16):
            left_word = 0   # pixels 0-7
            right_word = 0  # pixels 8-15
            for x in range(16):
                pixel = 0
                for p in range(3):
                    bp = base + plane_off[p] + yoffs[y] + xoffs[x]
                    if read_bit(gfx2, bp):
                        pixel |= (1 << (2 - p))  # MAME: planes[0] → MSB (bit 2)
                if x < 8:
                    left_word |= (pixel << (x * 3))
                else:
                    right_word |= (pixel << ((x - 8) * 3))
            rows.append((left_word, right_word))
        sprites.append(rows)
    return sprites

def make_flipped_sprites(sprites):
    """Generate 4 flip variants: [normal, flipX, flipY, flipXY].
    Each row = (left_word, right_word) with 8 pixels × 3bpp each.
    """
    def reverse_word(w):
        """Reverse 8 pixels within a 24-bit word."""
        out = 0
        for i in range(8):
            px = (w >> (i * 3)) & 7
            out |= (px << ((7 - i) * 3))
        return out

    all_variants = [[], [], [], []]
    for spr in sprites:
        # Variant 0: normal
        all_variants[0].append(spr)
        # Variant 1: flipX — swap halves + reverse within each half
        fx = []
        for (lw, rw) in spr:
            fx.append((reverse_word(rw), reverse_word(lw)))
        all_variants[1].append(fx)
        # Variant 2: flipY — reverse row order
        all_variants[2].append(list(reversed(spr)))
        # Variant 3: flipXY — both
        fxy = []
        for (lw, rw) in reversed(spr):
            fxy.append((reverse_word(rw), reverse_word(lw)))
        all_variants[3].append(fxy)
    return all_variants

# ── Write helpers ──
def write_rom_h(path, name, data, desc):
    with open(path, 'w') as f:
        f.write(f'// {desc}\n// Auto-generated by romconv_mariobros.py\n')
        f.write(f'const unsigned char {name}[] = {{\n')
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            f.write('  ' + ','.join(f'0x{b:02x}' for b in chunk) + ',\n')
        f.write('};\n')
    print(f"  -> {os.path.basename(path)} ({len(data)} bytes)")

def write_tilemap_h(path, tiles):
    with open(path, 'w') as f:
        f.write('// Mario Bros tiles 8x8 2bpp (512 tiles)\n')
        f.write('// Auto-generated by romconv_mariobros.py\n')
        f.write(f'const unsigned short mariobros_tilemap[][8] = {{\n')
        for t in tiles:
            f.write(' { ' + ','.join(f'0x{v:04x}' for v in t) + ' },\n')
        f.write('};\n')
    print(f"  -> {os.path.basename(path)} ({len(tiles)} tiles)")

def write_spritemap_h(path, variants):
    with open(path, 'w') as f:
        f.write('// Mario Bros sprites 16x16 3bpp (256 sprites, 4 flip variants)\n')
        f.write('// Auto-generated by romconv_mariobros.py\n')
        f.write('// Format: [flip_variant][sprite_code][32 words] = 16 rows x 2 unsigned long\n')
        f.write('//   word[row*2]   = left half  (pixels 0-7,  pixel 0 at bits 0-2)\n')
        f.write('//   word[row*2+1] = right half (pixels 8-15, pixel 8 at bits 0-2)\n')
        f.write(f'const unsigned long mariobros_sprites[][{len(variants[0])}][32] = {{\n')
        for vi, var in enumerate(variants):
            f.write(' {\n')
            for si, spr in enumerate(var):
                words = []
                for (lw, rw) in spr:
                    words.append(f'0x{lw:06x}')
                    words.append(f'0x{rw:06x}')
                f.write('  { ' + ','.join(words) + ' },\n')
            f.write(' },\n')
        f.write('};\n')
    print(f"  -> {os.path.basename(path)} ({len(variants[0])} sprites x 4 flips, 2x32bit/row)")

def write_colormap_h(path, prom):
    """Generate pre-computed RGB565 color maps.

    MAME mario_video.cpp (verified from GitHub):
      Tile: color = 8 + (tile_code >> 5) + 16 * palette_bank, granularity 8
        → pen = color * 8 + pixel_value
        → tile_colormap[palette_bank][tile_code>>5][pixel 0-3]

      Sprite: color_attr = (sp[1] & 0x0F) + 16 * palette_bank
        → pen = color_attr * 8 + pixel_value
        → sprite_colormap[palette_bank][color_attr][pixel 0-7]
    """
    with open(path, 'w') as f:
        f.write('// Mario Bros color maps (RGB565 byte-swapped)\n')
        f.write('// Auto-generated by romconv_mariobros.py\n\n')

        # Tile colormap: [palette_bank(0-1)][tile_code>>5 (0-7)][pixel(0-3)]
        # pen = (8 + top3bits + 16 * palette_bank) * 8 + pixel
        f.write('// Tile colors: [palette_bank][tile_code>>5][4 pens]\n')
        f.write('// color = 8 + (tile_code >> 5) + 16 * palette_bank, granularity 8\n')
        f.write('const unsigned short mariobros_colormap[2][8][4] = {\n')
        for bank in range(2):
            f.write('  {\n')
            for top3 in range(8):
                colors = []
                color_attr = 8 + top3 + 16 * bank
                for px in range(4):
                    pen = color_attr * 8 + px
                    if pen < 256:
                        r, g, b = decode_color(prom, pen)
                    else:
                        r, g, b = 0, 0, 0
                    colors.append(rgb565_swap(r, g, b))
                f.write('    {' + ','.join(f'0x{c:04x}' for c in colors) + '},\n')
            f.write('  },\n')
        f.write('};\n\n')

        # Sprite colormap: [palette_bank(0-1)][color_attr(0-15)][pixel(0-7)]
        # pen = (color_attr + 16 * palette_bank) * 8 + pixel
        f.write('// Sprite colors: [palette_bank][color_attr][8 pens]\n')
        f.write('const unsigned short mariobros_colormap_sprite[2][16][8] = {\n')
        for bank in range(2):
            f.write('  {\n')
            for attr in range(16):
                colors = []
                color_attr = attr + 16 * bank
                for px in range(8):
                    pen = color_attr * 8 + px
                    if pen < 256:
                        r, g, b = decode_color(prom, pen)
                    else:
                        r, g, b = 0, 0, 0
                    colors.append(rgb565_swap(r, g, b))
                f.write('    {' + ','.join(f'0x{c:04x}' for c in colors) + '},\n')
            f.write('  },\n')
        f.write('};\n')
    print(f"  -> {os.path.basename(path)} (tile 2x8x4 + sprite 2x16x8)")

def write_logo_h(path, out_dir):
    """Generate TFT menu logo (224x96 RLE) + SD .bin from image file.
    Also generates OLED logo (128x64 XBM) and flyer SD .bin.
    Source images searched in common locations.
    """
    from PIL import Image, ImageChops

    # Search for source images
    img_search = [
        os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'),
        os.path.expanduser('~/Desktop/GALAGHINO V4'),
        os.path.expanduser('~/Desktop/GALAGONE MARCO'),
        os.path.expanduser('~/Desktop'),
    ]
    tft_img_path = None
    flyer_img_path = None
    for d in img_search:
        for name in ('mario bross tft.jpg', 'mario_bros_tft.jpg', 'mariobros_tft.jpg'):
            p = os.path.join(d, name)
            if os.path.exists(p):
                tft_img_path = p
                break
        for name in ('mario bross flyer.png', 'mario_bros_flyer.png', 'mariobros_flyer.png'):
            p = os.path.join(d, name)
            if os.path.exists(p):
                flyer_img_path = p
                break

    def rle_encode(pixels):
        used = set(pixels)
        marker = next((m for m in range(65536) if m not in used), 1)
        encoded = [marker]
        i = 0
        while i < len(pixels):
            run = 1
            while i + run < len(pixels) and pixels[i + run] == pixels[i] and run < 256:
                run += 1
            if pixels[i] == marker or run >= 3:
                encoded.extend([marker, run - 1, pixels[i]])
                i += run
            else:
                encoded.append(pixels[i])
                i += 1
        return encoded

    # === TFT MENU LOGO (224x96, RLE .h + raw .bin for SD) ===
    TFT_W, TFT_H = 224, 96
    if tft_img_path:
        print(f"  Using TFT image: {tft_img_path}")
        img = Image.open(tft_img_path).convert("RGB")
        bg = Image.new(img.mode, img.size, (0, 0, 0))
        diff = ImageChops.add(img, bg, 2.0, -30)
        bbox = diff.getbbox()
        if bbox:
            img = img.crop(bbox)
        w, h = img.size
        scale = min(TFT_W / w, TFT_H / h) * 0.63  # 30% smaller than original 0.90
        nw, nh = int(w * scale), int(h * scale)
        img = img.resize((nw, nh), Image.LANCZOS)
        canvas = Image.new("RGB", (TFT_W, TFT_H), (0, 0, 0))
        canvas.paste(img, ((TFT_W - nw) // 2, (TFT_H - nh) // 2))
    else:
        print("  WARNING: TFT image not found, generating text logo")
        canvas = Image.new("RGB", (TFT_W, TFT_H), (0, 0, 0))
        # Fallback: plain text (basic)
        from PIL import ImageDraw
        draw = ImageDraw.Draw(canvas)
        draw.text((40, 30), "MARIO BROS", fill=(255, 40, 40))

    pixels = []
    for y in range(TFT_H):
        for x in range(TFT_W):
            r, g, b = canvas.getpixel((x, y))
            pixels.append(rgb565_swap(r, g, b))

    # RLE .h file (fallback logo in flash)
    encoded = rle_encode(pixels)
    with open(path, 'w') as f:
        f.write('// Mario Bros - TFT Logo (224x96, RGB565 byte-swapped, RLE)\n')
        f.write('// Auto-generated by romconv_mariobros.py\n')
        f.write('const unsigned short mariobros_logo[] = {\n')
        for i in range(0, len(encoded), 12):
            chunk = encoded[i:i + 12]
            f.write('  ' + ', '.join(str(v) for v in chunk) + ',\n')
        f.write('};\n')
    print(f"  -> {os.path.basename(path)} ({TFT_W}x{TFT_H}, RLE={len(encoded)*2}B)")

    # RLE .bin for SD card → logo/mariobros.bin (same RLE format as .h)
    sd_logo_dir = os.path.join(out_dir, '..', '..', 'logo')
    os.makedirs(sd_logo_dir, exist_ok=True)
    sd_logo_path = os.path.join(sd_logo_dir, 'mariobros.bin')
    with open(sd_logo_path, 'wb') as f:
        for v in encoded:
            f.write(v.to_bytes(2, 'little'))
    print(f"  -> logo/mariobros.bin (RLE, {len(encoded)*2}B)")

    # === OLED LOGO (128x64 XBM) ===
    OW, OH = 128, 64
    if tft_img_path:
        oled_img = Image.open(tft_img_path).convert("L")
        if bbox:
            oled_img = oled_img.crop(bbox)
        w2, h2 = oled_img.size
        s2 = min(OW / w2, OH / h2) * 0.85
        nw2, nh2 = int(w2 * s2), int(h2 * s2)
        oled_img = oled_img.resize((nw2, nh2), Image.LANCZOS)
        oled_canvas = Image.new("L", (OW, OH), 0)
        oled_canvas.paste(oled_img, ((OW - nw2) // 2, (OH - nh2) // 2))
        oled_canvas = oled_canvas.point(lambda p: 1 if p > 80 else 0)
    else:
        oled_canvas = Image.new("L", (OW, OH), 0)

    xbm_bytes = []
    for y in range(OH):
        for xb in range(OW // 8):
            byte = 0
            for bit in range(8):
                if oled_canvas.getpixel((xb * 8 + bit, y)):
                    byte |= (1 << bit)
            xbm_bytes.append(byte)

    oled_path = os.path.join(out_dir, 'mariobros_oled_logo.h')
    with open(oled_path, 'w') as f:
        f.write('// Mario Bros OLED logo 128x64 XBM\n')
        f.write('// Auto-generated by romconv_mariobros.py\n')
        f.write(f'#define MarioBros_Logo_width {OW}\n')
        f.write(f'#define MarioBros_Logo_height {OH}\n')
        f.write('const unsigned char MarioBros_Logo_bits[] = {\n')
        for i in range(0, len(xbm_bytes), 16):
            chunk = xbm_bytes[i:i + 16]
            f.write('  ' + ','.join(f'0x{b:02x}' for b in chunk) + ',\n')
        f.write('};\n')
    print(f"  -> mariobros_oled_logo.h ({OW}x{OH} XBM)")

    # === FLYER TFT (284x76 logical after MADCTL rotation, raw RGB565) ===
    FW, FH = 284, 76
    if flyer_img_path:
        print(f"  Using flyer image: {flyer_img_path}")
        fimg = Image.open(flyer_img_path).convert("RGB")
        fw, fh = fimg.size
        fs = min(FW / fw, FH / fh) * 0.95
        fnw, fnh = int(fw * fs), int(fh * fs)
        fimg = fimg.resize((fnw, fnh), Image.LANCZOS)
        fcanvas = Image.new("RGB", (FW, FH), (0, 0, 0))
        fcanvas.paste(fimg, ((FW - fnw) // 2, (FH - fnh) // 2))
    else:
        print("  WARNING: Flyer image not found")
        fcanvas = Image.new("RGB", (FW, FH), (0, 0, 0))

    sd_flyer_dir = os.path.join(out_dir, '..', '..', 'flyer')
    os.makedirs(sd_flyer_dir, exist_ok=True)
    flyer_path = os.path.join(sd_flyer_dir, 'mariobros.bin')
    with open(flyer_path, 'wb') as f:
        for y in range(FH):
            for x in range(FW):
                r, g, b = fcanvas.getpixel((x, y))
                v = rgb565_swap(r, g, b)
                f.write(v.to_bytes(2, 'little'))
    print(f"  -> flyer/mariobros.bin ({FW*FH*2}B)")

def write_dipswitches_h(path):
    with open(path, 'w') as f:
        f.write('// Mario Bros DIP switches (read at 0x7F80)\n')
        f.write('// Auto-generated by romconv_mariobros.py\n')
        f.write('//\n')
        f.write('// Bit 0-1: Lives (00=3, 01=4, 10=5, 11=6)\n')
        f.write('// Bit 2-3: Coinage (00=1C/1C, 01=2C/1C, 10=1C/2C, 11=free play) -- MAME verified\n')
        f.write('// Bit 4-5: Bonus Life (00=20K, 01=30K, 10=40K, 11=None)\n')
        f.write('// Bit 6-7: Difficulty (00=Easy, 01=Medium, 10=Hard, 11=Hardest)\n')
        f.write('// Default: 3 lives, 1 coin/1 play, 20K bonus, Easy\n')
        f.write('#define MARIOBROS_DIP 0x00\n')
    print(f"  -> {os.path.basename(path)}")

# ═══════════════════ MAIN ═══════════════════
def main():
    print("=" * 60)
    print("Mario Bros ROM Converter for GALAGONE")
    print("=" * 60)

    rom_path = find_zip()
    if not rom_path:
        print("ERROR: mario.zip not found! Place it in project root or ~/Desktop")
        sys.exit(1)
    print(f"ROM: {rom_path}")

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'machines', 'mariobros')
    os.makedirs(out, exist_ok=True)
    print(f"Output: {out}\n")

    z = zipfile.ZipFile(rom_path)

    print("Loading ROMs...")
    cpu_7f = load(z, 'tma1-c-7f_f.7f', 8192)
    cpu_7e = load(z, 'tma1-c-7e_f.7e', 8192)
    cpu_7d = load(z, 'tma1-c-7d_f.7d', 8192)
    cpu_7c = load(z, 'tma1-c-7c_f.7c', 4096)
    snd_6k = load(z, 'tma1-c-6k_e.6k', 4096)
    gfx_3f = load(z, 'tma1-v-3f.3f', 4096)
    gfx_3j = load(z, 'tma1-v-3j.3j', 4096)
    gfx_7m = load(z, 'tma1-v-7m.7m', 4096)
    gfx_7n = load(z, 'tma1-v-7n.7n', 4096)
    gfx_7p = load(z, 'tma1-v-7p.7p', 4096)
    gfx_7s = load(z, 'tma1-v-7s.7s', 4096)
    gfx_7t = load(z, 'tma1-v-7t.7t', 4096)
    gfx_7u = load(z, 'tma1-v-7u.7u', 4096)
    prom   = load(z, 'tma1-c-4p_1.4p', 512)
    z.close()

    print("\nBuilding CPU ROM...")
    cpu_main = bytearray(cpu_7f) + bytearray(cpu_7e) + bytearray(cpu_7d)
    cpu_high = bytearray(cpu_7c)
    write_rom_h(os.path.join(out, 'mariobros_rom1.h'), 'mariobros_rom_cpu1', cpu_main,
                'Mario Bros Main CPU ROM (24K: 0x0000-0x5FFF)')
    write_rom_h(os.path.join(out, 'mariobros_rom1_high.h'), 'mariobros_rom_cpu1_high', cpu_high,
                'Mario Bros Main CPU ROM high (4K: 0xF000-0xFFFF)')

    print("\nBuilding Sound CPU ROM...")
    write_rom_h(os.path.join(out, 'mariobros_rom2.h'), 'mariobros_rom_cpu2', snd_6k,
                'Mario Bros Sound CPU ROM (I8035, 4K)')

    print("\nDecoding tiles (8x8 2bpp)...")
    gfx1 = bytearray(gfx_3f) + bytearray(gfx_3j)
    tiles = decode_tiles(gfx1)
    write_tilemap_h(os.path.join(out, 'mariobros_tilemap.h'), tiles)

    print("\nDecoding sprites (16x16 3bpp)...")
    gfx2 = bytearray(gfx_7m) + bytearray(gfx_7n) + bytearray(gfx_7p) + \
           bytearray(gfx_7s) + bytearray(gfx_7t) + bytearray(gfx_7u)
    sprites_raw = decode_sprites(gfx2)
    sprites_flipped = make_flipped_sprites(sprites_raw)
    write_spritemap_h(os.path.join(out, 'mariobros_spritemap.h'), sprites_flipped)

    print("\nBuilding color maps...")
    write_colormap_h(os.path.join(out, 'mariobros_cmap.h'), prom)

    print("\nGenerating logos (TFT + OLED + Flyer SD)...")
    write_logo_h(os.path.join(out, 'mariobros_logo.h'), out)

    print("\nGenerating DIP switches...")
    write_dipswitches_h(os.path.join(out, 'mariobros_dipswitches.h'))

    print("\n" + "=" * 60)
    print("Mario Bros ROM conversion complete!")
    print("=" * 60)

if __name__ == '__main__':
    main()
