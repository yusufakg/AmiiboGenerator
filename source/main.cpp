#include <fstream>
#include <iostream>
#include <stdexcept>

#include "libs/json.hpp"
#include "amiibomenu.hpp"
#include "util.hpp"

#include <switch.h>

using json = nlohmann::json;

int main(int, char **)
{
    // Initialize console first for debug output
    consoleInit(NULL);
    printf("AmiiboGenerator Starting...\n");
    consoleUpdate(NULL);

    // Initialize sockets for network operations
    printf("Initializing sockets...\n");
    consoleUpdate(NULL);
    Result rc = socketInitializeDefault();
    if (R_FAILED(rc))
    {
        fprintf(stderr, "Error: Failed to initialize sockets (0x%x)\n", rc);
        fprintf(stderr, "Network features will not work\n");
        consoleUpdate(NULL);
        svcSleepThread(2000000000ULL);
    }
    else
    {
        printf("Sockets initialized successfully\n");
        consoleUpdate(NULL);
    }

    // Disable auto-sleep to prevent interruptions
    printf("Configuring applet settings...\n");
    consoleUpdate(NULL);
    appletSetAutoSleepDisabled(true);

    try
    {
        printf("Checking amiibo database...\n");
        consoleUpdate(NULL);

        // Check and load amiibo database
        bool dbCheckResult = UTIL::checkAmiiboDatabase();

        if (!dbCheckResult)
        {
            fprintf(stderr, "Error: Failed to check/load amiibo database\n");
            fprintf(stderr, "Press + to exit\n");
            consoleUpdate(NULL);

            // Wait for user to exit
            PadState pad;
            padConfigureInput(1, HidNpadStyleSet_NpadStandard);
            padInitializeDefault(&pad);

            while (appletMainLoop())
            {
                padUpdate(&pad);
                if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
                    break;
                svcSleepThread(50000000ULL);
            }
        }
        else
        {
            printf("Opening database file...\n");
            consoleUpdate(NULL);

            std::ifstream db_file("sdmc:/emuiibo/amiibos.json");
            if (!db_file.is_open())
            {
                fprintf(stderr, "Error: Failed to open amiibo database file\n");
                fprintf(stderr, "Press + to exit\n");
                consoleUpdate(NULL);
            }
            else
            {
                printf("Parsing database...\n");
                consoleUpdate(NULL);

                json amiibodata;
                try
                {
                    db_file >> amiibodata;
                    db_file.close();

                    if (!amiibodata.contains("amiibo"))
                    {
                        fprintf(stderr, "Error: Invalid database format - missing 'amiibo' key\n");
                        fprintf(stderr, "Press + to exit\n");
                        consoleUpdate(NULL);
                    }
                    else
                    {
                        printf("Creating menu with %zu amiibos...\n", amiibodata["amiibo"].size());
                        consoleUpdate(NULL);

                        // Create and run menu
                        AmiiboMenu menu(amiibodata);
                        menu.mainLoop();
                    }
                }
                catch (const json::exception &e)
                {
                    fprintf(stderr, "Error: Failed to parse database JSON: %s\n", e.what());
                    fprintf(stderr, "Press + to exit\n");
                    consoleUpdate(NULL);
                }
                catch (const std::exception &e)
                {
                    fprintf(stderr, "Error: Exception in main loop: %s\n", e.what());
                    fprintf(stderr, "Press + to exit\n");
                    consoleUpdate(NULL);
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "Fatal error: %s\n", e.what());
        fprintf(stderr, "Press + to exit\n");
        consoleUpdate(NULL);

        // Wait for user to exit
        PadState pad;
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);

        while (appletMainLoop())
        {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
                break;
            svcSleepThread(50000000ULL);
        }
    }

    // Cleanup in reverse order of initialization
    appletSetAutoSleepDisabled(false);
    socketExit();
    consoleExit(NULL);

    return 0;
}
