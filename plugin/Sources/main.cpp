#include <3ds.h>
#include "common.hpp"
#include "plgldr.h"
#include <CTRPluginFramework.hpp>
#include "HokakuCTR.hpp"
#include "PatternManager.hpp"
#include "OSDManager.hpp"
#include "patches.hpp"
#include "PIALogger.hpp"
#include "rt.h"

#include <vector>
#include <string.h>

const char *AXIOM_PLUGIN_MAGIC = "AXOM";
constexpr u32 AXIOM_PLUGIN_VERSION = SYSTEM_VERSION(1, 0, 0);

namespace CTRPluginFramework
{
    u64 titleID;
    PluginHeader* pluginHeader = nullptr;

    // This function executes before the game runs.
    void    PatchProcess(FwkSettings &settings)
    {
        pluginHeader = settings.Header;

        PatternManager pm;

        initHokakuCTR(pm);
        initPiaLogger(pm);

        titleID = Process::GetTitleID();
        initPatches(pm, titleID);

        pm.Perform();

        enablePatches();
    }

    bool onHokakuCTROptionsFolderAction(MenuFolder& folder, ActionType type) {
        if (type == ActionType::Opening) {
            PatternStatus status = getHokakuCTRStatus();
            std::string statusString;
            switch (status) {
            case PatternStatus::NotFound:
                statusString = "HokakuCTR status: Not available";
                break;
            case PatternStatus::NotActive:
                statusString = "HokakuCTR status: Not active";
                break;
            case PatternStatus::Active:
                statusString = "HokakuCTR status: Active";
                break;
            }
            MenuEntry* statusEntry = new MenuEntry(statusString);
            MenuEntry* toggleEntry = new MenuEntry(status == PatternStatus::Active ? "Disable HokakuCTR" : "Enable HokakuCTR", nullptr, [](MenuEntry* entry) {
                // TODO - Refresh status entry
                if (getHokakuCTRStatus() == PatternStatus::NotActive) {
                    enableHokakuCTR();
                    entry->Name() = "Disable HokakuCTR";
                } else {
                    disableHokakuCTR();
                    entry->Name() = "Enable HokakuCTR";
                }
            });

            if (status == PatternStatus::NotFound) {
                toggleEntry->Disable();
            }

            folder.Append(statusEntry);
            folder.Append(toggleEntry);
        } else if (type == ActionType::Closing) {
            folder.Clear();
        }

        return true;
    }

    bool onPatchesOptionsFolderAction(MenuFolder& folder, ActionType type) {
        if (type == ActionType::Opening) {
            std::string statusString;
            switch (getACNLPatchesStatus()) {
            case PatternStatus::NotFound:
                statusString = "ACNL patches status: Not available";
                break;
            case PatternStatus::NotActive:
                statusString = "ACNL patches status: Not active";
                break;
            case PatternStatus::Active:
                statusString = "ACNL patches status: Active";
                break;
            }

            MenuEntry* statusEntry = new MenuEntry(statusString);
            folder.Append(statusEntry);
        } else if (type == ActionType::Closing) {
            folder.Clear();
        }

        return true;
    }

    bool onPiaLoggerOptionsFolderAction(MenuFolder& folder, ActionType type) {
        if (type == ActionType::Opening) {
            std::string statusString = getPiaLoggerStatus() == PatternStatus::NotFound ? "PIA logger status: Not available" : "PIA logger status: Active";

            MenuEntry* statusEntry = new MenuEntry(statusString);
            folder.Append(statusEntry);
        } else if (type == ActionType::Closing) {
            folder.Clear();
        }

        return true;
    }

    // This function is called after the game starts executing and CTRPF is ready.
    int     main(void)
    {
        // If the launcher was used, handle any required cleanup
        if (memcmp(pluginHeader->config, AXIOM_PLUGIN_MAGIC, 4) == 0) {
            if (pluginHeader->config[1] == AXIOM_PLUGIN_VERSION) {
                plgLdrInit();
                bool prevPluginState = pluginHeader->config[2] != 0;
                PLGLDR__SetPluginLoaderState(prevPluginState);
                PLGLDR__ClearPluginLoadParameters();
                plgLdrExit();
            } else {
                OSD::Notify("Plugin and launcher version mismatch!");
            }
        }

        PluginMenu* menu = new PluginMenu("Axiom Plugin", 1, 0, 0);
        menu->SynchronizeWithFrame(true);

        if (getPiaLoggerStatus() == PatternStatus::Active) {
            MenuFolder* piaPlayersFolder = new MenuFolder("Players in session (PIA)");
            piaPlayersFolder->OnAction = onPiaPlayersFolderAction;
            menu->Append(piaPlayersFolder);
        }


        MenuFolder* hokakuCTROptionsFolder = new MenuFolder("HokakuCTR options");
        hokakuCTROptionsFolder->OnAction = onHokakuCTROptionsFolderAction;
        menu->Append(hokakuCTROptionsFolder);

        MenuFolder* patchesOptionsFolder = new MenuFolder("Patches options");
        patchesOptionsFolder->OnAction = onPatchesOptionsFolderAction;
        menu->Append(patchesOptionsFolder);

        MenuFolder* piaLoggerOptionsFolder = new MenuFolder("PIA logger options");
        piaLoggerOptionsFolder->OnAction = onPiaLoggerOptionsFolderAction;
        menu->Append(piaLoggerOptionsFolder);

        menu->Run();

        finiHokakuCTR();
        finiPatches();

        // Exit plugin
        return (0);
    }
}
