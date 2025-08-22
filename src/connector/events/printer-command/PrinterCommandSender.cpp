#include "connector/events/printer-command/PrinterCommandSender.hpp"

namespace connector::events::printer_command {

    PrinterCommandSender::PrinterCommandSender(const kafka::KafkaConfig &config)
            : kafka::KafkaProducerBase(config, "printer-command-response") {
    }

}