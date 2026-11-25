// Test di integrazione Driver <-> Firmware.
//
// Il firmware VERO (repo 3D-Printer-Firmware, compilato con -DPLATFORM_NATIVE)
// gira in un thread su una scheda simulata; il driver VERO gli parla attraverso
// una porta seriale finta che permette di iniettare guasti: righe perse o
// corrotte, firmware bloccato, riavvio a meta' movimento, salti di orologio.
//
// Build ed esecuzione: tests/integration/run.sh

#include "SimBoard.hpp"

#include "core/DriverInterface.hpp"
#include "core/motion/MotionPlanner.hpp"
#include "core/queue/CommandExecutorQueue.hpp"
#include "core/utils/FloatFormatter.hpp"
#include "logger/Logger.hpp"
#include "translator/GCodeTranslator.hpp"
#include "core/printer/job/JobCheckpoint.hpp"
#include "core/printer/job/PrintJobManager.hpp"
#include "core/printer/state/StateTracker.hpp"
#include "translator/dispatchers/fan/FanDispatcher.hpp"
#include "translator/dispatchers/motion/MotionDispatcher.hpp"
#include "translator/dispatchers/system/SystemDispatcher.hpp"
#include "translator/dispatchers/temperature/TemperatureDispatcher.hpp"
#include "translator/dispatchers/compat/CompatibilityDispatcher.hpp"
#include "translator/GCodeSupport.hpp"
#include "translator/GCodePreview.hpp"
#include "../../src/local/MeasureMath.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;
using core::command::motion::MoveTarget;
using translator::gcode::MotionDispatcher;

// ---------------------------------------------------------------------------
// Logger silenzioso: errori e warning restano consultabili se un test fallisce
// ---------------------------------------------------------------------------
static std::mutex logMutex;
static std::deque<std::string> recentLog;

static void keep(const std::string &level, const std::string &message) {
    std::lock_guard<std::mutex> lock(logMutex);
    recentLog.push_back(level + message);
    if (recentLog.size() > 40) recentLog.pop_front();
}

void Logger::logInfo(const std::string &) {}

void Logger::logWarning(const std::string &message) { keep("[W] ", message); }

void Logger::logError(const std::string &message) { keep("[E] ", message); }

// ---------------------------------------------------------------------------
// Il download del G-code via HTTP (libcurl) non fa parte di questi test
// ---------------------------------------------------------------------------
namespace core::print {
    GCodeDownloader::GCodeDownloader() = default;

    GCodeDownloader::~GCodeDownloader() = default;

    void GCodeDownloader::downloadAsync(const std::string &, const std::string &, ProgressCallback,
                                        CompletionCallback) {}

    void GCodeDownloader::cancelDownload() {}
}

// ---------------------------------------------------------------------------
// Mini framework
// ---------------------------------------------------------------------------
static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
        ++checks; \
        if (!(cond)) { \
            ++failures; \
            std::printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define CHECK_MSG(cond, text) do { \
        ++checks; \
        if (!(cond)) { \
            ++failures; \
            std::printf("    FAIL %s:%d  %s  [%s]\n", __FILE__, __LINE__, #cond, std::string(text).c_str()); \
        } \
    } while (0)

static bool contains(const core::types::Result &result, const std::string &text) {
    for (const auto &line: result.body) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Porta seriale finta con iniezione di guasti
// ---------------------------------------------------------------------------
class SimSerialPort : public core::SerialPort {
public:
    void send(const std::string &data) override {
        std::string out = data;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const bool isCommand = !data.empty() && data[0] == 'N';
            if (isCommand) {
                ++commandsSent_;
                if (dropTx_ > 0) {
                    --dropTx_;
                    return;
                }
                if (corruptTx_ > 0) {
                    --corruptTx_;
                    out[out.size() / 2] ^= 0x01;
                }
            }
        }
        sim::hostWrite(out + "\n");
    }

    void sendRaw(const std::string &bytes) override {
        std::string out = bytes;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++framesSent_;
            if (dropTx_ > 0) {
                --dropTx_;
                return;
            }
            if (corruptTx_ > 0) {
                --corruptTx_;
                out[out.size() / 2] ^= 0x01;
            }
        }
        sim::hostWrite(out);
    }

    int framesSent() {
        std::lock_guard<std::mutex> lock(mutex_);
        return framesSent_;
    }

    std::string receiveLine() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!injected_.empty()) {
                std::string line = injected_.front();
                injected_.pop_front();
                return line;
            }
        }

        std::string line = sim::hostReadLine(40);
        if (line.empty()) return "";

        std::lock_guard<std::mutex> lock(mutex_);
        // "OK0" vale anche per la conferma corta dei movimenti (4 byte con il bit 7 a 1)
        core::MoveFrame::Ack ack;
        const bool moveAck = dropRxPrefix_ == "OK0" && core::MoveFrame::decodeAck(line, ack);
        if (dropRx_ > 0 && (moveAck || line.rfind(dropRxPrefix_, 0) == 0)) {
            --dropRx_;
            return "";
        }
        return line;
    }

    bool isOpen() const override { return true; }

    void dropNextFromFirmware(const std::string &prefix, int count) {
        std::lock_guard<std::mutex> lock(mutex_);
        dropRxPrefix_ = prefix;
        dropRx_ = count;
    }

    void dropNextCommands(int count) {
        std::lock_guard<std::mutex> lock(mutex_);
        dropTx_ = count;
    }

    void corruptNextCommands(int count) {
        std::lock_guard<std::mutex> lock(mutex_);
        corruptTx_ = count;
    }

    void inject(const std::string &line) {
        std::lock_guard<std::mutex> lock(mutex_);
        injected_.push_back(line);
    }

    int commandsSent() {
        std::lock_guard<std::mutex> lock(mutex_);
        return commandsSent_;
    }

private:
    std::mutex mutex_;
    std::deque<std::string> injected_;
    std::string dropRxPrefix_;
    int dropRx_ = 0;
    int dropTx_ = 0;
    int corruptTx_ = 0;
    int commandsSent_ = 0;
    int framesSent_ = 0;
};

static std::string withChecksum(const std::string &payload) {
    uint8_t cs = 0;
    for (char c: payload) cs ^= static_cast<uint8_t>(c);
    return payload + " *" + std::to_string(cs);
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
static std::shared_ptr<SimSerialPort> port;
static std::shared_ptr<core::DriverInterface> driver;

static void fastTiming() {
    core::CommandExecutor::Timing timing;
    timing.inactivity = 1500ms; // > 1 s: il firmware manda BUSY ogni secondo
    timing.retryDelay = 20ms;
    timing.maxAttempts = 3;
    driver->executor()->setTiming(timing);
}

/// Riporta firmware e driver in uno stato noto: Idle, nessun errore, posizione zero.
static void normalize() {
    sim::setTimeScale(1.0);
    sim::clearEndstops();
    driver->system()->clearError();
    driver->waitForIdle(60s);
    driver->system()->endPrint();
    driver->motion()->setPosition({0.0, 0.0, 0.0, std::nullopt, std::nullopt});
    sim::resetSteps();
}

/// I movimenti rispondono OK appena accodati: attende che siano stati eseguiti.
static bool idle() {
    return driver->waitForIdle(60s);
}

static bool waitFor(const std::function<bool()> &condition, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) return true;
        std::this_thread::sleep_for(5ms);
    }
    return condition();
}

// ---------------------------------------------------------------------------
// Test
// ---------------------------------------------------------------------------
static void testBootAndStatus() {
    const auto result = driver->system()->printStatus();
    CHECK_MSG(result.isSuccess(), result.message);
    CHECK(contains(result, "STA Idle"));

    const auto diag = driver->system()->diagnostics();
    CHECK(diag.isSuccess());
    CHECK(contains(diag, "DIA "));
    CHECK(contains(diag, "POWERON"));
    CHECK(contains(diag, "PRF ")); // picco di stack, durata interrupt step, scadenze mancate
}

static void testRejectedIsNotSuccess() {
    normalize();
    // Movimento fuori dallo stato Printing: prima il firmware rispondeva "OK"
    const auto result = driver->motion()->moveRelative({1.0, std::nullopt, std::nullopt, std::nullopt, 1000.0});
    CHECK(result.isRejected());
    CHECK_MSG(result.message.find("INV_STATE") != std::string::npos, result.message);
    CHECK(sim::steps().x == 0);

    // Codice inesistente (era il caso della ritrazione A20)
    const auto unknown = driver->sendCommandInternal('A', 20, {});
    CHECK(unknown.isRejected());
}

static void testStraightLine() {
    normalize();
    CHECK(driver->system()->startPrint().isSuccess());

    sim::recordPath(true);
    const auto result = driver->motion()->moveAbsolute({20.0, 5.0, std::nullopt, std::nullopt, 3000.0});
    CHECK_MSG(result.isSuccess(), result.message);
    CHECK(idle());
    sim::recordPath(false);

    const auto steps = sim::steps();
    CHECK(steps.x == 1600);
    CHECK(steps.y == 400);

    // Ogni punto del percorso deve stare entro uno step dalla retta ideale
    double worst = 0.0;
    for (const auto &[x, y]: sim::path()) {
        worst = std::max(worst, std::fabs(x * 400.0 - y * 1600.0) / std::hypot(1600.0, 400.0));
    }
    CHECK_MSG(worst <= 1.0, "max deviation (steps): " + std::to_string(worst));

    const auto position = driver->motion()->getPosition();
    CHECK(position.has_value());
    if (position) {
        CHECK(std::fabs(position->x - 20.0) < 0.001);
        CHECK(std::fabs(position->y - 5.0) < 0.001);
    }

    // Asse omesso = invariato (prima veniva inviato -1)
    CHECK(driver->motion()->moveAbsolute({10.0, std::nullopt, std::nullopt, std::nullopt, 3000.0}).isSuccess());
    CHECK(idle());
    CHECK(sim::steps().x == 800);
    CHECK(sim::steps().y == 400);
}

static void testLongZBeyond16Bit() {
    normalize();
    CHECK(driver->system()->startPrint().isSuccess());

    sim::setTimeScale(20.0); // 90 mm a 150 mm/min = 36 s simulati
    const auto result = driver->motion()->moveAbsolute({std::nullopt, std::nullopt, 90.0, std::nullopt, 150.0});
    CHECK_MSG(result.isSuccess(), result.message);
    CHECK(idle());
    CHECK(sim::steps().z == 36000); // con "int" a 16 bit andava in overflow a 81,9 mm

    const auto position = driver->motion()->getPosition();
    CHECK(position && std::fabs(position->z - 90.0) < 0.001);
    sim::setTimeScale(1.0);
}

static void testExtrusionRemainder() {
    normalize();
    CHECK(driver->system()->startPrint().isSuccess());

    // 200 x 0.0105 mm = 2.1 mm = 168 step. Arrotondando ogni movimento sarebbero 200.
    for (int i = 0; i < 200; ++i) {
        const auto result = driver->motion()->moveRelative(
                {std::nullopt, std::nullopt, std::nullopt, 0.0105, 600.0});
        if (!result.isSuccess()) {
            CHECK_MSG(false, result.message);
            break;
        }
    }
    CHECK(idle());
    CHECK_MSG(sim::steps().e == 168, "E steps: " + std::to_string(sim::steps().e));

    // Estrusione coordinata con XY
    sim::resetSteps();
    CHECK(driver->motion()->moveRelative({10.0, std::nullopt, std::nullopt, 0.5, 1800.0}).isSuccess());
    CHECK(idle());
    CHECK(sim::steps().x == 800);
    CHECK(sim::steps().e == 40);
}

static void testLostResponse() {
    normalize();
    CHECK(driver->system()->startPrint().isSuccess());

    const int sentBefore = port->commandsSent();
    port->dropNextFromFirmware("OK0", 1);
    const auto result = driver->motion()->moveRelative({1.0, std::nullopt, std::nullopt, std::nullopt, 3000.0});

    CHECK_MSG(result.isSuccess(), result.message);
    CHECK(idle());
    CHECK(sim::steps().x == 80);                        // eseguito una volta sola
    CHECK(port->commandsSent() - sentBefore >= 2);       // ritrasmesso
    CHECK(!driver->isLinkFaulted());

    CHECK(driver->system()->printStatus().isSuccess()); // la sequenza prosegue
}

static void testLostAndCorruptedCommand() {
    normalize();
    CHECK(driver->system()->startPrint().isSuccess());

    port->corruptNextCommands(1);
    auto result = driver->motion()->moveRelative({1.0, std::nullopt, std::nullopt, std::nullopt, 3000.0});
    CHECK_MSG(result.isSuccess(), result.message);
    CHECK(idle());
    CHECK(sim::steps().x == 80);

    port->dropNextCommands(1);
    result = driver->motion()->moveRelative({1.0, std::nullopt, std::nullopt, std::nullopt, 3000.0});
    CHECK_MSG(result.isSuccess(), result.message);
    CHECK(idle());
    CHECK(sim::steps().x == 160);
}

