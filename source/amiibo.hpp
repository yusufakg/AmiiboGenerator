#pragma once

#include <fstream>
#include <iostream>
#include <vector>
#include <dirent.h>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <optional>
#include <cstring>

#include "util.hpp"
#include "libs/json.hpp"

using json = nlohmann::json;

class Amiibo
{
private:
    static constexpr const char *AMIIBO_BASE_PATH = "sdmc:/emuiibo/amiibo/";

    struct AmiiboId
    {
        std::string game_character_id;
        std::string character_variant;
        std::string character_figure_type;
        std::string model_number;
        std::string series;

        static std::optional<AmiiboId> parse(const std::string &amiibo_id_str)
        {
            if (amiibo_id_str.length() < 16)
            {
                return std::nullopt;
            }

            AmiiboId id;
            id.game_character_id = amiibo_id_str.substr(0, 4);
            id.character_variant = amiibo_id_str.substr(4, 2);
            id.character_figure_type = amiibo_id_str.substr(6, 2);
            id.model_number = amiibo_id_str.substr(8, 4);
            id.series = amiibo_id_str.substr(12, 2);
            return id;
        }
    };

    // Helper function to sanitize names for filesystem
    static std::string sanitizePath(const std::string &input)
    {
        std::string result = input;
        result.erase(
            std::remove_if(result.begin(), result.end(), &UTIL::isBlacklistedCharacter),
            result.end());
        std::replace(result.begin(), result.end(), '/', '_');
        return result;
    }

    // Helper function to convert hex string to int with error handling
    static std::optional<int> hexToInt(const std::string &hexStr)
    {
        if (hexStr.empty())
        {
            return std::nullopt;
        }
        try
        {
            return static_cast<int>(std::strtol(hexStr.c_str(), nullptr, 16));
        }
        catch (const std::exception &)
        {
            return std::nullopt;
        }
    }

    // Helper to validate and get amiibo data
    bool validateAmiiboData(std::string &head, std::string &tail) const
    {
        if (!amiibo.contains("head") || !amiibo.contains("tail"))
        {
            fprintf(stderr, "Error: Missing head or tail in amiibo data\n");
            return false;
        }
        head = amiibo["head"].get<std::string>();
        tail = amiibo["tail"].get<std::string>();
        return true;
    }

    // Helper to build amiibo path
    std::string buildAmiiboPath(const std::string &amiiboId) const
    {
        if (!amiibo.contains("amiiboSeries") || !amiibo.contains("name"))
        {
            return "";
        }

        std::string amiiboSeries = sanitizePath(amiibo["amiiboSeries"].get<std::string>());
        std::string amiiboName = sanitizePath(amiibo["name"].get<std::string>());

        return std::string(AMIIBO_BASE_PATH) + amiiboSeries + "/" + amiiboName + "_" + amiiboId + "/";
    }

public:
    json amiibo;

    explicit Amiibo(const json &data) : amiibo(data)
    {
        // Note: srand should ideally be called once in main, not per object
        // But keeping for compatibility - using nullptr instead of NULL for C++17
        srand(static_cast<unsigned>(time(nullptr)));
    }

    // Default destructor is fine for this class
    ~Amiibo() = default;

    // Delete copy operations to prevent accidental duplication
    Amiibo(const Amiibo &) = delete;
    Amiibo &operator=(const Amiibo &) = delete;

    // Allow move operations
    Amiibo(Amiibo &&) noexcept = default;
    Amiibo &operator=(Amiibo &&) noexcept = default;

