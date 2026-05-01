#include "LumaValidation.hpp"
#include <format>

void PlayBGM(const char* path) {
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

void PlaySFX(const char* path) {
    if (ndspChnIsPlaying(1)) return;

    FILE* f = fopen(path, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    u32 size = ftell(f);
    u32 dataSize = size - 44;
    fseek(f, 44, SEEK_SET);

    static u8* buffer = nullptr;
    static ndspWaveBuf waveBuf;

    if (buffer) {
        linearFree(buffer);
        buffer = nullptr;
    }

    buffer = (u8*)linearAlloc(dataSize);
    if (!buffer) { fclose(f); return; }
    fread(buffer, 1, dataSize, f);
    fclose(f);

    memset(&waveBuf, 0, sizeof(ndspWaveBuf));
    waveBuf.data_vaddr = buffer;
    waveBuf.nsamples = dataSize / 2;
    waveBuf.looping = false;

    DSP_FlushDataCache(buffer, dataSize);

    ndspChnSetRate(1, 16000.0f);
    ndspChnSetFormat(1, NDSP_FORMAT_MONO_PCM16);
    ndspChnWaveBufAdd(1, &waveBuf);
}

bool LumaValidation::checkIfLumaOptionsEnabled(MainStruct *mainStruct, C3D_RenderTarget* top_screen, C3D_RenderTarget* bottom_screen, u32 kDown, u32 kHeld, touchPosition touch)
{
    PlaySFX("romfs:/sfx/BIN_FALSE.wav");
    
    if (!mainStruct->musicStarted) {
        // Load and play BGM
        loadAndPlayBGM("romfs:/bgm/AXIOM_MAIN_BGM.wav");
        mainStruct->musicStarted = true;
    }
    
    kDown |= kHeld; // make kDown have held keys aswell
    
    C2D_SceneBegin(top_screen);
    DrawVersionString();
    
    C2D_SceneBegin(bottom_screen);
    
    // if running on citra, skip all luma checks
    s64 isCitra = 0;
    svcGetSystemInfo(&isCitra, 0x20000, 0);
    if (isCitra) {
        mainStruct->state = 1;
        return false;
    }
    
    PlaySFX("romfs:/sfx/MES_WARNING.wav");
    // if this is the first time the function has been run, get luma information
    if (mainStruct->firstRunOfState) {
        mainStruct->firmwareVersion = GetSystemInfoField(GetSystemInfoCFW, CFWSystemInfoField::FirmwareVersion);
        mainStruct->lumaVersion = UnpackLumaVersion(mainStruct->firmwareVersion);
        
        mainStruct->configVersion = GetSystemInfoField(GetSystemInfoCFW, CFWSystemInfoField::ConfigVersion); // this
        mainStruct->lumaConfigVersion = UnpackConfigVersion(mainStruct->configVersion);
        
        mainStruct->lumaOptions = GetSystemInfoField(GetSystemInfoCFW, CFWSystemInfoField::ConfigBits); // this
        mainStruct->externalFirmsAndModulesEnabled = GetLumaOptionByIndex(LumaConfigBitIndex::ExternalFirmsAndModules, mainStruct->lumaOptions); // this
        mainStruct->gamePatchingEnabled = GetLumaOptionByIndex(LumaConfigBitIndex::GamePatching, mainStruct->lumaOptions); // and this might need multiple updates due to the fact that they fluctuate occasionally, if need be i can make a function that handles multiple versions though
    }
    
    if (kDown & KEY_A) {
        PlaySFX("romfs:/sfx/BIN_NEXT.wav");
    }
    
    // if the major version of luma3ds is under the targetLumaVersion (defined earlier in the file), send an error
    if (std::get<0>(mainStruct->lumaVersion) < targetLumaVersion) {
        C2D_DrawSprite(&mainStruct->blank_info_message);
        DrawString(0.5f, infoColor, std::format("Your Luma3DS version is out of date, it should be Luma3DS {} or newer for {} to function. Press A to exit.", targetLumaVersion, APP_TITLE), 0);
        
        // if A is pressed, return true to exit
        if (kDown & KEY_A) return true;
    }
    // else if either external firms and modules or game patching is not enabled, send another error and draw luma info if b is pressed
    else if (!mainStruct->externalFirmsAndModulesEnabled || !mainStruct->gamePatchingEnabled)
        if (kDown & KEY_B) {
            PlaySFX("romfs:/sfx/BIN_TRUE.wav");
            drawLumaInfo(mainStruct);
        }
        else {
            C2D_DrawSprite(&mainStruct->blank_info_message);
            DrawString(0.5f, infoColor, std::format("Enable external FIRMs and modules: {}\nEnable game patching: {}\n\nFor {} to work, both of these Luma3DS options should be ENABLED. To open Luma3DS settings, hold SELECT while booting your system.\n\nIf you are sure both options are enabled and the options shown don't match your Luma3DS settings, please go to our Discord and open a support forum with an image of the more information screen attached.\nPress A to exit, or hold B for more information.", mainStruct->externalFirmsAndModulesEnabled, mainStruct->gamePatchingEnabled, APP_TITLE), 0);
        }
        
        // if A is pressed, return true to exit, else if X and Y is pressed
        if (kDown & KEY_A) return true;
        else if (kDown & KEY_X && kDown & KEY_Y) mainStruct->state = 1; // bypass if I need some time to fix it and get it released
    else {
        if (kDown & KEY_A) drawLumaInfo(mainStruct);
        else mainStruct->state = 1; // if A is held, show information, else go to the main menu
    }
    
    return false;
}
