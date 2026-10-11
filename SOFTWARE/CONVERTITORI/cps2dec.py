import struct, sys
sys.path.insert(0, ".")
from cps2_tabelle import *

def _opt(box):
    tab, inp, outp = box
    lut = [sum(((v >> inp[i]) & 1) << i for i in range(6) if inp[i] >= 0) for v in range(256)]
    out = [((1 << outp[0]) if (tab[i] & 1) else 0) | ((1 << outp[1]) if (tab[i] & 2) else 0)
           for i in range(64)]
    return lut, out

S1 = [_opt(b) for g in ("fn1_r1_boxes","fn1_r2_boxes","fn1_r3_boxes","fn1_r4_boxes") for b in globals()[g]]
S2 = [_opt(b) for g in ("fn2_r1_boxes","fn2_r2_boxes","fn2_r3_boxes","fn2_r4_boxes") for b in globals()[g]]

def _fn(v, sb, key):
    r = 0
    for j in range(4):
        lut, out = sb[j]
        r |= out[lut[v] ^ ((key >> (6 * j)) & 0x3F)]
    return r

def feistel(val, bitsA, bitsB, sb, k):
    l = sum(((val >> bitsB[i]) & 1) << i for i in range(8))
    r = sum(((val >> bitsA[i]) & 1) << i for i in range(8))
    l ^= _fn(r, sb[0:4],   k[0]); r ^= _fn(l, sb[4:8],   k[1])
    l ^= _fn(r, sb[8:12],  k[2]); r ^= _fn(l, sb[12:16], k[3])
    return (sum(((l >> i) & 1) << bitsA[i] for i in range(8)) |
            sum(((r >> i) & 1) << bitsB[i] for i in range(8)))

def _expand(bits, src, n):
    d = [0, 0, 0, 0]
    for i in range(n):
        d[i // 24] |= ((src[bits[i] // 32] >> (bits[i] % 32)) & 1) << (i % 24)
    return d

def chiave_da_region(key20):
    """cps2.cpp:10895-10926 — sbroglia i 20 byte in 10 word."""
    dec = [0] * 10
    for b in range(160):
        bit = (317 - b) % 160
        if (key20[bit // 8] >> ((bit ^ 7) % 8)) & 1:
            dec[b // 16] |= 0x8000 >> (b % 16)
    master = [(dec[0] << 16) | dec[1], (dec[2] << 16) | dec[3]]
    if dec[9] == 0xFFFF: lower, upper = 0xFF0000, 0xFFFFFF
    else:                lower, upper = 0, (((~dec[9] & 0x3FF) << 14) | 0x3FFF) + 1
    return master, lower, upper, dec

def decripta(words, master, lower_w, upper_w, log=None):
    k1 = _expand(bits1, master, 96)
    k1[0] ^= ((k1[0] >> 1) & 1) << 4;  k1[0] ^= ((k1[0] >> 2) & 1) << 5
    k1[0] ^= ((k1[0] >> 8) & 1) << 11; k1[1] ^= ((k1[1] >> 0) & 1) << 5
    k1[1] ^= ((k1[1] >> 8) & 1) << 11; k1[2] ^= ((k1[2] >> 1) & 1) << 5
    k1[2] ^= ((k1[2] >> 8) & 1) << 11
    dec = list(words); n = len(words)
    for i in range(0x10000):
        seed = feistel(i, fn1_groupA, fn1_groupB, S1, k1)
        sub = [0, 0]
        for b in range(64):
            sub[b // 32] |= ((seed >> bitsub[b]) & 1) << (b % 32)
        sub[0] ^= master[0]; sub[1] ^= master[1]
        k2 = _expand(bits2, sub, 96)
        k2[0] ^= ((k2[0] >> 0) & 1) << 5;  k2[0] ^= ((k2[0] >> 6) & 1) << 11
        k2[1] ^= ((k2[1] >> 0) & 1) << 5;  k2[1] ^= ((k2[1] >> 1) & 1) << 4
        k2[2] ^= ((k2[2] >> 2) & 1) << 5;  k2[2] ^= ((k2[2] >> 3) & 1) << 4
        k2[2] ^= ((k2[2] >> 7) & 1) << 11; k2[3] ^= ((k2[3] >> 1) & 1) << 5
        for a in range(i, n, 0x10000):
            if lower_w <= a <= upper_w:
                dec[a] = feistel(words[a], fn2_groupA, fn2_groupB, S2, k2)
        if log and (i & 0x3FFF) == 0: print(f"  {i*100//0x10000:3d}%", flush=True)
    return dec
