#include <array>
#include <format>
#include <string>
#include <3ds.h>
#include <sys/stat.h>
#include "MainUI.hpp"
#include "../sysmodules/acta.hpp"
#include "../sysmodules/httpc.hpp"
#include "../plgldr.h"

const char *AXIOM_PLUGIN       = "/luma/plugins/axiom.3gx";
const char *AXIOM_PLUGIN_MAGIC = "AXOM";
constexpr u32 AXIOM_PLUGIN_VERSION = SYSTEM_VERSION(1, 0, 0);

Result retBNID      = 0;
u32    bnidAccountSlot = 0;
AccountId bnid      = {};


void loadAndPlayBGM(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    u32 size     = ftell(f);
    u32 dataSize = size - 44;
    fseek(f, 44, SEEK_SET);

    u8* buffer = (u8*)linearAlloc(dataSize);
    fread(buffer, 1, dataSize, f);
    fclose(f);

    static ndspWaveBuf waveBuf;
    memset(&waveBuf, 0, sizeof(ndspWaveBuf));
    waveBuf.data_vaddr = buffer;
    waveBuf.nsamples   = dataSize / 4;
    waveBuf.looping    = true;
    waveBuf.status     = NDSP_WBUF_FREE;
    DSP_FlushDataCache(buffer, dataSize);
    ndspChnSetRate(0, 16000.0f);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    ndspChnWaveBufAdd(0, &waveBuf);
}

void loadAndPlaySFX(const char* path) {
    if (ndspChnIsPlaying(1)) return;
    FILE* f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    u32 size     = ftell(f);
    u32 dataSize = size - 44;
    fseek(f, 44, SEEK_SET);

    static u8* buffer = nullptr;
    static ndspWaveBuf waveBuf;
    if (buffer) { linearFree(buffer); buffer = nullptr; }
    buffer = (u8*)linearAlloc(dataSize);
    if (!buffer) { fclose(f); return; }
    fread(buffer, 1, dataSize, f);
    fclose(f);

    memset(&waveBuf, 0, sizeof(ndspWaveBuf));
    waveBuf.data_vaddr = buffer;
    waveBuf.nsamples   = dataSize / 2;
    waveBuf.looping    = false;
    DSP_FlushDataCache(buffer, dataSize);
    ndspChnSetRate(1, 16000.0f);
    ndspChnSetFormat(1, NDSP_FORMAT_MONO_PCM16);
    ndspChnWaveBufAdd(1, &waveBuf);
}

Result MainUI::unloadAccount(MainStruct *mainStruct) {
    Result rc = 0;
    handleResult(ACTA_UnloadConsoleAccount(), mainStruct, "Unload ACT account");
    if (R_FAILED(rc)) return rc;

    handleResult(NDMU_EnterExclusiveState(NDM_EXCLUSIVE_STATE_LOCAL_COMMUNICATIONS),
                 mainStruct, "Enter exclusive state");
    if (R_FAILED(rc)) return rc;

    bool online;
    while (true) {
        handleResult(FRD_IsOnline(&online), mainStruct, "Online check");
        if (R_FAILED(rc)) return rc;
        if (!online) break;
    }

    handleResult(FRDA_UnloadLocalAccount(), mainStruct, "Unload friends account");
    return rc;
}

Result MainUI::switchAccounts(MainStruct *mainStruct, u8 friend_account_id) {
    Result rc = 0;
    handleResult(FRDA_LoadLocalAccount(friend_account_id), mainStruct, "Switch account");
    if (R_FAILED(rc)) return rc;

    u32 act_account_index = 0;
    handleResult(ACT_GetAccountIndexOfFriendAccountId(&act_account_index, friend_account_id),
                 mainStruct, "Get ACT account ID of friend account ID");
    if (R_FAILED(rc)) return rc;

    if (act_account_index == 0) {
        u32 account_count;
        handleResult(ACT_GetAccountCount(&account_count), mainStruct, "Get account count");
        if (R_FAILED(rc)) return rc;

        handleResult(ACTA_CreateConsoleAccount(), mainStruct, "Create ACT account");
        if (R_FAILED(rc)) return rc;

        act_account_index = account_count + 1;
        handleResult(ACTA_CommitConsoleAccount(act_account_index), mainStruct, "Commit ACT account");
        if (R_FAILED(rc)) return rc;
    }

    handleResult(ACTA_SetDefaultAccount(act_account_index), mainStruct, "Set default account");
    return rc;
}

