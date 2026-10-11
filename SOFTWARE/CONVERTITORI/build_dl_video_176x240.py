#!/usr/bin/env python3
# ============================================================================
# build_dl_video_176x240.py
#   Ri-encoda i video Dragon's Lair del CABINATONE (800x600, per il display DSI
#   7") al formato PRONTO-DA-BLITTARE dello SPINNERINO (ST7789 240x320).
#
#   OUT:  SPINNERINO PLUS WIFI\SD SPINNERINO\dl\*.mjpg + .idx + .pcm
#
# PERCHE' 176x232 E PERCHE' RUOTATO (2026-07-15):
#   L'utente vuole il video DRITTO (non ruotato) sul pannello portrait, quindi
#   rimpicciolito con bande nere sopra/sotto. Il pannello e' portrait ma il
#   framebuffer del menu e' landscape-trasposto: display(x_p,y_p) = FB[239-x_p][y_p].
#   Fare rotazione+scaling A RUNTIME sarebbe cache-hostile (accessi a stride 640
#   in PSRAM) e costerebbe piu' del decode. Li facciamo QUI, una volta sola:
#     1) resize 800x600 -> 240x176   (riempie i 240 px; ~2% stirato, voluto)
#     2) rot90 ANTIORARIO            -> 176x240  (compensa la trasposizione del FB)
#     (niente canvas: 176x240 e' gia' esatta e allineata su entrambi gli assi)
#   Sul device: FB[y_l][72..247] = img[y_l][] -> un memcpy, con la finestra
#   allargata a 240 righe (setAddrWindow(x,0,320,240)): il FB normale ne copre
#   solo 232 (TFT_HW_Y=8) e lasciava 8 px neri irraggiungibili.
#   Risultato a schermo: video DRITTO 240x176 a TUTTA LARGHEZZA, bande nere
#   solo sopra e sotto.
#   Catena verificata in Python end-to-end (converter -> FB -> display).
#
#   BONUS: 176x240 = 42240 px contro 320x240 = 76800 -> file ~55%, decode piu'
#   veloce, meno banda SD.
#
#   .idx   invariato: uint32 nfr + nfr*(off,len) LOCALI al file
#   .pcm   s16le stereo 32000 Hz - copiato invariato (l'audio non cambia)
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

VID_W, VID_H = 240, 176      # video DRITTO: TUTTI i 240 px di larghezza
                             # 240/176 = 1.364 (4:3 = 1.333): ~2% stirato in
                             # verticale. Scelta esplicita dell'utente: meglio
                             # riempire lo schermo che l'aspect perfetto.
OUT_W, OUT_H = 176, 240      # immagine finale (ruotata). 176=11*16, 240=15*16:
                             # MCU 16x16 allineata, e nessun canvas: e' esatta.
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
            im.draft("RGB", (VID_W, VID_H))     # decode ridotto in-libjpeg: molto piu' veloce
            im = im.convert("RGB").resize((VID_W, VID_H), Image.LANCZOS)
            # ruota 90 ANTIORARI: compensa la trasposizione del framebuffer, cosi'
            # sul display il video appare DRITTO (vedi nota in testa al file).
            im = im.transpose(Image.ROTATE_90)               # 240x176 -> 176x240
            # canvas 176x232: 176 = 11*16 -> MCU 16x16 allineata, niente padding
            # interno al decoder. Il video (174) resta centrato: 1 px nero per lato.
            canvas = Image.new("RGB", (OUT_W, OUT_H), (0, 0, 0))
            canvas.paste(im, ((OUT_W - im.width) // 2, (OUT_H - im.height) // 2))
            im = canvas

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
