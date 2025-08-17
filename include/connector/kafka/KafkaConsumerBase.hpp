#pragma once

#include "../events/BaseReceiver.hpp"
#include "KafkaConfig.hpp"
#include <librdkafka/rdkafka.h>
#include <functional>
#include <thread>
#include <atomic>
#include <string>

namespace connector::kafka {

    /**
     * @brief Base Kafka consumer implementation
     */
    class KafkaConsumerBase : public events::BaseReceiver {
    public:
        using MessageCallback = std::function<void(const std::string &message, const std::string &key)>;

        explicit KafkaConsumerBase(const KafkaConfig &config, const std::string &topicName);

        virtual ~KafkaConsumerBase();

        // BaseReceiver implementation
        void startReceiving() override;

        void stopReceiving() override;

        bool isReceiving() const override;

        std::string getTopicName() const override;

        // Callback setup
        void setMessageCallback(MessageCallback callback) { messageCallback_ = std::move(callback); }

    protected:
        virtual std::string getReceiverName() const override = 0;

    private:
        KafkaConfig config_;
        std::string topicName_;
        rd_kafka_t *consumer_;
        std::thread consumerThread_;
        std::atomic<bool> running_;
        std::atomic<bool> receiving_;
        MessageCallback messageCallback_;

        void createConsumer();

        void destroyConsumer();

        void consumerLoop();

        static void errorCallback(rd_kafka_t *rk, int err, const char *reason, void *opaque);
    };

}