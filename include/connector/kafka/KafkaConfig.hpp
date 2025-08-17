#pragma once

#include <string>
#include <vector>

namespace connector::kafka {

    struct KafkaConfig {
        // Connection
        std::string brokers = "localhost:9092";
        std::string clientId = "3dp_driver_001";

        // Consumer settings
        std::string consumerGroupId = "3dp_driver_group";
        int sessionTimeoutMs = 30000;
        int pollTimeoutMs = 1000;
        bool autoCommit = true;
        int autoCommitIntervalMs = 5000;
        std::string autoOffsetReset = "latest";

        // Producer settings
        int deliveryTimeoutMs = 30000;
        int requestTimeoutMs = 5000;
        std::string compressionType = "snappy";
        int batchSize = 16384;
        int lingerMs = 5;

        // Security (optional)
        bool enableSsl = false;
        std::string sslCaLocation = "";
        std::string sslCertLocation = "";
        std::string sslKeyLocation = "";
        std::string saslMechanism = "";
        std::string saslUsername = "";
        std::string saslPassword = "";

        // Driver info
        std::string driverId = "3dp_driver_001";
        std::string location = "lab_001";

        static KafkaConfig fromEnvironment();
    };

}