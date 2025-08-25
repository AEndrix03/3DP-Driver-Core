//
// Created by Andrea on 26/08/2025.
//

#pragma once

#include "../BaseProcessor.hpp"
#include "../../models/printer-check/PrinterCheckRequest.hpp"
#include "../../models/printer-check/PrinterCheckResponse.hpp"
#include "../../events/printer-check/PrinterCheckSender.hpp"
#include "core/DriverInterface.hpp"
#include "core/printer/PrintState.hpp"
#include "core/queue/CommandExecutorQueue.hpp"
#include <memory>
#include <string>

namespace connector::processors::printer_check {
    class PrinterCheckProcessor : public BaseProcessor {
    public:
        PrinterCheckProcessor(std::shared_ptr<events::printer_check::PrinterCheckSender> sender,
                              std::shared_ptr<core::DriverInterface> driver,
                              std::shared_ptr<core::CommandExecutorQueue> commandQueue,
                              const std::string &driverId);

        void processPrinterCheckRequest(const connector::models::printer_check::PrinterCheckRequest &request);

        std::string getProcessorName() const override {
            return "PrinterCheckProcessor";
        }

        bool isReady() const override {
            return sender_ && sender_->isReady() && driver_ != nullptr;
        }

    private:
        std::shared_ptr<events::printer_check::PrinterCheckSender> sender_;
        std::shared_ptr<core::DriverInterface> driver_;
        std::shared_ptr<core::CommandExecutorQueue> commandQueue_; // For job status tracking
        std::string driverId_;

        void sendResponse(const connector::models::printer_check::PrinterCheckResponse &response);

        void sendErrorResponse(const std::string &jobId, const std::string &error);

        // Helper methods to collect printer state
        void collectPositionData(connector::models::printer_check::PrinterCheckResponse &response);

        void collectTemperatureData(connector::models::printer_check::PrinterCheckResponse &response);

        void collectFanData(connector::models::printer_check::PrinterCheckResponse &response);

        void collectJobStatusData(connector::models::printer_check::PrinterCheckResponse &response,
                                  const std::string &jobId);

        void collectDiagnosticData(connector::models::printer_check::PrinterCheckResponse &response);

        // State management
        std::string getJobStatusCode(const std::string &jobId) const;

        std::string getPrinterStatusCode() const;

        // Helper parsers
        std::string parseTemperatureFromResponse(const std::string &response) const;

        std::string parseFanFromResponse(const std::string &response) const;
    };
} // namespace connector::processors::printer_check
