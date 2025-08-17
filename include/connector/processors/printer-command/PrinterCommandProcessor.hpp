#pragma once

#include "../BaseProcessor.hpp"
#include "../../models/printer-command/PrinterCommandRequest.hpp"
#include "../../models/printer-command/PrinterCommandResponse.hpp"

namespace connector::processors::printer_command {

    class PrinterCommandProcessor : public BaseProcessor {
    public:
        void processPrinterCommandRequest(const connector::models::printer_command::PrinterCommandRequest &request) {
            // TODO: Process GCode command execution request
            // TODO: Validate command based on priority
            // TODO: Execute command via DriverInterface or GCodeTranslator
            // TODO: Capture execution result (success/failure)
            // TODO: Capture any exceptions or error info
            // TODO: Create PrinterCommandResponse with results
            // TODO: Call PrinterCommandSender to send response
            (void) request;
        }

        std::string getProcessorName() const override {
            return "PrinterCommandProcessor";
        }

        bool isReady() const override {
            // TODO: Check if command execution dependencies are ready
            return false;
        }
    };

}