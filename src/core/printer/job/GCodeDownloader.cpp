#include "core/printer/job/GCodeDownloader.hpp"
#include "logger/Logger.hpp"
#include <curl/curl.h>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>

namespace core::print {
    GCodeDownloader::GCodeDownloader() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    GCodeDownloader::~GCodeDownloader() {
        cancelDownload();
        if (downloadThread_.joinable()) {
            downloadThread_.join();
        }
        curl_global_cleanup();
    }

    void GCodeDownloader::downloadAsync(const std::string &url,
                                        const std::string &jobId,
                                        ProgressCallback progressCb,
                                        CompletionCallback completionCb) {
        if (downloading_) {
            if (completionCb) {
                completionCb(false, "", "Download already in progress");
            }
            return;
        }

        progressCallback_ = progressCb;
        completionCallback_ = completionCb;
        cancelRequested_ = false;
        {
            std::lock_guard<std::mutex> lock(progressMutex_);
            currentProgress_ = {};
            currentProgress_.url = url;
        }

        downloading_ = true;
        downloadThread_ = std::thread([this, url, jobId]() {
            downloadWorkerWithRetry(url, jobId);
        });

        Logger::logInfo("[GCodeDownloader] Started download: " + url);
    }

    void GCodeDownloader::downloadWorkerWithRetry(const std::string &url, const std::string &jobId) {
        int attemptNumber = 0;
        const std::chrono::seconds RETRY_DELAY(10);

        while (!cancelRequested_) {
            attemptNumber++;
            Logger::logInfo("[GCodeDownloader] Download attempt #" + std::to_string(attemptNumber) +
                            " for URL: " + url);

            bool success = performSingleDownload(url, jobId);

            if (success) {
                // Download completato con successo
                downloading_ = false;
                return;
            }

            if (cancelRequested_) {
                // Download cancellato dall'utente
                downloading_ = false;
                if (completionCallback_) {
                    completionCallback_(false, "", "Download cancelled by user");
                }
                return;
            }

            // Download fallito, aspetta prima di riprovare
            Logger::logWarning("[GCodeDownloader] Download failed on attempt #" +
                               std::to_string(attemptNumber) +
                               ". Retrying in " + std::to_string(RETRY_DELAY.count()) + " seconds...");

            // Notifica il callback del progress che stiamo aspettando per il retry
            {
                std::lock_guard<std::mutex> lock(progressMutex_);
                currentProgress_.status = "Waiting for retry (attempt #" +
                                          std::to_string(attemptNumber + 1) + " in " +
                                          std::to_string(RETRY_DELAY.count()) + " seconds)";
            }

            // Attendi con possibilità di interruzione
            for (int i = 0; i < RETRY_DELAY.count() && !cancelRequested_; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        // Se arriviamo qui, il download è stato cancellato
        downloading_ = false;
        if (completionCallback_) {
            completionCallback_(false, "", "Download cancelled");
        }
    }

    bool GCodeDownloader::performSingleDownload(const std::string &url, const std::string &jobId) {
        std::string tempFilePath = generateTempFilePath(jobId);
        std::ofstream outFile(tempFilePath, std::ios::binary);

        if (!outFile.is_open()) {
            Logger::logError("[GCodeDownloader] Cannot create temp file: " + tempFilePath);
            return false;
        }

        CURL *curl = curl_easy_init();
        if (!curl) {
            Logger::logError("[GCodeDownloader] Failed to initialize CURL");
            outFile.close();
            std::filesystem::remove(tempFilePath);
            return false;
        }

        // Configurazione CURL
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
        curl_easy_setopt(curl, CURLOPT_HTTP_TRANSFER_DECODING, 1L);

        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outFile);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

        // Timeouts
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "3DP-Driver/1.0");

        // Reset progress status
        {
            std::lock_guard<std::mutex> lock(progressMutex_);
            currentProgress_.status = "Downloading...";
        }

        CURLcode res = curl_easy_perform(curl);
        long responseCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

        curl_easy_cleanup(curl);
        outFile.close();

        bool success = false;
        std::string error;

        if (cancelRequested_) {
            std::filesystem::remove(tempFilePath);
            return false; // Verrà gestito dal chiamante
        } else if (res != CURLE_OK) {
            error = "Download failed: " + std::string(curl_easy_strerror(res));
            Logger::logError("[GCodeDownloader] " + error);
            std::filesystem::remove(tempFilePath);
        } else if (responseCode != 200) {
            error = "HTTP error: " + std::to_string(responseCode);
            Logger::logError("[GCodeDownloader] " + error);
            std::filesystem::remove(tempFilePath);
        } else {
            if (std::filesystem::exists(tempFilePath) && std::filesystem::file_size(tempFilePath) > 0) {
                success = true;
                Logger::logInfo("[GCodeDownloader] Download completed successfully: " + tempFilePath +
                                " (" + std::to_string(std::filesystem::file_size(tempFilePath)) + " bytes)");

                if (completionCallback_) {
                    completionCallback_(true, tempFilePath, "");
                }
            } else {
                error = "Downloaded file is empty or missing";
                Logger::logError("[GCodeDownloader] " + error);
                std::filesystem::remove(tempFilePath);
            }
        }

        return success;
    }

    size_t GCodeDownloader::writeCallback(void *contents, size_t size, size_t nmemb, void *userp) {
        std::ofstream *file = static_cast<std::ofstream *>(userp);
        size_t totalSize = size * nmemb;
        file->write(static_cast<char *>(contents), totalSize);
        return totalSize;
    }

    int GCodeDownloader::progressCallback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                                          curl_off_t ultotal, curl_off_t ulnow) {
        (void) ultotal;
        (void) ulnow;

        GCodeDownloader *downloader = static_cast<GCodeDownloader *>(clientp);

        if (downloader->cancelRequested_) {
            return 1; // Abort download
        }

        if (dltotal > 0) {
            std::lock_guard<std::mutex> lock(downloader->progressMutex_);
            downloader->currentProgress_.totalBytes = dltotal;
            downloader->currentProgress_.downloadedBytes = dlnow;
            downloader->currentProgress_.percentage = (double(dlnow) / dltotal) * 100.0;

            if (downloader->progressCallback_) {
                downloader->progressCallback_(downloader->currentProgress_);
            }
        }

        return 0; // Continue download
    }

    std::string GCodeDownloader::generateTempFilePath(const std::string &jobId) const {
        std::filesystem::create_directories("temp/gcode");

        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);

        std::ostringstream oss;
        oss << "temp/gcode/" << jobId << "_" << time_t << ".gcode";
        return oss.str();
    }

    void GCodeDownloader::cancelDownload() {
        if (downloading_) {
            Logger::logInfo("[GCodeDownloader] Cancelling download...");
            cancelRequested_ = true;

            if (downloadThread_.joinable()) {
                downloadThread_.join();
            }
        }
    }

    DownloadProgress GCodeDownloader::getCurrentProgress() const {
        std::lock_guard<std::mutex> lock(progressMutex_);
        return currentProgress_;
    }
} // namespace core::print