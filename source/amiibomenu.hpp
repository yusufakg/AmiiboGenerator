#pragma once

#include <switch.h>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cstdint>
#include <stdexcept>

#include "libs/json.hpp"
#include "amiibo.hpp"

using json = nlohmann::json;

class AmiiboMenu
{
private:
    static constexpr int VISIBLE_ITEMS = 38; // Items that fit on screen (leaving room for header)
    static constexpr int SORT_OPTIONS_COUNT = 4;
    static constexpr const char *SORT_FIELDS[] = {"amiiboSeries", "amiiboSeries", "name", "name"};
    static constexpr char SORT_DIRECTIONS[] = {'A', 'D', 'A', 'D'};

    json amiibodata;
    int currentSelectedAmiibos;
    int currentCursorIndex; // Global index in the full list
    int scrollOffset;       // Index of the first visible item
    int currentSortIndex;
    bool generateWithImage;
    bool shallExit;

    PadState pad;

    // Helper function to safely get JSON value
    template <typename T>
    static T getJsonValue(const json &obj, const std::string &key, const T &default_val = T())
    {
        try
        {
            if (obj.contains(key))
            {
                return obj[key].get<T>();
            }
        }
        catch (const std::exception &)
        {
            // Fall through to return default
        }
        return default_val;
    }

    // Validate cursor index
    bool isValidCursorIndex(int index) const
    {
        return index >= 0 && index < static_cast<int>(amiibodata["amiibo"].size());
    }

    // Adjust scroll offset to keep cursor visible
    void adjustScrollOffset()
    {
        // If cursor is above visible area, scroll up
        if (currentCursorIndex < scrollOffset)
        {
            scrollOffset = currentCursorIndex;
        }
        // If cursor is below visible area, scroll down
        else if (currentCursorIndex >= scrollOffset + VISIBLE_ITEMS)
        {
            scrollOffset = currentCursorIndex - VISIBLE_ITEMS + 1;
        }

        // Ensure scroll offset is valid
        int maxOffset = std::max(0, static_cast<int>(amiibodata["amiibo"].size()) - VISIBLE_ITEMS);
        scrollOffset = std::max(0, std::min(scrollOffset, maxOffset));
    }

public:
    AmiiboMenu(const json &AmiiboData)
        : amiibodata(AmiiboData),
          currentSelectedAmiibos(0),
          currentCursorIndex(0),
          scrollOffset(0),
          currentSortIndex(0),
          generateWithImage(false),
          shallExit(false),
          pad{}
    {
        // PadState is now zero-initialized in member initializer list

        // Sort by amiiboSeries ascending on initialization
        sortAmiibo();
    }

    // Delete copy operations
    AmiiboMenu(const AmiiboMenu &) = delete;
    AmiiboMenu &operator=(const AmiiboMenu &) = delete;

    // Allow move operations
    AmiiboMenu(AmiiboMenu &&) noexcept = default;
    AmiiboMenu &operator=(AmiiboMenu &&) noexcept = default;

    void toggleAllAmiibo()
    {
        clearScreen();
        UTIL::printMessage("Toggling all amiibos. This might take a few seconds.\n");

        int newselected = 0;
        for (size_t i = 0; i < amiibodata["amiibo"].size(); i++)
        {
            json &currentItem = amiibodata["amiibo"][i];
            bool selected = getJsonValue(currentItem, "selected", false);
            if (!selected)
            {
                newselected++;
            }
            currentItem["selected"] = !selected;
        }

        currentSelectedAmiibos = newselected;
        updateScreen();
    }

    void updateAmiiboDatabase()
    {
        clearScreen();
        UTIL::printMessage("Updating amiibo database. This might take a few seconds.\n");

        try
        {
            if (std::filesystem::exists(UTIL::AMIIBO_DB_PATH))
            {
                std::filesystem::remove(UTIL::AMIIBO_DB_PATH);
            }
        }
        catch (const std::filesystem::filesystem_error &e)
        {
            UTIL::printError("Warning: Failed to remove old database: %s\n", e.what());
        }

        if (UTIL::checkAmiiboDatabase() == false)
        {
            UTIL::printMessage("Download failed!\n");
            shallExit = true;
            return;
        }

        UTIL::printMessage("Database updated!\n");

        try
        {
            std::ifstream i(UTIL::AMIIBO_DB_PATH);
            if (!i.is_open())
            {
                UTIL::printMessage("Failed to open database file.\n");
                return;
            }
            i >> amiibodata;
        }
        catch (const std::exception &e)
        {
            UTIL::printError("Failed to parse database: %s\n", e.what());
            return;
        }

        // Reset scroll position
        currentCursorIndex = 0;
        scrollOffset = 0;
        currentSelectedAmiibos = 0;
        currentSortIndex = 0;

        // Countdown before returning
        int loop = 5;
        while (loop > 0)
        {
            printf("Back in %d seconds...\n", loop);
            consoleUpdate(NULL);
            svcSleepThread(1000000000ull);
            loop--;
        }

        updateScreen();
    }

