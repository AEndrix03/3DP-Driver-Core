#include "core/command/motion/MotionCommands.hpp"
#include "core/DriverInterface.hpp"
#include "core/utils/FloatFormatter.hpp"
#include "core/MoveFrame.hpp"
#include <sstream>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>

namespace core::command::motion {
    namespace {
        // X/Y/Z a 3 decimali (risoluzione meccanica 0.0125 mm), E a 4: il firmware
        // riporta il resto frazionario, quindi nessuna deriva di estrusione.
        std::vector<std::string> buildParams(const MoveTarget &t) {
            std::vector<std::string> params;
            if (t.x) params.push_back("X" + utils::formatFloat(*t.x, 3));
            if (t.y) params.push_back("Y" + utils::formatFloat(*t.y, 3));
            if (t.z) params.push_back("Z" + utils::formatFloat(*t.z, 3));
            if (t.e) params.push_back("E" + utils::formatFloat(*t.e, 4));
            if (t.feedrate) params.push_back("F" + utils::formatFloat(*t.feedrate, 0));
            return params;
        }
    }

    MotionCommands::MotionCommands(DriverInterface *driver)
        : CommandCategoryInterface(driver) {
    }

    types::Result MotionCommands::emergencyStop() {
        return sendCommand('M', 0, {});
    }

    types::Result MotionCommands::moveRelative(const MoveTarget &target, bool pipelined) {
        return move(target, false, pipelined);
    }

    types::Result MotionCommands::moveAbsolute(const MoveTarget &target, bool pipelined) {
        return move(target, true, pipelined);
    }

    types::Result MotionCommands::syncMirror() {
        uint64_t epoch = 0;
        types::Result result;
        bool gotFactors = false, gotPosition = false, gotLimits = false, gotTravel = false, gotQueue = false;
        planning::MachineLimits limits;
        int queueBlocks = limits.queueBlocks;

        // Se la risposta va persa il comando risulta "gia' eseguito" ma senza dati: si richiede.
        for (int attempt = 0; attempt < 3 && !(gotFactors && gotPosition && gotLimits && gotTravel && gotQueue); ++attempt) {
            epoch = driver_->positionEpoch(); // letta prima: un evento durante la lettura la invalida
            result = sendCommand('M', 115, {});
            if (!result.isSuccess()) return result;

            for (const auto &line: result.body) {
                std::istringstream iss(line);
                std::string token;
                iss >> token;
                if (token == "CRT") iss >> token;
                if (token == "SPM") {
                    gotFactors = static_cast<bool>(iss >> mirror_.stepsPerMm[0] >> mirror_.stepsPerMm[1]
                                                       >> mirror_.stepsPerMm[2] >> mirror_.stepsPerMm[3]);
                } else if (token == "STP") {
                    gotPosition = static_cast<bool>(iss >> mirror_.position[0] >> mirror_.position[1]
                                                        >> mirror_.position[2]);
                } else if (token == "TRV") {
                    gotTravel = static_cast<bool>(iss >> mirror_.travelMax[0] >> mirror_.travelMax[1]
                                                      >> mirror_.travelMax[2] >> mirror_.limitedAxes);
                } else if (token == "LIM") {
                    // velocita' massime (mm/min) X Y Z E, accelerazioni XY Z E, deviazione di giunzione
                    // (micron), step/s massimi
                    double feed[4], accelXY, accelZ, accelE, deviation, stepRate;
                    if (line.find('~') == std::string::npos &&
                        (iss >> feed[0] >> feed[1] >> feed[2] >> feed[3] >> accelXY >> accelZ >> accelE
                             >> deviation >> stepRate)) {
                        for (int a = 0; a < 4; ++a) limits.maxFeed[a] = feed[a] / 60.0;
                        limits.maxAcceleration[0] = limits.maxAcceleration[1] = accelXY;
                        limits.maxAcceleration[2] = accelZ;
                        limits.maxAcceleration[3] = accelE;
                        limits.junctionDeviation = deviation / 1000.0;
                        limits.maxStepRate = stepRate;
                        gotLimits = true;
                    }
                } else if (token == "QUE") {
                    // blocchi utilizzabili della coda, buffer di ricezione (byte)
                    int queue = 0, receiveBuffer = 0;
                    if ((iss >> queue >> receiveBuffer) && queue >= 2 && receiveBuffer >= 32) {
                        queueBlocks = queue;
                        driver_->executor()->setReceiveBuffer(static_cast<size_t>(receiveBuffer));
                        info_.receiveBuffer = receiveBuffer;
                        gotQueue = true;
                    }
                }
            }
        }
        limits.queueBlocks = queueBlocks;
        if (!gotFactors || !gotPosition || !gotLimits || !gotTravel || !gotQueue) {
            result.code = types::ResultCode::Error;
            result.message = "Firmware did not report step position and limits";
            return result;
        }
        for (int a = 0; a < 4; ++a) {
            if (mirror_.stepsPerMm[a] <= 0.0) {
                result.code = types::ResultCode::Error;
                result.message = "Firmware reported invalid steps per mm";
                return result;
            }
            limits.stepsPerMm[a] = mirror_.stepsPerMm[a];
        }

        {
            std::lock_guard<std::mutex> sentLock(sentMutex_);
            info_.valid = true;
            info_.limits = limits;
            for (int a = 0; a < 3; ++a) info_.travelMm[a] = static_cast<double>(mirror_.travelMax[a]) / mirror_.stepsPerMm[a];
        }
        if (hasCaps_) {
            // i tetti dell'operatore possono solo abbassare: oltre i valori del firmware non si va
            for (int a = 0; a < 4; ++a) {
                if (caps_.maxFeed[a] > 0.0) limits.maxFeed[a] = std::min(limits.maxFeed[a], caps_.maxFeed[a]);
                if (caps_.maxAcceleration[a] > 0.0) {
                    limits.maxAcceleration[a] = std::min(limits.maxAcceleration[a], caps_.maxAcceleration[a]);
                }
            }
            if (caps_.junctionDeviation > 0.0) {
                limits.junctionDeviation = std::min(limits.junctionDeviation, caps_.junctionDeviation);
            }
        }
        planner_.configure(limits); // scarta anche i movimenti in attesa: partivano da un'altra posizione
        {
            std::lock_guard<std::mutex> sentLock(sentMutex_);
            effective_ = limits;
        }
        mirror_.valid = true;
        mirror_.epoch = epoch;
        mirror_.eRemainder = 0.0;  // come il firmware dopo un arresto: si riparte senza resto
        mirror_.sentSpeed = 0;     // il prossimo frame porta di nuovo velocita' e accelerazione
        mirror_.sentAcceleration = 0;
        {
            std::lock_guard<std::mutex> sentLock(sentMutex_);
            sent_.valid = true;
            for (int a = 0; a < 3; ++a) sent_.steps[a] = mirror_.position[a];
            for (int a = 0; a < 4; ++a) sent_.stepsPerMm[a] = mirror_.stepsPerMm[a];
        }
        return result;
    }

