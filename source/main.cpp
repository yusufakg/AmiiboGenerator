#include <fstream>
#include <cstdlib>
#include <ctime>

#include <switch.h>

#include "libs/json.hpp"
#include "amiibomenu.hpp"
#include "util.hpp"

using json = nlohmann::json;

namespace
{
    void waitForExit(PadState &pad)
    {
        std::fputs("Press + to exit\n", stderr);
        consoleUpdate(nullptr);
        while (appletMainLoop())
        {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
                break;
            svcSleepThread(50000000ULL);
        }
    }
}

int main(int, char **)
{
    // Seed random number generator once
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    consoleInit(nullptr);
    std::puts("AmiiboGenerator Starting...");
    consoleUpdate(nullptr);

    // Initialize sockets
    std::puts("Initializing sockets...");
    consoleUpdate(nullptr);
    if (const Result rc = socketInitializeDefault(); R_FAILED(rc))
    {
        std::fprintf(stderr, "Error: Failed to initialize sockets (0x%x)\n", rc);
        std::fputs("Network features will not work\n", stderr);
        consoleUpdate(nullptr);
        svcSleepThread(2000000000ULL);
    }
    else
    {
        std::puts("Sockets initialized successfully");
        consoleUpdate(nullptr);
    }

    appletSetAutoSleepDisabled(true);

    PadState pad{};
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    std::puts("Checking amiibo database...");
    consoleUpdate(nullptr);

    if (!UTIL::checkAmiiboDatabase())
    {
        std::fputs("Error: Failed to check/load amiibo database\n", stderr);
        waitForExit(pad);
    }
    else
    {
        std::puts("Opening database file...");
        consoleUpdate(nullptr);

        const std::string dbPath(UTIL::AMIIBO_DB_PATH);
        if (std::ifstream dbFile(dbPath); dbFile)
        {
            std::puts("Parsing database...");
            consoleUpdate(nullptr);

            json amiibodata;
            dbFile >> amiibodata;
            dbFile.close();

            if (!amiibodata.contains("amiibo"))
            {
                std::fputs("Error: Invalid database format - missing 'amiibo' key\n", stderr);
                waitForExit(pad);
            }
            else
            {
                std::printf("Creating menu with %zu amiibos...\n", amiibodata["amiibo"].size());
                consoleUpdate(nullptr);

                AmiiboMenu menu(amiibodata);
                menu.mainLoop();
            }
        }
        else
        {
            std::fputs("Error: Failed to open amiibo database file\n", stderr);
            waitForExit(pad);
        }
    }

    appletSetAutoSleepDisabled(false);
    socketExit();
    consoleExit(nullptr);
    return 0;
}