static void testNoiseAndStaleResponses() {
    normalize();

    // Righe senza checksum e una vecchia risposta valida ma di un altro comando:
    // nessuna delle due deve chiudere il comando in corso.
    port->inject("IDLE MODE");
    port->inject(withChecksum("OK0 N1"));
    port->inject("OK0 N999999 *1");

    const auto position = driver->motion()->getPosition();
    CHECK(position.has_value()); // ha atteso la risposta vera, con i dati
}

static void testTimeoutIsNeverSuccess() {
    normalize();
    const uint64_t epoch = driver->faultEpoch();

    sim::freeze(true);
    const auto result = driver->system()->printStatus();
    CHECK(result.isTimeout());
    CHECK(!result.isSuccess());
    CHECK(driver->isLinkFaulted());
    CHECK(driver->faultEpoch() > epoch);
    CHECK(driver->getState() == core::PrintState::Error);
    sim::freeze(false);

    // Alla ripresa il primo comando risincronizza da solo (handshake N0)
    const auto recovered = driver->system()->printStatus();
    CHECK_MSG(recovered.isSuccess(), recovered.message);
    CHECK(!driver->isLinkFaulted());
    CHECK(driver->getState() == core::PrintState::Idle);
}

static void testFirmwareRebootDuringMove() {
    normalize();
    CHECK(driver->system()->startPrint().isSuccess());

    // Il movimento e' in coda sul firmware quando questo si riavvia (watchdog)
    CHECK(driver->motion()->moveRelative({100.0, std::nullopt, std::nullopt, std::nullopt, 1000.0}).isSuccess());
    std::this_thread::sleep_for(300ms);
    sim::reboot(4 /* hal::RESET_WATCHDOG */);
    std::this_thread::sleep_for(200ms);

    // Il primo comando successivo scopre il riavvio invece di proseguire alla cieca
    const auto result = driver->motion()->moveRelative({1.0, std::nullopt, std::nullopt, std::nullopt, 1000.0});
    CHECK(result.isSyncLost());
    CHECK_MSG(result.message.find("WDT") != std::string::npos, result.message);
    CHECK(driver->isLinkFaulted());

    // Dopo il riavvio il firmware e' Idle e riparte da N1: il driver si riallinea
    const auto status = driver->system()->printStatus();
    CHECK_MSG(status.isSuccess(), status.message);
    CHECK(contains(status, "STA Idle"));
    CHECK(driver->getState() == core::PrintState::Idle);

    // ...e sa che la posizione non ha piu' un riferimento valido
    bool referenced = true;
    CHECK(driver->motion()->getPosition(&referenced).has_value());
    CHECK(!referenced);
}

static void testSequenceBeyond16Bit() {
    normalize();

    // Il firmware e' piu' avanti del driver: il driver adotta la sua numerazione.
    sim::setFirmwareSequence(65534);
    auto result = driver->system()->printStatus(); // diventa N65535
    CHECK_MSG(result.isSuccess(), result.message);

    // N65536 e oltre: con il numero a 16 bit la stampa moriva qui
    for (int i = 0; i < 3; ++i) {
        result = driver->system()->printStatus();
        CHECK_MSG(result.isSuccess(), result.message);
    }
    CHECK(!driver->isLinkFaulted());
}

static void testAbortMotion() {
    normalize();
    CHECK(driver->system()->startPrint().isSuccess());

    // Tre segmenti in coda, 15 s di movimento in tutto
    for (int i = 0; i < 3; ++i) {
        CHECK(driver->motion()->moveRelative({50.0, std::nullopt, std::nullopt, std::nullopt, 600.0}).isSuccess());
    }
    std::this_thread::sleep_for(400ms);
    const auto start = std::chrono::steady_clock::now();
    driver->abortMotion();
    CHECK(driver->waitForIdle(3s));
    CHECK(std::chrono::steady_clock::now() - start < 2s);

    const long stopped = sim::steps().x;
    CHECK(stopped > 0 && stopped < 4000); // fermato nel primo segmento, la coda e' stata scartata
    CHECK(!driver->isLinkFaulted());

    // Arresto in decelerazione: la posizione riportata coincide con gli step fatti...
    const auto position = driver->motion()->getPosition();
    CHECK(position && std::fabs(position->x * 80.0 - stopped) < 0.5);

    // ...e il movimento successivo riparte esattamente da li'
    CHECK(driver->motion()->moveAbsolute({0.0, std::nullopt, std::nullopt, std::nullopt, 3000.0}).isSuccess());
    CHECK(idle());
    CHECK(sim::steps().x == 0);
}

static void testThermal() {
    normalize();
    const uint8_t heaterPin = 15;     // A1
    const uint8_t thermistorPin = 14; // A0

    // --- senza sensore il riscaldamento e' rifiutato ---
    sim::enableThermistor(false);
    auto result = driver->temperature()->setHotendTemperature(200);
    CHECK(result.isRejected());
    CHECK_MSG(result.message.find("NO_SENSOR") != std::string::npos, result.message);
    CHECK(!sim::pinLevel(heaterPin));

    // --- regolazione ---
    sim::setTimeScale(10.0);
    sim::setAnalog(thermistorPin, sim::adcForTemperature(25.0));
    sim::enableThermistor(true);
    std::this_thread::sleep_for(100ms);

    result = driver->temperature()->getHotendTemperature();
    CHECK(result.isSuccess());
    CHECK_MSG(contains(result, "T:2"), result.body.empty() ? "no body" : result.body[0]); // ~25 C

    result = driver->temperature()->setHotendTemperature(200);
    CHECK_MSG(result.isSuccess(), result.message);
    CHECK(waitFor([&] { return sim::pinLevel(heaterPin); }, 500ms)); // freddo: heater acceso

    sim::setAnalog(thermistorPin, sim::adcForTemperature(206.0));
    CHECK(waitFor([&] { return !sim::pinLevel(heaterPin); }, 1500ms)); // sopra target: spento

    sim::setAnalog(thermistorPin, sim::adcForTemperature(190.0));
    CHECK(waitFor([&] { return sim::pinLevel(heaterPin); }, 1500ms)); // sotto target: riacceso

    // --- estrusione a freddo bloccata, a caldo permessa ---
    CHECK(driver->system()->startPrint().isSuccess());
    sim::setAnalog(thermistorPin, sim::adcForTemperature(100.0));
    std::this_thread::sleep_for(600ms);
    result = driver->motion()->moveRelative({std::nullopt, std::nullopt, std::nullopt, 1.0, 600.0});
    CHECK(result.isRejected());
    CHECK_MSG(result.message.find("COLD_EXT") != std::string::npos, result.message);

    // --- termistore scollegato: heater spento e macchina in errore ---
    const uint64_t epoch = driver->faultEpoch();
    sim::setAnalog(thermistorPin, 1023);
    CHECK(waitFor([&] { return !sim::pinLevel(heaterPin); }, 500ms));

    result = driver->motion()->moveRelative({1.0, std::nullopt, std::nullopt, std::nullopt, 1000.0});
    CHECK(result.isBusy());
    CHECK_MSG(result.message.find("THERM_ERR") != std::string::npos, result.message);
    CHECK(driver->faultEpoch() > epoch);
    CHECK(driver->getState() == core::PrintState::Error);

    CHECK(contains(driver->system()->errorStatus(), "THERM_ERR"));
    CHECK(driver->system()->clearError().isSuccess());

    // --- heater che non scalda: allarme entro la finestra di osservazione ---
    sim::setAnalog(thermistorPin, sim::adcForTemperature(25.0));
    std::this_thread::sleep_for(600ms);
    CHECK(driver->temperature()->setHotendTemperature(200).isSuccess());
    CHECK(waitFor([&] { return sim::pinLevel(heaterPin); }, 500ms));
    sim::advanceTime(41000);
    CHECK(waitFor([&] { return !sim::pinLevel(heaterPin); }, 500ms));
    CHECK(contains(driver->system()->errorStatus(), "HEAT_FAIL"));
    CHECK(driver->system()->clearError().isSuccess());

    // --- host sparito con heater acceso: spento dopo HOST_LOST_TIMEOUT ---
    sim::setAnalog(thermistorPin, sim::adcForTemperature(199.5));
    std::this_thread::sleep_for(900ms);
    CHECK(driver->temperature()->setHotendTemperature(200).isSuccess());
    sim::setAnalog(thermistorPin, sim::adcForTemperature(190.0));
    CHECK(waitFor([&] { return sim::pinLevel(heaterPin); }, 1500ms));
    sim::advanceTime(301000);
    CHECK(waitFor([&] { return !sim::pinLevel(heaterPin); }, 500ms));

    sim::enableThermistor(false);
    sim::setTimeScale(1.0);
}

/// Minimo e massimo della temperatura del blocco simulato per `simSeconds` secondi simulati.
static std::pair<double, double> observeTemperature(double simSeconds, double timeScale) {
    double low = 1e9, high = -1e9;
    const auto end = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds(static_cast<int>(simSeconds / timeScale * 1000));
    int tick = 0;
    while (std::chrono::steady_clock::now() < end) {
        const double t = sim::modelTemperature();
        low = std::min(low, t);
        high = std::max(high, t);
        std::this_thread::sleep_for(5ms);
        // L'host resta presente, come durante una stampa: senza comandi per 5 minuti
        // il firmware spegnerebbe l'heater (HOST_LOST).
        if (++tick % 20 == 0) driver->temperature()->getHotendTemperature();
    }
    return {low, high};
}

static void testPidRegulation() {
    normalize();
    constexpr double SCALE = 40.0;
    // Hotend simulato con inerzia termica: il termistore segue il pin dell'heater
    sim::enableThermalModel(true, 25.0);
    sim::enableThermistor(true);
    sim::setTimeScale(SCALE);
    std::this_thread::sleep_for(100ms);

    // --- salita a 200 gradi: arriva, senza sforare e senza oscillare ---
    CHECK(driver->temperature()->setHotendTemperature(200).isSuccess());
    double peak = 0.0;
    const bool reached = waitFor([&] {
        peak = std::max(peak, sim::modelTemperature());
        driver->temperature()->getHotendTemperature();
        return sim::modelTemperature() >= 198.0;
    }, 15s); // 600 s simulati al massimo
    CHECK_MSG(reached, "temperature " + std::to_string(sim::modelTemperature()));
    peak = std::max(peak, observeTemperature(120.0, SCALE).second); // assestamento
    CHECK_MSG(peak < 205.0, "overshoot: peak " + std::to_string(peak));

    auto [low, high] = observeTemperature(120.0, SCALE);
    CHECK_MSG(high - low < 2.0, "ripple " + std::to_string(high - low));
    CHECK_MSG(std::fabs((high + low) / 2 - 200.0) < 1.0, "regime " + std::to_string((high + low) / 2));
    CHECK(contains(driver->system()->printStatus(), "STA Idle")); // nessuna protezione scattata

    // --- cambio di target verso l'alto ---
    CHECK(driver->temperature()->setHotendTemperature(220).isSuccess());
    CHECK(waitFor([&] { return sim::modelTemperature() >= 218.5; }, 10s));
    peak = observeTemperature(100.0, SCALE).second;
    CHECK_MSG(peak < 225.0, "overshoot on step: peak " + std::to_string(peak));
    std::tie(low, high) = observeTemperature(100.0, SCALE);
    CHECK_MSG(high - low < 2.0 && std::fabs((high + low) / 2 - 220.0) < 1.0,
              "regime " + std::to_string(low) + ".." + std::to_string(high));

    // --- i guadagni si tarano senza riprogrammare ---
    auto gains = driver->temperature()->setPid(18.5, 0.9, 80.0);
    CHECK_MSG(gains.isSuccess() && contains(gains, "PID 18.50 0.900 80.0"), gains.message);
    gains = driver->temperature()->setPid(22.0, 1.08, 114.0);
    CHECK(contains(gains, "PID 22.00 1.080 114.0"));

    // --- spegnimento: l'heater resta spento e la temperatura scende ---
    CHECK(driver->temperature()->setHotendTemperature(0).isSuccess());
    std::this_thread::sleep_for(200ms);
    const double afterOff = sim::modelTemperature();
    CHECK(!sim::pinLevel(15 /* A1: heater */));
    observeTemperature(60.0, SCALE);
    CHECK(sim::modelTemperature() < afterOff - 10.0);
    CHECK(!sim::pinLevel(15));

    sim::setTimeScale(1.0);
    sim::enableThermistor(false);
    sim::enableThermalModel(false);
}

