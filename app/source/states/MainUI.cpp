#include <format>
#include <3ds.h>
#include <sys/stat.h>
#include "MainUI.hpp"
#include "../sysmodules/acta.hpp"
#include "../plgldr.h"

constexpr Result ResultFPDLocalAccountNotExists = 0xC880C4ED; // FPD::LocalAccountNotExists
const char *AXIOM_PLUGIN = "/luma/plugins/axiom.3gx";
const char *AXIOM_PLUGIN_MAGIC = "AXOM";
constexpr u32 AXIOM_PLUGIN_VERSION = SYSTEM_VERSION(1, 0, 0);

Result retBNID = 0;
u32 bnidAccountSlot = 0;
AccountId bnid = {};

void loadAndPlayBGM(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    u32 size = ftell(f);
    
    // Skip the 44-byte WAV header
    u32 dataSize = size - 44;
    fseek(f, 44, SEEK_SET);

    // Allocate Linear Memory
    u8* buffer = (u8*)linearAlloc(dataSize);
    fread(buffer, 1, dataSize, f);
    fclose(f);

    static ndspWaveBuf waveBuf;
    memset(&waveBuf, 0, sizeof(ndspWaveBuf));
    waveBuf.data_vaddr = buffer;
    waveBuf.nsamples = dataSize / 4;
    waveBuf.looping = true;
    waveBuf.status = NDSP_WBUF_FREE;

    DSP_FlushDataCache(buffer, dataSize);
    
    ndspChnSetRate(0, 16000.0f);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

    ndspChnWaveBufAdd(0, &waveBuf);
}

void loadAndPlaySFX(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    u32 size = ftell(f);
    u32 dataSize = size - 44;
    fseek(f, 44, SEEK_SET);

    u8* buffer = (u8*)linearAlloc(dataSize);
    fread(buffer, 1, dataSize, f);
    fclose(f);

    static ndspWaveBuf waveBuf;
    memset(&waveBuf, 0, sizeof(ndspWaveBuf));
    waveBuf.data_vaddr = buffer;
    waveBuf.nsamples = dataSize / 2; 
    waveBuf.looping = false;
    waveBuf.status = NDSP_WBUF_FREE;

    DSP_FlushDataCache(buffer, dataSize);

    ndspChnSetRate(1, 16000.0f);
    ndspChnSetFormat(1, NDSP_FORMAT_MONO_PCM16); 
    ndspChnWaveBufAdd(1, &waveBuf);           
}

Result MainUI::unloadAccount(MainStruct *mainStruct) {
    Result rc = 0;

    handleResult(ACTA_UnloadConsoleAccount(), mainStruct, "Unload ACT account");
    if (R_FAILED(rc)) {
        return rc;
    }

    // In order to unload the friends account we need to make it go offline. Enter into exclusive state to disconnect from online
    handleResult(NDMU_EnterExclusiveState(NDM_EXCLUSIVE_STATE_LOCAL_COMMUNICATIONS), mainStruct, "Enter exclusive state");
    if (R_FAILED(rc)) {
        return rc;
    }

    bool online;
    // If the console was connected to online, wait until it disconnects.
    // I tried doing this through notification events but it didn't seem to work
    while (true) {
        handleResult(FRD_IsOnline(&online), mainStruct, "Online check");
        if (R_FAILED(rc)) {
            return rc;
        }

        if (!online) break;
    }

    // Now that the console is offline, unload the account
    handleResult(FRDA_UnloadLocalAccount(), mainStruct, "Unload friends account");

    return rc;
}

Result MainUI::switchAccounts(MainStruct *mainStruct, u8 friend_account_id) {
    Result rc = 0;

    handleResult(FRDA_LoadLocalAccount(friend_account_id), mainStruct, "Switch account");
    if (R_FAILED(rc)) {
        return rc;
    }

    u32 act_account_index = 0;
    handleResult(ACT_GetAccountIndexOfFriendAccountId(&act_account_index, friend_account_id), mainStruct, "Get ACT account ID of friend account ID");
    if (R_FAILED(rc)) {
        return rc;
    }

    if (act_account_index == 0) {
        u32 account_count;
        handleResult(ACT_GetAccountCount(&account_count), mainStruct, "Get account count");
        if (R_FAILED(rc)) {
            return rc;
        }

        handleResult(ACTA_CreateConsoleAccount(), mainStruct, "Create ACT account");
        if (R_FAILED(rc)) {
            return rc;
        }

        act_account_index = account_count + 1;

        handleResult(ACTA_CommitConsoleAccount(act_account_index), mainStruct, "Commit ACT account");
        if (R_FAILED(rc)) {
            return rc;
        }
    }

    handleResult(ACTA_SetDefaultAccount(act_account_index), mainStruct, "Set default account");

    return rc;
}

