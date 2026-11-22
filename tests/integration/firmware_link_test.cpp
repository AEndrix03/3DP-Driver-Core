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
#include "core/queue/CommandExecutorQueue.hpp"
#include "core/utils/FloatFormatter.hpp"
#include "logger/Logger.hpp"
#include "translator/GCodeTranslator.hpp"
#include "translator/dispatchers/motion/MotionDispatcher.hpp"
#include "translator/dispatchers/system/SystemDispatcher.hpp"
#include "translator/dispatchers/temperature/TemperatureDispatcher.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
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
        if (dropRx_ > 0 && line.rfind(dropRxPrefix_, 0) == 0) {
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
                {static_cast<double>(i), std::nullopt, std::nullopt, std::nullopt, 3600.0});
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
        driver->motion()->moveAbsolute({30.0, 0.0, std::nullopt, std::nullopt, 3600.0});
        driver->motion()->moveAbsolute({x2, y2, std::nullopt, std::nullopt, 3600.0});
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

    CHECK(run("G28")); // nessun finecorsa abilitato: azzera sul posto
    CHECK(driver->getState() == core::PrintState::Idle);
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
    CHECK(run("G28"));
    CHECK(driver->getState() == core::PrintState::Idle);
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
    for (int i = 1; i <= 30; ++i) lines.push_back("G1 X" + std::to_string(i) + " F6000");
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
    CHECK(sim::steps().x == 2400); // tutti e 30 i movimenti eseguiti
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

// ---------------------------------------------------------------------------
int main() {
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
            {"thermal control and protections",    testThermal},
            {"18h / millis / micros rollover",     testLongRunningClocks},
            {"arc and spline flattening",          testGeometry},
            {"g-code translation",                 testGCodeTranslation},
            {"queue pause and stop on fault",      testQueueStopsOnFault},
    };

    for (const auto &[name, test]: tests) {
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