    types::Result MotionCommands::sendBlocks(const std::vector<planning::PlannedBlock> &blocks, bool pipelined) {
        types::Result result;
        result.code = types::ResultCode::Success;
        result.message = "Planned";

        for (size_t i = 0; i < blocks.size(); ++i) {
            const MoveFrame::PlannedMove &block = blocks[i].move;
            const bool withPlan = block.nominalSpeed != mirror_.sentSpeed ||
                                  block.acceleration != mirror_.sentAcceleration;
            if (planner_.blockRate() > 0.0) {
                // tetto ai movimenti al secondo: il piano ne ha gia' tenuto conto, qui lo si fa rispettare
                const auto now = std::chrono::steady_clock::now();
                if (nextBlockAt_ > now) std::this_thread::sleep_until(nextBlockAt_);
                nextBlockAt_ = std::max(nextBlockAt_, now) +
                               std::chrono::microseconds(static_cast<long long>(1e6 / planner_.blockRate()));
            }
            // Solo l'ultimo blocco di un movimento non pipelined attende la risposta
            result = driver_->sendMove(block, withPlan, pipelined || i + 1 < blocks.size());
            if (!result.isSuccess()) {
                mirror_.valid = false; // la posizione verra' riletta dal firmware
                planner_.reset();
                std::lock_guard<std::mutex> sentLock(sentMutex_);
                sent_.valid = false;
                return result;
            }
            {
                std::lock_guard<std::mutex> sentLock(sentMutex_);
                for (int a = 0; a < 3; ++a) sent_.steps[a] += block.steps[a];
            }
            mirror_.sentSpeed = block.nominalSpeed;
            mirror_.sentAcceleration = block.acceleration;
        }
        return result;
    }

