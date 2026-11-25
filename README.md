# 3DP-Driver-Core

Core driver engine for modular 3D printing systems.

## Features
- Modular architecture (Driver Core, Comm Interface, App)
- Future-proof (WebSocket, MQTT, gRPC communication ready)
- CMake powered
- MIT Licensed

## Build
```bash
mkdir build && cd build
cmake ..
make
```

## Test end-to-end con il firmware (senza hardware)

```bash
tests/integration/run.sh [percorso del repo 3D-Printer-Firmware]
ONLY="travel limits" tests/integration/run.sh   # solo gli scenari il cui nome contiene il testo
```

Compila il firmware reale su PC (scheda simulata: seriale, pin, timer degli step, finecorsa,
termistore) insieme al core del driver e li fa lavorare insieme. Serve solo `g++` (C++17): niente
Boost, Kafka o vcpkg. Di default il repo del firmware viene cercato in `../3D-Printer-Firmware`.
Gli stessi test girano in CI a ogni push su entrambi i repository (`.github/workflows/e2e.yml`).

Cosa verificano:

- **Protocollo**: risposte perse, comandi corrotti o persi, righe spurie, risposte stantie,
  timeout (mai trattati come successo), riavvio del firmware, risincronizzazione, numeri comando
  oltre i 16 bit.
- **Movimento**: rette esatte passo per passo, corse lunghe, estrusione senza deriva, lookahead
  (segmenti collineari fusi, velocita' negli spigoli in base all'angolo), accelerazione mai oltre
  il limite, arresto controllato con posizione esatta.
- **Homing e finecorsa**: zero sui finecorsa, homing per asse, finecorsa urtato in stampa,
  finecorsa che non scatta.
- **Termica**: regolazione, sensore assente o scollegato, mancato riscaldamento, estrusione a
  freddo, host sparito con heater acceso.
- **Lunga durata**: 19 ore di inattivita', rollover di `millis()` e `micros()`, watchdog mai lasciato
  senza reset.
- **G-code**: rette, archi, spline G5, assoluto/relativo, G92, commenti.
- **Job completi** (da file, attraverso `PrintJobManager`): stampa intera, pausa con parcheggio e
  ripresa identica a una stampa mai interrotta, annullamento, ripresa dopo un guasto di
  comunicazione (anche a driver riavviato) e dopo un riavvio del firmware.

Cosa NON possono verificare (resta alla prova sulla macchina): tempi reali dell'ATmega328P
(vedi `S11`/`PRF` nel protocollo), porta seriale reale (`RealSerialPort`), Kafka, elettronica,
termistore e meccanica.

## Ripresa di una stampa interrotta

Durante un job il driver salva in `checkpoints/<jobId>.ckpt` la prima riga di G-code non ancora
eseguita fisicamente (segue il campo `D` del firmware, non l'ultimo comando inviato) e lo stato
della macchina all'inizio di quella riga: posizione, E, velocita', modalita', temperatura, ventola.

- **Pausa** (`PrintJobManager::pauseJob`): la coda si ferma, il firmware completa i pochi segmenti
  gia' accodati, il filamento viene ritratto e l'ugello sollevato di 5 mm. `resumeJob` torna
  esattamente al punto di pausa.
- **Guasto** (timeout, riavvio del firmware, macchina in errore): la coda viene svuotata e il job
  passa a FAILED, ma il checkpoint resta. `recoverJob()` (o `resumeJob()`) ripristina lo stato,
  torna al punto di interruzione e prosegue; funziona anche dopo un riavvio del driver.
- Se il firmware si e' riavviato ha perso la posizione: X e Y vengono riazzerati sui finecorsa
  (che devono essere abilitati), Z viene assunto fermo dove era. Senza finecorsa la ripresa viene
  rifiutata.
- Viene ristampata al massimo la riga interrotta.

## Versione locale: `3dp`

Un solo programma al posto di backend, frontend e Kafka, per usare la stampante dal computer
a cui e' collegata. Il resto dell'infrastruttura resta disponibile: questa e' un'alternativa.

```
tools/build_local.sh                       # servono solo g++ e SQLite (libsqlite3-dev / sqlite)
build-local/3dp serve --port /dev/ttyUSB0  # su Windows: --port COM3
```

- **Servizio**: tiene la porta seriale, esegue le stampe, salva lo storico in
  `3dp-data/history.sqlite` e i G-code in `3dp-data/gcode/`.
- **Pagina di gestione**: `http://127.0.0.1:7333`, quattro schede.
  - *Controllo*: la stampante in 3D con hotend e ugello, il percorso e la durata prevista; sotto,
    i valori letti dalla macchina (posizione eseguita, velocita', coda del firmware, movimenti al
    secondo, ritrasmissioni) e la traccia della seriale. A lato: lavoro in corso, file, ugello,
    movimento manuale.
  - *Test*: prove di movimento eseguite dal driver (vedi "Test").
  - *Configurazione*: i valori scelti dall'operatore, salvati in `3dp-data/config.ini`.
  - *Storico*: i lavori fatti, con gli errori e gli avvisi capitati durante ciascuno.
- **Modalita'**, indicata in alto: *Simulazione* (stampante simulata), *Pronta* (macchina vera
  ferma: comandi liberi e test), *In stampa* (macchina vera al lavoro: si osserva; restano
  pausa, ripresa, annulla, arresto e le regolazioni di velocita'; movimenti manuali e test sono
  spenti, e il servizio li rifiuta comunque).
