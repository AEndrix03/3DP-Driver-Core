#pragma once

#include <string>

namespace connector::events {

    /**
     * @brief Base interface for Kafka message senders
     */
    class BaseSender {
    public:
        virtual ~BaseSender() = default;

        /**
         * @brief Send message to Kafka topic
         * @param message JSON message to send
         * @param key Kafka message key (usually driverId)
         * @return true if message was sent successfully
         */
        virtual bool sendMessage(const std::string &message, const std::string &key = "") = 0;

        /**
         * @brief Check if sender is ready to send messages
         */
        virtual bool isReady() const = 0;

        /**
         * @brief Get the topic name this sender publishes to
         */
        virtual std::string getTopicName() const = 0;

        /**
         * @brief Get sender type name
         */
        virtual std::string getSenderName() const = 0;
    };

}