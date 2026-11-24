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
- **Pagina di gestione**: `http://127.0.0.1:7333` (stato, temperatura, avanzamento, movimento
  manuale, pausa, ripresa, annulla, arresto d'emergenza, caricamento di un G-code, storico).
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
| `3dp check file.gcode` | cosa farebbe il driver con il file, senza stampante: comandi saltati o incompatibili, ingombro, strati, filamento |
| `3dp slice modello.stl [layer=0.2 infill=20 temp=205 perimeters=2 speed=45 supports=0 brim=0]` | affetta il modello con PrusaSlicer e controlla il risultato, senza stampante |
| `3dp pause` / `resume` / `cancel` / `clear` | controllo della stampa, azzeramento errore |
| `3dp estop` | arresto di emergenza: ferma i motori e spegne l'heater |

### Slicer

Lo slicing usa **PrusaSlicer** da riga di comando con il profilo della macchina,
`config/slicer/3dp-prusaslicer.ini` (ugello 0,4 mm, filamento 1,75 mm, volume 220 x 220 x 200,
nessun piano riscaldato, archi veri `G2`/`G3`, G-code di avvio con homing e linea di spurgo).

- Installazione: `sudo apt install prusa-slicer` (Ubuntu) oppure `sudo pacman -S prusa-slicer`
  (Arch). Se l'eseguibile non e' nel PATH: variabile `THREEDP_SLICER`. Profilo diverso:
  `THREEDP_SLICER_PROFILE`.
- Dalla pagina basta scegliere un `.stl`: viene affettato, controllato e, dopo conferma, stampato.
  Nello storico restano il modello di partenza e i parametri usati.
- Una stampa non parte se il file non supera il controllo (esce dal volume, usa un secondo
  estrusore, pollici, ...); `force=1` per insistere.
- Cosa fa il driver con ogni comando G-code e' in `src/translator/GCodeSupport.cpp`: un solo
  elenco, usato sia dall'esecuzione sia dal controllo. Lo scenario e2e "sliced file from
  PrusaSlicer" stampa in simulazione un file prodotto davvero con questo profilo
  (`tests/fixtures/prusaslicer_ring.gcode`).
- I valori del profilo segnati "VERIFICARE" (ritrazione, velocita') dipendono dalla meccanica.

Le API (`GET /api/status`, `GET /api/history`, `POST /api/<comando>?chiave=valore`) ascoltano
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
- `M204 P<stampa> T<spostamento>` (o `S` per entrambe) limita l'accelerazione, mai oltre quella
  massima del firmware.

### Movimenti binari

- I movimenti partono in formato binario, in step e gia' pianificati: la conversione da
  millimetri (con il resto frazionario dell'estrusore) avviene qui. Formato in
  `include/core/MoveFrame.hpp` e in `PROTOCOL.md` del firmware.
- Fino a 4 movimenti (56 byte) restano in volo senza attendere la risposta
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
