#pragma once

#include <string>

namespace connector::kafka {
    struct KafkaConfig {
        // Connection - with Spring Boot style placeholders
        std::string brokers = "${KAFKA_BROKERS:localhost:9092}";
        std::string clientId = "${KAFKA_CLIENT_ID:3dp_driver_001}";

        // Consumer settings
        std::string consumerGroupId = "${KAFKA_CONSUMER_GROUP:3dp_driver_group}";
        int sessionTimeoutMs = 30000;
        int pollTimeoutMs = 1000;
        bool autoCommit = true;
        int autoCommitIntervalMs = 5000;
        std::string autoOffsetReset = "${KAFKA_AUTO_OFFSET_RESET:earliest}";

        // Producer settings
        int deliveryTimeoutMs = 30000;
        int requestTimeoutMs = 5000;
        std::string compressionType = "${KAFKA_COMPRESSION_TYPE:snappy}";
        int batchSize = 16384;
        int lingerMs = 5;

        // Security (optional)
        bool enableSsl = false;
        std::string sslCaLocation = "${KAFKA_SSL_CA_LOCATION:}";
        std::string sslCertLocation = "${KAFKA_SSL_CERT_LOCATION:}";
        std::string sslKeyLocation = "${KAFKA_SSL_KEY_LOCATION:}";
        std::string saslMechanism = "${KAFKA_SASL_MECHANISM:}";
        std::string saslUsername = "${KAFKA_SASL_USERNAME:}";
        std::string saslPassword = "${KAFKA_SASL_PASSWORD:}";

        // Driver info
        std::string driverId = "${DRIVER_ID:3dp_driver_001}";
        std::string location = "${DRIVER_LOCATION:lab_001}";

        // Serial port configuration
        std::string serialPort = "${SERIAL_PORT:COM4}";
        int serialBaudrate = 115200; // TODO: support int placeholders

        /**
         * @brief Resolve all placeholders with environment variables
         */
        void resolveFromEnvironment();

        /**
         * @brief Print current configuration (for debugging)
         */
        void printConfig() const;

    private:
        /**
         * @brief Resolve a single placeholder string
         * @param value String that may contain ${VAR:default} placeholder
         * @return Resolved string
         */
        static std::string resolvePlaceholder(const std::string &value);

        /**
         * @brief Load environment variables from .env file
         * @param envFilePath Path to .env file (default: ".env")
         */
        static void loadEnvFile(const std::string &envFilePath = ".env");
    };
}
