# ============================================================================
# romconv_dl_header.py — i due header di Dragon's Lair che restavano scoperti:
#
#   dl_sfx.h          i tre suoni dell'interfaccia (accept / buzz / credit),
#                     da  dl_ntsc.zip -> sound/dl_*.wav
#   lair_framefile.h  la mappa "frame laserdisc -> clip video",
#                     da  dl_ntsc.zip -> framefile/dlcdrom.TXT
#
# Metti `dl_ntsc.zip` (il pacchetto Daphne di Dragon's Lair) in VIDEO\.
# ⭐ La mappa e' la stessa di daphne: prima colonna del framefile = frame
# laserdisc dove INIZIA quel .m2v; ogni clip copre fino all'inizio del
# successivo. Niente assegnazioni indovinate.
# ============================================================================
import io, os, sys, wave, zipfile

QUI       = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)
VIDEO     = os.path.join(PACCHETTO, "VIDEO")
PROGETTO  = os.path.join(PACCHETTO, "SPINNERINO")

SUONI = [("sound/dl_accept.wav", "SFX_ACCEPT"),
         ("sound/dl_buzz.wav",   "SFX_BUZZ"),
         ("sound/dl_credit.wav", "SFX_CREDIT")]
HZ = 32000                      # il ponte audio dello SPINNERINO gira a 32 kHz


def trova_zip():
    for radice in (VIDEO, os.path.join(PACCHETTO, "ROMS")):
        if not os.path.isdir(radice):
            continue
        for r, _d, fs in os.walk(radice):
            for n in fs:
                if n.lower() == "dl_ntsc.zip":
                    return os.path.join(r, n)
    return None


def leggi_wav(dati):
    with wave.open(io.BytesIO(dati), "rb") as w:
        canali, larghezza, hz = w.getnchannels(), w.getsampwidth(), w.getframerate()
        grezzi = w.readframes(w.getnframes())
    if larghezza == 1:
        campioni = [(b - 128) * 256 for b in grezzi]
    elif larghezza == 2:
        campioni = [int.from_bytes(grezzi[i:i + 2], "little", signed=True)
                    for i in range(0, len(grezzi), 2)]
    else:
        raise ValueError("WAV a %d bit non gestito" % (larghezza * 8))
    if canali > 1:
        campioni = [sum(campioni[i:i + canali]) // canali
                    for i in range(0, len(campioni) - canali + 1, canali)]
    if hz != HZ and campioni:
        # ⚠️ Il +1 non e' un arrotondamento a caso: senza, tutti e tre i suoni
        # uscivano ESATTAMENTE un campione piu' corti di quelli che girano
        # (2341/3213/6460 invece di 2342/3214/6461). Un fuori-di-uno uguale su
        # tutti e tre e' un difetto del conto, non del materiale.
        n = max(1, int(len(campioni) * HZ / hz) + 1)
        fuori = []
        for i in range(n):
            p = i * (len(campioni) - 1) / max(1, n - 1)
            i0 = int(p);  i1 = min(i0 + 1, len(campioni) - 1);  f = p - i0
            fuori.append(int(round(campioni[i0] * (1 - f) + campioni[i1] * f)))
        campioni = fuori
    return campioni


