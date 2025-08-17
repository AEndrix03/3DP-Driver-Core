#include "connector/kafka/KafkaConsumerBase.hpp"
#include "logger/Logger.hpp"
#include <stdexcept>
#include <sstream>

namespace connector::kafka {

    KafkaConsumerBase::KafkaConsumerBase(const KafkaConfig &config, const std::string &topicName)
            : config_(config), topicName_(topicName), consumer_(nullptr), running_(false), receiving_(false) {

        Logger::logInfo("[KafkaConsumerBase] Initializing consumer for topic: " + topicName);

        // Non creare il consumer qui nel costruttore - rimandalo a startReceiving()
        // Questo evita crash durante la costruzione
    }

    KafkaConsumerBase::~KafkaConsumerBase() {
        try {
            stopReceiving();
            destroyConsumer();
        } catch (...) {
            // Ignora errori nel distruttore
        }
    }

    void KafkaConsumerBase::startReceiving() {
        if (receiving_) {
            Logger::logWarning("[" + getReceiverName() + "] Already receiving");
            return;
        }

        try {
            Logger::logInfo("[" + getReceiverName() + "] Creating Kafka consumer...");
            createConsumer();
            Logger::logInfo("[" + getReceiverName() + "] Kafka consumer created successfully");

            running_ = true;
            receiving_ = true;

            Logger::logInfo("[" + getReceiverName() + "] Starting consumer thread...");
            consumerThread_ = std::thread([this]() {
                try {
                    consumerLoop();
                } catch (const std::exception &e) {
                    Logger::logError("[" + getReceiverName() + "] Consumer thread crashed: " + std::string(e.what()));
                } catch (...) {
                    Logger::logError("[" + getReceiverName() + "] Consumer thread crashed with unknown exception");
                }
            });

            Logger::logInfo("[" + getReceiverName() + "] Started receiving from topic: " + topicName_);

        } catch (const std::exception &e) {
            receiving_ = false;
            running_ = false;
            Logger::logError("[" + getReceiverName() + "] Failed to start: " + std::string(e.what()));
            destroyConsumer();
            throw;
        }
    }

