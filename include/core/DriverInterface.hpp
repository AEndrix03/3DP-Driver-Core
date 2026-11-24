//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "CommandContext.hpp"
#include "core/printer/PrintState.hpp"
#include "core/printer/Printer.hpp"
#include "core/command/motion/MotionCommands.hpp"
#include "core/command/extruder/ExtruderCommands.hpp"
#include "core/command/endstop/EndstopCommands.hpp"
#include "core/command/fan/FanCommands.hpp"
#include "core/command/system/SystemCommands.hpp"
#include "core/CommandExecutor.hpp"
#include "core/MoveFrame.hpp"
#include "core/command/history/HistoryCommands.hpp"
#include "core/command/temperature/TemperatureCommands.hpp"
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <string>

namespace core {
    /**
     * @brief Interfaccia principale del driver, esposta verso applicazioni esterne.
     */
    class DriverInterface {
    public:
        explicit DriverInterface(std::shared_ptr<Printer> printer, std::shared_ptr<SerialPort> serialPort);

        std::shared_ptr<command::motion::MotionCommands> motion() const;

        std::shared_ptr<command::extruder::ExtruderCommands> extruder() const;

        std::shared_ptr<command::endstop::EndstopCommands> endstop() const;

        std::shared_ptr<command::fan::FanCommands> fan() const;

        std::shared_ptr<command::system::SystemCommands> system() const;

        std::shared_ptr<command::history::HistoryCommands> history() const;

        std::shared_ptr<command::temperature::TemperatureCommands> temperature() const;

        PrintState getState() const;

        void setState(PrintState newState);

        static std::string printStateToString(PrintState state);

        types::Result sendCommandInternal(char category, int code, const std::vector<std::string> &params) const;

        /**
         * @brief Interrompe subito il movimento in corso sul firmware (carattere '!').
         * Non passa dalla coda comandi: funziona anche mentre un comando sta eseguendo.
         */
        void abortMotion();

        /**
         * @brief Contatore dei guasti: aumenta a ogni timeout, perdita di sincronismo,
         * riavvio del firmware o macchina in errore. Chi esegue un job lo confronta
         * prima/dopo ogni comando per fermarsi invece di proseguire alla cieca.
         */
        uint64_t faultEpoch() const { return faultEpoch_.load(); }

        std::string lastFaultReason() const;

        /// Segnala un guasto rilevato a livello applicativo (es. riscaldamento fallito).
        void raiseFault(const std::string &reason) const;

        /// true finche' il collegamento col firmware non viene risincronizzato.
        bool isLinkFaulted() const { return linkFault_.load(); }

        std::shared_ptr<CommandExecutor> executor() const { return commandExecutor_; }

        /// Numero dell'ultimo comando inviato con successo.
        uint32_t lastIssuedCommand() const { return lastIssued_.load(); }

        /// Numero dell'ultimo comando che il firmware ha finito di eseguire.
        uint32_t lastCompletedCommand() const { return lastCompleted_.load(); }

        /// Cambia a ogni risincronizzazione: i numeri comando ripartono da 1.
        uint32_t sequenceEpoch() const { return sequenceEpoch_.load(); }

        /**
         * @brief Invia un movimento in formato binario: step relativi e piano di velocita'
         * gia' calcolati (vedi core::planning::MotionPlanner).
         * @param withPlan true = il frame porta anche velocita' nominale e accelerazione
         * @param pipelined true = non attende la risposta (fino a 4 movimenti in volo);
         *        un errore viene restituito da una chiamata successiva o segnalato come
         *        guasto al primo comando di testo.
         */
        types::Result sendMove(const MoveFrame::PlannedMove &move, bool withPlan, bool pipelined) const;

        /**
         * @brief Invia i movimenti ancora in attesa nel piano e attende le risposte di quelli in volo. Un errore diventa un guasto
         * (faultEpoch): da chiamare prima di considerare concluso un gruppo di movimenti.
         */
        void flushMoves() const;

        /**
         * @brief Versione della posizione nota al driver. Cambia ogni volta che la posizione
         * del firmware puo' essere diversa da quella calcolata qui (arresto, allarme, homing,
         * posizione impostata, movimento rifiutato, risincronizzazione): chi tiene una copia
         * della posizione deve rileggerla dal firmware.
         */
        uint64_t positionEpoch() const { return positionEpoch_.load(); }

        /**
         * @brief Attende che il firmware abbia eseguito tutti i movimenti in coda.
         * @return false su timeout o guasto
         */
        bool waitForIdle(std::chrono::milliseconds timeout = std::chrono::minutes(10));

    private:
        std::shared_ptr<Printer> printer_;
        std::shared_ptr<SerialPort> serialPort_;
        std::shared_ptr<CommandContext> commandContext_;
        std::shared_ptr<CommandExecutor> commandExecutor_;
        mutable std::atomic<PrintState> currentState_;

        mutable std::mutex commandMutex_;
        mutable std::mutex faultMutex_;
        mutable std::atomic<uint64_t> faultEpoch_{0};
        mutable std::atomic<bool> linkFault_{false};
        mutable std::string lastFaultReason_;
        mutable std::atomic<uint32_t> lastIssued_{0};
        mutable std::atomic<uint32_t> lastCompleted_{0};
        mutable std::atomic<uint32_t> sequenceEpoch_{0};
        mutable std::atomic<uint64_t> positionEpoch_{0};
        mutable std::atomic<bool> abortSent_{false};

        /// Corpo di sendCommandInternal. Richiede commandMutex_.
        types::Result sendTextLocked(char category, int code, const std::vector<std::string> &params) const;

        /// Attende le risposte dei movimenti in volo e ne gestisce l'esito. Richiede commandMutex_.
        void drainMovesLocked() const;

        /// Effetti di un movimento fallito su stato, guasti e copia della posizione.
        void handleMoveFailure(const types::Result &result, bool reportFault) const;

        /// Handshake "N0 S10": riallinea numerazione e stato. Richiede commandMutex_.
        bool resyncLocked() const;

        void applyStateAfterSuccess(char category, int code) const;

        std::shared_ptr<command::motion::MotionCommands> motion_;
        std::shared_ptr<command::endstop::EndstopCommands> endstop_;
        std::shared_ptr<command::extruder::ExtruderCommands> extruder_;
        std::shared_ptr<command::fan::FanCommands> fan_;
        std::shared_ptr<command::system::SystemCommands> system_;
        std::shared_ptr<command::history::HistoryCommands> history_;
        std::shared_ptr<command::temperature::TemperatureCommands> temperature_;
    };
} // namespace core