#pragma once

#include "../BaseProcessor.hpp"
#include "../../models/printer-control/PrinterStartRequest.hpp"
#include "../../models/printer-control/PrinterStopRequest.hpp"
#include "../../models/printer-control/PrinterPauseRequest.hpp"

namespace connector::processors::printer_control {

    class PrinterControlProcessor : public BaseProcessor {
    public:
        void processPrinterStartRequest(const connector::models::printer_control::PrinterStartRequest &request) {
            // TODO: Process start print request
            // TODO: Validate current printer state can start
            // TODO: Call SystemCommands::startPrint()
            // TODO: Send appropriate response/event
            (void) request;
        }

        void processPrinterStopRequest(const connector::models::printer_control::PrinterStopRequest &request) {
            // TODO: Process stop print request
            // TODO: Call emergency stop or graceful stop based on context
            // TODO: Update print state
            // TODO: Send appropriate response/event
            (void) request;
        }

        void processPrinterPauseRequest(const connector::models::printer_control::PrinterPauseRequest &request) {
            // TODO: Process pause print request
            // TODO: Call SystemCommands::pause()
            // TODO: Update print state
            // TODO: Send appropriate response/event
            (void) request;
        }

        std::string getProcessorName() const override {
            return "PrinterControlProcessor";
        }

        bool isReady() const override {
            // TODO: Check if SystemCommands and printer are ready
            return false;
        }
    };

}