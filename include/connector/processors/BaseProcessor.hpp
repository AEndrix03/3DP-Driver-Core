#pragma once

#include <string>

namespace connector::processors {

    /**
     * @brief Base interface for business logic processors
     */
    class BaseProcessor {
    public:
        virtual ~BaseProcessor() = default;

        /**
         * @brief Get processor type name
         */
        virtual std::string getProcessorName() const = 0;

        /**
         * @brief Check if processor is ready for operation
         */
        virtual bool isReady() const = 0;
    };

} // namespace connector::processors