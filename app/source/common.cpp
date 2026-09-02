#include "common.hpp"
#include <3ds/services/cfgu.h>
#include <format>

// credit to the universal-team for most/all of the code past here
C2D_Font font;
C2D_TextBuf textBuf;
CFG_Region loadedSystemFont = GetSystemRegion();

void GetStringSize(float size, float *width, float *height, const char *text) {
    C2D_Text c2d_text;
    C2D_TextFontParse(&c2d_text, font, textBuf, text);
    C2D_TextGetDimensions(&c2d_text, size, size, width, height);
}

float GetStringHeight(float size, const char *text) {
    float height = 0;
    GetStringSize(size, NULL, &height, text);
    return height;
}

void DrawString(float size, u32 color, std::string text, int flags) {
    C2D_Text c2d_text;

    C2D_TextBufClear(textBuf);
    C2D_TextFontParse(&c2d_text, font, textBuf, text.c_str());
    C2D_TextOptimize(&c2d_text);

    float x = 8;
    float y = 8;

    float screenWidthBottom = 320;
    float screenHeight      = 240;

    switch (loadedSystemFont) {
        case CFG_REGION_CHN:
        case CFG_REGION_KOR:
        case CFG_REGION_TWN:
            y += 3.0f * size;
            break;
        default:
            break;
    }

    float heightScale = std::min(size, size * ((screenHeight - y * 2) / GetStringHeight(size, text.c_str())));
    C2D_DrawText(&c2d_text, C2D_WithColor | C2D_WordWrap | flags, x, y, 0.5f, size, heightScale, color, screenWidthBottom - x * 2);
}

void DrawControls() {
    C2D_Text c2d_text;
    const char* text = "A: Select\nL: Switch to Pretendo\nY: Launch plugin\nX: Unlink BNID";
    float size = 0.5f;
    int offset = 6;
    int bottomOffset = 240 - offset - (int)GetStringHeight(size, text);

    C2D_TextBufClear(textBuf);
    C2D_TextFontParse(&c2d_text, font, textBuf, text);
    C2D_TextOptimize(&c2d_text);
    C2D_DrawText(&c2d_text, C2D_WithColor | C2D_AlignLeft, offset, bottomOffset, 0.5f, size, size, C2D_Color32(0, 0, 0, 255));
}

CFG_Region GetSystemRegion() {
    u8 systemRegion = 0;
    Result rc = CFGU_SecureInfoGetRegion(&systemRegion);
    if (R_FAILED(rc)) return CFG_REGION_USA;
    return static_cast<CFG_Region>(systemRegion);
}

void DrawVersionString() {
    C2D_Text c2d_text;
    std::string text = std::format("{} {}.{}.{}", APP_TITLE, VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
    float size = 0.5f;
    int offset = 6;
    int bottomOffset     = 240 - offset;
    int horizontalOffset = 400 - offset;

    bool hasBetaHash = AXIOM_BETA_HASH[0] != '\0';
    float versionY = bottomOffset - GetStringHeight(size, text.c_str());

    if (hasBetaHash) {
        std::string betaHash = std::format("Beta hash: {}", AXIOM_BETA_HASH);
        float betaHashSize = 0.4f;
        float betaHashHeight = GetStringHeight(betaHashSize, betaHash.c_str());
        float betaHashY = bottomOffset - betaHashHeight;

        versionY = betaHashY - GetStringHeight(size, text.c_str()) - 2;

        C2D_TextBufClear(textBuf);
        C2D_TextFontParse(&c2d_text, font, textBuf, betaHash.c_str());
        C2D_TextOptimize(&c2d_text);
        C2D_DrawText(&c2d_text, C2D_WithColor | C2D_AlignRight, horizontalOffset, betaHashY, 0.5f, betaHashSize, betaHashSize, C2D_Color32(70, 70, 70, 255));
    }

    C2D_TextBufClear(textBuf);
    C2D_TextFontParse(&c2d_text, font, textBuf, text.c_str());
    C2D_TextOptimize(&c2d_text);
    C2D_DrawText(&c2d_text, C2D_WithColor | C2D_AlignRight, horizontalOffset, versionY, 0.5f, size, size, C2D_Color32(0, 0, 0, 255));
}

void DrawLicenseString() {
    C2D_Text c2d_text;
    const char* text = "Licensed under the GNU General Public License v3.\nYou are entitled to the source code of this exact\nbuild. If you don't have it, ask whoever gave you\nthis app for it.";
    float size = 0.35f;
    int offset = 6;
    int bottomOffset = 240 - offset - (int)GetStringHeight(size, text);

    C2D_TextBufClear(textBuf);
    C2D_TextFontParse(&c2d_text, font, textBuf, text);
    C2D_TextOptimize(&c2d_text);
    C2D_DrawText(&c2d_text, C2D_WithColor | C2D_AlignLeft, offset, bottomOffset, 0.5f, size, size, C2D_Color32(50, 50, 50, 255));
}

bool GetLumaOptionByIndex(LumaConfigBitIndex index, s64 options) {
    return ((options >> (static_cast<s32>(index))) & 0x1) == 1;
}

s64 GetSystemInfoField(s32 category, CFWSystemInfoField accessor) {
    s64 out = 0;
    svcGetSystemInfo(&out, category, static_cast<s32>(accessor));
    return out;
}

std::tuple<u8, u8, u8> UnpackLumaVersion(s64 packed_version) {
    return { (packed_version >> 24) & 0xFF, (packed_version >> 16) & 0xFF, (packed_version >> 8) & 0xFF };
}

std::tuple<u8, u8> UnpackConfigVersion(s64 packed_config_version) {
    return { (packed_config_version >> 16) & 0xFF, packed_config_version & 0xFF };
}

void drawLumaInfo(MainStruct *mainStruct) {
    DrawString(0.5f, defaultColor, std::format("Luma version is {}.{}.{}\nLuma config version is {}.{}\n\nLuma3DS config bits are:\n{:016b}\n{:016b}\n{:016b}\n{:016b}",
        std::get<0>(mainStruct->lumaVersion), std::get<1>(mainStruct->lumaVersion), std::get<2>(mainStruct->lumaVersion),
        std::get<0>(mainStruct->lumaConfigVersion), std::get<1>(mainStruct->lumaConfigVersion),
        mainStruct->lumaOptions >> 48,
        (mainStruct->lumaOptions >> 32) & 0xFFFF,
        (mainStruct->lumaOptions >> 16) & 0xFFFF,
        mainStruct->lumaOptions & 0xFFFF), 0);
}