    void toggleImageGeneration()
    {
        generateWithImage = !generateWithImage;
        updateScreen();
    }

    void clearScreen()
    {
        consoleClear();
    }

    void updateScreen()
    {
        clearScreen();
        showMainScreen();
        consoleUpdate(NULL);
    }

    void showMainScreen()
    {
        // App header
        printf("=== AmiiboGenerator ===                        - : Update DB  |  + : Exit\n\n");

        // Status line
        printf("Selected: %d/%zu   Images: %s   Sort: %s %s\n\n",
               currentSelectedAmiibos, amiibodata["amiibo"].size(),
               generateWithImage ? "ON " : "OFF",
               SORT_FIELDS[currentSortIndex],
               SORT_DIRECTIONS[currentSortIndex] == 'A' ? "ASC" : "DESC");

        // Controls
        printf("ZL : Select All | ZR : Toggle Images | Y : Sort | X : Generate\n\n");

        showVisibleItems();
    }

    void nextInfoIndex()
    {
        currentSortIndex = (currentSortIndex + 1) % SORT_OPTIONS_COUNT;
        sortAmiibo();
    }

    void showVisibleItems()
    {
        int totalItems = static_cast<int>(amiibodata["amiibo"].size());
        int endIndex = std::min(scrollOffset + VISIBLE_ITEMS, totalItems);

        for (int i = scrollOffset; i < endIndex; i++)
        {
            const json &currentItem = amiibodata["amiibo"][i];
            showItem(i, currentItem);
        }
    }

    void showItem(int globalIndex, const json &data)
    {
        std::string indicatorSelected = " ";
        if (getJsonValue(data, "selected", false))
        {
            indicatorSelected = "x";
        }

        std::string cursor = (globalIndex == currentCursorIndex) ? ">" : " ";

        try
        {
            std::string amiiboSeries = getJsonValue(data, "amiiboSeries", std::string("Unknown"));
            std::string amiiboName = getJsonValue(data, "name", std::string("Unknown"));
            std::string text = amiiboSeries + " - " + amiiboName;
            printf("%s [%s] %d) %s\n", cursor.c_str(), indicatorSelected.c_str(), globalIndex + 1, text.c_str());
        }
        catch (const std::exception &e)
        {
            fprintf(stderr, "Error displaying item: %s\n", e.what());
        }
    }

    void moveCursor(int delta)
    {
        int newCursorIndex = currentCursorIndex + delta;
        int totalItems = static_cast<int>(amiibodata["amiibo"].size());

        // Clamp to valid range
        newCursorIndex = std::max(0, std::min(newCursorIndex, totalItems - 1));

        if (newCursorIndex != currentCursorIndex)
        {
            currentCursorIndex = newCursorIndex;
            adjustScrollOffset();
            updateScreen();
        }
    }

    void jumpCursor(int delta)
    {
        // Fast jump (10 items at a time)
        moveCursor(delta * 10);
    }

    void toggleCurrentItem()
    {
        if (!isValidCursorIndex(currentCursorIndex))
        {
            fprintf(stderr, "Item number is out of range.\n");
            return;
        }

        json &currentItem = amiibodata["amiibo"][currentCursorIndex];

        bool selected = getJsonValue(currentItem, "selected", false);
        currentItem["selected"] = !selected;

        if (!selected)
        {
            currentSelectedAmiibos++;
        }
        else
        {
            currentSelectedAmiibos--;
        }

        updateScreen();
    }

