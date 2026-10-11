#!/usr/bin/env python3
# copia dl2_video -> D:\dl2 con ripresa e verifica.
# NB: da lanciare SENZA pipe, altrimenti il codice di uscita e' quello di tail
# e un crash a meta' copia sembra un successo (gia' successo una volta).
import os, shutil, sys, time

SRC = r"C:\Users\Tecnico\Desktop\LASERDISC_PACKS\dl2_video"
DST = r"D:\dl2"
EXTRA = [(r"C:\Users\Tecnico\Desktop\LASERDISC_PACKS\dl2_sim\dl2_nvram.bin", "dl2_nvram.bin")]

def main():
    if not os.path.isdir(DST):
        print("ERRORE: %s non raggiungibile (SD non inserita?)" % DST); return 2

    files = sorted(os.listdir(SRC))
    todo = []
    for f in files:
        s, d = os.path.join(SRC, f), os.path.join(DST, f)
        if os.path.exists(d) and os.path.getsize(d) == os.path.getsize(s):
            continue                       # gia' copiato integro: salta
        todo.append(f)
    print("file totali %d, da copiare %d (%.0f MB)" % (
        len(files), len(todo),
        sum(os.path.getsize(os.path.join(SRC, f)) for f in todo) / 1e6))
    sys.stdout.flush()

    t0 = time.time()
    for i, f in enumerate(todo):
        s, d = os.path.join(SRC, f), os.path.join(DST, f)
        try:
            shutil.copy2(s, d)
        except OSError as e:
            print("INTERROTTA su %s dopo %d file: %s" % (f, i, e)); return 1
        if i % 50 == 0:
            print("  %d/%d  (%.0f s)" % (i + 1, len(todo), time.time() - t0))
            sys.stdout.flush()

    for s, name in EXTRA:
        try:
            shutil.copy2(s, os.path.join(DST, name))
            print("copiato", name)
        except OSError as e:
            print("ERRORE su %s: %s" % (name, e)); return 1

    print("COPIA COMPLETATA in %.0f s" % (time.time() - t0))
    return 0

sys.exit(main())
