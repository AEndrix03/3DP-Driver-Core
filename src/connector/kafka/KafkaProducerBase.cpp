#include "connector/kafka/KafkaProducerBase.hpp"
#include "logger/Logger.hpp"
#include <stdexcept>

namespace connector::kafka {

    KafkaProducerBase::KafkaProducerBase(const KafkaConfig &config, const std::string &topicName)
            : config_(config), topicName_(topicName), producer_(nullptr), ready_(false) {
        createProducer();
    }

    KafkaProducerBase::~KafkaProducerBase() {
        destroyProducer();
    }

    bool KafkaProducerBase::sendMessage(const std::string &message, const std::string &key) {
        if (!ready_ || !producer_) {
            Logger::logError("[" + getSenderName() + "] Producer not ready");
            return false;
        }

        const char *keyPtr = key.empty() ? nullptr : key.c_str();
        size_t keyLen = key.empty() ? 0 : key.length();

        int result = rd_kafka_producev(
                producer_,
                RD_KAFKA_V_TOPIC(topicName_.c_str()),
                RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
                RD_KAFKA_V_VALUE((void *) message.c_str(), message.length()),
                RD_KAFKA_V_KEY((void *) keyPtr, keyLen),
                RD_KAFKA_V_OPAQUE(nullptr),
                RD_KAFKA_V_END
        );

        if (result != RD_KAFKA_RESP_ERR_NO_ERROR) {
            Logger::logError("[" + getSenderName() + "] Failed to produce message: " +
                             std::string(rd_kafka_err2str(static_cast<rd_kafka_resp_err_t>(result))));
            return false;
        }

        rd_kafka_poll(producer_, 0);

        Logger::logInfo("[" + getSenderName() + "] Message sent to topic: " + topicName_ + ", key: " + key);
        return true;
    }

    bool KafkaProducerBase::isReady() const {
        return ready_;
    }

    std::string KafkaProducerBase::getTopicName() const {
        return topicName_;
    }

    void KafkaProducerBase::createProducer() {
        char errstr[512];

        rd_kafka_conf_t *conf = rd_kafka_conf_new();

        // Basic configuration
        rd_kafka_conf_set(conf, "bootstrap.servers", config_.brokers.c_str(), errstr, sizeof(errstr));
        rd_kafka_conf_set(conf, "client.id", config_.clientId.c_str(), errstr, sizeof(errstr));

        // Producer specific
        rd_kafka_conf_set(conf, "delivery.timeout.ms", std::to_string(config_.deliveryTimeoutMs).c_str(), errstr,
                          sizeof(errstr));
        rd_kafka_conf_set(conf, "request.timeout.ms", std::to_string(config_.requestTimeoutMs).c_str(), errstr,
                          sizeof(errstr));
        rd_kafka_conf_set(conf, "compression.type", config_.compressionType.c_str(), errstr, sizeof(errstr));
        rd_kafka_conf_set(conf, "batch.size", std::to_string(config_.batchSize).c_str(), errstr, sizeof(errstr));
        rd_kafka_conf_set(conf, "linger.ms", std::to_string(config_.lingerMs).c_str(), errstr, sizeof(errstr));

        // Security configuration
        if (config_.enableSsl) {
            rd_kafka_conf_set(conf, "security.protocol", "SSL", errstr, sizeof(errstr));
        }

        if (!config_.saslMechanism.empty()) {
            rd_kafka_conf_set(conf, "sasl.mechanism", config_.saslMechanism.c_str(), errstr, sizeof(errstr));
        }

        // Set callbacks
        rd_kafka_conf_set_dr_msg_cb(conf, deliveryReportCallback);
        rd_kafka_conf_set_error_cb(conf, errorCallback);

        producer_ = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
        if (!producer_) {
            rd_kafka_conf_destroy(conf);
            throw std::runtime_error("Failed to create Kafka producer: " + std::string(errstr));
        }

        ready_ = true;
        Logger::logInfo("[" + getSenderName() + "] Producer created and ready");
    }

    void KafkaProducerBase::destroyProducer() {
        if (producer_) {
            ready_ = false;
            rd_kafka_flush(producer_, 10000); // 10 second timeout
            rd_kafka_destroy(producer_);
            producer_ = nullptr;
        }
    }

    void KafkaProducerBase::deliveryReportCallback(rd_kafka_t *rk, const rd_kafka_message_t *rkmessage, void *opaque) {
        (void) rk;
        (void) opaque;

        if (rkmessage->err != RD_KAFKA_RESP_ERR_NO_ERROR) {
            Logger::logError("[KafkaProducer] Delivery failed: " + std::string(rd_kafka_err2str(rkmessage->err)));
        } else {
            Logger::logInfo("[KafkaProducer] Message delivered successfully");
        }
    }

    void KafkaProducerBase::errorCallback(rd_kafka_t *rk, int err, const char *reason, void *opaque) {
        (void) rk;
        (void) opaque;
        Logger::logError(
                "[KafkaProducer] Error: " + std::string(rd_kafka_err2str(static_cast<rd_kafka_resp_err_t>(err))) +
                " - " + reason);
    }

}