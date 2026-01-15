#pragma once

#include <filesystem>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <string_view>
#include <memory>
#include <stdexcept>
#include <fstream>
#include <utility>

#include <switch.h>
#include <curl/curl.h>

#include "libs/stb_image.h"
#include "libs/stb_image_write.h"
#include "libs/stb_image_resize2.h"

namespace UTIL
{
    // Constants
    inline constexpr std::string_view EMUIIBO_PATH = "sdmc:/emuiibo/";
    inline constexpr std::string_view AMIIBO_DB_PATH = "sdmc:/emuiibo/amiibos.json";
    inline constexpr std::string_view AMIIBO_API_URL = "https://www.amiiboapi.org/api/amiibo/";
    inline constexpr int TARGET_IMAGE_HEIGHT = 150;
    inline constexpr long CURL_TIMEOUT_SECONDS = 120L;

    // Helper function to print error and update console
    inline void printError(const char *format, ...)
    {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        std::fputs(buffer, stderr);
        consoleUpdate(nullptr);
    }

    // Helper function to print message and update console
    inline void printMessage(const char *format, ...)
    {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        std::fputs(buffer, stdout);
        consoleUpdate(nullptr);
    }

    // Callback for curl file writing
    inline size_t writeCallback(void *ptr, size_t size, size_t nmemb, void *userdata) noexcept
    {
        auto *stream = static_cast<std::ofstream *>(userdata);
        if (!stream || !stream->is_open())
            return 0;
        const size_t bytes = size * nmemb;
        stream->write(static_cast<const char *>(ptr), static_cast<std::streamsize>(bytes));
        return bytes;
    }

    // RAII wrapper for CURL handles
    class CurlHandle
    {
        CURL *handle_;

    public:
        CurlHandle() noexcept : handle_(curl_easy_init()) {}
        ~CurlHandle()
        {
            if (handle_)
                curl_easy_cleanup(handle_);
        }

        CurlHandle(const CurlHandle &) = delete;
        CurlHandle &operator=(const CurlHandle &) = delete;
        CurlHandle(CurlHandle &&o) noexcept : handle_(std::exchange(o.handle_, nullptr)) {}
        CurlHandle &operator=(CurlHandle &&o) noexcept
        {
            if (this != &o)
            {
                if (handle_)
                    curl_easy_cleanup(handle_);
                handle_ = std::exchange(o.handle_, nullptr);
            }
            return *this;
        }

        [[nodiscard]] CURL *get() const noexcept { return handle_; }
        [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }
    };

