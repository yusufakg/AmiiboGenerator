#pragma once

#include <switch.h>
#include <string>
#include <string_view>
#include <filesystem>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>

#include "libs/json.hpp"
#include "amiibo.hpp"

using json = nlohmann::json;

class AmiiboMenu
{
    static constexpr int VISIBLE_ITEMS = 38;
    static constexpr int SORT_OPTIONS_COUNT = 4;
    static constexpr std::string_view SORT_FIELDS[] = {"amiiboSeries", "amiiboSeries", "name", "name"};
    static constexpr char SORT_DIRECTIONS[] = {'A', 'D', 'A', 'D'};

    json amiibodata_;
    int selectedCount_ = 0;
    int cursorIndex_ = 0;
    int scrollOffset_ = 0;
    int sortIndex_ = 0;
    bool withImage_ = false;
    bool shouldExit_ = false;
    PadState pad_{};

    template <typename T>
    [[nodiscard]] static T getJsonValue(const json &obj, std::string_view key, const T &defVal = T{})
    {
        const std::string keyStr(key);
        if (obj.contains(keyStr))
        {
            try
            {
                return obj[keyStr].get<T>();
            }
            catch (...)
            {
            }
        }
        return defVal;
    }

    [[nodiscard]] bool isValidIndex(int idx) const noexcept
    {
        return idx >= 0 && idx < static_cast<int>(amiibodata_["amiibo"].size());
    }

    void adjustScrollOffset() noexcept
    {
        if (cursorIndex_ < scrollOffset_)
            scrollOffset_ = cursorIndex_;
        else if (cursorIndex_ >= scrollOffset_ + VISIBLE_ITEMS)
            scrollOffset_ = cursorIndex_ - VISIBLE_ITEMS + 1;

        const int maxOffset = std::max(0, static_cast<int>(amiibodata_["amiibo"].size()) - VISIBLE_ITEMS);
        scrollOffset_ = std::clamp(scrollOffset_, 0, maxOffset);
    }

public:
    explicit AmiiboMenu(const json &data) : amiibodata_(data) { sortAmiibo(); }

    AmiiboMenu(const AmiiboMenu &) = delete;
    AmiiboMenu &operator=(const AmiiboMenu &) = delete;
    AmiiboMenu(AmiiboMenu &&) noexcept = default;
    AmiiboMenu &operator=(AmiiboMenu &&) noexcept = default;

    void toggleAllAmiibo()
    {
        clearScreen();

        int newSelected = 0;
        for (auto &item : amiibodata_["amiibo"])
        {
            const bool selected = getJsonValue(item, "selected", false);
            if (!selected)
                ++newSelected;
            item["selected"] = !selected;
        }
        selectedCount_ = newSelected;
        updateScreen();
    }

    void updateAmiiboDatabase()
    {
        clearScreen();
        UTIL::printMessage("Updating amiibo database...\n");

        std::error_code ec;
        const std::string dbPath(UTIL::AMIIBO_DB_PATH);
        std::filesystem::remove(dbPath, ec);

        if (!UTIL::checkAmiiboDatabase())
        {
            UTIL::printMessage("Download failed!\n");
            shouldExit_ = true;
            return;
        }

        UTIL::printMessage("Database updated!\n");

        if (std::ifstream file(dbPath); file)
            file >> amiibodata_;
        else
        {
            UTIL::printError("Failed to open database file.\n");
            return;
        }

        cursorIndex_ = scrollOffset_ = selectedCount_ = sortIndex_ = 0;

        for (int i = 5; i > 0; --i)
        {
            std::printf("Back in %d seconds...\n", i);
            consoleUpdate(nullptr);
            svcSleepThread(1000000000ULL);
        }
        updateScreen();
    }

    void toggleImageGeneration()
    {
        withImage_ = !withImage_;
        updateScreen();
    }
    void clearScreen() { consoleClear(); }
    void updateScreen()
    {
        clearScreen();
        showMainScreen();
        consoleUpdate(nullptr);
    }