    bool generate(bool withImage = false)
    {
        try
        {
            // Get current date/time
            time_t unixTime = time(nullptr);
            struct tm *timeStruct = gmtime(&unixTime);
            if (timeStruct == nullptr)
            {
                fprintf(stderr, "Error: Failed to get current time\n");
                return false;
            }

            int day = timeStruct->tm_mday;
            int month = timeStruct->tm_mon;
            int year = timeStruct->tm_year + 1900;

            // Validate and parse amiibo data
            std::string head, tail;
            if (!validateAmiiboData(head, tail))
            {
                return false;
            }

            std::string amiiboId = head + tail;

            // Parse amiibo ID
            auto id_opt = AmiiboId::parse(amiiboId);
            if (!id_opt.has_value())
            {
                fprintf(stderr, "Amiibo ID is invalid\n");
                return false;
            }

            const auto &id = id_opt.value();

            // Convert hex values to integers
            auto cgid = hexToInt(id.game_character_id);
            auto cvar = hexToInt(id.character_variant);
            auto ftype = hexToInt(id.character_figure_type);
            auto mnum = hexToInt(id.model_number);
            auto snum = hexToInt(id.series);

            if (!cgid.has_value() || !cvar.has_value() || !ftype.has_value() ||
                !mnum.has_value() || !snum.has_value())
            {
                fprintf(stderr, "Error: Invalid hex values in amiibo ID\n");
                return false;
            }

            int character_game_id_int_swap = UTIL::swap_uint16(cgid.value());

            // Build amiibo data JSON
            json amiiboData = json::object();
            amiiboData["name"] = amiibo["name"];
            amiiboData["write_counter"] = 0;
            amiiboData["version"] = 0;

            amiiboData["first_write_date"] = json::object();
            amiiboData["first_write_date"]["y"] = year;
            amiiboData["first_write_date"]["m"] = month + 1;
            amiiboData["first_write_date"]["d"] = day;

            amiiboData["last_write_date"] = json::object();
            amiiboData["last_write_date"]["y"] = year;
            amiiboData["last_write_date"]["m"] = month + 1;
            amiiboData["last_write_date"]["d"] = day;

            amiiboData["mii_charinfo_file"] = "mii-charinfo.bin";

            amiiboData["id"] = json::object();
            amiiboData["id"]["game_character_id"] = character_game_id_int_swap;
            amiiboData["id"]["character_variant"] = cvar.value();
            amiiboData["id"]["figure_type"] = ftype.value();
            amiiboData["id"]["series"] = snum.value();
            amiiboData["id"]["model_number"] = mnum.value();

            // Generate UUID
            amiiboData["uuid"] = json::array();
            for (int i = 0; i < 7; i++)
            {
                amiiboData["uuid"][i] = UTIL::RandU(0, 255);
            }
            amiiboData["uuid"][7] = 0;
            amiiboData["uuid"][8] = 0;
            amiiboData["uuid"][9] = 0;

            // Build full path
            std::string amiiboPathFull = buildAmiiboPath(amiiboId);
            if (amiiboPathFull.empty())
            {
                fprintf(stderr, "Error: Missing amiiboSeries or name in amiibo data\n");
                return false;
            }

            // Check if already exists
            if (std::filesystem::exists(amiiboPathFull))
            {
                printf("Amiibo already exists.\n");
                return false;
            }

            // Create directories and files
            try
            {
                std::filesystem::create_directories(amiiboPathFull);
            }
            catch (const std::filesystem::filesystem_error &e)
            {
                fprintf(stderr, "Error: Failed to create amiibo directory: %s\n", e.what());
                return false;
            }

            // Write amiibo.flag file
            try
            {
                std::ofstream flag_file(amiiboPathFull + "amiibo.flag");
                if (!flag_file.is_open())
                {
                    fprintf(stderr, "Error: Failed to create amiibo.flag\n");
                    return false;
                }
                flag_file.close();
            }
            catch (const std::exception &e)
            {
                fprintf(stderr, "Error: Failed to write amiibo.flag: %s\n", e.what());
                return false;
            }

            // Write amiibo.json file
            try
            {
                std::ofstream json_file(amiiboPathFull + "amiibo.json");
                if (!json_file.is_open())
                {
                    fprintf(stderr, "Error: Failed to create amiibo.json\n");
                    return false;
                }
                json_file << amiiboData.dump(2);
                json_file.close();
            }
            catch (const std::exception &e)
            {
                fprintf(stderr, "Error: Failed to write amiibo.json: %s\n", e.what());
                return false;
            }

            // Download image if requested
            if (withImage && amiibo.contains("image"))
            {
                try
                {
                    std::string image_url = amiibo["image"].get<std::string>();
                    int ret = UTIL::downloadFile(image_url, amiiboPathFull + "amiibo.png");
                    if (ret != 0)
                    {
                        fprintf(stderr, "Warning: Failed to download image. Error code: %d\n", ret);
                    }
                    else
                    {
                        UTIL::loadAndResizeImageInRatio(amiiboPathFull + "amiibo.png");
                    }
                }
                catch (const std::exception &e)
                {
                    fprintf(stderr, "Warning: Failed to process image: %s\n", e.what());
                }
            }

            return true;
        }
        catch (const std::exception &e)
        {
            fprintf(stderr, "Error: Exception during amiibo generation: %s\n", e.what());
            return false;
        }
    }

    bool erase()
    {
        try
        {
            // Validate data
            std::string head, tail;
            if (!validateAmiiboData(head, tail))
            {
                return false;
            }

            std::string amiiboId = head + tail;

            // Parse amiibo ID
            auto id_opt = AmiiboId::parse(amiiboId);
            if (!id_opt.has_value())
            {
                fprintf(stderr, "Amiibo ID is invalid\n");
                return false;
            }

            // Build full path
            std::string amiiboPathFull = buildAmiiboPath(amiiboId);
            if (amiiboPathFull.empty())
            {
                fprintf(stderr, "Error: Missing amiiboSeries or name in amiibo data\n");
                return false;
            }

            // Remove directory
            try
            {
                std::uintmax_t removed_count = std::filesystem::remove_all(amiiboPathFull);
                printf("Deleted amiibo directory with %ju items\n", removed_count);
                return true;
            }
            catch (const std::filesystem::filesystem_error &e)
            {
                fprintf(stderr, "Error: Failed to delete amiibo: %s\n", e.what());
                return false;
            }
        }
        catch (const std::exception &e)
        {
            fprintf(stderr, "Error: Exception during amiibo deletion: %s\n", e.what());
            return false;
        }
    }
};
