#!/usr/bin/env python3
# ============================================================================
# convert_supxevs_grid_logo.py — icona 72x48 della griglia GIOCHI ARCADE per
# Vs. Super Xevious.
#
# ⭐ La sorgente non e' una foto presa in rete: e' **la schermata del titolo
#    generata dall'emulatore stesso** (_verify_gfx/supxevs_harness), cioe' i
#    pixel veri del gioco. Basta lanciare l'harness una volta:
#        cd _verify_gfx/supxevs_harness && python run.py 300
#    che scrive supxevs_f0300.ppm; questo script ne ritaglia il logo.
#
# Per le icone della griglia l'utente vuole l'ASPECT RATIO PRESERVATO con
# letterbox su nero (non lo stretch: quello vale per i loghi grandi/flyer).
#
# USO:  python convert_supxevs_grid_logo.py [sorgente.ppm] [uscita.h]
# ============================================================================
# --- percorsi: questo script viveva altrove, vedi PY\_fix_percorsi.py ---
import os as _os
__file__ = _os.path.abspath(_os.path.join(
    _os.path.dirname(_os.path.abspath(__file__)), "..", "..", "FILE ROMCONV PY",
    _os.path.basename(__file__)))
# --- fine blocco percorsi ---

import os
import sys

QUI = os.path.dirname(os.path.abspath(__file__))
PPM_DEFAULT = os.path.join(QUI, "..", "_verify_gfx", "supxevs_harness", "supxevs_f0300.ppm")
OUT_DEFAULT = os.path.join(QUI, "..", "BACKUP SPINNERINO PLUS", "SPINNERINO", "supxevs_grid_logo.h")

GLOGO_W, GLOGO_H = 72, 48

# Ritaglio del logo dentro la schermata 256x240: il disegno "SUPER XEVIOUS"
# con la scritta giapponese sotto. Misurato sul PNG del titolo.
CROP_X, CROP_Y, CROP_W, CROP_H = 40, 40, 176, 96


def leggi_ppm(path):
    with open(path, "rb") as f:
        dati = f.read()
    if dati[:2] != b"P6":
        raise SystemExit("ERRORE: %s non e' un PPM binario (P6)" % path)
    campi, i = [], 2
    while len(campi) < 3:
        while i < len(dati) and dati[i:i + 1].isspace():
            i += 1
        if dati[i:i + 1] == b"#":
            while dati[i:i + 1] not in (b"\n", b""):
                i += 1
            continue
        j = i
        while j < len(dati) and not dati[j:j + 1].isspace():
            j += 1
        campi.append(int(dati[i:j]))
        i = j
    i += 1
    w, h, _ = campi
    return w, h, dati[i:i + w * h * 3]


def rgb565_swap(r, g, b):
    c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return ((c >> 8) & 0xFF) | ((c & 0xFF) << 8)


def main():
    ppm = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else PPM_DEFAULT)
    out = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else OUT_DEFAULT)

    if not os.path.isfile(ppm):
        raise SystemExit(
            "ERRORE: manca %s\n"
            "        Generalo con: cd _verify_gfx/supxevs_harness && python run.py 300" % ppm)

    w, h, px = leggi_ppm(ppm)
    print("sorgente %dx%d, ritaglio (%d,%d) %dx%d" % (w, h, CROP_X, CROP_Y, CROP_W, CROP_H))

    # fit "contain": aspect preservato, bande nere ai lati (regola per le icone
    # della griglia, non lo stretch dei loghi grandi).
    scala = min(GLOGO_W / CROP_W, GLOGO_H / CROP_H)
    dest_w = max(1, int(CROP_W * scala))
    dest_h = max(1, int(CROP_H * scala))
    off_x = (GLOGO_W - dest_w) // 2
    off_y = (GLOGO_H - dest_h) // 2

    valori = []
    for y in range(GLOGO_H):
        for x in range(GLOGO_W):
            if not (off_x <= x < off_x + dest_w and off_y <= y < off_y + dest_h):
                valori.append(0x0000)
                continue
            # media a box del blocco sorgente (riduzione morbida, niente aliasing)
            sx0 = CROP_X + int((x - off_x) * CROP_W / dest_w)
            sx1 = CROP_X + int((x - off_x + 1) * CROP_W / dest_w)
            sy0 = CROP_Y + int((y - off_y) * CROP_H / dest_h)
            sy1 = CROP_Y + int((y - off_y + 1) * CROP_H / dest_h)
            sx1 = max(sx1, sx0 + 1)
            sy1 = max(sy1, sy0 + 1)
            r = g = b = n = 0
            for sy in range(sy0, min(sy1, h)):
                base = (sy * w) * 3
                for sx in range(sx0, min(sx1, w)):
                    p = base + sx * 3
                    r += px[p]; g += px[p + 1]; b += px[p + 2]
                    n += 1
            if n == 0:
                valori.append(0x0000)
            else:
                valori.append(rgb565_swap(r // n, g // n, b // n))

    righe = []
    for i in range(0, len(valori), 12):
        righe.append("  " + ",".join("0x%04x" % v for v in valori[i:i + 12]))

    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("// Vs. Super Xevious -- icona %dx%d della griglia GIOCHI ARCADE.\n"
                "// Ritagliata dalla SCHERMATA DEL TITOLO generata dall'emulatore\n"
                "// (_verify_gfx/supxevs_harness), non da un'immagine esterna.\n"
                "// RGB565 byte-swapped, aspect preservato con letterbox nero.\n"
                "// (!) NON modificare a mano: FILE ROMCONV PY/convert_supxevs_grid_logo.py\n"
                "const unsigned short supxevs_grid_logo[%d] = {\n" % (GLOGO_W, GLOGO_H, len(valori)))
        f.write(",\n".join(righe))
        f.write("\n};\n")

    print("scritto %s (%d pixel)" % (out, len(valori)))


if __name__ == "__main__":
    main()
