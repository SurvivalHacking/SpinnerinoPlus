# -*- coding: utf-8 -*-
# ============================================================================
# convert_xevious_boom.py — i due campioni delle ESPLOSIONI di Xevious.
#
# Xevious non emula il Namco 54XX (il microcontrollore che sull'hardware fa le
# esplosioni pilotando un generatore di rumore), quindi navetta e bersagli
# esplodono muti. I comandi pero' ARRIVANO gia' a `xev_namco_write()`: qui si
# preparano i due suoni da riprodurre quando arrivano.
#
# Sorgente: **Super Xevious NES** (`machines/supxevs`), su richiesta dell'utente.
# Estratti con `_verify_gfx\xevious_54xx\mk_boom.py`, che compila il `nes_vs.cpp`
# vero con il mixer APU patchato in modo da lasciare il **solo canale rumore**
# (sul NES le esplosioni stanno li'; musica e motore spariscono) e un copione
# che fa avvenire l'evento voluto. Le assegnazioni sono provate coi fotogrammi:
#   - navetta  = copione "muori" (fermo e senza sparare): resta solo la sua
#   - bersaglio = copione che spara, e nei fotogrammi il PUNTEGGIO SALE
#
# Formato in uscita: PCM **8 bit con segno a 11025 Hz**, che per un rumore basta
# e costa un quarto della flash. Il player in audio.cpp ricampiona a 32 kHz.
#
# USO:  python convert_xevious_boom.py
# ============================================================================
import pathlib, wave
import numpy as np

QUI  = pathlib.Path(__file__).parent
SRC  = QUI / "campioni"
DEST = QUI.parent / "SPINNERINO" / "machines" / "xevious" / "xevious_boom.h"

RATE_OUT = 16000                      # meta di 32 kHz: stesso schema di Galaga (ptr avanza ogni 2 campioni)
# (file, nome, durata da tenere in secondi)
CAMPIONI = [
    ("BOOM_navetta_1.wav", "XEV_BOOM_NAVETTA",   1.00),
    # ⭐ il bersaglio ACCORCIATO (richiesta utente): nell'originale arcade le
    #   esplosioni piccole durano molto meno di un secondo, e cosi' i due suoni
    #   si distinguono anche a orecchio.
    #   ⚠️ 2026-08-11: allungato da 0,38 a **0,50 s** su richiesta dell'utente
    #   (prima su CABINATONE, poi riportato qui per tenere allineati i due
    #   progetti). Il WAV sorgente dura 1080 ms: sono 120 ms di audio VERO, non
    #   di silenzio aggiunto in coda.
    ("BOOM_bersaglio.wav", "XEV_BOOM_BERSAGLIO", 0.50),
]


def leggi(p):
    with wave.open(str(p), "rb") as w:
        sr, nch = w.getframerate(), w.getnchannels()
        d = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float32)
    if nch == 2:
        d = d.reshape(-1, 2).mean(axis=1)
    return d, sr


righe = ["// generato da convert_xevious_boom.py -- NON modificare a mano",
         "// Le due esplosioni di Xevious, prese da Super Xevious (NES).",
         "// PCM 8 bit con segno, %d Hz. Vedi la testa del .py per come sono" % RATE_OUT,
         "// state estratte e perche' l'assegnazione e' provata e non intuita.",
         "#pragma once", "#include <stdint.h>", ""]

for nome_file, simbolo, durata in CAMPIONI:
    d, sr = leggi(SRC / nome_file)
    d = d[:int(durata * sr)]
    # ricampionamento lineare a RATE_OUT
    n_out = int(len(d) * RATE_OUT / sr)
    x = np.arange(n_out) * (sr / RATE_OUT)
    i0 = np.clip(x.astype(np.int32), 0, len(d) - 2)
    y = d[i0] * (1 - (x - i0)) + d[i0 + 1] * (x - i0)
    # dissolvenza finale di 15 ms: un taglio netto si sente come "clic"
    nf = int(RATE_OUT * 0.015)
    if len(y) > nf:
        y[-nf:] *= np.linspace(1.0, 0.0, nf)
    y = y * (127.0 / max(1.0, np.abs(y).max()))
    b = np.clip(np.round(y), -127, 127).astype(np.int8)
    righe.append("// %-18s %5d campioni = %.0f ms" % (nome_file, len(b), 1000.0 * len(b) / RATE_OUT))
    righe.append("#define %s_LEN %d" % (simbolo, len(b)))
    righe.append("static const int8_t %s[] PROGMEM = {" % simbolo)
    for i in range(0, len(b), 20):
        righe.append("  " + ",".join("%4d" % v for v in b[i:i + 20]) + ",")
    righe.append("};")
    righe.append("")
    print("%-20s -> %-20s %5d campioni (%.0f ms, %.1f KB)"
          % (nome_file, simbolo, len(b), 1000.0 * len(b) / RATE_OUT, len(b) / 1024.0))

righe.append("#define XEV_BOOM_RATE %d" % RATE_OUT)
DEST.write_text("\n".join(righe) + "\n", encoding="utf-8")
print("scritto", DEST)
print("totale in flash: %.1f KB" % (DEST.stat().st_size / 1024.0 / 5))  # ~5 char per byte
