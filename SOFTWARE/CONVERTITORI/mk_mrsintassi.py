#!/usr/bin/env python3
# ============================================================================
# mk_mrsintassi.py — controllo di SINTASSI dei due blitter di MotoRace, sul PC.
#
# ⭐ Su questo progetto compila e flasha l'UTENTE ([[feedback_user_flashes]]):
# io non lancio arduino-cli. Ma consegnare del codice mai passato da un
# compilatore e' l'altro estremo. Questo script ESTRAE dal file vero le due
# funzioni di disegno e le compila da sole con `zig cc`, dando loro un guscio
# minimo (le tabelle, `memory`, `frame_buffer`, `sprite`).
#
# ⚠️ Estrae dal SORGENTE, non da una copia ([[feedback_verify_against_source_not_copy]]):
# se qualcuno rinomina le funzioni o cambia i marcatori, questo si ferma e lo
# dice, invece di continuare a compilare una versione vecchia
# ([[feedback_harness_orfani_segnalano_rollback]]).
#
# NON prova che il gioco funzioni: prova che il file COMPILA e che i due cicli
# sono coerenti. La prova di equivalenza pixel per pixel e' un'altra cosa (vedi
# la scheda di memoria: 39.936 casi, zero differenze).
#
# Serve: `pip install ziglang`.  USO: python mk_mrsintassi.py
# ============================================================================
import pathlib, re, subprocess, sys

HERE = pathlib.Path(__file__).parent
CPP = HERE.parent / "SPINNERINO" / "machines" / "motorace" / "motorace.cpp"
if not CPP.exists():
    sys.exit(f"ERRORE: non trovo {CPP}")
src = CPP.read_text(encoding="utf-8", errors="replace")


def estrai(firma):
    i = src.find(firma)
    if i < 0:
        sys.exit(f"ERRORE: non trovo '{firma}' in motorace.cpp. "
                 f"Rinominata? Aggiornare questo script.")
    # dalla firma alla graffa che la chiude, contando l'annidamento
    j = src.index("{", i)
    liv = 0
    for k in range(j, len(src)):
        if src[k] == "{":
            liv += 1
        elif src[k] == "}":
            liv -= 1
            if liv == 0:
                return src[i:k + 1]
    sys.exit(f"ERRORE: graffa non chiusa dopo '{firma}'")


def macro(nome):
    m = re.search(r"^#define\s+%s.*$" % nome, src, re.M)
    if not m:
        sys.exit(f"ERRORE: manca la macro {nome} in motorace.cpp")
    return m.group(0)


BLOCCHI = "\n".join([macro("MR_LY"), macro("MR_PX"),
                     macro("FB_W"), macro("ARCADE_Y_OFFSET")])
FONDO   = estrai("void motorace::blit_scroll_strip_t")
SPRITE  = estrai("void motorace::blit_sprite_t")
RIGA    = estrai("void motorace::render_row")

guscio = f"""/* generato da mk_mrsintassi.py - non modificare a mano */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define MR_MEM_VRAM 0x0000
{BLOCCHI}

/* PROGMEM e le pgm_read_* su ESP32 non fanno niente: qui nemmeno esistono */
#define pgm_read_dword_near(p) (*(p))
#define pgm_read_word_near(p)  (*(p))

static const unsigned long  motorace_tilemap[1024][8] = {{{{0}}}};
static const unsigned long  motorace_spritemap[4][256][32] = {{{{{{0}}}}}};
static const unsigned short motorace_char_cmap[16][8] = {{{{0}}}};
static const unsigned short motorace_spr_cmap[16][8] = {{{{0}}}};

struct sprite_S {{ unsigned short code; unsigned char color, flags; short x, y; }};

/* il guscio della classe: solo cio' che i tre metodi toccano davvero */
struct motorace {{
    unsigned char  *memory;
    unsigned short *frame_buffer;
    sprite_S       *sprite;
    unsigned char   active_sprites;
    unsigned char   scroll_x_low, scroll_x_high;
    void blit_scroll_strip_t(short strip_r);
    void blit_sprite_t(short strip_r, unsigned char s);
    void render_row(short row);
}};

{FONDO}

{SPRITE}

{RIGA}

int main(void)
{{
    static unsigned char  mem[0x2200];
    static unsigned short fb[8 * FB_W];
    static sprite_S       spr[128];
    motorace m;
    m.memory = mem; m.frame_buffer = fb; m.sprite = spr;
    m.active_sprites = 0; m.scroll_x_low = 0; m.scroll_x_high = 0;
    for (int s = 0; s < 28; s++) m.render_row(s);
    printf("OK: i tre blitter di motorace.cpp compilano e girano.\\n");
    printf("    FB_W = %d, MR_LY(0)=%d MR_LY(223)=%d, MR_PX(0)=%d MR_PX(255)=%d\\n",
           FB_W, MR_LY(0), MR_LY(223), MR_PX(0), MR_PX(255));
    if (MR_LY(223) != 255 || MR_PX(255) != 223) {{
        printf("    ERRORE: le due macro non coprono tutto l'intervallo!\\n");
        return 1;
    }}
    return 0;
}}
"""

out = HERE / "_mrsintassi.cpp"
out.write_text(guscio, encoding="utf-8")

ZIG = [sys.executable, "-m", "ziglang"]
exe = HERE / "_mrsintassi.exe"
r = subprocess.run(ZIG + ["c++", "-std=c++14", "-O2", "-Wall", "-Wextra",
                          "-Wno-unused-parameter", "-Wno-unused-variable",
                          "-o", str(exe), str(out)],
                   capture_output=True, text=True)
if r.returncode:
    print(r.stdout[-4000:])
    print(r.stderr[-4000:])
    sys.exit("COMPILAZIONE FALLITA")
if r.stderr.strip():
    print("avvisi del compilatore:")
    print(r.stderr[-2000:])
sys.exit(subprocess.run([str(exe)]).returncode)
