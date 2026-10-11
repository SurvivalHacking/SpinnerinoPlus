#!/usr/bin/env python3
# ----------------------------------------------------------------------------
# COPIA SPINNERINO (2026-10-08) di CABINATONE\romconv_neogeo.py — la copia in
# RADICE del 20/09, quella col fix «un file di sprite in DUE pezzi» che serve a
# VIEW POINT (la copia in "ROM CONVERTER PY" e quella del pacchetto del 5/9 NON
# ce l'hanno). Cambia SOLO l'ancoraggio: ci si sposta sulla cartella SOPRA
# CONVERTITORI, cioe' la radice del pacchetto, dove stanno ROMS\ e
# DA COPIARE SU SD\. Nessun percorso di un PC preciso.
# Il RESTO e' identico: provato rigenerando VIEW POINT e PULSTAR, file uguali
# byte per byte a quelli che girano sullo SPINNERINO.
# USO:  python romconv_neogeo.py viewpoin      python romconv_neogeo.py pulstar
# ⚠️ In ROMS\ servono anche neogeo.zip (BIOS) e gngeo_data.zip (mappe .drv).
# ⚠️ PULSTAR vuole anche sram.bin: lo fa romconv_neogeo_sram.py, non questo.
import os as _os
_os.chdir(_os.path.dirname(_os.path.dirname(_os.path.abspath(__file__))))
# ----------------------------------------------------------------------------
# ============================================================================
# romconv_neogeo.py - prepara un gioco NEO GEO per la microSD del CABINATONE.
#
# Lo ZIP ORIGINALE RESTA SUL PC e non viene modificato: si legge soltanto.
# Sulla SD finiscono file .bin gia' pronti, che il firmware carica e basta —
# niente unzip ne' decodifica all'avvio (stessa scelta di tutti gli altri
# converter del progetto). ⭐ Vale anche per la CIFRATURA: i set protetti
# (Metal Slug 3 & c.) vengono decifrati QUI, una volta, sul PC.
#
# USO:  python romconv_neogeo.py [gioco] [--png]
#       default: mslug          --png = fogli di controllo della grafica
#
# SORGENTI (in ROMS/):
#   <gioco>.zip      il gioco             (es. mslug.zip)
#   neogeo.zip       il BIOS Neo Geo      (uguale per TUTTI i giochi)
#   gngeo_data.zip   le mappe .drv        (dice quale file va in quale regione)
#                    + le tabelle rom/cmc42.xor e rom/cmc50.xor
#
# USCITA in DA COPIARE SU SD/neogeo/:
#   bios/            sp.bin sfix.bin lo.bin sm1.bin   (una volta per tutte)
#   <gioco>/p.bin    programma 68000
#           m.bin    programma Z80 sonoro
#           s.bin    fix layer (il testo)
#           v.bin    campioni ADPCM del YM2610
#           c.ctile  sprite gia' convertiti nel formato veloce
#           c.cusage mappa dei tile invisibili, da saltare nel rendering
#
# ⚠️ TRE COSE CHE NON SI POSSONO SBAGLIARE, lette dal .drv e non indovinate:
#   1) la P-ROM da 2 MB si carica con le DUE META' SCAMBIATE
#      (src 0x000000 -> dest 0x100000 e src 0x100000 -> dest 0x000000):
#      senza lo scambio il 68000 parte sul codice sbagliato e il gioco non boota;
#   2) le C-ROM vanno INTERLACCIATE a coppie, un byte dall'una e uno dall'altra
#      (c1/c2 formano gli 8 MB bassi, c3/c4 gli 8 MB alti);
#   3) le DIMENSIONI DELLE REGIONI (romsize[]) cambiano da gioco a gioco e sono
#      quelle che comandano: la fix di Metal Slug 3 e' 512 KB, non 128.
#
# La conversione dei tile e' il port ESATTO di convert_roms_tile() di
# gngeo/roms.c: 128 byte in, 128 byte out. NON espande — riordina i bit da
# planare a 4bpp "packed" (8 pixel per dword) perche' il blitter li legga di
# fila. Qui e' vettorizzata con numpy, altrimenti 131.072 tile in Python puro
# richiederebbero parecchi minuti.
#
# ---------------------------------------------------------------------------
# ⭐ SET CIFRATI (dal 2026-08-01, per Metal Slug 3)
# ---------------------------------------------------------------------------
# Tre protezioni diverse, tutte sciolte QUI e mai sul device:
#   * CMC42/CMC50 sulla GRAFICA — port di neogeo_gfx_decrypt() (neocrypt.c).
#     Le nove tabelle da 256 byte NON sono trascritte a mano: si leggono da
#     `rom/cmc42.xor` dentro gngeo_data.zip, cioe' dalla stessa fonte che usa
#     gngeo (res_load_data("rom/cmc42.xor"), neocrypt.c:load_cmc42_table).
#   * SFIX — nei set CMC la S-ROM non esiste: il fix layer sta negli ULTIMI
#     tx_size byte della C-ROM decifrata (neogeo_sfix_decrypt, neocrypt.c:615).
#   * SMA — i set kof99/garou/mslug3/kof2000 hanno un chip che contiene codice e
#     descrambla la P-ROM: mslug3_decrypt_68k(), neocrypt.c:1199.
# ============================================================================
import os, sys, struct, zipfile, zlib, time

try:
    import numpy as np
except ImportError:
    print("ERRORE: serve numpy  ->  pip install numpy")
    sys.exit(1)

QUI = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)

ROMS   = os.path.join(PACCHETTO, "ROMS")
OUTDIR = os.path.join(PACCHETTO, "DA COPIARE SU SD", "neogeo")

# regioni, come le numera il .drv di gngeo (roms.h)
REG_Z80    = 1
# ⭐ regione 2 = Z80 CIFRATO. Nei set CMC50 (Metal Slug 4...) il .drv lascia la
# regione 1 VUOTA e mette l'M1 cifrato qui: il programma sonoro in chiaro si
# ottiene solo con cmc50_m1_decrypt(). Senza questo, m.bin non verrebbe scritto.
REG_Z80_CRYPT = 2
REG_ADPCMA = 3
REG_ADPCMB = 4
REG_FIXBIO = 5
REG_FIX    = 6
REG_M68K   = 8
REG_SPRITE = 9

# il BIOS: nomi nello zip -> nome sulla SD
BIOS = [("neo-geo.rom", "sp.bin"), ("ng-sfix.rom", "sfix.bin"),
        ("ng-lo.rom", "lo.bin"),   ("ng-sm1.rom", "sm1.bin")]


