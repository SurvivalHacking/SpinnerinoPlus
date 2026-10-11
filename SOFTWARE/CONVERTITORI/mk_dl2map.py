#!/usr/bin/env python3
# ============================================================================
# mk_dl2map.py — verifica OFFLINE della mappa frame_laserdisc -> (clip, indice)
#                di Dragon's Lair II su SPINNERINO PLUS.
#
# ⭐ NON contiene una copia delle funzioni: le ESTRAE PER NOME da
#    SPINNERINO/dl2.cpp, genera un programma C host, lo compila con mingw e lo
#    fa girare sui FILE VERI della SD (dl2.meta + i 212 .idx).
#    Confrontarsi con una propria copia misura la coerenza fra due copie, non la
#    correttezza (vedi feedback_verify_against_source_not_copy).
#
# ⭐ E' anche un RILEVATORE DI ROLLBACK: se un giorno non trova piu' le funzioni
#    dentro dl2.cpp, non va "aggiustato" l'harness — vuol dire che il codice
#    validato e' stato sostituito (vedi feedback_harness_orfani_segnalano_rollback).
#
# Uso:  python mk_dl2map.py
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
import csv

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)                       # ...\BACKUP SPINNERINO PLUS
SKETCH = os.path.join(ROOT, "SPINNERINO", "dl2.cpp")
SDDIR = os.path.join(ROOT, "SD SPINNERINO", "dl2")
OUTDIR = os.path.join(HERE, "_out_dl2map")

GCC_CANDIDATES = [
    r"C:\Users\Tecnico\Desktop\LASERDISC_PACKS\tools\mingw64\bin\gcc.exe",
    "gcc",
]
MOVES_CSV = r"C:\Users\Tecnico\Desktop\LASERDISC_PACKS\dl2_sim\dl2_moves.csv"

LD_IN_MAX = 48 * 1024          # deve restare allineato a dl2.cpp


def die(msg):
    print("ERRORE: " + msg)
    sys.exit(1)


def estrai_funzione(src, firma):
    """Ritorna il testo della funzione che inizia con `firma`, graffe bilanciate."""
    i = src.find(firma)
    if i < 0:
        die("in dl2.cpp non c'e' piu' '%s'.\n"
            "       NON aggiustare questo script: capire quale versione del\n"
            "       codice stava validando e perche' non c'e' piu'." % firma)
    j = src.find("{", i)
    if j < 0:
        die("corpo mancante per '%s'" % firma)
    livello, k = 0, j
    while k < len(src):
        if src[k] == "{":
            livello += 1
        elif src[k] == "}":
            livello -= 1
            if livello == 0:
                return src[i:k + 1]
        k += 1
    die("graffe non bilanciate in '%s'" % firma)


def estrai_struct(src):
    m = re.search(r"typedef struct \{.*?\} dl2_clip_t;", src, re.S)
    if not m:
        die("in dl2.cpp non c'e' piu' la struct dl2_clip_t")
    return m.group(0)


def estrai_define(src, nome):
    m = re.search(r"^#define\s+%s\s+(\S+)" % re.escape(nome), src, re.M)
    if not m:
        die("in dl2.cpp non c'e' piu' #define %s" % nome)
    return m.group(1)


def cerca_gcc():
    for g in GCC_CANDIDATES:
        try:
            subprocess.run([g, "--version"], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, check=True)
            return g
        except Exception:
            continue
    die("nessun gcc trovato (provati: %s)" % ", ".join(GCC_CANDIDATES))


