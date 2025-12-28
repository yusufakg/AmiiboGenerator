#pragma once

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE2_IMPLEMENTATION

#include <filesystem>
#include <cstdio>
#include <string>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <switch.h>
#include <curl/curl.h>

#include "libs/stb_image.h"
#include "libs/stb_image_write.h"
#include "libs/stb_image_resize2.h"

namespace UTIL
{
    // Constants
    constexpr const char *EMUIIBO_PATH = "sdmc:/emuiibo/";
    constexpr const char *AMIIBO_DB_PATH = "sdmc:/emuiibo/amiibos.json";
    constexpr const char *AMIIBO_API_URL = "https://www.amiiboapi.com/api/amiibo/";
    constexpr int TARGET_IMAGE_HEIGHT = 150;
    constexpr long CURL_TIMEOUT_SECONDS = 120L;

    // Helper function to print error and update console
    inline void printError(const char *format, ...)
    {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        fprintf(stderr, "%s", buffer);
        consoleUpdate(NULL);
    }

    // Helper function to print message and update console
    inline void printMessage(const char *format, ...)
    {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        printf("%s", buffer);
        consoleUpdate(NULL);
    }

    // Callback for curl file writing
    static size_t write_data(void *ptr, size_t size, size_t nmemb, std::ofstream *stream)
    {
        if (stream == nullptr || !stream->is_open())
        {
            return 0;
        }
        stream->write(static_cast<char *>(ptr), size * nmemb);
        return size * nmemb;
    }

    // RAII wrapper for CURL handles
    class CurlHandle
    {
    private:
        CURL *handle;

    public:
        CurlHandle() : handle(curl_easy_init()) {}

        ~CurlHandle()
        {
            if (handle)
            {
                curl_easy_cleanup(handle);
            }
        }

        CURL *get() const { return handle; }
        bool is_valid() const { return handle != nullptr; }

        // Disable copy operations
        CurlHandle(const CurlHandle &) = delete;
        CurlHandle &operator=(const CurlHandle &) = delete;

        // Allow move operations
        CurlHandle(CurlHandle &&other) noexcept : handle(other.release()) {}
        CurlHandle &operator=(CurlHandle &&other) noexcept
        {
            if (this != &other)
            {
                if (handle)
                {
                    curl_easy_cleanup(handle);
                }
                handle = other.release();
            }
            return *this;
        }

    private:
        CURL *release()
        {
            CURL *tmp = handle;
            handle = nullptr;
            return tmp;
        }
    };