Result MainUI::createAccount(MainStruct *mainStruct, u8 friend_account_id, NascEnvironment environmentId) {
    Result rc = 0;
    handleResult(FRDA_CreateLocalAccount(friend_account_id, static_cast<u8>(environmentId), 0, 1),
                 mainStruct, "Create account");
    if (R_FAILED(rc)) return rc;

    handleResult(switchAccounts(mainStruct, friend_account_id), mainStruct, "Switch account");
    if (R_FAILED(rc)) return rc;

    handleResult(ACTA_UnbindServerAccount(friend_account_id, true), mainStruct, "Reset account");
    return rc;
}

Result MainUI::switchAccountWithRetry(MainStruct* mainStruct, u8 friend_account_id, NascEnvironment environmentId) {
    u8 previousFriendId = (u8)mainStruct->currentAccount + 1;

    Result rc = unloadAccount(mainStruct);
    if (R_SUCCEEDED(rc)) {
        rc = switchAccounts(mainStruct, friend_account_id);
        if (R_FAILED(rc)) {
            memset(mainStruct->errorString, 0, sizeof(mainStruct->errorString));
            rc = createAccount(mainStruct, friend_account_id, environmentId);
        }
    }

    if (R_FAILED(rc) && previousFriendId != friend_account_id) {
        // Switch back to whatever account was active before, then retry once.
        memset(mainStruct->errorString, 0, sizeof(mainStruct->errorString));
        Result restoreRc = switchAccounts(mainStruct, previousFriendId);
        if (R_SUCCEEDED(restoreRc)) {
            memset(mainStruct->errorString, 0, sizeof(mainStruct->errorString));
            rc = unloadAccount(mainStruct);
            if (R_SUCCEEDED(rc)) {
                rc = switchAccounts(mainStruct, friend_account_id);
                if (R_FAILED(rc)) {
                    memset(mainStruct->errorString, 0, sizeof(mainStruct->errorString));
                    rc = createAccount(mainStruct, friend_account_id, environmentId);
                }
            }
        }
    }

    return rc;
}

