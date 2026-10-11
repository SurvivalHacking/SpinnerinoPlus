# ============================================================================
# romconv_galaga.py — da `ROMS/galaga.zip` agli header di GALAGA (Namco 1981).
#
# (Spinnerino Maker, 2026-10-11) PROVENIENZA.txt nomina un romconv_galaga.py
# che nel pacchetto non c'e': senza, gli otto header di Galaga non li faceva
# nessuno e il firmware non compilava ("galaga_rom1.h: No such file").
# Questo NON reinventa niente: scompatta la zip e chiama, uno per uno, gli
# attrezzi di galagino che stanno in `galagino/`, con gli stessi argomenti che
# ciascuno di loro stampa nel suo «Usage».
#
# Come romconv_galaga_starseed.py, gli attrezzi girano come processi a parte e
# con percorsi ASSOLUTI: niente dipende dalla cartella di lavoro.
# ============================================================================
import os, subprocess, sys, zipfile

QUI       = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)
ATTREZZI  = os.path.join(QUI, "galagino")
ZIP       = os.path.join(PACCHETTO, "ROMS", "galaga.zip")
ROM       = os.path.join(PACCHETTO, "ROMS", "_estratte", "galaga")
USCITA    = os.path.join(PACCHETTO, "SPINNERINO", "machines", "galaga")

# file della zip -> dimensione in byte (set MAME `galaga`)
ATTESI = {
    "gg1_1b.3p": 4096, "gg1_2b.3m": 4096, "gg1_3.2m": 4096, "gg1_4b.2l": 4096,
    "gg1_5b.3f": 4096, "gg1_7b.2c": 4096,
    "gg1_9.4l": 4096, "gg1_11.4d": 4096, "gg1_10.4f": 4096,
    "prom-5.5n": 32, "prom-4.2n": 256, "prom-3.1c": 256, "prom-1.1d": 256,
}

# attrezzo, argomenti (i nomi di ROM diventano percorsi), header che scrive
PASSI = [
    ("romconv.py",    ["-p", "galaga_rom_cpu1", "gg1_1b.3p", "gg1_2b.3m",
                       "gg1_3.2m", "gg1_4b.2l"],                 "galaga_rom1.h"),
    ("romconv.py",    ["galaga_rom_cpu2", "gg1_5b.3f"],          "galaga_rom2.h"),
    ("romconv.py",    ["galaga_rom_cpu3", "gg1_7b.2c"],          "galaga_rom3.h"),
    ("tileconv.py",   ["gg1_9.4l"],                              "galaga_tilemap.h"),
    ("spriteconv.py", ["galaga_sprites", "galaga", "gg1_11.4d",
                       "gg1_10.4f"],                             "galaga_spritemap.h"),
    ("cmapconv.py",   ["galaga_colormap_sprites", "prom-5.5n", "0",
                       "prom-3.1c"],                             "galaga_cmap_sprites.h"),
    ("cmapconv.py",   ["galaga_colormap_tiles", "prom-5.5n", "16",
                       "prom-4.2n"],                             "galaga_cmap_tiles.h"),
    ("audioconv.py",  ["galaga_wavetable", "prom-1.1d"],         "galaga_wavetable.h"),
]

if not os.path.isfile(ZIP):
    sys.exit("ERRORE: manca ROMS/galaga.zip")

with zipfile.ZipFile(ZIP) as z:
    dentro = {os.path.basename(n).lower(): n for n in z.namelist()}
    manca = [n for n in ATTESI if n not in dentro]
    if manca:
        sys.exit("ERRORE: in galaga.zip mancano: %s  (serve il set MAME `galaga`)"
                 % ", ".join(manca))
    os.makedirs(ROM, exist_ok=True)
    for nome, lungo in ATTESI.items():
        dati = z.read(dentro[nome])
        if len(dati) != lungo:
            sys.exit("ERRORE: %s e' %d byte, ne servono %d"
                     % (nome, len(dati), lungo))
        with open(os.path.join(ROM, nome), "wb") as f:
            f.write(dati)
print("galaga.zip: %d file, dimensioni verificate" % len(ATTESI))

os.makedirs(USCITA, exist_ok=True)
for attrezzo, argomenti, header in PASSI:
    script = os.path.join(ATTREZZI, attrezzo)
    if not os.path.isfile(script):
        sys.exit("ERRORE: manca galagino/%s" % attrezzo)
    fuori = os.path.join(USCITA, header)
    riga = [os.path.join(ROM, a) if a in ATTESI else a for a in argomenti]
    p = subprocess.run([sys.executable, script] + riga + [fuori],
                       capture_output=True, text=True)
    if p.returncode != 0 or not os.path.isfile(fuori) or not os.path.getsize(fuori):
        print("ERRORE nel generare %s" % header)
        print((p.stdout or "") + (p.stderr or ""))
        sys.exit(1)
    if attrezzo == "tileconv.py":
        # tileconv chiama la tabella col nome del file di ROM (`gg1_9_4l`, o
        # peggio l'intero percorso su Windows): il firmware vuole galaga_tilemap.
        with open(fuori, encoding="utf-8") as f:
            testo = f.read()
        capo = testo.index("[][8]")
        with open(fuori, "w", encoding="utf-8", newline="\n") as f:
            f.write("const unsigned short galaga_tilemap" + testo[capo:])
    print("  scritto %s" % header)
print("\nFatto.")