Result MainUI::createAccount(MainStruct *mainStruct, u8 friend_account_id, NascEnvironment environmentId) {
    Result rc = 0;

    // (Re)Create the friend account
    handleResult(FRDA_CreateLocalAccount(friend_account_id, static_cast<u8>(environmentId), 0, 1), mainStruct, "Create account");
    if (R_FAILED(rc)) {
        return rc;
    }

    // Switch to the friend/act accounts
    handleResult(switchAccounts(mainStruct, friend_account_id), mainStruct, "Switch account");
    if (R_FAILED(rc)) {
        return rc;
    }

    // Reset the act account
    handleResult(ACTA_UnbindServerAccount(friend_account_id, true), mainStruct, "Reset account");

    return rc;
}

void MainUI::migrateAccount(MainStruct *mainStruct) {
    Result rc = 0;
    u32 pretendo_account_index = 0;
    // Logs won't override any previous errors
    handleResult(ACT_GetAccountIndexOfFriendAccountId(&pretendo_account_index, 2), mainStruct, "Get BNID for migration");
    if (pretendo_account_index != 0) {
        bool is_commited = false;
        handleResult(ACT_GetAccountInfo(&is_commited, sizeof(bool), pretendo_account_index, INFO_TYPE_IS_COMMITTED), mainStruct, "Get BNID commit status");
        if (!is_commited) {
            handleResult(ACTA_CommitConsoleAccount(pretendo_account_index), mainStruct, "Commit BNID");
        }
    }
}

void MainUI::unlinkBNID(MainStruct *mainStruct) {
    if (R_FAILED(retBNID = ACTA_UnbindServerAccount(bnidAccountSlot, true))) {
        LOG_AXIOM_ERROR(mainStruct, std::format("ACTA_UnbindServerAccount failed with error code {}!", retBNID).c_str());
	} else {
		LOG_AXIOM_ERROR(mainStruct, "Successfully unlinked BNID!");
	}
}

// Borrowed from https://github.com/azahar-emu/ArticBaseServer/blob/d93be050a4787ed602c603aa14bbaaee066dc1d9/app/sources/main.cpp#L79
void MainUI::launchPlugin(MainStruct *mainStruct) {
    Result rc = 0;
    PluginLoadParameters plgparam = { 0 };
    bool isPlgEnabled = false;

    // Ideally we wouldn't have to set the persistent flag and instead use the builtin
    // functionality, but the Old 3DS does a reboot before opening a Mode3 game,
    // erasing any non-persistent configurations we use since this as a generic launcher.
    //
    // Instead, we have to mark the parameters as persistent and then disable them
    // from the plugin itself
    plgparam.noFlash = false;
    plgparam.pluginMemoryStrategy = PLG_STRATEGY_SWAP;
    plgparam.persistent = 1;
    plgparam.lowTitleId = 0;
    strcpy(plgparam.path, AXIOM_PLUGIN);

    // Use custom header on config as a way to differentiate between plugin load from launcher
    // and load by the user (by saving the plugin file as default or on a specific game)
    strcpy(reinterpret_cast<char*>(plgparam.config), AXIOM_PLUGIN_MAGIC);
    plgparam.config[1] = AXIOM_PLUGIN_VERSION;

    handleResult(plgLdrInit(), mainStruct, "Initialize plg:ldr");
    if (R_FAILED(rc)) {
        return;
    }

    u32 version;
    handleResult(PLGLDR__GetVersion(&version), mainStruct, "Get plg:ldr version");
    if (R_FAILED(rc)) {
        plgLdrExit();
        return;
    }

    if (version < SYSTEM_VERSION(1, 0, 2)) {
        LOG_AXIOM_ERROR(mainStruct, "Unsupported plg:ldr version, please update Luma3DS");
        plgLdrExit();
        return;
    }

    // Save the previous plugin loader state
    handleResult(PLGLDR__IsPluginLoaderEnabled(&isPlgEnabled), mainStruct, "Get plugin loader state");
    if (R_FAILED(rc)) {
        plgLdrExit();
        return;
    }
    plgparam.config[2] = isPlgEnabled;

    handleResult(PLGLDR__SetPluginLoaderState(true), mainStruct, "Enable plugin loader");
    if (R_FAILED(rc)) {
        plgLdrExit();
        return;
    }

    handleResult(PLGLDR__SetPluginLoadParameters(&plgparam), mainStruct, "Set plugin load params");
    plgLdrExit();

    // Logs won't override any previous errors
    LOG_AXIOM_ERROR(mainStruct, "Axiom plugin ready! Launch a game from the Home Menu");
    return;
}