# ============================================================================
# CIFRATURA — tabella dei set protetti
# ============================================================================
# Copiata dalle init_*() di gngeo/roms.c: e' li' che si vede, per ogni set,
# QUALE decrittazione si applica e con quale chiave. Aggiungere un set cifrato
# = aggiungere una riga qui (e, se ha l'SMA, la sua voce in SMA_RECIPES).
#
#   gfx      "cmc42" / "cmc50" / None      grafica + (per CMC) fix ricavata
#   xor      extra_xor della chiave gfx
#   sma      nome della ricetta SMA per la P-ROM, oppure None
#
# ⚠️ Sono elencati SOLO i set che ho verificato riga per riga sul sorgente.
#    Gli altri della famiglia CMC (kof99, garou, ganryu, nitd, sengoku3...)
#    hanno la stessa forma: si aggiungono qui con la chiave che sta in
#    roms.c, ma non li ho mai provati.
# ⛔ NON aspettarsi qui TUTTI i giochi protetti: `mslugx` (Metal Slug X) NON e'
#    cifrato — le sue ROM sono in chiaro (vettori 10 00 00 F3 C0 00 02 04, m.bin
#    che parte con 0xF3, s.bin col 48,7% di byte a zero). Quello che ha e' un
#    chip ALTERA a 0x2FFFE0 che si emula A RUNTIME nel firmware (ng_mslugx_* in
#    neogeo.cpp, da MAME prot_mslugx.cpp). Qui non va aggiunto niente.
CIFRATI = {
    # roms.c:264  init_mslug3(): mslug3_decrypt_68k + kof99_neogeo_gfx_decrypt(0xad)
    "mslug3":  dict(gfx="cmc42", xor=0xAD, sma="mslug3"),
    # roms.c:280  init_mslug3h(): solo la grafica, P-ROM in chiaro (set non-SMA)
    "mslug3h": dict(gfx="cmc42", xor=0xAD, sma=None),
    "mslug3n": dict(gfx="cmc42", xor=0xAD, sma=None),
    # roms.c  init_mslug4(): neogeo_cmc50_m1_decrypt + kof2000_neogeo_gfx_decrypt(0x31)
    #                        + neo_pcm2_snk_1999(8);  neogeo_fix_bank_type = 1
    # ⚠️ kof2000_neogeo_gfx_decrypt = load_cmc50_table() + le STESSE
    #    neogeo_gfx_decrypt/neogeo_sfix_decrypt di kof99: cambiano solo le
    #    tabelle. Niente SMA: la P-ROM di Metal Slug 4 e' in chiaro.
    # ⚠️ l'M1 sta nella regione 2 (Z80 CIFRATO) e la regione 1 e' vuota:
    #    senza `m1="cmc50"` non verrebbe scritto nessun m.bin e il gioco
    #    sarebbe muto. `pcm2` e' il `value` di neo_pcm2_snk_1999.
    "mslug4":  dict(gfx="cmc50", xor=0x31, sma=None, m1="cmc50", pcm2=8),
    # roms.c  init_mslug5(): mslug5_decrypt_68k + neogeo_cmc50_m1_decrypt
    #                        + kof2000_neogeo_gfx_decrypt(0x19) + neo_pcm2_swap(2)
    #                        + protezione NEO-PVC a runtime
    # ⭐ Qui la P-ROM NON ha l'SMA: ha una cifratura tutta sua (`p68k`), fatta di
    #    due tabelle XOR, uno scambio di bit dentro ogni dword e due
    #    rimescolamenti di indirizzo. E' la famiglia mslug5/svc/kof2003.
    # ⚠️ Il PCM2 e' la variante `neo_pcm2_swap`, NON `neo_pcm2_snk_1999`: sono
    #    due funzioni diverse, con tabelle diverse. Vedi pcm2_swap() piu' sotto.
    "mslug5":  dict(gfx="cmc50", xor=0x19, sma=None, m1="cmc50",
                    pcm2_swap=2, p68k="mslug5"),
    # ---- Metal Slug 6 (BOOTLEG di Metal Slug 3, set `mslug3b6`) -------------
    # ⭐ E' un CLONE: nel suo zip ci sono solo le due P-ROM e la S1; grafica,
    #    campioni e Z80 arrivano da `mslug3.zip` (il .drv lo dice: parent
    #    mslug3, e i file si chiamano 256-*).
    # La ricetta e' quella della cartuccia di MAME, letta dal sorgente
    # (src/devices/bus/neogeo/boot_misc.cpp, neogeo_mslug3b6_cart_device):
    #     m_prot->sx_decrypt(fix_region, fix_region_size, 2);
    #     m_cmc_prot->cmc42_gfx_decrypt(spr_region, spr_region_size, MSLUG3_GFX_KEY);
    # cioe': **grafica CMC42 con la chiave di Metal Slug 3 (0xAD)** — sono le
    # stesse C-ROM — e la **fix del bootleg descramblata a byte**. Il resto e'
    # in chiaro: la P-ROM NON ha l'SMA (e' il bootleg che l'ha tolto), l'M1 e i
    # campioni non sono cifrati.
    # ⚠️ `sfix_boot=2` NON e' la fix ricavata dalla C-ROM dei set CMC: qui la S1
    #    esiste come file e va solo srotolata, un bitswap per byte.
    "mslug3b6": dict(gfx="cmc42", xor=0xAD, sma=None, sfix_boot=2),
}

# ---------------------------------------------------------------------------
# Ricette SMA — trascritte da neocrypt.c, riga per riga di sorgente.
# Tutte hanno la stessa forma (mslug3_decrypt_68k, neocrypt.c:1199):
#   1) scambio delle linee DATI su tutta la ROM bancata (da 0x100000 in poi)
#   2) scambio delle linee INDIRIZZO + rilocazione della parte fissa: i primi
#      0xC0000 byte si ricostruiscono leggendo da `fissa_src` piu' avanti
#   3) scambio delle linee indirizzo DENTRO ogni blocco della parte bancata
# I numeri sono gli argomenti di BITSWAP16/BITSWAP24 nell'ordine del sorgente
# (il primo e' il bit piu' alto del risultato: mame_layer.h:65).
# ---------------------------------------------------------------------------
SMA_RECIPES = {
    "mslug3": dict(
        # rom[i] = BITSWAP16(rom[i], 4,11,14,3,1,13,0,7,2,8,12,15,10,9,5,6)
        dati=[4, 11, 14, 3, 1, 13, 0, 7, 2, 8, 12, 15, 10, 9, 5, 6],
        dati_da=0x100000, dati_len=0x800000,
        # rom[i] = rom[0x5d0000/2 + BITSWAP24(i, ...)]   per i < 0xC0000/2
        fissa_len=0x0C0000, fissa_src=0x5D0000,
        fissa=[23, 22, 21, 20, 19, 18, 15, 2, 1, 13, 3, 0, 9, 6,
               16, 4, 11, 5, 7, 12, 17, 14, 10, 8],
        # blocchi da 0x10000 byte dentro la parte bancata
        blocco=0x10000,
        banco=[23, 22, 21, 20, 19, 18, 17, 16, 15, 2, 11, 0, 14, 6,
               4, 13, 8, 9, 3, 10, 7, 5, 12, 1],
    ),
}


def _bitswap(v, bits, larghezza):
    """BITSWAP16/24 di mame_layer.h, vettorizzato.
    bits[0] e' il bit sorgente che finisce nella posizione piu' alta."""
    assert len(bits) == larghezza
    out = np.zeros_like(v)
    for k, b in enumerate(bits):
        out |= ((v >> np.uint32(b)) & 1) << np.uint32(larghezza - 1 - k)
    return out