    void inputHandler()
    {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        if (kDown & HidNpadButton_Plus)
            shallExit = true;
        if (kDown & HidNpadButton_Minus)
            updateAmiiboDatabase();
        if (kDown & HidNpadButton_Up)
            moveCursor(-1);
        if (kDown & HidNpadButton_Down)
            moveCursor(+1);
        if (kDown & HidNpadButton_Left)
            jumpCursor(-1); // Jump up 10 items
        if (kDown & HidNpadButton_Right)
            jumpCursor(+1); // Jump down 10 items
        if (kDown & HidNpadButton_L)
            moveCursor(-VISIBLE_ITEMS); // Jump up one screen
        if (kDown & HidNpadButton_R)
            moveCursor(+VISIBLE_ITEMS); // Jump down one screen
        if (kDown & HidNpadButton_ZL)
            toggleAllAmiibo();
        if (kDown & HidNpadButton_ZR)
            toggleImageGeneration();
        if (kDown & HidNpadButton_A)
            toggleCurrentItem();
        if (kDown & HidNpadButton_X)
            generateAmiibo();
        if (kDown & HidNpadButton_Y)
            nextInfoIndex();
        if (kDown & HidNpadButton_StickL)
            deleteSelectedAmiibo();

        // Left stick scrolling with variable speed based on stick position
        HidAnalogStickState analog_stick_l = padGetStickPos(&pad, 0);
        const int STICK_DEADZONE = 8000;
        const int STICK_MAX = 32767;

        int stick_y = analog_stick_l.y;
        if (abs(stick_y) > STICK_DEADZONE)
        {
            // Calculate scroll speed based on stick position
            // Range from 1 (just past deadzone) to 10 (max deflection)
            float stick_percentage = (float)(abs(stick_y) - STICK_DEADZONE) / (float)(STICK_MAX - STICK_DEADZONE);
            int scroll_speed = 1 + (int)(stick_percentage * 9); // 1 to 10

            if (stick_y > 0)
            {
                // Stick up - scroll up
                moveCursor(-scroll_speed);
            }
            else
            {
                // Stick down - scroll down
                moveCursor(scroll_speed);
            }
        }
    }

    void generateAmiibo()
    {
        clearScreen();

        if (currentSelectedAmiibos == 0)
        {
            UTIL::printMessage("No amiibos selected.\n");
            svcSleepThread(2000000000ull);
            updateScreen();
            return;
        }

        int generatedAmiibo = 1;

        for (size_t i = 0; i < amiibodata["amiibo"].size(); i++)
        {
            const json &currentItem = amiibodata["amiibo"][i];
            if (getJsonValue(currentItem, "selected", false))
            {
                try
                {
                    std::string amiiboSeries = getJsonValue(currentItem, "amiiboSeries", std::string("Unknown"));
                    std::string amiiboName = getJsonValue(currentItem, "name", std::string("Unknown"));
                    printf("%d/%d - Generating: %s - %s\n",
                           generatedAmiibo,
                           currentSelectedAmiibos,
                           amiiboSeries.c_str(),
                           amiiboName.c_str());
                    consoleUpdate(NULL);

                    Amiibo amiibo(currentItem);
                    if (!amiibo.generate(generateWithImage))
                    {
                        printf("Failed to generate amiibo.\n");
                    }
                    generatedAmiibo++;
                }
                catch (const std::exception &e)
                {
                    fprintf(stderr, "Error generating amiibo: %s\n", e.what());
                }
            }
        }

        printf("Done!\n");
        printf("Press B to go back.\n");
        consoleUpdate(NULL);

        bool loop = true;
        while (loop)
        {
            padUpdate(&pad);
            u64 kDown = padGetButtonsDown(&pad);

            if (kDown & HidNpadButton_B)
                loop = false;

            svcSleepThread(50000000ull);
        }

        updateScreen();
    }

