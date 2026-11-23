//
// Created by redeg on 26/04/2025.
//

#include "core/DriverInterface.hpp"
#include "core/CommandBuilder.hpp"
#include "core/MoveFrame.hpp"
#include "logger/Logger.hpp"
#include <chrono>
#include <mutex>
#include <thread>

namespace core {

    DriverInterface::DriverInterface(std::shared_ptr<Printer> printer, std::shared_ptr<SerialPort> serialPort)
            : printer_(std::move(printer)),
              serialPort_(std::move(serialPort)),
              commandContext_(std::make_shared<CommandContext>()),
              commandExecutor_(std::make_shared<CommandExecutor>(serialPort_, commandContext_)),
              currentState_(PrintState::Idle),
              motion_(std::make_shared<command::motion::MotionCommands>(this)),
              endstop_(std::make_shared<command::endstop::EndstopCommands>(this)),
              extruder_(std::make_shared<command::extruder::ExtruderCommands>(this)),
              fan_(std::make_shared<command::fan::FanCommands>(this)),
              system_(std::make_shared<command::system::SystemCommands>(this)),
              history_(std::make_shared<command::history::HistoryCommands>(this)),
              temperature_(std::make_shared<command::temperature::TemperatureCommands>(this)) {
    }

    std::shared_ptr<command::motion::MotionCommands> DriverInterface::motion() const {
        return motion_;
    }

    std::shared_ptr<command::endstop::EndstopCommands> DriverInterface::endstop() const {
        return endstop_;
    }

    std::shared_ptr<command::extruder::ExtruderCommands> DriverInterface::extruder() const {
        return extruder_;
    }

    std::shared_ptr<command::fan::FanCommands> DriverInterface::fan() const {
        return fan_;
    }

    std::shared_ptr<command::system::SystemCommands> DriverInterface::system() const {
        return system_;
    }

    std::shared_ptr<command::history::HistoryCommands> DriverInterface::history() const {
        return history_;
    }

    std::shared_ptr<command::temperature::TemperatureCommands> DriverInterface::temperature() const {
        return temperature_;
    }

    PrintState DriverInterface::getState() const {
        return currentState_.load();
    }

    void DriverInterface::setState(PrintState newState) {
        const PrintState old = currentState_.exchange(newState);
        if (old != newState) {
            Logger::logInfo("[DriverInterface] State change: " +
                            printStateToString(old) + " -> " +
                            printStateToString(newState));
        }
    }

    std::string DriverInterface::printStateToString(PrintState state) {
        switch (state) {
            case PrintState::Idle:
                return "Idle";
            case PrintState::Homing:
                return "Homing";
            case PrintState::Printing:
                return "Printing";
            case PrintState::Paused:
                return "Paused";
            case PrintState::Error:
                return "Error";
            default:
                return "Unknown";
        }
    }

    std::string DriverInterface::lastFaultReason() const {
        std::lock_guard<std::mutex> lock(faultMutex_);
        return lastFaultReason_;
    }

    void DriverInterface::raiseFault(const std::string &reason) const {
        {
            std::lock_guard<std::mutex> lock(faultMutex_);
            lastFaultReason_ = reason;
        }
        faultEpoch_.fetch_add(1);
        positionEpoch_.fetch_add(1);
        Logger::logError("[DriverInterface] FAULT: " + reason);
    }

    void DriverInterface::abortMotion() {
        // Volutamente fuori da commandMutex_: deve arrivare mentre un comando sta eseguendo.
        if (serialPort_ && serialPort_->isOpen()) {
            Logger::logWarning("[DriverInterface] Sending motion abort");
            abortSent_ = true; // i movimenti ancora in volo verranno scartati dal firmware: e' previsto
            positionEpoch_.fetch_add(1);
            serialPort_->send("!");
        }
    }