void MainUI::openPrompt(MainStruct* mainStruct, const std::string& message, PromptStatus promptStatus)
{
    mainStruct->prompt.active = true;
    mainStruct->prompt.message = message;
    mainStruct->prompt.result = PromptResult::None;
    mainStruct->prompt.status = promptStatus;
}

void MainUI::updatePrompt(MainStruct* mainStruct, u32 kDown)
{
    if (!mainStruct->prompt.active)
        return;

    if (kDown & KEY_A) {
        mainStruct->prompt.result = PromptResult::Yes;
    }
    else if (kDown & KEY_B) {
        mainStruct->prompt.result = PromptResult::No;
    }
}

void MainUI::drawPrompt(MainStruct* mainStruct)
{
    if (!mainStruct->prompt.active) return;

    const float screenW = 320.0f;
    const float screenH = 240.0f;

    // Dim background
    C2D_DrawRectSolid(0, 0, 0.1f, screenW, screenH, C2D_Color32(0, 0, 0, 140));

    const float boxW = 280.0f, boxH = 110.0f;
    const float boxX = (screenW - boxW) / 2.0f;
    const float boxY = (screenH - boxH) / 2.0f;

    const u32 fill   = C2D_Color32(25, 25, 25, 240);
    const u32 border = C2D_Color32(255, 255, 255, 255);
    const u32 white  = C2D_Color32(0x75, 0x6C, 0x48, 0xFF);

    // Box + border
    C2D_DrawRectSolid(boxX, boxY, 0.2f, boxW, boxH, fill);
    C2D_DrawRectSolid(boxX, boxY,               0.3f, boxW, 2, border);
    C2D_DrawRectSolid(boxX, boxY + boxH - 2.0f, 0.3f, boxW, 2, border);
    C2D_DrawRectSolid(boxX, boxY,               0.3f, 2, boxH, border);
    C2D_DrawRectSolid(boxX + boxW - 2.0f, boxY, 0.3f, 2, boxH, border);

    // Parse both strings in one buffer
    C2D_Text msgText, hintText;
    C2D_TextBufClear(textBuf);

    C2D_TextFontParse(&msgText, font, textBuf, mainStruct->prompt.message.c_str());
    C2D_TextOptimize(&msgText);

    C2D_TextFontParse(&hintText, font, textBuf, "A: Yes    B: Cancel");
    C2D_TextOptimize(&hintText);

    // Draw (z <= 1.0!!)
    C2D_DrawText(&msgText,
                 C2D_WithColor | C2D_WordWrap,
                 boxX + 10.0f, boxY + 10.0f, 0.4f,
                 0.55f, 0.55f, white,
                 boxW - 20.0f);

    C2D_DrawText(&hintText,
                 C2D_WithColor | C2D_AlignCenter,
                 screenW / 2.0f, boxY + boxH - 18.0f, 0.4f,
                 0.5f, 0.5f, white);
}

