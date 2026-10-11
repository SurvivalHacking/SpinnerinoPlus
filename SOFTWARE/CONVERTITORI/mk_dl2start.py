#!/usr/bin/env python3
# ============================================================================
# mk_dl2start.py — MISURA quanto va tenuto START perche' DL2 parta, sul ROM VERO.
#
# IL PROBLEMA (trovato leggendo i sorgenti, non indovinato):
#   Su SPINNERINO una pressione del tasto START genera una SEQUENZA
#   (input.cpp:521): COIN per 80 ms, gap, poi START per 240 ms. Lo START arriva
#   quindi 160 ms dopo la moneta. Ma il ROM di DL2 impiega ~3,2 s a registrare
#   il credito a 1,18 MHz (ritardo cycle-dependent misurato su CABINATONE).
#   Il valore ereditato da CABINATONE (1200 ms) era stato validato con la moneta
#   a t=77 s e lo START a t=82 s — CINQUE SECONDI di margine. Qui non c'e'.
#
# COSA FA:
#   Estrae PER NOME da SPINNERINO/dl2.cpp tutto il blocco di emulazione hardware
#   (allocatore, patch del ROM, LDP-1450, EEPROM 9536, porte 0x201/0x202,
#   dl2_read_inputs e dl2_rom_setup), lo compila con libvxt e il ROM VERO, e
#   riproduce ESATTAMENTE la sequenza del firmware SPINNERINO facendo variare
#   DL2_START_HOLD_MS con -D. Per ogni valore dice se e quando parte la partita.
#
# ⭐ Non e' una copia: e' il codice del firmware. Ed e' anche un rilevatore di
#    rollback ([[feedback_harness_orfani_segnalano_rollback]]).
#
# Uso:  python mk_dl2start.py [valori,separati,da,virgole]
# ============================================================================
import os
import re
import subprocess
import sys

# Windows: la console e' cp1252 e gcc emette UTF-8 (i commenti estratti hanno
# caratteri accentati). Senza questo, un errore di compilazione fa CRASHARE lo
# script invece di mostrarlo — insidia gia' nota su questo progetto.
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SK = os.path.join(ROOT, "SPINNERINO")
SKETCH = os.path.join(SK, "dl2.cpp")
OUTDIR = os.path.join(HERE, "_out_dl2start")

GCC_CANDIDATES = [
    r"C:\Users\Tecnico\Desktop\LASERDISC_PACKS\tools\mingw64\bin\gcc.exe",
    "gcc",
]

VALORI_DEFAULT = [1200, 2000, 3000, 4000, 4500, 6000]
T_PRESS_MS = 80000        # pressione di START a 80 s emulati (attract in corso)
T_END_MS = 130000         # si osserva fino a 130 s


def die(msg):
    print("ERRORE: " + msg)
    sys.exit(1)


def fine_funzione(src, i):
    j = src.find("{", i)
    liv, k = 0, j
    while k < len(src):
        if src[k] == "{":
            liv += 1
        elif src[k] == "}":
            liv -= 1
            if liv == 0:
                return k + 1
        k += 1
    die("graffe non bilanciate")


def estrai_blocco(src, firma_da, firma_a):
    i = src.find(firma_da)
    if i < 0:
        die("in dl2.cpp non c'e' piu' '%s'.\n"
            "       NON aggiustare questo script: capire quale versione del\n"
            "       codice stava validando e perche' non c'e' piu'." % firma_da)
    j = src.find(firma_a, i)
    if j < 0:
        die("in dl2.cpp non c'e' piu' '%s'" % firma_a)
    return src[i:fine_funzione(src, j)]


def estrai_funzione(src, firma):
    i = src.find(firma)
    if i < 0:
        die("in dl2.cpp non c'e' piu' '%s'" % firma)
    return src[i:fine_funzione(src, i)]


def estrai_define(src, nome, default=None):
    m = re.search(r"^#define\s+%s\s+(\S+)" % re.escape(nome), src, re.M)
    if not m:
        if default is not None:
            return default
        die("manca #define %s" % nome)
    return m.group(1)


def cerca_gcc():
    for g in GCC_CANDIDATES:
        try:
            subprocess.run([g, "--version"], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, check=True)
            return g
        except Exception:
            continue
    die("nessun gcc trovato")


