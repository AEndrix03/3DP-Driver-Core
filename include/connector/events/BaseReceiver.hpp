#pragma once

#include <string>
#include <functional>

namespace connector::events {

    /**
     * @brief Base interface for Kafka message receivers
     */
    class BaseReceiver {
    public:
        virtual ~BaseReceiver() = default;

        /**
         * @brief Start receiving messages from Kafka topic
         */
        virtual void startReceiving() = 0;

        /**
         * @brief Stop receiving messages
         */
        virtual void stopReceiving() = 0;

        /**
         * @brief Check if receiver is actively listening
         */
        virtual bool isReceiving() const = 0;

        /**
         * @brief Get the topic name this receiver listens to
         */
        virtual std::string getTopicName() const = 0;

        /**
         * @brief Get receiver type name
         */
        virtual std::string getReceiverName() const = 0;
    };

} 