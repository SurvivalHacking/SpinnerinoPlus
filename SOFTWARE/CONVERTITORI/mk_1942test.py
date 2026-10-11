#!/usr/bin/env python3
# ============================================================================
# mk_1942test.py — fa girare 1942 **sul PC**, col codice VERO del firmware.
#
# ⭐⭐ NON C'E' UNA COPIA DEL MOTORE: i blocchi sono ESTRATTI da c1942.cpp con un
# taglio testuale ([[feedback_verify_against_source_not_copy]]). Se qualcuno
# cambia il firmware e non rilancia questo script, l'estrazione fallisce e lo
# dice ([[feedback_harness_orfani_segnalano_rollback]]).
#
# Serve a verificare le DUE cose che sono cambiate rispetto al porting
# SPINNERINO da cui viene la trascrizione:
#   1. il VIDEO — qui si disegna dritti in `fb` invece di trasporre;
#   2. l'AUDIO — AY-3-8910 vero al posto del render semplificato.
#
# Serve: `pip install ziglang` e PIL. USO: python mk_1942test.py [quadri]
# ============================================================================
import pathlib, subprocess, sys

HERE = pathlib.Path(__file__).parent
ROOT = HERE.parent.parent
cpp = (ROOT / "c1942.cpp").read_text(encoding="utf-8", errors="replace")


def taglia(dal, al, nome):
    i, j = cpp.find(dal), cpp.find(al)
    if i < 0 or j < 0 or j <= i:
        sys.exit(f"ERRORE: non trovo il blocco '{nome}' in c1942.cpp.\n"
                 f"        iniziale {'OK' if i>=0 else 'MANCANTE'}, "
                 f"finale {'OK' if j>=0 else 'MANCANTE'}. Aggiornare i marcatori.")
    return cpp[i:j]


SEZ_BUS   = "// ============================================================ bus Z80"
SEZ_DIS   = "// ============================================================ disegno"
SEZ_AUD   = "// ============================================================ audio: 2x AY-3-8910"
SEZ_SCALA = "// ============================================================ scala + blit"
SEZ_CPU   = "// ============================================================ esecuzione CPU"
SEZ_TASK  = "// ============================================================ task"

COST  = taglia("#define C19_W 224", SEZ_BUS, "costanti e stato")
BUS   = taglia(SEZ_BUS, SEZ_DIS, "bus Z80")
DIS   = taglia(SEZ_DIS, SEZ_AUD, "disegno")
AUD   = taglia(SEZ_AUD, SEZ_SCALA, "audio AY")
CPU   = taglia(SEZ_CPU, SEZ_TASK, "quadro")

