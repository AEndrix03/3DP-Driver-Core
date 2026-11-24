#include "translator/GCodeSupport.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

namespace translator::gcode {
    namespace {
        const std::map<std::string, SupportInfo> &table() {
            static const std::map<std::string, SupportInfo> entries = {
                    // --- eseguiti ---
                    {"G0",   {Support::Executed, "movimento rapido"}},
                    {"G1",   {Support::Executed, "movimento lineare"}},
                    {"G2",   {Support::Executed, "arco orario"}},
                    {"G3",   {Support::Executed, "arco antiorario"}},
                    {"G5",   {Support::Executed, "spline cubica"}},
                    {"G4",   {Support::Executed, "pausa (dopo la fine dei movimenti in coda)"}},
                    {"G10",  {Support::Executed, "ritrazione"}},
                    {"G11",  {Support::Executed, "recupero della ritrazione"}},
                    {"M119", {Support::Executed, "stato dei finecorsa"}},
                    {"M702", {Support::Executed, "storico comandi"}},
                    {"G28",  {Support::Executed, "homing"}},
                    {"G90",  {Support::Executed, "coordinate assolute"}},
                    {"G91",  {Support::Executed, "coordinate relative"}},
                    {"G92",  {Support::Executed, "imposta la posizione"}},
                    {"M82",  {Support::Executed, "estrusione assoluta"}},
                    {"M83",  {Support::Executed, "estrusione relativa"}},
                    {"M104", {Support::Executed, "temperatura ugello"}},
                    {"M109", {Support::Executed, "temperatura ugello e attesa"}},
                    {"M105", {Support::Executed, "lettura temperatura"}},
                    {"M106", {Support::Executed, "ventola"}},
                    {"M107", {Support::Executed, "ventola spenta"}},
                    {"M114", {Support::Executed, "posizione"}},
                    {"M301", {Support::Executed, "guadagni PID dell'ugello"}},
                    {"M400", {Support::Executed, "attende la fine dei movimenti"}},
                    {"M204", {Support::Executed, "accelerazione di stampa (P o S) e di spostamento (T)"}},
                    {"M900", {Support::Executed, "linear advance (K)"}},
                    {"M24",  {Support::Executed, "riprendi"}},
                    {"M25",  {Support::Executed, "pausa"}},
                    {"M26",  {Support::Executed, "posizione nel file"}},
                    {"M112", {Support::Executed, "arresto di emergenza"}},
                    {"M999", {Support::Executed, "azzera errore"}},
                    {"G220", {Support::Executed, "comando interno"}},
                    {"G999", {Support::Executed, "comando interno"}},
                    // --- riconosciuti e saltati ---
                    {"G21",  {Support::Ignored, "unita' in millimetri: e' gia' cosi'"}},
                    {"T0",   {Support::Ignored, "selezione del primo estrusore: ce n'e' uno solo"}},
                    {"M73",  {Support::Ignored, "avanzamento per il display: il driver lo calcola da se'"}},
                    {"M84",  {Support::Ignored, "spegnimento motori: lo fa il firmware a fine stampa dopo l'inattivita'"}},
                    {"M18",  {Support::Ignored, "spegnimento motori: come M84"}},
                    {"M140", {Support::Ignored, "piano riscaldato: questa macchina non lo ha"}},
                    {"M190", {Support::Ignored, "piano riscaldato: questa macchina non lo ha"}},
                    {"M201", {Support::Ignored, "accelerazioni massime: sono fissate nel firmware"}},
                    {"M203", {Support::Ignored, "velocita' massime: sono fissate nel firmware"}},
                    {"M205", {Support::Ignored, "jerk: il firmware usa la deviazione di giunzione"}},
                    {"M115", {Support::Ignored, "informazioni sul firmware"}},
                    {"M117", {Support::Ignored, "messaggio per il display"}},
                    {"M220", {Support::Ignored, "correzione della velocita': non disponibile"}},
                    {"M221", {Support::Ignored, "correzione del flusso: non disponibile"}},
                    // --- la stampa uscirebbe sbagliata ---
                    {"G20",  {Support::Fatal, "unita' in pollici: tutte le quote verrebbero lette in millimetri"}},
                    {"G29",  {Support::Fatal, "livellamento del piano con sonda: non disponibile"}},
                    {"M600", {Support::Fatal, "cambio filamento: non disponibile"}},
            };
            return entries;
        }
    }

    SupportInfo classify(const std::string &command) {
        const auto it = table().find(command);
        if (it != table().end()) return it->second;
        // Controlli di compatibilita' di PrusaSlicer (M862.1 ... M862.6): non riguardano questa macchina
        if (command.rfind("M862", 0) == 0) return {Support::Ignored, "controllo di compatibilita' dello slicer"};
        // Un estrusore diverso dal primo non esiste
        if (command.size() >= 2 && command[0] == 'T' && std::isdigit(static_cast<unsigned char>(command[1]))) {
            return {Support::Fatal, "estrusore " + command.substr(1) + ": la macchina ne ha uno solo"};
        }
        return {Support::Unknown, "non riconosciuto"};
    }

