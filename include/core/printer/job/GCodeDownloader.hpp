#pragma once

#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <filesystem>
#include <chrono>

// Forward declare CURL per evitare dipendenza header
typedef void CURL;

namespace core::print {
    struct DownloadProgress {
        std::string url;
        size_t totalBytes = 0;
        size_t downloadedBytes = 0;
        double percentage = 0.0;
        std::string status = "Initializing...";
    };

    class GCodeDownloader {
    public:
        using ProgressCallback = std::function<void(const DownloadProgress &)>;
        using CompletionCallback = std::function<void(bool success, const std::string &filePath,
                                                      const std::string &error)>;

        GCodeDownloader();

        ~GCodeDownloader();

        void downloadAsync(const std::string &url, const std::string &jobId,
                           ProgressCallback progressCb = nullptr,
                           CompletionCallback completionCb = nullptr);

        void cancelDownload();

        bool isDownloading() const { return downloading_; }

        DownloadProgress getCurrentProgress() const;

        void downloadWorkerWithRetry(const std::string &url, const std::string &jobId);

        bool performSingleDownload(const std::string &url, const std::string &jobId);

    private:
        std::atomic<bool> downloading_{false};
        std::atomic<bool> cancelRequested_{false};
        std::thread downloadThread_;
        mutable std::mutex progressMutex_;
        DownloadProgress currentProgress_;

        ProgressCallback progressCallback_;
        CompletionCallback completionCallback_;

        static size_t writeCallback(void *contents, size_t size, size_t nmemb, void *userp);

        static int progressCallback(void *clientp, long long dltotal, long long dlnow,
                                    long long ultotal, long long ulnow);

        void downloadWorker(const std::string &url, const std::string &jobId);

        std::string generateTempFilePath(const std::string &jobId) const;
    };
} // namespace core::print
