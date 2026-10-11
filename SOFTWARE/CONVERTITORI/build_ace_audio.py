# ============================================================================
# build_ace_audio.py — audio di scena (musica/voce) di SPACE ACE per CABINATONE
#
#   ace.daphne/ace.ogg  ->  ace_video/segNNN.pcm   (s16le STEREO 32kHz)
#
# Segmentato ESATTAMENTE come il video (segNNN.mjpg, SEG_FRAMES frame l'uno):
# un file unico da 182MB darebbe fseek lento su FAT (lezione Dragon's Lair:
# il video unico da 1.1GB rallentava il gioco -> tornati ai file per-clip).
#
# ---------------------------------------------------------------------------
# MAPPING (verificato 2026-07-14, offline):
#   ace.m2v  = 58643 frame @ 23.976fps (24000/1001)  = 2445.9 s
#   MA il contenuto vero finisce al frame ~34108: da li' in poi sono 24533
#   frame di NERO FISSO (verificato con mpdecimate: 32288 frame unici, l'ultimo
#   a pts 1422.63s; i frame 34500/40000/55000/58600 sono byte-identici e neri).
#   ace.ogg  = 1422.7355 s  ==  1422.63 s di contenuto video (match a 5e-5).
#   => IL MAPPING E' LINEARE. Nessuna non-linearita' da still-frame.
#
#   sample(f) = f / (24000/1001) * 32000 = f * 1001 * 32000 / 24000 = f*4004/3
#   (esatto in interi; il firmware usa la STESSA formula - devono combaciare)
#
# I segmenti >= 35 non hanno audio (la' il video e' nero): non vengono scritti,
# e il firmware tratta il file mancante come silenzio.
# ============================================================================
import os, subprocess, sys

import _ffmpeg as _ff
FFMPEG = _ff.ffmpeg()
# ⛔ Qui c'erano tre percorsi del PC di chi ha scritto lo script
# (C:/Users/.../LASERDISC_PACKS/...): su un altro computer non esistono e
# l'audio di Space Ace muore al primo `open`. Ora arrivano da chi lo chiama.
# Uso:  python build_ace_audio.py "<cartella ace.daphne>" "<cartella segNNN>"
PACK   = sys.argv[1] if len(sys.argv) > 1 else "ace.daphne"
OUT    = sys.argv[2] if len(sys.argv) > 2 else "ace_video"
TMP    = os.path.join(OUT, "_tmp")

SR       = 32000          # sample rate ES8311 (PCE_SAMPLERATE)
CH       = 2              # stereo (mono duplicato) -> 4 byte/sample
BPS      = 2 * CH         # byte per sample-frame

def seg_start_sample(k, seg_frames):
    """Primo sample del segmento k. STESSA formula del firmware (ace_hw.cpp)."""
    return (k * seg_frames * 4004) // 3

def read_meta():
    """ace.meta = 'SEG_FRAMES nframes ld_start' (scritto dal converter video)."""
    with open(os.path.join(OUT, "ace.meta")) as f:
        sf, nf, ls = (int(x) for x in f.read().split()[:3])
    return sf, nf, ls

def main():
    seg_frames, nframes, ld_start = read_meta()
    print(f"ace.meta: SEG_FRAMES={seg_frames} nframes={nframes} ld_start={ld_start}")

    os.makedirs(TMP, exist_ok=True)
    raw = os.path.join(TMP, "ace_audio.raw")

    # --- 1) decodifica UNA volta l'ogg intero a raw s16le stereo 32k ---
    if not os.path.exists(raw):
        print("decodifica ace.ogg -> raw s16le 32k stereo (una passata)...")
        subprocess.run([FFMPEG, "-y", "-v", "error",
                        "-i", os.path.join(PACK, "ace.ogg"),
                        "-vn",
                        "-ac", str(CH), "-ar", str(SR),
                        "-f", "s16le", raw], check=True)
    total_samples = os.path.getsize(raw) // BPS
    print(f"audio totale: {total_samples} sample = {total_samples/SR:.2f} s")

    # --- 2) taglia in segmenti allineati ai frame del video ---
    nsegs = (nframes + seg_frames - 1) // seg_frames
    written = 0
    with open(raw, "rb") as fr:
        for k in range(nsegs):
            s0 = seg_start_sample(k, seg_frames)
            s1 = seg_start_sample(k + 1, seg_frames)
            if s0 >= total_samples:
                break                      # da qui in poi il video e' nero: niente audio
            s1 = min(s1, total_samples)
            fr.seek(s0 * BPS)
            data = fr.read((s1 - s0) * BPS)
            p = os.path.join(OUT, "seg%03d.pcm" % k)
            with open(p, "wb") as fw:
                fw.write(data)
            written += 1
            print("  seg%03d.pcm  frame %5d..%5d  %6.2f MB"
                  % (k, k * seg_frames, min((k + 1) * seg_frames, nframes) - 1,
                     len(data) / 1e6))

    print(f"\nscritti {written} segmenti audio su {nsegs} (gli altri = video nero, nessun audio)")
    print(f"COPIARE {OUT}/seg*.pcm  ->  SD /ace/")

if __name__ == "__main__":
    main()
