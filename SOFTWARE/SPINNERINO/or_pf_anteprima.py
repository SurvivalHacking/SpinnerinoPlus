#!/usr/bin/env python3
# ============================================================================
# or_pf_anteprima.py — mostra COM'E' FATTO l'overlay di misura di OUT RUN
#                      (outrun.cpp, blocco OR_MISURA) senza flashare niente.
#
# ⭐⭐ Serve perche' **la geometria di un overlay non si calcola, si GUARDA**
# ([[feedback_fps_a_passo_fisso_sono_ciechi]]): su questo pannello «in basso per
# il giocatore» NON e' «in basso nel framebuffer» — il monitor si pilota
# TRASPOSTO, e le coordinate della striscia sono girate e invertite rispetto a
# quelle di chi gioca. Su Raiden quel passaggio era rimasto «calcolato e non
# visto» per mesi ed e' costato un referto dell'utente.
#
# Lo script rifa' OFFLINE gli stessi due passaggi del firmware:
#   1. `or_pf_blit()` scrive nella finestra della banda:
#         k    = riga verticale dentro la banda (0 = subito sotto il gioco)
#         y_l  = 239 - colonna del giocatore
#   2. il pannello e' trasposto, quindi il giocatore vede  px = 239 - y_l,
#      py = x_l
# e salva il PNG di **cio' che vede il giocatore**.
#
# ⚠️ Il font, le misure, i colori E LE STRINGHE DI FORMATO sono letti DAI FILE
# VERI (`emulation/font8.h` e `outrun.cpp`), non ricopiati: se qualcuno allunga
# una riga e non rilancia questo script, l'estrazione fallisce o l'anteprima
# denuncia lo sforamento ([[feedback_verify_against_source_not_copy]] ·
# [[feedback_harness_orfani_segnalano_rollback]]).
#
# USO: python or_pf_anteprima.py        -> or_pf_anteprima.png
# ============================================================================
import os
import re
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("ERRORE: serve Pillow (pip install pillow)")

QUI = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------- i sorgenti
def testo(nome):
    return open(os.path.join(QUI, nome), encoding="utf-8", errors="replace").read()


def define_num(txt, nome):
    m = re.search(r"#define\s+%s\s+(0x[0-9A-Fa-f]+|\d+)" % nome, txt)
    if not m:
        sys.exit("ERRORE: non trovo #define %s" % nome)
    return int(m.group(1), 0)


