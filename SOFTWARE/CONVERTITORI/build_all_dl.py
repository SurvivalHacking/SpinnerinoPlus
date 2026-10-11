#!/usr/bin/env python3
# ============================================================================
# build_all_dl.py — DRAGON'S LAIR, passo 1: dallo zip Daphne ai clip 800x600.
#
#   dl_ntsc.zip  ->  <clip>.mjpg + <clip>.idx + <clip>.pcm   (195 clip)
#
# ⭐ E' lo script che ha prodotto i video che GIRANO, non un rifacimento. Qui
# cambiano solo i percorsi: erano scritti dentro (`C:/Users/.../DragonsLair_NTSC`,
# `C:/Users/.../ffmpeg/...`) e su un altro computer non esistono.
#
# ⛔ NON usare `build_ld.py` per Dragon's Lair: quello toglie solo `.m2v` dal
# nome, quindi scrive `dls00.vob.mjpg`. Il firmware cerca `dls00.mjpg` (i nomi
# in `lair_framefile.h` hanno perso anche il `.vob`) e non troverebbe NIENTE.
#
# Legge la lista dei clip dal framefile UFFICIALE dentro lo zip
# (framefile/dlcdrom.TXT) ed estrae .m2v/.ogg al volo, uno per volta: lo zip e'
# quasi 1 GB e non serve scompattarlo tutto.
# RIPARTIBILE: salta i clip che hanno gia' il .idx.
#
# Uso:  python build_all_dl.py "<dl_ntsc.zip>" "<cartella uscita>" [--force]
# ============================================================================
import os, struct, subprocess, sys, zipfile

import _ffmpeg as _ff


def clip_list_from_framefile(zip_path):
    z = zipfile.ZipFile(zip_path)
    nome = next((n for n in z.namelist()
                 if n.lower().endswith("framefile/dlcdrom.txt")), None)
    if nome is None:
        sys.exit("ERRORE: nello zip manca framefile/dlcdrom.TXT")
    txt = z.read(nome).decode("latin-1")
    seen, out = set(), []
    for line in txt.splitlines():
        line = line.strip()
        if not line or line.startswith(".."):
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        fn = parts[1].strip()
        for suf in (".vob.m2v", ".m2v"):
            if fn.endswith(suf):
                clip = fn[:-len(suf)]
                break
        else:
            continue
        if clip not in seen:
            seen.add(clip)
            out.append((clip, fn))     # (nome sulla SD, nome dentro lo zip)
    return out


def zip_member(fn, ext):
    """`dls01.vob.m2v` -> `DLCDROM/dls01.vob.<ext>`"""
    base, stem_is_vob = fn, False
    for suf in (".vob.m2v", ".m2v"):
        if base.endswith(suf):
            base = base[:-len(suf)]
            stem_is_vob = (suf == ".vob.m2v")
            break
    return "DLCDROM/%s.%s" % (base + ".vob" if stem_is_vob else base, ext)


def ensure_extracted(zip_path, tmp, fn):
    z = zipfile.ZipFile(zip_path)
    paths = {}
    for ext in ("m2v", "ogg"):
        member = zip_member(fn, ext)
        local = os.path.join(tmp, "DLCDROM", os.path.basename(member))
        if not os.path.exists(local):
            try:
                z.extract(member, tmp)
            except KeyError:
                paths[ext] = None
                continue
        paths[ext] = local if os.path.exists(local) else None
    return paths.get("m2v"), paths.get("ogg")


def build_idx(mjpg, idx):
    data = open(mjpg, "rb").read()
    frames, pos = [], 0
    while True:
        soi = data.find(b"\xff\xd8", pos)
        if soi < 0: break
        eoi = data.find(b"\xff\xd9", soi + 2)
        if eoi < 0: break
        frames.append((soi, eoi + 2 - soi))
        pos = eoi + 2
    with open(idx, "wb") as f:
        f.write(struct.pack("<I", len(frames)))
        for o, l in frames:
            f.write(struct.pack("<II", o, l))
    return len(frames)


def convert(ffmpeg, zip_path, tmp, out, clip, fn):
    m2v, ogg = ensure_extracted(zip_path, tmp, fn)
    if not m2v:
        print("  %s: .m2v mancante, salto" % clip); return
    mjpg = os.path.join(out, clip + ".mjpg")
    pcm  = os.path.join(out, clip + ".pcm")
    idx  = os.path.join(out, clip + ".idx")
    subprocess.run([ffmpeg, "-y", "-i", m2v,
                    "-vf", "scale=800:600:flags=lanczos", "-r", "25",
                    "-c:v", "mjpeg", "-q:v", "6", "-f", "mjpeg", mjpg],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if ogg:
        subprocess.run([ffmpeg, "-y", "-i", ogg, "-vn",
                        "-af", "pan=stereo|FL=0.5*FL+0.5*FR|FR=0.5*FL+0.5*FR",
                        "-ar", "32000", "-f", "s16le", pcm],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        open(pcm, "wb").close()   # clip muto (es. le slates): .pcm vuoto = silenzio
    # i .m2v/.ogg gia' usati non servono piu': lo zip e' quasi 1 GB
    for p in (m2v, ogg):
        if p:
            try: os.remove(p)
            except OSError: pass
    nf = build_idx(mjpg, idx)
    print("  %s: %d frame, %.1fMB" % (clip, nf, os.path.getsize(mjpg) / 1048576.0),
          flush=True)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    zip_path, out = sys.argv[1], sys.argv[2]
    force = "--force" in sys.argv
    tmp = os.path.join(out, "_estratti")
    os.makedirs(out, exist_ok=True)
    os.makedirs(os.path.join(tmp, "DLCDROM"), exist_ok=True)
    ffmpeg = _ff.ffmpeg()

    allclips = clip_list_from_framefile(zip_path)
    todo = [(c, f) for (c, f) in allclips
            if force or not os.path.exists(os.path.join(out, c + ".idx"))]
    if "--limit" in sys.argv:      # per provare senza aspettare ore
        todo = todo[:int(sys.argv[sys.argv.index("--limit") + 1])]
    print("Clip nel framefile: %d - da convertire ora: %d"
          % (len(allclips), len(todo)), flush=True)
    for i, (clip, fn) in enumerate(todo, 1):
        print("[%d/%d]" % (i, len(todo)), end=" ")
        try:
            convert(ffmpeg, zip_path, tmp, out, clip, fn)
        except subprocess.CalledProcessError:
            print("  %s: ERRORE ffmpeg, salto" % clip)
    print("Fatto: %d clip in %s" % (len(allclips), out))


if __name__ == "__main__":
    main()
