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

### Comandi dal backend (Kafka)

| Topic | Effetto |
|-------|---------|
| `printer-start-request` | avvia il job; `jobId` (id del job sul backend) diventa l'id del job anche sul driver, `endGCode` viene eseguito dopo l'ultima riga |
| `printer-pause-request` | pausa con parcheggio |
| `printer-resume-request` | riprende il job in pausa oppure, se interrotto, lo fa ripartire dal checkpoint di `jobId` |
| `printer-stop-request` | annulla il job (arresto controllato, heater spento, checkpoint eliminato) |

Il protocollo seriale e' documentato in `PROTOCOL.md` nel repo del firmware.
