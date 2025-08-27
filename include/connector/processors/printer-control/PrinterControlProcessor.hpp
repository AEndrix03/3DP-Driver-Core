#pragma once

#include "../BaseProcessor.hpp"
#include "../../models/printer-control/PrinterStartRequest.hpp"
#include "../../models/printer-control/PrinterStopRequest.hpp"
#include "../../models/printer-control/PrinterPauseRequest.hpp"
#include "core/DriverInterface.hpp"
#include "core/queue/CommandExecutorQueue.hpp"
#include "core/printer/job/PrintJobManager.hpp"
#include <memory>

namespace connector::processors::printer_control {
    class PrinterControlProcessor : public BaseProcessor {
    public:
        PrinterControlProcessor(std::shared_ptr<core::DriverInterface> driver,
                                std::shared_ptr<core::CommandExecutorQueue> commandQueue,
                                std::shared_ptr<core::print::PrintJobManager> jobManager);

        void processPrinterStartRequest(const models::printer_control::PrinterStartRequest &request);

        void processPrinterStopRequest(const models::printer_control::PrinterStopRequest &request) const;

        void processPrinterPauseRequest(const models::printer_control::PrinterPauseRequest &request) const;

        std::string getProcessorName() const override {
            return "PrinterControlProcessor";
        }

        bool isReady() const override {
            return driver_ && commandQueue_ && jobManager_;
        }

    private:
        std::shared_ptr<core::DriverInterface> driver_;
        std::shared_ptr<core::CommandExecutorQueue> commandQueue_;
        std::shared_ptr<core::print::PrintJobManager> jobManager_;

        void executeGCodeSequence(const std::string &gcode, const std::string &jobId) const;

        static std::string generateJobId(const std::string &driverId);
    };
}
