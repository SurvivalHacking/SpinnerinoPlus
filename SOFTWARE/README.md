# Spinnerino Maker

Uno strumento con finestra, uguale su **Windows**, **Mac** e **Linux**, che fa da solo la parte di *preparazione* dello **Spinnerino Plus**: prende le tue ROM MAME e i tuoi pacchetti Daphne e li trasforma nei file che servono al firmware.

Sostituisce `prepara.bat` (che gira solo su Windows) con due avvii, `setup.bat` e `setup.sh`, e con un'interfaccia che mostra cosa c'è, cosa manca e cosa sta succedendo.

Autore dello strumento: **Alessandro Porcheddu**

---

## Il progetto e i ringraziamenti

Spinnerino Maker non è un progetto a sé: è un accessorio per lo **Spinnerino Plus**, un mini cabinato arcade basato su ESP32-P4, nato per i giochi a manopola come Arkanoid e cresciuto fino a far girare decine di classici, i laserdisc (Dragon's Lair, Space Ace, Dragon's Lair 2) e il Neo Geo.

Tutto il lavoro vero è di chi ha fatto lo Spinnerino Plus:

- **Marco Prunca**, autore del progetto, del firmware e del codice per l'ESP32-C6;
- **Davide Gatti** di **Survival Hacking** ([github.com/survivalhacking](https://github.com/survivalhacking)), che lo ha sviluppato, documentato e pubblicato.

Video del progetto: <https://youtu.be/VE9SN8RUckk>

Grazie a tutti e due. `prepara.py`, i convertitori e il sistema che li fa girare su qualunque computer sono un lavoro enorme e curato, e questo strumento si limita a metterci sopra una faccia comoda. `prepara.py` e i convertitori sono quelli originali; le uniche modifiche sono tre piccole correzioni per Mac e Linux in `_ambiente.py` e i due convertitori di Galaga e Galaxian, descritte più sotto.

Grazie anche a chi ha fatto le cose su cui ci si appoggia:

- **galagino** di Till Harbaum, da cui discendono molti convertitori di ROM;
- **DirkSimple**, da cui viene lo script di gioco di Dragon's Lair;
- **python-build-standalone** (Astral), per il Python portatile;
- **FFmpeg**, con le build di BtbN (Windows) ed evermeet.cx (Mac);
- **Pillow** e **NumPy**.

### Licenza

Lo Spinnerino Plus è distribuito con licenza **Creative Commons Attribuzione - Non Commerciale 4.0 (CC BY-NC 4.0)**: si può condividere e modificare citando l'autore (Davide Gatti / Survival Hacking) e senza scopi commerciali. Spinnerino Maker segue la stessa licenza.

<https://creativecommons.org/licenses/by-nc/4.0/>

---

## Cosa fa, in breve

1. Si procura un Python che vive tutto dentro questa cartella.
2. Installa lì dentro le due librerie che servono (Pillow e NumPy).
3. Apre una finestra che mostra lo stato: ROM, laserdisc, header, librerie, ffmpeg.
4. Con **Prepara** lancia `prepara.py` e ne segue l'avanzamento, convertitore per convertitore.
5. Se servono i laserdisc, scarica ffmpeg con un clic.
6. A conversione finita copia tutto sulla microSD, formattandola prima se glielo chiedi.
7. Scrive tutto quello che succede in un file di log.

Fa **solo** la preparazione, microSD compresa. Non compila, non programma la scheda, non crea la partizione: quei passi restano quelli del progetto originale (vedi [Dopo la preparazione](#dopo-la-preparazione)).

---

## Come si usa

### 1. Metti i tuoi file

| Cosa | Dove | Note |
|---|---|---|
| ROM MAME, una zip per gioco | `ROMS/` | Col nome che hanno in MAME (`dkong.zip`, `galaga.zip`...). L'elenco delle 62 zip è in `ROMS/LEGGIMI.txt`. |
| Pacchetti Daphne | `VIDEO/` | Facoltativi. Servono `dl_ntsc.zip`, `Space Ace.zip`, `Dragon's Lair 2.zip`. |

Non servono tutte: i giochi di cui manca la zip semplicemente non vengono rigenerati.

Xevious vuole due zip: `xevious.zip` e `xevios.zip`.

### 2. Avvia

**Windows**: doppio clic su `setup.bat`.

**Mac e Linux**: apri il Terminale, scrivi `bash` seguito da uno spazio, trascina `setup.sh` dentro la finestra e premi Invio. (Scritto così funziona anche quando lo zip ha perso il permesso di esecuzione, cosa che succede spesso.)

La prima volta ci vuole un paio di minuti, perché scarica e prepara l'ambiente. Dalla seconda parte subito.

### 3. Premi Prepara

Alla fine la riga di stato dice quanti convertitori sono andati bene, quanti non avevano la zip e quanti hanno dato errore.

---

## La finestra

Usa i controlli del sistema: su Windows sembra un programma di Windows, su Mac uno del Mac. Su Linux usa il tema `clam` di Tk.

### Materiale

Cinque righe, ognuna con un pallino colorato: verde è a posto, giallo manca qualcosa, rosso manca una cosa indispensabile, grigio è facoltativo.

| Riga | Cosa dice | Pulsante |
|---|---|---|
| **ROM MAME** | Quante delle zip attese sono in `ROMS/`. | Apri cartella |
| **Laserdisc** | Quanti dei tre pacchetti Daphne sono in `VIDEO/`. | Apri cartella |
| **ffmpeg** | Se c'è. Diventa rosso solo quando serve, cioè quando hai messo dei pacchetti Daphne. | Installa: lo scarica in `FFMPEG/`. Attivo solo se manca. |
| **Header firmware** | Quanti header sono pronti, sul totale di quelli che i convertitori generano. È il numero che conta davvero. | Cosa manca...: elenca subito, senza lanciare niente, le zip mancanti, quelle che il firmware non conosce, i pacchetti video e gli header da rigenerare gioco per gioco. |
| **Ambiente** | Versione di Python e presenza di Pillow e NumPy. | |

Il tasto **F5** rilegge cartelle e schede.

### Opzioni

- **Salta i laserdisc**: non converte i video. Va spuntata ogni volta che aggiungi una ROM nuova e i video li hai già fatti, perché rifarli può richiedere molto tempo. Parte già spuntata se in `VIDEO/` non c'è nessun pacchetto.
- **Rifai anche i loghi**: di norma non serve. I loghi sono già nel pacchetto e per rifarli occorrono le immagini originali.
- **Solo il gioco**: scrivi un pezzo del nome (per esempio `galaga`) per lanciare soltanto i convertitori che lo contengono.

### I pulsanti

- **Prepara**: lancia la conversione.
- **Ferma**: interrompe il lavoro. Si può rilanciare quando si vuole.
- **File per la SD**, **Firmware**: aprono `DA COPIARE SU SD` e la cartella del firmware.
- **Registro**: apre il file con l'uscita completa dell'ultimo giro.
- **Errori**: compare solo se un convertitore ha dato errore, e apre `prepara_errori.txt`.

### La microSD

La riga **microSD** porta i file convertiti sulla scheda, senza passare da Esplora file o dal Finder.

- **L'elenco** mostra solo i supporti rimovibili, con lettera (o nome), etichetta, capienza e formato. Il disco di sistema non compare mai. Se la scheda non c'è, inseriscila e premi **Cerca**. Se ce n'è una sola, viene scelta da sé.
- **Copia ora** copia subito il contenuto di `DA COPIARE SU SD` nella radice della scheda.
- **Copia sulla scheda a fine conversione**: spuntala prima di premere Prepara e la copia parte da sola quando i convertitori hanno finito. Comodo con i laserdisc, che possono richiedere molto tempo.
- **Formatta prima in FAT32**: formatta la scheda in FAT32, con etichetta `SPINNERINO`, prima di copiare.

La copia è fatta per essere ripetuta: i file che sulla scheda ci sono già, con lo stesso peso e la stessa data, vengono saltati. Aggiungere un gioco non fa ricopiare i GB dei laserdisc. Alla fine tutti i file vengono ricontrollati uno per uno.

Prima di copiare lo strumento controlla che sulla scheda ci sia posto e che nessun file superi i 4 GB, che FAT32 non regge.

#### La formattazione cancella tutto

Per questo ci sono quattro protezioni:

1. si possono scegliere solo supporti rimovibili;
2. una finestra chiede conferma mostrando lettera, etichetta, capienza e quanti file ci sono sopra, e la risposta predefinita è "No";
3. oltre i 256 GB la formattazione viene rifiutata: una microSD da gioco non è così grande, un disco esterno sì;
4. al momento di formattare la scheda viene riguardata: se non è più quella confermata (stesso posto, stessa capienza), lo strumento si ferma senza toccare niente.

Tutte le domande vengono fatte **quando premi Prepara**, non alla fine: così la conversione può girare a lungo a computer incustodito senza restare ferma su una finestra.

Se la scheda non è in FAT32 e non hai chiesto di formattarla, lo strumento ti avvisa: lo Spinnerino in exFAT non la legge.

Limiti da conoscere:

- su Windows alcune versioni non formattano in FAT32 le schede oltre i 32 GB. In quel caso lo strumento lo dice: formatta la scheda con un altro programma e rilancia la copia senza la spunta;
- una scheda nuova mai formattata, o illeggibile, non compare nell'elenco;
- su Linux la scheda deve essere montata (basta aprirla dal gestore dei file) e la formattazione passa da UDisks2: servono i comandi `udisksctl` e `gdbus`, presenti su quasi tutti i desktop. Se mancano lo strumento lo dice e non formatta;
- a copia finita **espelli la scheda in modo sicuro** prima di toglierla: lo strumento non lo fa al posto tuo.

### La tabella e il log

A sinistra c'è una riga per convertitore:

| Stato | Significato |
|---|---|
| `ok` | Ha scritto i suoi file. |
| `ok (avvisi)` | Ha scritto i file ma si è lamentato di qualcosa. Di solito va bene. |
| `senza zip` | Manca la zip di quel gioco. Non è un errore. |
| `niente` | È uscito senza errori ma non ha scritto niente: quasi sempre gli manca un ingresso. Lo strumento lo verifica da sé, guardando gli header prima e dopo: non si fida della riga di `prepara.py`. |
| `ERRORE` | Va guardato. Il dettaglio completo finisce in `prepara_errori.txt`, e compare il pulsante Errori per aprirlo. |

A destra scorre l'uscita di `prepara.py`, a colori. Durante i laserdisc lì si vede l'avanzamento.

---

## Cosa c'è nella cartella

Tutto sta qui dentro. **Cancellata questa cartella, sul computer non resta niente**: nessun programma installato, nessuna voce nel PATH o nel registro, nessuna cache.

```
Spinnerino_Maker/
  setup.bat              avvio su Windows
  setup.sh               avvio su Mac e Linux (lo stesso file per tutti e due)
  spinnerino_tool.py     lo strumento
  README.md              questo file
  prepara.py             il motore originale del progetto, non modificato
  CONVERTITORI/          i convertitori originali (tre correzioni in _ambiente.py, vedi "Cose da sapere")

  ROMS/                  QUI le tue zip MAME
  VIDEO/                 QUI i tuoi pacchetti Daphne
  SPINNERINO/            il firmware: qui dentro arrivano gli header convertiti
  DA COPIARE SU SD/      qui arrivano i file per la microSD

  .venv/                 creata da setup: l'ambiente Python con le librerie
  .python/               creata da setup, solo se serve: il Python portatile
  FFMPEG/                creata da "Installa ffmpeg"
  log/                   un file per ogni giro
```

`prepara.py`, `CONVERTITORI/`, `SPINNERINO/`, `ROMS/`, `VIDEO/` e `DA COPIARE SU SD/` sono una **copia** di quelli della cartella `SOFTWARE` del progetto, fatta perché qui dentro ci sia tutto. Gli originali non sono stati toccati.

Attenzione: gli header convertiti finiscono nel firmware **di questa cartella**. Lo sketch da compilare è quindi `Spinnerino_Maker/SPINNERINO/SPINNERINO.ino`, non quello della cartella sopra.

---

## Come funziona sotto

### L'avvio (`setup.bat` e `setup.sh`)

I due file fanno la stessa cosa, ognuno nella lingua del suo sistema.

1. **Se `.venv` esiste e parte, la usano.** Un ambiente virtuale ricorda il percorso assoluto in cui è nato: se la cartella è stata spostata o rinominata non parte più, e in quel caso viene cancellato e rifatto.
2. **Altrimenti cercano un Python 3.12 o successivo**, provando a eseguirlo e non limitandosi a vedere se il file esiste. Su Windows c'è un finto `python.exe` che apre il Microsoft Store; su Mac, senza gli strumenti di Xcode, `/usr/bin/python3` fa comparire una richiesta di installazione da 2 GB. Tutti e due vengono riconosciuti ed evitati.
3. **Se un Python adatto non c'è, ne scaricano uno portatile** dentro `.python` (circa 45 MB su Windows, 24 MB su Mac, 50-65 MB su Linux). È un archivio che viene solo scompattato: non c'è installatore e non serve la password di amministratore. Su Mac lo scarica anche quando il Python presente non sa aprire una finestra decente (quello di Apple ha un Tk vecchio che disegna male).
4. **Creano `.venv`** e ci installano Pillow e NumPy. Su Debian e Ubuntu il Python di sistema spesso non sa creare un ambiente virtuale (manca `python3-venv`): invece di chiedere un `sudo apt install`, `setup.sh` ripiega da solo sul Python portatile. Tutto questo avviene con la cache di pip disattivata perché non resti niente nella cartella dell'utente.
5. **Su Mac e Linux scelgono chi disegna la finestra.** Chi converte e chi disegna possono essere due Python diversi: le conversioni le fa sempre quello di `.venv` (3.12), la finestra la apre il primo Python del computer il cui Tk funziona davvero, provato aprendo una finestra in un processo a parte. Serve perché su un Mac il Python portatile andava in "segmentation fault" aprendo la finestra, mentre quello di sistema la apriva bene. Se nessuno ci riesce si lavora nel terminale.
6. **Aprono lo strumento.** Su Windows con `pythonw.exe`, così la console nera si chiude da sola.

Se il Python trovato sul computer va bene, `.python` non viene creata: `.venv` si appoggia a quello, senza modificarlo.

### Lo strumento (`spinnerino_tool.py`)

Non riscrive la logica di conversione e non importa `prepara.py`: lo lancia in un processo separato e ne legge l'uscita, riconoscendo le righe che lui stampa per ogni convertitore. Così il comportamento è esattamente quello del progetto originale, e un aggiornamento di `prepara.py` o dei convertitori si porta dentro copiando i file.

Le righe di "Materiale" sono calcolate dallo strumento:

- le zip attese vengono lette da `ROMS/LEGGIMI.txt` e `VIDEO/LEGGIMI.txt`;
- gli header vengono contati su `CONVERTITORI/INDICE_HEADER.txt`, che elenca tutti quelli generati dai convertitori. Un header che comincia con `SEGNAPOSTO` è contato come da rigenerare.

### ffmpeg

Serve solo ai tre laserdisc. Il pulsante lo scarica in `FFMPEG/`, ne estrae `ffmpeg` e `ffprobe`, e poi **lo fa partire** per controllare che funzioni davvero.

- Windows: build di BtbN, circa 190 MB di download.
- Linux: build di BtbN, circa 150 MB, per processori x86_64 e ARM64.
- Mac: build di evermeet.cx. Sono per processori Intel: sui Mac con chip Apple girano tramite Rosetta. Se Rosetta manca lo strumento lo dice, e si installa con `softwareupdate --install-rosetta`.

Se sul computer c'è già un ffmpeg nel PATH, viene usato quello e non si scarica niente.

### I log

Ogni giro scrive `log/prepara_AAAAMMGG_HHMMSS.log` (e `sd_...log` per la microSD, `ffmpeg_...log` per ffmpeg) con data, sistema, versione di Python, comando lanciato e tutta l'uscita. È il file da mandare quando si chiede aiuto.

Gli errori dei convertitori, per intero, sono in `prepara_errori.txt`, che scrive `prepara.py`.

---

## Senza finestra

```
setup.bat --terminale              Windows
bash setup.sh --terminale          Mac e Linux
```

Stesso lavoro, a colori, dentro il terminale. Dopo `--terminale` si possono aggiungere gli argomenti di `prepara.py`:

```
setup.bat --terminale galaga            solo i convertitori di galaga
setup.bat --terminale --senza-video     tutto tranne i laserdisc
setup.bat --terminale --elenco          solo l'elenco, senza toccare niente
```

Lo strumento passa da solo al terminale quando la finestra non si può aprire.

---

## Si possono usare altre ROM MAME?

**No, non aggiungendo zip.** È stato verificato sul codice, e i motivi sono due.

1. **Il firmware non è MAME.** Contiene un emulatore scritto a mano per ogni gioco: nella cartella `SPINNERINO/machines/` ce ne sono 43, più quelli che leggono dalla microSD. Un gioco che non è lì dentro non ha niente che lo faccia girare, qualunque cosa si converta.
2. **Ogni convertitore conosce un solo set.** Dentro c'è scritto il nome della zip e il nome di ogni file al suo interno (per Donkey Kong: `c_5et_g.bin`, `c_5ct_g.bin`, `l_4m_b.bin`...). Una zip con un altro nome non viene nemmeno aperta.

Cosa succede in pratica:

- una zip che il firmware non conosce viene **ignorata**: non fa danni, e "Cosa manca?" la segnala per nome;
- una zip col nome giusto ma di un set diverso (un clone, una revisione) funziona solo se dentro i file hanno gli stessi nomi; altrimenti il convertitore dà errore e dice quale file non trova.

Aggiungere un gioco nuovo significa scrivere l'emulazione di quella scheda nel firmware, il suo convertitore e le voci nell'indice degli header. È lavoro sul progetto, non sullo strumento.

---

## Dopo la preparazione

Questi passi sono quelli del progetto originale e si fanno a mano, come spiegato in `LEGGIMI.txt` e nel `README.md` dello Spinnerino Plus.

1. La microSD: se non l'ha già fatta lo strumento, copia il contenuto di `DA COPIARE SU SD/` nella radice di una scheda formattata FAT32.
2. Programma l'ESP32-C6 con `P4_C6_Programmer`.
3. Crea la partizione con `crea_Partizione_SPINNERINO_30MB`.
4. Apri `Spinnerino_Maker/SPINNERINO/SPINNERINO.ino` con l'Arduino IDE (core esp32 3.3.8, scheda ESP32-P4) e compila.

---

## Cose da sapere

- **La riga "Header firmware" dice la verità anche quando il riepilogo no.** In questo pacchetto manca la cartella `CONVERTITORI/SEGNAPOSTO`. Senza, `prepara.py` conta zero header attesi e alla fine scrive "TUTTO RIGENERATO" anche con le ROM mancanti; inoltre non può mettere i segnaposto, quindi il progetto compila solo quando gli header ci sono tutti. Lo strumento lo segnala in rosso a fine giro. Il numero da guardare è quello di quella riga.
- **Serve Python 3.12 o successivo, e ci pensa il setup.** Con Python 3.10 e precedenti i convertitori che usano `pathlib` muoiono con `TypeError: stat() takes exactly 1 positional argument` oppure `open() argument 'mode' must be str`. La causa è in `CONVERTITORI/_ambiente.py`: aggancia `os.stat` e `io.open`, e il vecchio `pathlib` li chiama in un modo che con l'aggancio non funziona. È successo su un Mac col Python 3.10 di sistema ed è stato riprodotto su Windows. Per questo il setup scarta i Python più vecchi e usa quello portatile; se `.venv` era stata creata con un Python vecchio, la rifà da solo.
- **Gli esiti sono verificati dallo strumento.** `prepara.py` capisce se un convertitore ha scritto guardando gli header elencati in `CONVERTITORI/SEGNAPOSTO`, che in questo pacchetto manca: da solo scriverebbe "NON HA PRODOTTO NIENTE" per ogni convertitore che scrive solo header, anche quando ha lavorato bene. Lo strumento controlla invece gli header uno per uno, prima e dopo ogni convertitore, e mostra il risultato vero sia nella tabella sia nel registro: `ok (N header scritti)`. Anche i conti del riepilogo sono i suoi. Un "NON HA PRODOTTO NIENTE" che resta scritto vuol dire che quel convertitore non ha scritto davvero.
- **Tre correzioni a `CONVERTITORI/_ambiente.py`, solo in questa copia.** È il file che fa girare i convertitori fuori dal PC di chi li ha scritti. Su Mac e Linux aveva tre difetti, emersi alla prima prova su un Mac vero; ogni correzione è marcata nel codice con "Spinnerino Maker". Su Windows il comportamento non cambia.
  1. I percorsi Windows scritti nei convertitori (quelli che cominciano con `C:`) su Mac diventavano cartelle vere: dentro `SPINNERINO/` compariva una cartella `C:` (il Finder la mostra come `C/`) con dentro un albero di cartelle vuote. Ora quei percorsi vengono riconosciuti come estranei, come succede su Windows. Lo strumento toglie da sé le cartelle vuote rimaste dai giri precedenti; se dentro trova un file vero lo lascia e lo segnala.
  2. `romconv_pang.py` moriva con `FileNotFoundError: ''`: chiedeva di creare una cartella dal nome vuoto. Ora la richiesta viene ignorata.
  3. `romconv_mariobros.py` moriva con `FileNotFoundError: '/Us...'`: usava un percorso con dei `..` che Windows risolve sulla carta e Mac no. Ora il percorso viene normalizzato. Questa è la causa più probabile, non una certezza: l'errore completo non era disponibile.
- **Galaga e Galaxian: due convertitori sistemati, solo in questa copia.** Nel pacchetto originale i loro 11 header non li scriveva nessuno, su qualunque sistema, e il firmware non compilava (`galaga_rom1.h: No such file or directory`).
  1. Galaxian: il convertitore c'era, ma si chiamava `galaxian__romconv_galaxian.py` mentre `prepara.py` cerca `romconv_galaxian.py`, quindi non partiva mai. È stato rinominato, senza toccarne il contenuto.
  2. Galaga: `PROVENIENZA.txt` nomina un `romconv_galaga.py` che nel pacchetto non c'è. Ne è stato scritto uno nuovo che scompatta `galaga.zip` e chiama gli attrezzi originali di galagino (cartella `CONVERTITORI/galagino/`) con gli argomenti che loro stessi indicano. Provato solo con ROM finte delle dimensioni giuste: che gli header siano quelli buoni lo dice la scheda.
- **I laserdisc possono richiedere molto tempo**: dipende dal computer. Su un Mac recente sono bastati pochi minuti; su macchine lente molto di più. Vogliono alcuni GB liberi per il passaggio intermedio (`VIDEO/_800x600`, che poi si può cancellare). Si possono interrompere: al giro dopo riprendono dai clip già fatti.
- **Non spostare la cartella a metà lavoro.** Se la sposti, al prossimo avvio `.venv` viene rifatta da sola: è solo un minuto in più.
- **Serve internet la prima volta**, per Python, le librerie ed eventualmente ffmpeg. Dopo no.

### Cosa è stato provato

- **Windows 11**: provato per intero. Avvio su un sistema senza Python (download del Python portatile, `.venv`, librerie), finestra, "Cosa manca?", giro completo di Prepara sui 62 convertitori, installazione di ffmpeg, modalità terminale, cartella con spazi e parentesi nel nome.
- **microSD**: la copia (file saltati se già presenti, controllo finale) è stata provata su una cartella al posto della scheda, e il rifiuto di formattare un disco non rimovibile è stato verificato. **La formattazione vera non è stata provata** su nessuno dei tre sistemi, perché durante lo sviluppo non c'era una scheda inserita. La prima volta usala su una scheda che non contiene niente di importante.
- **Mac**: provato da un utente su un Mac vero, con 62 ROM e i tre laserdisc. L'avvio, la finestra e la conversione dei laserdisc hanno funzionato; cinque convertitori sono falliti per il Python 3.10 di sistema, e al secondo tentativo il Python portatile è andato in crash aprendo la finestra. Entrambe le cose sono state corrette nel setup, e al terzo tentativo finestra e conversioni sono partite. Restavano due convertitori in errore (`pang`, `mariobros`) e la cartella `C:` dentro `SPINNERINO/`: le tre correzioni a `_ambiente.py` sono per questo, e **non sono ancora state riprovate su quel Mac**. La conversione col Python portatile per Mac, l'elenco delle schede, la formattazione e lo scaricamento di ffmpeg su Mac restano non provati.
- **Linux**: `setup.sh` è stato controllato solo nella sintassi e gli indirizzi dei download sono stati verificati. **Non è stato eseguito su un Linux vero**, e lo stesso vale per le parti dello strumento specifiche di Linux (elenco delle schede, formattazione, ffmpeg). Se qualcosa non va, il messaggio nel Terminale e il file in `log/` dicono dove.
- **Conversione con ROM vere**: non provata qui, perché le ROM non fanno parte del pacchetto. Il giro è stato fatto a cartella `ROMS` vuota, che esercita tutto tranne la conversione in sé, la quale resta quella di `prepara.py`.

---

## Se qualcosa non torna

| Sintomo | Cosa fare |
|---|---|
| `setup.bat` dice che non riesce a scaricare Python | Manca internet o un antivirus ha bloccato il download. Riprova; in alternativa installa Python da python.org e rilancia. |
| Su Mac: "permission denied" su `setup.sh` | Lancialo con `bash setup.sh`, come scritto sopra. |
| La finestra non si apre e parte il terminale | Quel Python non ha tkinter, oppure su Linux non c'è uno schermo grafico (ssh). Cancella `.venv` e `.python` e rilancia: `setup.sh` scarica il Python portatile, che tkinter ce l'ha. |
| Su Linux i caratteri della finestra sono sgranati | È il Python portatile, che ha un Tk senza antialiasing. Per l'aspetto migliore installa `python3-tk` e `python3-venv` dal gestore dei pacchetti, cancella `.venv` e `.python` e rilancia. |
| Su Windows non succede niente dopo il doppio clic | Guarda `log/errore_avvio.txt`, oppure lancia `setup.bat --terminale` per leggere l'errore. |
| Un convertitore dice che non trova un file nella zip | La zip è di un set diverso da quello atteso. Serve quel set. |
| `ERRORE` su un convertitore | Apri `prepara_errori.txt`: c'è l'errore intero, percorso compreso. |
| La microSD non compare nell'elenco | Inseriscila e premi "Cerca". Se è nuova o illeggibile, formattala prima dal sistema. |
| "formattazione non riuscita" | Scheda oltre i 32 GB su Windows: formattala in FAT32 con un altro programma e rilancia la copia senza la spunta. |
| `TypeError: stat() takes exactly 1 positional argument` | Python troppo vecchio. Chiudi e rilancia il setup aggiornato: rifà `.venv` con Python 3.12. |
| Su Mac: `segmentation fault` all'apertura | Il Tk di quel Python è rotto. Il setup aggiornato lo prova prima e ne sceglie un altro; se serve, `bash setup.sh --terminale` lavora senza finestra. |
| Vuoi ripartire da zero | Cancella `.venv` e `.python` e rilancia il setup. |