- **Riga di comando**: lo stesso eseguibile e' il client delle stesse API, e risponde in JSON.
  E' il modo in cui uno script o un agente pilota la stampante.

| Comando | Effetto |
|---------|---------|
| `3dp status` / `history` / `diag` | stato, storico, diagnostica del firmware (`S11`) |
| `3dp home [XYZ]` | homing |
| `3dp jog x=10 y=-5 z=1 e=2 f=3000` | movimento relativo in mm |
| `3dp temp 200` / `3dp fan 50` | ugello (gradi), ventola (percento) |
| `3dp gcode "G1 X10 F3000"` | una riga di G-code; durante una stampa passa davanti al job |
| `3dp upload file.gcode` / `3dp print file.gcode` | salva nello spazio del servizio / avvia la stampa |
| `3dp check file.gcode` | cosa farebbe il driver con il file, senza stampante: comandi saltati o incompatibili, ingombro, strati, filamento, durata prevista (`estimatedSeconds`) |
| `3dp slice modello.stl [layer=0.2 infill=20 temp=205 perimeters=2 speed=45 supports=0 brim=0]` | affetta il modello con PrusaSlicer e controlla il risultato, senza stampante |
| `3dp config [chiave=valore ...]` | mostra o cambia la configurazione |
| `3dp test kind=shuttle axis=x distance=50 feed=3000 repeats=5` | prova di movimento (vedi "Test") |
| `3dp endstops` | stato dei finecorsa |
| `3dp pause` / `resume` / `cancel` / `clear` | controllo della stampa, azzeramento errore |
| `3dp estop` | arresto di emergenza: ferma i motori e spegne l'heater |

### Anteprima e durata prevista

Scelto un file, la pagina ne disegna il percorso (trascinare per ruotare, rotella per lo zoom,
Maiusc o tasto destro per spostare, doppio clic per reinquadrare; cursore a destra per fermarsi
a uno strato) e indica quanto durera' la stampa. Il tasto di riproduzione fa scorrere la stampa
a velocita' accelerata.

- La durata non e' una stima a velocita' costante: `GET /api/preview?file=...` fa passare il
  file per lo stesso pianificatore usato in stampa (`src/translator/GCodePreview.cpp`), con i
  limiti letti dal firmware e le regolazioni in uso, e somma la durata di ogni blocco.
- Verifica: lo scenario e2e "preview and time estimate" esegue lo stesso file sul firmware
  simulato e richiede che la stima stia entro il 5% del tempo impiegato dal generatore di step.
  Un pezzo vero (3DBenchy, 94.000 comandi) e' stato stampato per intero sulla macchina simulata.
- Restano fuori riscaldamento e homing, che non dipendono dal percorso: la pagina lo segnala.
- Il disegno della macchina (ponte mobile, hotend) e' una rappresentazione generica: non
  descrive la meccanica reale.

### Cosa viene dalla macchina e cosa no