def main():
    if not os.path.isfile(SKETCH):
        die("non trovo %s" % SKETCH)
    if not os.path.isdir(SDDIR):
        die("non trovo il pack video %s" % SDDIR)

    src = open(SKETCH, encoding="utf-8", errors="replace").read()

    struct = estrai_struct(src)
    f_meta = estrai_funzione(src, "static bool dl2_load_meta(void)")
    f_find = estrai_funzione(src, "static int dl2_find_clip(int f)")
    f_loc = estrai_funzione(src, "static int dl2_local_index(const dl2_clip_t *c, int f)")
    f_idx = estrai_funzione(src, "static bool dl2_clip_idx(dl2_clip_t *c)")

    d_fps_n = estrai_define(src, "DISC_FPS_N")
    d_fps_d = estrai_define(src, "DISC_FPS_D")
    d_maxcl = estrai_define(src, "DL2_MAX_CLIPS")
    print("estratte da dl2.cpp: struct + 4 funzioni  (DISC %s/%s, MAX_CLIPS %s)"
          % (d_fps_n, d_fps_d, d_maxcl))

    # ⚠️ DL2_DIR deve restare CORTO. dl2_clip_idx/dl2_open_mjpg compongono il path
    # in un `char p[64]`: sul device "/sdcard/dl2/dl2-00001.mjpg" sono 26 caratteri
    # e il margine c'e' tutto, ma il path ASSOLUTO del PC (~110) verrebbe TRONCATO
    # da snprintf e tutte le fopen fallirebbero. Non e' un difetto del firmware, e'
    # del banco di prova: si esegue con cwd = cartella del pack e DL2_DIR = ".".
    sd_c = "."

    testa = """// GENERATO DA mk_dl2map.py -- non modificare a mano.
// Le funzioni qui sotto sono ESTRATTE ALLA LETTERA da SPINNERINO/dl2.cpp.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef int bool_t;
#define bool  int
#define true  1
#define false 0
#define nullptr NULL

#define DISC_FPS_N    %(fps_n)s
#define DISC_FPS_D    %(fps_d)s
#define DL2_MAX_CLIPS %(maxcl)s
#define DL2_DIR       "%(sd)s"
#define DL2_META      DL2_DIR "/dl2.meta"
#define LD_IN_MAX     (%(inmax)d)

#define MALLOC_CAP_SPIRAM 0
static void *heap_caps_malloc(size_t n, int cap) { (void)cap; return malloc(n); }
static void  heap_caps_free(void *p) { free(p); }

%(struct)s

static dl2_clip_t *g_clip = NULL;
static int         g_nclip = 0;
static int         g_frames_tot = 0;

%(f_meta)s

%(f_find)s

%(f_loc)s

%(f_idx)s
""" % dict(fps_n=d_fps_n, fps_d=d_fps_d, maxcl=d_maxcl, sd=sd_c,
           inmax=LD_IN_MAX, struct=struct, f_meta=f_meta, f_find=f_find,
           f_loc=f_loc, f_idx=f_idx)

    corpo = r"""
static int err = 0;
static void ko(const char *fmt, ...) { (void)fmt; }

int main(int argc, char **argv)
{
    if (!dl2_load_meta()) { printf("KO: dl2.meta non caricata\n"); return 2; }
    printf("clip caricati      : %d   (frame dichiarati %d)\n", g_nclip, g_frames_tot);

    /* --- 1. tutti gli .idx si aprono e concordano col .meta --- */
    int idx_ko = 0, nfr_corretti = 0;
    long tot_frames = 0, max_len = 0;
    for (int i = 0; i < g_nclip; i++) {
        int nfr_meta = g_clip[i].nfr;
        if (!dl2_clip_idx(&g_clip[i])) { idx_ko++; continue; }
        if (g_clip[i].nfr != nfr_meta) nfr_corretti++;
        tot_frames += g_clip[i].nfr;
        for (int k = 0; k < g_clip[i].nfr; k++) {
            uint32_t len = g_clip[i].pairs[2 * k + 1];
            if (!len || len > LD_IN_MAX) {
                printf("KO: %s#%d len=%u fuori da (0,%d]\n", g_clip[i].name, k, len, LD_IN_MAX);
                err++;
            }
            if ((long)len > max_len) max_len = len;
        }
    }
    printf("idx non apribili   : %d\n", idx_ko);
    printf("nfr corretti da idx: %d\n", nfr_corretti);
    printf("frame indicizzati  : %ld   frame piu' grande %ld byte\n", tot_frames, max_len);
    if (idx_ko) err++;

    /* --- 2. il primo frame di ogni clip deve dare indice 0, e find_clip deve
             tornare proprio quel clip --- */
    int primo_ko = 0, find_ko = 0;
    for (int i = 0; i < g_nclip; i++) {
        int f = g_clip[i].ld_start;
        if (dl2_find_clip(f) != i)                  find_ko++;
        if (dl2_local_index(&g_clip[i], f) != 0)    primo_ko++;
    }
    printf("primo frame != 0   : %d\n", primo_ko);
    printf("find_clip sbagliato: %d\n", find_ko);
    err += primo_ko + find_ko;

    /* --- 3. scansione di TUTTI i frame laserdisc coperti: indice sempre in
             range, mai decrescente, e saturazioni contate --- */
    long scanned = 0, sat = 0, fuori = 0, retro = 0, zero_len = 0;
    for (int i = 0; i < g_nclip; i++) {
        if (!g_clip[i].pairs) continue;
        int f0 = g_clip[i].ld_start;
        int f1 = (i + 1 < g_nclip) ? g_clip[i + 1].ld_start - 1
                                   : f0 + (int)((int64_t)g_clip[i].nfr * g_clip[i].fps_d * DISC_FPS_N
                                                / ((int64_t)g_clip[i].fps_n * DISC_FPS_D));
        int prev = -1;
        for (int f = f0; f <= f1; f++) {
            int ci = dl2_find_clip(f);
            if (ci != i) { fuori++; continue; }
            int ix = dl2_local_index(&g_clip[i], f);
            scanned++;
            if (ix < 0 || ix >= g_clip[i].nfr) { fuori++; continue; }
            if (ix < prev) retro++;
            if (ix == prev && ix == g_clip[i].nfr - 1) sat++;
            if (!g_clip[i].pairs[2 * ix + 1]) zero_len++;
            prev = ix;
        }
    }
    printf("frame LD scanditi  : %ld\n", scanned);
    printf("indici fuori range : %ld\n", fuori);
    printf("indici che tornano indietro: %ld\n", retro);
    printf("saturazioni a fine clip    : %ld\n", sat);
    printf("frame con len 0    : %ld\n", zero_len);
    err += (int)(fuori + retro + zero_len);

    /* --- 4. i frame REALMENTE cercati dal ROM (dl2_moves.csv del simulatore) --- */
    if (argc > 1) {
        FILE *m = fopen(argv[1], "r");
        if (!m) { printf("(dl2_moves.csv non leggibile: salto)\n"); }
        else {
            char line[128]; int n = 0, bad = 0; int first = 1;
            while (fgets(line, sizeof line, m)) {
                if (first) { first = 0; if (line[0] < '0' || line[0] > '9') continue; }
                int lev = 0, fr = 0;
                if (sscanf(line, "%d,%d,", &lev, &fr) != 2) continue;
                n++;
                int ci = dl2_find_clip(fr);
                if (ci < 0) { bad++; continue; }
                if (!dl2_clip_idx(&g_clip[ci])) { bad++; continue; }
                int ix = dl2_local_index(&g_clip[ci], fr);
                if (ix < 0 || ix >= g_clip[ci].nfr) { bad++; continue; }
                if (!g_clip[ci].pairs[2 * ix + 1]) bad++;
            }
            fclose(m);
            printf("frame veri del ROM : %d   non mappabili %d\n", n, bad);
            err += bad;
        }
    }

    printf("\n%s\n", err ? "### FALLITO" : "### TUTTO OK");
    return err ? 1 : 0;
}
"""

    os.makedirs(OUTDIR, exist_ok=True)
    cfile = os.path.join(OUTDIR, "dl2map_gen.c")
    exe = os.path.join(OUTDIR, "dl2map_gen.exe")
    with open(cfile, "w", encoding="utf-8") as fh:
        fh.write(testa)
        fh.write(corpo)

    gcc = cerca_gcc()
    r = subprocess.run([gcc, "-std=c11", "-O1", "-o", exe, cfile],
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    if r.returncode != 0:
        print(r.stdout)
        print(r.stderr)
        die("il C generato non compila (vedi %s)" % cfile)

    args = [exe]
    if os.path.isfile(MOVES_CSV):
        args.append(MOVES_CSV)
    r = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=SDDIR)
    print(r.stdout)
    if r.stderr:
        print(r.stderr)
    sys.exit(r.returncode)


if __name__ == "__main__":
    main()
