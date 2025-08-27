//
// Created by Andrea on 26/08/2025.
//

#pragma once

#include "../BaseProcessor.hpp"
#include "../../models/printer-check/PrinterCheckRequest.hpp"
#include "../../models/printer-check/PrinterCheckResponse.hpp"
#include "../../events/printer-check/PrinterCheckSender.hpp"
#include "core/DriverInterface.hpp"
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

        void collectPositionDataAsync(models::printer_check::PrinterCheckResponse &response) const;

        void collectTemperatureDataAsync(models::printer_check::PrinterCheckResponse &response) const;

        static void collectFanDataAsync(models::printer_check::PrinterCheckResponse &response);

        static void collectJobStatusDataAsync(models::printer_check::PrinterCheckResponse &response,
                                              const std::string &jobId);

        void collectDiagnosticDataAsync(models::printer_check::PrinterCheckResponse &response) const;

        static double parseTemperatureFromResponse(const std::string &response);

        static std::string formatDouble(double value);

        // State management
        std::string getJobStatusCode(const std::string &jobId) const;

        std::string getPrinterStatusCode() const;

        static std::string parseFanFromResponse(const std::string &response);
    };
} // namespace connector::processors::printer_check
