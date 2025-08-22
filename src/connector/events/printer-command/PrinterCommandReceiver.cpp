#include "connector/events/printer-command/PrinterCommandReceiver.hpp"

namespace connector::events::printer_command {

    PrinterCommandReceiver::PrinterCommandReceiver(const kafka::KafkaConfig &config)
            : kafka::KafkaConsumerBase(config, "printer-command-request") {
    }

}