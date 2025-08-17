#pragma once

#include "../events/BaseSender.hpp"
#include "KafkaConfig.hpp"
#include <librdkafka/rdkafka.h>
#include <string>

namespace connector::kafka {

    /**
     * @brief Base Kafka producer implementation
     */
    class KafkaProducerBase : public events::BaseSender {
    public:
        explicit KafkaProducerBase(const KafkaConfig &config, const std::string &topicName);

        virtual ~KafkaProducerBase();

        // BaseSender implementation
        bool sendMessage(const std::string &message, const std::string &key = "") override;

        bool isReady() const override;

        std::string getTopicName() const override;

    protected:
        virtual std::string getSenderName() const override = 0;

    private:
        KafkaConfig config_;
        std::string topicName_;
        rd_kafka_t *producer_;
        bool ready_;

        void createProducer();

        void destroyProducer();

        static void deliveryReportCallback(rd_kafka_t *rk, const rd_kafka_message_t *rkmessage, void *opaque);

        static void errorCallback(rd_kafka_t *rk, int err, const char *reason, void *opaque);
    };

}