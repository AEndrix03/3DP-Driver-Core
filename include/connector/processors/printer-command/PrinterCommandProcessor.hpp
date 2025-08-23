#pragma once

#include "../BaseProcessor.hpp"
#include "../../models/printer-command/PrinterCommandRequest.hpp"
#include "../../models/printer-command/PrinterCommandResponse.hpp"
#include "../../events/printer-command/PrinterCommandSender.hpp"
#include "../../../core/queue/CommandExecutorQueue.hpp"

namespace connector::processors::printer_command {
    class PrinterCommandProcessor : public BaseProcessor {
    public:
        PrinterCommandProcessor(std::shared_ptr<events::printer_command::PrinterCommandSender> sender,
                                std::shared_ptr<core::CommandExecutorQueue> commandQueue,
                                const std::string &driverId);

        void dispatch(const connector::models::printer_command::PrinterCommandRequest &request);

        std::string getProcessorName() const override {
            return "PrinterCommandProcessor";
        }

        bool isReady() const override {
            return commandQueue_ && sender_ && sender_->isReady();
        }

    private:
        std::shared_ptr<events::printer_command::PrinterCommandSender> sender_;
        std::shared_ptr<core::CommandExecutorQueue> commandQueue_;
        std::string driverId_;

        void sendResponse(const connector::models::printer_command::PrinterCommandResponse &response);

        void sendErrorResponse(const std::string &requestId, const std::string &exception, const std::string &message);

        std::vector<std::string> splitCommands(const std::string &command);
    };
}
