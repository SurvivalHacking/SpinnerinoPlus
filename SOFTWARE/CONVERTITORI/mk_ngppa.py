# ============================================================================
# mk_ngppa.py — verifica OFFLINE della geometria del blit PPA di NEO GEO.
#
# PERCHE': si sta sostituendo la trasposizione software di ng_blit() con il PPA
# del P4 (scala + rotazione in DMA). Il verso della rotazione **si guarda, non si
# deduce** ([[ppa_srm_ordine_operazioni]]: su Mazinger un simulatore con due
# rotazioni sbagliate che si compensavano aveva "dimostrato" il contrario).
#
# COSA FA: prende il frame vero prodotto dall'harness, applica
#   A) la formula del ng_blit() SOFTWARE che gira oggi sul device
#   B) la catena PPA (scala 0,75 su x, poi rotazione 90 ANTIORARIA)
# e le confronta pixel per pixel, salvando i due PNG.
#
# Le due NON possono venire identiche al 100%: il software tiene le colonne
# {0,1,3} di ogni gruppo di 4 (KEEP), il PPA usa il suo accumulatore. Cio' che
# deve combaciare e' la GEOMETRIA — orientamento, bordi, proporzioni.
# ============================================================================
import sys, pathlib
from PIL import Image

QUI = pathlib.Path(__file__).parent
SRC = pathlib.Path(r"C:\Users\Tecnico\Desktop\PROGETTI SH\CABINATONE\_verify_gfx"
                   r"\neogeo_harness\neogeo_frame_mslug.png")

NG_VW, NG_VH = 320, 224      # quadro visibile del NeoGeo
NG_VX, NG_VY = 16, 16        # ⚠️ dove sta lo schermo DENTRO il quadro 352x256
LD_FB_H      = 240           # righe della finestra sul pannello
KEEP         = (0, 1, 3)     # ng_blit(): indici tenuti in ogni gruppo di 4

im = Image.open(SRC).convert("RGB")
print("sorgente:", SRC.name, im.size)
if im.size != (NG_VW, NG_VH):
    # ⛔ Il quadro di gngeo e' 352x256 e lo SCHERMO sta a (16,16): ritagliare da
    # (0,0) prenderebbe il margine che serve agli sprite che entrano da sinistra
    # — e' lo stesso +16 che ng_blit() applica a `src`.
    im = im.crop((NG_VX, NG_VY, NG_VX + NG_VW, NG_VY + NG_VH))
    print("  ritagliata a", im.size, "da offset", (NG_VX, NG_VY))
px = im.load()

# ---------------------------------------------------------------- A) software
# Trascritta da ng_blit():
#   x_p    = (LD_FB_H-1) - riga_pannello        colonna UTENTE 0..239
#   gx     = (x_p/3)*4 + KEEP[x_p%3]            colonna SORGENTE 0..319
#   out(riga_pannello, colonna) = src(gx, colonna)
A = Image.new("RGB", (NG_VH, LD_FB_H))
a = A.load()
for r in range(LD_FB_H):
    x_p = (LD_FB_H - 1) - r
    gx  = (x_p // 3) * 4 + KEEP[x_p % 3]
    if gx >= NG_VW:
        continue
    for c in range(NG_VH):
        a[c, r] = px[gx, c]
A.save(QUI / "ngppa_A_software.png")

# ---------------------------------------------------------------- B) catena PPA
# 1. SCALA nel verso del GIOCO (il PPA scala PRIMA di ruotare):
#    con rotazione 90/270 le scale vanno INCROCIATE
#      scale_x = ALTEZZA_USCITA / W_GIOCO = 240/320 = 0,75
#      scale_y = LARGHEZZA_USCITA / H_GIOCO = 224/224 = 1,0
S = im.resize((int(NG_VW * 0.75), NG_VH), Image.NEAREST)   # 240 x 224
# 2. ROTAZIONE 90 ANTIORARIA (ppa.h: "Rotation (counter-clockwise)")
B = S.transpose(Image.ROTATE_90)                            # -> 224 x 240
B.save(QUI / "ngppa_B_ppa90.png")

# ...e la variante sbagliata, per vedere che differenza fa (270 invece di 90)
C = S.transpose(Image.ROTATE_270)
C.save(QUI / "ngppa_C_ppa270.png")

print("A software :", A.size)
print("B ppa  90  :", B.size)
print("C ppa 270  :", C.size)

def confronta(nome, X, Y):
    if X.size != Y.size:
        print(f"{nome}: DIMENSIONI DIVERSE {X.size} vs {Y.size}")
        return
    xx, yy = X.load(), Y.load()
    n = diff = 0
    for r in range(X.size[1]):
        for c in range(X.size[0]):
            n += 1
            if xx[c, r] != yy[c, r]:
                diff += 1
    print(f"{nome}: {diff} pixel diversi su {n}  ({100.0*diff/n:.1f}%)")

confronta("A software vs B ppa 90 ", A, B)
confronta("A software vs C ppa 270", A, C)
