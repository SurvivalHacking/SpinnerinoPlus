# ============================================================================
# romconv_campioni.py — i campioni audio, dai TUOI file WAV agli header.
#
# Questi suoni NON nascono da nessuna ROM: sul cabinato originale li fa un
# circuito analogico che qui non e' emulato, quindi sono registrazioni.
#
# Due sorgenti, in quest'ordine:
#   1. i tuoi WAV in  CAMPIONI\  (se ci sono, VINCONO)
#   2. i .bin gia' pronti in  CONVERTITORI\campioni\  (quelli che girano oggi)
# Cosi' il progetto suona subito, e chi vuole rifarli con le proprie
# registrazioni mette i WAV e rilancia.
#
#   s_walk0.wav  s_walk1.wav  s_walk2.wav   Donkey Kong, i passi
#   s_jump.wav   s_stomp.wav                salto, pestata
#   s_roar.wav   s_fall.wav   s_snap.wav    ruggito, caduta, morso
#   galaga_boom.wav                         Galaga, l'esplosione
#   nibbler_crunch.wav                      Nibbler, il crunch
#
# Formato accettato: WAV PCM, mono o stereo, 8 o 16 bit, qualunque frequenza.
# Ci pensa lui a portarlo a **8 bit CON SEGNO** alla frequenza giusta.
# ⚠️ Le frequenze non sono a caso: sono quelle a cui il firmware LEGGE i
# campioni, misurate sul codice che gira (vedi la tabella qui sotto).
# ============================================================================
import os, sys, wave

QUI       = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)
SORGENTE  = os.path.join(PACCHETTO, "CAMPIONI")
MACHINES  = os.path.join(PACCHETTO, "SPINNERINO", "machines")

#  file WAV            gioco     simbolo dell'array          Hz di destinazione
LAVORI = [
    ("s_walk0.wav",        "dkong",   "dkong_sample_walk0",   11765),
    ("s_walk1.wav",        "dkong",   "dkong_sample_walk1",   11765),
    ("s_walk2.wav",        "dkong",   "dkong_sample_walk2",   11765),
    ("s_jump.wav",         "dkong",   "dkong_sample_jump",    11765),
    ("s_stomp.wav",        "dkong",   "dkong_sample_stomp",   11765),
    ("s_roar.wav",         "dkong",   "dkong_sample_roar",    11765),
    ("s_fall.wav",         "dkong",   "dkong_sample_fall",    11765),
    ("s_snap.wav",         "dkong",   "dkong_sample_snapjaw", 11765),
    ("galaga_boom.wav",    "galaga",  "galaga_sample_boom",   12000),
    ("nibbler_crunch.wav", "nibbler", "nibbler_crunch",       24000),
]


def leggi_wav(percorso):
    """WAV -> (campioni con segno da -128 a 127, frequenza)."""
    with wave.open(percorso, "rb") as w:
        canali, larghezza, hz, n = w.getnchannels(), w.getsampwidth(), \
                                   w.getframerate(), w.getnframes()
        grezzi = w.readframes(n)
    if larghezza == 1:
        # WAV a 8 bit e' SENZA segno: si centra ([[feedback_dac_unipolare]])
        campioni = [b - 128 for b in grezzi]
    elif larghezza == 2:
        campioni = [int.from_bytes(grezzi[i:i + 2], "little", signed=True) >> 8
                    for i in range(0, len(grezzi), 2)]
    else:
        raise ValueError("WAV a %d byte per campione: servono 8 o 16 bit"
                         % (larghezza * 8))
    if canali > 1:                       # stereo -> mono, media dei canali
        campioni = [sum(campioni[i:i + canali]) // canali
                    for i in range(0, len(campioni) - canali + 1, canali)]
    return campioni, hz


def ricampiona(campioni, da_hz, a_hz):
    """Lineare. Se le frequenze coincidono non tocca niente."""
    if da_hz == a_hz or not campioni:
        return campioni
    n = max(1, int(len(campioni) * a_hz / da_hz))
    fuori = []
    for i in range(n):
        p = i * (len(campioni) - 1) / max(1, n - 1)
        i0 = int(p);  i1 = min(i0 + 1, len(campioni) - 1);  f = p - i0
        fuori.append(int(round(campioni[i0] * (1 - f) + campioni[i1] * f)))
    return fuori


def pronto_bin(wav):
    """Il .bin gia' pronto che corrisponde a quel WAV, se c'e'."""
    nome = "galaga_sample_boom.bin" if wav == "galaga_boom.wav" \
        else os.path.splitext(wav)[0] + ".bin"
    p = os.path.join(QUI, "campioni", nome)
    return p if os.path.isfile(p) else None


def main():
    os.makedirs(SORGENTE, exist_ok=True)
    fatti = 0
    for wav, gioco, simbolo, hz in LAVORI:
        tuo = os.path.join(SORGENTE, wav)
        if os.path.isfile(tuo):
            try:
                campioni, hz_in = leggi_wav(tuo)
            except Exception as e:
                print("  ERRORE su %s: %s" % (wav, e))
                return 1
            campioni = ricampiona(campioni, hz_in, hz)
            da = "dal tuo WAV"
        else:
            b = pronto_bin(wav)
            if not b:
                continue
            # i .bin sono gia' 8 bit CON SEGNO alla frequenza giusta
            campioni = [x - 256 if x > 127 else x for x in open(b, "rb").read()]
            da = "gia' pronto"
        byte = [(c if c >= -128 else -128) if c <= 127 else 127
                for c in campioni]
        byte = [b & 0xFF for b in byte]        # signed char -> byte
        uscita = os.path.join(MACHINES, gioco, simbolo + ".h")
        os.makedirs(os.path.dirname(uscita), exist_ok=True)
        righe = ["  " + ",".join("0x%02X" % b for b in byte[i:i + 16])
                 for i in range(0, len(byte), 16)]
        with open(uscita, "w", encoding="latin-1", newline="\n") as f:
            f.write("const unsigned char %s[] = {\n" % simbolo)
            f.write(",\n".join(righe) + "\n};\n")
        print("  %-22s %6d campioni @ %d Hz  (%s)"
              % (simbolo, len(byte), hz, da))
        fatti += 1
    print("campioni: %d header scritti" % fatti)
    return 0


sys.exit(main())