    void MotionCommands::flushPlanned() {
        std::lock_guard<std::recursive_mutex> lock(mirrorMutex_);
        if (planner_.pending() == 0 || std::this_thread::get_id() != plannerThread_) return;
        if (!mirror_.valid || mirror_.epoch != driver_->positionEpoch()) {
            // Arresto o guasto nel frattempo: quei movimenti non vanno piu' eseguiti
            planner_.reset();
            return;
        }
        std::vector<planning::PlannedBlock> blocks;
        planner_.flush(blocks);
        sendBlocks(blocks, true); // un errore diventa un guasto quando la finestra viene chiusa
    }

    // Le regolazioni non toccano mirrorMutex_: quel lucchetto resta preso per tutto il tempo in cui
    // un movimento attende posto nella coda del firmware (anche secondi), e chi regola o legge
    // (la pagina, un comando dell'operatore) non deve restare fermo con lui.
    void MotionCommands::applyRequested() {
        Requested wanted;
        {
            std::lock_guard<std::mutex> lock(requestedMutex_);
            if (!requested_.dirty) return;
            requested_.dirty = false;
            wanted = requested_;
        }
        planner_.setSpeedFactor(wanted.speedFactor);
        planner_.setBlockRate(wanted.blockRate);
        planner_.setAccelerations(wanted.accelPrint, wanted.accelTravel);
        planner_.setLinearAdvance(wanted.advance);
    }

    void MotionCommands::setLinearAdvance(double k) {
        std::lock_guard<std::mutex> lock(requestedMutex_);
        requested_.advance = k < 0.0 ? 0.0 : k;
        requested_.dirty = true;
    }

    double MotionCommands::linearAdvance() {
        std::lock_guard<std::mutex> lock(requestedMutex_);
        return requested_.advance;
    }

    void MotionCommands::setSpeedFactor(double factor) {
        std::lock_guard<std::mutex> lock(requestedMutex_);
        requested_.speedFactor = factor;
        requested_.dirty = true;
    }

    void MotionCommands::setBlockRate(double perSecond) {
        std::lock_guard<std::mutex> lock(requestedMutex_);
        requested_.blockRate = perSecond;
        requested_.dirty = true;
    }

    void MotionCommands::setLimitCaps(const planning::MachineLimits &caps) {
        std::lock_guard<std::recursive_mutex> lock(mirrorMutex_);
        caps_ = caps;
        hasCaps_ = true;
        // Da fermi (niente in attesa nel piano) i limiti si rileggono al prossimo movimento
        if (planner_.pending() == 0) mirror_.valid = false;
    }

    types::Result MotionCommands::readMachine() {
        std::lock_guard<std::recursive_mutex> lock(mirrorMutex_);
        mirror_.valid = false;
        planner_.reset();
        return syncMirror();
    }

    MotionCommands::MachineInfo MotionCommands::machineInfo() {
        std::lock_guard<std::mutex> sentLock(sentMutex_);
        return info_;
    }

    MotionCommands::SentPosition MotionCommands::sentPosition() {
        std::lock_guard<std::mutex> sentLock(sentMutex_);
        return sent_;
    }

    planning::MachineLimits MotionCommands::plannerLimits() {
        std::lock_guard<std::mutex> sentLock(sentMutex_);
        return effective_;
    }

    void MotionCommands::setAccelerations(double print, double travel) {
        std::lock_guard<std::mutex> lock(requestedMutex_);
        requested_.accelPrint = print;
        requested_.accelTravel = travel;
        requested_.dirty = true;
    }

