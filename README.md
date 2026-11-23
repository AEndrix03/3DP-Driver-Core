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

### Movimenti binari

- I movimenti partono in formato binario, in step: la conversione da millimetri (con il resto
  frazionario dell'estrusore) avviene qui. Formato in `include/core/MoveFrame.hpp` e in
  `PROTOCOL.md` del firmware.
- Durante una stampa fino a 4 movimenti restano in volo senza attendere la risposta
  (`CommandExecutor::submitMove`). Un comando di testo attende sempre che siano stati tutti
  confermati.
- Dopo un frame corrotto, perso o una risposta persa, i movimenti vengono rispediti dal primo
  che il firmware non ha accettato.
- `MotionCommands` tiene una copia della posizione del firmware in step, riletta con `M115`
  ogni volta che `DriverInterface::positionEpoch()` cambia.
- Con `DRIVER_SERIAL_TRACE=1` i frame compaiono decodificati nella traccia (`MOVE #12 X16 Y-4 E1`).

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