static void testLongRunningClocks() {
    normalize();

    // --- 19 ore di inattivita': i timer a 16 bit andavano in overflow a 18h12m ---
    sim::advanceTime(19ULL * 3600 * 1000);
    std::this_thread::sleep_for(100ms);
    auto power = driver->sendCommandInternal('S', 8, {});
    CHECK_MSG(power.isSuccess(), power.message);
    CHECK(contains(power, "PWR Active")); // il comando stesso ha risvegliato la macchina
    CHECK(contains(power, "PWR DEEP"));   // ...che era arrivata fino al deep sleep

    // --- rollover di millis() (~49,7 giorni) durante un movimento ---
    CHECK(driver->system()->startPrint().isSuccess());
    sim::resetSteps();
    sim::jumpNearMillisRollover(500);
    auto result = driver->motion()->moveRelative({40.0, std::nullopt, std::nullopt, std::nullopt, 2400.0}); // ~1 s
    CHECK_MSG(result.isSuccess(), result.message);
    CHECK(idle());
    CHECK(sim::steps().x == 3200);

    power = driver->sendCommandInternal('S', 8, {});
    CHECK(contains(power, "PWR Active 0")); // inattivita' sensata anche dopo il rollover

    // --- rollover di micros() (ogni ~71 minuti) durante un movimento ---
    sim::resetSteps();
    sim::jumpNearMicrosRollover(300000);
    const auto start = std::chrono::steady_clock::now();
    result = driver->motion()->moveRelative({40.0, std::nullopt, std::nullopt, std::nullopt, 2400.0});
    CHECK(idle());
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK_MSG(result.isSuccess(), result.message);
    CHECK(sim::steps().x == 3200);
    CHECK(elapsed > 800ms && elapsed < 3s); // ne' bloccato ne' "raffica" di step
}

/// Velocita' (step/s) programmate dal firmware, una per step.
static std::vector<double> recordedRates() {
    std::vector<double> rates;
    for (uint16_t ticks: sim::intervals()) rates.push_back(2000000.0 / ticks);
    return rates;
}

static void testLookahead() {
    normalize();
    CHECK(driver->system()->startPrint().isSuccess());

    // --- 60 segmenti collineari da 1 mm a 60 mm/s: devono fondersi in un unico movimento ---
    sim::recordIntervals(true);
    for (int i = 1; i <= 60; ++i) {
        const auto result = driver->motion()->moveAbsolute(
                {static_cast<double>(i), std::nullopt, std::nullopt, std::nullopt, 3600.0}, true);
        if (!result.isSuccess()) {
            CHECK_MSG(false, result.message);
            break;
        }
    }
    CHECK(idle());
    sim::recordIntervals(false);
    CHECK(sim::steps().x == 4800);

    auto rates = recordedRates();
    CHECK(rates.size() >= 4700);
    if (rates.size() >= 4700) {
        // A regime (terzo centrale) la velocita' resta quella nominale: nessuna frenata tra i segmenti.
        // Senza lookahead ogni segmento da 1 mm ripartirebbe da fermo senza mai superare ~30 mm/s.
        double slowest = 1e9, fastest = 0.0;
        for (size_t i = rates.size() / 3; i < rates.size() * 2 / 3; ++i) {
            slowest = std::min(slowest, rates[i]);
            fastest = std::max(fastest, rates[i]);
        }
        CHECK_MSG(slowest > 4500.0, "slowest mid-path rate " + std::to_string(slowest));
        CHECK_MSG(fastest < 4810.0, "fastest rate " + std::to_string(fastest));

        // Mai oltre l'accelerazione configurata (1000 mm/s^2 = 80000 step/s^2).
        // Misurata su finestre di 16 step: il singolo intervallo e' quantizzato in tick di timer.
        double worst = 0.0;
        for (size_t i = 0; i + 16 < rates.size(); ++i) {
            double dt = 0.0;
            for (size_t k = i; k < i + 16; ++k) dt += 1.0 / rates[k];
            worst = std::max(worst, std::fabs(rates[i + 16] - rates[i]) / dt);
        }
        CHECK_MSG(worst < 80000.0 * 1.10, "peak acceleration (steps/s^2) " + std::to_string(worst));

        // Partenza e arrivo dolci
        CHECK(rates.front() < 600.0);
        CHECK(rates.back() < 600.0);
    }

    // --- spigolo: la velocita' di giunzione dipende dall'angolo ---
    const auto cornerMinimum = [&](double x2, double y2) {
        normalize();
        driver->system()->startPrint();
        sim::recordIntervals(true);
        driver->motion()->moveAbsolute({30.0, 0.0, std::nullopt, std::nullopt, 3600.0}, true);
        driver->motion()->moveAbsolute({x2, y2, std::nullopt, std::nullopt, 3600.0}, true);
        idle();
        sim::recordIntervals(false);
        const auto r = recordedRates();
        double minimum = 1e9;
        for (size_t i = 1600; i < r.size() && i < 3400; ++i) minimum = std::min(minimum, r[i]);
        return minimum;
    };

    const double gentle = cornerMinimum(55.98, 15.0); // deviazione di 30 gradi
    const double sharp = cornerMinimum(30.0, 30.0);   // 90 gradi
    CHECK_MSG(gentle > 1500.0, "30 degree corner rate " + std::to_string(gentle)); // attraversato in velocita'
    CHECK_MSG(sharp < 900.0, "90 degree corner rate " + std::to_string(sharp));    // quasi fermo
    CHECK(gentle > sharp * 2.0);
    CHECK(sim::steps().x == 2400 && sim::steps().y == 2400);
}

// ---------------------------------------------------------------------------
// Pianificatore lato host e linear advance (senza firmware: sola logica)
// ---------------------------------------------------------------------------
static void testHostPlanner() {
    using core::planning::MotionPlanner;
    using core::planning::PlannedBlock;
    const core::planning::MachineLimits limits; // valori di default = MotionConfig.hpp del firmware

    // --- cerchio di raggio 20 mm in segmenti da 0,2 mm a 60 mm/s ---
    {
        MotionPlanner planner;
        planner.configure(limits);
        std::vector<PlannedBlock> blocks;
        const int segments = 628;
        long long at[2] = {20 * 80, 0};
        for (int i = 1; i <= segments; ++i) {
            const double angle = 2.0 * 3.14159265358979323846 * i / segments;
            const long long next[2] = {std::llround(20.0 * std::cos(angle) * 80.0),
                                       std::llround(20.0 * std::sin(angle) * 80.0)};
            const long long steps[4] = {next[0] - at[0], next[1] - at[1], 0, 0};
            at[0] = next[0];
            at[1] = next[1];
            planner.push(steps, 3600.0, blocks);
            // un segmento esce solo quando il suo piano e' definitivo: mai tutti subito
            if (i == 5) CHECK(blocks.empty());
        }
        CHECK(planner.pending() > 0);
        planner.flush(blocks);
        CHECK(planner.pending() == 0);
        CHECK(blocks.size() == static_cast<size_t>(segments));

        bool continuous = true, physical = true;
        double slowestMid = 1e9;
        for (size_t i = 0; i < blocks.size(); ++i) {
            const PlannedBlock &b = blocks[i];
            if (i + 1 < blocks.size() && std::fabs(b.exit - blocks[i + 1].entry) > 1e-9) continuous = false;
            // nessun cambio di velocita' oltre quello che l'accelerazione consente sulla lunghezza del blocco
            const double room = 2.0 * 1000.0 * b.length * 1.02 + 0.3;
            if (std::fabs(b.exit * b.exit - b.entry * b.entry) > room) physical = false;
            if (b.entry > b.nominal + 1e-9 || b.exit > b.nominal + 0.26) physical = false;
            if (i > blocks.size() / 3 && i < blocks.size() * 2 / 3) slowestMid = std::min(slowestMid, b.entry);
        }
        CHECK(continuous);
        CHECK(physical);
        CHECK(blocks.front().entry == 0.0 && blocks.back().exit == 0.0);
        // in curva non ci si ferma a ogni segmento: la velocita' resta alta
        CHECK_MSG(slowestMid > 40.0, "slowest mid-curve entry " + std::to_string(slowestMid));
    }

    // --- spigolo a 90 gradi: quasi fermo; proseguimento rettilineo: a velocita' piena ---
    {
        MotionPlanner planner;
        planner.configure(limits);
        std::vector<PlannedBlock> blocks;
        const long long east[4] = {2400, 0, 0, 0}, north[4] = {0, 2400, 0, 0};
        planner.push(east, 3600.0, blocks);
        planner.push(east, 3600.0, blocks);
        planner.push(north, 3600.0, blocks);
        planner.flush(blocks);
        CHECK(blocks.size() == 3);
        if (blocks.size() == 3) {
            CHECK_MSG(blocks[1].entry > 59.0, "straight junction " + std::to_string(blocks[1].entry));
            CHECK_MSG(blocks[2].entry < 12.0 && blocks[2].entry > 1.0, "corner " + std::to_string(blocks[2].entry));
        }
    }

    // --- linear advance: una linea estrusa di 50 mm ---
    {
        const long long line[4] = {4000, 0, 0, 160}; // 50 mm, 2 mm di filamento
        std::vector<PlannedBlock> plain, advanced;

        MotionPlanner planner;
        planner.configure(limits);
        planner.push(line, 3600.0, plain);
        planner.flush(plain);
        CHECK(plain.size() == 1); // senza linear advance resta un blocco solo

        planner.setLinearAdvance(0.5);
        planner.push(line, 3600.0, advanced);
        planner.flush(advanced);
        CHECK_MSG(advanced.size() == 3, "blocks " + std::to_string(advanced.size())); // accelera, costante, frena
        if (advanced.size() == 3) {
            long long x = 0, e = 0;
            for (const auto &b: advanced) {
                x += b.move.steps[0];
                e += b.move.steps[3];
            }
            // la geometria e il filamento totale non cambiano: cambia solo quando viene spinto
            CHECK(x == 4000);
            CHECK_MSG(e == 160, "total E " + std::to_string(e));
            const auto surplus = [](const PlannedBlock &b) {
                return b.move.steps[3] - b.move.steps[0] * 160.0 / 4000.0;
            };
            // K * estrusione per mm * velocita' di crociera * step/mm, con l'accelerazione limitata dal salto ammesso
            const double expected = 0.5 * (2.0 / 50.0) * advanced[1].entry * 80.0;
            CHECK_MSG(std::fabs(surplus(advanced[0]) - expected) < 2.0,
                      "accel surplus " + std::to_string(surplus(advanced[0])) + " expected " + std::to_string(expected));
            CHECK_MSG(std::fabs(surplus(advanced[1])) < 1.0, "cruise surplus " + std::to_string(surplus(advanced[1])));
            CHECK_MSG(std::fabs(surplus(advanced[2]) + expected) < 2.0, "decel surplus " + std::to_string(surplus(advanced[2])));
            CHECK(advanced[0].exit > 30.0 && advanced[2].exit == 0.0);
            // con K alto l'accelerazione viene ridotta: il salto di velocita' dell'estrusore resta entro 5 mm/s
            CHECK(advanced[0].move.acceleration <= 250 && advanced[0].move.acceleration >= 200);
        }

        // uno spostamento senza estrusione non viene toccato
        std::vector<PlannedBlock> travel;
        const long long hop[4] = {4000, 0, 0, 0};
        planner.push(hop, 6000.0, travel);
        planner.flush(travel);
        CHECK(travel.size() == 1 && travel[0].move.steps[3] == 0);
    }
}

// ---------------------------------------------------------------------------
// Il firmware deve potersi fermare entro i blocchi in coda, qualunque cosa dica l'host
// ---------------------------------------------------------------------------
static void testStopGuarantee() {
    normalize();
    CHECK(driver->system()->startPrint().isSuccess());

    // Un host "bugiardo": 30 segmenti da 0,5 mm, ognuno dichiarato percorribile a 100 mm/s
    // gia' dall'ingresso, e poi piu' niente (nessun segmento di frenata).
    core::MoveFrame::PlannedMove move;
    move.steps[0] = 40;
    move.nominalSpeed = 100 * 64;
    move.acceleration = 1000;
    move.stepScale = 80 * 16;
    move.entryQuarter = 100 * 4;

    sim::recordIntervals(true);
    bool sent = true;
    for (int i = 0; i < 30 && sent; ++i) sent = driver->sendMove(move, i == 0, true).isSuccess();
    CHECK(sent);
    CHECK(idle());
    sim::recordIntervals(false);
    CHECK(sim::steps().x == 30 * 40); // nessuno step perso

    const auto rates = recordedRates();
    CHECK(rates.size() >= 1190);
    if (rates.size() >= 1190) {
        double fastest = 0.0, worst = 0.0;
        size_t worstAt = 0;
        for (const double r: rates) fastest = std::max(fastest, r);
        // Finestre di 64 step: a 8000 step/s un tick di timer vale 32 step/s, e su finestre
        // piu' corte la quantizzazione dell'intervallo pesa piu' dell'accelerazione misurata.
        for (size_t i = 0; i + 64 < rates.size(); ++i) {
            double dt = 0.0;
            for (size_t k = i; k < i + 64; ++k) dt += 1.0 / rates[k];
            const double here = std::fabs(rates[i + 64] - rates[i]) / dt;
            if (here > worst) {
                worst = here;
                worstAt = i;
            }
        }
        std::string around;
        for (size_t i = worstAt; i <= worstAt + 64 && i < rates.size(); i += 4) {
            around += std::to_string(static_cast<int>(rates[i])) + " ";
        }
        // parte da fermo e arriva da fermo, sempre entro l'accelerazione (80000 step/s^2)
        CHECK_MSG(rates.front() < 600.0, "first rate " + std::to_string(rates.front()));
        CHECK_MSG(rates.back() < 600.0, "last rate " + std::to_string(rates.back()));
        CHECK_MSG(worst < 80000.0 * 1.10, "peak acceleration (steps/s^2) " + std::to_string(worst) +
                                          " at step " + std::to_string(worstAt) + ": " + around);
        CHECK_MSG(fastest > 6000.0 && fastest < 8010.0, "fastest rate " + std::to_string(fastest));
    }

    // Valori assurdi (velocita' e accelerazione fuori scala): il firmware li limita, non li esegue
    normalize();
    CHECK(driver->system()->startPrint().isSuccess());
    move.nominalSpeed = 65535;
    move.acceleration = 65535;
    move.entryQuarter = 4095;
    move.steps[0] = 2000;
    sim::recordIntervals(true);
    CHECK(driver->sendMove(move, true, false).isSuccess());
    CHECK(idle());
    sim::recordIntervals(false);
    CHECK(sim::steps().x == 2000);
    double fastest = 0.0;
    for (const double r: recordedRates()) fastest = std::max(fastest, r);
    CHECK_MSG(fastest <= 10010.0, "fastest rate " + std::to_string(fastest)); // MAX_STEP_RATE

    // Un movimento senza piano dopo un riavvio (velocita' mai ricevute) viene rifiutato
    // (lo scenario "recover after firmware reboot" verifica che il driver si riallinei da solo)
    core::MoveFrame::PlannedMove wide;
    wide.steps[2] = 40000; // oltre gli step ammessi in un blocco
    wide.nominalSpeed = 64;
    wide.acceleration = 100;
    wide.stepScale = 400 * 16;
    const auto refused = driver->sendMove(wide, true, false);
    CHECK(refused.isRejected());
    CHECK_MSG(refused.message.find("RANGE") != std::string::npos, refused.message);
    CHECK(idle());
    CHECK(sim::steps().z == 0);
}

