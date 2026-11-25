#include "core/motion/MotionPlanner.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace core::planning {
    namespace {
        constexpr double MIN_SPEED = 0.5;            // mm/s lungo il percorso
        constexpr double PLANNING_MARGIN = 0.9;      // come il firmware: distanze calcolate col 90% dell'accelerazione
        constexpr long long GAIN_PER_STEP = 117965;  // stessa costante del firmware (Planner.cpp)
        constexpr int MAX_ACCELERATION = 30000;
        constexpr int QUEUE_MARGIN = 5;              // blocchi della coda del firmware su cui non si conta
        constexpr double SPLIT_MIN_LENGTH = 2.0;     // mm: sotto, il linear advance si applica al segmento intero
        constexpr double MIN_PHASE_LENGTH = 0.3;     // mm: fasi piu' corte si fondono con quella accanto
        constexpr double ADVANCE_MAX_JUMP = 5.0;     // mm/s: salto di velocita' dell'estrusore tollerato a inizio rampa
        constexpr double UNLIMITED = std::numeric_limits<double>::infinity();

        /// Le velocita' di ingresso viaggiano in quarti di mm/s: si pianifica gia' su quella griglia.
        double floorQuarter(double speed) {
            return std::floor(speed * 4.0 + 1e-9) / 4.0;
        }
    }

    void MotionPlanner::configure(const MachineLimits &limits) {
        limits_ = limits;
        reset();
    }

    void MotionPlanner::setAccelerations(double print, double travel) {
        printAcceleration_ = print > 0.0 ? print : 0.0;
        travelAcceleration_ = travel > 0.0 ? travel : 0.0;
    }

    void MotionPlanner::reset() {
        pending_.clear();
        advance_ = 0.0;
        advanceRemainder_ = 0.0;
    }

    int MotionPlanner::horizon() const {
        return std::max(2, limits_.queueBlocks - QUEUE_MARGIN);
    }

    bool MotionPlanner::describe(Segment &s, double feedMmS) const {
        double mm[4];
        s.count = 0;
        for (int a = 0; a < 4; ++a) {
            mm[a] = static_cast<double>(s.steps[a]) / limits_.stepsPerMm[a];
            s.count = std::max(s.count, std::llabs(s.steps[a]));
        }
        if (s.count == 0) return false;

        const double path = std::sqrt(mm[0] * mm[0] + mm[1] * mm[1] + mm[2] * mm[2]);
        s.cartesian = s.steps[0] != 0 || s.steps[1] != 0 || s.steps[2] != 0;
        // Un movimento quasi tutto di estrusore (percorso minuscolo) si tratta come sola estrusione:
        // la scala "step per mm di percorso" non starebbe nel campo del frame.
        if (s.cartesian && static_cast<double>(s.count) / path * 16.0 > 65535.0) s.cartesian = false;
        s.length = s.cartesian ? path : std::fabs(mm[3]);
        if (s.length <= 0.0) return false;

        const double stepsPerMm = static_cast<double>(s.count) / s.length;
        s.stepScale = static_cast<int>(std::clamp(std::llround(stepsPerMm * 16.0), 1LL, 65535LL));

        s.print = s.cartesian && s.steps[3] > 0;
        s.extrusion = s.print ? mm[3] / s.length : 0.0;
        for (int a = 0; a < 3; ++a) s.unit[a] = s.cartesian ? mm[a] / s.length : 0.0;

        s.cost = (s.print && advanceK_ > 0.0 && s.length >= SPLIT_MIN_LENGTH) ? 3 : 1;

        // --- velocita' e accelerazione: quelle richieste, entro i limiti di ogni asse ---
        double speed = feedMmS * (s.cartesian ? speedFactor_ : 1.0); // ritrazioni e spurghi restano come chiesti
        // non piu' in fretta di quanto i suoi blocchi si riescono a inviare
        if (blockRate_ > 0.0) speed = std::min(speed, s.length * blockRate_ / s.cost);
        speed = std::max(speed, MIN_SPEED);
        double acceleration = limits_.maxAcceleration[3];
        if (s.cartesian) {
            acceleration = limits_.maxAcceleration[0];
            const double requested = s.print ? printAcceleration_ : travelAcceleration_;
            if (requested > 0.0) acceleration = std::min(acceleration, requested);
        }
        for (int a = 0; a < 4; ++a) {
            const double share = std::fabs(mm[a]) / s.length; // quota del percorso su questo asse
            if (share <= 0.0) continue;
            speed = std::min(speed, limits_.maxFeed[a] / share);
            acceleration = std::min(acceleration, limits_.maxAcceleration[a] / share);
        }
        speed = std::min(speed, limits_.maxStepRate / stepsPerMm);
        if (s.print && advanceK_ > 0.0 && s.extrusion > 0.0) {
            // Con il linear advance l'estrusore cambia velocita' di colpo a inizio e fine rampa,
            // di K * estrusione * accelerazione: si tiene quel salto entro un valore sopportabile.
            acceleration = std::min(acceleration, ADVANCE_MAX_JUMP / (advanceK_ * s.extrusion));
        }

        const long long nominalUnits = std::clamp(static_cast<long long>(std::floor(speed * 64.0)), 1LL, 65535LL);
        s.nominal = static_cast<double>(nominalUnits) / 64.0;
        s.acceleration = static_cast<int>(std::clamp(static_cast<long long>(std::floor(acceleration)), 1LL,
                                                     static_cast<long long>(MAX_ACCELERATION)));

        // Quanto puo' cambiare la velocita' lungo il segmento: la stessa aritmetica intera del firmware,
        // cosi' il suo controllo di arresto conferma il piano invece di correggerlo.
        const long long gain = std::max(1LL, s.acceleration * GAIN_PER_STEP / s.stepScale);
        const long long nominalSqr = nominalUnits * nominalUnits;
        s.gainSqr = s.count >= nominalSqr / gain ? UNLIMITED : static_cast<double>(gain * s.count) / 4096.0;
        s.gainPerMm = static_cast<double>(gain) * static_cast<double>(s.count) / 4096.0 / s.length;
        return true;
    }

    double MotionPlanner::junctionSpeed(const Segment &before, const Segment &after) const {
        if (!before.cartesian || !after.cartesian) return 0.0; // l'estrusore da solo parte e arriva da fermo

        const double cosTheta = -(before.unit[0] * after.unit[0] + before.unit[1] * after.unit[1] +
                                  before.unit[2] * after.unit[2]);
        const double limit = std::min(before.nominal, after.nominal);
        if (cosTheta < -0.999999) return limit; // stessa direzione: nessun rallentamento
        if (cosTheta > 0.999999) return 0.0;    // inversione completa

        const double sinHalf = std::sqrt(0.5 * (1.0 - cosTheta));
        const double acceleration = PLANNING_MARGIN * std::min(before.acceleration, after.acceleration);
        const double speed = std::sqrt(acceleration * limits_.junctionDeviation * sinHalf / (1.0 - sinHalf));
        return std::min(speed, limit);
    }

    double PlannedBlock::seconds() const {
        if (length <= 0.0 || nominal <= 0.0 || acceleration <= 0.0 || deceleration <= 0.0) return 0.0;
        const double v0 = std::min(entry, nominal), v1 = std::min(exit, nominal);
        const double up = (nominal * nominal - v0 * v0) / (2.0 * acceleration);
        const double down = (nominal * nominal - v1 * v1) / (2.0 * deceleration);
        if (up + down <= length) {
            return (nominal - v0) / acceleration + (nominal - v1) / deceleration + (length - up - down) / nominal;
        }
        // troppo corto per arrivare alla velocita' nominale: si accelera fino a un picco e si frena
        const double peakSqr = (2.0 * acceleration * deceleration * length + deceleration * v0 * v0 +
                                acceleration * v1 * v1) / (acceleration + deceleration);
        const double peak = std::sqrt(std::max(peakSqr, std::max(v0 * v0, v1 * v1)));
        return (peak - v0) / acceleration + (peak - v1) / deceleration;
    }

    void MotionPlanner::push(const long long steps[4], double feedMmMin, std::vector<PlannedBlock> &out,
                             uint32_t tag) {
        Segment segment;
        segment.tag = tag;
        for (int a = 0; a < 4; ++a) segment.steps[a] = steps[a];
        if (!describe(segment, feedMmMin / 60.0)) return;

        // Coda vuota = il piano emesso finora si chiude con un arresto: si riparte da fermi
        segment.junction = pending_.empty() ? 0.0 : floorQuarter(junctionSpeed(pending_.back(), segment));
        segment.entry = 0.0;
        pending_.push_back(segment);
        release(false, out);
    }

    void MotionPlanner::flush(std::vector<PlannedBlock> &out) {
        release(true, out);
    }

    void MotionPlanner::release(bool closing, std::vector<PlannedBlock> &out) {
        const int budget = horizon();

        while (!pending_.empty()) {
            double exit = 0.0;
            if (pending_.size() > 1) {
                // Finestra: i blocchi che il firmware avra' in coda dopo questo segmento
                size_t end = 1;
                int cost = 0;
                while (end < pending_.size() && (end == 1 || cost + pending_[end].cost <= budget)) {
                    cost += pending_[end].cost;
                    ++end;
                }
                const bool complete = end < pending_.size() || cost >= budget;
                if (!complete && !closing) return; // servono altri segmenti per decidere

                // All'indietro: in fondo alla finestra si deve poter essere fermi
                double bound = 0.0;
                for (size_t k = end - 1; k >= 1; --k) {
                    const Segment &s = pending_[k];
                    bound = floorQuarter(std::min(s.junction, std::sqrt(bound * bound + s.gainSqr)));
                }
                // In avanti: non piu' di quanto questo segmento riesce ad accelerare
                const Segment &first = pending_.front();
                const double reachable = floorQuarter(std::sqrt(first.entry * first.entry + first.gainSqr));
                exit = std::min(bound, reachable);
            } else if (!closing) {
                return;
            }

            const Segment first = pending_.front();
            pending_.pop_front();
            if (!pending_.empty()) pending_.front().entry = exit;
            emit(first, exit, out);
        }
    }

    void MotionPlanner::emit(const Segment &s, double exit, std::vector<PlannedBlock> &out) {
        if (s.cost < 3 || advanceK_ <= 0.0) {
            emitPart(s, s.steps, s.entry, exit, out);
            return;
        }

        // --- linear advance: il segmento si divide in accelerazione, velocita' costante, frenata ---
        // L'estrusore deve trovarsi "avanti" di K * (velocita' dell'estrusore): avanza di piu' mentre
        // si accelera e di meno mentre si frena. Dentro un blocco il rapporto tra estrusore e
        // percorso e' fisso, quindi ogni fase diventa un blocco con il suo rapporto.
        const double v0 = s.entry, v1 = exit, vn = s.nominal;
        const double accel2 = 2.0 * s.acceleration; // lo Stepper accelera con l'accelerazione vera
        const double decel2 = s.gainPerMm;          // e frena dove dice il piano (90%)

        double accelLength = std::max(0.0, (vn * vn - v0 * v0) / accel2);
        double decelLength = std::max(0.0, (vn * vn - v1 * v1) / decel2);
        double peak = vn;
        if (accelLength + decelLength > s.length) {
            // troppo corto per arrivare alla velocita' nominale
            accelLength = std::clamp((v1 * v1 - v0 * v0 + decel2 * s.length) / (accel2 + decel2), 0.0, s.length);
            decelLength = s.length - accelLength;
            peak = std::sqrt(v0 * v0 + accel2 * accelLength);
        } else {
            decelLength = std::min(decelLength * 1.02, s.length - accelLength); // margine sugli arrotondamenti
        }
        if (accelLength < MIN_PHASE_LENGTH) accelLength = 0.0;
        if (decelLength < MIN_PHASE_LENGTH) decelLength = 0.0;
        double cruiseEnd = s.length - decelLength;
        if (cruiseEnd - accelLength < MIN_PHASE_LENGTH) cruiseEnd = accelLength;

        const double cuts[4] = {0.0, accelLength / s.length, cruiseEnd / s.length, 1.0};
        long long done[4] = {0, 0, 0, 0};
        const double peakEntry = floorQuarter(peak);
        bool firstPart = true;

        for (int phase = 0; phase < 3; ++phase) {
            long long part[4];
            bool any = false;
            for (int a = 0; a < 4; ++a) {
                const long long upTo = phase == 2 ? s.steps[a]
                                                  : std::llround(static_cast<double>(s.steps[a]) * cuts[phase + 1]);
                part[a] = upTo - done[a];
                if (a < 3 && part[a] != 0) any = true;
            }
            if (!any && phase != 2) continue; // fase vuota: i suoi step (solo E) passano alla successiva
            for (int a = 0; a < 4; ++a) done[a] += part[a];
            const bool last = phase == 2 || cuts[phase + 1] >= 1.0;
            emitPart(s, part, firstPart ? v0 : peakEntry, last ? v1 : peakEntry, out);
            firstPart = false;
            if (last) break;
        }
    }

    void MotionPlanner::emitPart(const Segment &parent, const long long steps[4], double entry, double exit,
                                 std::vector<PlannedBlock> &out) {
        PlannedBlock block;
        long long delta[4] = {steps[0], steps[1], steps[2], steps[3]};

        // --- linear advance: alla fine del blocco l'anticipo deve valere K * velocita' dell'estrusore ---
        const double target = parent.print ? advanceK_ * parent.extrusion * exit : 0.0;
        if (target != advance_ || advanceRemainder_ != 0.0) {
            const double exact = (target - advance_) * limits_.stepsPerMm[3] + advanceRemainder_;
            const long long extra = std::llround(exact);
            advanceRemainder_ = exact - static_cast<double>(extra);
            advance_ = target;
            delta[3] = std::clamp(delta[3] + extra, -static_cast<long long>(MoveFrame::STEP_LIMIT),
                                  static_cast<long long>(MoveFrame::STEP_LIMIT));
        }

        long long count = 0;
        for (int a = 0; a < 4; ++a) {
            block.move.steps[a] = static_cast<int32_t>(delta[a]);
            count = std::max(count, std::llabs(delta[a]));
        }
        if (count == 0) return;

        // La geometria del pezzo: uguale al segmento intero salvo arrotondamenti e step di anticipo
        double length = parent.length;
        if (parent.cartesian) {
            double sum = 0.0;
            for (int a = 0; a < 3; ++a) {
                const double mm = static_cast<double>(delta[a]) / limits_.stepsPerMm[a];
                sum += mm * mm;
            }
            length = std::sqrt(sum);
        } else {
            length = std::fabs(static_cast<double>(delta[3])) / limits_.stepsPerMm[3];
        }
        double nominal = parent.nominal;
        long long stepScale = parent.stepScale;
        if (length > 0.0) {
            const double stepsPerMm = static_cast<double>(count) / length;
            stepScale = std::clamp(std::llround(stepsPerMm * 16.0), 1LL, 65535LL);
            // gli step di anticipo possono rendere dominante l'estrusore: il generatore di step ha un tetto
            const double fastest = std::floor(limits_.maxStepRate / stepsPerMm * 64.0) / 64.0;
            if (nominal > fastest) nominal = std::max(fastest, 1.0 / 64.0);
        }
        entry = std::min(entry, nominal);

        block.move.nominalSpeed = static_cast<uint16_t>(std::clamp(std::llround(nominal * 64.0), 1LL, 65535LL));
        block.move.acceleration = static_cast<uint16_t>(parent.acceleration);
        block.move.stepScale = static_cast<uint16_t>(stepScale);
        block.move.entryQuarter = static_cast<uint16_t>(
                std::clamp(static_cast<long long>(std::floor(entry * 4.0 + 1e-9)), 0LL,
                           static_cast<long long>(MoveFrame::ENTRY_LIMIT)));
        block.entry = entry;
        block.exit = exit;
        block.nominal = nominal;
        block.length = length;
        block.acceleration = parent.acceleration;
        block.deceleration = parent.gainPerMm > 0.0 ? parent.gainPerMm / 2.0 : parent.acceleration;
        block.tag = parent.tag;
        out.push_back(block);
    }

} // namespace core::planning