    void KafkaConsumerBase::stopReceiving() {
        if (!receiving_) {
            return;
        }

        Logger::logInfo("[" + getReceiverName() + "] Stopping consumer...");
        running_ = false;

        if (consumerThread_.joinable()) {
            try {
                consumerThread_.join();
            } catch (const std::exception &e) {
                Logger::logError("[" + getReceiverName() + "] Error joining consumer thread: " + std::string(e.what()));
            }
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
        errstr[0] = '\0';

        Logger::logInfo("[" + getReceiverName() + "] Creating librdkafka configuration...");
        rd_kafka_conf_t *conf = rd_kafka_conf_new();
        if (!conf) {
            throw std::runtime_error("Failed to create Kafka configuration object");
        }

        try {
            // Basic configuration con controllo errori
            Logger::logInfo("[" + getReceiverName() + "] Setting brokers: " + config_.brokers);
            if (rd_kafka_conf_set(conf, "bootstrap.servers", config_.brokers.c_str(), errstr, sizeof(errstr)) !=
                RD_KAFKA_CONF_OK) {
                throw std::runtime_error("Failed to set bootstrap.servers: " + std::string(errstr));
            }

            Logger::logInfo("[" + getReceiverName() + "] Setting group.id: " + config_.consumerGroupId);
            if (rd_kafka_conf_set(conf, "group.id", config_.consumerGroupId.c_str(), errstr, sizeof(errstr)) !=
                RD_KAFKA_CONF_OK) {
                throw std::runtime_error("Failed to set group.id: " + std::string(errstr));
            }

            Logger::logInfo("[" + getReceiverName() + "] Setting client.id: " + config_.clientId);
            if (rd_kafka_conf_set(conf, "client.id", config_.clientId.c_str(), errstr, sizeof(errstr)) !=
                RD_KAFKA_CONF_OK) {
                throw std::runtime_error("Failed to set client.id: " + std::string(errstr));
            }

            // Consumer specific settings
            Logger::logInfo("[" + getReceiverName() + "] Setting consumer-specific configuration...");
            rd_kafka_conf_set(conf, "session.timeout.ms", std::to_string(config_.sessionTimeoutMs).c_str(), errstr,
                              sizeof(errstr));
            rd_kafka_conf_set(conf, "enable.auto.commit", config_.autoCommit ? "true" : "false", errstr,
                              sizeof(errstr));
            rd_kafka_conf_set(conf, "auto.commit.interval.ms", std::to_string(config_.autoCommitIntervalMs).c_str(),
                              errstr, sizeof(errstr));
            rd_kafka_conf_set(conf, "auto.offset.reset", config_.autoOffsetReset.c_str(), errstr, sizeof(errstr));

            // Impostazioni di timeout più aggressive per evitare hang
            rd_kafka_conf_set(conf, "socket.timeout.ms", "10000", errstr, sizeof(errstr));
            rd_kafka_conf_set(conf, "socket.keepalive.enable", "true", errstr, sizeof(errstr));

            // Set error callback
            rd_kafka_conf_set_error_cb(conf, errorCallback);

            Logger::logInfo("[" + getReceiverName() + "] Creating Kafka consumer instance...");
            consumer_ = rd_kafka_new(RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
            if (!consumer_) {
                rd_kafka_conf_destroy(conf);
                throw std::runtime_error("Failed to create Kafka consumer: " + std::string(errstr));
            }

            Logger::logInfo("[" + getReceiverName() + "] Subscribing to topic: " + topicName_);
            rd_kafka_topic_partition_list_t *subscription = rd_kafka_topic_partition_list_new(1);
            rd_kafka_topic_partition_list_add(subscription, topicName_.c_str(), RD_KAFKA_PARTITION_UA);

            rd_kafka_resp_err_t err = rd_kafka_subscribe(consumer_, subscription);
            rd_kafka_topic_partition_list_destroy(subscription);

            if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
                rd_kafka_destroy(consumer_);
                consumer_ = nullptr;
                throw std::runtime_error("Failed to subscribe to topic: " + std::string(rd_kafka_err2str(err)));
            }

            Logger::logInfo("[" + getReceiverName() + "] Consumer created and subscribed successfully");

        } catch (const std::exception &e) {
            if (conf) rd_kafka_conf_destroy(conf);
            throw;
        }
    }

    void KafkaConsumerBase::destroyConsumer() {
        if (consumer_) {
            try {
                Logger::logInfo("[" + getReceiverName() + "] Closing consumer...");
                rd_kafka_consumer_close(consumer_);
                rd_kafka_destroy(consumer_);
                consumer_ = nullptr;
                Logger::logInfo("[" + getReceiverName() + "] Consumer destroyed");
            } catch (...) {
                Logger::logError("[" + getReceiverName() + "] Error destroying consumer");
                consumer_ = nullptr;
            }
        }
    }

    void KafkaConsumerBase::consumerLoop() {
        Logger::logInfo("[" + getReceiverName() + "] Consumer loop started");

        while (running_ && consumer_) {
            try {
                rd_kafka_message_t *msg = rd_kafka_consumer_poll(consumer_, config_.pollTimeoutMs);

                if (!msg) continue;

                if (msg->err != RD_KAFKA_RESP_ERR_NO_ERROR) {
                    if (msg->err != RD_KAFKA_RESP_ERR__PARTITION_EOF) {
                        Logger::logError("[" + getReceiverName() + "] Consumer error: " +
                                         std::string(rd_kafka_message_errstr(msg)));
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
                        Logger::logError(
                                "[" + getReceiverName() + "] Message processing error: " + std::string(e.what()));
                    }
                }

                rd_kafka_message_destroy(msg);

            } catch (const std::exception &e) {
                Logger::logError("[" + getReceiverName() + "] Error in consumer loop: " + std::string(e.what()));
                std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Wait before retry
            }
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