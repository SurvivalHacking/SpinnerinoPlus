import os as _os
# --- uscita adattata al PACCHETTO (2026-09-05) ------------------------
# Questi quattro giochi NON hanno la ROM negli header: la leggono dalla SD.
# I file prodotti qui vanno in "DA COPIARE SU SD\<gioco>" nella radice del
# pacchetto, che e' quello che l'utente copia poi sulla microSD.
_PACCHETTO_SD = _os.path.join(
    _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__))),
    "DA COPIARE SU SD")

def _project_root():
    d = _os.path.dirname(_os.path.abspath(__file__))
    for cand in (d, _os.path.dirname(d)):
        if _os.path.isfile(_os.path.join(cand, "CABINATONE.ino")):
            return cand
    return d
_os.chdir(_project_root())
# ----------------------------------------------------------------------------
# ============================================================================
# romconv_1944.py - prepara 1944: The Loop Master (CPS-2) per la microSD.
#
# Lo ZIP ORIGINALE RESTA SUL PC e non viene modificato: si legge soltanto.
# Sulla SD finiscono .bin gia' pronti: niente unzip, niente decrittazione e
# niente decodifica grafica all'avvio (stessa scelta di tutti gli altri
# converter del progetto).
#
# USO:  python romconv_1944.py [--png]
#       --png = fogli di controllo della grafica in _verify_gfx/
#
# SORGENTE:  ROMS/1944.zip   (set MAME `1944u`, USA 000620)
#
# ============================================================================
# MAPPA, presa da src/mame/capcom/cps2.cpp  ROM_START(1944u)
#   maincpu  0x000000 nffu.03  0x080000 nff.04  0x100000 nffu.05   LOAD16_WORD_SWAP
#   gfx      20 MB: 13m/15m/17m/19m a 0,2,4,6 (LOAD64_WORD, passo 8)
#                   14m/16m/18m/20m a 0x1000000 +0,2,4,6
#   audiocpu nff.01: 0x8000 a 0x00000, poi ROM_CONTINUE 0x18000 a 0x10000
#   qsound   8 MB: 11m a 0, 12m a 0x400000                          LOAD16_WORD_SWAP
#   key      1944.key (20 byte)
#
# ⭐⭐ DUE IMMAGINI DEL PROGRAMMA, non una. Il CPS-2 tiene SEPARATI lo spazio
#    OPCODE e quello DATI: il 68000 preleva le ISTRUZIONI dalla ROM decifrata ma
#    legge i DATI da quella cifrata (cps2.cpp usa `m_decrypted_opcodes` solo per
#    gli opcode). Servono quindi p.bin (cifrata, dati) e pdec.bin (decifrata,
#    opcode). Anche i VETTORI DI RESET arrivano dallo spazio opcode.
#
# ⛔ ORDINE DEI BYTE — verificato sui dati, non dedotto:
#    la word i-esima e' la lettura **LITTLE-endian** dei byte del file. Con la
#    lettura big-endian la decrittazione produce spazzatura. Le due prove:
#      - vettori di reset SSP=00930000 PC=000006D0 (sensati) contro C374C482
#      - l'istruzione di watchdog compare **102 volte** contro 0
#    I .bin in uscita sono invece scritti in **BIG-endian** (byte alto per
#    primo), cioe' nell'ordine naturale del 68000: il bus del firmware fa
#    `(p[0] << 8) | p[1]`.
#
# ⭐ VERIFICHE AUTOMATICHE (se una salta, lo script si ferma):
#    1. lo sbroglio della chiave DEVE dare decoded[7]=0x4000 e decoded[8]=0x0900
#       (cps2.cpp li documenta come costanti)
#    2. dopo la decrittazione l'istruzione di watchdog, che la chiave stessa
#       porta con se' (decoded[6],[5],[4]), deve comparire nel codice
#    3. i CRC32 di ogni file dello zip contro quelli di MAME
# ============================================================================
import hashlib, struct, sys, zlib, zipfile
import numpy as np

sys.path.insert(0, _os.path.join(_project_root(), "ROM CONVERTER PY"))
from cps2dec import chiave_da_region, decripta

ROMS   = "ROMS"
OUTDIR = _os.path.join(_PACCHETTO_SD, "1944")
ZIP    = _os.path.join(ROMS, "1944.zip")

# CRC32 attesi, da ROM_START(1944u) di cps2.cpp
CRC_MAME = {
    "nffu.03": 0x9693CF8F, "nff.04": 0xDBA1C66E, "nffu.05": 0xEA813EB7,
    "nff.13m": 0xC9FCA741, "nff.15m": 0xF809D898, "nff.17m": 0x15BA4507,
    "nff.19m": 0x3DD41B8C, "nff.14m": 0x3FE3A54B, "nff.16m": 0x565CD231,
    "nff.18m": 0x63CA5988, "nff.20m": 0x21EB8F3B,
    "nff.01":  0xD2E44318, "nff.11m": 0x243E4E05, "nff.12m": 0x4FCF1600,
    "1944.key": 0x61734F5B,          # = 1944u.key, nello zip e' solo rinominata
}