    void deleteSelectedAmiibo()
    {
        clearScreen();

        if (currentSelectedAmiibos == 0)
        {
            UTIL::printMessage("No amiibos selected for deletion.\n");
            svcSleepThread(1500000000ull);
            updateScreen();
            return;
        }

        UTIL::printMessage("Deleting %d amiibos. Please wait...\n\n", currentSelectedAmiibos);
        consoleUpdate(NULL);

        int deletedCount = 0;
        int skippedCount = 0;
        int processedCount = 0;

        for (size_t i = 0; i < amiibodata["amiibo"].size(); i++)
        {
            json &currentItem = amiibodata["amiibo"][i];
            if (getJsonValue(currentItem, "selected", false))
            {
                processedCount++;
                try
                {
                    std::string name = getJsonValue(currentItem, "name", std::string("Unknown"));
                    printf("[%d/%d] %s... ", processedCount, currentSelectedAmiibos, name.c_str());
                    consoleUpdate(NULL);

                    // First check if the amiibo folder actually exists
                    Amiibo amiibo(currentItem);

                    // Build path to check existence (same logic as in Amiibo class)
                    std::string head = getJsonValue(currentItem, "head", std::string(""));
                    std::string tail = getJsonValue(currentItem, "tail", std::string(""));

                    if (!head.empty() && !tail.empty())
                    {
                        std::string amiiboSeries = getJsonValue(currentItem, "amiiboSeries", std::string(""));
                        std::string amiiboName = getJsonValue(currentItem, "name", std::string(""));
                        std::string amiiboId = head + tail;

                        if (!amiiboSeries.empty() && !amiiboName.empty())
                        {
                            std::string path = std::string("sdmc:/emuiibo/amiibo/") + amiiboSeries + "/" + amiiboName + "_" + amiiboId + "/";

                            if (!std::filesystem::exists(path))
                            {
                                printf("SKIP (not found)\n");
                                skippedCount++;
                                currentItem["selected"] = false;
                                consoleUpdate(NULL);
                                continue;
                            }
                        }
                    }

                    if (amiibo.erase())
                    {
                        printf("OK\n");
                        deletedCount++;
                    }
                    else
                    {
                        printf("FAILED\n");
                    }

                    // Reset selection for this item only
                    currentItem["selected"] = false;
                    consoleUpdate(NULL);
                }
                catch (const std::exception &e)
                {
                    printf("ERROR: %s\n", e.what());
                    currentItem["selected"] = false;
                    consoleUpdate(NULL);
                }
            }
        }

        currentSelectedAmiibos = 0;

        // Clean up empty directories
        try
        {
            std::string base_path = "sdmc:/emuiibo/amiibo/";
            if (std::filesystem::exists(base_path))
            {
                for (const auto &entry : std::filesystem::directory_iterator(base_path))
                {
                    if (entry.is_directory() && std::filesystem::is_empty(entry.path()))
                    {
                        std::filesystem::remove(entry.path());
                    }
                }
            }
        }
        catch (const std::exception &e)
        {
            // Silently ignore cleanup errors
        }

        printf("\nCompleted: %d deleted, %d skipped, %d failed.\n", deletedCount, skippedCount, processedCount - deletedCount - skippedCount);
        printf("Press B to continue.\n");
        consoleUpdate(NULL);

        bool loop = true;
        while (loop)
        {
            padUpdate(&pad);
            u64 kDown = padGetButtonsDown(&pad);

            if (kDown & HidNpadButton_B)
                loop = false;

            svcSleepThread(50000000ull);
        }

        updateScreen();
    }

    void sortAmiibo()
    {
        const std::string sortingKey = SORT_FIELDS[currentSortIndex];
        bool ascending = (SORT_DIRECTIONS[currentSortIndex] == 'A');

        try
        {
            std::sort(amiibodata["amiibo"].begin(), amiibodata["amiibo"].end(),
                      [this, &sortingKey, ascending](const json &a, const json &b)
                      {
                          try
                          {
                              auto a_val = a.contains(sortingKey) ? a[sortingKey] : json();
                              auto b_val = b.contains(sortingKey) ? b[sortingKey] : json();
                              if (ascending)
                              {
                                  return a_val < b_val;
                              }
                              else
                              {
                                  return a_val > b_val;
                              }
                          }
                          catch (const std::exception &)
                          {
                              return false;
                          }
                      });

            updateScreen();
        }
        catch (const std::exception &e)
        {
            fprintf(stderr, "Error sorting amiibo: %s\n", e.what());
        }
    }

    int mainLoop()
    {
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);

        updateScreen();

        while (appletMainLoop())
        {
            if (shallExit)
            {
                break;
            }
            inputHandler();
            svcSleepThread(50000000ull); // 50ms
        }

        return 0;
    }

    bool shouldExit() const
    {
        return shallExit;
    }
};
