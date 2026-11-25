# Usare `3dp` su Linux

Tutto quello che serve per compilare il servizio locale, collegare la scheda e stampare da una
macchina Linux (PC, mini PC, Raspberry Pi). I comandi sono per Debian/Ubuntu/Raspberry Pi OS; su
altre distribuzioni cambiano solo i nomi dei pacchetti.

## Cosa e' verificato e cosa no

| | Stato |
|---|---|
| Compilazione del servizio `3dp` e del core del driver | verificata a ogni push dalla CI (Ubuntu) |
| Suite end-to-end contro il firmware compilato per PC | verificata dalla CI |
| `3dp serve --sim` (stampante simulata), homing e una misura | verificata dalla CI |
| Sorgenti del driver completo (Kafka, Boost.Asio): controllo di sintassi | verificato dalla CI; mai collegato ed eseguito |
| Seriale a 250000 baud contro una scheda vera | **mai provata su Linux** (vedi "Se la scheda non risponde") |
| Ricerca e lancio di PrusaSlicer | **mai provati su Linux** |
| Architettura ARM (Raspberry Pi) | **mai provata**: il codice non ha nulla di specifico per x86, ma nessuno l'ha compilato li' |

## 1. Pacchetti

```bash
sudo apt-get update
sudo apt-get install -y git g++ libsqlite3-dev
```

Serve un `g++` con C++17 (dalla versione 9). Nient'altro: niente CMake, Boost o Kafka per il
servizio locale.

## 2. Sorgenti e compilazione

I due repository vanno affiancati nella stessa cartella: il driver usa i sorgenti del firmware per
la stampante simulata e per i test.

```bash
mkdir 3dp && cd 3dp
git clone https://github.com/AEndrix03/3DP-Driver-Core.git
git clone https://github.com/AEndrix03/3D-Printer-Firmware.git
cd 3DP-Driver-Core
tools/build_local.sh --sim        # produce build-local/3dp, con dentro la stampante simulata
```

Senza `--sim` si ottiene lo stesso programma senza stampante simulata (non servono i sorgenti del
firmware). La prima compilazione richiede qualche minuto; su un Raspberry Pi di piu'.

Per controllare che firmware e driver si parlino, senza hardware:

```bash
tests/integration/run.sh          # circa 600 controlli, deve finire con "0 failures"
```

## 3. Prima prova, senza scheda

```bash
build-local/3dp serve --sim --speed 12
```

Poi `http://127.0.0.1:7333` nel browser della stessa macchina. E' il firmware vero compilato per il
PC: homing, stampa, misure e stress test funzionano come sulla macchina reale. Se questo passo
funziona, programma e pagina sono a posto e resta solo il collegamento.

## 4. Collegare la scheda

Collegata la scheda via USB:

```bash
sudo dmesg | tail -n 5            # deve comparire "ch341-uart converter now attached to ttyUSB0"
ls -l /dev/ttyUSB*
```

Il driver del CH340 e' nel kernel: non c'e' nulla da installare, e non esiste il problema degli
aggiornamenti che su Windows rimettono il driver sbagliato.

**Permessi.** La porta appartiene al gruppo `dialout` (`uucp` su Arch):

```bash
sudo usermod -aG dialout "$USER"  # poi uscire e rientrare nella sessione
```

**Due programmi che disturbano il CH340.**

- `brltty` (lettore braille, installato di serie su Ubuntu): si prende il dispositivo, e
  `/dev/ttyUSB0` sparisce un secondo dopo essere comparso. Se in `dmesg` si legge
  `usbfs: interface 0 claimed by ch341 while 'brltty' sets config`: `sudo apt-get remove brltty`.
- `ModemManager`: all'inserimento interroga la porta come se fosse un modem, e la scheda riceve
  caratteri a caso. La regola qui sotto gli dice di ignorarla.

**Nome fisso e regola udev.** `ttyUSB0` puo' diventare `ttyUSB1` se si collega altro. Questa regola
crea `/dev/3dp`, sempre uguale, e tiene lontano ModemManager:

```bash
sudo tee /etc/udev/rules.d/99-3dp.rules >/dev/null <<'EOF'
SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", SYMLINK+="3dp", GROUP="dialout", MODE="0660", ENV{ID_MM_DEVICE_IGNORE}="1"
EOF
sudo udevadm control --reload-rules && sudo udevadm trigger
```

`1a86:7523` e' il CH340; con un adattatore diverso i due numeri si leggono con `lsusb`.

## 5. Avvio

```bash
build-local/3dp serve --port /dev/3dp      # oppure /dev/ttyUSB0
```

Deve stampare `3dp: printer on /dev/3dp, page and API on http://127.0.0.1:7333`. All'avvio il
servizio legge dalla scheda limiti, coda e corsa: se non li riceve si ferma e dice perche'.

Controlli, da un altro terminale:

```bash
build-local/3dp status            # "connected":true
build-local/3dp config            # in fondo, "firmware": queueBlocks 15, receiveBuffer 128
build-local/3dp diag              # diagnostica della scheda
```

I dati (storico, configurazione, G-code) stanno in `3dp-data/` nella cartella da cui si lancia il
servizio; `--data <cartella>` la sposta, `--http <porta>` cambia la porta.

