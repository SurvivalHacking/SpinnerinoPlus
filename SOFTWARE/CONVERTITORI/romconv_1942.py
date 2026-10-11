# ============================================================================
# romconv_1942.py — 1942 (Capcom): da 1942.zip agli 11 header del firmware.
#
# NON reinventa niente: e' la TRASCRIZIONE delle undici righe di `conv.sh` di
# galagino, che sono la catena da cui quegli header sono nati. Gli script veri
# (romconv/tileconv/spriteconv/cmapconv) stanno in galagino\ e non si toccano.
#
# Serviva perche' 1942 era l'unico gioco senza convertitore: l'unico candidato
# trovato su tutto il disco era `mk_1942test.py`, che e' un BANCO DI PROVA e
# quegli header li LEGGE, non li produce.
# ============================================================================
import os, re, shutil, subprocess, sys, tempfile, zipfile

QUI       = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)
GALAGINO  = os.path.join(QUI, "galagino")
ROMS      = os.path.join(PACCHETTO, "ROMS")
USCITA    = os.path.join(PACCHETTO, "SPINNERINO", "machines", "1942")

# Le undici righe di conv.sh, alla lettera. La prima voce e' lo script.
LAVORI = [
    ("cmapconv.py",   ["_1942_colormap_chars",   "sb-5.e8,sb-6.e9,sb-7.e10",
                       "128", "sb-0.f1"],                    "1942_character_cmap.h"),
    ("cmapconv.py",   ["_1942_colormap_tiles",   "sb-5.e8,sb-6.e9,sb-7.e10",
                       "-1", "sb-4.d6,sb-3.d2,sb-2.d1"],     "1942_tile_cmap.h"),
    ("cmapconv.py",   ["_1942_colormap_sprites", "sb-5.e8,sb-6.e9,sb-7.e10",
                       "64", "sb-8.k3"],                     "1942_sprite_cmap.h"),
    ("romconv.py",    ["_1942_rom_cpu1", "srb-03.m3", "srb-04.m4"], "1942_rom1.h"),
    ("romconv.py",    ["_1942_rom_cpu1_b0", "srb-05.m5"],    "1942_rom1_b0.h"),
    ("romconv.py",    ["_1942_rom_cpu1_b1", "srb-06.m6"],    "1942_rom1_b1.h"),
    ("romconv.py",    ["_1942_rom_cpu1_b2", "srb-07.m7"],    "1942_rom1_b2.h"),
    ("romconv.py",    ["_1942_rom_cpu2", "sr-01.c11"],       "1942_rom2.h"),
    ("spriteconv.py", ["_1942_sprites", "1942",
                       "sr-14.l1", "sr-15.l2", "sr-16.n1", "sr-17.n2"],
                                                             "1942_spritemap.h"),
    ("tileconv.py",   ["sr-02.f2"],                          "1942_charmap.h"),
    ("tileconv.py",   ["sr-08.a1", "sr-09.a2", "sr-10.a3",
                       "sr-11.a4", "sr-12.a5", "sr-13.a6"],  "1942_tilemap.h"),
]


def assoluto(arg, rom_dir):
    """Un argomento puo' essere un file di ROM, una lista separata da virgole,
    o un numero: solo i primi due diventano percorsi."""
    if arg.lstrip("-").isdigit():
        return arg
    if "," in arg:
        return ",".join(os.path.join(rom_dir, p) for p in arg.split(","))
    if arg.startswith("_") or arg == "1942":
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
    zip_1942 = os.path.join(ROMS, "1942.zip")
    if not os.path.isfile(zip_1942):
        print("ERRORE: manca ROMS\\1942.zip")
        return 1

    rom_dir = tempfile.mkdtemp(prefix="rom1942_")
    try:
        with zipfile.ZipFile(zip_1942) as z:
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
            # ⚠️ SOLO per tileconv: romconv/cmapconv/spriteconv ricevono il
            # nome come ARGOMENTO e ce l'hanno gia' giusto. Applicandola a
            # tutti si rinominava `_1942_rom_cpu1` in `_1942_rom1` e il
            # firmware non trovava piu' i simboli.
            if script == "tileconv.py":
                correggi_nome(os.path.join(USCITA, header))
            print("  %s" % header)
        print("1942: 11 header scritti in machines\\1942")
        return 0
    finally:
        shutil.rmtree(rom_dir, ignore_errors=True)


sys.exit(main())