Durante un lavoro la testina disegnata e la parte colorata del percorso seguono la **posizione
eseguita**, non le righe lette dal file. Viene dalle conferme del firmware: ogni risposta dice
fino a quale comando e' arrivata l'esecuzione, quindi posizione eseguita = posizione dell'ultimo
movimento inviato meno i movimenti inviati e non ancora completati (`src/local/WirePort.cpp`,
`GET /api/live`). La precisione e' di un movimento: dentro un movimento la pagina interpola.
Velocita' = quella nominale del movimento in esecuzione, non una misura.

La stessa "spia" sulla seriale tiene la traccia degli ultimi scambi: per ogni frame binario i
byte e la traduzione (`MOVE #12 X160 Y-8 E5 V60 A1000 S..J..`), per ogni risposta il testo o
`ACK #12, 3 da completare`.

### Prova a secco

Per provare meccanica e motori con un pezzo vero, prima che ci sia un hotend o senza consumare
filamento: nella scheda Controllo si sceglie "A secco" prima di avviare, oppure
`3dp print file.gcode dry=motion`. Il servizio stampa una copia del file (`*.secco.gcode`, accanto
all'originale nello spazio dei dati):

- `dry=motion`: tolti riscaldamento ed estrusione, si muovono solo X, Y e Z;
- `dry=heat`: tolto solo il riscaldamento, l'estrusore gira (da non usare con filamento caricato
  e ugello freddo; se il termistore e' attivo il firmware rifiuta comunque l'estrusione a freddo).

Percorso, velocita' e accelerazioni sono quelli della stampa vera. Senza termistore una stampa
normale si ferma subito (`NO_SENSOR`): e' la prova a secco quella da usare.

### Regolare la velocita'

Due leve, nella scheda Controllo e in Configurazione, modificabili anche a lavoro in corso:

- **Velocita' del lavoro (%)**: moltiplica le velocita' chieste dal G-code (non le ritrazioni).
  E' quella da usare se la meccanica non regge.
- **Movimenti al secondo**: tetto ai blocchi inviati alla scheda; il massimo, 678, e' quello
  misurato sul collegamento. Il pianificatore ne tiene conto (un segmento non viene percorso in
  meno del tempo che serve a inviarlo), quindi con un tetto basso i tratti corti rallentano in
  modo regolare invece di svuotare la coda del firmware.

### Configurazione

`3dp-data/config.ini`, righe `chiave=valore`; si modifica dalla pagina o con
`3dp config chiave=valore ...` (`3dp config` da solo la mostra). Il driver la legge all'avvio e a
ogni modifica. Contiene le scelte dell'operatore: velocita', accelerazioni richieste, linear
advance, tetti piu' bassi di quelli del firmware per velocita'/accelerazione/giunzione, volume
usato nei controlli, guadagni PID da inviare alla scheda, slicer.