def leggi(z, nome, atteso=None):
    d = z.read(nome)
    c = zlib.crc32(d) & 0xFFFFFFFF
    if nome in CRC_MAME and c != CRC_MAME[nome]:
        sys.exit(f"ERRORE: {nome} ha CRC {c:08X}, MAME dice {CRC_MAME[nome]:08X}")
    if atteso is not None and len(d) != atteso:
        sys.exit(f"ERRORE: {nome} e' {len(d)} byte invece di {atteso}")
    return d


def unshuffle(buf):
    """cps2.cpp:758 unshuffle(), su un banco da 2 MB visto come uint64.
    La ricorsione originale e' O(n log n) di scambi; qui la stessa cosa in
    numpy, con gli scambi fatti a fette invece che elemento per elemento."""
    def ric(a):
        n = len(a)
        if n == 2:
            return
        assert n % 4 == 0
        m = n // 2
        ric(a[:m]); ric(a[m:])
        meta = m // 2
        t = a[meta:meta + meta].copy()
        a[meta:meta + meta] = a[m:m + meta]
        a[m:m + meta] = t
    ric(buf)


def main():
    fai_png = "--png" in sys.argv
    if not _os.path.isfile(ZIP):
        sys.exit(f"ERRORE: manca {ZIP}")
    _os.makedirs(OUTDIR, exist_ok=True)
    z = zipfile.ZipFile(ZIP)
    print(f"1944: The Loop Master (CPS-2, set MAME 1944u) - da {ZIP}\n")

    # ---------------------------------------------------------------- 68000
    print("[1/5] programma 68000: montaggio e DECRITTAZIONE CPS-2")
    prog = (leggi(z, "nffu.03", 0x80000) + leggi(z, "nff.04", 0x80000)
            + leggi(z, "nffu.05", 0x80000))
    master, lower, upper, dec10 = chiave_da_region(leggi(z, "1944.key", 20))
    if dec10[7] != 0x4000 or dec10[8] != 0x0900:
        sys.exit(f"ERRORE: sbroglio della chiave sbagliato "
                 f"(decoded[7]={dec10[7]:04X}, decoded[8]={dec10[8]:04X}; "
                 f"devono essere 4000 e 0900)")
    print(f"      chiave OK  master {master[0]:08X} {master[1]:08X}  "
          f"cifrato {lower:06X}..{upper:06X}")
    # ⛔ little-endian: vedi la nota in testa
    words = list(struct.unpack(f"<{len(prog) // 2}H", prog))
    print(f"      decrittazione di {len(words)} word (ci vogliono ~10 s)...")
    deco = decripta(words, master, lower // 2, upper // 2)

    watchdog = (dec10[6], dec10[5], dec10[4])
    n_wd = sum(1 for a in range(len(deco) - 2)
               if (deco[a], deco[a + 1], deco[a + 2]) == watchdog)
    if not n_wd:
        sys.exit("ERRORE: dopo la decrittazione l'istruzione di watchdog "
                 f"{watchdog[0]:04X} {watchdog[1]:04X} {watchdog[2]:04X} non "
                 "compare: la decrittazione NON e' andata a buon fine")
    ssp = (deco[0] << 16) | deco[1]
    pc  = (deco[2] << 16) | deco[3]
    print(f"      watchdog {watchdog[0]:04X} {watchdog[1]:04X} {watchdog[2]:04X} "
          f"trovata {n_wd} volte -> decrittazione VERIFICATA")
    print(f"      vettori di reset: SSP={ssp:08X}  PC={pc:08X}")

    # in uscita: BIG-endian, l'ordine naturale del 68000
    p_enc = struct.pack(f">{len(words)}H", *words)
    p_dec = struct.pack(f">{len(deco)}H", *deco)

    # ---------------------------------------------------------------- grafica
    print("\n[2/5] grafica: interleave a 8 vie + unshuffle")
    gfx = np.zeros(0x1400000, dtype=np.uint8)
    for base, nomi, dim in ((0x0000000, ("nff.13m", "nff.15m", "nff.17m", "nff.19m"), 0x400000),
                            (0x1000000, ("nff.14m", "nff.16m", "nff.18m", "nff.20m"), 0x100000)):
        for k, nome in enumerate(nomi):
            d = np.frombuffer(leggi(z, nome, dim), dtype=np.uint8)
            # ROM_LOAD64_WORD: word k-esima a base + off + 8*k  (off = 0,2,4,6)
            off = base + 2 * k
            gfx[off + 0::8][:dim // 2] = d[0::2]
            gfx[off + 1::8][:dim // 2] = d[1::2]
        print(f"      banco a {base:#09x}: 4 file da {dim // 1024} KB montati")
    print("      unshuffle per banchi da 2 MB...")
    v = gfx.view(np.uint64)
    for i in range(0, len(v), 0x200000 // 8):
        unshuffle(v[i:i + 0x200000 // 8])

    # ---------------------------------------------------------------- Z80
    print("\n[3/5] programma Z80 (con ROM_CONTINUE)")
    m = leggi(z, "nff.01", 0x20000)
    z80 = bytearray(0x50000)                 # QSOUND_SIZE
    z80[0x00000:0x08000] = m[0x00000:0x08000]
    z80[0x10000:0x28000] = m[0x08000:0x20000]   # ROM_CONTINUE
    print(f"      0x8000 byte a 0x00000, 0x18000 a 0x10000 (regione {len(z80):#x})")

    # ---------------------------------------------------------------- QSound
    print("\n[4/5] campioni QSound")
    q = bytearray(leggi(z, "nff.11m", 0x400000) + leggi(z, "nff.12m", 0x400000))
    # ⛔⭐ BYTE-SWAP a coppie. MAME carica le sample ROM del CPS-2 con
    # ROM_LOAD16_WORD_SWAP e FBNeo chiama BurnByteswap (cps.cpp, ramo CPS2_QSND):
    # nella ROM i due byte di ogni word stanno scambiati rispetto all'ordine in
    # cui il DSP li legge (un byte = un campione PCM a 8 bit CON SEGNO).
    # ⭐ MISURATO, non dedotto: sul salto medio fra campioni adiacenti, su 375
    # regioni non vuote della ROM, l'ordine swappato da' **7,48** contro
    # **12,36** del file grezzo. Il PCM vero e' quello liscio.
    q[0::2], q[1::2] = q[1::2], q[0::2]
    q = bytes(q)
    print(f"      {len(q) // 1048576} MB  (byte-swap applicato)")

    # ---------------------------------------------------------------- uscita
    print("\n[5/5] scrittura")
    uscite = (("p.bin",    p_enc, "programma 68000 (dati, cifrato)"),
              ("pdec.bin", p_dec, "programma 68000 (opcode, decifrato)"),
              ("gfx.bin",  gfx.tobytes(), "grafica"),
              ("m.bin",    bytes(z80), "programma Z80"),
              ("q.bin",    q, "campioni QSound"))
    tot = 0
    for nome, dati, che in uscite:
        with open(_os.path.join(OUTDIR, nome), "wb") as f:
            f.write(dati)
        c = zlib.crc32(dati) & 0xFFFFFFFF
        tot += len(dati)
        print(f"      {nome:<10} {len(dati):>9} B  CRC {c:08X}  {che}")
    print(f"\n      totale sulla SD: {tot / 1048576:.2f} MB  in {OUTDIR}")
    print(f"      SHA1 di gfx.bin: {hashlib.sha1(gfx.tobytes()).hexdigest()}")

    if fai_png:
        try:
            from PIL import Image
        except ImportError:
            print("\n(--png: manca Pillow, salto i fogli di controllo)")
            return
        # 4bpp planare CPS: 8 byte per riga di tile 16x16 (4 piani)
        d = _os.path.join("_verify_gfx")
        _os.makedirs(d, exist_ok=True)
        n_tile, lato = 1024, 32
        img = Image.new("L", (lato * 16, (n_tile // lato) * 16))
        px = img.load()
        for t in range(n_tile):
            b = gfx[t * 128:(t + 1) * 128]
            for riga in range(16):
                p0, p1, p2, p3 = b[riga * 8 + 0], b[riga * 8 + 1], b[riga * 8 + 2], b[riga * 8 + 3]
                p4, p5, p6, p7 = b[riga * 8 + 4], b[riga * 8 + 5], b[riga * 8 + 6], b[riga * 8 + 7]
                for c in range(8):
                    v = ((((p0 >> (7 - c)) & 1) << 0) | (((p1 >> (7 - c)) & 1) << 1) |
                         (((p2 >> (7 - c)) & 1) << 2) | (((p3 >> (7 - c)) & 1) << 3))
                    px[(t % lato) * 16 + c, (t // lato) * 16 + riga] = int(v) * 17
                for c in range(8):
                    v = ((((p4 >> (7 - c)) & 1) << 0) | (((p5 >> (7 - c)) & 1) << 1) |
                         (((p6 >> (7 - c)) & 1) << 2) | (((p7 >> (7 - c)) & 1) << 3))
                    px[(t % lato) * 16 + 8 + c, (t // lato) * 16 + riga] = int(v) * 17
        img.save(_os.path.join(d, "1944_tiles.png"))
        print(f"\n      foglio di controllo: _verify_gfx/1944_tiles.png")


if __name__ == "__main__":
    main()
