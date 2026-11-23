#!/usr/bin/env bash
# Compila "3dp", il servizio locale con riga di comando (src/local).
#
# Servono solo g++ (C++17) e SQLite (libsqlite3-dev su Ubuntu, sqlite su Arch):
# niente Boost, Kafka, curl o vcpkg.
#
#   tools/build_local.sh          -> build-local/3dp
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-local}"
CXX="${CXX:-g++}"
mkdir -p "$BUILD/obj"

sources=(
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
    objects+=("$obj")
    if [ ! -f "$obj" ] || [ "$ROOT/$rel" -nt "$obj" ]; then
        echo "  CXX $rel"
        "$CXX" -std=c++17 -O2 -Wall -Wextra -I"$ROOT/include" -I"$ROOT/src/local" -c "$ROOT/$rel" -o "$obj"
    fi
done

link=(-lsqlite3 -pthread)
case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) link+=(-lws2_32 -static) ;;
esac

"$CXX" "${objects[@]}" -o "$BUILD/3dp" "${link[@]}"
echo "built $BUILD/3dp"
