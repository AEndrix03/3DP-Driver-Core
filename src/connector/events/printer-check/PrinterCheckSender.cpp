//
// Created by Andrea on 26/08/2025.
//

#include "connector/events/printer-check/PrinterCheckSender.hpp"

namespace connector::events::printer_check {
    PrinterCheckSender::PrinterCheckSender(const kafka::KafkaConfig &config)
        : kafka::KafkaProducerBase(config, "printer-check-response") {
    }
}
