#pragma once
#include <string>

namespace core {

class PrinterInterface {
public:
    virtual ~PrinterInterface() = default;
    virtual void initialize() = 0;
    virtual bool sendCommand(const std::string& command) = 0;
    virtual void shutdown() = 0;
};

}