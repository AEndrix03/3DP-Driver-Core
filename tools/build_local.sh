#!/usr/bin/env bash
# Compila "3dp", il servizio locale con riga di comando (src/local).
#
# Servono solo g++ (C++17) e SQLite (libsqlite3-dev su Ubuntu, sqlite su Arch):
# niente Boost, Kafka, curl o vcpkg.
#
#   tools/build_local.sh          -> build-local/3dp
#   tools/build_local.sh --sim    -> lo stesso, con dentro la stampante simulata (3dp serve --sim):
#                                    compila anche il firmware per il PC, quindi serve il repo
#                                    3D-Printer-Firmware accanto a questo (o FIRMWARE_DIR=...)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-local}"
CXX="${CXX:-g++}"
SIM=""
[ "${1:-}" = "--sim" ] && SIM=1
FIRMWARE_DIR="${FIRMWARE_DIR:-$ROOT/../3D-Printer-Firmware}"
if [ -n "$SIM" ] && [ ! -f "$FIRMWARE_DIR/test/native/SimBoard.cpp" ]; then
    echo "firmware repo not found at: $FIRMWARE_DIR (set FIRMWARE_DIR)" >&2
    exit 2
fi
mkdir -p "$BUILD/obj" "$BUILD/fw"

sources=(
    src/core/CommandBuilder.cpp
    src/core/CommandContext.cpp
    src/core/CommandExecutor.cpp
    src/core/DriverInterface.cpp
    src/core/serial/handler/SerialProtocolHandler.cpp
    src/core/serial/LinuxBaud.cpp
    src/core/command/CommandCategoryInterface.cpp
    src/core/command/endstop/EndstopCommands.cpp
    src/core/command/extruder/ExtruderCommands.cpp
    src/core/command/fan/FanCommands.cpp
    src/core/command/history/HistoryCommands.cpp
    src/core/command/motion/MotionCommands.cpp
    src/core/motion/MotionPlanner.cpp
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
    src/translator/GCodePreview.cpp
    src/translator/dispatchers/extruder/ExtruderDispatcher.cpp
    src/translator/dispatchers/endstop/EndstopDispatcher.cpp
    src/local/Slicer.cpp
    src/local/SimPort.cpp
    src/local/WirePort.cpp
    src/local/Measure.cpp
    src/local/MeasureMath.cpp
    src/local/Settings.cpp
    src/logger/Logger.cpp
    src/local/NativeSerialPort.cpp
    src/local/History.cpp
    src/local/main.cpp
)

# Un header modificato fa ricompilare tutto (niente oggetti incoerenti)
stamp="$BUILD/.headers"
newest="$(find "$ROOT/include" "$ROOT/src/local" "$ROOT/third_party" -name '*.h*' -newer "$stamp" 2>/dev/null | head -n 1 || true)"
if [ ! -f "$stamp" ] || [ -n "$newest" ]; then
    rm -f "$BUILD"/obj/*.o
    touch "$stamp"
fi

objects=()
for rel in "${sources[@]}"; do
    obj="$BUILD/obj/$(echo "$rel" | tr '/' '_').o"
    extra=()
    if [ "$rel" = "src/local/SimPort.cpp" ] && [ -n "$SIM" ]; then
        obj="${obj%.o}.sim.o"
        extra=(-DTHREEDP_SIM -I"$FIRMWARE_DIR/test/native")
    fi
    objects+=("$obj")
    if [ ! -f "$obj" ] || [ "$ROOT/$rel" -nt "$obj" ]; then
        echo "  CXX $rel"
        "$CXX" -std=c++17 -O2 -Wall -Wextra -I"$ROOT/include" -I"$ROOT/src/local" "${extra[@]}" -c "$ROOT/$rel" -o "$obj"
    fi
done

# Stampante simulata: il firmware vero con la scheda finta dei test (stesse opzioni di tests/integration/run.sh)
if [ -n "$SIM" ]; then
    fw_stamp="$BUILD/fw/.headers"
    fw_newest="$(find "$FIRMWARE_DIR/src" "$FIRMWARE_DIR/test/native" -name '*.h*' -newer "$fw_stamp" 2>/dev/null | head -n 1 || true)"
    if [ ! -f "$fw_stamp" ] || [ -n "$fw_newest" ]; then
        rm -f "$BUILD"/fw/*.o
        touch "$fw_stamp"
    fi
    while IFS= read -r src; do
        obj="$BUILD/fw/$(echo "${src#$FIRMWARE_DIR/}" | tr '/' '_').o"
        objects+=("$obj")
        if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
            echo "  FW  ${src#$FIRMWARE_DIR/}"
            "$CXX" -std=c++17 -O2 -DPLATFORM_NATIVE -DSIM_ENDSTOPS -Dposition=fw_position \
                -I"$FIRMWARE_DIR/test/native" -c "$src" -o "$obj"
        fi
    done < <(find "$FIRMWARE_DIR/src" "$FIRMWARE_DIR/test/native" -name '*.cpp' | sort)
fi

link=(-lsqlite3 -pthread)
case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) link+=(-lws2_32 -static) ;;
esac

"$CXX" "${objects[@]}" -o "$BUILD/3dp" "${link[@]}"
echo "built $BUILD/3dp"
