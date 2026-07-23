#include "LumaValidation.hpp"
#include "../patchswap.hpp"
#include "../sysmodules/acta.hpp"
#include <format>

namespace MainUI {
    Result unloadAccount(MainStruct* mainStruct);
    Result switchAccounts(MainStruct* mainStruct, u8 friend_account_id);
    Result createAccount(MainStruct* mainStruct, u8 friend_account_id, NascEnvironment environmentId);
    Result switchAccountWithRetry(MainStruct* mainStruct, u8 friend_account_id, NascEnvironment environmentId);
    void   openPrompt(MainStruct* mainStruct, const std::string& message, PromptStatus promptStatus);
    void   updatePrompt(MainStruct* mainStruct, u32 kDown);
    void   drawPrompt(MainStruct* mainStruct);
}


void PlayBGM(const char* path) {
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

void PlaySFX(const char* path) {
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

static void doSwitchBackToBrewtendo(MainStruct* mainStruct) {
    bool spaceOK = PatchSwap::CheckFreeSpace(mainStruct);
    if (!spaceOK) {
        mainStruct->swapStatusMsg = std::format(
            "Warning: only {} MB free. Proceeding anyway...",
            mainStruct->sdFreeBytes / (1024 * 1024));
    }

    bool ok = PatchSwap::SwitchToBrewtendo(mainStruct);

    if (!ok && mainStruct->swapPhase == SwapPhase::Failed) {
        if (mainStruct->errorString[0] == 0 && !mainStruct->swapStatusMsg.empty())
            snprintf(mainStruct->errorString, sizeof(mainStruct->errorString),
                "%s", mainStruct->swapStatusMsg.c_str());
        ensureRebootPrompt(mainStruct);
        char testPath[256];
        snprintf(testPath, sizeof(testPath), "%s/%s", AXIOM_TEMP_PATH,
                 AXIOM_PATCH_FILES[0].filename);
        FILE* t = fopen(testPath, "rb");
        if (!t) {
            MainUI::openPrompt(mainStruct,
                std::format(
                    "CDN is unreachable.\n"
                    "Fall back to local backup?\n"
                    "(Backup may be outdated)\n\n"
                    "A: Use backup    B: Cancel"),
                PromptStatus::BrewtendoCDNFail);
        } else {
            fclose(t);
        }
        return;
    }

    if (ok) {
        Result rc = MainUI::switchAccountWithRetry(mainStruct, 3, NascEnvironment::NASC_ENV_Dev);

        if (R_FAILED(rc)) {
            LOGF_AXIOM_ERROR(mainStruct, "Account switch failed: %08lx.\n\nPress start to reboot.", rc);
            ensureRebootPrompt(mainStruct);
            aptSetHomeAllowed(false);
            mainStruct->needsReboot = true;
            return;
        }

        PatchSwap::DeleteHandoff();
        mainStruct->swapPhase = SwapPhase::Done;
        mainStruct->needsReboot = true;
    }
}

static void doRestoreFromBackup(MainStruct* mainStruct) {
    mainStruct->swapStatusMsg = "Restoring Brewtendo patches from backup...";
    bool ok = PatchSwap::RestoreFromBackup(mainStruct);

    if (!ok) {
        LOG_AXIOM_ERROR(mainStruct,
            "Backup restore failed. Please re-install axiom "
            "or open a support ticket on the Brewtendo Discord.");
        ensureRebootPrompt(mainStruct);
        aptSetHomeAllowed(false);
        mainStruct->needsReboot = true;
        return;
    }

    Result rc = MainUI::switchAccountWithRetry(mainStruct, 3, NascEnvironment::NASC_ENV_Dev);

    if (R_FAILED(rc)) {
        LOGF_AXIOM_ERROR(mainStruct, "Account switch failed: %08lx.\n\nPress start to reboot", rc);
        ensureRebootPrompt(mainStruct);
        aptSetHomeAllowed(false);
        mainStruct->needsReboot = true;
        return;
    }

    PatchSwap::DeleteHandoff();
    mainStruct->swapPhase   = SwapPhase::Done;
    mainStruct->needsReboot = true;
}

static void doLaunchNimbus(MainStruct* mainStruct) {
    mainStruct->needsReboot = false;
    aptSetHomeAllowed(true);
    svcExitProcess();
}

bool LumaValidation::checkIfLumaOptionsEnabled(
    MainStruct* mainStruct,
    C3D_RenderTarget* top_screen,
    C3D_RenderTarget* bottom_screen,
    u32 kDown, u32 kHeld, touchPosition touch)
{
    kDown |= kHeld;

    C2D_SceneBegin(top_screen);
    DrawVersionString();
    C2D_DrawSprite(&mainStruct->top);

    if (mainStruct->swapPhase != SwapPhase::Idle &&
        mainStruct->swapPhase != SwapPhase::Done &&
        mainStruct->errorString[0] == 0) {
        DrawString(0.45f, infoColor, mainStruct->swapStatusMsg, 0);
    }

    C2D_SceneBegin(bottom_screen);

    if (!mainStruct->musicStarted) {
        PlayBGM("romfs:/bgm/AXIOM_SETUP_BGM.wav");
        mainStruct->musicStarted = true;
    }

    s64 isCitra = 0;
    svcGetSystemInfo(&isCitra, 0x20000, 0);
    if (isCitra) {
        mainStruct->state = 1;
        return false;
    }

    MainUI::updatePrompt(mainStruct, kDown);

    if (mainStruct->prompt.active) {
        if (mainStruct->prompt.result == PromptResult::Yes) {
            PromptStatus status = mainStruct->prompt.status;
            mainStruct->prompt.result = PromptResult::None;
            mainStruct->prompt.active = false;

            switch (status) {
                case PromptStatus::PretendoIntercept:
                    doSwitchBackToBrewtendo(mainStruct);
                    break;

                case PromptStatus::BrewtendoCDNFail:
                    doRestoreFromBackup(mainStruct);
                    break;

                default:
                    break;
            }
        }
        else if (mainStruct->prompt.result == PromptResult::No) {
            PromptStatus status = mainStruct->prompt.status;
            mainStruct->prompt.result = PromptResult::None;
            mainStruct->prompt.active = false;

            switch (status) {
                case PromptStatus::PretendoIntercept:
                    doLaunchNimbus(mainStruct);
                    break;

                case PromptStatus::BrewtendoCDNFail:
                    mainStruct->pretendoInterceptActive = false;
                    mainStruct->state = 1;
                    break;

                default:
                    break;
            }
        }

        MainUI::drawPrompt(mainStruct);
        return false;
    }

    if (mainStruct->needsReboot) {
        if (mainStruct->errorString[0] != 0) {
            DrawString(0.5f, 0xFF000000, mainStruct->errorString, 0);
        } else {
            DrawString(0.5f, infoColor, "Done!\n\nPress start to reboot.", 0);
        }
        if (kDown & KEY_START) return true;
        return false;
    }

    if (!mainStruct->pretendoInterceptChecked) {
        mainStruct->pretendoInterceptChecked = true;
        if (PatchSwap::HandoffExists()) {
            mainStruct->pretendoInterceptActive = true;
            PlaySFX("romfs:/sfx/MES_INFO.wav");
            MainUI::openPrompt(mainStruct,
                "Pretendo patches are currently active.\n\n"
                "A: Switch back to Brewtendo\n"
                "B: Exit Axiom (open Nimbus yourself)",
                PromptStatus::PretendoIntercept);
            MainUI::drawPrompt(mainStruct);
            return false;
        }
    }

    if (mainStruct->pretendoInterceptActive && !mainStruct->prompt.active) {
        MainUI::openPrompt(mainStruct,
            "Pretendo patches are currently active.\n\n"
            "A: Switch back to Brewtendo\n"
            "B: Exit Axiom (open Nimbus yourself)",
            PromptStatus::PretendoIntercept);
    }

    if (mainStruct->pretendoInterceptActive) {
        MainUI::drawPrompt(mainStruct);
        return false;
    }

    PlaySFX("romfs:/sfx/MES_WARNING.wav");

    if (mainStruct->firstRunOfState) {
        mainStruct->firmwareVersion = GetSystemInfoField(GetSystemInfoCFW, CFWSystemInfoField::FirmwareVersion);
        mainStruct->lumaVersion     = UnpackLumaVersion(mainStruct->firmwareVersion);

        mainStruct->configVersion      = GetSystemInfoField(GetSystemInfoCFW, CFWSystemInfoField::ConfigVersion);
        mainStruct->lumaConfigVersion  = UnpackConfigVersion(mainStruct->configVersion);

        mainStruct->lumaOptions                  = GetSystemInfoField(GetSystemInfoCFW, CFWSystemInfoField::ConfigBits);
        mainStruct->externalFirmsAndModulesEnabled = GetLumaOptionByIndex(LumaConfigBitIndex::ExternalFirmsAndModules, mainStruct->lumaOptions);
        mainStruct->gamePatchingEnabled            = GetLumaOptionByIndex(LumaConfigBitIndex::GamePatching,            mainStruct->lumaOptions);
    }

    if (std::get<0>(mainStruct->lumaVersion) < targetLumaVersion) {
        PlaySFX("romfs:/sfx/MES_WARNING.wav");
        DrawString(0.5f, infoColor,
            std::format("Your Luma3DS version is out of date, it should be Luma3DS {} or newer "
                        "for {} to function. Press A to exit.", targetLumaVersion, APP_TITLE), 0);
        if (kDown & KEY_A) { PlaySFX("romfs:/sfx/BIN_NEXT.wav"); }
        if (kDown & KEY_A) return true;
    }
    else if (!mainStruct->externalFirmsAndModulesEnabled || !mainStruct->gamePatchingEnabled) {
        if (kDown & KEY_B) {
            PlaySFX("romfs:/sfx/BIN_TRUE.wav");
            drawLumaInfo(mainStruct);
        } else {
            DrawString(0.5f, infoColor,
                std::format("Enable external FIRMs and modules: {}\nEnable game patching: {}\n\n"
                    "For {} to work, both of these Luma3DS options should be ENABLED. "
                    "To open Luma3DS settings, hold SELECT while booting your system.\n\n"
                    "If you are sure both options are enabled and the options shown don't match "
                    "your Luma3DS settings, please go to our Discord and open a support forum "
                    "with an image of the more information screen attached.\n"
                    "Press A to exit, or hold B for more information.",
                    mainStruct->externalFirmsAndModulesEnabled,
                    mainStruct->gamePatchingEnabled,
                    APP_TITLE), 0);
        }
        if (kDown & KEY_A) return true;
        else if ((kDown & KEY_X) && (kDown & KEY_Y)) mainStruct->state = 1;
    }
    else {
        if (kDown & KEY_A) drawLumaInfo(mainStruct);
        else mainStruct->state = 1;
    }

    return false;
}