src = f'''/* generato da mk_1942test.py — NON modificare a mano: si rigenera. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
extern "C" {{
#include "cpus/z80/Z80.h"
}}
#include "z80_bus.h"

#include "machines/1942/1942_rom1.h"
#include "machines/1942/1942_rom2.h"
#include "machines/1942/1942_rom1_b0.h"
#include "machines/1942/1942_rom1_b1.h"
#include "machines/1942/1942_rom1_b2.h"
#include "machines/1942/1942_dipswitches.h"
#include "machines/1942/1942_character_cmap.h"
#include "machines/1942/1942_spritemap.h"
#include "machines/1942/1942_tilemap.h"
#include "machines/1942/1942_charmap.h"
#include "machines/1942/1942_sprite_cmap.h"
#include "machines/1942/1942_tile_cmap.h"
#include "machines/1942/tileaddr.h"

/* ---- ambiente: cio' che sul device danno Arduino/FreeRTOS/l'I2S --------- */
#define PPA_SRM_ROTATION_ANGLE_270 270
#define PPA_SRM_ROTATION_ANGLE_0     0
#define C19_HAS_PPA 0
#define PCE_SAMPLERATE 32000
#define LCD_H_RES 1024
#define LCD_V_RES 600
#include "esp_heap_caps.h"
#define ESP_CACHE_MSYNC_FLAG_DIR_C2M 0
static inline void esp_cache_msync(void *p, unsigned long n, int f) {{ (void)p;(void)n;(void)f; }}
static void audio_pce_begin(void) {{}}
static void audio_pce_write(const int16_t *stereo, int frames);

#define B_LEFT  0x01
#define B_RIGHT 0x02
#define B_UP    0x04
#define B_DOWN  0x08
#define B_FIRE  0x10
#define B_START 0x20
#define B_COIN  0x40
#define B_EXTRA 0x80
static uint8_t tasti_finti = 0;
static uint8_t bt_get_buttons(void) {{ return tasti_finti; }}
/* sul device la barra si scrive nel framebuffer del PANNELLO: qui non c'e',
   si da' un guscio con un buffer finto cosi' la funzione compila ed e' quella
   VERA del firmware, non una copia. */
static uint16_t panel_finto[LCD_H_RES * LCD_V_RES];
struct FintoLcd {{ void *get_frame_buffer() {{ return panel_finto; }} }};
static FintoLcd lcd;
static bool vol_bar_prova = false;
static bool bt_vol_mode(void) {{ return vol_bar_prova; }}
static int  bt_vol_pct(void)  {{ return 60; }}

{COST}
{BUS}
{DIS}
{AUD}
{CPU}

/* ---- WAV ---------------------------------------------------------------- */
static int16_t *wav = NULL; static long wav_n = 0, wav_cap = 0;
static void audio_pce_write(const int16_t *stereo, int frames)
{{
    if (wav_n + frames*2 > wav_cap) {{
        wav_cap = wav_cap ? wav_cap*2 : (1<<20);
        wav = (int16_t*)realloc(wav, wav_cap*sizeof(int16_t));
    }}
    memcpy(wav + wav_n, stereo, frames*2*sizeof(int16_t));
    wav_n += frames*2;
}}
static void salva_wav(const char *nome)
{{
    FILE *f = fopen(nome, "wb"); if (!f) return;
    const int ch=2, sr=AUD_RATE, bits=16; const long ds = wav_n*2;
    fwrite("RIFF",1,4,f); {{unsigned v=(unsigned)(36+ds); fwrite(&v,4,1,f);}}
    fwrite("WAVEfmt ",1,8,f); {{unsigned v=16; fwrite(&v,4,1,f);}}
    {{unsigned short v=1; fwrite(&v,2,1,f); v=ch; fwrite(&v,2,1,f);}}
    {{unsigned v=sr; fwrite(&v,4,1,f); v=sr*ch*bits/8; fwrite(&v,4,1,f);}}
    {{unsigned short v=ch*bits/8; fwrite(&v,2,1,f); v=bits; fwrite(&v,2,1,f);}}
    fwrite("data",1,4,f); {{unsigned v=(unsigned)ds; fwrite(&v,4,1,f);}}
    fwrite(wav,2,wav_n,f); fclose(f);
    long nz=0; int picco=0;
    for (long i=0;i<wav_n;i++) {{ if (wav[i]) nz++; int a=wav[i]<0?-wav[i]:wav[i]; if(a>picco) picco=a; }}
    printf("WAV %s: %.1f s, campioni non nulli %.1f%%, picco %d/32767%s\\n",
           nome, (double)wav_n/2/sr, 100.0*nz/(wav_n?wav_n:1), picco,
           picco>=32767?"  <-- TOSA":"");
}}

/* ---- PPM: il quadro come lo vede il GIOCATORE (gia' verticale) ---------- */
static void salva_ppm(const char *nome)
{{
    FILE *f = fopen(nome,"wb"); if(!f) return;
    fprintf(f,"P6\\n%d %d\\n255\\n", C19_W, C19_H);
    for (int y=0;y<C19_H;y++) for (int x=0;x<C19_W;x++) {{
        const uint16_t v = fb[(size_t)y*C19_W+x];
        const unsigned char rgb[3]={{(unsigned char)(((v>>11)&31)<<3),
                                    (unsigned char)(((v>>5)&63)<<2),
                                    (unsigned char)((v&31)<<3)}};
        fwrite(rgb,1,3,f);
    }}
    fclose(f);
}}

static void statistiche(int n)
{{
    long div=0; const uint16_t primo=fb[0]; int col[64]={{0}};
    for (long i=0;i<(long)C19_W*C19_H;i++) {{ if (fb[i]!=primo) div++; col[(fb[i]>>10)&63]++; }}
    int nc=0; for (int i=0;i<64;i++) if (col[i]) nc++;
    printf("quadro %4d: %5.1f%% dei pixel diverso dal fondo, %2d gruppi di colore\\n",
           n, 100.0*div/(C19_W*C19_H), nc);
}}

int main(int argc, char **argv)
{{
    int quadri = (argc>1) ? atoi(argv[1]) : 900;
    for (int i=1;i<argc;i++) if (!strcmp(argv[i],"volbar")) vol_bar_prova = true;

    memory = (uint8_t*)calloc(1, MEM_SIZE);
    fb     = (uint16_t*)calloc((size_t)C19_W*C19_H, 2);
    z80_bus_set(&C19_BUS);
    for (int i=0;i<2;i++) {{ current_cpu=i; ResetZ80(&cpu[i]); cpu[i].IAutoReset=1; cpu[i].TrapBadOps=0; }}
    current_cpu = 0;
    c19_audio_init();
    printf("audio_ok = %d\\n", (int)audio_ok);

    double resto = 0.0;
    for (int n=0;n<quadri;n++) {{
        tasti_finti = 0;
        if (n>=180 && n<210) tasti_finti |= B_COIN;
        if (n>=260 && n<290) tasti_finti |= B_START;
        if (n>=360 && (n/4)%2) tasti_finti |= B_FIRE;

        c19_run_frame();
        resto += 32000.0/60.0;
        while (resto >= AUD_CHUNK) {{ c19_audio_block(); resto -= AUD_CHUNK; }}
        c19_render();

        if (n%60==0 || n==quadri-1) {{
            char nome[64]; snprintf(nome,sizeof nome,"f%05d.ppm",n);
            salva_ppm(nome); statistiche(n);
        }}
    }}
    printf("scroll=%04X banco=%d palette=%d sub_cpu_reset=%d\\n",
           c19_scroll, c19_bank, c19_palette, (int)sub_cpu_reset);
    salva_wav("1942_audio.wav");
    return 0;
}}
'''