Result MainUI::handleAzahar(u8 friend_account_id) {
    s64 emu_id = 0;
    svcGetSystemInfo(&emu_id, 0x20000, 0);

    if (emu_id != 2) {
        // This is not Azahar, nothing to do
        return 0;
    }

    std::array<std::string, 3> patterns = {{
        {"nintendowifi\\.net"},
        {"nintendo\\.net"},
        {"pokemon-gl\\.com"}
    }};

    Result res = httpcInit(0x1000);
    if (friend_account_id == 2 || friend_account_id == 4) {
        const std::string replacement = friend_account_id == 2 ? "pretendo.cc" : "brewtendo.org";
        // Register Pretendo replacement URLs
        if (R_SUCCEEDED(res)) {
            for (const auto& pattern : patterns) {
                res = HTTPC_Azahar_RegisterURLReplacement(pattern.c_str(), replacement.c_str(), pattern.size(), replacement.size());
                if (R_FAILED(res)) {
                    // If the rule already exists clear it and register again
                    if (res == MAKERESULT(RL_STATUS, RS_INVALIDARG, RM_HTTP, RD_ALREADY_EXISTS)) {
                        res = HTTPC_Azahar_UnregisterURLReplacement(pattern.c_str(), pattern.size());
                        if (R_FAILED(res)) {
                            break;
                        }
                        res = HTTPC_Azahar_RegisterURLReplacement(pattern.c_str(), replacement.c_str(), pattern.size(), replacement.size());
                        if (R_FAILED(res)) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
            }
        }
    } else {
        // Unregister Pretendo replacement URLs
        for (const auto& pattern : patterns) {
            res = HTTPC_Azahar_UnregisterURLReplacement(pattern.c_str(), pattern.size());
            if (R_FAILED(res)) {
                break;
            } else {
                // Result may have a non-failure info value indicating that the pattern
                // was not registered, clear it.
                res = 0;
            }
        }
    }

    if (res == MAKERESULT(RL_PERMANENT, RS_WRONGARG, RM_OS, 47)) {
        // LLE HTTP was enabled, so custom command didn't exist
        // the HTTP patches will be used instead, so ignore error.
        res = 0;
    }

    httpcExit();
    return res;
}

void MainUI::migrateAccount(MainStruct *mainStruct) {
    Result rc = 0;
    u32 pretendo_account_index = 0;
    handleResult(ACT_GetAccountIndexOfFriendAccountId(&pretendo_account_index, 2),
                 mainStruct, "Get BNID for migration");
    if (pretendo_account_index != 0) {
        bool is_commited = false;
        handleResult(ACT_GetAccountInfo(&is_commited, sizeof(bool), pretendo_account_index,
                     INFO_TYPE_IS_COMMITTED), mainStruct, "Get BNID commit status");
        if (!is_commited)
            handleResult(ACTA_CommitConsoleAccount(pretendo_account_index), mainStruct, "Commit BNID");
    }
}

void MainUI::unlinkBNID(MainStruct *mainStruct) {
    if (R_FAILED(retBNID = ACTA_UnbindServerAccount(bnidAccountSlot, true))) {
        LOG_AXIOM_ERROR(mainStruct,
            std::format("ACTA_UnbindServerAccount failed with error code {}!", retBNID).c_str());
    } else {
        LOG_AXIOM_ERROR(mainStruct, "Successfully unlinked BNID!");
        loadAndPlaySFX("romfs:/sfx/MES_INFO.wav");
    }
}

void MainUI::launchPlugin(MainStruct *mainStruct) {
    Result rc = 0;
    PluginLoadParameters plgparam = { 0 };
    bool isPlgEnabled = false;

    plgparam.noFlash              = false;
    plgparam.pluginMemoryStrategy = PLG_STRATEGY_SWAP;
    plgparam.persistent           = 1;
    plgparam.lowTitleId           = 0;
    strcpy(plgparam.path, AXIOM_PLUGIN);
    strcpy(reinterpret_cast<char*>(plgparam.config), AXIOM_PLUGIN_MAGIC);
    plgparam.config[1] = AXIOM_PLUGIN_VERSION;

    handleResult(plgLdrInit(), mainStruct, "Initialize plg:ldr");
    if (R_FAILED(rc)) return;

    u32 version;
    handleResult(PLGLDR__GetVersion(&version), mainStruct, "Get plg:ldr version");
    if (R_FAILED(rc)) { plgLdrExit(); return; }

    if (version < SYSTEM_VERSION(1, 0, 2)) {
        LOG_AXIOM_ERROR(mainStruct, "Unsupported plg:ldr version, please update Luma3DS");
        plgLdrExit();
        return;
    }

    handleResult(PLGLDR__IsPluginLoaderEnabled(&isPlgEnabled), mainStruct, "Get plugin loader state");
    if (R_FAILED(rc)) { plgLdrExit(); return; }
    plgparam.config[2] = isPlgEnabled;

    handleResult(PLGLDR__SetPluginLoaderState(true), mainStruct, "Enable plugin loader");
    if (R_FAILED(rc)) { plgLdrExit(); return; }

    handleResult(PLGLDR__SetPluginLoadParameters(&plgparam), mainStruct, "Set plugin load params");
    plgLdrExit();

    LOG_AXIOM_ERROR(mainStruct, "Axiom plugin ready! Launch a game from the Home Menu");
    loadAndPlaySFX("romfs:/sfx/SFX_PUGIN_START.wav");
}

void MainUI::openPrompt(MainStruct* mainStruct, const std::string& message, PromptStatus promptStatus) {
    mainStruct->prompt.active  = true;
    mainStruct->prompt.message = message;
    mainStruct->prompt.result  = PromptResult::None;
    mainStruct->prompt.status  = promptStatus;
}

void MainUI::updatePrompt(MainStruct* mainStruct, u32 kDown) {
    if (!mainStruct->prompt.active) return;
    if      (kDown & KEY_A) mainStruct->prompt.result = PromptResult::Yes;
    else if (kDown & KEY_B) mainStruct->prompt.result = PromptResult::No;
}

void MainUI::drawPrompt(MainStruct* mainStruct) {
    if (!mainStruct->prompt.active) return;

    const float screenW = 320.0f, screenH = 240.0f;
    C2D_DrawRectSolid(0, 0, 0.1f, screenW, screenH, C2D_Color32(0, 0, 0, 140));

    const float boxW = 280.0f, boxH = 110.0f;
    const float boxX = (screenW - boxW) / 2.0f;
    const float boxY = (screenH - boxH) / 2.0f;

    const u32 fill   = C2D_Color32(0x75, 0x6C, 0x48, 0xFF);
    const u32 border = C2D_Color32(108, 98, 64, 255);
    const u32 white  = C2D_Color32(255, 255, 255, 255);

    C2D_DrawRectSolid(boxX, boxY,               0.2f, boxW, boxH, fill);
    C2D_DrawRectSolid(boxX, boxY,               0.3f, boxW, 2,    border);
    C2D_DrawRectSolid(boxX, boxY + boxH - 2.0f, 0.3f, boxW, 2,    border);
    C2D_DrawRectSolid(boxX, boxY,               0.3f, 2,    boxH, border);
    C2D_DrawRectSolid(boxX + boxW - 2.0f, boxY, 0.3f, 2,    boxH, border);

    C2D_Text msgText, hintText;
    C2D_TextBufClear(textBuf);
    C2D_TextFontParse(&msgText,  font, textBuf, mainStruct->prompt.message.c_str());
    C2D_TextOptimize(&msgText);
    C2D_TextFontParse(&hintText, font, textBuf, "A: Yes    B: Cancel");
    C2D_TextOptimize(&hintText);

    C2D_DrawText(&msgText,  C2D_WithColor | C2D_WordWrap,    boxX + 10.0f, boxY + 10.0f,       0.4f, 0.55f, 0.55f, white, boxW - 20.0f);
    C2D_DrawText(&hintText, C2D_WithColor | C2D_AlignCenter, screenW / 2.0f, boxY + boxH - 18.0f, 0.4f, 0.5f,  0.5f,  white);
}

bool MainUI::drawUI(MainStruct *mainStruct, C3D_RenderTarget* top_screen,
                    C3D_RenderTarget* bottom_screen, u32 kDown, u32 kHeld, touchPosition touch)
{
    if (!mainStruct->musicStarted) {
        loadAndPlayBGM("romfs:/bgm/AXIOM_MAIN_BGM.wav");
        mainStruct->musicStarted = true;
    }

    if (!mainStruct->updateChecked) {
        mainStruct->updateChecked = true;
        if (auto* updateCheck = std::fopen(AXIOM_UPDATE_PATH "/update.txt", "rb")) {
            std::fclose(updateCheck);
            migrateAccount(mainStruct);
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
                std::rename(AXIOM_UPDATE_PATH "/axiom.3gx", "/luma/plugins/axiom.3gx");

                std::remove("/3ds/bver-prod.pem");
                std::rename(AXIOM_UPDATE_PATH "/bver-prod.pem", "/3ds/bver-prod.pem");
                std::remove("/3ds/juxt-prod.pem");
                std::rename(AXIOM_UPDATE_PATH "/juxt-prod.pem", "/3ds/juxt-prod.pem");

                std::remove(AXIOM_UPDATE_PATH "/update.txt");
            }
            LOG_AXIOM_ERROR(mainStruct, "Axiom has been updated!\n\nPress start to reboot.");
            loadAndPlaySFX("romfs:/sfx/MES_INFO.wav");
            aptSetHomeAllowed(false);
            mainStruct->needsReboot     = true;
            mainStruct->buttonWasPressed = false;
            return false;
        }
    }

    if (kDown & KEY_START) loadAndPlaySFX("romfs:/sfx/HOME_OPEN.wav");
    if (kDown & KEY_START) return true;

    updatePrompt(mainStruct, kDown);
    if (mainStruct->prompt.active) {
        if (mainStruct->prompt.result == PromptResult::Yes) {
            PromptStatus status        = mainStruct->prompt.status;
            mainStruct->prompt.result  = PromptResult::None;
            mainStruct->prompt.active  = false;
            switch (status) {
                case PromptStatus::BNIDUnlink:
                    unlinkBNID(mainStruct);
                    break;
                default:
                    LOG_AXIOM_ERROR(mainStruct, "Unknown prompt called.");
                    break;
            }
            return false;
        }
        if (mainStruct->prompt.result == PromptResult::No) {
            mainStruct->prompt.result = PromptResult::None;
            mainStruct->prompt.active = false;
            return false;
        }
    }

    C2D_SceneBegin(top_screen);
    C2D_DrawSprite(&mainStruct->top);
    DrawVersionString();
    DrawLicenseString();


    if (mainStruct->errorString[0] != 0) {
        DrawString(0.5f, 0xFF000000, mainStruct->errorString, 0);
    }

    C2D_SceneBegin(bottom_screen);
    C2D_DrawSprite(&mainStruct->bottom);
    if (!mainStruct->prompt.active) DrawControls();

    if (mainStruct->buttonSelected == NascEnvironment::NASC_ENV_Prod) {
        if (mainStruct->currentAccount == NascEnvironment::NASC_ENV_Prod) {
            C2D_DrawSprite(&mainStruct->nintendo_loaded_selected);
            C2D_DrawSprite(&mainStruct->brewtendo_unloaded_deselected);
        } else if (mainStruct->currentAccount == NascEnvironment::NASC_ENV_Dev) {
            C2D_DrawSprite(&mainStruct->nintendo_unloaded_selected);
            C2D_DrawSprite(&mainStruct->brewtendo_loaded_deselected);
        } else {
            C2D_DrawSprite(&mainStruct->nintendo_unloaded_selected);
            C2D_DrawSprite(&mainStruct->brewtendo_unloaded_deselected);
        }
    } else if (mainStruct->buttonSelected == NascEnvironment::NASC_ENV_Dev) {
        if (mainStruct->currentAccount == NascEnvironment::NASC_ENV_Dev) {
            C2D_DrawSprite(&mainStruct->nintendo_unloaded_deselected);
            C2D_DrawSprite(&mainStruct->brewtendo_loaded_selected);
        } else if (mainStruct->currentAccount == NascEnvironment::NASC_ENV_Prod) {
            C2D_DrawSprite(&mainStruct->nintendo_loaded_deselected);
            C2D_DrawSprite(&mainStruct->brewtendo_unloaded_selected);
        } else {
            C2D_DrawSprite(&mainStruct->nintendo_unloaded_deselected);
            C2D_DrawSprite(&mainStruct->brewtendo_unloaded_selected);
        }
    } else {
        C2D_DrawSprite(&mainStruct->nintendo_unloaded_deselected);
        C2D_DrawSprite(&mainStruct->brewtendo_unloaded_deselected);
    }
    C2D_DrawSprite(&mainStruct->header);
    drawPrompt(mainStruct);

    if (!mainStruct->needsReboot && !mainStruct->prompt.active) {
        if (kDown & KEY_TOUCH) {
            if ((touch.px >= 165 && touch.px <= 269) && (touch.py >= 59 && touch.py <= 172)) {
                mainStruct->buttonSelected  = NascEnvironment::NASC_ENV_Prod;
                mainStruct->buttonWasPressed = true;
                loadAndPlaySFX("romfs:/sfx/ACC_TAP.wav");
            } else if ((touch.px >= 49 && touch.px <= 153) && (touch.py >= 59 && touch.py <= 172)) {
                mainStruct->buttonSelected  = NascEnvironment::NASC_ENV_Dev;
                mainStruct->buttonWasPressed = true;
                loadAndPlaySFX("romfs:/sfx/ACC_TAP.wav");
            }
        } else if (kDown & KEY_LEFT || kDown & KEY_RIGHT) {
            mainStruct->buttonSelected =
                mainStruct->buttonSelected == NascEnvironment::NASC_ENV_Dev
                    ? NascEnvironment::NASC_ENV_Prod
                    : NascEnvironment::NASC_ENV_Dev;
            loadAndPlaySFX("romfs:/sfx/ACC_SELECT.wav");
        }

        if (mainStruct->prompt.active) return false;

        if (kDown & KEY_A) {
            loadAndPlaySFX("romfs:/sfx/ACC_START.wav");
            mainStruct->buttonWasPressed = true;
        }

        if (kDown & KEY_L) {
            loadAndPlaySFX("romfs:/sfx/ACC_TAP.wav");

            if (mainStruct->currentAccount != NascEnvironment::NASC_ENV_Test) {
                Result rc = MainUI::switchAccountWithRetry(mainStruct, 2, NascEnvironment::NASC_ENV_Test);

                if (R_SUCCEEDED(rc)) {
                    Result azaharRc = handleAzahar(2);
                    if (R_FAILED(azaharRc))
                        LOG_AXIOM_ERROR(mainStruct, std::format("Failed to apply Azahar configuration: {}", azaharRc).c_str());
                }

                if (R_FAILED(rc)) {
                    ensureRebootPrompt(mainStruct);
                    aptSetHomeAllowed(false);
                    mainStruct->needsReboot = true;
                    return false;
                }

                mainStruct->needsReboot = true;
            }
            return true;
        }

        if (kDown & KEY_X) {
            if (R_SUCCEEDED(retBNID)) {
                if (R_FAILED(retBNID = ACT_GetAccountIndexOfFriendAccountId(&bnidAccountSlot, 3)))
                    LOG_AXIOM_ERROR(mainStruct,
                        std::format("ACT_GetAccountIndexOfFriendAccountId failed: {}!", retBNID).c_str());
            }
            if (bnidAccountSlot == 0) {
                LOG_AXIOM_ERROR(mainStruct, "There is no BNID linked on this console!");
                loadAndPlaySFX("romfs:/sfx/MES_WARNING.wav");
            }
            if (R_SUCCEEDED(retBNID)) {
                if (R_FAILED(retBNID = ACT_GetAccountInfo(bnid, sizeof(bnid), bnidAccountSlot, INFO_TYPE_ACCOUNT_ID))) {
                    LOG_AXIOM_ERROR(mainStruct,
                        std::format("ACT_GetAccountInfo failed: {}!", retBNID).c_str());
                    loadAndPlaySFX("romfs:/sfx/MES_WARNING.wav");
                }
            }
            if (R_SUCCEEDED(retBNID)) {
                if (bnid[0] != '\0') {
                    openPrompt(mainStruct,
                        std::format("Are you sure you would like to unlink your BNID {}? "
                                    "Your BNID can be relinked at any time.", bnid),
                        PromptStatus::BNIDUnlink);
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
        mainStruct->errorString[0] = 0;

        if (mainStruct->currentAccount == mainStruct->buttonSelected) return true;

        u8 accountId = (u8)mainStruct->buttonSelected + 1;

        Result rc = MainUI::switchAccountWithRetry(mainStruct, accountId, mainStruct->buttonSelected);

        if (R_SUCCEEDED(rc)) {
            Result azaharRc = handleAzahar(accountId);
            if (R_FAILED(azaharRc))
                LOG_AXIOM_ERROR(mainStruct, std::format("Failed to apply Azahar configuration: {}", azaharRc).c_str());
        }

        if (R_FAILED(rc)) {
            ensureRebootPrompt(mainStruct);
            aptSetHomeAllowed(false);
            mainStruct->needsReboot      = true;
            mainStruct->buttonWasPressed = false;
            return false;
        }

        mainStruct->needsReboot = true;
        return true;
    }

    return false;
}