    void showMainScreen()
    {
        std::puts("=== AmiiboGenerator ===                        - : Update DB  |  + : Exit\n");
        std::printf("Selected: %d/%zu   Images: %s   Sort: %.*s %s\n\n",
                    selectedCount_, amiibodata_["amiibo"].size(),
                    withImage_ ? "ON " : "OFF",
                    static_cast<int>(SORT_FIELDS[sortIndex_].size()), SORT_FIELDS[sortIndex_].data(),
                    SORT_DIRECTIONS[sortIndex_] == 'A' ? "ASC" : "DESC");
        std::puts("ZL : Select All | ZR : Toggle Images | Y : Sort | X : Generate\n");
        showVisibleItems();
    }

    void nextSortOption()
    {
        sortIndex_ = (sortIndex_ + 1) % SORT_OPTIONS_COUNT;
        sortAmiibo();
    }

    void showVisibleItems()
    {
        const int total = static_cast<int>(amiibodata_["amiibo"].size());
        const int end = std::min(scrollOffset_ + VISIBLE_ITEMS, total);
        for (int i = scrollOffset_; i < end; ++i)
            showItem(i, amiibodata_["amiibo"][i]);
    }

    void showItem(int idx, const json &data)
    {
        const char sel = getJsonValue(data, "selected", false) ? 'x' : ' ';
        const char cur = (idx == cursorIndex_) ? '>' : ' ';
        const auto series = getJsonValue(data, "amiiboSeries", std::string("Unknown"));
        const auto name = getJsonValue(data, "name", std::string("Unknown"));
        std::printf("%c [%c] %d) %s - %s\n", cur, sel, idx + 1, series.c_str(), name.c_str());
    }

    void moveCursor(int delta)
    {
        const int total = static_cast<int>(amiibodata_["amiibo"].size());
        const int newIdx = std::clamp(cursorIndex_ + delta, 0, total - 1);
        if (newIdx != cursorIndex_)
        {
            cursorIndex_ = newIdx;
            adjustScrollOffset();
            updateScreen();
        }
    }

    void jumpCursor(int delta) { moveCursor(delta * 10); }

    void toggleCurrentItem()
    {
        if (!isValidIndex(cursorIndex_))
            return;

        auto &item = amiibodata_["amiibo"][cursorIndex_];
        const bool wasSelected = getJsonValue(item, "selected", false);
        item["selected"] = !wasSelected;
        selectedCount_ += wasSelected ? -1 : 1;
        updateScreen();
    }

    void inputHandler()
    {
        padUpdate(&pad_);
        const u64 kDown = padGetButtonsDown(&pad_);

        if (kDown & HidNpadButton_Plus)
            shouldExit_ = true;
        if (kDown & HidNpadButton_Minus)
            updateAmiiboDatabase();
        if (kDown & HidNpadButton_Up)
            moveCursor(-1);
        if (kDown & HidNpadButton_Down)
            moveCursor(+1);
        if (kDown & HidNpadButton_Left)
            jumpCursor(-1);
        if (kDown & HidNpadButton_Right)
            jumpCursor(+1);
        if (kDown & HidNpadButton_L)
            moveCursor(-VISIBLE_ITEMS);
        if (kDown & HidNpadButton_R)
            moveCursor(+VISIBLE_ITEMS);
        if (kDown & HidNpadButton_ZL)
            toggleAllAmiibo();
        if (kDown & HidNpadButton_ZR)
            toggleImageGeneration();
        if (kDown & HidNpadButton_A)
            toggleCurrentItem();
        if (kDown & HidNpadButton_X)
            generateAmiibo();
        if (kDown & HidNpadButton_Y)
            nextSortOption();
        if (kDown & HidNpadButton_StickL)
            deleteSelectedAmiibo();

        // Analog stick scrolling
        const auto stick = padGetStickPos(&pad_, 0);
        constexpr int DEADZONE = 8000, MAX_STICK = 32767;
        if (std::abs(stick.y) > DEADZONE)
        {
            const float pct = static_cast<float>(std::abs(stick.y) - DEADZONE) / (MAX_STICK - DEADZONE);
            const int speed = 1 + static_cast<int>(pct * 9);
            moveCursor(stick.y > 0 ? -speed : speed);
        }
    }

