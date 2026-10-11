# ============================================================================
# romconv_dkong.py — Donkey Kong: da dkong.zip ai 5 header grafici e di CPU.
#
# Trascrizione delle righe di `conv.sh` di galagino (13/36/37/53/69). Gli
# script veri stanno in galagino\ e non si toccano.
#
# ⚠️ NON copre gli 8 `dkong_sample_*.h` (i campioni digitalizzati): quelli non
# stanno in nessuna catena trovata sul disco. Restano SEGNAPOSTO: il gioco
# compila e si gioca, mancano quegli effetti sonori.
# ⛔ Da non confondere col `romconv_dkong.py` di CABINATONE, che e' un h2bin:
# LEGGE questi header per farne .bin, non li produce.
# ============================================================================
import os, re, subprocess, sys, zipfile

QUI       = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)
GALAGINO  = os.path.join(QUI, "galagino")
ROMS      = os.path.join(PACCHETTO, "ROMS")
USCITA    = os.path.join(PACCHETTO, "SPINNERINO", "machines", "dkong")

LAVORI = [
    ("cmapconv.py",   ["dkong_colormap", "c-2k.bpr", "c-2j.bpr", "0", "v-5e.bpr"],
                                                            "dkong_cmap.h"),
    ("romconv.py",    ["dkong_rom_cpu1", "c_5et_g.bin", "c_5ct_g.bin",
                       "c_5bt_g.bin", "c_5at_g.bin"],       "dkong_rom1.h"),
    ("romconv.py",    ["dkong_rom_cpu2", "s_3i_b.bin", "s_3j_b.bin"],
                                                            "dkong_rom2.h"),
    ("spriteconv.py", ["dkong_sprites", "dkong", "l_4m_b.bin", "l_4n_b.bin",
                       "l_4r_b.bin", "l_4s_b.bin"],         "dkong_spritemap.h"),
    ("tileconv.py",   ["v_5h_b.bin", "v_3pt.bin"],          "dkong_tilemap.h"),
]


def assoluto(arg, rom_dir):
    if arg.lstrip("-").isdigit() or arg in ("dkong", "dkong_colormap") \
            or arg.startswith("dkong_rom") or arg.startswith("dkong_sprites"):
        return arg
    return os.path.join(rom_dir, arg)



def correggi_nome(header):
    """tileconv.py ricava il nome dell'array dal nome del FILE DI INGRESSO,
    separando su "/": con un percorso Windows non separa niente e il nome
    diventa il percorso intero, che il compilatore rifiuta con
    «stray backslash in program». Il nome giusto e' quello dell'header."""
    simbolo = os.path.splitext(os.path.basename(header))[0]
    if simbolo[0].isdigit():
        simbolo = "_" + simbolo          # 1942_tilemap -> _1942_tilemap
    t = open(header, encoding="latin-1").read()
    # ⚠️ Il nome sbagliato e' un PERCORSO, e qui i percorsi hanno gli SPAZI
    # ("SPINNERINO PER AMICO"): cercare caratteri non-spazio non lo prende.
    nuovo = re.sub(r"^(const\s+unsigned\s+(?:long\s+long|short|long|char|int)\s+)"
                   r"(.*?)(\[)",
                   lambda m: m.group(1) + simbolo + m.group(3), t, count=1,
                   flags=re.M)
    if nuovo != t:
        with open(header, "w", encoding="latin-1", newline="\n") as f:
            f.write(nuovo)

def main():
    zip_dk = os.path.join(ROMS, "dkong.zip")
    if not os.path.isfile(zip_dk):
        print("ERRORE: manca ROMS\\dkong.zip")
        return 1

    rom_dir = os.path.join(ROMS, "_estratte", "dkong")
    os.makedirs(rom_dir, exist_ok=True)
    with zipfile.ZipFile(zip_dk) as z:
        z.extractall(rom_dir)
    os.makedirs(USCITA, exist_ok=True)

    for script, argomenti, header in LAVORI:
        percorso = os.path.join(GALAGINO, script)
        if not os.path.isfile(percorso):
            print("ERRORE: manca galagino\\%s" % script)
            return 1
        comando = ([sys.executable, percorso]
                   + [assoluto(a, rom_dir) for a in argomenti]
                   + [os.path.join(USCITA, header)])
        p = subprocess.run(comando, capture_output=True, text=True)
        if p.returncode != 0 or not os.path.isfile(os.path.join(USCITA, header)):
            print("ERRORE su %s -> %s" % (script, header))
            print((p.stdout or "") + (p.stderr or ""))
            return 1
        # ⚠️ SOLO per tileconv: gli altri ricevono il nome come ARGOMENTO e
        # ce l'hanno gia' giusto.
        if script == "tileconv.py":
            correggi_nome(os.path.join(USCITA, header))
        print("  %s" % header)
    print("dkong: 5 header scritti (gli 8 campioni restano segnaposto)")
    return 0


sys.exit(main())