    // Download file with proper error handling
    inline int downloadFile(const std::string &url, const std::string &path)
    {
        if (url.empty() || path.empty())
        {
            printError("Error: empty URL or path provided to downloadFile\n");
            return -1;
        }

        CurlHandle curl;
        if (!curl.is_valid())
        {
            printError("Error: Failed to initialize CURL\n");
            return -1;
        }

        std::ofstream ofs(path, std::ios::binary);
        if (!ofs.is_open())
        {
            printError("Error: Failed to open file for writing: %s\n", path.c_str());
            return -1;
        }

        // Configure CURL options
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, UTIL::write_data);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &ofs);
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, CURL_TIMEOUT_SECONDS);
        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "AmiiboGenerator/2.0");

        // Perform download
        CURLcode res = curl_easy_perform(curl.get());

        // Close file stream after download
        ofs.close();

        if (res != CURLE_OK)
        {
            printError("CURL error: %s\n", curl_easy_strerror(res));
            std::filesystem::remove(path);
            return static_cast<int>(res);
        }

        // Verify downloaded file size
        long http_code = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);

        if (http_code != 200)
        {
            printError("HTTP error: %ld\n", http_code);
            std::filesystem::remove(path);
            return -1;
        }

        double download_size = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_SIZE_DOWNLOAD, &download_size);
        printMessage("Downloaded: %.0f bytes\n", download_size);

        if (download_size < 100)
        {
            printError("Downloaded file too small: %.0f bytes\n", download_size);
            std::filesystem::remove(path);
            return -1;
        }

        return 0;
    }

    inline bool downloadAmiiboDatabase()
    {
        printMessage("Starting database download from API...\n");

        if (std::filesystem::exists(AMIIBO_DB_PATH))
        {
            printMessage("Removing old database file...\n");
            try
            {
                std::filesystem::remove(AMIIBO_DB_PATH);
            }
            catch (const std::filesystem::filesystem_error &e)
            {
                printError("Warning: Failed to remove old database: %s\n", e.what());
            }
        }

        printMessage("Connecting to AmiiboAPI...\n");
        printMessage("URL: %s\n", AMIIBO_API_URL);
        printMessage("This may take 30-60 seconds depending on connection...\n");
        printMessage("Please wait...\n");

        int ret = UTIL::downloadFile(AMIIBO_API_URL, AMIIBO_DB_PATH);

        if (ret == 0)
        {
            // Double-check the file was actually written
            if (std::filesystem::exists(AMIIBO_DB_PATH))
            {
                auto size = std::filesystem::file_size(AMIIBO_DB_PATH);
                printMessage("Download completed successfully (%zu bytes)\n", static_cast<size_t>(size));
                return size > 100;
            }
            else
            {
                printError("Download reported success but file not found!\n");
                return false;
            }
        }
        else
        {
            printError("Download failed with error code: %d\n", ret);
            printError("Check your internet connection and DNS settings\n");
        }

        return false;
    }

    inline bool checkAmiiboDatabase()
    {
        // Ensure directory exists
        try
        {
            if (!std::filesystem::exists(EMUIIBO_PATH))
            {
                std::filesystem::create_directories(EMUIIBO_PATH);
            }
        }
        catch (const std::filesystem::filesystem_error &e)
        {
            printError("Error: Failed to create emuiibo directory: %s\n", e.what());
            return false;
        }

        // Check if database exists
        if (std::filesystem::exists(AMIIBO_DB_PATH))
        {
            try
            {
                auto file_size = std::filesystem::file_size(AMIIBO_DB_PATH);
                printMessage("Database found (%zu bytes)\n", static_cast<size_t>(file_size));
                return true;
            }
            catch (const std::filesystem::filesystem_error &e)
            {
                printMessage("Database found (size unknown)\n");
                return true;
            }
        }

        printMessage("\nNo database found. Downloading...\n");
        return downloadAmiiboDatabase();
    }

    inline bool isBlacklistedCharacter(char c)
    {
        // Only allow ASCII characters in file paths (cast to unsigned for comparison)
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 128)
        {
            return true;
        }

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

    // Random number generation with better seeding
    inline int RandU(int nMin, int nMax)
    {
        if (nMin > nMax)
        {
            std::swap(nMin, nMax);
        }
        return nMin + (rand() % (nMax - nMin + 1));
    }

    // Endian swap for 16-bit values
    inline uint16_t swap_uint16(uint16_t val)
    {
        return (val << 8) | (val >> 8);
    }

    // RAII wrapper for stbi allocated memory
    class ImageData
    {
    private:
        unsigned char *data;
        int width;
        int height;
        int channels;

    public:
        ImageData(const std::string &path)
            : data(nullptr), width(0), height(0), channels(0)
        {
            if (path.empty())
            {
                throw std::invalid_argument("Image path cannot be empty");
            }
            data = stbi_load(path.c_str(), &width, &height, &channels, 0);
            if (data == nullptr)
            {
                throw std::runtime_error("Failed to load image: " + path);
            }
        }

        ~ImageData()
        {
            if (data != nullptr)
            {
                stbi_image_free(data);
                data = nullptr;
            }
        }

        unsigned char *get() const { return data; }
        int get_width() const { return width; }
        int get_height() const { return height; }
        int get_channels() const { return channels; }

        // Disable copy operations
        ImageData(const ImageData &) = delete;
        ImageData &operator=(const ImageData &) = delete;

        // Allow move operations
        ImageData(ImageData &&other) noexcept
            : data(other.data), width(other.width), height(other.height), channels(other.channels)
        {
            other.data = nullptr;
        }

        ImageData &operator=(ImageData &&other) noexcept
        {
            if (this != &other)
            {
                if (data != nullptr)
                {
                    stbi_image_free(data);
                }
                data = other.data;
                width = other.width;
                height = other.height;
                channels = other.channels;
                other.data = nullptr;
            }
            return *this;
        }
    };

    inline int loadAndResizeImageInRatio(const std::string &imagePath)
    {
        try
        {
            if (imagePath.empty())
            {
                printError("Error: empty image path\n");
                return 0;
            }

            // Load image with RAII
            ImageData imgData(imagePath);

            // Calculate new dimensions maintaining aspect ratio
            int new_width = (TARGET_IMAGE_HEIGHT * imgData.get_width()) / imgData.get_height();

            if (new_width <= 0)
            {
                printError("Error: Invalid image dimensions for resizing\n");
                return 0;
            }

            // Allocate resized image buffer using unique_ptr
            size_t resized_size = new_width * TARGET_IMAGE_HEIGHT * imgData.get_channels();
            auto resized_data = std::make_unique<unsigned char[]>(resized_size);

            // Resize image
            stbir_resize_uint8_linear(
                imgData.get(), imgData.get_width(), imgData.get_height(), 0,
                resized_data.get(), new_width, TARGET_IMAGE_HEIGHT, 0,
                (stbir_pixel_layout)imgData.get_channels());

            // Convert RGB to RGBA if needed
            int final_channels = imgData.get_channels();
            std::unique_ptr<unsigned char[]> final_data;

            if (imgData.get_channels() == 3)
            {
                size_t rgba_size = new_width * TARGET_IMAGE_HEIGHT * 4;
                final_data = std::make_unique<unsigned char[]>(rgba_size);

                for (int i = 0; i < new_width * TARGET_IMAGE_HEIGHT; i++)
                {
                    final_data[i * 4 + 0] = resized_data[i * 3 + 0];
                    final_data[i * 4 + 1] = resized_data[i * 3 + 1];
                    final_data[i * 4 + 2] = resized_data[i * 3 + 2];
                    final_data[i * 4 + 3] = 255;
                }
                final_channels = 4;
            }
            else
            {
                final_data = std::move(resized_data);
            }

            // Write resized image
            int write_result = stbi_write_png(
                imagePath.c_str(), new_width, TARGET_IMAGE_HEIGHT, final_channels,
                final_data.get(), new_width * final_channels);

            return write_result ? 1 : 0;
        }
        catch (const std::exception &e)
        {
            printError("Error loading/resizing image: %s\n", e.what());
            return 0;
        }
    }
}
