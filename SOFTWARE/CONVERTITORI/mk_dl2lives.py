#!/usr/bin/env python3
# ============================================================================
# mk_dl2lives.py — VERIFICA che "FACILE = vite illimitate" funzioni davvero.
#
# Il cheat consiste nel ricaricare il contatore vite del giocatore in RAM
# (0x156AC) quando scende. Finche' non si vede il ROM MORIRE e continuare
# comunque, resta un'IPOTESI: qui la si prova col ROM vero.
#
# COME:
#   - prende il simulatore LASERDISC_PACKS\dl2_sim\dl2_vxt.c (che ha il solutore
#     delle mosse e l'opzione "sbaglia 1 mossa ogni N", cioe' sa far MORIRE il
#     giocatore) e ne fa una COPIA in _out_dl2lives: l'originale non si tocca;
#   - inietta nel ciclo principale lo STESSO ricarico del firmware;
#   - compila due binari, con e senza cheat, e li fa girare identici;
#   - confronta come si muove il contatore vite.
#
#   NORMALE  -> le vite calano fino a 0 e la partita finisce
#   FACILE   -> le vite non scendono mai sotto la soglia e si continua a giocare
#
# Il ROM viene ricavato da SPINNERINO\dl2_rom.h, cosi' e' BIT PER BIT lo stesso
# che gira sul device (e non un file trovato altrove che potrebbe divergere).
#
# Uso:  python mk_dl2lives.py [secondi]
# ============================================================================
import os
import re
import subprocess
import sys

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SK = os.path.join(ROOT, "SPINNERINO")
OUTDIR = os.path.join(HERE, "_out_dl2lives")

SIM = r"C:\Users\Tecnico\Desktop\LASERDISC_PACKS\dl2_sim\dl2_vxt.c"
SIMDIR = os.path.dirname(SIM)
NVRAM = os.path.join(SIMDIR, "dl2_nvram_10.bin")     # 10 vite, come il nostro EEPROM
GCC_CANDIDATES = [
    r"C:\Users\Tecnico\Desktop\LASERDISC_PACKS\tools\mingw64\bin\gcc.exe",
    "gcc",
]

CPU_HZ = 1180000
SKIP_EVERY = 2          # il solutore sbaglia una mossa su due: muore spesso
SECONDI = 260


def die(msg):
    print("ERRORE: " + msg)
    sys.exit(1)


def cerca_gcc():
    for g in GCC_CANDIDATES:
        try:
            subprocess.run([g, "--version"], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, check=True)
            return g
        except Exception:
            continue
    die("nessun gcc trovato")


def rom_da_header(dst):
    """Ricava il .bin del ROM da SPINNERINO/dl2_rom.h: stessi byte del device."""
    h = open(os.path.join(SK, "dl2_rom.h"), encoding="utf-8", errors="replace").read()
    corpo = h[h.index("{") + 1: h.rindex("}")]
    byte = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", corpo)]
    if len(byte) != 0x10000:
        die("dl2_rom.h non ha 65536 byte ma %d" % len(byte))
    open(dst, "wb").write(bytes(byte))
    return len(byte)


INIEZIONE = r"""
        /* ---- INIETTATO da mk_dl2lives.py: e' lo STESSO ricarico del firmware
           (dl2.cpp, ramo FACILE in dl2_rom_task). ARMATO: si comincia a
           ricaricare solo dopo aver visto il contatore gia' inizializzato, per
           non pestare il transitorio fra LEVEL e l'init del giocatore. ---- */
        {
            static int armed = 0, last = -1;
            unsigned lv = vxt_system_read_byte(s, 0x156AC);
            if (lv > 0) armed = 1;
#ifdef EASY_ON
            if (armed && lv < 10) { vxt_system_write_byte(s, 0x156AC, 10); lv = 10; }
#endif
            if ((int)lv != last) { printf("[VITE] %7.1fs  %u\n", now_s(), lv); last = (int)lv; }
        }
"""