    types::Result MotionCommands::move(const MoveTarget &target, bool absolute, bool pipelined) {
        std::lock_guard<std::recursive_mutex> lock(mirrorMutex_);

        if (!mirror_.valid || mirror_.epoch != driver_->positionEpoch()) {
            mirror_.valid = false;
            planner_.reset();
            const types::Result synced = syncMirror();
            if (!synced.isSuccess()) return synced;
        }
        applyRequested();
        plannerThread_ = std::this_thread::get_id();

        long long delta[4] = {0, 0, 0, 0};
        const std::optional<double> axes[3] = {target.x, target.y, target.z};
        for (int a = 0; a < 3; ++a) {
            if (!axes[a]) continue;
            const long long steps = std::llround(*axes[a] * mirror_.stepsPerMm[a]);
            delta[a] = absolute ? steps - mirror_.position[a] : steps;
        }
        // Limiti di corsa: stessa regola del firmware (un asse gia' fuori puo' sempre rientrare).
        // Controllati qui sul movimento intero: una corsa lunga parte in piu' blocchi, e il
        // firmware da solo ne rifiuterebbe soltanto l'ultimo.
        for (int a = 0; a < 3; ++a) {
            if (!(mirror_.limitedAxes & (1 << a)) || delta[a] == 0) continue;
            const long long destination = mirror_.position[a] + delta[a];
            if ((delta[a] < 0 && destination < 0) || (delta[a] > 0 && destination > mirror_.travelMax[a])) {
                // I movimenti validi gia' nel piano vanno comunque eseguiti: ci si ferma sull'ultima
                // posizione buona, come quando a rifiutare e' il firmware.
                std::vector<planning::PlannedBlock> waiting;
                planner_.flush(waiting);
                sendBlocks(waiting, true);
                return {types::ResultCode::Rejected, "Rejected: LIMIT (outside the travel volume)", std::nullopt, {}};
            }
        }
        if (target.e && *target.e != 0.0) {
            // E e' sempre relativo; il resto frazionario passa al movimento successivo (nessuna deriva)
            const double exact = *target.e * mirror_.stepsPerMm[3] + mirror_.eRemainder;
            delta[3] = std::llround(exact);
            mirror_.eRemainder = exact - static_cast<double>(delta[3]);
        }
        if (target.feedrate && *target.feedrate > 0.0) mirror_.lastFeedrate = *target.feedrate;
        const double feedrate = mirror_.lastFeedrate > 0.0 ? mirror_.lastFeedrate : 1000.0;

        // Un movimento piu' lungo di un blocco viene spezzato in parti uguali, sulla stessa retta
        long long longest = 0;
        for (const long long d: delta) longest = std::max(longest, std::llabs(d));
        const long long parts = std::max(1LL, (longest + MoveFrame::STEP_LIMIT - 1) / MoveFrame::STEP_LIMIT);

        std::vector<planning::PlannedBlock> blocks;
        long long sent[4] = {0, 0, 0, 0};
        for (long long part = 1; part <= parts && longest > 0; ++part) {
            long long chunk[4];
            for (int a = 0; a < 4; ++a) {
                const long long upTo = delta[a] * part / parts;
                chunk[a] = upTo - sent[a];
                sent[a] = upTo;
            }
            planner_.push(chunk, feedrate, blocks);
        }
        for (int a = 0; a < 3; ++a) mirror_.position[a] += delta[a];
        if (!pipelined) planner_.flush(blocks);

        if (blocks.empty()) {
            types::Result result;
            result.code = types::ResultCode::Success;
            result.message = longest == 0 ? "Nothing to move" : "Planned";
            return result;
        }
        return sendBlocks(blocks, pipelined);
    }

    types::Result MotionCommands::setPosition(const MoveTarget &position) {
        MoveTarget p = position;
        p.e.reset();
        p.feedrate.reset();
        return sendCommand('M', 12, buildParams(p));
    }

    types::Result MotionCommands::moveTo(float x, float y, float z, float feedrate) {
        return moveRelative({x, y, z, std::nullopt, feedrate});
    }

    types::Result MotionCommands::goTo(float x, float y, float z, float feedrate) {
        return moveAbsolute({x, y, z, std::nullopt, feedrate});
    }

    types::Result MotionCommands::diagnoseAxis(const std::string &axis, float feedrate) {
        std::vector<std::string> params = {
            axis,
            "F" + utils::formatFloat(feedrate, 0)
        };
        return sendCommand('M', 99, params);
    }

    std::optional<position::Position> MotionCommands::getPosition(bool *referenced) {
        auto result = sendCommand('M', 114, {});
        if (!result.isSuccess()) return std::nullopt;

        // Risposta del firmware: "CRT POS <x> <y> <z> <riferita 0/1>"
        for (const auto &line: result.body) {
            std::istringstream iss(line);
            std::string token;
            iss >> token;
            if (token == "CRT") iss >> token;
            if (token != "POS") continue;

            position::Position pos{};
            if (iss >> pos.x >> pos.y >> pos.z) {
                int flag = 0;
                if (referenced) *referenced = (iss >> flag) && flag != 0;
                return pos;
            }
        }

        return std::nullopt;
    }
} // namespace core::command::motion