def main():
    valori = VALORI_DEFAULT
    if len(sys.argv) > 1:
        valori = [int(x) for x in sys.argv[1].split(",")]

    src = open(SKETCH, encoding="utf-8", errors="replace").read()

    # UN SOLO blocco contiguo, dall'allocatore alla fine della lettura input:
    # cosi' non si sceglie cosa includere e non si puo' "dimenticare" un pezzo.
    slab = estrai_blocco(src,
                         "static void *dl2_alloc(void *ptr, size_t sz)",
                         "static void dl2_read_inputs(uint8_t b)")
    setup = estrai_funzione(src, "static const char *dl2_rom_setup(void)")
    cpu_hz = estrai_define(src, "DL2_CPU_HZ")
    fps_n = estrai_define(src, "DISC_FPS_N")
    fps_d = estrai_define(src, "DISC_FPS_D")
    sc_addr = estrai_define(src, "DL2_SCORE_ADDR")
    sc_div = estrai_define(src, "DL2_SCORE_DIV")
    lv_addr = estrai_define(src, "DL2_LIVES_ADDR")
    print("estratti da dl2.cpp: blocco hardware (%d righe) + dl2_rom_setup"
          % slab.count("\n"))
    print("DL2_CPU_HZ = %s\n" % cpu_hz)

    testa = """// GENERATO DA mk_dl2start.py -- non modificare a mano.
// Il blocco hardware e dl2_rom_setup sono ESTRATTI ALLA LETTERA da dl2.cpp.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#define nullptr NULL

#if defined(__cplusplus) && !defined(_Static_assert)
  #define _Static_assert static_assert
#endif
#include <vxt/vxt.h>
#include <vxt/vxtu.h>

/* ---- ambiente SPINNERINO simulato ---- */
#define BUTTON_LEFT  0x01
#define BUTTON_RIGHT 0x02
#define BUTTON_UP    0x04
#define BUTTON_DOWN  0x08
#define BUTTON_FIRE  0x10
#define BUTTON_START 0x20
#define BUTTON_COIN  0x40

#define DL2_CPU_HZ       %(cpu)s
#define DISC_FPS_N       %(fn)s
#define DISC_FPS_D       %(fd)s
#define DL2_ROM_BASE     0xF0000
#define DL2_SCORE_ADDR   %(sca)s
#define DL2_SCORE_DIV    %(scd)s
#define DL2_LIVES_ADDR   %(lva)s
#define DL2_AUDIO        0
#ifndef DL2_START_HOLD_MS
  #define DL2_START_HOLD_MS 1200
#endif

#define MALLOC_CAP_SPIRAM   0
#define MALLOC_CAP_INTERNAL 0
static void *heap_caps_realloc(void *p, size_t n, int c) { (void)c; return realloc(p, n); }
static void *heap_caps_malloc(size_t n, int c) { (void)c; return malloc(n); }
static void  heap_caps_free(void *p) { free(p); }

static uint32_t g_millis_v = 0;
static uint32_t millis(void) { return g_millis_v; }
static int  input_is_vol_mode_active(void) { return 0; }

#include "dl2_rom.h"
#include "dl2_nvram.h"

/* ATTENZIONE: NIENTE dichiarazioni qui delle variabili di stato, degli enum TL
   o di dl2_text_class: sono GIA' dentro il blocco estratto, che parte da
   dl2_alloc e arriva fino a dl2_read_inputs. Dichiararle di nuovo =
   redefinition, ed e' l'errore che questo banco ha fatto al primo giro.
   NB: mai una sequenza asterisco-barra dentro un commento generato. */
%(slab)s

static vxt_system *dl2_sys = NULL;

%(setup)s
""" % dict(cpu=cpu_hz, fn=fps_n, fd=fps_d, sca=sc_addr, scd=sc_div, lva=lv_addr,
           slab=slab, setup=setup)

    corpo = r"""
/* ---- sequenza del firmware SPINNERINO (input.cpp:521), riprodotta alla lettera:
       0..80 ms COIN · 80..160 gap · 160..400 START ---- */
static uint8_t bottoni(uint32_t t_ms, uint32_t t_press)
{
    if (t_ms < t_press) return 0;
    uint32_t dt = t_ms - t_press;
    if (dt <  80) return BUTTON_COIN;
    if (dt < 160) return 0;
    if (dt < 400) return BUTTON_START;
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t t_press = (argc > 1) ? (uint32_t)atoi(argv[1]) : 80000;
    uint32_t t_end   = (argc > 2) ? (uint32_t)atoi(argv[2]) : 130000;

    const char *err = dl2_rom_setup();
    if (err) { printf("KO setup: %s\n", err); return 2; }

    int64_t cycles = 0, next_frame = DL2_CPU_HZ * (int64_t)DISC_FPS_D / DISC_FPS_N;
    const int64_t frame_cycles = next_frame;
    double t_credit = -1, t_level = -1, t_search = -1;
    int    credit_visto = 0;

    for (;;) {
        struct vxt_step st = vxt_system_step(dl2_sys, 2000);
        if (st.err != VXT_NO_ERROR) { printf("KO: errore CPU\n"); return 3; }
        cycles += st.cycles;
        rx_flush();

        if (cycles >= next_frame) {
            next_frame += frame_cycles;
            if (ldp.state == LDP_PLAY) {
                ldp.frame++;
                if (ldp.repeat_to && ldp.frame >= ldp.repeat_to) {
                    ldp.state = LDP_PAUSE; ldp.repeat_to = 0;
                }
            }
        }
        if (ldp.seek_target) g_ld_frame = ldp.frame;

        /* il tempo che vede il firmware e' quello EMULATO: sul device il pacing
           tiene emulazione e tempo reale agganciati, quindi e' la stessa cosa */
        g_millis_v = (uint32_t)(cycles * 1000 / DL2_CPU_HZ);
        double t = g_millis_v / 1000.0;

        dl2_read_inputs(bottoni(g_millis_v, t_press));

        if (t_search < 0 && ldp.seek_target)          t_search = t;
        if (!credit_visto && g_hud_credit > 0)      { credit_visto = 1; t_credit = t; }
        if (t_level < 0 && g_hud_level > 0)         { t_level = t; break; }
        if (g_millis_v > t_end) break;
    }

    printf("  primo SEARCH   : %s\n", t_search < 0 ? "MAI" : "");
    if (t_search >= 0) printf("  primo SEARCH   : %.1f s\n", t_search);
    printf("  CREDIT visto   : %s", credit_visto ? "" : "MAI\n");
    if (credit_visto) printf("%.1f s  (%.2f s dopo la pressione)\n",
                             t_credit, t_credit - t_press / 1000.0);
    if (t_level >= 0) {
        printf("  >>> PARTITA AVVIATA a %.1f s  (LEVEL %d)\n", t_level, g_hud_level);
        return 0;
    }
    printf("  >>> PARTITA NON AVVIATA entro %.0f s\n", t_end / 1000.0);
    return 1;
}
"""

    os.makedirs(OUTDIR, exist_ok=True)
    cfile = os.path.join(OUTDIR, "dl2start_gen.c")
    with open(cfile, "w", encoding="utf-8") as fh:
        fh.write(testa)
        fh.write(corpo)

    gcc = cerca_gcc()
    wrappers = ["vxt_cpu.c", "vxt_pit.c", "vxt_pic.c", "vxt_memory.c", "vxt_ppi.c",
                "vxt_uart.c", "vxt_dma.c", "vxt_system.c", "vxt_utils.c", "vxt_dummy.c"]

    print("sequenza del firmware: COIN 0-80 ms, gap, START 160-400 ms")
    print("pressione di START a %.0f s emulati, osservazione fino a %.0f s\n"
          % (T_PRESS_MS / 1000, T_END_MS / 1000))

    esiti = []
    for v in valori:
        exe = os.path.join(OUTDIR, "dl2start_%d.exe" % v)
        cmd = [gcc, "-std=c11", "-O2", "-w", "-DDL2_START_HOLD_MS=%d" % v,
               "-I", SK, "-o", exe, cfile] + [os.path.join(SK, w) for w in wrappers]
        r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=SK)
        if r.returncode != 0:
            print(r.stdout[-3000:])
            print(r.stderr[-3000:])
            die("il C generato non compila (vedi %s)" % cfile)
        print("--- DL2_START_HOLD_MS = %d" % v)
        r = subprocess.run([exe, str(T_PRESS_MS), str(T_END_MS)],
                           capture_output=True, text=True, encoding="utf-8", errors="replace")
        print(r.stdout.rstrip())
        esiti.append((v, r.returncode == 0))
        print()

    ok = [v for v, o in esiti if o]
    print("=" * 60)
    if ok:
        print("MINIMO CHE FA PARTIRE LA PARTITA: %d ms" % min(ok))
        print("valori che funzionano: %s" % ", ".join(str(v) for v in ok))
    else:
        print("### NESSUN valore provato fa partire la partita: la causa NON e'")
        print("### (solo) la durata di START. Continuare l'analisi, non flashare.")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
