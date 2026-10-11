#!/usr/bin/env python3
# ============================================================================
# mk_dl2hud.py — verifica OFFLINE dell'HUD di Dragon's Lair II su SPINNERINO.
#
# Risponde a UNA domanda: sopprimendo dal filmato le righe che finiscono
# nell'HUD, e sostituendo i prompt delle mosse col simbolo, si sta cancellando
# per sbaglio qualche riga BUONA (titolo, tabella record, inserimento iniziali)?
#
# ⭐ Come mk_dl2map.py: NON contiene una copia delle funzioni, le ESTRAE PER NOME
#    da SPINNERINO/dl2.cpp. E' anche un rilevatore di rollback
#    ([[feedback_harness_orfani_segnalano_rollback]]).
#
# Dati: LASERDISC_PACKS\dl2_sim\dl2_hud_strings.txt = le 123 stringhe VERE
#       mandate all'overlay LDP-1450 nelle partite simulate col ROM vero.
#
# Uso:  python mk_dl2hud.py
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
SKETCH = os.path.join(ROOT, "SPINNERINO", "dl2.cpp")
OUTDIR = os.path.join(HERE, "_out_dl2hud")

STRINGS = r"C:\Users\Tecnico\Desktop\LASERDISC_PACKS\dl2_sim\dl2_hud_strings.txt"
GCC_CANDIDATES = [
    r"C:\Users\Tecnico\Desktop\LASERDISC_PACKS\tools\mingw64\bin\gcc.exe",
    "gcc",
]


def die(msg):
    print("ERRORE: " + msg)
    sys.exit(1)


def estrai_funzione(src, firma):
    i = src.find(firma)
    if i < 0:
        die("in dl2.cpp non c'e' piu' '%s'.\n"
            "       NON aggiustare questo script: capire quale versione del\n"
            "       codice stava validando e perche' non c'e' piu'." % firma)
    j = src.find("{", i)
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
    if not os.path.isfile(SKETCH):
        die("non trovo %s" % SKETCH)
    if not os.path.isfile(STRINGS):
        die("non trovo %s" % STRINGS)

    src = open(SKETCH, encoding="utf-8", errors="replace").read()
    f_class = estrai_funzione(src, "static int dl2_text_class(const char *s, int *val)")
    f_move = estrai_funzione(src, "static int dl2_move_of(const char *s)")
    print("estratte da dl2.cpp: dl2_text_class + dl2_move_of")

    testa = """// GENERATO DA mk_dl2hud.py -- non modificare a mano.
// Le due funzioni sono ESTRATTE ALLA LETTERA da SPINNERINO/dl2.cpp.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TL_ALTRO = 0, TL_LEVEL, TL_LIVES, TL_CREDIT, TL_MARQUEE };
enum { MV_NONE = 0, MV_UP, MV_DOWN, MV_LEFT, MV_RIGHT, MV_SWORD };

%(f_class)s

%(f_move)s
""" % dict(f_class=f_class, f_move=f_move)

    corpo = r"""
static const char *nome_tl(int t) {
    switch (t) { case TL_LEVEL: return "LEVEL"; case TL_LIVES: return "LIVES";
                 case TL_CREDIT: return "CREDIT"; case TL_MARQUEE: return "MARQUEE"; }
    return "ALTRO";
}
static const char *nome_mv(int m) {
    switch (m) { case MV_UP: return "UP"; case MV_DOWN: return "DOWN";
                 case MV_LEFT: return "LEFT"; case MV_RIGHT: return "RIGHT";
                 case MV_SWORD: return "SWORD"; }
    return "-";
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "dl2_hud_strings.txt";
    FILE *f = fopen(path, "r");
    if (!f) { printf("KO: non apro %s\n", path); return 2; }

    char line[256];
    int n = 0, n_marquee = 0, n_level = 0, n_lives = 0, n_credit = 0, n_altro = 0;
    int n_mosse = 0, n_vuote = 0, err = 0;
    /* le righe che DEVONO sopravvivere: titolo, tabella record, iniziali */
    const char *buone[] = { "BLUTH", "CORP", "SULLIVAN", "AAA", "DRAGON", "LAIR", NULL };

    printf("--- righe che verrebbero CANCELLATE dal filmato ---\n");
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        /* togli BOM e fine riga, ma NON gli spazi: fanno parte del dato */
        if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB) p += 3;
        size_t L = strlen(p);
        while (L && (p[L-1] == '\n' || p[L-1] == '\r')) p[--L] = 0;
        n++;

        int vuota = 1;
        for (size_t i = 0; i < L; i++) if (p[i] != ' ') { vuota = 0; break; }
        if (vuota) { n_vuote++; continue; }

        int mv = dl2_move_of(p);
        int tl = dl2_text_class(p, NULL);
        if (mv != MV_NONE) { n_mosse++; continue; }

        switch (tl) {
            case TL_MARQUEE: n_marquee++; break;
            case TL_LEVEL:   n_level++;   break;
            case TL_LIVES:   n_lives++;   break;
            case TL_CREDIT:  n_credit++;  break;
            default:         n_altro++;   break;
        }
        if (tl != TL_ALTRO) {
            /* soppressa: era una riga buona? */
            for (int b = 0; buone[b]; b++)
                if (strstr(p, buone[b])) {
                    printf("  ### CANCELLATA UNA RIGA BUONA: '%s'  (%s)\n", p, nome_tl(tl));
                    err++;
                }
        }
    }
    fclose(f);

    printf("\nrighe totali        : %d  (di cui vuote/pulizia %d)\n", n, n_vuote);
    printf("MOSSE -> simbolo    : %d\n", n_mosse);
    printf("soppresse dall'HUD  : %d   (marquee %d, level %d, lives %d, credit %d)\n",
           n_marquee + n_level + n_lives + n_credit, n_marquee, n_level, n_lives, n_credit);
    printf("lasciate al ROM     : %d\n", n_altro);
    printf("righe buone cancellate: %d\n", err);

    /* controprova mirata: le 5 parole delle mosse devono essere riconosciute
       anche riempite di spazi, com'e' nel dato vero */
    const char *mosse[] = { "UP", "DOWN", "LEFT", "RIGHT", "SWORD", NULL };
    for (int i = 0; mosse[i]; i++) {
        char buf[32];
        snprintf(buf, sizeof buf, "  %s      ", mosse[i]);
        if (dl2_move_of(buf) == MV_NONE) { printf("### '%s' NON riconosciuta\n", buf); err++; }
    }
    /* e NON devono scattare su righe che le contengono come sottostringa */
    const char *falsi[] = { "8    LIVES", "CREDIT   1", "LEVEL    2", " BLUTH CO.", NULL };
    for (int i = 0; falsi[i]; i++)
        if (dl2_move_of(falsi[i]) != MV_NONE) {
            printf("### falso positivo mossa su '%s' -> %s\n", falsi[i], nome_mv(dl2_move_of(falsi[i])));
            err++;
        }

    printf("\n%s\n", err ? "### FALLITO" : "### TUTTO OK");
    return err ? 1 : 0;
}
"""

    os.makedirs(OUTDIR, exist_ok=True)
    cfile = os.path.join(OUTDIR, "dl2hud_gen.c")
    exe = os.path.join(OUTDIR, "dl2hud_gen.exe")
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

    r = subprocess.run([exe, STRINGS], capture_output=True, text=True, encoding="utf-8", errors="replace")
    print(r.stdout)
    if r.stderr:
        print(r.stderr)
    sys.exit(r.returncode)


if __name__ == "__main__":
    main()
