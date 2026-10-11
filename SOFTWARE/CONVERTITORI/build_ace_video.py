#!/usr/bin/env python3
# ============================================================================
# build_ace_video.py — SPACE ACE, passo 1: da `ace.m2v` ai segmenti 800x600.
#
#   ace.daphne/ace.m2v  ->  segNNN.mjpg + segNNN.idx  +  ace.meta
#
# ⭐ PERCHE' SPACE ACE E' DIVERSO dagli altri due laserdisc: il suo pacchetto
# Daphne NON ha un clip per scena, ha UN SOLO file da 58643 frame. Dragon's
# Lair e DL2 hanno il framefile che dice dove tagliare; qui non c'e' niente da
# leggere, quindi si taglia a passo FISSO: 1000 frame per segmento.
# Il firmware fa lo stesso conto (`spaceace.cpp`, ace_seg_of_sample): il numero
# di segmento e' `frame / SEG_FRAMES`, non serve nessuna mappa.
#
# ⛔ NIENTE "-r": i frame restano quelli nativi (23.976 = 24000/1001). Il
# firmware converte frame->campione con `frame * 4004 / 3`, che vale SOLO a
# 23.976 fps. Forzando 25 fps l'audio andrebbe fuori sincrono di piu' di un
# minuto sul finale.
#
# ace.meta = "SEG_FRAMES  nframes  ld_start"
#   ld_start = 2  -> il frame 0 del video e' il frame 2 del laserdisc (la ROM
#   parla in frame di laserdisc; senza questo scostamento le scene partono
#   spostate). E' il valore che ha il file che gira.
#
# Uso:  python build_ace_video.py "<cartella ace.daphne>" "<cartella uscita>"
#       [--limit N]  ferma dopo N segmenti (per provare senza aspettare ore;
#                    NON scrive ace.meta, che sarebbe una mappa incompleta)
# ============================================================================
import os, struct, subprocess, sys

import _ffmpeg as _ff

SEG_FRAMES = 1000        # deve combaciare con ace.meta letto dal firmware
LD_START   = 2
QUALITA    = 6           # -q:v di ffmpeg, come gli altri due laserdisc


class Segmentatore:
    """Riceve i JPEG uno per uno e li impacchetta a gruppi di SEG_FRAMES,
    scrivendo per ognuno il .mjpg e il suo .idx (offset+lunghezza LOCALI)."""

    def __init__(self, uscita):
        self.uscita = uscita
        self.seg = -1
        self.f = None
        self.voci = []
        self.totale = 0

    def _chiudi(self):
        if self.f is None:
            return
        self.f.close()
        idx = os.path.join(self.uscita, "seg%03d.idx" % self.seg)
        with open(idx, "wb") as g:
            g.write(struct.pack("<I", len(self.voci)))
            for off, ln in self.voci:
                g.write(struct.pack("<II", off, ln))
        print("  seg%03d: %d frame, %.1f MB"
              % (self.seg, len(self.voci),
                 os.path.getsize(os.path.join(self.uscita,
                                              "seg%03d.mjpg" % self.seg)) / 1048576.0),
              flush=True)
        self.f, self.voci = None, []

    def aggiungi(self, jpeg):
        seg = self.totale // SEG_FRAMES
        if seg != self.seg:
            self._chiudi()
            self.seg = seg
            self.f = open(os.path.join(self.uscita, "seg%03d.mjpg" % seg), "wb")
        self.voci.append((self.f.tell(), len(jpeg)))
        self.f.write(jpeg)
        self.totale += 1

    def fine(self):
        self._chiudi()
        return self.totale


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    pack, uscita = sys.argv[1], sys.argv[2]
    limite = None
    if "--limit" in sys.argv:
        limite = int(sys.argv[sys.argv.index("--limit") + 1])
    m2v = os.path.join(pack, "ace.m2v")
    if not os.path.isfile(m2v):
        sys.exit("ERRORE: manca %s" % m2v)
    os.makedirs(uscita, exist_ok=True)

    meta = os.path.join(uscita, "ace.meta")
    if os.path.isfile(meta) and "--force" not in sys.argv:
        print("ace.meta c'e' gia' (%s): niente da rifare"
              % open(meta).read().strip())
        return

    print("Space Ace: un solo file da segmentare, e' LUNGO (decodifica intera)",
          flush=True)
    # ⭐ UNA sola passata di decodifica: ffmpeg manda i JPEG sullo standard
    # output e li smistiamo qui. Scrivere prima un .mjpg unico da ~2 GB e poi
    # ritagliarlo vorrebbe dire leggere e riscrivere due volte lo stesso video.
    p = subprocess.Popen(
        [_ff.ffmpeg(), "-v", "error", "-i", m2v,
         "-vf", "scale=800:600:flags=lanczos", "-fps_mode", "passthrough",
         "-c:v", "mjpeg", "-q:v", str(QUALITA), "-f", "mjpeg", "pipe:1"],
        stdout=subprocess.PIPE, bufsize=1 << 20)

    s = Segmentatore(uscita)
    buf = b""
    while True:
        pezzo = p.stdout.read(1 << 20)
        if not pezzo:
            break
        buf += pezzo
        # stessa regola di lettura degli altri convertitori: da SOI al primo EOI
        while True:
            i = buf.find(b"\xff\xd8")
            if i < 0:
                break
            j = buf.find(b"\xff\xd9", i + 2)
            if j < 0:
                break
            s.aggiungi(buf[i:j + 2])
            buf = buf[j + 2:]
        if limite is not None and s.totale >= limite * SEG_FRAMES:
            p.kill()
            break
    p.stdout.close()
    p.wait()
    n = s.fine()
    if n == 0:
        sys.exit("ERRORE: ffmpeg non ha prodotto frame (uscita %s)" % p.returncode)
    if limite is not None:
        print("PROVA: %d frame in %d segmenti, ace.meta NON scritto" % (n, limite))
        return

    with open(meta, "w") as f:
        f.write("%d %d %d\n" % (SEG_FRAMES, n, LD_START))
    print("ace.meta: %d %d %d   (%d segmenti)"
          % (SEG_FRAMES, n, LD_START, (n + SEG_FRAMES - 1) // SEG_FRAMES))


if __name__ == "__main__":
    main()
