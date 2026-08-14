#pragma once

#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

#include "../build/version.hpp"
#include <citro2d.h>
#include <3ds.h>
#include <algorithm>
#include <cctype>
#include <string>

#define AXIOM_UPDATE_PATH       "/3ds/axiom/update"

#define NIMBUS_TITLE_ID         0x000400000D40D200ULL

enum class NascEnvironment : u8 {
    NASC_ENV_Prod = 0, // Nintendo
    NASC_ENV_Test = 1, // Pretendo
    NASC_ENV_Dev  = 2  // Brewtendo
};

enum class CFWSystemInfoField : s32 {
    FirmwareVersion = 0,
    CommitHash      = 1,
    ConfigVersion   = 2,
    ConfigBits      = 3
};

enum class LumaConfigBitIndex : s32 {
    AutobootEmunand         = 0,
    ExternalFirmsAndModules = 1,
    GamePatching            = 2
};

enum class PromptResult {
    None,
    Yes,
    No
};

enum class PromptStatus {
    Unknown,
    BNIDUnlink,
};

struct PromptState {
    bool         active  = false;
    std::string  message;
    PromptResult result  = PromptResult::None;
    PromptStatus status  = PromptStatus::Unknown;
};

struct MainStruct {
    C2D_Sprite debug_button;
    C2D_Sprite debug_header;
    C2D_Sprite go_back;
    C2D_Sprite header;
    C2D_Sprite nintendo_unloaded_deselected;
    C2D_Sprite nintendo_unloaded_selected;
    C2D_Sprite nintendo_loaded_selected;
    C2D_Sprite nintendo_loaded_deselected;
    C2D_Sprite brewtendo_unloaded_deselected;
    C2D_Sprite brewtendo_unloaded_selected;
    C2D_Sprite brewtendo_loaded_selected;
    C2D_Sprite brewtendo_loaded_deselected;
    C2D_Sprite top;
    C2D_Sprite bottom;

    u32 screen    = 0;
    u32 state     = 0;
    u32 lastState = 0;
    u32 welcome   = 1;

    NascEnvironment currentAccount = NascEnvironment::NASC_ENV_Prod;
    NascEnvironment buttonSelected = NascEnvironment::NASC_ENV_Prod;

    bool firstRunOfState  = true;
    bool buttonWasPressed = false;
    bool needsReboot      = false;
    bool updateChecked    = false;
    bool musicStarted     = false;

    char errorString[256];

    s64 firmwareVersion;
    std::tuple<u8, u8, u8> lumaVersion;
    s64 configVersion;
    std::tuple<u8, u8> lumaConfigVersion;
    s64  lumaOptions;
    bool gamePatchingEnabled;
    bool externalFirmsAndModulesEnabled;

    PromptState prompt;
};

const int  targetLumaVersion = 13;
const int  GetSystemInfoCFW  = 0x10000;
const u32  defaultColor      = C2D_Color32(108, 98, 64, 255);
const u32  infoColor         = C2D_Color32(45, 45, 44, 255);

#define LOG_AXIOM_ERROR(mainStruct, fmt) \
    if (mainStruct->errorString[0] == 0) {                                       \
        snprintf(mainStruct->errorString, sizeof(mainStruct->errorString), fmt); \
    }

#define LOGF_AXIOM_ERROR(mainStruct, fmt, ...) \
    if (mainStruct->errorString[0] == 0) {                                                    \
        snprintf(mainStruct->errorString, sizeof(mainStruct->errorString), fmt, __VA_ARGS__); \
    }

inline std::string sanitizeForDisplay(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        if (c >= 0x20 && c <= 0x7E) out.push_back((char)c);
    }
    return out;
}

inline void ensureRebootPrompt(MainStruct* mainStruct) {
    std::string es(mainStruct->errorString);
    std::string lower = es;
    for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
    if (lower.find("press start") != std::string::npos) return;

    std::string result = es.empty()
        ? "Done!\n\nPress START to reboot."
        : es + "\n\nPress START to reboot.";
    snprintf(mainStruct->errorString, sizeof(mainStruct->errorString), "%s", result.c_str());
}

#define handleResult(action, mainStruct, name) \
    rc = action;                                                                \
    if (R_FAILED(rc)) {                                                         \
        LOGF_AXIOM_ERROR(mainStruct, "%s failed with error: %08lx", name, rc); \
        printf("%s failed with error: %08lx\n\n", name, rc);                   \
    }

extern C2D_Font    font;
extern C2D_TextBuf textBuf;
extern CFG_Region  loadedSystemFont;

void       GetStringSize(float size, float *width, float *height, const char *text);
float      GetStringHeight(float size, const char *text);
void       DrawString(float size, u32 color, std::string text, int flags);
void       DrawControls();
CFG_Region GetSystemRegion();
void       DrawVersionString();

bool GetLumaOptionByIndex(LumaConfigBitIndex index, s64 options);
s64  GetSystemInfoField(s32 category, CFWSystemInfoField accessor);
std::tuple<u8, u8, u8> UnpackLumaVersion(s64 packed_version);
std::tuple<u8, u8>     UnpackConfigVersion(s64 packed_config_version);
void drawLumaInfo(MainStruct *mainStruct);
