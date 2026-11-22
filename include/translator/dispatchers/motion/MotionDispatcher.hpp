//
// Created by redeg on 01/05/2025.
//

#pragma once

#include "translator/dispatchers/ICommandDispatcher.hpp"
#include <memory>
#include <optional>
#include <vector>

#include "core/DriverInterface.hpp"

namespace translator::gcode {

    /**
     * @brief Traduce i G-code di movimento in segmenti rettilinei per il firmware.
     *
     * Gestisce: G0/G1 (rette, con E), G2/G3 (archi I/J, anche elicoidali),
     * G5 (spline cubica di Bezier), G28 (homing), G90/G91, M82/M83, G92.
     * Archi e spline vengono suddivisi con errore di corda controllato
     * (CURVE_TOLERANCE_MM), non con un numero fisso di segmenti.
     */
    class MotionDispatcher : public ICommandDispatcher {
    public:
        /// Scostamento massimo tra curva ideale e segmenti (mm)
        static constexpr double CURVE_TOLERANCE_MM = 0.02;
        /// Segmenti piu' corti di cosi' non migliorano la stampa e rallentano il flusso
        static constexpr double MIN_SEGMENT_MM = 0.2;
        static constexpr int MAX_SEGMENTS = 2000;

        struct Point {
            double x = 0.0;
            double y = 0.0;
        };

        explicit MotionDispatcher(std::shared_ptr<core::DriverInterface> driver);

        bool canHandle(const std::string &command) const override;

        bool validate(const std::string &command, const std::map<std::string, double> &params) const override;

        void handle(const std::string &command, const std::map<std::string, double> &params) override;

        /**
         * @brief Punti di un arco (escluso il punto iniziale, incluso quello finale esatto).
         * @param clockwise true per G2
         */
        static std::vector<Point> flattenArc(Point start, Point end, Point center, bool clockwise);

        /**
         * @brief Punti di una Bezier cubica (escluso p0, incluso p3 esatto).
         */
        static std::vector<Point> flattenBezier(Point p0, Point p1, Point p2, Point p3);

    private:
        std::shared_ptr<core::DriverInterface> driver_;

        bool absoluteXYZ_ = true;
        bool absoluteE_ = true;
        double logicalE_ = 0.0;
        double feedrate_ = 1500.0;
        double lastLayerZ_ = 0.0;

        // Posizione macchina nota (mm). Vuota dopo homing, errori o abort:
        // viene riletta dal firmware (M114) solo quando serve davvero.
        std::optional<position::Position> position_;

        // Secondo punto di controllo dell'ultimo G5 (per G5 concatenati senza I/J)
        std::optional<Point> lastSplineControl_;

        position::Position currentPosition();

        void ensurePrinting();

        std::optional<double> extrusionDelta(const std::map<std::string, double> &params);

        void linearMove(const std::map<std::string, double> &params);

        void home();

        void setPosition(const std::map<std::string, double> &params);

        /// Invia una polilinea in coordinate assolute distribuendo Z ed E lungo il percorso.
        void sendPath(const position::Position &start, const std::vector<Point> &points,
                      std::optional<double> targetZ, std::optional<double> extrusion);

        void trackLayer(const std::map<std::string, double> &params);
    };

}
