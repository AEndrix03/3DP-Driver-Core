#include "connector/kafka/KafkaConsumerBase.hpp"
#include "logger/Logger.hpp"
#include <stdexcept>
#include <sstream>

namespace connector::kafka {

    KafkaConsumerBase::KafkaConsumerBase(const KafkaConfig &config, const std::string &topicName)
            : config_(config), topicName_(topicName), consumer_(nullptr), running_(false), receiving_(false) {
    }

    KafkaConsumerBase::~KafkaConsumerBase() {
        stopReceiving();
        destroyConsumer();
    }

    void KafkaConsumerBase::startReceiving() {
        if (receiving_) {
            Logger::logWarning("[" + getReceiverName() + "] Already receiving");
            return;
        }

        try {
            createConsumer();

            running_ = true;
            receiving_ = true;

            consumerThread_ = std::thread([this]() { consumerLoop(); });

            Logger::logInfo("[" + getReceiverName() + "] Started receiving from topic: " + topicName_);

        } catch (const std::exception &e) {
            receiving_ = false;
            Logger::logError("[" + getReceiverName() + "] Failed to start: " + std::string(e.what()));
            throw;
        }
    }

    void KafkaConsumerBase::stopReceiving() {
        if (!receiving_) {
            return;
        }

        running_ = false;

        if (consumerThread_.joinable()) {
            consumerThread_.join();
        }

        receiving_ = false;
        Logger::logInfo("[" + getReceiverName() + "] Stopped receiving");
    }

    bool KafkaConsumerBase::isReceiving() const {
        return receiving_;
    }

    std::string KafkaConsumerBase::getTopicName() const {
        return topicName_;
    }

    void KafkaConsumerBase::createConsumer() {
        char errstr[512];

        rd_kafka_conf_t *conf = rd_kafka_conf_new();

        // Basic configuration
        rd_kafka_conf_set(conf, "bootstrap.servers", config_.brokers.c_str(), errstr, sizeof(errstr));
        rd_kafka_conf_set(conf, "group.id", config_.consumerGroupId.c_str(), errstr, sizeof(errstr));
        rd_kafka_conf_set(conf, "client.id", config_.clientId.c_str(), errstr, sizeof(errstr));

        // Consumer specific
        rd_kafka_conf_set(conf, "session.timeout.ms", std::to_string(config_.sessionTimeoutMs).c_str(), errstr,
                          sizeof(errstr));
        rd_kafka_conf_set(conf, "enable.auto.commit", config_.autoCommit ? "true" : "false", errstr, sizeof(errstr));
        rd_kafka_conf_set(conf, "auto.commit.interval.ms", std::to_string(config_.autoCommitIntervalMs).c_str(), errstr,
                          sizeof(errstr));
        rd_kafka_conf_set(conf, "auto.offset.reset", config_.autoOffsetReset.c_str(), errstr, sizeof(errstr));

        // Security configuration
        if (config_.enableSsl) {
            rd_kafka_conf_set(conf, "security.protocol", "SSL", errstr, sizeof(errstr));
            if (!config_.sslCaLocation.empty()) {
                rd_kafka_conf_set(conf, "ssl.ca.location", config_.sslCaLocation.c_str(), errstr, sizeof(errstr));
            }
        }

        if (!config_.saslMechanism.empty()) {
            rd_kafka_conf_set(conf, "sasl.mechanism", config_.saslMechanism.c_str(), errstr, sizeof(errstr));
            if (!config_.saslUsername.empty()) {
                rd_kafka_conf_set(conf, "sasl.username", config_.saslUsername.c_str(), errstr, sizeof(errstr));
            }
            if (!config_.saslPassword.empty()) {
                rd_kafka_conf_set(conf, "sasl.password", config_.saslPassword.c_str(), errstr, sizeof(errstr));
            }
        }

        // Set error callback
        rd_kafka_conf_set_error_cb(conf, errorCallback);

        consumer_ = rd_kafka_new(RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
        if (!consumer_) {
            rd_kafka_conf_destroy(conf);
            throw std::runtime_error("Failed to create Kafka consumer: " + std::string(errstr));
        }

        // Subscribe to topic
        rd_kafka_topic_partition_list_t *subscription = rd_kafka_topic_partition_list_new(1);
        rd_kafka_topic_partition_list_add(subscription, topicName_.c_str(), RD_KAFKA_PARTITION_UA);

        rd_kafka_resp_err_t err = rd_kafka_subscribe(consumer_, subscription);
        rd_kafka_topic_partition_list_destroy(subscription);

        if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
            rd_kafka_destroy(consumer_);
            consumer_ = nullptr;
            throw std::runtime_error("Failed to subscribe to topic: " + std::string(rd_kafka_err2str(err)));
        }
    }

    void KafkaConsumerBase::destroyConsumer() {
        if (consumer_) {
            rd_kafka_consumer_close(consumer_);
            rd_kafka_destroy(consumer_);
            consumer_ = nullptr;
        }
    }

    void KafkaConsumerBase::consumerLoop() {
        Logger::logInfo("[" + getReceiverName() + "] Consumer loop started");

        while (running_) {
            rd_kafka_message_t *msg = rd_kafka_consumer_poll(consumer_, config_.pollTimeoutMs);

            if (!msg) continue;

            if (msg->err != RD_KAFKA_RESP_ERR_NO_ERROR) {
                if (msg->err != RD_KAFKA_RESP_ERR__PARTITION_EOF) {
                    Logger::logError(
                            "[" + getReceiverName() + "] Consumer error: " + std::string(rd_kafka_message_errstr(msg)));
                }
                rd_kafka_message_destroy(msg);
                continue;
            }

            // Process message
            std::string message(static_cast<const char *>(msg->payload), msg->len);
            std::string key = msg->key ? std::string(static_cast<const char *>(msg->key), msg->key_len) : "";

            Logger::logInfo("[" + getReceiverName() + "] Received message, key: " + key);

            if (messageCallback_) {
                try {
                    messageCallback_(message, key);
                } catch (const std::exception &e) {
                    Logger::logError("[" + getReceiverName() + "] Message processing error: " + std::string(e.what()));
                }
            }

            rd_kafka_message_destroy(msg);
        }

        Logger::logInfo("[" + getReceiverName() + "] Consumer loop stopped");
    }

    void KafkaConsumerBase::errorCallback(rd_kafka_t *rk, int err, const char *reason, void *opaque) {
        (void) rk;
        (void) opaque;
        Logger::logError(
                "[KafkaConsumer] Error: " + std::string(rd_kafka_err2str(static_cast<rd_kafka_resp_err_t>(err))) +
                " - " + reason);
    }

}