    // Download file with proper error handling
    [[nodiscard]] inline int downloadFile(std::string_view url, std::string_view path)
    {
        if (url.empty() || path.empty())
        {
            printError("Error: empty URL or path provided to downloadFile\n");
            return -1;
        }

        CurlHandle curl;
        if (!curl)
        {
            printError("Error: Failed to initialize CURL\n");
            return -1;
        }

        std::ofstream ofs(std::string(path), std::ios::binary);
        if (!ofs)
        {
            printError("Error: Failed to open file for writing: %.*s\n", static_cast<int>(path.size()), path.data());
            return -1;
        }

        // Configure CURL options
        curl_easy_setopt(curl.get(), CURLOPT_URL, std::string(url).c_str());
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &ofs);
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, CURL_TIMEOUT_SECONDS);
        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "AmiiboGenerator/2.2");

        const CURLcode res = curl_easy_perform(curl.get());
        ofs.close();

        const std::string pathStr(path);
        if (res != CURLE_OK)
        {
            printError("CURL error: %s\n", curl_easy_strerror(res));
            std::filesystem::remove(pathStr);
            return static_cast<int>(res);
        }

        long http_code = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code != 200)
        {
            printError("HTTP error: %ld\n", http_code);
            std::filesystem::remove(pathStr);
            return -1;
        }

        double download_size = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_SIZE_DOWNLOAD, &download_size);
        printMessage("Downloaded: %.0f bytes\n", download_size);

        if (download_size < 100)
        {
            printError("Downloaded file too small: %.0f bytes\n", download_size);
            std::filesystem::remove(pathStr);
            return -1;
        }

        return 0;
    }

    [[nodiscard]] inline bool downloadAmiiboDatabase()
    {
        printMessage("Starting database download from API...\n");

        const std::string dbPath(AMIIBO_DB_PATH);
        std::error_code ec;
        if (std::filesystem::exists(dbPath, ec))
        {
            printMessage("Removing old database file...\n");
            std::filesystem::remove(dbPath, ec);
            if (ec)
                printError("Warning: Failed to remove old database: %s\n", ec.message().c_str());
        }

        printMessage("Connecting to AmiiboAPI...\n");
        printMessage("URL: %.*s\n", static_cast<int>(AMIIBO_API_URL.size()), AMIIBO_API_URL.data());
        printMessage("This may take 30-60 seconds depending on connection...\n");
        printMessage("Please wait...\n");

        if (downloadFile(AMIIBO_API_URL, AMIIBO_DB_PATH) == 0)
        {
            if (const auto size = std::filesystem::file_size(dbPath, ec); !ec && size > 100)
            {
                printMessage("Download completed successfully (%zu bytes)\n", static_cast<size_t>(size));
                return true;
            }
            printError("Download reported success but file invalid!\n");
        }
        else
        {
            printError("Download failed. Check your internet connection.\n");
        }
        return false;
    }

    [[nodiscard]] inline bool checkAmiiboDatabase()
    {
        std::error_code ec;
        const std::string emuPath(EMUIIBO_PATH);
        const std::string dbPath(AMIIBO_DB_PATH);

        // Ensure directory exists
        if (!std::filesystem::exists(emuPath, ec))
        {
            std::filesystem::create_directories(emuPath, ec);
            if (ec)
            {
                printError("Error: Failed to create emuiibo directory: %s\n", ec.message().c_str());
                return false;
            }
        }

        // Check if database exists
        if (std::filesystem::exists(dbPath, ec))
        {
            const auto file_size = std::filesystem::file_size(dbPath, ec);
            printMessage("Database found (%zu bytes)\n", ec ? 0u : static_cast<size_t>(file_size));
            return true;
        }

        printMessage("\nNo database found. Downloading...\n");
        return downloadAmiiboDatabase();
    }

    [[nodiscard]] constexpr bool isBlacklistedCharacter(char c) noexcept
    {
        const auto uc = static_cast<unsigned char>(c);
        if (uc >= 128)
            return true;
        switch (c)
        {
        case '!':
        case '?':
        case '.':
        case ',':
        case '\'':
        case '\\':
            return true;
        default:
            return false;
        }
    }

    // Random number in range [nMin, nMax]
    [[nodiscard]] inline int RandU(int nMin, int nMax) noexcept
    {
        if (nMin > nMax)
            std::swap(nMin, nMax);
        return nMin + (std::rand() % (nMax - nMin + 1));
    }

    // Endian swap for 16-bit values
    [[nodiscard]] constexpr uint16_t swap_uint16(uint16_t val) noexcept
    {
        return static_cast<uint16_t>((val << 8) | (val >> 8));
    }

    // RAII wrapper for stbi allocated memory
    class ImageData
    {
        unsigned char *data_ = nullptr;
        int width_ = 0, height_ = 0, channels_ = 0;

    public:
        explicit ImageData(std::string_view path)
        {
            if (path.empty())
                throw std::invalid_argument("Image path cannot be empty");
            data_ = stbi_load(std::string(path).c_str(), &width_, &height_, &channels_, 0);
            if (!data_)
                throw std::runtime_error(std::string("Failed to load image: ") + std::string(path));
        }
        ~ImageData()
        {
            if (data_)
                stbi_image_free(data_);
        }

        ImageData(const ImageData &) = delete;
        ImageData &operator=(const ImageData &) = delete;
        ImageData(ImageData &&o) noexcept
            : data_(std::exchange(o.data_, nullptr)), width_(o.width_), height_(o.height_), channels_(o.channels_) {}
        ImageData &operator=(ImageData &&o) noexcept
        {
            if (this != &o)
            {
                if (data_)
                    stbi_image_free(data_);
                data_ = std::exchange(o.data_, nullptr);
                width_ = o.width_;
                height_ = o.height_;
                channels_ = o.channels_;
            }
            return *this;
        }

        [[nodiscard]] unsigned char *get() const noexcept { return data_; }
        [[nodiscard]] int width() const noexcept { return width_; }
        [[nodiscard]] int height() const noexcept { return height_; }
        [[nodiscard]] int channels() const noexcept { return channels_; }
    };

    [[nodiscard]] inline bool loadAndResizeImageInRatio(std::string_view imagePath)
    {
        if (imagePath.empty())
        {
            printError("Error: empty image path\n");
            return false;
        }

        try
        {
            ImageData img(imagePath);
            const int newWidth = (TARGET_IMAGE_HEIGHT * img.width()) / img.height();
            if (newWidth <= 0)
            {
                printError("Error: Invalid image dimensions for resizing\n");
                return false;
            }

            const int pixelCount = newWidth * TARGET_IMAGE_HEIGHT;
            auto resized = std::make_unique<unsigned char[]>(pixelCount * img.channels());

            stbir_resize_uint8_linear(
                img.get(), img.width(), img.height(), 0,
                resized.get(), newWidth, TARGET_IMAGE_HEIGHT, 0,
                static_cast<stbir_pixel_layout>(img.channels()));

            // Convert RGB to RGBA if needed
            std::unique_ptr<unsigned char[]> finalData;
            int finalChannels = img.channels();

            if (img.channels() == 3)
            {
                finalData = std::make_unique<unsigned char[]>(pixelCount * 4);
                for (int i = 0; i < pixelCount; ++i)
                {
                    finalData[i * 4 + 0] = resized[i * 3 + 0];
                    finalData[i * 4 + 1] = resized[i * 3 + 1];
                    finalData[i * 4 + 2] = resized[i * 3 + 2];
                    finalData[i * 4 + 3] = 255;
                }
                finalChannels = 4;
            }
            else
            {
                finalData = std::move(resized);
            }

            const std::string pathStr(imagePath);
            return stbi_write_png(pathStr.c_str(), newWidth, TARGET_IMAGE_HEIGHT,
                                  finalChannels, finalData.get(), newWidth * finalChannels) != 0;
        }
        catch (const std::exception &e)
        {
            printError("Error loading/resizing image: %s\n", e.what());
            return false;
        }
    }
} // namespace UTIL
