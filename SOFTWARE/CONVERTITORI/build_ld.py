#!/usr/bin/env python3
# ⛔⛔ NON SERVE PER I TRE GIOCHI DEL PACCHETTO: e' rimasto qui come attrezzo
# per pacchetti Daphne NUOVI. Per Dragon's Lair usa `build_all_dl.py`, non
# questo: qui il nome del clip perde solo `.m2v`, quindi esce `dls00.vob.mjpg`
# mentre il firmware cerca `dls00.mjpg` e non troverebbe niente.
# Chi lancia la catena giusta e' `build_laserdisc.py`.
#
# build_ld.py - converte un pacchetto Daphne VLDP (framefile + .m2v/.ogg) nel
# formato del player CABINATONE, come build_all_dl.py ma GENERICO per piu' giochi:
#   <clip>.mjpg  MJPEG 800x600 25fps q6
#   <clip>.pcm   PCM s16le stereo 32kHz (downmix mono, ES8311)
#   <clip>.idx   uint32 nframes + nframes x (uint32 off, uint32 len) LE
#
# Adatto ai pacchetti scaricati (archive.org/details/space-ace), es:
#   Dragon's Lair 2  -> lair2.daphne/  (213 clip dl2-NNNNN.m2v + .ogg per-clip)  << pronto
#   Space Ace        -> ace.daphne/    (ace.m2v UNICO)  << 1 file gigante: per il
#                       gameplay va tagliato per scena (servono i frame dal ROM)
#   Cliff Hanger     -> cliff.daphne/  (cliff.m2v UNICO) idem, ma logica in DirkSimple
#
# Il framefile (prima riga '.', poi '<frame_laserdisc>  <file.m2v>') dice quali
# .m2v compongono il gioco: qui convertiamo ogni .m2v UNICO citato. Per i pacchetti
# multi-clip (DL2) esce un file per scena, come DL1. RESUMABILE (salta se .idx c'e').
#
# Uso:  python build_ld.py "<cartella .daphne>"  [--force]  [--limit N]
import subprocess, struct, os, sys, glob

import _ffmpeg as _ff
FFMPEG = _ff.ffmpeg()

def framefile_clips(pack):
    txt = None
    for t in glob.glob(os.path.join(pack, "*.txt")):
        if os.path.basename(t).lower().endswith("_info.txt"):
            continue
        txt = t; break
    if not txt:
        return []
    seen, out = set(), []
    for line in open(txt, encoding="latin-1").read().splitlines():
        line = line.strip()
        if not line or line == "." or line.startswith(".."):
            continue
        parts = line.split(None, 1)
        if len(parts) != 2 or not parts[1].lower().endswith(".m2v"):
            continue
        m2v = parts[1].strip()
        clip = m2v[:-4]
        if clip not in seen:
            seen.add(clip); out.append(clip)
    return out

def build_idx(mjpg, idx):
    data = open(mjpg, "rb").read()
    frames, pos = [], 0
    while True:
        soi = data.find(b"\xff\xd8", pos)
        if soi < 0: break
        eoi = data.find(b"\xff\xd9", soi + 2)
        if eoi < 0: break
        frames.append((soi, eoi + 2 - soi)); pos = eoi + 2
    with open(idx, "wb") as f:
        f.write(struct.pack("<I", len(frames)))
        for o, l in frames:
            f.write(struct.pack("<II", o, l))
    return len(frames)

def convert(pack, out, clip):
    m2v = os.path.join(pack, clip + ".m2v")
    ogg = os.path.join(pack, clip + ".ogg")
    if not os.path.exists(m2v):
        print(f"  {clip}: .m2v mancante, salto"); return
    mjpg, pcm, idx = (os.path.join(out, clip + e) for e in (".mjpg", ".pcm", ".idx"))
    subprocess.run([FFMPEG, "-y", "-i", m2v,
                    "-vf", "scale=800:600:flags=lanczos", "-r", "25",
                    "-c:v", "mjpeg", "-q:v", "6", "-f", "mjpeg", mjpg],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if os.path.exists(ogg):
        subprocess.run([FFMPEG, "-y", "-i", ogg, "-vn",
                        "-af", "pan=stereo|FL=0.5*FL+0.5*FR|FR=0.5*FL+0.5*FR",
                        "-ar", "32000", "-f", "s16le", pcm],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        open(pcm, "wb").close()
    nf = build_idx(mjpg, idx)
    print(f"  {clip}: {nf} frame, {os.path.getsize(mjpg)/1048576:.1f}MB")

def main():
    if len(sys.argv) < 2:
        print(__doc__); return
    pack = sys.argv[1].rstrip("/\\")
    force = "--force" in sys.argv
    limit = None
    if "--limit" in sys.argv:
        limit = int(sys.argv[sys.argv.index("--limit") + 1])
    out = pack.rstrip("/\\") + "_out"
    os.makedirs(out, exist_ok=True)
    clips = framefile_clips(pack)
    todo = [c for c in clips if force or not os.path.exists(os.path.join(out, c + ".idx"))]
    if limit:
        todo = todo[:limit]
    print(f"Pack {pack}: {len(clips)} clip nel framefile - da convertire ora: {len(todo)}")
    for i, clip in enumerate(todo, 1):
        print(f"[{i}/{len(todo)}]", end=" ")
        try:
            convert(pack, out, clip)
        except subprocess.CalledProcessError:
            print(f"  {clip}: ERRORE ffmpeg, salto")
    print(f"Fatto. Output in {out}/ -> copiare in /<gioco>/ sulla SD.")

if __name__ == "__main__":
    main()