def scrivi_sfx(z):
    pezzi = []
    for dentro, simbolo in SUONI:
        nome = next((n for n in z.namelist() if n.lower() == dentro), None)
        if nome is None:
            print("  ERRORE: in dl_ntsc.zip manca %s" % dentro)
            return False
        v = leggi_wav(z.read(nome))
        righe = ["  " + ",".join(str(x) for x in v[i:i + 16])
                 for i in range(0, len(v), 16)]
        # ⛔ La LUNGHEZZA e' obbligatoria: `laserdisc.cpp` scrive
        # `g_sfx_len = SFX_ACCEPT_LEN`. Senza, il progetto NON COMPILA
        # («SFX_ACCEPT_LEN was not declared»), e l'errore non nomina mai
        # questo file: sembra un guasto di laserdisc.cpp.
        pezzi.append("static const int16_t %s[%d] = {\n%s\n};\n"
                     "static const int %s_LEN = %d;\n"
                     % (simbolo, len(v), ",\n".join(righe), simbolo, len(v)))
    p = os.path.join(PROGETTO, "dl_sfx.h")
    with open(p, "w", encoding="latin-1", newline="\n") as f:
        f.write("#pragma once\n")
        f.write("// Campioni sonori ORIGINALI Dragon's Lair "
                "(dl_ntsc.zip/sound/*.wav) a %dkHz mono s16.\n" % (HZ // 1000))
        f.write("#include <stdint.h>\n\n")
        f.write("\n".join(pezzi))
    print("  dl_sfx.h  (%d suoni)" % len(SUONI))
    return True


def scrivi_framefile(z):
    nome = next((n for n in z.namelist()
                 if n.lower().endswith("framefile/dlcdrom.txt")), None)
    if nome is None:
        print("  ERRORE: in dl_ntsc.zip manca framefile/dlcdrom.TXT")
        return False
    voci = []
    for riga in z.read(nome).decode("latin-1").splitlines():
        pezzi = riga.split()
        if len(pezzi) != 2 or not pezzi[0].isdigit():
            continue            # la prima riga e' la cartella, non una voce
        clip = pezzi[1]
        for coda in (".vob.m2v", ".m2v"):
            if clip.lower().endswith(coda):
                clip = clip[:-len(coda)];  break
        voci.append((int(pezzi[0]), clip))
    if not voci:
        print("  ERRORE: framefile vuoto o in un formato inatteso")
        return False
    voci.sort()

    # ⚠️ Il framefile ha dei DOPPIONI: `dls20` compare due volte (14229 e
    # 14232). La versione che gira tiene la PRIMA e le lascia coprire fino al
    # clip successivo DIVERSO. Tenendole entrambe si creava una regione da 3
    # frame che il firmware non trova mai.
    ripulite = []
    for inizio, clip in voci:
        if ripulite and ripulite[-1][1] == clip:
            continue
        ripulite.append((inizio, clip))
    voci = ripulite

    righe = []
    for i, (inizio, clip) in enumerate(voci):
        # l'ampiezza e' fino all'inizio del clip successivo (l'ultima e' stimata)
        durata = (voci[i + 1][0] - inizio) if i + 1 < len(voci) else 0
        righe.append('    { %7d, %7d, "%s" },' % (inizio, durata, clip))

    p = os.path.join(PROGETTO, "lair_framefile.h")
    with open(p, "w", encoding="latin-1", newline="\n") as f:
        f.write("#pragma once\n")
        f.write("// lair_framefile.h - mappa «frame laserdisc -> clip video».\n")
        f.write("// Generato dal framefile UFFICIALE dl_ntsc.zip:"
                " framefile/dlcdrom.TXT.\n")
        f.write("// Prima colonna = frame dove INIZIA quel file; ogni clip copre\n"
                "// [ld_start, ld_start del successivo).\n")
        f.write("// I clip vanno su SD come /sdcard/dl/<clip>.mjpg + .idx + .pcm\n\n")
        f.write("typedef struct {\n"
                "    int         ld_start;\n"
                "    int         ld_len;\n"
                "    const char *clip;\n"
                "} dl_frame_entry_t;\n\n")
        f.write("static const dl_frame_entry_t DL_FRAMEFILE[] = {\n")
        f.write("\n".join(righe) + "\n};\n")
        f.write("\nstatic const int DL_FRAMEFILE_N =\n"
                "    (int)(sizeof(DL_FRAMEFILE) / sizeof(DL_FRAMEFILE[0]));\n")
        # ⛔ Anche la FUNZIONE di ricerca sta in questo header, e la chiama
        # laserdisc.cpp in tre punti. Scrivendo solo la tabella il progetto non
        # compila: «dl_framefile_find was not declared in this scope».
        # La lista e' ordinata per ld_start: si tiene l'ultima voce che sta
        # sotto al frame cercato.
        f.write("\n// Indice della voce che contiene 'ldframe' (o la piu'"
                " vicina sotto;\n// -1 se il frame precede la prima voce)."
                " Lista ordinata per ld_start.\n"
                "static inline int dl_framefile_find(int ldframe)\n"
                "{\n"
                "    int best = -1;\n"
                "    for (int i = 0; i < DL_FRAMEFILE_N; i++) {\n"
                "        if (DL_FRAMEFILE[i].ld_start <= ldframe) best = i;\n"
                "        else break;\n"
                "    }\n"
                "    return best;\n"
                "}\n")
    print("  lair_framefile.h  (%d clip)" % len(voci))
    return True


def main():
    z_path = trova_zip()
    if not z_path:
        print("ERRORE: manca dl_ntsc.zip (mettilo in VIDEO\\)")
        return 1
    with zipfile.ZipFile(z_path) as z:
        ok = scrivi_sfx(z) and scrivi_framefile(z)
    return 0 if ok else 1


sys.exit(main())
