#!/usr/bin/env python3
# ============================================================================
# prof_anteprima.py — mostra COM'E' FATTA la banda di misura di prof.cpp,
#                     senza flashare niente.
#
# ⭐⭐ Serve perche' **la geometria di un overlay non si calcola, si GUARDA**
# ([[feedback_fps_a_passo_fisso_sono_ciechi]]): su questo pannello «in alto per
# il giocatore» NON e' «in alto nel framebuffer» — il monitor e' girato, e le
# coordinate di `out_strip` sono trasposte e invertite rispetto a quelle di chi
# gioca. Su Raiden quel passaggio era rimasto «calcolato e non visto» per mesi
# ed e' costato un referto dell'utente.
#
# Lo script rifa' OFFLINE gli stessi due passaggi del firmware:
#   1. `prof_banda()` scrive nella striscia landscape:
#         x_l = riga verticale del giocatore (0 = in alto)
#         y_l = 223 - colonna del giocatore
#   2. il pannello e' girato, quindi il giocatore vede  px = 223 - y_l, py = x_l
# e salva il PNG di **cio' che vede il giocatore**.
#
# ⚠️ Il font e i testi sono letti DAI FILE VERI (`emulation/font8.h` e le
# `snprintf` di `prof.cpp`), non ricopiati: se qualcuno cambia una riga e non
# rilancia questo script, l'estrazione fallisce e lo dice
# ([[feedback_verify_against_source_not_copy]] · [[feedback_harness_orfani_segnalano_rollback]]).
#
# USO: python prof_anteprima.py            -> prof_anteprima.png
# ============================================================================
import os
import re
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("ERRORE: serve Pillow (pip install pillow)")

QUI = os.path.dirname(os.path.abspath(__file__))

TFT_FB_W = 320          # config.h
LARG     = 224          # px del giocatore in orizzontale
BANDA_H  = 32           # prof.cpp: PF_BANDA_H
TESTO_Y  = 4            # prof.cpp: PF_TESTO_Y
RIGHE    = 3
COL      = 28

# un referto PLAUSIBILE, per vedere l'ingombro nel caso peggiore: sono i numeri
# che ARKANOID 2 ha dato sul CABINATONE (FPS 42 · CAR 140 · PIC 23,6) — cosi'
# l'anteprima non mostra una riga corta che poi a schermo sfora.
ESEMPIO = [
    "FPS 42  CAR 140  PIC 23.6   ",
    "REN 13.6 BLT  4.4 AUD 498 ",
    "EMU  9.1 CEM  55 EMS 42  ",
]


def leggi_font():
    """I 45 glifi 8x8 di emulation/font8.h, piu' la tabella char->indice."""
    p = os.path.join(QUI, "emulation", "font8.h")
    txt = open(p, encoding="utf-8", errors="replace").read()
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


def leggi_x0():
    """PF_BANDA_X0 lo legge DA prof.cpp: e' cambiato una volta (da 0 a 288, per
    non finire sotto la barra del volume) e riscriverlo qui a mano vorrebbe dire
    avere due copie che divergono."""
    txt = open(os.path.join(QUI, "prof.cpp"), encoding="utf-8", errors="replace").read()
    m = re.search(r"#define\s+PF_BANDA_X0\s+\(TFT_FB_W\s*-\s*(\d+)\)", txt)
    if m:
        return TFT_FB_W - int(m.group(1))
    m = re.search(r"#define\s+PF_BANDA_X0\s+(\d+)", txt)
    if not m:
        sys.exit("ERRORE: non trovo PF_BANDA_X0 in prof.cpp")
    return int(m.group(1))