(HERE / "t1942.cpp").write_text(src, encoding="utf-8")

ZIG = [sys.executable, "-m", "ziglang"]
cmd = ZIG + ["c++", "-std=c++14", "-O2", "-w", "-Wno-register", f"-I{ROOT}", f"-I{HERE/'shim'}",
             "-o", str(HERE/"t1942.exe"), str(HERE/"t1942.cpp"),
             # ⚠️ Il core Z80 sta in `Z80.c` nella RADICE del progetto, non in
             # cpus/z80/ (li' ci sono solo gli header). Si compila a parte in un
             # .o — come fa l'harness di Raiden — perche' dato a zig insieme al
             # resto fa "CacheCheckFailed".
             str(HERE / "Z80_1942.o"),
             str(ROOT / "z80_bus.cpp")]
print("compilo…")
r = subprocess.run(cmd, capture_output=True, text=True)
if r.returncode:
    print(r.stdout[-6000:]); print(r.stderr[-6000:]); sys.exit("compilazione FALLITA")
print("eseguo…")
r = subprocess.run([str(HERE/"t1942.exe")] + sys.argv[1:], cwd=HERE)
try:
    from PIL import Image
    for p in sorted(HERE.glob("*.ppm")):
        Image.open(p).save(p.with_suffix(".png")); p.unlink()
    print("PPM convertiti in PNG.")
except ImportError:
    print("(PIL assente: restano i .ppm)")
sys.exit(r.returncode)
