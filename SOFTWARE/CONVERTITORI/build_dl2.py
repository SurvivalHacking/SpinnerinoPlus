#!/usr/bin/env python3
# ============================================================================
# build_dl2.py — converte il pacchetto Daphne di Dragon's Lair II nel formato
# del player CABINATONE. Derivato da build_ld.py ma con DUE differenze critiche.
#
# ⚠️ PERCHE' NON SI USA build_ld.py:
#   1. build_ld.py forza "-r 25" -> RESAMPLA il video e distrugge la
#      corrispondenza frame-a-frame. Andava bene per le intro in loop, NON per
#      il gioco ROM-driven, dove il ROM cerca il frame 4577 esatto e li' deve
#      trovare quel fotogramma. (Stessa trappola gia' pagata su Space Ace.)
#   2. Il pack DL2 e' MISTO: quasi tutti i clip sono a 23.976 fps (rip con
#      inverse telecine: frame_video = frame_LD * 4/5), ma almeno dl2-00002.m2v
#      e' a 29.97 fps = mappa 1:1. MISURATO, non dedotto. Un rapporto globale
#      sbaglierebbe meta' gioco. Qui il rapporto e' PER CLIP.
#
# Uscita, per ogni clip <nome>:
#   <nome>.mjpg  MJPEG 800x600 q6, frame NATIVI (nessun resampling)
#   <nome>.pcm   PCM s16le stereo 32 kHz (per ES8311)
#   <nome>.idx   uint32 nframes + nframes x (uint32 off, uint32 len) LE
# piu' un unico  dl2.meta  con la mappa: <frame_LD_iniziale> <nome> <nframes>
#   <num> <den>  (fps del clip come frazione, per calcolare il rapporto)
#
# Il firmware mappa:  clip = ultimo con inizio <= frame_LD
#                     indice = round((frame_LD - inizio) * fps_clip / 29.97)
#
# Uso:  python build_dl2.py <cartella lair2.daphne> <cartella uscita> [--limit N]
# ============================================================================
import subprocess, struct, os, sys, glob, json

import _ffmpeg as _ff
FFMPEG = _ff.ffmpeg()
FFPROBE = _ff.ffprobe()
DISC_FPS_NUM, DISC_FPS_DEN = 30000, 1001      # 29.97: la numerazione del laserdisc


def probe_fps(path):
    """framerate reale del clip, come frazione esatta (24000/1001 o 30000/1001)."""
    out = subprocess.run(
        [FFPROBE, "-v", "error", "-select_streams", "v:0",
         "-show_entries", "stream=r_frame_rate", "-of", "csv=p=0", path],
        capture_output=True, text=True).stdout.strip()
    out = ''.join(c for c in out if c.isdigit() or c == '/')   # ffprobe puo'
    num, den = out.split('/') if '/' in out else (out, '1')    # lasciare una virgola
    return int(num), int(den)


def framefile(pack):
    """[(frame_laserdisc, nome.m2v)] dal framefile del pacchetto."""
    txt = None
    for t in glob.glob(os.path.join(pack, "*.txt")):
        if not os.path.basename(t).lower().endswith("_info.txt"):
            txt = t; break
    if not txt:
        sys.exit("framefile non trovato in " + pack)
    rows = []
    for line in open(txt, encoding='latin1').read().splitlines()[1:]:
        p = line.split()
        if len(p) >= 2:
            rows.append((int(p[0]), p[1]))
    return sorted(rows)


def build_idx(mjpg, idx_path):
    """indice dei frame MJPEG: scansione dei marcatori SOI/EOI."""
    data = open(mjpg, 'rb').read()
    offs, i, n = [], 0, len(data)
    while True:
        s = data.find(b'\xff\xd8', i)
        if s < 0: break
        e = data.find(b'\xff\xd9', s + 2)
        if e < 0: break
        offs.append((s, e + 2 - s))
        i = e + 2
    with open(idx_path, 'wb') as f:
        f.write(struct.pack('<I', len(offs)))
        for o, l in offs:
            f.write(struct.pack('<II', o, l))
    return len(offs)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    pack, outdir = sys.argv[1], sys.argv[2]
    limit = None
    if '--limit' in sys.argv:
        limit = int(sys.argv[sys.argv.index('--limit') + 1])
    os.makedirs(outdir, exist_ok=True)

    clips = framefile(pack)
    if limit: clips = clips[:limit]
    meta, tot_fr = [], 0

    for k, (ld_start, name) in enumerate(clips):
        src = os.path.join(pack, name)
        if not os.path.exists(src):
            print("  MANCA %s (salto)" % name); continue
        base = os.path.splitext(name)[0]
        mjpg = os.path.join(outdir, base + '.mjpg')
        idx  = os.path.join(outdir, base + '.idx')
        pcm  = os.path.join(outdir, base + '.pcm')
        num, den = probe_fps(src)

        if os.path.exists(idx):                       # ripartenza: gia' fatto
            nfr = struct.unpack('<I', open(idx, 'rb').read(4))[0]
        else:
            # NIENTE "-r": i frame restano quelli nativi del sorgente.
            subprocess.run([FFMPEG, "-y", "-v", "error", "-i", src,
                            "-vf", "scale=800:600:flags=lanczos",
                            "-fps_mode", "passthrough",
                            "-c:v", "mjpeg", "-q:v", "6", "-f", "mjpeg", mjpg],
                           check=True)
            nfr = build_idx(mjpg, idx)

        ogg = os.path.join(pack, base + '.ogg')
        if os.path.exists(ogg) and not os.path.exists(pcm):
            subprocess.run([FFMPEG, "-y", "-v", "error", "-i", ogg, "-vn",
                            "-ac", "2", "-ar", "32000",
                            "-f", "s16le", pcm], check=True)

        meta.append((ld_start, base, nfr, num, den))
        tot_fr += nfr
        gap = clips[k + 1][0] - ld_start if k + 1 < len(clips) else None
        atteso = (gap * num * DISC_FPS_DEN) // (den * DISC_FPS_NUM) if gap else None
        flag = ''
        if atteso is not None and abs(nfr - atteso) > 2:
            flag = '  <-- ATTENZIONE: attesi %d' % atteso
        print("[%3d/%3d] %-16s LD %-6d  %5d frame  %d/%d fps%s"
              % (k + 1, len(clips), base, ld_start, nfr, num, den, flag))

    with open(os.path.join(outdir, 'dl2.meta'), 'w') as f:
        f.write("# frame_LD  clip  nframes  fps_num  fps_den   (disco = 30000/1001)\n")
        for ld, base, nfr, num, den in meta:
            f.write("%d %s %d %d %d\n" % (ld, base, nfr, num, den))

    print("\nclip: %d   frame totali: %d" % (len(meta), tot_fr))
    rat = {}
    for _, _, _, num, den in meta:
        rat['%d/%d' % (num, den)] = rat.get('%d/%d' % (num, den), 0) + 1
    print("framerate trovati:", rat)


if __name__ == '__main__':
    main()
