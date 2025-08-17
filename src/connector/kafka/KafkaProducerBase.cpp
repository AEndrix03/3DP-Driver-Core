#include "connector/kafka/KafkaProducerBase.hpp"
#include "logger/Logger.hpp"
#include <stdexcept>

namespace connector::kafka {

    KafkaProducerBase::KafkaProducerBase(const KafkaConfig &config, const std::string &topicName)
            : config_(config), topicName_(topicName), producer_(nullptr), ready_(false) {

        Logger::logInfo("[KafkaProducerBase] Initializing producer for topic: " + topicName);

        try {
            createProducer();
        } catch (const std::exception &e) {
            Logger::logError("[KafkaProducerBase] Failed to initialize producer: " + std::string(e.what()));
            // Non rethrow - lascia il producer in stato non-ready ma non crasha
            ready_ = false;
        }
    }

    KafkaProducerBase::~KafkaProducerBase() {
        try {
            destroyProducer();
        } catch (...) {
            // Ignora errori nel distruttore
        }
    }

    bool KafkaProducerBase::sendMessage(const std::string &message, const std::string &key) {
        if (!ready_ || !producer_) {
            Logger::logError("[" + getSenderName() + "] Producer not ready");
            return false;
        }

        try {
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

        } catch (const std::exception &e) {
            Logger::logError("[" + getSenderName() + "] Exception sending message: " + std::string(e.what()));
            return false;
        }
    }

    bool KafkaProducerBase::isReady() const {
        return ready_;
    }

    std::string KafkaProducerBase::getTopicName() const {
        return topicName_;
    }

    void KafkaProducerBase::createProducer() {
        char errstr[512];
        errstr[0] = '\0';

        Logger::logInfo("[KafkaProducerBase] Creating librdkafka producer configuration...");
        rd_kafka_conf_t *conf = rd_kafka_conf_new();
        if (!conf) {
            throw std::runtime_error("Failed to create Kafka producer configuration object");
        }

        try {
            // Basic configuration con controllo errori
            Logger::logInfo("[KafkaProducerBase] Setting brokers: " + config_.brokers);
            if (rd_kafka_conf_set(conf, "bootstrap.servers", config_.brokers.c_str(), errstr, sizeof(errstr)) !=
                RD_KAFKA_CONF_OK) {
                throw std::runtime_error("Failed to set bootstrap.servers: " + std::string(errstr));
            }

            Logger::logInfo("[KafkaProducerBase] Setting client.id: " + config_.clientId);
            if (rd_kafka_conf_set(conf, "client.id", config_.clientId.c_str(), errstr, sizeof(errstr)) !=
                RD_KAFKA_CONF_OK) {
                throw std::runtime_error("Failed to set client.id: " + std::string(errstr));
            }

            // Producer specific settings
            Logger::logInfo("[KafkaProducerBase] Setting producer-specific configuration...");
            rd_kafka_conf_set(conf, "delivery.timeout.ms", std::to_string(config_.deliveryTimeoutMs).c_str(), errstr,
                              sizeof(errstr));
            rd_kafka_conf_set(conf, "request.timeout.ms", std::to_string(config_.requestTimeoutMs).c_str(), errstr,
                              sizeof(errstr));
            rd_kafka_conf_set(conf, "compression.type", config_.compressionType.c_str(), errstr, sizeof(errstr));
            rd_kafka_conf_set(conf, "batch.size", std::to_string(config_.batchSize).c_str(), errstr, sizeof(errstr));
            rd_kafka_conf_set(conf, "linger.ms", std::to_string(config_.lingerMs).c_str(), errstr, sizeof(errstr));

            // Impostazioni di timeout più aggressive
            rd_kafka_conf_set(conf, "socket.timeout.ms", "10000", errstr, sizeof(errstr));
            rd_kafka_conf_set(conf, "socket.keepalive.enable", "true", errstr, sizeof(errstr));

            // Set callbacks
            rd_kafka_conf_set_dr_msg_cb(conf, deliveryReportCallback);
            rd_kafka_conf_set_error_cb(conf, errorCallback);

            Logger::logInfo("[KafkaProducerBase] Creating Kafka producer instance...");
            producer_ = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
            if (!producer_) {
                rd_kafka_conf_destroy(conf);
                throw std::runtime_error("Failed to create Kafka producer: " + std::string(errstr));
            }

            ready_ = true;
            Logger::logInfo("[KafkaProducerBase] Producer created and ready");

        } catch (const std::exception &e) {
            if (conf) rd_kafka_conf_destroy(conf);
            ready_ = false;
            throw;
        }
    }

    void KafkaProducerBase::destroyProducer() {
        if (producer_) {
            try {
                ready_ = false;
                Logger::logInfo("[KafkaProducerBase] Flushing producer...");
                rd_kafka_flush(producer_, 5000); // 5 second timeout
                Logger::logInfo("[KafkaProducerBase] Destroying producer...");
                rd_kafka_destroy(producer_);
                producer_ = nullptr;
                Logger::logInfo("[KafkaProducerBase] Producer destroyed");
            } catch (...) {
                Logger::logError("[KafkaProducerBase] Error destroying producer");
                producer_ = nullptr;
                ready_ = false;
            }
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