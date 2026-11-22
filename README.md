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

## Test di integrazione con il firmware (senza hardware)

```bash
tests/integration/run.sh [percorso del repo 3D-Printer-Firmware]
```

Compila il firmware reale su PC (scheda simulata) insieme al core del driver e verifica il
collegamento iniettando guasti: risposte perse, comandi corrotti, firmware bloccato, riavvio a
meta' movimento, abort, protezioni termiche, rollover degli orologi, numeri comando oltre 16 bit,
traduzione di rette/archi/spline. Serve solo `g++` (C++17): niente Boost, Kafka o vcpkg.
Di default il repo del firmware viene cercato in `../3D-Printer-Firmware`.

Il protocollo seriale e' documentato in `PROTOCOL.md` nel repo del firmware.
