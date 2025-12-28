#pragma once

#include <fstream>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "util.hpp"
#include "libs/json.hpp"

using json = nlohmann::json;

class Amiibo
{
private:
    static constexpr std::string_view AMIIBO_BASE_PATH = "sdmc:/emuiibo/amiibo/";

    struct AmiiboId
    {
        std::string game_character_id;
        std::string character_variant;
        std::string figure_type;
        std::string model_number;
        std::string series;

        [[nodiscard]] static std::optional<AmiiboId> parse(std::string_view id_str) noexcept
        {
            if (id_str.length() < 16)
                return std::nullopt;
            return AmiiboId{
                std::string(id_str.substr(0, 4)),
                std::string(id_str.substr(4, 2)),
                std::string(id_str.substr(6, 2)),
                std::string(id_str.substr(8, 4)),
                std::string(id_str.substr(12, 2))};
        }
    };

    // Helper function to sanitize names for filesystem
    [[nodiscard]] static std::string sanitizePath(std::string_view input)
    {
        std::string result;
        result.reserve(input.size());
        for (char c : input)
        {
            if (!UTIL::isBlacklistedCharacter(c))
                result += (c == '/') ? '_' : c;
        }
        return result;
    }

    // Helper function to convert hex string to int with error handling
    [[nodiscard]] static std::optional<int> hexToInt(std::string_view hexStr) noexcept
    {
        if (hexStr.empty())
            return std::nullopt;
        char *end = nullptr;
        const std::string str(hexStr);
        const long val = std::strtol(str.c_str(), &end, 16);
        if (end == str.c_str())
            return std::nullopt; // No conversion
        return static_cast<int>(val);
    }

    // Helper to validate and get amiibo data
    [[nodiscard]] bool validateAmiiboData(std::string &head, std::string &tail) const
    {
        if (!amiibo_.contains("head") || !amiibo_.contains("tail"))
        {
            std::fputs("Error: Missing head or tail in amiibo data\n", stderr);
            return false;
        }
        head = amiibo_["head"].get<std::string>();
        tail = amiibo_["tail"].get<std::string>();
        return true;
    }

    // Helper to build amiibo path
    [[nodiscard]] std::string buildAmiiboPath(std::string_view amiiboId) const
    {
        if (!amiibo_.contains("amiiboSeries") || !amiibo_.contains("name"))
            return {};

        const auto series = sanitizePath(amiibo_["amiiboSeries"].get<std::string>());
        const auto name = sanitizePath(amiibo_["name"].get<std::string>());
        return std::string(AMIIBO_BASE_PATH) + series + "/" + name + "_" + std::string(amiiboId) + "/";
    }

    json amiibo_;

public:
    explicit Amiibo(const json &data) : amiibo_(data) {}
    ~Amiibo() = default;

    Amiibo(const Amiibo &) = delete;
    Amiibo &operator=(const Amiibo &) = delete;
    Amiibo(Amiibo &&) noexcept = default;
    Amiibo &operator=(Amiibo &&) noexcept = default;

    [[nodiscard]] const json &data() const noexcept { return amiibo_; }