    std::string commandOf(const std::string &rawLine) {
        std::string line = rawLine.substr(0, rawLine.find(';'));
        std::istringstream stream(line);
        std::string command;
        stream >> command;
        for (char &c: command) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        while (command.size() > 2 && command[1] == '0' && std::isdigit(static_cast<unsigned char>(command[2]))) {
            command.erase(1, 1);
        }
        return command;
    }

    FileReport checkFile(const std::string &path, double volumeX, double volumeY, double volumeZ) {
        FileReport report;
        std::ifstream file(path);
        if (!file.is_open()) {
            report.problems.push_back("impossibile aprire il file");
            return report;
        }
        report.readable = true;

        bool absolute = true, absoluteE = true, moved = false, homedBeforeMove = false;
        double x = 0, y = 0, z = 0, e = 0;
        std::string line;

        while (std::getline(file, line)) {
            ++report.lines;
            if (line.rfind(";LAYER", 0) == 0 || line.rfind(";LAYER_CHANGE", 0) == 0) ++report.layers;

            const std::string command = commandOf(line);
            if (command.empty()) continue;
            ++report.commands;

            const SupportInfo info = classify(command);
            if (info.support == Support::Ignored) ++report.ignored[command];
            else if (info.support == Support::Unknown) ++report.unknown[command];
            else if (info.support == Support::Fatal) ++report.fatal[command];

            // parametri numerici della riga
            std::map<char, double> params;
            {
                std::istringstream stream(line.substr(0, line.find(';')));
                std::string token;
                stream >> token;
                while (stream >> token) {
                    if (token.size() < 2) continue;
                    try {
                        params[static_cast<char>(std::toupper(static_cast<unsigned char>(token[0])))] =
                                std::stod(token.substr(1));
                    } catch (...) {}
                }
            }

            if (command == "G90") absolute = true;
            else if (command == "G91") absolute = absoluteE = false, absolute = false;
            else if (command == "M82") absoluteE = true;
            else if (command == "M83") absoluteE = false;
            else if (command == "G28") {
                if (!moved) homedBeforeMove = true;
                if (params.empty() || params.count('X')) x = 0;
                if (params.empty() || params.count('Y')) y = 0;
                if (params.empty() || params.count('Z')) z = 0;
            } else if (command == "G92") {
                if (params.count('X')) x = params['X'];
                if (params.count('Y')) y = params['Y'];
                if (params.count('Z')) z = params['Z'];
                if (params.count('E')) e = params['E'];
            } else if (command == "M104" || command == "M109") {
                if (params.count('S')) report.maxHotend = std::max(report.maxHotend, params['S']);
            } else if (command == "G0" || command == "G1" || command == "G2" || command == "G3" || command == "G5") {
                moved = true;
                ++report.moves;
                if (command == "G2" || command == "G3" || command == "G5") ++report.arcs;
                if (params.count('X')) x = absolute ? params['X'] : x + params['X'];
                if (params.count('Y')) y = absolute ? params['Y'] : y + params['Y'];
                if (params.count('Z')) z = absolute ? params['Z'] : z + params['Z'];
                if (params.count('E')) {
                    const double target = absoluteE ? params['E'] : e + params['E'];
                    if (target > e) report.filamentMm += target - e;
                    e = target;
                }
                if (!report.hasBounds) {
                    report.minX = report.maxX = x;
                    report.minY = report.maxY = y;
                    report.minZ = report.maxZ = z;
                    report.hasBounds = true;
                } else {
                    report.minX = std::min(report.minX, x);
                    report.maxX = std::max(report.maxX, x);
                    report.minY = std::min(report.minY, y);
                    report.maxY = std::max(report.maxY, y);
                    report.minZ = std::min(report.minZ, z);
                    report.maxZ = std::max(report.maxZ, z);
                }
            }
        }
        report.homes = homedBeforeMove;

        // --- giudizio ---
        for (const auto &[command, count]: report.fatal) {
            report.problems.push_back(command + " (" + std::to_string(count) + " volte): " + classify(command).note);
        }
        if (report.moves == 0) report.problems.push_back("il file non contiene movimenti");
        if (report.hasBounds) {
            const double tolerance = 0.001;
            if (report.minX < -tolerance || report.minY < -tolerance || report.minZ < -tolerance) {
                report.problems.push_back("il percorso scende sotto lo zero di un asse");
            }
            if ((volumeX > 0 && report.maxX > volumeX + tolerance) || (volumeY > 0 && report.maxY > volumeY + tolerance) ||
                (volumeZ > 0 && report.maxZ > volumeZ + tolerance)) {
                report.problems.push_back("il percorso esce dal volume di lavoro");
            }
        }
        if (report.moves > 0 && !report.homes) {
            report.warnings.push_back("nessun G28 prima del primo movimento: la posizione di partenza non e' riferita");
        }
        for (const auto &[command, count]: report.unknown) {
            report.warnings.push_back(command + " (" + std::to_string(count) + " volte): non riconosciuto, verra' saltato");
        }
        return report;
    }
}
