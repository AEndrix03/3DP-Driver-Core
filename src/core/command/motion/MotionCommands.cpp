#include "core/command/motion/MotionCommands.hpp"
#include "core/DriverInterface.hpp"
#include "core/utils/FloatFormatter.hpp"
#include "core/MoveFrame.hpp"
#include <sstream>
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
        bool gotFactors = false, gotPosition = false;

        // Se la risposta va persa il comando risulta "gia' eseguito" ma senza dati: si richiede.
        for (int attempt = 0; attempt < 3 && !(gotFactors && gotPosition); ++attempt) {
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
                }
            }
        }
        if (!gotFactors || !gotPosition) {
            result.code = types::ResultCode::Error;
            result.message = "Firmware did not report step position";
            return result;
        }

        mirror_.valid = true;
        mirror_.epoch = epoch;
        mirror_.eRemainder = 0.0;  // come il firmware dopo un arresto: si riparte senza resto
        mirror_.lastFeedrate = 0.0; // la prossima velocita' viene sempre inviata
        return result;
    }

    types::Result MotionCommands::move(const MoveTarget &target, bool absolute, bool pipelined) {
        std::lock_guard<std::mutex> lock(mirrorMutex_);

        if (!mirror_.valid || mirror_.epoch != driver_->positionEpoch()) {
            mirror_.valid = false;
            const types::Result synced = syncMirror();
            if (!synced.isSuccess()) return synced;
        }

        long long delta[4] = {0, 0, 0, 0};
        const std::optional<double> axes[3] = {target.x, target.y, target.z};
        for (int a = 0; a < 3; ++a) {
            if (!axes[a]) continue;
            const long long steps = std::llround(*axes[a] * mirror_.stepsPerMm[a]);
            delta[a] = absolute ? steps - mirror_.position[a] : steps;
        }
        double remainder = mirror_.eRemainder;
        if (target.e && *target.e != 0.0) {
            // E e' sempre relativo; il resto frazionario passa al movimento successivo (nessuna deriva)
            const double exact = *target.e * mirror_.stepsPerMm[3] + mirror_.eRemainder;
            delta[3] = std::llround(exact);
            remainder = exact - static_cast<double>(delta[3]);
        }

        uint32_t feedrate = 0;
        if (target.feedrate && *target.feedrate > 0.0 && *target.feedrate != mirror_.lastFeedrate) {
            feedrate = static_cast<uint32_t>(std::clamp(std::llround(*target.feedrate), 1LL,
                                                        static_cast<long long>(MoveFrame::FEED_LIMIT)));
        }

        // Un movimento piu' lungo di un frame viene spezzato in parti uguali, sulla stessa retta
        long long longest = 0;
        for (const long long d: delta) longest = std::max(longest, std::llabs(d));
        const long long parts = std::max(1LL, (longest + MoveFrame::WIDE_LIMIT - 1) / MoveFrame::WIDE_LIMIT);

        types::Result result;
        result.code = types::ResultCode::Success;
        result.message = "Nothing to move";
        if (longest == 0 && feedrate == 0) {
            mirror_.eRemainder = remainder; // meno di mezzo step: si accumula per il prossimo
            return result;
        }

        long long sent[4] = {0, 0, 0, 0};
        for (long long part = 1; part <= parts; ++part) {
            int32_t chunk[4];
            for (int a = 0; a < 4; ++a) {
                const long long upTo = delta[a] * part / parts;
                chunk[a] = static_cast<int32_t>(upTo - sent[a]);
                sent[a] = upTo;
            }
            result = driver_->sendMove(chunk, part == 1 ? feedrate : 0, pipelined);
            if (!result.isSuccess()) {
                mirror_.valid = false; // la posizione verra' riletta dal firmware
                return result;
            }
        }

        for (int a = 0; a < 3; ++a) mirror_.position[a] += delta[a];
        mirror_.eRemainder = remainder;
        if (feedrate > 0) mirror_.lastFeedrate = *target.feedrate;
        return result;
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
