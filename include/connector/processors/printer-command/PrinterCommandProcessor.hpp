#pragma once

#include "../BaseProcessor.hpp"
#include "../../models/printer-command/PrinterCommandRequest.hpp"
#include "../../events/printer-command/PrinterCommandSender.hpp"
#include "../../models/printer-command/PrinterCommandResponse.hpp"
#include "../../../translator/GCodeTranslator.hpp"

namespace connector::processors::printer_command {
    class PrinterCommandProcessor : public BaseProcessor {
    public:
        PrinterCommandProcessor(std::shared_ptr<events::printer_command::PrinterCommandSender> sender,
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
        std::shared_ptr<events::printer_command::PrinterCommandSender> sender_;
        std::shared_ptr<translator::gcode::GCodeTranslator> translator_;
        std::string driverId_;
    };
}
