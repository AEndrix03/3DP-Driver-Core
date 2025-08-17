#include "connector/kafka/KafkaConfig.hpp"
#include <cstdlib>
#include <string>

namespace connector::kafka {

    KafkaConfig KafkaConfig::fromEnvironment() {
        KafkaConfig config;

        // Connection
        if (const char *brokers = std::getenv("KAFKA_BROKERS")) {
            config.brokers = brokers;
        }
        if (const char *clientId = std::getenv("KAFKA_CLIENT_ID")) {
            config.clientId = clientId;
        }

        // Consumer
        if (const char *groupId = std::getenv("KAFKA_CONSUMER_GROUP")) {
            config.consumerGroupId = groupId;
        }
        if (const char *timeout = std::getenv("KAFKA_SESSION_TIMEOUT_MS")) {
            config.sessionTimeoutMs = std::stoi(timeout);
        }
        if (const char *pollTimeout = std::getenv("KAFKA_POLL_TIMEOUT_MS")) {
            config.pollTimeoutMs = std::stoi(pollTimeout);
        }
        if (const char *autoCommit = std::getenv("KAFKA_AUTO_COMMIT")) {
            config.autoCommit = std::string(autoCommit) == "true";
        }

        // Producer
        if (const char *deliveryTimeout = std::getenv("KAFKA_DELIVERY_TIMEOUT_MS")) {
            config.deliveryTimeoutMs = std::stoi(deliveryTimeout);
        }
        if (const char *compression = std::getenv("KAFKA_COMPRESSION")) {
            config.compressionType = compression;
        }

        // Security
        if (const char *enableSsl = std::getenv("KAFKA_ENABLE_SSL")) {
            config.enableSsl = std::string(enableSsl) == "true";
        }
        if (const char *caLocation = std::getenv("KAFKA_SSL_CA_LOCATION")) {
            config.sslCaLocation = caLocation;
        }
        if (const char *certLocation = std::getenv("KAFKA_SSL_CERT_LOCATION")) {
            config.sslCertLocation = certLocation;
        }
        if (const char *keyLocation = std::getenv("KAFKA_SSL_KEY_LOCATION")) {
            config.sslKeyLocation = keyLocation;
        }
        if (const char *saslMech = std::getenv("KAFKA_SASL_MECHANISM")) {
            config.saslMechanism = saslMech;
        }
        if (const char *saslUser = std::getenv("KAFKA_SASL_USERNAME")) {
            config.saslUsername = saslUser;
        }
        if (const char *saslPass = std::getenv("KAFKA_SASL_PASSWORD")) {
            config.saslPassword = saslPass;
        }

        // Driver info
        if (const char *driverId = std::getenv("DRIVER_ID")) {
            config.driverId = driverId;
        }
        if (const char *location = std::getenv("DRIVER_LOCATION")) {
            config.location = location;
        }

        return config;
    }

}