static void testGeometry() {
    using Point = MotionDispatcher::Point;

    // Archi: scostamento massimo dalla circonferenza entro la tolleranza
    for (double radius: {0.5, 5.0, 50.0, 200.0}) {
        const Point center{0.0, 0.0};
        const Point start{radius, 0.0};
        const Point end{0.0, radius};
        const auto points = MotionDispatcher::flattenArc(start, end, center, false);

        double worst = 0.0;
        Point previous = start;
        for (const auto &p: points) {
            const double mid = std::hypot((previous.x + p.x) / 2, (previous.y + p.y) / 2);
            worst = std::max(worst, std::fabs(radius - mid));
            CHECK(p.x >= -1e-9 && p.y >= -1e-9); // primo quadrante: verso antiorario corretto
            previous = p;
        }
        CHECK_MSG(worst <= MotionDispatcher::CURVE_TOLERANCE_MM * 1.01,
                  "r=" + std::to_string(radius) + " err=" + std::to_string(worst));
        CHECK(points.back().x == end.x && points.back().y == end.y);
    }

    // Cerchio completo: inizio == fine
    const auto circle = MotionDispatcher::flattenArc({10.0, 0.0}, {10.0, 0.0}, {5.0, 0.0}, true);
    double length = 0.0;
    Point previous{10.0, 0.0};
    for (const auto &p: circle) {
        length += std::hypot(p.x - previous.x, p.y - previous.y);
        previous = p;
    }
    CHECK_MSG(std::fabs(length - 2 * 3.14159265 * 5.0) < 0.05, "circle length " + std::to_string(length));
    CHECK(circle.size() > 4 && circle[0].y < 0.0); // orario: parte verso il basso

    // Bezier: la spezzata resta entro la tolleranza dalla curva campionata fitta
    const Point p0{0, 0}, p1{0, 20}, p2{30, 20}, p3{30, 0};
    const auto spline = MotionDispatcher::flattenBezier(p0, p1, p2, p3);
    std::vector<Point> polyline{p0};
    polyline.insert(polyline.end(), spline.begin(), spline.end());

    double worst = 0.0;
    for (int i = 0; i <= 4000; ++i) {
        const double t = i / 4000.0, u = 1 - t;
        const Point c{u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x,
                      u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y};
        double best = 1e9;
        for (size_t s = 0; s + 1 < polyline.size(); ++s) {
            const Point a = polyline[s], b = polyline[s + 1];
            const double dx = b.x - a.x, dy = b.y - a.y;
            const double k = std::clamp(((c.x - a.x) * dx + (c.y - a.y) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
            best = std::min(best, std::hypot(c.x - (a.x + k * dx), c.y - (a.y + k * dy)));
        }
        worst = std::max(worst, best);
    }
    CHECK_MSG(worst <= MotionDispatcher::CURVE_TOLERANCE_MM * 1.01, "bezier err=" + std::to_string(worst));
    CHECK(spline.back().x == p3.x && spline.back().y == p3.y);

    CHECK(core::utils::formatFloat(12.3456, 3) == "12.346");
    CHECK(core::utils::formatFloat(-0.0001, 3) == "0");
    CHECK(core::utils::formatFloat(100.0, 3) == "100");
}

static void testGCodeTranslation() {
    normalize();

    auto translator = std::make_shared<translator::gcode::GCodeTranslator>(driver);
    translator->registerDispatcher(std::make_unique<MotionDispatcher>(driver));
    translator->registerDispatcher(std::make_unique<translator::gcode::SystemDispatcher>(driver));
    translator->registerDispatcher(std::make_unique<translator::gcode::TemperatureDispatcher>(driver));

    const auto run = [&](const std::string &line) {
        try {
            translator->parseLine(line);
            return idle();
        } catch (const std::exception &e) {
            std::printf("    gcode '%s' threw: %s\n", line.c_str(), e.what());
            return false;
        }
    };

    // Finecorsa a 5 mm (X, Y) e 1 mm (Z) dalla posizione attuale
    sim::setEndstop(0, -400);
    sim::setEndstop(1, -400);
    sim::setEndstop(2, -400);
    CHECK(run("G28"));
    CHECK(driver->getState() == core::PrintState::Idle);
    CHECK(sim::steps().x == -300 && sim::steps().y == -300 && sim::steps().z == -350); // contatto + rimbalzo
    sim::clearEndstops();
    CHECK(run("G92 X0 Y0 Z0"));
    sim::resetSteps();

    CHECK(run("G90"));
    CHECK(run("G01 X10 Y10 F6000")); // "G01" = "G1"; entra da solo in stato Printing
    CHECK(sim::steps().x == 800 && sim::steps().y == 800);
    CHECK(driver->getState() == core::PrintState::Printing);

    // Commento che sembra un parametro: non deve estrudere 3 mm
    CHECK(run("G1 X12 ; E3D nozzle"));
    CHECK(sim::steps().e == 0);
    CHECK(run("G1 X10"));

    // Arco orario (10,10) -> (20,10), centro (15,10), con estrusione di 1 mm
    CHECK(run("M83"));
    sim::recordPath(true);
    CHECK(run("G2 X20 Y10 I5 J0 E1.0"));
    sim::recordPath(false);
    CHECK(sim::steps().x == 1600 && sim::steps().y == 800);
    CHECK_MSG(std::labs(sim::steps().e - 80) <= 1, "E steps " + std::to_string(sim::steps().e));

    double worst = 0.0, top = 0.0;
    for (const auto &[x, y]: sim::path()) {
        const double px = x / 80.0, py = y / 80.0;
        worst = std::max(worst, std::fabs(std::hypot(px - 15.0, py - 10.0) - 5.0));
        top = std::max(top, py);
    }
    CHECK_MSG(worst <= 0.045, "arc deviation mm: " + std::to_string(worst)); // tolleranza + quantizzazione step
    CHECK_MSG(std::fabs(top - 15.0) < 0.05, "arc apex " + std::to_string(top)); // orario = semicerchio superiore

    // Spline cubica (20,10) -> (30,10)
    CHECK(run("G5 X30 Y10 I0 J5 P0 Q5"));
    CHECK(sim::steps().x == 2400 && sim::steps().y == 800);

    // Ritrazione senza assi: prima veniva scartata come "invalid"
    const long eBefore = sim::steps().e;
    CHECK(run("G1 E-2 F1800"));
    CHECK(sim::steps().e - eBefore == -160);

    // Relativo
    CHECK(run("G91"));
    CHECK(run("G1 X-5 F6000"));
    CHECK(sim::steps().x == 2000);
    CHECK(run("G90"));

    // G92 allinea le coordinate del firmware
    CHECK(run("G92 X0 Y0"));
    CHECK(run("G1 X1 F6000"));
    CHECK(sim::steps().x == 2080);

    // Nuovo homing a stampa in corso: il driver esce da Printing prima di S0
    sim::setEndstop(0, sim::steps().x - 80);
    sim::setEndstop(1, sim::steps().y - 80);
    sim::setEndstop(2, sim::steps().z - 80);
    CHECK(run("G28"));
    CHECK(driver->getState() == core::PrintState::Idle);
}

static void testPreviewAndEstimate() {
    normalize();

    // --- cio' che il firmware dichiara deve arrivare intero: una riga tagliata dava coda = 1 ---
    CHECK(driver->system()->startPrint().isSuccess());
    CHECK(driver->motion()->moveRelative({0.1, std::nullopt, std::nullopt, std::nullopt, 3000.0}).isSuccess());
    CHECK(idle());
    {
        const auto info = driver->motion()->machineInfo();
        CHECK(info.valid);
        CHECK_MSG(info.limits.queueBlocks == 15, "queue " + std::to_string(info.limits.queueBlocks));
        CHECK_MSG(info.receiveBuffer == 128, "rx " + std::to_string(info.receiveBuffer));
        CHECK(info.limits.maxStepRate == 10000.0 && std::fabs(info.limits.junctionDeviation - 0.02) < 1e-9);
        CHECK(std::fabs(info.travelMm[0] - 220.0) < 1e-6 && std::fabs(info.travelMm[2] - 200.0) < 1e-6);
    }
    normalize();
    std::filesystem::create_directories("gcode");
    const std::string path = "gcode/preview.gcode";
    {
        std::ofstream out(path);
        out << "; percorso di prova\n"
               "G90\n"
               "G92 E0\n"
               "G1 X40 Y0 F3000\n"
               "G1 X40 Y40 E2\n"
               "G2 X0 Y40 I-20 J0 E5 ; mezzo cerchio\n"
               "\n"
               "G1 X0 Y5 F6000\n";
        // tratti corti a zig-zag: qui decide la regola di arresto, non la velocita' richiesta
        for (int i = 1; i <= 60; ++i) {
            out << "G1 X" << i * 0.5 << " Y" << (i % 2 ? 5.2 : 5.0) << " E" << 5.0 + i * 0.02 << " F2400\n";
        }
        out << "G1 E4 F1800 ; ritrazione\n"
               "G1 X0 Y0 F6000\n";
    }

    // --- anteprima: stesso percorso che fara' il driver ---
    const core::planning::MachineLimits limits; // default = MotionConfig.hpp del firmware
    const auto preview = translator::gcode::buildPreview(path, limits, 0.0);
    CHECK(preview.readable);
    CHECK_MSG(preview.points.size() > 80, std::to_string(preview.points.size())); // l'arco e' spezzato in corde
    const auto &last = preview.points.back();
    CHECK(std::fabs(last.x) < 1e-3 && std::fabs(last.y) < 1e-3);
    CHECK(preview.points[1].command == 3 && !preview.points[1].extruding); // "G1 X40 Y0" e' il terzo comando
    CHECK(preview.points[2].extruding);
    CHECK(last.command == 68 && !last.extruding); // 5 comandi, 60 tratti corti, ritrazione, rientro
    bool ordered = true;
    for (size_t i = 1; i < preview.points.size(); ++i) {
        if (preview.points[i].seconds < preview.points[i - 1].seconds ||
            preview.points[i].command < preview.points[i - 1].command) ordered = false;
    }
    CHECK(ordered);
    CHECK(std::fabs(preview.points.back().seconds - preview.seconds()) < 1e-6);
    CHECK(preview.heatWaits == 0 && preview.homings == 0);
    // a velocita' costante sarebbero ~5,3 s: accelerazioni e tratti corti lo allungano
    CHECK_MSG(preview.motionSeconds > 5.5 && preview.motionSeconds < 12.0, std::to_string(preview.motionSeconds));

    // --- regolazioni dell'operatore: meta' velocita', poi un tetto ai movimenti al secondo ---
    {
        const auto half = translator::gcode::buildPreview(path, limits, 0.0, 0.5, 0.0);
        CHECK_MSG(half.motionSeconds > preview.motionSeconds * 1.4 && half.motionSeconds < preview.motionSeconds * 2.1,
                  std::to_string(half.motionSeconds));
        // 10 movimenti al secondo: i 60 tratti corti da soli chiedono almeno 6 secondi
        const auto capped = translator::gcode::buildPreview(path, limits, 0.0, 1.0, 10.0);
        CHECK_MSG(capped.motionSeconds > preview.motionSeconds + 5.0, std::to_string(capped.motionSeconds));
        CHECK(capped.points.size() == preview.points.size()); // stesso percorso, solo piu' lento

        // lo stesso tetto, visto sul piano: nessun blocco dura meno di un decimo di secondo
        core::planning::MotionPlanner planner;
        planner.configure(limits);
        planner.setBlockRate(10.0);
        std::vector<core::planning::PlannedBlock> blocks;
        for (int i = 0; i < 40; ++i) {
            const long long steps[4] = {40, 0, 0, 0}; // 0,5 mm
            planner.push(steps, 6000.0, blocks);
        }
        planner.flush(blocks);
        CHECK(blocks.size() == 40);
        bool slowEnough = true;
        for (const auto &b: blocks) {
            if (b.nominal > 0.5 * 10.0 + 1e-9) slowEnough = false; // 0,5 mm in almeno 0,1 s
        }
        CHECK(slowEnough);
    }

    // --- lo stesso file sulla macchina simulata: la durata decisa dal firmware ---
    auto translator = std::make_shared<translator::gcode::GCodeTranslator>(driver);
    translator->registerDispatcher(std::make_unique<MotionDispatcher>(driver));
    translator->registerDispatcher(std::make_unique<translator::gcode::SystemDispatcher>(driver));

    sim::recordIntervals(true);
    {
        std::ifstream in(path);
        std::string line;
        bool ok = true;
        while (ok && std::getline(in, line)) {
            if (line.empty() || line[0] == ';') continue;
            try {
                translator->parseLine(line);
            } catch (const std::exception &e) {
                ok = false;
                CHECK_MSG(false, line + ": " + e.what());
            }
        }
        driver->flushMoves();
    }
    CHECK(idle());
    sim::recordIntervals(false);
    double executed = 0.0;
    for (const uint16_t ticks: sim::intervals()) executed += ticks * 0.5e-6;

    CHECK_MSG(sim::steps().x == 0 && sim::steps().y == 0, "x=" + std::to_string(sim::steps().x));
    CHECK_MSG(sim::steps().e == 4 * 80, "e=" + std::to_string(sim::steps().e));
    // la stima deve stare entro il 5% di quanto il firmware ha davvero impiegato
    CHECK_MSG(std::fabs(preview.motionSeconds - executed) < 0.05 * executed,
              "estimated " + std::to_string(preview.motionSeconds) + " s, executed " + std::to_string(executed) + " s");
    std::printf("    estimate %.3f s, firmware %.3f s\n", preview.motionSeconds, executed);

    // --- filamento: i recuperi dopo una ritrazione non sono consumo ---
    {
        std::ofstream out("gcode/filament.gcode");
        out << "G28\nG90\nG1 X10 E5 F1200\nG1 E4\nG1 E5\nG1 X20 E8\nG92 E0\nG1 X30 E2\n";
    }
    const auto report = translator::gcode::checkFile("gcode/filament.gcode", 220.0, 220.0, 200.0);
    CHECK_MSG(std::fabs(report.filamentMm - 10.0) < 1e-6, std::to_string(report.filamentMm));
}

static void testQueueStopsOnFault() {
    normalize();

    auto translator = std::make_shared<translator::gcode::GCodeTranslator>(driver);
    translator->registerDispatcher(std::make_unique<MotionDispatcher>(driver));
    core::CommandExecutorQueue queue(translator);

    std::atomic<int> faults{0};
    std::atomic<int> finished{0};
    queue.setJobFaultCallback([&](const std::string &, const std::string &) { ++faults; });
    queue.setJobFinishedCallback([&](const std::string &) { ++finished; });

    // --- pausa: nessun comando viene inviato ne' perso ---
    std::vector<std::string> lines{"G90"};
    // 80 righe: piu' di quante ne assorbono insieme il piano dell'host (10), i frame in volo (8)
    // e la coda del firmware (15), altrimenti alla pausa la coda del driver sarebbe gia' vuota
    for (int i = 1; i <= 80; ++i) lines.push_back("G1 X" + std::to_string(i) + " F6000");
    queue.enqueueCommands(lines, 3, "job-pause");

    CHECK(waitFor([&] { return sim::steps().x >= 400; }, 5s));
    queue.setPaused(true);
    std::this_thread::sleep_for(300ms);
    const int sentWhilePaused = port->commandsSent();
    std::this_thread::sleep_for(400ms);
    CHECK(port->commandsSent() == sentWhilePaused);
    CHECK(queue.getQueueSize() > 0);

    queue.setPaused(false);
    CHECK(waitFor([&] { return finished.load() == 1; }, 10s));
    CHECK(idle());
    CHECK(sim::steps().x == 6400); // tutti e 80 i movimenti eseguiti
    CHECK(faults.load() == 0);

    // --- guasto: la coda si svuota e il job fallisce, niente invii alla cieca ---
    lines.clear();
    for (int i = 0; i < 200; ++i) lines.push_back("G1 X" + std::to_string(i % 2 ? 31 : 30) + " F6000");
    queue.enqueueCommands(lines, 3, "job-fault");

    std::this_thread::sleep_for(200ms);
    sim::freeze(true);
    CHECK(waitFor([&] { return faults.load() == 1; }, 8s));
    CHECK(queue.getQueueSize() == 0);
    sim::freeze(false);

    std::this_thread::sleep_for(300ms);
    const int sentAfterFault = port->commandsSent();
    std::this_thread::sleep_for(500ms);
    CHECK(port->commandsSent() == sentAfterFault);
    CHECK(finished.load() == 1);

    queue.stop();
}


static void testHomingAndEndstops() {
    normalize();

    // --- homing riuscito: zero sul finecorsa, poi rimbalzo ---
    sim::setEndstop(0, -800);
    sim::setEndstop(1, -800);
    sim::setEndstop(2, -400);
    auto result = driver->system()->homing();
    CHECK_MSG(result.isSuccess(), result.message);
    CHECK(contains(result, "HOM X OK") && contains(result, "HOM Y OK") && contains(result, "HOM Z OK"));
    CHECK(sim::steps().x == -700 && sim::steps().y == -700 && sim::steps().z == -350);

    bool referenced = false;
    auto position = driver->motion()->getPosition(&referenced);
    CHECK(referenced);
    CHECK(position && std::fabs(position->x - 1.25) < 0.001 && std::fabs(position->z - 0.125) < 0.001);

    // --- solo X: gli altri assi non si muovono ---
    CHECK(driver->system()->startPrint().isSuccess());
    CHECK(driver->motion()->moveAbsolute({20.0, 20.0, std::nullopt, std::nullopt, 6000.0}).isSuccess());
    CHECK(idle());
    CHECK(driver->system()->endPrint().isSuccess());
    const long yBefore = sim::steps().y;
    result = driver->system()->homeAxes("X");
    CHECK_MSG(result.isSuccess(), result.message);
    CHECK(sim::steps().x == -700 && sim::steps().y == yBefore);

    // --- finecorsa urtato durante la stampa: arresto e stato di errore ---
    // Con la posizione riferita i limiti di corsa lo impediscono: puo' succedere solo
    // se il riferimento e' andato perso (qui: motori spenti da un arresto di emergenza).
    CHECK(driver->motion()->emergencyStop().isSuccess());
    CHECK(driver->system()->startPrint().isSuccess());
    CHECK(driver->motion()->moveAbsolute({-5.0, std::nullopt, std::nullopt, std::nullopt, 3000.0}).isSuccess());
    CHECK(waitFor([&] {
        return contains(driver->system()->errorStatus(), "ENDSTOP");
    }, 3s));
    CHECK(sim::steps().x > -1300); // fermato sul finecorsa, non a destinazione (-1200 - ...)
    result = driver->motion()->moveRelative({1.0, std::nullopt, std::nullopt, std::nullopt, 1000.0});
    CHECK(result.isBusy());
    CHECK(driver->system()->clearError().isSuccess());

    // --- finecorsa che non scatta mai: l'homing fallisce invece di girare all'infinito ---
    sim::clearEndstops();
    sim::setTimeScale(20.0);
    result = driver->system()->homeAxes("X");
    sim::setTimeScale(1.0);
    CHECK(result.isRejected());
    CHECK(contains(result, "HOM X FAIL"));
    CHECK(contains(driver->system()->printStatus(), "STA Idle"));
}

// ---------------------------------------------------------------------------
// Job di stampa completi, attraverso PrintJobManager
// ---------------------------------------------------------------------------
namespace {
    constexpr int LAYERS = 3;
    constexpr double E_PER_MOVE = 0.5;

    /// Piccola stampa: per ogni layer due rette, un arco e una spline, tutti con estrusione.
    std::string writePrintFile() {
        std::filesystem::create_directories("gcode");
        const std::string path = "gcode/test_print.gcode";
        std::ofstream out(path);
        out << "; stampa di prova\nG28\nG90\nM82\nG92 E0\nM106 S200\nG1 X10 Y10 F6000 ; avvicinamento\n";
        double e = 0.0;
        for (int layer = 0; layer < LAYERS; ++layer) {
            out << ";LAYER:" << layer << "\n";
            out << "G1 Z" << 0.2 * (layer + 1) << " F600\n";
            out << "G1 X30 Y10 E" << (e += E_PER_MOVE) << " F2400\n";
            out << "G1 X30 Y30 E" << (e += E_PER_MOVE) << "\n";
            out << "G2 X10 Y30 I-10 J0 E" << (e += E_PER_MOVE) << " ; arco\n";
            out << "G5 X10 Y10 I-5 J-5 P-5 Q5 E" << (e += E_PER_MOVE) << " ; spline\n";
        }
        out << "G1 E" << (e - 1.0) << " F1800 ; ritrazione finale\nM107\nG1 X5 Y5 F6000\n";
        return path;
    }

    struct JobRig {
        std::shared_ptr<translator::gcode::GCodeTranslator> translator;
        std::shared_ptr<core::CommandExecutorQueue> queue;
        std::shared_ptr<core::print::PrintJobManager> manager;

        JobRig() {
            translator = std::make_shared<translator::gcode::GCodeTranslator>(driver);
            translator->registerDispatcher(std::make_unique<MotionDispatcher>(driver));
            translator->registerDispatcher(std::make_unique<translator::gcode::SystemDispatcher>(driver));
            translator->registerDispatcher(std::make_unique<translator::gcode::TemperatureDispatcher>(driver));
            translator->registerDispatcher(std::make_unique<translator::gcode::FanDispatcher>(driver));
            translator->registerDispatcher(std::make_unique<translator::gcode::CompatibilityDispatcher>(driver));
            queue = std::make_shared<core::CommandExecutorQueue>(translator);
            manager = std::make_shared<core::print::PrintJobManager>(driver, queue);
        }

        ~JobRig() { queue->stop(); }

        bool waitState(core::print::JobState state, std::chrono::milliseconds timeout) {
            return waitFor([&] { return manager->getCurrentState() == state; }, timeout);
        }
    };

    // La macchina simulata ha i finecorsa in un punto fisso: ogni job si azzera li',
    // quindi le posizioni finali dei diversi job sono confrontabili passo per passo.
    void prepareMachine() {
        static bool endstopsPlaced = false;
        sim::setTimeScale(1.0);
        driver->system()->clearError();
        driver->waitForIdle(60s);
        driver->system()->endPrint();
        if (!endstopsPlaced) {
            sim::resetSteps();
            endstopsPlaced = true;
        }
        sim::setEndstop(0, -800);
        sim::setEndstop(1, -800);
        sim::setEndstop(2, -400);
        // stato lasciato dai test precedenti: nessuna temperatura da ripristinare
        core::state::StateTracker::getInstance().setHotendTargetTemp(0.0);
        std::error_code ec;
        std::filesystem::remove_all(core::print::JobCheckpoint::directory(), ec);
        sim::setTimeScale(4.0);
    }

    sim::Steps referenceSteps; // posizione finale di una stampa senza interruzioni
    const long E_PER_LINE = static_cast<long>(E_PER_MOVE * 80);
}

static void testShortSegmentStream() {
    normalize();
    CHECK(driver->system()->startPrint().isSuccess());

    // Curva fitta: 800 segmenti da 0,1 mm a 80 mm/s, inviati senza attendere ogni risposta.
    // Il firmware riceve piu' in fretta di quanto esegue: non deve bloccarsi ne' perdere step.
    const int before = port->framesSent();
    bool ok = true;
    for (int i = 0; i < 800 && ok; ++i) {
        const auto result = driver->motion()->moveRelative(
                {0.1, i % 2 == 0 ? 0.05 : -0.05, std::nullopt, 0.0125, i == 0 ? std::optional<double>(4800.0)
                                                                              : std::nullopt}, true);
        ok = result.isSuccess();
        if (!ok) CHECK_MSG(false, "segment " + std::to_string(i) + ": " + result.message);
    }
    CHECK(idle());
    CHECK(!driver->isLinkFaulted());
    CHECK(port->framesSent() - before == 800); // nessuna ritrasmissione
    CHECK_MSG(sim::steps().x == 6400 && sim::steps().y == 0 && sim::steps().e == 800,
              "x=" + std::to_string(sim::steps().x) + " y=" + std::to_string(sim::steps().y) +
              " e=" + std::to_string(sim::steps().e));
}

/// Invia `count` segmenti da 0,5 mm lungo X senza attendere le risposte. Ritorna false al primo errore.
static bool streamSegments(int count) {
    for (int i = 0; i < count; ++i) {
        const auto result = driver->motion()->moveRelative(
                {0.5, std::nullopt, std::nullopt, 0.025, i == 0 ? std::optional<double>(3000.0) : std::nullopt}, true);
        if (!result.isSuccess()) return false;
    }
    return true;
}

static void testStreamFaults() {
    // --- un frame arriva corrotto a meta' flusso: rispedito, nessuno step perso o doppio ---
    normalize();
    CHECK(driver->system()->startPrint().isSuccess());
    int before = port->framesSent();
    CHECK(streamSegments(20));
    port->corruptNextCommands(1);
    CHECK(streamSegments(40));
    CHECK(idle());
    CHECK(!driver->isLinkFaulted());
    CHECK(port->framesSent() - before > 60); // c'e' stata almeno una ritrasmissione
    CHECK_MSG(sim::steps().x == 60 * 40 && sim::steps().e == 60 * 2,
              "x=" + std::to_string(sim::steps().x) + " e=" + std::to_string(sim::steps().e));

    // --- un frame non arriva affatto ---
    normalize();
    CHECK(driver->system()->startPrint().isSuccess());
    CHECK(streamSegments(20));
    port->dropNextCommands(1);
    CHECK(streamSegments(40));
    CHECK(idle());
    CHECK(!driver->isLinkFaulted());
    CHECK_MSG(sim::steps().x == 60 * 40 && sim::steps().e == 60 * 2,
              "x=" + std::to_string(sim::steps().x) + " e=" + std::to_string(sim::steps().e));

    // --- una risposta va persa: il movimento era stato accettato, non va eseguito due volte ---
    normalize();
    CHECK(driver->system()->startPrint().isSuccess());
    CHECK(streamSegments(20));
    driver->flushMoves(); // i segmenti ancora trattenuti dal piano partono: da qui il conteggio e' esatto
    before = port->framesSent();
    port->dropNextFromFirmware("OK0", 1);
    CHECK(streamSegments(40));
    CHECK(idle());
    CHECK(!driver->isLinkFaulted());
    // la conferma di un movimento successivo vale anche per quello rimasto senza risposta:
    // niente attesa e niente rinvio
    CHECK(port->framesSent() - before == 40);
    CHECK_MSG(sim::steps().x == 60 * 40 && sim::steps().e == 60 * 2,
              "x=" + std::to_string(sim::steps().x) + " e=" + std::to_string(sim::steps().e));

    // --- un movimento viene rifiutato: quelli gia' in volo dietro di lui non partono ---
    normalize();
    sim::setEndstop(0, -800);
    sim::setEndstop(1, -800);
    sim::setEndstop(2, -400);
    CHECK(driver->system()->homing().isSuccess());
    CHECK(driver->system()->startPrint().isSuccess());
    const auto home = sim::steps();
    const uint64_t faults = driver->faultEpoch();
    // Il driver rifiuta da se' un movimento fuori corsa: qui i frame partono direttamente,
    // per verificare che il controllo del firmware resti l'ultima parola.
    const auto frame = [](int x, int y) {
        core::MoveFrame::PlannedMove move;
        move.steps[0] = x;
        move.steps[1] = y;
        move.nominalSpeed = 100 * 64;
        move.acceleration = 1000;
        move.stepScale = 80 * 16;
        return move;
    };
    CHECK(driver->sendMove(frame(700, 700), true, true).isSuccess()); // da 1,25 mm (dopo l'homing) a X10 Y10
    driver->sendMove(frame(23200, 0), false, true); // X a 300 mm: fuori corsa
    driver->sendMove(frame(800, 800), false, true);
    driver->sendMove(frame(800, 800), false, true);
    // Il primo comando di testo chiude i movimenti in volo e scopre il rifiuto: e' un guasto,
    // quindi l'attesa si interrompe subito invece di far finta di niente.
    CHECK(!idle());
    CHECK(driver->faultEpoch() > faults);
    CHECK(idle()); // il movimento valido accodato prima del rifiuto viene comunque completato
    CHECK_MSG(driver->lastFaultReason().find("LIMIT") != std::string::npos, driver->lastFaultReason());
    // fermo sull'ultima posizione valida: X10 Y10 dallo zero dei finecorsa
    CHECK_MSG(sim::steps().x == -800 + 800 && sim::steps().y == -800 + 800,
              "x=" + std::to_string(sim::steps().x) + " y=" + std::to_string(sim::steps().y));
    // e da li' si riparte, con la posizione riletta dal firmware
    CHECK(driver->motion()->moveAbsolute({20.0, 10.0, std::nullopt, std::nullopt, 6000.0}).isSuccess());
    CHECK(idle());
    CHECK(sim::steps().x == -800 + 1600 && sim::steps().y == -800 + 800);
    (void) home;
}

constexpr uint8_t ENABLE_PIN = 8; // LOW = motori alimentati

static void testStackGuard() {
    normalize();
    CHECK(driver->system()->startPrint().isSuccess());
    CHECK(driver->motion()->moveRelative({30.0, std::nullopt, std::nullopt, std::nullopt, 600.0}).isSuccess());
    std::this_thread::sleep_for(200ms);

    // Lo stack entra nella zona di guardia: il firmware si ferma prima di corrompere le variabili
    sim::breakStackGuard(true);
    CHECK(waitFor([&] { return sim::pinLevel(ENABLE_PIN); }, 3s)); // motori senza corrente
    const long stopped = sim::steps().x;
    std::this_thread::sleep_for(200ms);
    CHECK(sim::steps().x == stopped && stopped < 2400);
    CHECK(contains(driver->system()->errorStatus(), "STACK"));
    CHECK(driver->motion()->moveRelative({1.0, std::nullopt, std::nullopt, std::nullopt, 600.0}).isBusy());

    // Finche' la causa resta, azzerare l'errore non basta: l'allarme scatta di nuovo
    driver->system()->clearError();
    CHECK(waitFor([&] { return contains(driver->system()->errorStatus(), "STACK"); }, 2s));

    sim::breakStackGuard(false);
    CHECK(driver->system()->clearError().isSuccess());
    CHECK(driver->waitForIdle(3s));
}

namespace {
    /// Registra l'ordine in cui la coda esegue le righe, senza traffico verso il firmware.
    struct OrderProbe : translator::gcode::ICommandDispatcher {
        std::vector<int> *seen;

        explicit OrderProbe(std::vector<int> *target) : seen(target) {}

        bool canHandle(const std::string &command) const override { return command == "M9000"; }

        bool validate(const std::string &, const std::map<std::string, double> &) const override { return true; }

        void handle(const std::string &, const std::map<std::string, double> &params) override {
            seen->push_back(static_cast<int>(params.at("S")));
        }
    };
}

static void testLargeJobOrder() {
    prepareMachine();
    // Una stampa vera ha centinaia di migliaia di righe: devono essere eseguite tutte,
    // nell'ordine del file, senza tenere il file intero in memoria.
    constexpr int LINES = 40000;
    const std::string path = "gcode/large.gcode";
    {
        std::filesystem::create_directories("gcode");
        std::ofstream out(path);
        for (int i = 0; i < LINES; ++i) out << "M9000 S" << i << "\n";
    }

    std::vector<int> seen;
    seen.reserve(LINES);
    JobRig rig;
    rig.translator->registerDispatcher(std::make_unique<OrderProbe>(&seen));

    CHECK(rig.manager->startPrintJob(path, "job-large"));
    CHECK(rig.waitState(core::print::JobState::COMPLETED, 120s));
    CHECK_MSG(static_cast<int>(seen.size()) == LINES, "executed " + std::to_string(seen.size()));

    int firstWrong = -1;
    for (size_t i = 0; i < seen.size() && firstWrong < 0; ++i) {
        if (seen[i] != static_cast<int>(i)) firstWrong = static_cast<int>(i);
    }
    CHECK_MSG(firstWrong < 0, "out of order at position " + std::to_string(firstWrong) + ": got line " +
                              std::to_string(firstWrong >= 0 ? seen[firstWrong] : 0));
    CHECK(rig.queue->getQueueSize() == 0);
    std::filesystem::remove(path);
}

static void testTravelLimits() {
    normalize();
    sim::setEndstop(0, -800);
    sim::setEndstop(1, -800);
    sim::setEndstop(2, -400);
    CHECK(driver->system()->homing().isSuccess());
    CHECK(driver->system()->startPrint().isSuccess());
    const auto home = sim::steps();
    const auto at = [](double x, double y, double z) {
        return core::command::motion::MoveTarget{x, y, z, std::nullopt, 6000.0};
    };

    // Oltre il volume di lavoro (220 x 220 x 200): rifiutato, nessuno step
    auto result = driver->motion()->moveAbsolute({230.0, std::nullopt, std::nullopt, std::nullopt, 6000.0});
    CHECK(result.isRejected());
    CHECK_MSG(result.message.find("LIMIT") != std::string::npos, result.message);
    CHECK(driver->motion()->moveAbsolute({std::nullopt, -0.5, std::nullopt, std::nullopt, 6000.0}).isRejected());
    CHECK(driver->motion()->moveAbsolute({std::nullopt, std::nullopt, 200.5, std::nullopt, 600.0}).isRejected());
    // un solo asse fuori basta a rifiutare tutto il movimento
    CHECK(driver->motion()->moveAbsolute(at(10.0, 221.0, 1.0)).isRejected());
    CHECK(idle());
    CHECK(sim::steps().x == home.x && sim::steps().y == home.y && sim::steps().z == home.z);
    CHECK(!driver->isLinkFaulted()); // un rifiuto non e' un guasto del collegamento

    // I bordi sono raggiungibili, e i movimenti relativi sono controllati sulla destinazione
    sim::setTimeScale(8.0);
    CHECK(driver->motion()->moveAbsolute({220.0, 220.0, std::nullopt, std::nullopt, 6000.0}).isSuccess());
    CHECK(driver->motion()->moveRelative({0.1, std::nullopt, std::nullopt, std::nullopt, 6000.0}).isRejected());
    CHECK(driver->motion()->moveRelative({-20.0, std::nullopt, std::nullopt, std::nullopt, 6000.0}).isSuccess());
    CHECK(idle());
    sim::setTimeScale(1.0);
    CHECK(sim::steps().x == -800 + 200 * 80 && sim::steps().y == -800 + 220 * 80);

    // Posizione impostata a mano fuori dal volume: l'asse puo' rientrare, non allontanarsi
    CHECK(driver->motion()->setPosition({-10.0, std::nullopt, std::nullopt, std::nullopt, std::nullopt}).isSuccess());
    CHECK(driver->motion()->moveRelative({-1.0, std::nullopt, std::nullopt, std::nullopt, 6000.0}).isRejected());
    CHECK(driver->motion()->moveRelative({4.0, std::nullopt, std::nullopt, std::nullopt, 6000.0}).isSuccess());
    CHECK(idle());

    // Un job che esce dal volume fallisce su quella riga invece di proseguire
    CHECK(driver->system()->endPrint().isSuccess());
    std::filesystem::create_directories("gcode");
    {
        std::ofstream out("gcode/out_of_volume.gcode");
        out << "G28\nG90\nG1 X50 Y50 F6000\nG1 X300 Y50\nG1 X60 Y60\n";
    }
    {
        JobRig rig;
        sim::setTimeScale(8.0);
        CHECK(rig.manager->startPrintJob("gcode/out_of_volume.gcode", "job-limit"));
        CHECK(rig.waitState(core::print::JobState::FAILED, 60s));
        CHECK(idle());
        sim::setTimeScale(1.0);
        // fermo all'ultima posizione valida: la riga dopo quella rifiutata non e' stata eseguita
        CHECK_MSG(sim::steps().x == -800 + 50 * 80 && sim::steps().y == -800 + 50 * 80,
                  "x=" + std::to_string(sim::steps().x) + " y=" + std::to_string(sim::steps().y));
    }
    std::error_code ec;
    std::filesystem::remove_all(core::print::JobCheckpoint::directory(), ec);
}

static void testMotorEnable() {
    normalize();
    CHECK(!sim::pinLevel(ENABLE_PIN)); // alimentati dall'avvio

    // --- emergenza: oltre a fermare gli step toglie corrente ai motori ---
    CHECK(driver->system()->startPrint().isSuccess());
    CHECK(driver->motion()->moveRelative({50.0, std::nullopt, std::nullopt, std::nullopt, 3000.0}).isSuccess());
    sim::setEndstop(0, 400); // un finecorsa scatta a meta' movimento -> allarme
    CHECK(waitFor([&] { return sim::pinLevel(ENABLE_PIN); }, 5s));
    sim::clearEndstops();
    CHECK(driver->waitForIdle(3s));

    // Un motore senza corrente puo' essere spostato a mano: la posizione non e' piu' riferita
    bool referenced = true;
    CHECK(driver->motion()->getPosition(&referenced).has_value());
    CHECK(!referenced);

    // Restano spenti finche' l'errore non viene azzerato
    CHECK(driver->motion()->moveRelative({1.0, std::nullopt, std::nullopt, std::nullopt, 600.0}).isBusy());
    CHECK(sim::pinLevel(ENABLE_PIN));
    CHECK(driver->system()->clearError().isSuccess());
    CHECK(!sim::pinLevel(ENABLE_PIN));

    // --- M0 dall'host spegne i motori; il primo movimento li riaccende ---
    CHECK(driver->system()->endPrint().isSuccess());
    CHECK(driver->system()->startPrint().isSuccess());
    CHECK(driver->motion()->emergencyStop().isSuccess());
    CHECK(sim::pinLevel(ENABLE_PIN));
    sim::resetSteps();
    CHECK(driver->motion()->moveRelative({1.0, std::nullopt, std::nullopt, std::nullopt, 600.0}).isSuccess());
    CHECK(!sim::pinLevel(ENABLE_PIN));
    CHECK(idle());
    CHECK(sim::steps().x == 80);

    // --- 31 minuti di inattivita' in pausa: i motori restano alimentati e riferiti ---
    CHECK(driver->motion()->setPosition({10.0, 10.0, 1.0, std::nullopt, std::nullopt}).isSuccess());
    CHECK(driver->system()->pause().isSuccess());
    sim::advanceTime(31ULL * 60 * 1000);
    std::this_thread::sleep_for(100ms);
    CHECK(!sim::pinLevel(ENABLE_PIN));
    CHECK(driver->motion()->getPosition(&referenced).has_value());
    CHECK(referenced);
    CHECK(driver->system()->resume().isSuccess());

    // --- 31 minuti di inattivita' da fermi: motori spenti, riaccesi dal comando successivo ---
    CHECK(driver->system()->endPrint().isSuccess());
    sim::advanceTime(31ULL * 60 * 1000);
    CHECK(waitFor([&] { return sim::pinLevel(ENABLE_PIN); }, 2s));
    CHECK(driver->motion()->getPosition(&referenced).has_value());
    CHECK(!referenced);
    CHECK(!sim::pinLevel(ENABLE_PIN));
}

static void testFullPrintJob() {
    prepareMachine();
    const std::string path = writePrintFile();
    JobRig rig;

    CHECK(rig.manager->startPrintJob(path, "job-full"));
    CHECK(rig.waitState(core::print::JobState::COMPLETED, 120s));
    CHECK(idle());

    referenceSteps = sim::steps();
    // X5 Y5 e Z 0.6 rispetto allo zero dei finecorsa (-800, -800, -400)
    CHECK_MSG(referenceSteps.x == -800 + 400 && referenceSteps.y == -800 + 400,
              "x=" + std::to_string(referenceSteps.x) + " y=" + std::to_string(referenceSteps.y));
    CHECK_MSG(referenceSteps.z == -400 + 240, "z=" + std::to_string(referenceSteps.z));
    // 12 movimenti da 0.5 mm, meno 1 mm di ritrazione finale = 5 mm = 400 step
    CHECK_MSG(referenceSteps.e == 400, "e=" + std::to_string(referenceSteps.e));

    CHECK(driver->getState() == core::PrintState::Idle);                 // il firmware e' tornato Idle
    CHECK(!core::print::JobCheckpoint::load("job-full").has_value());    // niente da riprendere
    CHECK(!driver->isLinkFaulted());

    // Un secondo job parte senza interventi manuali
    CHECK(rig.manager->startPrintJob(path, "job-second"));
    CHECK(rig.waitState(core::print::JobState::COMPLETED, 120s));
    CHECK(idle());
    CHECK(sim::steps().x == referenceSteps.x && sim::steps().e == 2 * referenceSteps.e);
}

static void testSlicedFile() {
    // File prodotto da PrusaSlicer 2.9 con il profilo config/slicer/3dp-prusaslicer.ini:
    // un anello di 5 strati, con archi veri (G2/G3), riscaldamento, ventola e ritrazioni.
    const char *fixtures = std::getenv("THREEDP_FIXTURES");
    const std::string path = std::string(fixtures ? fixtures : "tests/fixtures") + "/prusaslicer_ring.gcode";

    // --- controllo preventivo: tutto cio' che lo slicer scrive e' gestito ---
    const auto report = translator::gcode::checkFile(path, 220.0, 220.0, 200.0);
    CHECK_MSG(report.printable(), report.problems.empty() ? "unreadable" : report.problems.front());
    CHECK(report.unknown.empty() && report.fatal.empty());
    CHECK(report.arcs > 100 && report.layers == 5 && report.homes);
    CHECK(report.maxHotend == 210.0);

    // --- stampa completa sulla macchina simulata, hotend compreso ---
    prepareMachine();
    sim::enableThermalModel(true, 25.0);
    sim::enableThermistor(true);
    sim::setTimeScale(12.0);
    {
        JobRig rig;
        const uint64_t faults = driver->faultEpoch();
        const long eStart = sim::steps().e;
        CHECK(rig.manager->startPrintJob(path, "job-sliced"));
        CHECK_MSG(rig.waitState(core::print::JobState::COMPLETED, 300s),
                  "job state after timeout, fault: " + driver->lastFaultReason());
        CHECK(idle());
        CHECK(driver->faultEpoch() == faults);
        CHECK(!driver->isLinkFaulted());

        // G-code di fine stampa: pezzo presentato a X5 Y200, ugello 5 mm sopra l'ultimo strato (1 mm)
        const auto end = sim::steps();
        CHECK_MSG(end.x == -800 + 5 * 80 && end.y == -800 + 200 * 80,
                  "x=" + std::to_string(end.x) + " y=" + std::to_string(end.y));
        CHECK_MSG(end.z == -400 + 6 * 400, "z=" + std::to_string(end.z));
        CHECK(end.e - eStart > 100 * 80); // oltre 100 mm di filamento estrusi

        // heater spento dal G-code di fine stampa
        std::this_thread::sleep_for(300ms);
        CHECK(!sim::pinLevel(15 /* A1: heater */));
    }
    sim::setTimeScale(1.0);
    sim::enableThermistor(false);
    sim::enableThermalModel(false);
    std::error_code ec;
    std::filesystem::remove_all(core::print::JobCheckpoint::directory(), ec);
}

static void testPauseAndResume() {
    prepareMachine();
    const std::string path = writePrintFile();
    JobRig rig;
    const long eStart = sim::steps().e;

    CHECK(rig.manager->startPrintJob(path, "job-pause"));
    CHECK(waitFor([&] { return sim::steps().e - eStart > 3 * E_PER_LINE; }, 60s)); // a stampa avviata

    CHECK(rig.manager->pauseJob());
    CHECK(rig.manager->getCurrentState() == core::print::JobState::PAUSED);
    CHECK(driver->getState() == core::PrintState::Paused);

    // In pausa: macchina ferma, ugello sollevato di 5 mm e filamento ritratto di 2 mm
    const auto parked = sim::steps();
    std::this_thread::sleep_for(400ms);
    CHECK(sim::steps().x == parked.x && sim::steps().z == parked.z && sim::steps().e == parked.e);
    CHECK(rig.queue->getQueueSize() > 0);                             // il resto del job e' ancora in coda
    CHECK(core::print::JobCheckpoint::load("job-pause").has_value()); // e il punto di ripresa e' su disco

    CHECK(rig.manager->resumeJob());
    CHECK(rig.waitState(core::print::JobState::COMPLETED, 120s));
    CHECK(idle());

    // Identico a una stampa mai interrotta: stessa posizione, stesso filamento estruso
    const auto end = sim::steps();
    CHECK(end.x == referenceSteps.x && end.y == referenceSteps.y && end.z == referenceSteps.z);
    CHECK_MSG(end.e - eStart == referenceSteps.e, "e=" + std::to_string(end.e - eStart));
}

static void testRecoverAfterLinkFault() {
    prepareMachine();
    const std::string path = writePrintFile();
    const long eStart = sim::steps().e;
    {
        JobRig rig;
        CHECK(rig.manager->startPrintJob(path, "job-fault"));
        CHECK(waitFor([&] { return sim::steps().e - eStart > 5 * E_PER_LINE; }, 60s));

        // Il firmware smette di rispondere (cavo, blocco...): il job si ferma, non prosegue alla cieca
        sim::freeze(true);
        CHECK(rig.waitState(core::print::JobState::FAILED, 30s));
        CHECK(rig.queue->getQueueSize() == 0);
        sim::freeze(false);
    }

    const auto checkpoint = core::print::JobCheckpoint::load("job-fault");
    CHECK(checkpoint.has_value());
    if (!checkpoint) return;
    CHECK(checkpoint->nextLine > 8 && checkpoint->positionKnown);

    // Il driver viene "riavviato": nuovo gestore, che ritrova il job dal checkpoint su disco
    JobRig rig;
    CHECK_MSG(rig.manager->recoverJob(), rig.manager->lastRecoveryError());
    CHECK(rig.waitState(core::print::JobState::COMPLETED, 180s));
    CHECK(idle());

    const auto end = sim::steps();
    CHECK(end.x == referenceSteps.x && end.y == referenceSteps.y && end.z == referenceSteps.z);
    // Al massimo viene ristampata la riga interrotta (piu' quella rimasta in volo)
    const long extruded = end.e - eStart;
    CHECK_MSG(extruded >= referenceSteps.e && extruded <= referenceSteps.e + 2 * E_PER_LINE,
              "e=" + std::to_string(extruded));
    CHECK(!core::print::JobCheckpoint::load("job-fault").has_value());
}

static void testRecoverAfterFirmwareReboot() {
    prepareMachine();
    const std::string path = writePrintFile();
    JobRig rig;
    const long eStart = sim::steps().e;

    CHECK(rig.manager->startPrintJob(path, "job-reboot"));
    CHECK(waitFor([&] { return sim::steps().e - eStart > 6 * E_PER_LINE; }, 60s));

    // Watchdog: il firmware riparte da zero e perde posizione e stato
    sim::reboot(4 /* hal::RESET_WATCHDOG */);
    CHECK(rig.waitState(core::print::JobState::FAILED, 30s));
    CHECK(driver->isLinkFaulted());

    // X e Y vengono ritrovati sui finecorsa, Z ripreso dal checkpoint, la stampa continua
    CHECK_MSG(rig.manager->recoverJob("job-reboot"), rig.manager->lastRecoveryError());
    CHECK(rig.waitState(core::print::JobState::COMPLETED, 180s));
    CHECK(idle());

    const auto end = sim::steps();
    CHECK_MSG(end.x == referenceSteps.x && end.y == referenceSteps.y,
              "x=" + std::to_string(end.x) + " y=" + std::to_string(end.y));
    CHECK_MSG(end.z == referenceSteps.z, "z=" + std::to_string(end.z));
    const long extruded = end.e - eStart;
    CHECK_MSG(extruded >= referenceSteps.e - E_PER_LINE && extruded <= referenceSteps.e + 2 * E_PER_LINE,
              "e=" + std::to_string(extruded));
}

static void testEndGCodeAndCheckpointFile() {
    prepareMachine();
    const std::string path = writePrintFile();
    JobRig rig;

    // Il G-code di fine stampa del backend viene eseguito dopo l'ultima riga del file
    rig.manager->setEndGCode(std::string("; fine stampa") + char(10) + "G1 X8 Y6 F6000" + char(10) + "M107");
    CHECK(rig.manager->startPrintJob(path, "job-end"));
    CHECK(rig.waitState(core::print::JobState::COMPLETED, 120s));
    CHECK(idle());
    CHECK_MSG(sim::steps().x == -800 + 640 && sim::steps().y == -800 + 480,
              "x=" + std::to_string(sim::steps().x) + " y=" + std::to_string(sim::steps().y));

    // Il checkpoint conserva tutto cio' che serve a riprendere, anche dopo un riavvio del driver
    core::print::JobCheckpoint saved;
    saved.jobId = "3f0c2a6e-roundtrip";
    saved.gcodePath = path;
    saved.nextLine = 42;
    saved.positionKnown = true;
    saved.x = 12.345;
    saved.y = -6.5;
    saved.z = 0.6;
    saved.e = 123.4567;
    saved.feedrate = 2400;
    saved.absoluteE = false;
    saved.hotendTarget = 205;
    saved.fanSpeed = 128;
    saved.endGCode = std::string("M104 S0") + char(10) + "G1 X0 Y200";
    CHECK(saved.save());

    const auto loaded = core::print::JobCheckpoint::load(saved.jobId);
    CHECK(loaded.has_value());
    if (loaded) {
        CHECK(loaded->nextLine == 42 && loaded->positionKnown && !loaded->absoluteE && loaded->absoluteXYZ);
        CHECK(std::fabs(loaded->x - 12.345) < 1e-9 && std::fabs(loaded->e - 123.4567) < 1e-9);
        CHECK(loaded->hotendTarget == 205 && loaded->fanSpeed == 128);
        CHECK(loaded->endGCode == saved.endGCode);
    }
    const auto latest = core::print::JobCheckpoint::latest();
    CHECK(latest && latest->jobId == saved.jobId);
    core::print::JobCheckpoint::remove(saved.jobId);
    CHECK(!core::print::JobCheckpoint::load(saved.jobId).has_value());
}

static void testReadableWhileMoving() {
    // Un lavoro di pochi movimenti lunghi: le righe finiscono subito, la macchina lavora ancora
    // per secondi. In quel tempo chi legge (la pagina) e chi regola non deve restare in attesa.
    prepareMachine();
    sim::setTimeScale(1.0);
    std::filesystem::create_directories("gcode");
    const std::string path = "gcode/long_moves.gcode";
    {
        std::ofstream out(path);
        out << "G28\nG90\n";
        for (int i = 0; i < 3; ++i) out << "G1 X60 F2400\nG1 X2\n"; // 1,5 s circa l'uno; X0 e' il finecorsa
    }
    JobRig rig;
    CHECK(rig.manager->startPrintJob(path, "job-long"));
    CHECK(waitFor([&] { return sim::steps().x > 800; }, 20s)); // homing fatto, primo movimento in corso
    std::this_thread::sleep_for(400ms);

    const auto slow = [](const std::function<void()> &call) {
        const auto start = std::chrono::steady_clock::now();
        call();
        return std::chrono::steady_clock::now() - start > 100ms;
    };
    int stalls = 0;
    const long before = sim::steps().x;
    for (int i = 0; i < 20; ++i) {
        if (slow([&] { rig.manager->getCurrentState(); })) ++stalls;
        if (slow([&] { driver->motion()->linearAdvance(); })) ++stalls;
        if (slow([&] { driver->motion()->plannerLimits(); })) ++stalls;
        if (slow([&] { driver->motion()->sentPosition(); })) ++stalls;
        if (slow([&] { driver->motion()->setSpeedFactor(1.0); })) ++stalls;
        if (slow([&] { rig.queue->getQueueSize(); })) ++stalls;
        std::this_thread::sleep_for(100ms);
    }
    CHECK_MSG(stalls == 0, std::to_string(stalls) + " calls waited more than 100 ms");
    CHECK(sim::steps().x != before);                                  // nel frattempo la macchina si muoveva
    CHECK(rig.manager->getCurrentState() == core::print::JobState::RUNNING); // non e' "finito" prima di fermarsi
    if (rig.manager->getCurrentState() != core::print::JobState::RUNNING) {
        std::printf("    fault: %s\n", driver->lastFaultReason().c_str());
        for (const auto &l: driver->system()->errorStatus().body) std::printf("    err: %s\n", l.c_str());
        for (const auto &l: driver->system()->diagnostics().body) std::printf("    diag: %s\n", l.c_str());
    }

    CHECK(rig.waitState(core::print::JobState::COMPLETED, 30s));
    CHECK(idle());
    CHECK_MSG(sim::steps().x == -800 + 160, "x=" + std::to_string(sim::steps().x)); // tornato a X2
    std::error_code ec;
    std::filesystem::remove_all(core::print::JobCheckpoint::directory(), ec);
}

static void testHomingMeasuresLostSteps() {
    // L'homing dice dove il firmware credeva di essere al contatto: su un asse gia' riferito e'
    // il numero di step che la meccanica ha perso. E' la base di tutte le misure automatiche.
    prepareMachine();
    sim::setTimeScale(8.0);
    auto result = driver->system()->homing();
    CHECK_MSG(result.isSuccess(), result.message);

    const auto away = [&](double x) {
        CHECK(driver->system()->startPrint().isSuccess());
        CHECK(driver->motion()->moveAbsolute({x, std::nullopt, std::nullopt, std::nullopt, 4800.0}).isSuccess());
        CHECK(idle());
        CHECK(driver->system()->endPrint().isSuccess());
    };

    // nessuno step perso: il finecorsa scatta esattamente a zero
    away(30.0);
    auto homing = local::measure::parseHoming(driver->system()->homeAxes("X").body);
    CHECK(homing.done[0] && homing.offset[0].has_value());
    CHECK_MSG(homing.offset[0].value_or(99) == 0, "offset " + std::to_string(homing.offset[0].value_or(99)));
    CHECK(!homing.done[1] && !homing.offset[1].has_value()); // Y non e' stato chiesto

    // finecorsa 7 step piu' in alto = carrello rimasto indietro di 7 step: il contatto arriva in anticipo
    away(30.0);
    sim::setEndstop(0, -800 + 7);
    homing = local::measure::parseHoming(driver->system()->homeAxes("X").body);
    CHECK_MSG(homing.offset[0].value_or(99) == 7, "offset " + std::to_string(homing.offset[0].value_or(99)));

    // dopo l'homing lo zero e' quello nuovo: rimesso il finecorsa al suo posto, la differenza e' -7
    away(30.0);
    sim::setEndstop(0, -800);
    homing = local::measure::parseHoming(driver->system()->homeAxes("X").body);
    CHECK_MSG(homing.offset[0].value_or(99) == -7, "offset " + std::to_string(homing.offset[0].value_or(99)));

    // da non riferito (dopo un arresto di emergenza) non c'e' nulla da confrontare: nessun numero
    CHECK(driver->motion()->emergencyStop().isSuccess());
    CHECK(driver->system()->clearError().isSuccess());
    result = driver->system()->homing();
    CHECK_MSG(result.isSuccess(), result.message);
    homing = local::measure::parseHoming(result.body);
    CHECK(homing.done[0] && homing.done[1] && homing.done[2]);
    CHECK(!homing.offset[0].has_value() && !homing.offset[1].has_value() && !homing.offset[2].has_value());
    CHECK(sim::steps().x == -700 && sim::steps().y == -700);
}

static void testMeasureMath() {
    using namespace local::measure;
    const auto s = stats({1.0, 2.0, 3.0, 4.0});
    CHECK(s.count == 4 && std::fabs(s.mean - 2.5) < 1e-9 && std::fabs(s.deviation - 1.2909944) < 1e-6);
    CHECK(s.min == 1.0 && s.max == 4.0);
    CHECK(stats({}).count == 0);

    // righe come arrivano dalla scheda, con il checksum in coda
    auto homing = parseHoming({"HOM X START *12", "HOM X OK -3 *77", "HOM Y SKIP *5", "HOM Z FAIL *9"});
    CHECK(homing.done[0] && homing.offset[0].value_or(0) == -3);
    CHECK(homing.skipped[1] && !homing.done[1] && homing.failed[2]);
    homing = parseHoming({"HOM X OK *77"}); // firmware precedente: nessuna misura
    CHECK(homing.done[0] && !homing.offset[0].has_value());

    const auto health = parseHealth({"DIA 120 1 5521 2 0 1 812 *40", "PRF 640 64 3 1 *12"});
    CHECK(health.valid && health.checksumErrors == 2 && health.lostAcks == 1);
    CHECK(health.freeRamMin == 640 && health.stepInterruptMaxUs == 64 && health.missedDeadlines == 3 && health.uartErrors == 1);
    CHECK(!parseHealth({"DIA 120 1 5521 2 0 1 812"}).valid);

    CHECK(std::fabs(peakSpeed(80.0, 1000.0, 100.0) - 80.0) < 1e-9); // tratto lungo: si arriva alla velocita' chiesta
    CHECK(std::fabs(peakSpeed(80.0, 1000.0, 4.0) - 63.2455) < 1e-3); // tratto corto: non si fa in tempo

    // Hotend di prova: fermo per 5 s (ritardo), poi sale a 2 gradi/s. La regola deve ritrovare i due numeri.
    std::vector<TempSample> curve;
    for (double t = 0.0; t <= 120.0; t += 0.5) curve.push_back({t, 25.0 + std::max(0.0, t - 5.0) * 2.0});
    const auto pid = tuneFromRise(curve, 190.0);
    CHECK_MSG(pid.valid, pid.why);
    CHECK_MSG(std::fabs(pid.rate - 2.0) < 0.05, "rate " + std::to_string(pid.rate));
    CHECK_MSG(std::fabs(pid.deadTime - 5.0) < 0.5, "dead time " + std::to_string(pid.deadTime));
    CHECK(pid.kp > 5.0 && pid.kp < 60.0 && pid.ki > 0.0 && pid.kd > 0.0);
    // partendo gia' caldi non c'e' abbastanza salita a piena potenza: nessuna proposta, e si dice perche'
    std::vector<TempSample> warm;
    for (double t = 0.0; t <= 20.0; t += 0.5) warm.push_back({t, 170.0 + t});
    const auto none = tuneFromRise(warm, 190.0);
    CHECK(!none.valid && !none.why.empty());
}

static void testCancelJob() {
    prepareMachine();
    const std::string path = writePrintFile();
    JobRig rig;
    const long eStart = sim::steps().e;

    CHECK(rig.manager->startPrintJob(path, "job-cancel"));
    CHECK(waitFor([&] { return sim::steps().e - eStart > 3 * E_PER_LINE; }, 60s));

    const auto start = std::chrono::steady_clock::now();
    CHECK(rig.manager->cancelJob());
    CHECK(std::chrono::steady_clock::now() - start < 5s);
    CHECK(idle());

    const auto stopped = sim::steps();
    std::this_thread::sleep_for(300ms);
    CHECK(sim::steps().x == stopped.x && sim::steps().e == stopped.e); // fermo davvero
    CHECK(rig.queue->getQueueSize() == 0);
    CHECK(driver->getState() == core::PrintState::Idle);
    CHECK(!core::print::JobCheckpoint::load("job-cancel").has_value()); // un job annullato non si riprende
    CHECK(!rig.manager->recoverJob("job-cancel"));
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    // Argomento facoltativo: esegue solo gli scenari il cui nome lo contiene
    const std::string filter = argc > 1 ? argv[1] : "";
    sim::start();

    port = std::make_shared<SimSerialPort>();
    bool ready = false;
    for (int i = 0; i < 100 && !ready; ++i) {
        ready = sim::hostReadLine(100).find("Sistema pronto") != std::string::npos;
    }
    if (!ready) {
        std::printf("firmware did not boot\n");
        return 2;
    }

    driver = std::make_shared<core::DriverInterface>(nullptr, port);
    fastTiming();

    const std::pair<const char *, void (*)()> tests[] = {
            {"boot and status",                    testBootAndStatus},
            {"rejected command is not success",    testRejectedIsNotSuccess},
            {"straight line (Bresenham)",          testStraightLine},
            {"long Z move beyond 16 bit",          testLongZBeyond16Bit},
            {"extrusion remainder carry",          testExtrusionRemainder},
            {"lost response -> duplicate",         testLostResponse},
            {"lost / corrupted command",           testLostAndCorruptedCommand},
            {"noise and stale responses",          testNoiseAndStaleResponses},
            {"timeout is never success + resync",  testTimeoutIsNeverSuccess},
            {"firmware reboot during move",        testFirmwareRebootDuringMove},
            {"command number beyond 16 bit",       testSequenceBeyond16Bit},
            {"motion abort",                       testAbortMotion},
            {"lookahead and speed profile",        testLookahead},
            {"host planner and linear advance",    testHostPlanner},
            {"stop within queued moves",           testStopGuarantee},
            {"thermal control and protections",    testThermal},
            {"PID temperature regulation",         testPidRegulation},
            {"18h / millis / micros rollover",     testLongRunningClocks},
            {"arc and spline flattening",          testGeometry},
            {"g-code translation",                 testGCodeTranslation},
            {"preview and time estimate",          testPreviewAndEstimate},
            {"queue pause and stop on fault",      testQueueStopsOnFault},
            {"homing and endstops",                testHomingAndEndstops},
            {"full print job from file",           testFullPrintJob},
            {"large job keeps file order",         testLargeJobOrder},
            {"software travel limits",             testTravelLimits},
            {"short segment stream (binary)",      testShortSegmentStream},
            {"binary stream fault recovery",       testStreamFaults},
            {"motor enable pin",                   testMotorEnable},
            {"stack guard",                        testStackGuard},
            {"sliced file from PrusaSlicer",       testSlicedFile},
            {"pause, park and resume",             testPauseAndResume},
            {"recover after link fault",           testRecoverAfterLinkFault},
            {"recover after firmware reboot",      testRecoverAfterFirmwareReboot},
            {"end g-code and checkpoint file",     testEndGCodeAndCheckpointFile},
            {"cancel job",                         testCancelJob},
            {"homing measures lost steps",         testHomingMeasuresLostSteps},
            {"measurement maths",                  testMeasureMath},
            {"state readable while moving",        testReadableWhileMoving},
    };

    for (const auto &[name, test]: tests) {
        if (!filter.empty() && std::string(name).find(filter) == std::string::npos) continue;
        const int before = failures;
        std::printf("[ RUN  ] %s\n", name);
        std::fflush(stdout);
        test();
        if (failures != before) {
            std::lock_guard<std::mutex> lock(logMutex);
            for (const auto &line: recentLog) std::printf("      log: %s\n", line.c_str());
        }
        {
            std::lock_guard<std::mutex> lock(logMutex);
            recentLog.clear();
        }
        std::printf("[ %s ] %s\n", failures == before ? " OK " : "FAIL", name);
    }

    const int bites = sim::watchdogBites();
    std::printf("watchdog starvation events: %d\n", bites);
    if (bites != 0) ++failures;

    sim::stop();
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
