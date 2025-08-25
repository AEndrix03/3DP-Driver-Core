//
// Created by Andrea on 26/08/2025.
//

#include "connector/events/printer-check/PrinterCheckReceiver.hpp"

namespace connector::events::printer_check {
    PrinterCheckReceiver::PrinterCheckReceiver(const kafka::KafkaConfig &config)
        : kafka::KafkaConsumerBase(config, "printer-check-request") {
    }
}