### Se la scheda non risponde

| Messaggio | Causa probabile |
|---|---|
| `cannot open /dev/ttyUSB0: Permission denied` | utente non nel gruppo `dialout`, o sessione non riavviata |
| `cannot open ...: No such file or directory` | scheda non vista: `dmesg`, cavo, `brltty` |
| risposte rovinate, ritrasmissioni continue | un altro programma legge la stessa porta (un secondo `3dp`, un monitor seriale, ModemManager): Linux lo permette, e i caratteri si dividono tra i due |
| `cannot set 250000 baud: ...` | il kernel o l'adattatore rifiutano la velocita' |
| `the firmware did not answer on the serial port` | porta aperta ma nessun testo leggibile: velocita' sbagliata o firmware non caricato |
| `the firmware did not report its limits` | sulla scheda c'e' un firmware precedente: ricaricarlo |

Il collegamento e' a **250000 baud, 8 bit, 2 bit di stop**. 250000 non e' una velocita' standard di
POSIX: si imposta con `termios2` (`src/core/serial/LinuxBaud.cpp`). E' la parte mai provata su una
scheda vera. Per isolarla, con la scheda collegata e il servizio fermo:

```bash
python3 -m pip install pyserial
python3 -c "
import serial, time
p = serial.Serial('/dev/ttyUSB0', 250000, stopbits=2, timeout=3)
time.sleep(2); print(p.read(200))"
```

Se qui si legge `Avvio firmware 3DP...` e `Sistema pronto.`, adattatore e kernel reggono i 250000
baud e un eventuale problema e' nel codice di `3dp`; se si leggono caratteri a caso, il problema
e' nell'adattatore.

## 6. Slicer (facoltativo)

Serve solo per affettare gli STL dalla pagina; i G-code gia' pronti non ne hanno bisogno.

```bash
sudo apt-get install -y prusa-slicer      # oppure l'AppImage dal sito di Prusa
```

Il servizio cerca `prusa-slicer` nel `PATH`. Con l'AppImage o un'installazione altrove, il percorso
dell'eseguibile va nella voce *Eseguibile di PrusaSlicer* della scheda Configurazione (o
`3dp config slicer_path=/percorso/PrusaSlicer.AppImage`). All'avvio `3dp serve` scrive quale slicer
ha trovato. Su una macchina senza schermo conviene affettare altrove e caricare il G-code.

## 7. Usarlo da un altro computer

Il servizio ascolta solo su `127.0.0.1` e **non ha autenticazione**: chi lo raggiunge muove la
macchina e accende l'heater. Non va esposto sulla rete. Per usarlo da un altro PC si passa da SSH:

```bash
ssh -L 7333:127.0.0.1:7333 utente@macchina-della-stampante
```

e sul proprio PC si apre `http://127.0.0.1:7333`.

## 8. Avvio automatico (facoltativo)

```bash
mkdir -p ~/.config/systemd/user
cat > ~/.config/systemd/user/3dp.service <<EOF
[Unit]
Description=3dp, servizio locale della stampante

[Service]
WorkingDirectory=$HOME/3dp/3DP-Driver-Core
ExecStart=$HOME/3dp/3DP-Driver-Core/build-local/3dp serve --port /dev/3dp
Restart=on-failure
RestartSec=5

[Install]
WantedBy=default.target
EOF
systemctl --user daemon-reload
systemctl --user enable --now 3dp
loginctl enable-linger "$USER"     # parte all'accensione, anche senza login
journalctl --user -u 3dp -f        # cosa sta scrivendo
```

Attenzione: aprire la porta seriale riavvia la scheda. Un riavvio del servizio a stampa in corso la
interrompe (la stampa si puo' poi riprendere: vedi "Ripresa di una stampa interrotta" nel README).

## 9. Caricare il firmware da Linux

```bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=~/.local/bin sh
arduino-cli core install arduino:avr
cd ../3D-Printer-Firmware
tools/check_budget.sh             # compila per Uno e controlla flash e RAM
arduino-cli upload -p /dev/ttyUSB0 --fqbn arduino:avr:uno --input-dir build/budget
```

Il servizio `3dp` deve essere fermo: la porta e' una sola. Il caricamento usa il bootloader a
115200 baud, che e' una velocita' standard.

## 10. Prima accensione con la meccanica

Nell'ordine, dalla scheda Test della pagina (o `3dp measure ...`, vedi "Misure automatiche" nel
README):

1. *Finecorsa* → *Leggi*, premendo a mano ogni interruttore: deve cambiare quello giusto.
2. Homing, con una mano sull'arresto.
3. *Ripetibilita' del finecorsa* su X, Y e Z.
4. *Corsa dell'asse*, a bassa velocita'.
5. *Velocita' massima* e *Accelerazione massima*; applicare i valori proposti se ne escono.
6. *Stress test* di dieci minuti.
7. *Temperatura*: sensore a riposo, poi riscaldamento; applicare il PID proposto e ripetere.

I finecorsa vanno prima abilitati nel firmware (`src/include/endstop/EndstopConfig.hpp`) e la
scheda ricaricata: senza, le misure non hanno un riferimento e lo dicono.
