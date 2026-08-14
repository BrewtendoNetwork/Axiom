#include "LumaValidation.hpp"
#include <format>

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

    if (mainStruct->needsReboot) {
        if (mainStruct->errorString[0] != 0) {
            DrawString(0.5f, 0xFF000000, mainStruct->errorString, 0);
        } else {
            DrawString(0.5f, infoColor, "Done!\n\nPress start to reboot.", 0);
        }
        if (kDown & KEY_START) return true;
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