    bool DriverInterface::waitForIdle(std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        const uint64_t epoch = faultEpoch();

        while (std::chrono::steady_clock::now() < deadline) {
            // Un comando di stato non entra nella coda dei movimenti: quando il suo
            // "D" coincide col suo numero, il firmware e' fermo.
            const types::Result result = sendCommandInternal('S', 10, {});
            if (!result.isSuccess() || faultEpoch() != epoch) return false;
            if (result.completedNumber && result.commandNumber &&
                *result.completedNumber == *result.commandNumber) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        return false;
    }

    void DriverInterface::applyStateAfterSuccess(char category, int code) const {
        auto *self = const_cast<DriverInterface *>(this);
        if (category != 'S') return;

        switch (code) {
            case 0:  // homing completato
            case 12: // fine stampa
                self->setState(PrintState::Idle);
                break;
            case 1:
            case 3:
                self->setState(PrintState::Printing);
                break;
            case 2:
                self->setState(PrintState::Paused);
                break;
            default:
                break;
        }
    }

    bool DriverInterface::resyncLocked() const {
        // N0 = comando di sincronizzazione: il firmware lo accetta sempre e
        // riparte da N1. S10 restituisce lo stato macchina reale.
        const std::string command = CommandBuilder::buildCommand(0, 'S', 10, {});
        const types::Result result = commandExecutor_->sendCommandAndAwaitResponse(command, 0);

        if (!result.isSuccess()) {
            Logger::logError("[DriverInterface] Resync failed: " + result.message);
            return false;
        }

        commandContext_->setCommandNumber(1);
        linkFault_ = false;
        lastIssued_ = 0;
        lastCompleted_ = 0;
        sequenceEpoch_.fetch_add(1);
        positionEpoch_.fetch_add(1);

        auto *self = const_cast<DriverInterface *>(this);
        for (const auto &line: result.body) {
            if (line.rfind("STA ", 0) != 0) continue;
            const std::string state = line.substr(4);
            if (state == "Idle") self->setState(PrintState::Idle);
            else if (state == "Homing") self->setState(PrintState::Homing);
            else if (state == "Printing") self->setState(PrintState::Printing);
            else if (state == "Paused") self->setState(PrintState::Paused);
            else self->setState(PrintState::Error);
        }

        Logger::logInfo("[DriverInterface] Link resynchronized, firmware state: " +
                        printStateToString(currentState_.load()));
        return true;
    }

    void DriverInterface::handleMoveFailure(const types::Result &result, bool reportFault) const {
        positionEpoch_.fetch_add(1);
        auto *self = const_cast<DriverInterface *>(this);

        switch (result.code) {
            case types::ResultCode::Rejected:
                // Sequenza integra, movimento non eseguito. Dopo un arresto chiesto da noi
                // i movimenti scartati sono attesi, non un guasto.
                if (reportFault && !abortSent_.load()) raiseFault(result.message);
                break;
            case types::ResultCode::Busy:
                self->setState(PrintState::Error);
                raiseFault(result.message);
                break;
            default:
                linkFault_ = true;
                self->setState(PrintState::Error);
                raiseFault(result.code == types::ResultCode::Resend
                           ? "Firmware lost command sequence (" + result.message + ")" : result.message);
                break;
        }
    }

    void DriverInterface::drainMovesLocked() const {
        if (!commandExecutor_->hasPendingMoves()) {
            abortSent_ = false;
            return;
        }
        const types::Result drained = commandExecutor_->drainMoves();
        if (const auto completed = commandExecutor_->lastWindowCompleted()) lastCompleted_ = *completed;
        // Nessun chiamante riceve questo esito: se e' un errore va segnalato come guasto
        if (!drained.isSuccess()) handleMoveFailure(drained, true);
        abortSent_ = false;
    }

    void DriverInterface::flushMoves() const {
        std::lock_guard<std::mutex> lock(commandMutex_);
        drainMovesLocked();
    }

    types::Result DriverInterface::sendMove(const int32_t delta[4], uint32_t feedrate, bool pipelined) const {
        std::lock_guard<std::mutex> lock(commandMutex_);

        try {
            if (linkFault_) {
                drainMovesLocked();
                if (!resyncLocked()) {
                    return {types::ResultCode::Error, "Link fault: " + lastFaultReason(), std::nullopt, {}};
                }
            }

            const uint32_t cmdNum = commandContext_->nextCommandNumber();
            const std::string frame = MoveFrame::encode(cmdNum, delta, feedrate);
            types::Result result = commandExecutor_->submitMove(frame, cmdNum, !pipelined);
            if (const auto completed = commandExecutor_->lastWindowCompleted()) lastCompleted_ = *completed;

            if (result.isSuccess()) {
                lastIssued_ = cmdNum;
                return result;
            }

            // In modalita' pipelined l'errore puo' riguardare un movimento precedente: questo
            // frame allora non e' partito e il suo numero non e' stato consumato dal firmware.
            if (result.commandNumber != std::optional<uint32_t>(cmdNum) && pipelined) {
                commandContext_->setCommandNumber(cmdNum);
            }
            handleMoveFailure(result, false);

            if (result.isRejected() || result.isBusy()) {
                // Un comando di testo dice al firmware che ci siamo riallineati: i movimenti
                // binari tornano ad essere accettati.
                sendTextLocked('S', 10, {});
            }
            if (result.code == types::ResultCode::Resend) result.code = types::ResultCode::SyncLost;
            return result;

        } catch (const std::exception &e) {
            Logger::logError("[DriverInterface] Exception in sendMove: " + std::string(e.what()));
            return {types::ResultCode::Error, std::string("Exception: ") + e.what(), std::nullopt, {}};
        }
    }

    types::Result
    DriverInterface::sendCommandInternal(char category, int code, const std::vector<std::string> &params) const {
        std::lock_guard<std::mutex> lock(commandMutex_);
        // I comandi di testo sono uno alla volta: prima si chiudono i movimenti in volo
        drainMovesLocked();
        return sendTextLocked(category, code, params);
    }

    types::Result
    DriverInterface::sendTextLocked(char category, int code, const std::vector<std::string> &params) const {
        try {
            if (linkFault_ && !resyncLocked()) {
                return {types::ResultCode::Error, "Link fault: " + lastFaultReason(), std::nullopt, {}};
            }

            uint32_t cmdNum = commandContext_->nextCommandNumber();
            std::string command = CommandBuilder::buildCommand(cmdNum, category, code, params);
            types::Result result = commandExecutor_->sendCommandAndAwaitResponse(command, cmdNum);

            if (result.isResend() && result.commandNumber.has_value() && *result.commandNumber > cmdNum) {
                // Firmware piu' avanti di noi (driver riavviato a firmware acceso):
                // nessun comando perso, basta adottare la sua numerazione.
                cmdNum = *result.commandNumber;
                commandContext_->setCommandNumber(cmdNum + 1);
                command = CommandBuilder::buildCommand(cmdNum, category, code, params);
                result = commandExecutor_->sendCommandAndAwaitResponse(command, cmdNum);
            }

            switch (result.code) {
                case types::ResultCode::Success:
                    applyStateAfterSuccess(category, code);
                    // S6 riporta in Idle solo se c'era davvero un errore da azzerare
                    if (category == 'S' && code == 6 && result.message == "Error cleared") {
                        const_cast<DriverInterface *>(this)->setState(PrintState::Idle);
                    }
                    lastIssued_ = cmdNum;
                    if (result.completedNumber) lastCompleted_ = *result.completedNumber;
                    // Comandi che cambiano la posizione senza passare dai movimenti binari
                    if ((category == 'M' && code != 114 && code != 115) || (category == 'S' && code == 0)) {
                        positionEpoch_.fetch_add(1);
                    }
                    break;
                case types::ResultCode::Duplicate:
                    // Eseguito (o accodato), ma la risposta e gli eventuali dati sono andati persi
                    applyStateAfterSuccess(category, code);
                    lastIssued_ = cmdNum;
                    result.code = types::ResultCode::Success;
                    break;
                case types::ResultCode::Rejected:
                    // Il firmware ha risposto: sequenza integra, comando non eseguito
                    break;
                case types::ResultCode::BufferOverflow:
                    // Riga scartata dal firmware: il numero non risulta consumato
                    commandContext_->setCommandNumber(cmdNum);
                    break;
                case types::ResultCode::Busy:
                    const_cast<DriverInterface *>(this)->setState(PrintState::Error);
                    raiseFault(result.message);
                    break;
                case types::ResultCode::Resend:
                    // Il firmware chiede un N precedente: ha perso la sequenza (riavvio)
                    result.code = types::ResultCode::SyncLost;
                    result.message = "Firmware lost command sequence (" + result.message + ")";
                    [[fallthrough]];
                default:
                    // Timeout, SyncLost, checksum irrecuperabile, porta assente:
                    // non sappiamo se il comando ha avuto effetto. Stop, serve un resync.
                    linkFault_ = true;
                    const_cast<DriverInterface *>(this)->setState(PrintState::Error);
                    raiseFault(result.message);
                    break;
            }

            return result;

        } catch (const std::exception &e) {
            Logger::logError("[DriverInterface] Exception in sendCommandInternal: " + std::string(e.what()));
            return {types::ResultCode::Error, std::string("Exception: ") + e.what(), std::nullopt, {}};
        }
    }

} // namespace core