bool MainUI::drawUI(MainStruct *mainStruct, C3D_RenderTarget* top_screen, C3D_RenderTarget* bottom_screen, u32 kDown, u32 kHeld, touchPosition touch)
{
    
        // One-time BGM Setup
        if (!mainStruct->musicStarted) {
            // Load and play BGM
            //"TEMP" loadAndPlayBGM("romfs:/bgm/AXIOM_MAIN_BGM.wav");
            mainStruct->musicStarted = true;
        }
    
    // Check if Axiom has been updated
    if (!mainStruct->updateChecked) {
        mainStruct->updateChecked = true;
        if (auto* updateCheck = std::fopen(AXIOM_UPDATE_PATH "/update.txt", "rb")) {
            std::fclose(updateCheck);

            migrateAccount(mainStruct);

            // If the migration has failed, don't do the update. In such case users need to contact support
            if (mainStruct->errorString[0] == 0) {
                mkdir("/luma", 0777);
                mkdir("/luma/sysmodules", 0777);
                std::remove("/luma/sysmodules/0004013000003202.ips");
                std::rename(AXIOM_UPDATE_PATH "/0004013000003202.ips", "/luma/sysmodules/0004013000003202.ips");
                std::remove("/luma/sysmodules/0004013000003802.ips");
                std::rename(AXIOM_UPDATE_PATH "/0004013000003802.ips", "/luma/sysmodules/0004013000003802.ips");
                std::remove("/luma/sysmodules/0004013000002902.ips");
                std::rename(AXIOM_UPDATE_PATH "/0004013000002902.ips", "/luma/sysmodules/0004013000002902.ips");
                std::remove("/luma/sysmodules/0004013000002E02.ips");
                std::rename(AXIOM_UPDATE_PATH "/0004013000002E02.ips", "/luma/sysmodules/0004013000002E02.ips");
                std::remove("/luma/sysmodules/0004013000002F02.ips");
                std::rename(AXIOM_UPDATE_PATH "/0004013000002F02.ips", "/luma/sysmodules/0004013000002F02.ips");

                mkdir("/luma/titles", 0777);
                mkdir("/luma/titles/000400300000BC02", 0777);
                std::remove("/luma/titles/000400300000BC02/code.ips");
                std::rename(AXIOM_UPDATE_PATH "/000400300000BC02.ips", "/luma/titles/000400300000BC02/code.ips");

                mkdir("/luma/titles/000400300000BD02", 0777);
                std::remove("/luma/titles/000400300000BD02/code.ips");
                std::rename(AXIOM_UPDATE_PATH "/000400300000BD02.ips", "/luma/titles/000400300000BD02/code.ips");

                mkdir("/luma/titles/000400300000BE02", 0777);
                std::remove("/luma/titles/000400300000BE02/code.ips");
                std::rename(AXIOM_UPDATE_PATH "/000400300000BE02.ips", "/luma/titles/000400300000BE02/code.ips");

                mkdir("/luma/plugins", 0777);
                std::remove("/luma/plugins/axiom.3gx");
                std::rename(AXIOM_UPDATE_PATH "/axiom.3gx",           "/luma/plugins/axiom.3gx");

                std::remove("/3ds/bver-prod.pem");
                std::rename(AXIOM_UPDATE_PATH "/bver-prod.pem",        "/3ds/bver-prod.pem");

                std::remove(AXIOM_UPDATE_PATH "/update.txt");
            }

            // Logs won't override any previous errors
            LOG_AXIOM_ERROR(mainStruct, "Axiom has been updated!");

            aptSetHomeAllowed(false);
            mainStruct->needsReboot = true;
            mainStruct->buttonWasPressed = false;
            return false;
        }
    }

    // if start is pressed, exit to hbl/the home menu depending on if the app was launched from cia or 3dsx
    if (kDown & KEY_START) return true;
    
    updatePrompt(mainStruct, kDown);

    if (mainStruct->prompt.active) {
        if (mainStruct->prompt.result == PromptResult::Yes) {
            switch (mainStruct->prompt.status) {
                case PromptStatus::BNIDUnlink:
                    unlinkBNID(mainStruct);
                    break;
                default:
                    LOG_AXIOM_ERROR(mainStruct, "Unknown prompt called.");
                    break;
            }
            mainStruct->prompt.result = PromptResult::None;
            mainStruct->prompt.active = false;
            return false;
        }
        if (mainStruct->prompt.result == PromptResult::No) {
            mainStruct->prompt.result = PromptResult::None;
            mainStruct->prompt.active = false;
            return false;
        }
    }

    C2D_SceneBegin(top_screen);
    DrawVersionString();
    C2D_DrawSprite(&mainStruct->top);

    if (mainStruct->errorString[0] != 0) {
        DrawString(0.5f, 0xFFFFFFFF, std::format("{}{}", mainStruct->errorString, mainStruct->needsReboot ? "\n\nPress START to reboot the system" : ""), 0);
    }

    C2D_SceneBegin(bottom_screen);
    C2D_DrawSprite(&mainStruct->bottom);
    DrawControls();

    if (mainStruct->buttonSelected == NascEnvironment::NASC_ENV_Prod) {
        if (mainStruct->currentAccount == NascEnvironment::NASC_ENV_Prod) {
            C2D_DrawSprite(&mainStruct->nintendo_loaded_selected);
            C2D_DrawSprite(&mainStruct->brewtendo_unloaded_deselected);
        }
        else {
            C2D_DrawSprite(&mainStruct->nintendo_unloaded_selected);
            C2D_DrawSprite(&mainStruct->brewtendo_loaded_deselected);
        }
    }
    else if (mainStruct->buttonSelected == NascEnvironment::NASC_ENV_Dev) {
        if (mainStruct->currentAccount == NascEnvironment::NASC_ENV_Dev) {
            C2D_DrawSprite(&mainStruct->nintendo_unloaded_deselected);
            C2D_DrawSprite(&mainStruct->brewtendo_loaded_selected);
        }
        else {
            C2D_DrawSprite(&mainStruct->nintendo_loaded_deselected);
            C2D_DrawSprite(&mainStruct->brewtendo_unloaded_selected);
        }
    }
    C2D_DrawSprite(&mainStruct->header);
    drawPrompt(mainStruct);

    // Only allow user interaction when the system doesn't need a restart
    if (!mainStruct->needsReboot && !mainStruct->prompt.active) {
        // handle touch input
        if (kDown & KEY_TOUCH) {
            if ((touch.px >= 165 && touch.px <= 165 + 104) && (touch.py >= 59 && touch.py <= 59 + 113)) {
                mainStruct->buttonSelected = NascEnvironment::NASC_ENV_Prod;
                mainStruct->buttonWasPressed = true;
                loadAndPlaySFX("romfs:/sfx/ACC_TAP.wav");
            }
            else if ((touch.px >= 49 && touch.px <= 49 + 104) && (touch.py >= 59 && touch.py <= 59 + 113)) {
                mainStruct->buttonSelected = NascEnvironment::NASC_ENV_Dev;
                mainStruct->buttonWasPressed = true;
                loadAndPlaySFX("romfs:/sfx/ACC_TAP.wav");
            }
        }
        else if (kDown & KEY_LEFT || kDown & KEY_RIGHT) {
            mainStruct->buttonSelected = mainStruct->buttonSelected == NascEnvironment::NASC_ENV_Dev ? NascEnvironment::NASC_ENV_Prod : NascEnvironment::NASC_ENV_Dev;
            loadAndPlaySFX("romfs:/sfx/ACC_SELECT.wav");
        }

        if (mainStruct->prompt.active) {
            return false;
        }

        if (kDown & KEY_A) {
            loadAndPlaySFX("romfs:/sfx/ACC_START.wav");
            mainStruct->buttonWasPressed = true;
        }

        if (kDown & KEY_X) {
            // We need to confirm we actually even have a linked BNID.
	        if (R_SUCCEEDED(retBNID)) {
		        if (R_FAILED(retBNID = ACT_GetAccountIndexOfFriendAccountId(&bnidAccountSlot, 2))) {
			        LOG_AXIOM_ERROR(mainStruct, std::format("ACT_GetAccountIndexOfFriendAccountId failed with error code {}!", retBNID).c_str());
		        }
	        }

            if (bnidAccountSlot == 0) {
                LOG_AXIOM_ERROR(mainStruct, "There is no BNID linked on this console!");
                loadAndPlaySFX("romfs:/sfx/MES_WARNING.wav");
            }

	        if (R_SUCCEEDED(retBNID)) {
		        if (R_FAILED(retBNID = ACT_GetAccountInfo(bnid, sizeof(bnid), bnidAccountSlot, INFO_TYPE_ACCOUNT_ID))) {
			        LOG_AXIOM_ERROR(mainStruct, std::format("ACT_GetAccountInfo failed with error code {}!", retBNID).c_str());
                    loadAndPlaySFX("romfs:/sfx/MES_WARNING.wav");
		        }
	        }

            if (R_SUCCEEDED(retBNID)) {
		        if (bnid[0] != '\0') {
			        openPrompt(mainStruct, std::format("Are you sure you would like to unlink your BNID {}? Your BNID can be relinked at any time.", bnid), PromptStatus::BNIDUnlink);
		        } else {
			        LOG_AXIOM_ERROR(mainStruct, "There is no BNID linked on this console!");
                    loadAndPlaySFX("romfs:/sfx/MES_WARNING.wav");
		        }
	        }
        }

        if (kDown & KEY_Y) {
            launchPlugin(mainStruct);
            mainStruct->buttonWasPressed = false;
            return false;
        }
    }

    if (mainStruct->buttonWasPressed) {
        // Clear any previous logs
        mainStruct->errorString[0] = 0;

        // If the chosen account is the one we are already logged into, exit without rebooting
        if (mainStruct->currentAccount == mainStruct->buttonSelected) return true;

        u8 accountId = (u8)mainStruct->buttonSelected + 1; // by default set accountId to nasc environment + 1

        Result rc = unloadAccount(mainStruct);
        if (R_SUCCEEDED(rc)) {
            rc = switchAccounts(mainStruct, accountId);
            if (rc == ResultFPDLocalAccountNotExists && accountId == 3) {
                // Clear the error to allow createAccount to override it
                memset(mainStruct->errorString, 0, 256);
                rc = createAccount(mainStruct, accountId, NascEnvironment::NASC_ENV_Dev);
            }
        }

        if (R_FAILED(rc)) {
            aptSetHomeAllowed(false);
            mainStruct->needsReboot = true;
            mainStruct->buttonWasPressed = false;
            return false;
        }

        mainStruct->needsReboot = true;

        return true;
    }

    return false;
}