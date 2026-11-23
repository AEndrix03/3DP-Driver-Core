#!/usr/bin/env bash
# Test di integrazione Driver <-> Firmware su PC, senza hardware.
#
# Compila il firmware reale (-DPLATFORM_NATIVE, scheda simulata) insieme al core
# reale del driver e li fa parlare iniettando guasti. Servono solo g++ (C++17):
# niente Boost, Kafka o vcpkg.
#
#   tests/integration/run.sh [percorso del repo 3D-Printer-Firmware]
#   ONLY="travel limits" tests/integration/run.sh   -> solo gli scenari il cui nome contiene il testo
set -euo pipefail

DRIVER_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
FIRMWARE_DIR="$(cd "${1:-$DRIVER_DIR/../3D-Printer-Firmware}" && pwd)"
BUILD_DIR="${BUILD_DIR:-$DRIVER_DIR/build-sim}"
CXX="${CXX:-g++}"

if [ ! -f "$FIRMWARE_DIR/test/native/SimBoard.cpp" ]; then
    echo "Firmware repo not found at: $FIRMWARE_DIR" >&2
    exit 2
fi

mkdir -p "$BUILD_DIR/fw" "$BUILD_DIR/drv" "$BUILD_DIR/work"
objects=()

# Un header modificato in uno dei due repo fa ricompilare tutto (niente oggetti incoerenti)
stamp="$BUILD_DIR/.headers"
newest_header="$(find "$DRIVER_DIR/include" "$FIRMWARE_DIR/src" "$FIRMWARE_DIR/test/native"     \( -name '*.hpp' -o -name '*.h' \) -newer "$stamp" -print -quit 2>/dev/null || true)"
if [ ! -f "$stamp" ] || [ -n "$newest_header" ]; then
    REBUILD=1
    touch "$stamp"
fi

compile() { # <src> <obj> <flags...>
    local src="$1" obj="$2"
    shift 2
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ -n "${REBUILD:-}" ]; then
        "$CXX" -std=c++17 -O1 -g -Wall -Wextra "$@" -c "$src" -o "$obj"
    fi
    objects+=("$obj")
}

# ---- firmware: tutti i sorgenti, con l'Arduino.h simulato ------------------
# -Dposition=fw_position: firmware e driver hanno entrambi position::Position
while IFS= read -r src; do
    obj="$BUILD_DIR/fw/$(echo "${src#$FIRMWARE_DIR/}" | tr '/' '_').o"
    compile "$src" "$obj" -DPLATFORM_NATIVE -DSIM_ENDSTOPS -Dposition=fw_position -I"$FIRMWARE_DIR/test/native"
done < <(find "$FIRMWARE_DIR/src" "$FIRMWARE_DIR/test/native" -name '*.cpp' | sort)

# ---- driver: solo il core (protocollo, comandi, traduttore, coda) -----------
driver_sources=(
    src/core/CommandBuilder.cpp
    src/core/CommandContext.cpp
    src/core/CommandExecutor.cpp
    src/core/DriverInterface.cpp
    src/core/serial/handler/SerialProtocolHandler.cpp
    src/core/command/CommandCategoryInterface.cpp
    src/core/command/endstop/EndstopCommands.cpp
    src/core/command/extruder/ExtruderCommands.cpp
    src/core/command/fan/FanCommands.cpp
    src/core/command/history/HistoryCommands.cpp
    src/core/command/motion/MotionCommands.cpp
    src/core/command/system/SystemCommands.cpp
    src/core/command/temperature/TemperatureCommands.cpp
    src/core/printer/job/tracking/JobTracker.cpp
    src/core/printer/job/JobCheckpoint.cpp
    src/core/printer/job/PrintJobManager.cpp
    src/core/printer/state/StateTracker.cpp
    src/core/queue/CommandExecutorQueue.cpp
    src/translator/GCodeTranslator.cpp
    src/translator/dispatchers/fan/FanDispatcher.cpp
    src/translator/dispatchers/motion/MotionDispatcher.cpp
    src/translator/dispatchers/system/SystemDispatcher.cpp
    src/translator/dispatchers/temperature/TemperatureDispatcher.cpp
    src/translator/dispatchers/compat/CompatibilityDispatcher.cpp
    src/translator/GCodeSupport.cpp
    tests/integration/firmware_link_test.cpp
)
for rel in "${driver_sources[@]}"; do
    obj="$BUILD_DIR/drv/$(echo "$rel" | tr '/' '_').o"
    compile "$DRIVER_DIR/$rel" "$obj" -I"$DRIVER_DIR/include" -I"$FIRMWARE_DIR/test/native"
done

# Su Windows (MSYS2/MinGW) link statico: evita di caricare a runtime una
# libstdc++ diversa da quella del compilatore se nel PATH ce ne sono piu' di una.
link_flags=(-pthread)
case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) link_flags+=(-static) ;;
esac

"$CXX" "${objects[@]}" -o "$BUILD_DIR/firmware_link_test" "${link_flags[@]}"

# La coda comandi crea temp/command_queue.dat nella directory corrente
# File G-code di prova (prodotti da uno slicer vero) usati da alcuni scenari
export THREEDP_FIXTURES="$DRIVER_DIR/tests/fixtures"
cd "$BUILD_DIR/work"
"$BUILD_DIR/firmware_link_test" ${ONLY:+"$ONLY"}
