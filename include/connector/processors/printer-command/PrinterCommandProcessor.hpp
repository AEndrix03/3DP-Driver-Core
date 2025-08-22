#pragma once

#include "../BaseProcessor.hpp"
#include "../../models/printer-command/PrinterCommandRequest.hpp"
#include "../../models/printer-command/PrinterCommandResponse.hpp"

namespace connector::processors::printer_command {

    class PrinterCommandProcessor : public BaseProcessor {
    public:
        void processPrinterCommandRequest(std::shared_ptr<events::printer-command::PrinterCommandSender> sender,
                               std::shared_ptr<translator::gcode::GCodeTranslator> translator,
                                const std::string &driverId);

        void dispatch(const connector::models::printer_command::PrinterCommandRequest &request);

        std::string getProcessorName() const override {
            return "PrinterCommandProcessor";
        }

        bool isReady() const override {
            return true;
        }

    private:
        std::shared_ptr<events::printer-command::PrinterCommandSender> sender_;
        std::shared_ptr<translator::gcode::GCodeTranslator> translator_;
        std::string driverId_;
    };

}