#!/usr/bin/env python3
# ============================================================================
# build_dl_video_320x240.py
#   Ri-encoda i video Dragon's Lair del CABINATONE (800x600, per il display DSI
#   7") alla risoluzione dello SPINNERINO (ST7789 320x240).
#
#   IN :  CABINATONE\BACKUP CABINATONE\CONTENUTO SD\dl\*.mjpg + .idx + .pcm
#   OUT:  SPINNERINO PLUS WIFI\SD SPINNERINO\dl\*.mjpg + .idx + .pcm
#
#   ~1,3 GB @800x600  ->  ~210 MB @320x240
#
# PERCHE' 320x240 E NON 320x232 (l'altezza utile del framebuffer):
#   Il decoder JPEG HW dell'ESP32-P4 lavora a MCU. Con subsampling 4:2:0 la MCU
#   e' 16x16: 320/16=20 e 240/16=15 sono interi -> nessun padding interno.
#   232 non e' multiplo di 16. Encodiamo 240 (4:3 esatto, come l'originale) e il
#   player mostra le 232 righe centrali (taglia 4 sopra + 4 sotto, 3%).
#
# VINCOLI SUL JPEG (non cambiare senza motivo):
#   - baseline, NON progressive  -> il decoder HW non gestisce il progressive
#   - subsampling 4:2:0          -> MCU 16x16, vedi sopra
#   - niente EXIF/thumbnail      -> byte inutili in un contenitore senza header
#
# FORMATO (identico al CABINATONE, vedi daphne_poc.cpp):
#   .mjpg  JPEG concatenati, nessun header contenitore
#   .idx   uint32 nfr, poi nfr x (uint32 off, uint32 len) — offset LOCALI
#   .pcm   s16le stereo 32000 Hz — copiato invariato (l'audio non cambia)
# ============================================================================
import io
import os
import shutil
import struct
import sys
import time
from concurrent.futures import ProcessPoolExecutor

from PIL import Image

SRC = r"C:\Users\Tecnico\Desktop\PROGETTI SH\CABINATONE\BACKUP CABINATONE\CONTENUTO SD\dl"
DST = r"C:\Users\Tecnico\Desktop\PROGETTI SH\SPINNERINO PLUS WIFI\SD SPINNERINO\dl"

OUT_W, OUT_H = 320, 240
QUALITY      = 85          # 85 = buon compromesso nitidezza/dimensione
WORKERS      = 4


def read_idx(path):
    """Legge il .idx -> lista di (offset, len)."""
    with open(path, "rb") as f:
        nfr = struct.unpack("<I", f.read(4))[0]
        raw = f.read(nfr * 8)
    return [struct.unpack_from("<II", raw, i * 8) for i in range(nfr)]


def write_idx(path, entries):
    with open(path, "wb") as f:
        f.write(struct.pack("<I", len(entries)))
        for off, ln in entries:
            f.write(struct.pack("<II", off, ln))


def convert_clip(name):
    """Converte un clip. Ritorna (name, nframes, bytes_in, bytes_out, secondi)."""
    t0 = time.time()
    src_mjpg = os.path.join(SRC, name + ".mjpg")
    src_idx  = os.path.join(SRC, name + ".idx")
    src_pcm  = os.path.join(SRC, name + ".pcm")
    dst_mjpg = os.path.join(DST, name + ".mjpg")
    dst_idx  = os.path.join(DST, name + ".idx")
    dst_pcm  = os.path.join(DST, name + ".pcm")

    entries = read_idx(src_idx)
    out_entries = []
    bytes_in = os.path.getsize(src_mjpg)

    with open(src_mjpg, "rb") as fin, open(dst_mjpg, "wb") as fout:
        for off, ln in entries:
            fin.seek(off)
            im = Image.open(io.BytesIO(fin.read(ln)))
            im.draft("RGB", (OUT_W, OUT_H))      # decode ridotto in-libjpeg: molto piu' veloce
            im = im.convert("RGB").resize((OUT_W, OUT_H), Image.LANCZOS)

            buf = io.BytesIO()
            im.save(buf, "JPEG", quality=QUALITY, subsampling=2,
                    progressive=False, optimize=False)
            data = buf.getvalue()

            out_entries.append((fout.tell(), len(data)))
            fout.write(data)

    write_idx(dst_idx, out_entries)
    if os.path.exists(src_pcm):
        shutil.copy2(src_pcm, dst_pcm)          # audio invariato

    return (name, len(out_entries), bytes_in,
            os.path.getsize(dst_mjpg), time.time() - t0)


def main():
    os.makedirs(DST, exist_ok=True)
    names = sorted(f[:-5] for f in os.listdir(SRC) if f.lower().endswith(".mjpg"))
    if not names:
        sys.exit("Nessun .mjpg trovato in " + SRC)

    # i clip grandi per primi: con pochi worker evita la coda lunga in fondo
    names.sort(key=lambda n: -os.path.getsize(os.path.join(SRC, n + ".mjpg")))

    print(f"{len(names)} clip da convertire -> {OUT_W}x{OUT_H} q{QUALITY}, {WORKERS} worker")
    print(f"OUT: {DST}\n", flush=True)

    t0 = time.time()
    tot_fr = tot_in = tot_out = 0
    done = 0
    with ProcessPoolExecutor(max_workers=WORKERS) as ex:
        for name, nfr, bin_, bout, secs in ex.map(convert_clip, names):
            done += 1
            tot_fr += nfr
            tot_in += bin_
            tot_out += bout
            print(f"[{done:3d}/{len(names)}] {name:16s} {nfr:5d} fr  "
                  f"{bin_/1048576:7.1f}MB -> {bout/1048576:6.1f}MB  "
                  f"({bin_/max(bout,1):4.1f}x)  {secs:5.1f}s", flush=True)

    el = time.time() - t0
    print(f"\nFATTO in {el/60:.1f} min")
    print(f"  clip   : {len(names)}")
    print(f"  frame  : {tot_fr}")
    print(f"  video  : {tot_in/1048576:.0f} MB -> {tot_out/1048576:.0f} MB "
          f"({tot_in/max(tot_out,1):.1f}x piu' piccolo)")
    pcm = sum(os.path.getsize(os.path.join(DST, f))
              for f in os.listdir(DST) if f.endswith(".pcm"))
    print(f"  audio  : {pcm/1048576:.0f} MB (copiato)")
    print(f"  TOTALE SD: {(tot_out+pcm)/1048576:.0f} MB")


if __name__ == "__main__":
    main()