def main():
    sec = int(sys.argv[1]) if len(sys.argv) > 1 else SECONDI
    if not os.path.isfile(SIM):
        die("non trovo il simulatore %s" % SIM)
    if not os.path.isfile(NVRAM):
        die("non trovo la NVRAM a 10 vite %s" % NVRAM)

    os.makedirs(OUTDIR, exist_ok=True)
    rom = os.path.join(OUTDIR, "dl2_rom_dal_header.bin")
    n = rom_da_header(rom)
    print("ROM ricavato da dl2_rom.h: %d byte" % n)

    src = open(SIM, encoding="utf-8", errors="replace").read()
    ancora = "        ldp.cycles += st.cycles;\n        rx_flush();\n"
    if src.count(ancora) != 1:
        die("nel simulatore non trovo (una sola volta) il punto di innesto nel "
            "ciclo principale: e' cambiato, va guardato invece di forzarlo")
    patched = src.replace(ancora, ancora + INIEZIONE, 1)

    cfile = os.path.join(OUTDIR, "dl2lives_gen.c")
    open(cfile, "w", encoding="utf-8").write(patched)
    print("simulatore copiato e iniettato (l'originale NON e' stato toccato)\n")

    gcc = cerca_gcc()
    wrappers = ["vxt_cpu.c", "vxt_pit.c", "vxt_pic.c", "vxt_memory.c", "vxt_ppi.c",
                "vxt_uart.c", "vxt_dma.c", "vxt_system.c", "vxt_utils.c", "vxt_dummy.c"]

    risultati = {}
    for nome, extra in (("NORMALE", []), ("FACILE", ["-DEASY_ON"])):
        exe = os.path.join(OUTDIR, "dl2lives_%s.exe" % nome)
        cmd = ([gcc, "-std=c11", "-O2", "-w"] + extra +
               ["-I", SK, "-o", exe, cfile] + [os.path.join(SK, w) for w in wrappers])
        r = subprocess.run(cmd, capture_output=True, text=True,
                           encoding="utf-8", errors="replace", cwd=SK)
        if r.returncode != 0:
            print((r.stdout or "")[-3000:])
            print((r.stderr or "")[-3000:])
            die("il simulatore iniettato non compila")

        print("=== %s ===" % nome)
        r = subprocess.run([exe, rom, NVRAM, str(sec), str(CPU_HZ), "1", "0", "0",
                            str(SKIP_EVERY)],
                           capture_output=True, text=True,
                           encoding="utf-8", errors="replace", cwd=SIMDIR)
        out = r.stdout or ""
        vite = [(float(m.group(1)), int(m.group(2)))
                for m in re.finditer(r"\[VITE\]\s+([\d.]+)s\s+(\d+)", out)]
        # si guarda solo da quando la partita e' cominciata (prima e' 0)
        gioco = [v for v in vite if v[0] > 0]
        minimo = min((v for _, v in gioco), default=-1)
        zeri = sum(1 for _, v in gioco if v == 0)
        cali = sum(1 for i in range(1, len(gioco)) if gioco[i][1] < gioco[i - 1][1])
        print("  transizioni del contatore : %d" % len(gioco))
        print("  cali (morti)              : %d" % cali)
        print("  valore MINIMO raggiunto   : %d" % minimo)
        print("  volte a ZERO (game over)  : %d" % zeri)
        if gioco:
            print("  andamento: %s" % " ".join(str(v) for _, v in gioco[:24]))
        risultati[nome] = (cali, minimo, zeri)
        print()

    print("=" * 62)
    nc, nmin, nzero = risultati["NORMALE"]
    fc, fmin, fzero = risultati["FACILE"]
    ok = True
    if nc == 0:
        print("### Il solutore NON e' mai morto in NORMALE: il test non prova nulla.")
        print("### Alzare i secondi o abbassare SKIP_EVERY.")
        ok = False
    else:
        print("NORMALE: %d morti, minimo %d, game over %d volte" % (nc, nmin, nzero))
        print("FACILE : %d cali,  minimo %d, game over %d volte" % (fc, fmin, fzero))
        if fzero > 0 or fmin < 10:
            print("\n### FALLITO: in FACILE le vite scendono comunque sotto la soglia.")
            ok = False
        else:
            print("\n### TUTTO OK: in FACILE il contatore non scende mai sotto 10")
            print("###           mentre in NORMALE arriva a %d." % nmin)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