    [[nodiscard]] bool generate(bool withImage = false)
    {
        // Get current date/time
        const time_t unixTime = std::time(nullptr);
        const struct tm *ts = std::gmtime(&unixTime);
        if (!ts)
        {
            std::fputs("Error: Failed to get current time\n", stderr);
            return false;
        }
        const int day = ts->tm_mday;
        const int month = ts->tm_mon + 1;
        const int year = ts->tm_year + 1900;

        // Validate and parse amiibo data
        std::string head, tail;
        if (!validateAmiiboData(head, tail))
            return false;

        const std::string amiiboId = head + tail;
        const auto id = AmiiboId::parse(amiiboId);
        if (!id)
        {
            std::fputs("Amiibo ID is invalid\n", stderr);
            return false;
        }

        // Convert hex values to integers
        const auto cgid = hexToInt(id->game_character_id);
        const auto cvar = hexToInt(id->character_variant);
        const auto ftype = hexToInt(id->figure_type);
        const auto mnum = hexToInt(id->model_number);
        const auto snum = hexToInt(id->series);

        if (!cgid || !cvar || !ftype || !mnum || !snum)
        {
            std::fputs("Error: Invalid hex values in amiibo ID\n", stderr);
            return false;
        }

        // Build amiibo data JSON
        json amiiboData;
        amiiboData["name"] = amiibo_["name"];
        amiiboData["write_counter"] = 0;
        amiiboData["version"] = 0;
        amiiboData["first_write_date"] = {{"y", year}, {"m", month}, {"d", day}};
        amiiboData["last_write_date"] = {{"y", year}, {"m", month}, {"d", day}};
        amiiboData["mii_charinfo_file"] = "mii-charinfo.bin";
        amiiboData["id"] = {
            {"game_character_id", UTIL::swap_uint16(static_cast<uint16_t>(*cgid))},
            {"character_variant", *cvar},
            {"figure_type", *ftype},
            {"series", *snum},
            {"model_number", *mnum}};

        // Generate UUID
        amiiboData["uuid"] = json::array();
        for (int i = 0; i < 7; ++i)
            amiiboData["uuid"].push_back(UTIL::RandU(0, 255));
        for (int i = 0; i < 3; ++i)
            amiiboData["uuid"].push_back(0);

        // Build full path
        const std::string path = buildAmiiboPath(amiiboId);
        if (path.empty())
        {
            std::fputs("Error: Missing amiiboSeries or name\n", stderr);
            return false;
        }

        // Check if already exists
        std::error_code ec;
        if (std::filesystem::exists(path, ec))
        {
            std::puts("Amiibo already exists.");
            return false;
        }

        // Create directory
        if (!std::filesystem::create_directories(path, ec) && ec)
        {
            std::fprintf(stderr, "Error: Failed to create directory: %s\n", ec.message().c_str());
            return false;
        }

        // Write amiibo.flag
        if (std::ofstream flag(path + "amiibo.flag"); !flag)
        {
            std::fputs("Error: Failed to create amiibo.flag\n", stderr);
            return false;
        }

        // Write amiibo.json
        if (std::ofstream jsonFile(path + "amiibo.json"); jsonFile)
            jsonFile << amiiboData.dump(2);
        else
        {
            std::fputs("Error: Failed to create amiibo.json\n", stderr);
            return false;
        }

        // Download image if requested
        if (withImage && amiibo_.contains("image"))
        {
            const std::string imageUrl = amiibo_["image"].get<std::string>();
            const std::string imagePath = path + "amiibo.png";
            if (UTIL::downloadFile(imageUrl, imagePath) == 0)
            {
                if (!UTIL::loadAndResizeImageInRatio(imagePath))
                    std::fputs("Warning: Failed to resize image\n", stderr);
            }
        }

        return true;
    }

    [[nodiscard]] bool erase()
    {
        std::string head, tail;
        if (!validateAmiiboData(head, tail))
            return false;

        const std::string amiiboId = head + tail;
        if (!AmiiboId::parse(amiiboId))
        {
            std::fputs("Amiibo ID is invalid\n", stderr);
            return false;
        }

        const std::string path = buildAmiiboPath(amiiboId);
        if (path.empty())
        {
            std::fputs("Error: Missing amiiboSeries or name\n", stderr);
            return false;
        }

        std::error_code ec;
        const auto removed = std::filesystem::remove_all(path, ec);
        if (ec)
        {
            std::fprintf(stderr, "Error: Failed to delete amiibo: %s\n", ec.message().c_str());
            return false;
        }
        std::printf("Deleted amiibo directory with %ju items\n", static_cast<std::uintmax_t>(removed));
        return true;
    }
};