def leggi_font(txt):
    """I 45 glifi 8x8 di emulation/font8.h."""
    m = re.search(r"FONT8\s*\[\]\[8\]\s*PROGMEM\s*=\s*\{(.*?)\n\};", txt, re.S)
    if not m:
        sys.exit("ERRORE: non trovo FONT8 in emulation/font8.h")
    glifi = []
    for riga in re.findall(r"\{([^{}]*)\}", m.group(1)):
        v = [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+", riga)]
        if len(v) == 8:
            glifi.append(v)
    if len(glifi) < 45:
        sys.exit("ERRORE: letti solo %d glifi" % len(glifi))
    return glifi


def leggi_formati(txt):
    """Le tre stringhe di formato delle snprintf() di or_pf_giro(), IN ORDINE.
    Prese dal sorgente apposta: sono loro a decidere la larghezza delle righe."""
    corpo = txt[txt.index("static void or_pf_giro"):]
    corpo = corpo[:corpo.index("\n}\n")]
    f = re.findall(r'snprintf\(or_pf_riga\[\d\][^"]*"([^"]*)"', corpo)
    if len(f) != 3:
        sys.exit("ERRORE: trovate %d stringhe di formato invece di 3" % len(f))
    return f


def py_fmt(f):
    """`%2lu` -> `%2d`, `%3lu` -> `%3d`, `%lu` -> `%d`."""
    return re.sub(r"%(\d*)lu", r"%\1d", f)


def idx(c):
    if "A" <= c <= "Z": return ord(c) - ord("A")
    if "a" <= c <= "z": return ord(c) - ord("a")
    if "0" <= c <= "9": return 26 + ord(c) - ord("0")
    return {" ": 36, "/": 37, ":": 38, "<": 39, ">": 40,
            "!": 41, ".": 42, "-": 43, "_": 44}.get(c, 36)


def main():
    src  = testo("outrun.cpp")
    cfg  = testo("config.h")
    font = leggi_font(testo(os.path.join("emulation", "font8.h")))

    TFT_FB_W = define_num(cfg, "TFT_FB_W")            # 320
    OR_H     = define_num(src, "OR_H")                # 224, altezza del gioco
    LD_FB_H  = define_num(src, "LD_FB_H")             # 240
    ZOOM     = define_num(src, "OR_PF_ZOOM")
    RIGHE    = define_num(src, "OR_PF_RIGHE")
    COL      = define_num(src, "OR_PF_COL")
    X_OFF    = (TFT_FB_W - OR_H) // 2                 # OR_X_OFF = 48
    X0       = X_OFF + OR_H                           # OR_PF_X0 = 272
    BANDA_H  = TFT_FB_W - X0                          # OR_PF_H  = 48
    LARG     = LD_FB_H                                # OR_PF_LARG = 240

    # colori BYTE-SWAPPATI nel sorgente (display SPI): per il PNG si rigirano
    GIALLO = define_num(src, "OR_PF_GIALLO")
    BIANCO = define_num(src, "OR_PF_BIANCO")
    GIALLO = ((GIALLO & 0xFF) << 8) | (GIALLO >> 8)
    BIANCO = ((BIANCO & 0xFF) << 8) | (BIANCO >> 8)

    print("banda a x_l %d..%d (%s per il giocatore), %d px su %d"
          % (X0, X0 + BANDA_H - 1, "in fondo" if X0 > TFT_FB_W // 2 else "in cima",
             BANDA_H, TFT_FB_W))
    print("righe %d x %d px (zoom %d) = %d px: %s"
          % (RIGHE, 8 * ZOOM, ZOOM, RIGHE * 8 * ZOOM,
             "ci stanno" if RIGHE * 8 * ZOOM <= BANDA_H else "NON CI STANNO"))
    print("colonne %d x 8 px = %d px sui %d del giocatore" % (COL, COL * 8, LARG))

    fmt = [py_fmt(f) for f in leggi_formati(src)]
    # due giochi di valori: uno PLAUSIBILE (quello che ci si aspetta di leggere
    # su Out Run) e uno col MASSIMO in ogni campo, per vedere se sfora.
    plaus = [(30, 60, 118, 24, 3), (8, 6, 6, 0, 12, 4), (500, 12, 19, 7)]
    massi = [(99, 99, 999, 99, 9), (99, 9, 99, 9, 99, 9), (999, 999, 99, 9)]

    for nome, giochi in (("plausibile", plaus), ("massimo", massi)):
        for i, (f, v) in enumerate(zip(fmt, giochi)):
            s = f % v
            stato = "ok" if len(s) <= COL else "SFORA di %d" % (len(s) - COL)
            print("  %-10s riga %d: %2d/%d car  %s  |%s|"
                  % (nome, i, len(s), COL, stato, s))
            if len(s) > COL:
                sys.exit("ERRORE: la riga %d sfora i %d caratteri" % (i, COL))

    righe = [(f % v)[:COL] for f, v in zip(fmt, plaus)]

    # ---- passo 1: quello che or_pf_blit() scrive nella finestra della banda --
    land = [[0] * TFT_FB_W for _ in range(LARG)]        # [y_l][x_l]
    for y_l in range(LARG):
        px = (LARG - 1) - y_l                           # colonna del GIOCATORE
        col, gx = px >> 3, px & 7
        for k in range(BANDA_H):
            c = 0
            riga = k // (8 * ZOOM)
            gy   = (k % (8 * ZOOM)) // ZOOM
            if riga < RIGHE and col < COL and col < len(righe[riga]):
                ch = righe[riga][col]
                if font[idx(ch)][gy] & (0x80 >> gx):
                    c = GIALLO if riga == 0 else BIANCO
            land[y_l][X0 + k] = c

    # il quadro di gioco (grigio) e la banda della BARRA DEL VOLUME (x_l 0..15,
    # blu), per far vedere che le due bande non si pestano i piedi
    for y_l in range(LARG):
        for x_l in range(X_OFF, X_OFF + OR_H):
            land[y_l][x_l] = 0x2104                     # grigio = area di gioco
        for x_l in range(0, 16):
            if 64 <= y_l <= 175:                        # videoVolBarTop: LO..HI
                land[y_l][x_l] = 0x001F                 # blu = barra del volume

    # ---- passo 2: il pannello e' trasposto -> cosa vede il giocatore --------
    im = Image.new("RGB", (LARG, TFT_FB_W))
    pix = im.load()
    for y_l in range(LARG):
        for x_l in range(TFT_FB_W):
            v = land[y_l][x_l]
            px_, py_ = (LARG - 1) - y_l, x_l            # coordinate giocatore
            pix[px_, py_] = (((v >> 11) & 31) * 255 // 31,
                             ((v >> 5) & 63) * 255 // 63,
                             (v & 31) * 255 // 31)

    fuori = os.path.join(QUI, "or_pf_anteprima.png")
    im.resize((LARG * 2, TFT_FB_W * 2), Image.NEAREST).save(fuori)
    print("scritto %s - e' cio' che vede il GIOCATORE (2x)" % fuori)
    return 0


if __name__ == "__main__":
    sys.exit(main())