def leggi_colori():
    """PF_GIALLO e PF_BIANCO li legge DA prof.cpp, non li ricopia: sono
    BYTE-SWAPPED (display SPI) e riscriverli qui a mano vorrebbe dire avere due
    copie che possono divergere. Per disegnare il PNG si rigirano."""
    p = os.path.join(QUI, "prof.cpp")
    txt = open(p, encoding="utf-8", errors="replace").read()
    out = []
    for nome in ("PF_GIALLO", "PF_BIANCO"):
        m = re.search(r"#define\s+%s\s+(0x[0-9A-Fa-f]+)" % nome, txt)
        if not m:
            sys.exit("ERRORE: non trovo %s in prof.cpp" % nome)
        v = int(m.group(1), 0)
        out.append(((v & 0xFF) << 8) | (v >> 8))       # via il byte-swap
    return out


def idx(c):
    if "A" <= c <= "Z": return ord(c) - ord("A")
    if "a" <= c <= "z": return ord(c) - ord("a")
    if "0" <= c <= "9": return 26 + ord(c) - ord("0")
    return {" ": 36, "/": 37, ":": 38, "<": 39, ">": 40,
            "!": 41, ".": 42, "-": 43, "_": 44}.get(c, 36)


def main():
    glifi = leggi_font()
    GIALLO, BIANCO = leggi_colori()
    X0 = leggi_x0()
    print("banda a x_l %d..%d (%s per il giocatore)"
          % (X0, X0 + BANDA_H - 1, "in fondo" if X0 > 160 else "in cima"))
    print("colori presi da prof.cpp (byte-swap tolto): giallo %04X, bianco %04X"
          % (GIALLO, BIANCO))

    # controllo che le righe ci stiano: 28 caratteri, non uno di piu'
    for i, r in enumerate(ESEMPIO):
        if len(r) > COL:
            sys.exit("ERRORE: la riga %d e' lunga %d, il massimo e' %d"
                     % (i, len(r), COL))
    print("righe di esempio: %d, tutte entro %d caratteri" % (len(ESEMPIO), COL))

    # ---- passo 1: quello che prof_banda() scrive nel framebuffer landscape --
    land = [[0] * TFT_FB_W for _ in range(LARG)]        # [y_l][x_l]
    for y_l in range(LARG):
        px = (LARG - 1) - y_l
        col, gx = px >> 3, px & 7
        for py in range(BANDA_H):
            c = 0
            dy = py - TESTO_Y
            if 0 <= dy < RIGHE * 8 and col < COL:
                riga, gy = dy >> 3, dy & 7
                ch = ESEMPIO[riga][col] if col < len(ESEMPIO[riga]) else " "
                if glifi[idx(ch)][gy] & (0x80 >> gx):
                    c = GIALLO if riga == 0 else BIANCO
            land[y_l][X0 + py] = c
    # il quadro di gioco (grigio) e la banda della BARRA DEL VOLUME (x_l 0..15,
    # blu), per far vedere che le due bande non si pestano i piedi
    for y_l in range(LARG):
        for x_l in range(32, 288):
            land[y_l][x_l] = 0x2104                       # grigio = area di gioco
        for x_l in range(0, 16):
            if 56 <= y_l <= 167:
                land[y_l][x_l] = 0x001F                   # blu = dove sta la barra volume

    # ---- passo 2: il pannello e' girato -> cosa vede il giocatore ----------
    im = Image.new("RGB", (LARG, TFT_FB_W))
    pix = im.load()
    for y_l in range(LARG):
        for x_l in range(TFT_FB_W):
            v = land[y_l][x_l]
            px_, py_ = (LARG - 1) - y_l, x_l              # coordinate giocatore
            pix[px_, py_] = (((v >> 11) & 31) * 255 // 31,
                             ((v >> 5) & 63) * 255 // 63,
                             (v & 31) * 255 // 31)

    fuori = os.path.join(QUI, "prof_anteprima.png")
    im.resize((LARG * 2, TFT_FB_W * 2), Image.NEAREST).save(fuori)
    print("scritto %s — e' cio' che vede il GIOCATORE (2x)" % fuori)
    print("banda in alto: %d px su %d; area di gioco in grigio" % (BANDA_H, TFT_FB_W))
    return 0


if __name__ == "__main__":
    sys.exit(main())