    void generateAmiibo()
    {
        clearScreen();
        if (selectedCount_ == 0)
        {
            UTIL::printMessage("No amiibos selected.\n");
            svcSleepThread(2000000000ULL);
            updateScreen();
            return;
        }

        int count = 0;
        for (const auto &item : amiibodata_["amiibo"])
        {
            if (!getJsonValue(item, "selected", false))
                continue;
            ++count;
            const auto series = getJsonValue(item, "amiiboSeries", std::string("Unknown"));
            const auto name = getJsonValue(item, "name", std::string("Unknown"));
            std::printf("%d/%d - Generating: %s - %s\n", count, selectedCount_, series.c_str(), name.c_str());
            consoleUpdate(nullptr);

            Amiibo amiibo(item);
            if (!amiibo.generate(withImage_))
                std::puts("Failed to generate amiibo.");
        }

        std::puts("Done!\nPress B to go back.");
        consoleUpdate(nullptr);
        waitForButton(HidNpadButton_B);
        updateScreen();
    }

    void waitForButton(u64 button)
    {
        while (appletMainLoop())
        {
            padUpdate(&pad_);
            if (padGetButtonsDown(&pad_) & button)
                break;
            svcSleepThread(50000000ULL);
        }
    }

    void deleteSelectedAmiibo()
    {
        clearScreen();
        if (selectedCount_ == 0)
        {
            UTIL::printMessage("No amiibos selected for deletion.\n");
            svcSleepThread(1500000000ULL);
            updateScreen();
            return;
        }

        UTIL::printMessage("Deleting %d amiibos. Please wait...\n\n", selectedCount_);
        consoleUpdate(nullptr);

        int deleted = 0, skipped = 0, processed = 0;

        for (auto &item : amiibodata_["amiibo"])
        {
            if (!getJsonValue(item, "selected", false))
                continue;
            ++processed;

            const auto name = getJsonValue(item, "name", std::string("Unknown"));
            std::printf("[%d/%d] %s... ", processed, selectedCount_, name.c_str());
            consoleUpdate(nullptr);

            Amiibo amiibo(item);
            if (amiibo.erase())
            {
                std::puts("OK");
                ++deleted;
            }
            else
            {
                std::puts("SKIP");
                ++skipped;
            }
            item["selected"] = false;
            consoleUpdate(nullptr);
        }

        selectedCount_ = 0;

        // Clean up empty directories
        std::error_code ec;
        constexpr std::string_view basePath = "sdmc:/emuiibo/amiibo/";
        if (std::filesystem::exists(std::string(basePath), ec))
        {
            for (const auto &entry : std::filesystem::directory_iterator(std::string(basePath), ec))
            {
                if (entry.is_directory(ec) && std::filesystem::is_empty(entry.path(), ec))
                    std::filesystem::remove(entry.path(), ec);
            }
        }

        std::printf("\nCompleted: %d deleted, %d skipped, %d failed.\n",
                    deleted, skipped, processed - deleted - skipped);
        std::puts("Press B to continue.");
        consoleUpdate(nullptr);
        waitForButton(HidNpadButton_B);
        updateScreen();
    }

    void sortAmiibo()
    {
        const std::string sortKey(SORT_FIELDS[sortIndex_]);
        const bool ascending = (SORT_DIRECTIONS[sortIndex_] == 'A');

        std::sort(amiibodata_["amiibo"].begin(), amiibodata_["amiibo"].end(),
                  [&sortKey, ascending](const json &a, const json &b)
                  {
                      const auto aVal = a.contains(sortKey) ? a[sortKey] : json();
                      const auto bVal = b.contains(sortKey) ? b[sortKey] : json();
                      return ascending ? (aVal < bVal) : (aVal > bVal);
                  });
        updateScreen();
    }

    int mainLoop()
    {
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad_);
        updateScreen();

        while (appletMainLoop() && !shouldExit_)
        {
            inputHandler();
            svcSleepThread(50000000ULL);
        }
        return 0;
    }

    [[nodiscard]] bool shouldExit() const noexcept { return shouldExit_; }
};