I valori che proteggono la macchina (step/mm, velocita' e accelerazioni massime, corsa, coda)
restano nel firmware: il driver li legge con `M115`, la pagina li mostra in sola lettura, e i
tetti della configurazione possono solo abbassarli. Le voci che cambiano i limiti si possono
modificare solo a macchina ferma.

### Test

Scheda Test, o `3dp test kind=... [axis= distance= feed= feed2= repeats= segment=]`. Ogni test
genera un piccolo G-code di movimenti relativi alla posizione attuale e lo esegue come un lavoro:
si vede nel visualizzatore, si puo' mettere in pausa o annullare, resta nello storico con i suoi
errori. Prima di partire il servizio controlla che ci sia corsa sufficiente.

| `kind` | Cosa fa |
|--------|---------|
| `shuttle` | un asse avanti e indietro |
| `ladder` | la stessa corsa a velocita' crescenti, da `feed` a `feed2` |
| `square`, `circle` | quadrato e cerchio (arco `G2`) su X e Y |
| `stream` | migliaia di segmenti corti: mette sotto sforzo collegamento e coda |
| `extrude` | spinge una lunghezza nota di filamento |

`3dp endstops` legge i finecorsa, `3dp diag` la diagnostica del firmware. I test dicono cosa ha
fatto il collegamento (movimenti, ritrasmissioni, errori); se la meccanica ha perso passi lo
si vede e lo si sente, il driver non lo misura.

### Stampante simulata

```
tools/build_local.sh --sim                 # compila anche il firmware per il PC (serve il suo repo accanto)
build-local/3dp serve --sim --speed 12     # nessuna scheda: http://127.0.0.1:7333
```

Al posto della scheda c'e' il firmware vero, compilato per il PC con la scheda finta dei test
(`test/native` nel repo del firmware): stesso protocollo, stesso generatore di step, finecorsa
e ugello come modelli. Serve a provare pagina, file e tempi senza hardware. `--speed` e' quante
volte piu' in fretta del reale scorre il tempo della macchina (si cambia anche dalla pagina).
I dati vanno in `3dp-data-sim/`, separati da quelli della macchina vera. Non dice nulla su
meccanica, motori e termica reali.

### Slicer

Lo slicing usa **PrusaSlicer** da riga di comando con il profilo della macchina,
`config/slicer/3dp-prusaslicer.ini` (ugello 0,4 mm, filamento 1,75 mm, volume 220 x 220 x 200,
nessun piano riscaldato, archi veri `G2`/`G3`, G-code di avvio con homing e linea di spurgo).

- Installazione: `sudo apt install prusa-slicer` (Ubuntu) oppure `sudo pacman -S prusa-slicer`
  (Arch). Se l'eseguibile non e' nel PATH: variabile `THREEDP_SLICER`. Profilo diverso:
  `THREEDP_SLICER_PROFILE`.
- Dalla pagina basta scegliere un `.stl`: viene affettato, controllato e mostrato in anteprima;
  la stampa parte con "Avvia stampa".
  Nello storico restano il modello di partenza e i parametri usati.
- Una stampa non parte se il file non supera il controllo (esce dal volume, usa un secondo
  estrusore, pollici, ...); `force=1` per insistere.
- Cosa fa il driver con ogni comando G-code e' in `src/translator/GCodeSupport.cpp`: un solo
  elenco, usato sia dall'esecuzione sia dal controllo. Lo scenario e2e "sliced file from
  PrusaSlicer" stampa in simulazione un file prodotto davvero con questo profilo
  (`tests/fixtures/prusaslicer_ring.gcode`).
- I valori del profilo segnati "VERIFICARE" (ritrazione, velocita') dipendono dalla meccanica.

Le API (`GET /api/status`, `/api/live`, `/api/history`, `/api/preview`, `/api/config`, `POST /api/<comando>?chiave=valore`) ascoltano
solo su `127.0.0.1` e non hanno autenticazione: muovono una macchina, non vanno esposte in rete.
La porta seriale e' in `src/local/NativeSerialPort.cpp` (Win32 e termios, nessuna libreria).

### Pianificazione dei movimenti e linear advance

Il piano di velocita' si calcola qui, non sul firmware (`core::planning::MotionPlanner`,
`include/core/motion/MotionPlanner.hpp`). La divisione dei compiti e' descritta in
`PROTOCOL.md` del firmware ("Chi pianifica cosa"); in breve:

- Per ogni segmento: velocita' nominale e accelerazione entro i limiti di ogni asse, velocita'
  alle giunzioni, passate avanti e indietro. I limiti della macchina non sono copiati qui: si
  leggono dal firmware con `M115` insieme alla posizione.
- Un movimento di una stampa (`pipelined`) entra nel piano ed esce quando sono noti abbastanza
  movimenti successivi; quelli ancora in attesa partono al primo comando non di movimento, a
  fine job e quando la coda dei comandi resta vuota. Un movimento singolo (jog) e' pianificato
  da solo, da fermo a fermo, e parte subito.
- Il firmware resta l'ultima parola sulla sicurezza: si ferma comunque entro i blocchi che ha
  in coda, parte sempre da fermo, rifiuta i movimenti fuori volume. Lo scenario e2e "stop within
  queued moves" lo verifica con un host che dichiara velocita' impossibili e poi tace.
- **Linear advance** (`M900 K<secondi>`, spento di default): l'estrusore viene tenuto "avanti"
  di K volte la sua velocita', quindi spinge di piu' mentre si accelera e di meno mentre si
  frena. Un segmento estruso lungo diventa tre blocchi (accelerazione, velocita' costante,
  frenata), ognuno con il suo rapporto tra estrusore e percorso; il filamento totale non
  cambia. Con K alto l'accelerazione dei tratti estrusi viene ridotta perche' il salto di
  velocita' dell'estrusore resti entro 5 mm/s. **K va tarato sulla macchina** (tipicamente
  0,03-0,1 con estrusore diretto, 0,3-0,8 con bowden): senza hotend non e' stato possibile.
- Dalla versione locale K si imposta anche con `3dp advance <K>` o dalla pagina, e vale dai
  movimenti successivi.
- `M204 P<stampa> T<spostamento>` (o `S` per entrambe) limita l'accelerazione, mai oltre quella
  massima del firmware.

### Porta seriale

250000 baud, 8 bit, 2 bit di stop: e' l'unica velocita' alta che il quarzo della scheda genera
esatta, e il secondo bit di stop da' margine all'adattatore USB (misure in `PROTOCOL.md` del
firmware, "Velocita' della linea"). Su Linux 250000 si imposta con `termios2`
(`src/core/serial/LinuxBaud.cpp`); questa parte e' compilata dalla CI ma non e' stata provata
su una scheda collegata a Linux.

Su Windows, con le schede clone che montano un CH340 non originale: il driver WCH 3.8/3.9
rifiuta la configurazione della porta (`cannot configure COMx (error 31)`); funziona il
3.5.2019.1. Windows Update tende a rimettere quello nuovo: `pnputil /enum-drivers` mostra quale
pacchetto `ch341ser.inf` e' installato, `pnputil /delete-driver oemNN.inf /uninstall /force`
(da amministratore) toglie quello sbagliato.

### Movimenti binari

- I movimenti partono in formato binario, in step e gia' pianificati: la conversione da
  millimetri (con il resto frazionario dell'estrusore) avviene qui. Formato in
  `include/core/MoveFrame.hpp` e in `PROTOCOL.md` del firmware.
- Fino a 8 movimenti restano in volo senza attendere la risposta, mai piu' byte di quanti ne
  tiene il buffer di ricezione del firmware (letto con `M115`). Un movimento accettato viene
  confermato con 5 byte invece di una riga di testo
  (`CommandExecutor::submitMove`). Un comando di testo attende sempre che siano stati tutti
  confermati.
- Dopo un frame corrotto, perso o una risposta persa, i movimenti vengono rispediti dal primo
  che il firmware non ha accettato.
- `MotionCommands` tiene una copia della posizione del firmware in step, riletta con `M115`
  ogni volta che `DriverInterface::positionEpoch()` cambia. Un movimento fuori dal volume di
  lavoro viene rifiutato gia' qui, intero, prima di essere spezzato in blocchi.
- Con `DRIVER_SERIAL_TRACE=1` i frame compaiono decodificati nella traccia
  (`MOVE #12 X16 Y-4 E1 S80.000 J12.500`).

### Coda dei comandi e log

- Un job viene letto dal file G-code **riga per riga mentre viene eseguito**: in memoria c'e' solo la
  riga corrente, qualunque sia la dimensione del file, e l'ordine di esecuzione e' quello del file.
- La richiesta di avanzamento non interroga il firmware: usa la posizione tenuta dal traduttore.
- Le righe inviate e ricevute sulla seriale non vengono scritte nel log (in stampa sono centinaia al
  secondo). Per vederle: variabile d'ambiente `DRIVER_SERIAL_TRACE=1`.

### Comandi dal backend (Kafka)

| Topic | Effetto |
|-------|---------|
| `printer-start-request` | avvia il job; `jobId` (id del job sul backend) diventa l'id del job anche sul driver, `endGCode` viene eseguito dopo l'ultima riga |
| `printer-pause-request` | pausa con parcheggio |
| `printer-resume-request` | riprende il job in pausa oppure, se interrotto, lo fa ripartire dal checkpoint di `jobId` |
| `printer-stop-request` | annulla il job (arresto controllato, heater spento, checkpoint eliminato) |

Il protocollo seriale e' documentato in `PROTOCOL.md` nel repo del firmware.