def sma_decrypt_68k(buf, ricetta):
    """Port di mslug3_decrypt_68k() (neocrypt.c:1199).
    ⚠️ Il bus del NeoGeo e' LITTLE-ENDIAN NATIVO in gngeo (memory.h:
    READ_WORD(a) = *(Uint16*)(a)): le word si leggono come le vede l'x86,
    esattamente come fa l'emulatore che gira. Leggendole big-endian lo stack
    pointer usciva 0x100000F3 invece di 0x0010F300."""
    r = ricetta
    p = np.frombuffer(buf, dtype="<u2").copy()      # tutta la regione, in word

    # 1) linee DATI su tutta la ROM bancata
    a = r["dati_da"] // 2
    n = r["dati_len"] // 2
    w = p[a:a + n].astype(np.uint32)
    p[a:a + n] = _bitswap(w, r["dati"], 16).astype("<u2")
    del w

    # 2) linee INDIRIZZO + rilocazione della parte fissa (i primi 0xC0000 byte)
    #    ⚠️ va fatta PRIMA del punto 3: legge la parte bancata cosi' come la
    #    lascia il punto 1. Invertire i due passi darebbe una parte fissa
    #    sbagliata e il 68000 non boota.
    nf  = r["fissa_len"] // 2
    idx = _bitswap(np.arange(nf, dtype=np.uint32), r["fissa"], 24)
    p[0:nf] = p[r["fissa_src"] // 2 + idx]
    del idx

    # 3) linee indirizzo DENTRO ogni blocco della parte bancata
    blocco_w = r["blocco"] // 2
    jdx = _bitswap(np.arange(blocco_w, dtype=np.uint32), r["banco"], 24)
    for i in range(0, n, blocco_w):
        blk = p[a + i: a + i + blocco_w].copy()
        p[a + i: a + i + blocco_w] = blk[jdx]

    return p.tobytes()


def _bitswap_scalare(v, bits, larghezza):
    """Come _bitswap ma su un intero solo (serve negli indici dei blocchi)."""
    out = 0
    for k, b in enumerate(bits):
        out |= ((v >> b) & 1) << (larghezza - 1 - k)
    return out


# ============================================================================
# P-ROM della famiglia mslug5 / svc / kof2003 — mslug5_decrypt_68k()
# ============================================================================
# Trascritta riga per riga da gngeo/src/neocrypt.c (identica a MAME
# src/devices/bus/neogeo/prot_pvc.cpp: le due fonti coincidono, controllate).
#
# ⚠️ `BYTE_XOR_LE(i)` vale **i** (host little-endian, e la regione di gngeo tiene
#    le word in ordine nativo — la stessa convenzione gia' usata da
#    sma_decrypt_68k). NON e' un'ipotesi: e' MISURATA. Con questa scelta i primi
#    8 byte del p.bin escono
#        10 00 00 F3 C0 00 02 04  ->  SSP 0x0010F300 · PC 0x00C00402
#    cioe' esattamente i vettori dei set NON cifrati gia' verificati
#    (mslug/aof2/sonicwi2). Con `i ^ 1` escono numeri senza senso.
#
# ⛔⛔ E LA REGIONE VA COSTRUITA INTERLACCIATA. Nel .drv `268-p1cr` sta a
#     dest 0x0 e `268-p2cr` a dest **0x2**: non e' un accodamento, e' un
#     interlacciamento a WORD dentro dword (p1 le word pari, p2 le dispari) —
#     l'equivalente di ROM_LOAD32_WORD_SWAP di MAME. Copiando p2 "a offset 2"
#     come fosse un blocco si sovrascrive p1 e non ne esce nulla. Vedi
#     assembla_p68k().
def mslug5_decrypt_68k(buf):
    XOR1 = np.array([0xc2,0x4b,0x74,0xfd,0x0b,0x34,0xeb,0xd7,0x10,0x6d,0xf9,0xce,
                     0x5d,0xd5,0x61,0x29,0xf5,0xbe,0x0d,0x82,0x72,0x45,0x0f,0x24,
                     0xb3,0x34,0x1b,0x99,0xea,0x09,0xf3,0x03], dtype=np.uint8)
    XOR2 = np.array([0x36,0x09,0xb0,0x64,0x95,0x0f,0x90,0x42,0x6e,0x0f,0x30,0xf6,
                     0xe5,0x08,0x30,0x64,0x08,0x04,0x00,0x2f,0x72,0x09,0xa0,0x13,
                     0xc9,0x0b,0xa0,0x3e,0xc2,0x00,0x40,0x2b], dtype=np.uint8)
    N = 0x800000
    if len(buf) != N:
        raise ValueError("mslug5: P-ROM di %d byte invece di %d" % (len(buf), N))
    rom = np.frombuffer(buf, dtype=np.uint8).copy()

    # 1) due tabelle XOR, una per la parte fissa e una per quella bancata
    rom[:0x100000] ^= np.tile(XOR1, 0x100000 // 0x20)
    rom[0x100000:] ^= np.tile(XOR2, (N - 0x100000) // 0x20)

    # 2) scambio di bit sulla word CENTRALE di ogni dword (byte +1 e +2)
    idx = np.arange(0x100000, N, 4)
    r16 = (rom[idx + 1].astype(np.uint32) | (rom[idx + 2].astype(np.uint32) << 8))
    r16 = _bitswap(r16, [15, 14, 13, 12, 10, 11, 8, 9, 6, 7, 4, 5, 3, 2, 1, 0], 16)
    rom[idx + 1] = (r16 & 0xFF).astype(np.uint8)
    rom[idx + 2] = (r16 >> 8).astype(np.uint8)

    # 3) rimescolamento degli INDIRIZZI: blocchi da 64 KB nella parte fissa,
    #    blocchi da 256 byte in quella bancata
    src = rom.copy()
    for i in range(0x100000 // 0x10000):
        ofst = (i & 0xf0) + _bitswap_scalare(i & 0x0f, [7, 6, 5, 4, 1, 0, 3, 2], 8)
        rom[i * 0x10000:(i + 1) * 0x10000] = src[ofst * 0x10000:(ofst + 1) * 0x10000]
    for i in range(0x100000, N, 0x100):
        ofst = ((i & 0xf000ff) + ((i & 0x000f00) ^ 0x00700)
                + (_bitswap_scalare((i & 0x0ff000) >> 12, [5, 4, 7, 6, 1, 0, 3, 2], 8) << 12))
        rom[i:i + 0x100] = src[ofst:ofst + 0x100]

    # 4) rilocazione finale: l'ultimo mega va a 0x100000 e il resto scorre
    src = rom.copy()
    rom[0x100000:0x200000] = src[0x700000:0x800000]
    rom[0x200000:0x800000] = src[0x100000:0x700000]
    return rom.tobytes()


P68K_DECRYPT = { "mslug5": mslug5_decrypt_68k }


def carica_tabelle_xor(nome):
    """Le nove tabelle da 256 byte, dalla STESSA fonte di gngeo:
    res_load_data("rom/cmc42.xor") — neocrypt.c:load_cmc42_table()."""
    with zipfile.ZipFile(os.path.join(ROMS, "gngeo_data.zip")) as z:
        d = z.read("rom/%s.xor" % nome)
    if len(d) < 2304:
        raise ValueError("%s.xor corta: %d byte" % (nome, len(d)))
    n = len(d) // 256
    t = [np.frombuffer(d[i * 256:(i + 1) * 256], dtype=np.uint8) for i in range(n)]
    T = dict(type0_t03=t[0], type0_t12=t[1], type1_t03=t[2], type1_t12=t[3],
             a8_15_x1=t[4], a8_15_x2=t[5], a16_23_x1=t[6], a16_23_x2=t[7],
             a0_7_x=t[8])
    # ⭐ cmc42.xor e' lungo 0x900 (9 tabelle), cmc50.xor 0xB00 (11): le DUE in
    #    piu' sono quelle del decrypt dell'M1, e finora nessuno le leggeva.
    if n >= 11:
        T["m1_a8_15_x"] = t[9]
        T["m1_a0_7_x"]  = t[10]
    return T


# ============================================================================
# NEO-PCM2: rimescolamento dei campioni ADPCM (neo_pcm2_snk_1999)
# ============================================================================
# Il piu' semplice di tutte le protezioni NeoGeo: la V-ROM si legge a word da 16
# bit e, dentro ogni blocco di `value` byte, le word vengono permutate con
#   nuovo[j] = vecchio[j ^ (value/4)]
# Con value = 8 (Metal Slug 4) il blocco e' di 4 word e l'effetto e' scambiare
# le due meta' del blocco. Nessuna tabella, nessuna chiave.
def pcm2_snk_1999(dati, value):
    a = np.frombuffer(dati, dtype="<u2")
    meta = value // 2                     # word per blocco
    if a.size % meta:
        raise ValueError("V-ROM non multipla del blocco PCM2 (%d word)" % meta)
    idx = np.arange(meta, dtype=np.intp) ^ np.intp(value // 4)
    return a.reshape(-1, meta)[:, idx].tobytes()


# ----------------------------------------------------------------------------
# NEO-PCM2, variante `neo_pcm2_swap` (mslug5, svc, kof2003)
# ----------------------------------------------------------------------------
# ⚠️ E' un'ALTRA funzione, non un altro parametro di quella qui sopra: qui i
# campioni vengono rimescolati per indirizzo E poi passati sotto XOR.
# Trascritta da gngeo/src/neocrypt.c neo_pcm2_swap() (identica a MAME
# prot_pcm2.cpp pcm2_prot_device::swap()).
#
# ⭐ Il BITSWAP24(i, 23,22,21,20,19,18,17,0,15,...,1,16) sembra una permutazione
#    complicata: srotolandolo, tutti i bit restano dove sono TRANNE il bit 0 e il
#    bit 16, che si SCAMBIANO. Cosi' diventa una riga di numpy invece di 24
#    passate su 16 milioni di indici.
PCM2_SWAP_ADDRS = [(0x000000, 0xa5000), (0xffce20, 0x01000), (0xfe2cf6, 0x4e001),
                   (0xffac28, 0xc2000), (0xfeb2c0, 0x0a000), (0xff14ea, 0xa7001),
                   (0xffb440, 0x02000)]
PCM2_SWAP_XOR = [
    [0xf9, 0xe0, 0x5d, 0xf3, 0xea, 0x92, 0xbe, 0xef],
    [0xc4, 0x83, 0xa8, 0x5f, 0x21, 0x27, 0x64, 0xaf],
    [0xc3, 0xfd, 0x81, 0xac, 0x6d, 0xe7, 0xbf, 0x9e],
    [0xc3, 0xfd, 0x81, 0xac, 0x6d, 0xe7, 0xbf, 0x9e],
    [0xcb, 0x29, 0x7d, 0x43, 0xd2, 0x3a, 0xc2, 0xb4],
    [0x4b, 0xa4, 0x63, 0x46, 0xf0, 0x91, 0xea, 0x62],
    [0x4b, 0xa4, 0x63, 0x46, 0xf0, 0x91, 0xea, 0x62]]


def pcm2_swap(dati, value):
    N = 0x1000000
    if len(dati) != N:
        raise ValueError("pcm2_swap: V-ROM di %d byte invece di %d" % (len(dati), N))
    src = np.frombuffer(dati, dtype=np.uint8)
    out = np.empty(N, dtype=np.uint8)
    add, axor = PCM2_SWAP_ADDRS[value]
    xd = np.array(PCM2_SWAP_XOR[value], dtype=np.uint8)
    # a blocchi, per non tenere in RAM tre array da 64 MB di indici a 32 bit
    PASSO = 1 << 21
    for base in range(0, N, PASSO):
        i = np.arange(base, base + PASSO, dtype=np.uint32)
        b0  = i & np.uint32(1)
        b16 = (i >> np.uint32(16)) & np.uint32(1)
        j = (i & np.uint32(~0x10001 & 0xFFFFFFFF)) | (b0 << np.uint32(16)) | b16
        j ^= np.uint32(axor)
        d = (i + np.uint32(add)) & np.uint32(0xFFFFFF)
        out[j] = src[d] ^ xd[j & np.uint32(7)]
    return out.tobytes()


# ============================================================================
# CMC50: decrittazione dell'M1, il programma Z80 del sonoro
# ============================================================================
# Porting di neogeo_cmc50_m1_decrypt()/m1_address_scramble(). L'M1 non e' cifrato
# nei DATI: sono gli INDIRIZZI a essere rimescolati, quindi il risultato e' una
# permutazione dei byte di partenza. La chiave e' un checksum a 16 bit dei primi
# 64 KB della ROM cifrata, quindi cambia da gioco a gioco senza tabelle in piu'.
#
# Trascritto da gngeo/src/neocrypt.c — m1_address_scramble(), riga per riga.
# ⚠️ DUE trappole, costate un giro a vuoto quando le costanti venivano da un
#    riassunto invece che dal sorgente:
#      1) la permutazione p1 si passa AL CONTRARIO, da p1[15] a p1[0];
#      2) lo XOR `0_7` viene PRIMA di `8_15`, non dopo.
# ⭐ Si decifra la dimensione VERA della regione, non gli 0x80000 fissi del
#    sorgente: lo scramble non tocca mai `block`, quindi l'indirizzo sorgente
#    resta sempre dentro lo stesso blocco da 64 KB della destinazione. Con
#    0x20000 byte (Metal Slug 4) si decifra tutto e non si legge fuori.
CMC50_M1_P1 = (
    (15, 14, 10,  7,  1,  2,  3,  8,  0, 12, 11, 13,  6,  9,  5,  4),
    ( 7,  1,  8, 11, 15,  9,  2,  3,  5, 13,  4, 14, 10,  0,  6, 12),
    ( 8,  6, 14,  3, 10,  7, 15,  1,  4,  0,  2,  5, 13, 11, 12,  9),
    ( 2,  8, 15,  9,  3,  4, 11,  7, 13,  6,  0, 10,  1, 12, 14,  5),
    ( 1, 13,  6, 15, 14,  3,  8, 10,  9,  4,  7, 12,  5,  2,  0, 11),
    (11, 15,  3,  4,  7,  0,  9,  2,  6, 14, 12,  1,  8,  5, 10, 13),
    (10,  5, 13,  8,  6, 15,  1, 14, 11,  9,  3,  0, 12,  7,  4,  2),
    ( 9,  3,  7,  0,  2, 12,  4, 11, 14, 10,  5,  8, 15, 13,  1,  6),
)
CMC50_M1_KEY = (12, 0, 2, 4, 8, 15, 7, 13, 10, 1, 3, 6, 11, 9, 14, 5)
CMC50_M1_OUT = (7, 15, 14, 6, 5, 13, 12, 4, 11, 3, 10, 2, 9, 1, 8, 0)


def cmc50_m1_decrypt(m1, T):
    """m1: bytes della regione Z80 CIFRATA (regione 2 del .drv)."""
    rom  = np.frombuffer(m1, dtype=np.uint8)
    size = rom.size
    # chiave = somma a 16 bit dei primi 64 KB della ROM CIFRATA
    key = int(rom[:0x10000].sum(dtype=np.int64)) & 0xFFFF
    kx  = int(_bitswap(np.array([key], dtype=np.uint32), CMC50_M1_KEY, 16)[0])

    addr  = np.arange(size, dtype=np.uint32)
    block = (addr >> np.uint32(16)) & np.uint32(7)
    aux   = (addr & np.uint32(0xFFFF)) ^ np.uint32(kx)

    # permutazione dei bit diversa per ogni blocco da 64 KB.
    # ⚠️ AL CONTRARIO: il sorgente scrive BITSWAP16(aux, p1[15], p1[14], ... p1[0])
    mesc = np.zeros_like(aux)
    for b in range(8):
        m = (block == b)
        if m.any():
            mesc[m] = _bitswap(aux[m], tuple(reversed(CMC50_M1_P1[b])), 16)
    aux = mesc

    # ⚠️ 0_7 PRIMA, 8_15 dopo (neocrypt.c:946-947)
    aux ^= T["m1_a0_7_x"][(aux >> np.uint32(8)) & 0xFF].astype(np.uint32)
    aux ^= T["m1_a8_15_x"][aux & 0xFF].astype(np.uint32) << np.uint32(8)

    aux = _bitswap(aux, CMC50_M1_OUT, 16)
    src = ((block << np.uint32(16)) | aux).astype(np.intp)
    return rom[src].tobytes()


def cmc50_m1_decrypt_verificato(m1, T):
    """Decifra e controlla che il risultato SIA un programma Z80.

    ⭐ Rete di sicurezza, non un selettore: l'algoritmo e' trascritto dal
    sorgente. Il driver sonoro SNK comincia sempre con 0xF3 (DI) — verificato
    sull'm.bin gia' convertito di Metal Slug 3, che parte con
    `F3 D3 18 C3 00 10` (DI; OUT (18),A; JP 0x1000) e NON e' cifrato.
    Una permutazione sbagliata da' byte casuali fin dal primo."""
    d = cmc50_m1_decrypt(m1, T)
    testa = " ".join("%02X" % b for b in d[:8])
    if len(d) < 4 or d[0] != 0xF3:
        raise ValueError(
            "decrypt M1 CMC50 NON verificato: inizia con %s, atteso 0xF3 (DI).\n"
            "    Ricontrollare CMC50_M1_* su gngeo/src/neocrypt.c "
            "(m1_address_scramble)." % testa)
    print("    programma Z80 riconosciuto: %s" % testa)
    return d


def gfx_decrypt(rom, extra_xor, T):
    """Port vettorizzato di neogeo_gfx_decrypt() (neocrypt.c:534).
    rom: array uint8 (N,) — viene decifrato sul posto e ritornato."""
    n4  = rom.size // 4
    src = rom.reshape(n4, 4)
    buf = np.empty_like(src)

    rpos = np.arange(n4, dtype=np.uint32)
    hi   = ((rpos >> 8) & 0xFF).astype(np.uint8)   # (base >> 8) & 0xff
    it   = T["a0_7_x"][hi] ^ (rpos & 0xFF).astype(np.uint8)   # indice di table1

    def coppia(c0, c1, t0hi, t0lo, t1, inv):
        """decrypt() di neocrypt.c:507, sui due byte insieme."""
        tmp  = t1[it]
        xor0 = (t0hi[hi] & 0xFE) | (tmp & 0x01)
        xor1 = (tmp & 0xFE) | (t0lo[hi] & 0x01)
        r0 = np.where(inv, c1 ^ xor0, c0 ^ xor0)
        r1 = np.where(inv, c0 ^ xor1, c1 ^ xor1)
        return r0.astype(np.uint8), r1.astype(np.uint8)

    # byte 0 e 3:  invert = (rpos >> 8) & 1
    inv = (((rpos >> 8) & 1) != 0)
    buf[:, 0], buf[:, 3] = coppia(src[:, 0], src[:, 3],
                                  T["type0_t03"], T["type0_t12"], T["type1_t03"], inv)
    # byte 1 e 2:  invert = ((rpos>>16) ^ address_16_23_xor2[(rpos>>8)&0xff]) & 1
    inv2 = ((((rpos >> 16) ^ T["a16_23_x2"][hi]) & 1) != 0)
    buf[:, 1], buf[:, 2] = coppia(src[:, 1], src[:, 2],
                                  T["type0_t12"], T["type0_t03"], T["type1_t12"], inv2)
    del it, hi, inv, inv2, rpos

    # ---- seconda passata: rimescolamento degli INDIRIZZI --------------------
    # ⛔ kf2k3pcb (0x6000000 = 96 MB) resta fuori: non entrerebbe mai qui dentro,
    #    e un ramo mai eseguito e' solo un posto in piu' dove sbagliare.
    if rom.size == 0x6000000:
        raise NotImplementedError(
            "set con clamp speciale kf2k3pcb (0x6000000): vedi neocrypt.c")

    # "Clamp to the real rom size" (neocrypt.c, in fondo al ciclo degli indirizzi).
    # ⭐ Nel caso NORMALE la C-ROM e' una potenza di due e basta una maschera.
    #    I set da 0x3000000 (48 MB) NON lo sono — Metal Slug 4 e' uno di questi,
    #    come preisle2 — e si spezzano in due:
    #      rpos < 32 MB  ->  baser mascherato dentro i primi 32 MB
    #      rpos >= 32 MB ->  baser mascherato dentro 16 MB e rimappato sopra i 32
    #    ⚠️ Il confronto usa `rpos`, l'indice ORIGINALE del ciclo, non `baser`
    #    che a quel punto e' gia' rimescolato.
    SPEZZATA = (rom.size == 0x3000000)
    SPLIT    = 0x2000000 // 4          # 32 MB, in word da 4 byte
    CODA     = 0x1000000 // 4          # 16 MB

    # a pezzi, per non tenere in RAM i 64 MB tre volte
    PEZZO = 1 << 21
    for a in range(0, n4, PEZZO):
        b = min(a + PEZZO, n4)
        rpos_o = np.arange(a, b, dtype=np.uint32)   # indice originale
        baser  = rpos_o.copy()
        baser ^= np.uint32(extra_xor)
        baser ^= T["a8_15_x1"][(baser >> 16) & 0xFF].astype(np.uint32) << np.uint32(8)
        baser ^= T["a8_15_x2"][baser & 0xFF].astype(np.uint32) << np.uint32(8)
        baser ^= T["a16_23_x1"][baser & 0xFF].astype(np.uint32) << np.uint32(16)
        baser ^= T["a16_23_x2"][(baser >> 8) & 0xFF].astype(np.uint32) << np.uint32(16)
        baser ^= T["a0_7_x"][(baser >> 8) & 0xFF].astype(np.uint32)
        if SPEZZATA:
            baser = np.where(rpos_o < np.uint32(SPLIT),
                             baser & np.uint32(SPLIT - 1),
                             np.uint32(SPLIT) + (baser & np.uint32(CODA - 1))
                             ).astype(np.uint32)
        else:
            baser &= np.uint32(n4 - 1)
        src[a:b] = buf[baser]
    return rom


def sfix_da_grafica(spr, tx_size):
    """Port di neogeo_sfix_decrypt() (neocrypt.c:615): nei set CMC la S-ROM non
    esiste, il fix layer sta negli ULTIMI tx_size byte della C-ROM decifrata."""
    s = spr[spr.size - tx_size:]
    i = np.arange(tx_size, dtype=np.uint32)
    idx = ((i & ~np.uint32(0x1F)) + ((i & 7) << np.uint32(2)) +
           ((~i & 8) >> np.uint32(2)) + ((i & 0x10) >> np.uint32(4)))
    return s[idx].tobytes()


# ============================================================================
def leggi_drv(gioco):
    """Mappa delle ROM del gioco, presa dal .drv di gngeo_data.zip."""
    with zipfile.ZipFile(os.path.join(ROMS, "gngeo_data.zip")) as z:
        d = z.read("rom/%s.drv" % gioco)
    nome   = d[0:32].split(b"\0")[0].decode()
    parent = d[32:64].split(b"\0")[0].decode()
    off = 32 + 32 + 128 + 4
    romsize = [struct.unpack_from("<I", d, off + i * 4)[0] for i in range(10)]
    off += 40
    nb = struct.unpack_from("<I", d, off)[0]; off += 4
    files = []
    for _ in range(nb):
        fn = d[off:off + 32].split(b"\0")[0].decode(); off += 32
        reg = d[off]; off += 1
        src, dest, size, crc = struct.unpack_from("<IIII", d, off); off += 16
        files.append(dict(nome=fn, reg=reg, src=src, dest=dest, size=size, crc=crc))
    return nome, parent, romsize, files


def apri_rom(zip_paths):
    """Lettore di ROM da UNO O PIU' zip, provati nell'ordine.
    ⭐ Serve per i CLONI: uno zip MAME di un clone contiene **solo i file che
    cambiano** rispetto al genitore, e tutto il resto (grafica, campioni, Z80)
    va cercato nello zip del parent. Il primo caso e' `mslug3b6`, che porta 3
    file suoi (2 P-ROM e la S1) e ne prende 13 da `mslug3.zip`. Se un giorno il
    parent servisse e mancasse, l'errore dice ESATTAMENTE quale zip cercare."""
    if isinstance(zip_paths, str):
        zip_paths = [zip_paths]
    aperti = []
    for p in zip_paths:
        z = zipfile.ZipFile(p)
        aperti.append((p, z, {n.lower(): n for n in z.namelist()}))

    def leggi(nome):
        k = nome.lower()
        base = os.path.splitext(k)[0]
        for _, z, mappa in aperti:
            if k in mappa:
                return z.read(mappa[k])
            for low, vero in mappa.items():
                if os.path.splitext(low)[0] == base:
                    return z.read(vero)
        raise FileNotFoundError("%s non trovato in %s"
                                % (nome, " ne' in ".join(p for p, _, _ in aperti)))
    return leggi


def converti_tile(raw):
    """Port vettorizzato di convert_roms_tile() (gngeo/roms.c).
    raw: array (N,128) uint8 -> ritorna (N,128) uint8 convertito."""
    n = raw.shape[0]
    out = np.zeros((n, 128), dtype=np.uint8)
    for y in range(16):
        # per ogni riga due dword: prima dalla meta' ALTA del tile (offset 64),
        # poi dalla meta' bassa (offset 0) — e' l'ordine di roms.c
        for meta, base in ((0, 64), (1, 0)):
            b0 = raw[:, base + y * 4 + 0].astype(np.uint32)
            b1 = raw[:, base + y * 4 + 1].astype(np.uint32)
            b2 = raw[:, base + y * 4 + 2].astype(np.uint32)
            b3 = raw[:, base + y * 4 + 3].astype(np.uint32)
            dw = np.zeros(n, dtype=np.uint32)
            for x in range(8):
                pen = ((((b3 >> x) & 1) << 3) | (((b1 >> x) & 1) << 2) |
                       (((b2 >> x) & 1) << 1) |  ((b0 >> x) & 1))
                dw |= pen << ((7 - x) * 4)
            i = (y * 2 + meta) * 4
            out[:, i + 0] = (dw       & 0xFF).astype(np.uint8)
            out[:, i + 1] = ((dw >> 8)  & 0xFF).astype(np.uint8)
            out[:, i + 2] = ((dw >> 16) & 0xFF).astype(np.uint8)
            out[:, i + 3] = ((dw >> 24) & 0xFF).astype(np.uint8)
    return out


def fogli_png(conv, dirout, quanti=4096):
    """Fogli di controllo: si GUARDANO le forme prima di mettere qualcosa sul
    device. La palette vera sta nella RAM del gioco, qui una di comodo."""
    try:
        from PIL import Image
    except ImportError:
        print("  (PIL non installato: salto i fogli PNG)")
        return
    pal = [(0,0,0),(255,255,255),(255,0,0),(0,255,0),(0,0,255),(255,255,0),
           (255,0,255),(0,255,255),(128,128,128),(192,96,0),(0,128,0),(0,0,128),
           (128,0,128),(0,128,128),(192,192,192),(255,128,0)]
    os.makedirs(dirout, exist_ok=True)
    COLS = 32                       # 32 tile da 16x16 per riga = 512 px
    righe = quanti // COLS
    im = Image.new("RGB", (COLS * 16, righe * 16)); px = im.load()
    for t in range(quanti):
        tile = conv[t]
        tx, ty = (t % COLS) * 16, (t // COLS) * 16
        for y in range(16):
            for meta in (0, 1):
                dw = int.from_bytes(bytes(tile[(y*2+meta)*4:(y*2+meta)*4+4]), "little")
                for x in range(8):
                    pen = (dw >> ((7 - x) * 4)) & 0xF
                    # meta' 0 = colonne 8..15, meta' 1 = colonne 0..7
                    px[tx + (8 if meta == 0 else 0) + x, ty + y] = pal[pen]
    p = os.path.join(dirout, "neogeo_sprite_check.png")
    im.save(p)
    print("  %s: primi %d tile" % (p, quanti))


def main():
    args  = [a for a in sys.argv[1:] if not a.startswith("--")]
    png   = "--png" in sys.argv
    gioco = args[0] if args else "mslug"

    zip_gioco = os.path.join(ROMS, "%s.zip" % gioco)
    for f in (zip_gioco, os.path.join(ROMS, "neogeo.zip"),
              os.path.join(ROMS, "gngeo_data.zip")):
        if not os.path.isfile(f):
            print("ERRORE: manca %s" % f); return 1

    nome, parent, romsize, files = leggi_drv(gioco)
    # ⭐ CLONE: i file che non stanno nel suo zip vanno cercati nel PARENT
    # (mslug3b6 prende 13 file su 16 da mslug3.zip). `neogeo` come parent e' il
    # BIOS, non un gioco: quello si legge da neogeo.zip e non c'entra qui.
    zips = [zip_gioco]
    if parent and parent != "neogeo":
        zip_parent = os.path.join(ROMS, "%s.zip" % parent)
        if not os.path.isfile(zip_parent):
            print("ERRORE: %s e' un clone di %s, ma manca %s"
                  % (gioco, parent, zip_parent)); return 1
        zips.append(zip_parent)
    cif = CIFRATI.get(gioco)
    dirout = os.path.join(OUTDIR, gioco)
    os.makedirs(dirout, exist_ok=True)
    print("NEO GEO — %s  (parent: %s)" % (nome, parent))
    print("  da  : %s   (gli zip NON vengono modificati)" % " + ".join(zips))
    print("  a   : %s" % dirout)
    print("  regioni (dal .drv): 68000 %.2f MB · sprite %.2f MB · ADPCM-A %.2f MB"
          " · Z80 %d KB · fix %d KB"
          % (romsize[REG_M68K] / 1048576.0, romsize[REG_SPRITE] / 1048576.0,
             romsize[REG_ADPCMA] / 1048576.0, romsize[REG_Z80] // 1024,
             romsize[REG_FIX] // 1024))
    if romsize[REG_ADPCMB]:
        print("  ATTENZIONE: ADPCM-B presente (%.2f MB), il firmware non lo emula"
              % (romsize[REG_ADPCMB] / 1048576.0))
    if cif:
        print("  set CIFRATO: grafica %s (xor 0x%02X)%s"
              % (cif["gfx"].upper(), cif["xor"],
                 ", P-ROM con SMA" if cif["sma"] else ""))
    print("")

    leggi = apri_rom(zips)
    crc = {}
    t0 = time.time()
    # le tabelle CMC servono sia alla grafica sia (nei CMC50) all'M1: una volta sola
    tab_cmc = carica_tabelle_xor(cif["gfx"]) if (cif and cif.get("gfx")) else None

    def scrivi(nomefile, dati, descr):
        p = os.path.join(dirout, nomefile)
        open(p, "wb").write(dati)
        c = zlib.crc32(dati) & 0xFFFFFFFF
        crc[nomefile] = c
        print("  %-10s %9d byte  CRC32 %08X   %s" % (nomefile, len(dati), c, descr))

    # ---- regioni "semplici": si assemblano rispettando src/dest del .drv ----
    # ⚠️ la fix (s.bin) puo' NON avere file: nei set CMC si ricava dalla C-ROM
    #    ed e' scritta piu' sotto, dopo la decrittazione della grafica.
    for reg, nomefile, descr in ((REG_M68K,   "p.bin", "programma 68000"),
                                 (REG_Z80,    "m.bin", "programma Z80 sonoro"),
                                 (REG_FIX,    "s.bin", "fix layer (testo)"),
                                 (REG_ADPCMA, "v.bin", "campioni ADPCM (YM2610)")):
        voci = [f for f in files if f["reg"] == reg]
        if not voci:
            continue
        # ⛔⛔ P-ROM INTERLACCIATA (mslug5 e la sua famiglia). Nel .drv i due
        #    pezzi stanno a dest 0x0 e dest **0x2**: non e' "il secondo due byte
        #    piu' avanti", e' un interlacciamento a WORD dentro ogni dword —
        #    p1 le word pari, p2 le dispari (il ROM_LOAD32_WORD_SWAP di MAME).
        #    Trattandolo come una copia a blocchi, p2 SOVRASCRIVE p1 e la ROM
        #    esce senza senso. Si riconosce dal dest dispari-ma-non-1: qui e' 2.
        if reg == REG_M68K and len(voci) == 2 and {v["dest"] for v in voci} == {0, 2}:
            a = next(v for v in voci if v["dest"] == 0)
            b = next(v for v in voci if v["dest"] == 2)
            print("  P-ROM: %s + %s INTERLACCIATI a word (dest 0/2)"
                  % (a["nome"], b["nome"]))
            da = np.frombuffer(leggi(a["nome"]), dtype="<u2")[
                a["src"] // 2: a["src"] // 2 + a["size"] // 2]
            db = np.frombuffer(leggi(b["nome"]), dtype="<u2")[
                b["src"] // 2: b["src"] // 2 + b["size"] // 2]
            w = np.empty(da.size + db.size, dtype="<u2")
            w[0::2] = da
            w[1::2] = db
            buf = bytearray(w.tobytes())
            if len(buf) != romsize[reg]:
                raise ValueError("P-ROM interlacciata %d byte, il .drv ne vuole %d"
                                 % (len(buf), romsize[reg]))
        else:
            buf = bytearray(romsize[reg])
            for f in voci:
                d = leggi(f["nome"])
                buf[f["dest"]:f["dest"] + f["size"]] = d[f["src"]:f["src"] + f["size"]]
        # ⭐ per la P-ROM da 2 MB questo realizza lo SCAMBIO DELLE DUE META'
        #    che il .drv prescrive: senza, il 68000 non boota.
        dati = bytes(buf)
        if reg == REG_M68K and cif and cif.get("p68k"):
            print("  P-ROM: decrypt %s (8 MB)..." % cif["p68k"])
            dati = P68K_DECRYPT[cif["p68k"]](dati)
            # ⭐ ORACOLO, come il 0xF3 dell'M1: dopo la decrittazione i primi
            #    8 byte devono essere i vettori del 68000. SSP nella RAM
            #    (0x0010xxxx) e PC nella BIOS (0x00Cxxxxx): se non tornano, la
            #    ROM non arriva nemmeno sulla SD.
            v = np.frombuffer(dati[:8], dtype="<u2")
            ssp = (int(v[0]) << 16) | int(v[1])
            pc  = (int(v[2]) << 16) | int(v[3])
            print("    vettori: SSP=%08X  PC=%08X" % (ssp, pc))
            if (ssp >> 16) != 0x0010 or (pc >> 20) != 0x00C:
                raise ValueError("decrypt 68000 SBAGLIATO: SSP=%08X PC=%08X "
                                 "(attesi 0x0010xxxx / 0x00Cxxxxx)" % (ssp, pc))
        if reg == REG_M68K and cif and cif["sma"]:
            print("  P-ROM: descramble SMA (%s)..." % cif["sma"])
            dati = sma_decrypt_68k(dati, SMA_RECIPES[cif["sma"]])
        if reg == REG_ADPCMA and cif and cif.get("pcm2"):
            print("  campioni: NEO-PCM2 (blocco %d byte)..." % cif["pcm2"])
            dati = pcm2_snk_1999(dati, cif["pcm2"])
        # ⭐ FIX DEI BOOTLEG (sx_decrypt di MAME, prot_misc.cpp:61). Il file c'e'
        # ma e' rimescolato: valore 2 = un bitswap sugli 8 bit di OGNI byte,
        # valore 1 = scambio delle due meta' di ogni blocco da 16 byte.
        if reg == REG_FIX and cif and cif.get("sfix_boot"):
            v = cif["sfix_boot"]
            print("  fix: descramble bootleg (sx_decrypt tipo %d)..." % v)
            a = np.frombuffer(dati, dtype=np.uint8)
            if v == 2:
                # rom[i] = BITSWAP8(rom[i], 7,6,0,4,3,2,1,5): il primo numero e'
                # il bit 7 dell'uscita, l'ultimo il bit 0.
                sorgente = [7, 6, 0, 4, 3, 2, 1, 5]
                out = np.zeros_like(a)
                for pos, bit in enumerate(sorgente):
                    out |= (((a >> np.uint8(bit)) & np.uint8(1)) << np.uint8(7 - pos))
                dati = out.tobytes()
            elif v == 1:
                b = a.reshape(-1, 16).copy()
                b[:, :8], b[:, 8:] = a.reshape(-1, 16)[:, 8:], a.reshape(-1, 16)[:, :8]
                dati = b.tobytes()
            else:
                raise ValueError("sx_decrypt tipo %d non implementato" % v)
        if reg == REG_ADPCMA and cif and cif.get("pcm2_swap") is not None:
            print("  campioni: NEO-PCM2 variante SWAP (tabella %d)..."
                  % cif["pcm2_swap"])
            dati = pcm2_swap(dati, cif["pcm2_swap"])
        scrivi(nomefile, dati, descr)

    # ---- M1 cifrato (regione 2): nei set CMC50 il programma sonoro sta QUI ---
    # ⚠️ Va dopo il ciclo qui sopra, non dentro: la regione 1 e' vuota e quel
    #    ciclo non produrrebbe nessun m.bin. Senza, il gioco sarebbe muto.
    if cif and cif.get("m1") == "cmc50":
        voci = [f for f in files if f["reg"] == REG_Z80_CRYPT]
        if not voci:
            raise ValueError("set con M1 CMC50 ma regione 2 vuota nel .drv")
        buf = bytearray(romsize[REG_Z80_CRYPT])
        for f in voci:
            d = leggi(f["nome"])
            buf[f["dest"]:f["dest"] + f["size"]] = d[f["src"]:f["src"] + f["size"]]
        print("  M1: decrypt CMC50 (%d KB)..." % (len(buf) // 1024))
        scrivi("m.bin", cmc50_m1_decrypt_verificato(bytes(buf), tab_cmc),
               "programma Z80 sonoro (decifrato)")

    # ---- sprite: interlacciamento a coppie + conversione dei tile ----
    spr = [f for f in files if f["reg"] == REG_SPRITE]
    if spr:
        tot = romsize[REG_SPRITE]
        pari    = sorted([f for f in spr if (f["dest"] & 1) == 0], key=lambda f: f["dest"])
        dispari = sorted([f for f in spr if (f["dest"] & 1) == 1], key=lambda f: f["dest"])
        print("\n  sprite: %d MB in %d coppie da interlacciare" % (tot // (1024*1024), len(pari)))
        buf = np.zeros(tot, dtype=np.uint8)
        # ⛔⛔⛔ UN FILE DI SPRITE PUO' ESSERE USATO IN **DUE PEZZI**, e il `src`
        # del .drv NON e' un offset dentro il file: e' in byte di REGIONE
        # INTERLACCIATA, cioe' **il doppio**.
        # Il primo caso e' VIEW POINT, ma sono 26 i set che lo fanno (kotm2,
        # ssideki, wh1, fatfury2...). Il .drv dice:
        #     051-c1.bin  src 0x000000 -> dest 0x000000  size 0x100000
        #     051-c1.bin  src 0x200000 -> dest 0x400000  size 0x100000
        # e `src 0x200000` su un file di 0x200000 byte sarebbe **la fine del
        # file**: preso alla lettera non si leggerebbe niente. La fonte lo dice
        # in chiaro ([[feedback_mame_driver_source]]):
        #     ROM_LOAD16_BYTE("051-c1.c1", 0x000000, 0x100000)
        #     ROM_CONTINUE   (            0x400000, 0x100000)
        # cioe' il secondo pezzo comincia a **0x100000 del FILE** = src/2. Ogni
        # byte del file occupa due byte di regione, ed e' quella l'unita' di
        # misura che gngeo usa qui.
        # ⚠️ Fino a Blazing Star `size` era sempre la dimensione INTERA del file
        # e questo codice prendeva il file tutto: con View Point falliva con un
        # «could not broadcast input array». L'errore era il sintomo giusto —
        # una misura che «vale per tutti» non valeva piu'
        # ([[feedback_costante_del_primo_caso]]).
        def pezzo(f):
            d = np.frombuffer(leggi(f["nome"]), dtype=np.uint8)
            s = f["src"] // 2                     # byte di regione -> byte di file
            if s + f["size"] > len(d):
                raise ValueError(
                    "%s: servono %d byte dall'offset 0x%X ma il file ne ha %d.\n"
                    "        (src del .drv 0x%X, interpretato come 0x%X nel file)"
                    % (f["nome"], f["size"], s, len(d), f["src"], s))
            return d[s:s + f["size"]]

        for e, o in zip(pari, dispari):
            assert e["size"] == o["size"], "coppia sbilanciata: %s / %s" % (e["nome"], o["nome"])
            base = e["dest"] & ~1
            print("    %s + %s -> 0x%07X%s" % (e["nome"], o["nome"], base,
                  "  (dal byte 0x%X del file)" % (e["src"] // 2) if e["src"] else ""))
            de = pezzo(e)
            do = pezzo(o)
            buf[base     : base + 2 * e["size"] : 2] = de     # byte pari
            buf[base + 1 : base + 2 * e["size"] : 2] = do     # byte dispari

        # ---- decrittazione CMC42/CMC50 della grafica (set protetti) --------
        if cif and cif["gfx"]:
            t1 = time.time()
            print("  decifro la grafica %s (xor 0x%02X)..."
                  % (cif["gfx"].upper(), cif["xor"]))
            buf = gfx_decrypt(buf, cif["xor"], carica_tabelle_xor(cif["gfx"]))
            print("    fatto in %.1fs" % (time.time() - t1))
            # la fix esce dalla C-ROM appena decifrata (nessun file nello zip)
            if romsize[REG_FIX] and not [f for f in files if f["reg"] == REG_FIX]:
                scrivi("s.bin", sfix_da_grafica(buf, romsize[REG_FIX]),
                       "fix layer ricavato dalla C-ROM")

        n_tiles = tot >> 7
        print("  converto %d tile 16x16 (formato veloce, stessa dimensione)..." % n_tiles)
        conv = converti_tile(buf.reshape(n_tiles, 128))
        del buf

        # tile completamente trasparenti (solo pen 0) = da saltare nel rendering
        invisibili = np.all(conv == 0, axis=1)
        print("  tile con grafica: %d   invisibili: %d (%.1f%% saltabile)"
              % (n_tiles - invisibili.sum(), invisibili.sum(),
                 100.0 * invisibili.sum() / n_tiles))

        scrivi("c.ctile", conv.tobytes(), "sprite convertiti")

        # .cusage: 2 bit per tile, 16 tile per dword (formato di roms.c)
        usage = np.zeros((n_tiles + 15) // 16, dtype=np.uint32)
        idx = np.nonzero(invisibili)[0]
        np.bitwise_or.at(usage, idx >> 4, (1 << ((idx & 15) * 2)).astype(np.uint32))
        scrivi("c.cusage", struct.pack("<I", 0x43553039) + usage.tobytes(),
               "mappa dei tile invisibili")

        if png:
            print("\n  foglio di controllo della grafica:")
            fogli_png(conv, "_verify_gfx")

    # ---- BIOS: uguale per tutti i giochi, si scrive una volta ----
    bdir = os.path.join(OUTDIR, "bios")
    os.makedirs(bdir, exist_ok=True)
    print("\n  BIOS Neo Geo (comune a tutti i giochi):")
    bl = apri_rom(os.path.join(ROMS, "neogeo.zip"))
    for dentro, fuori in BIOS:
        d = bl(dentro)
        open(os.path.join(bdir, fuori), "wb").write(d)
        print("    %-9s %8d byte  CRC32 %08X" % (fuori, len(d), zlib.crc32(d) & 0xFFFFFFFF))

    tot_b = sum(os.path.getsize(os.path.join(dirout, f)) for f in os.listdir(dirout))
    print("\nfatto in %.1fs — %s: %.2f MB" % (time.time() - t0, gioco, tot_b / 1048576))
    print("\nDa incollare nel firmware (verifica CRC all'avvio):")
    # ⚠️ la misura dell'M1 NON e' romsize[REG_Z80]: nei set CMC50 quella regione
    #    e' VUOTA e il programma sonoro esce dalla 2 (cifrata). Si prende dal
    #    file scritto davvero, altrimenti qui uscirebbe "m_sz 0x0" e nel
    #    firmware il bank switch del Z80 non copierebbe niente.
    m_sz = os.path.getsize(os.path.join(dirout, "m.bin")) \
        if os.path.exists(os.path.join(dirout, "m.bin")) else 0
    print("    prom_sz 0x%X · ctile_sz 0x%X · vrom_sz 0x%X · fix_sz 0x%X · m_sz 0x%X"
          % (romsize[REG_M68K], romsize[REG_SPRITE], romsize[REG_ADPCMA],
             romsize[REG_FIX], m_sz))
    for f in ("p.bin", "m.bin", "s.bin", "v.bin", "c.ctile", "c.cusage"):
        if f in crc:
            print('    { "/sdcard/neogeo/%s/%-9s 0x%08X },' % (gioco, f + '",', crc[f]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
