#include "patchswap.hpp"
#include <mbedtls/sha256.h>
#include <3ds.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <format>

static const Result HTTPC_PENDING = (Result)HTTPC_RESULTCODE_DOWNLOADPENDING;

alignas(0x1000) static u32 s_socBuffer[0x20000 / 4];
static bool s_socInitialized = false;

static void axiomSocInit() {
    if (!s_socInitialized) {
        Result rc = socInit(s_socBuffer, sizeof(s_socBuffer));
        if (R_SUCCEEDED(rc)) s_socInitialized = true;
    }
}

static void axiomSocExit() {
    if (s_socInitialized) {
        socExit();
        s_socInitialized = false;
    }
}

static bool jsonGetString(const char* json, const char* key, char* out, size_t outLen) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t' || *p == ':') p++;
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < outLen - 1)
        out[i++] = *p++;
    out[i] = '\0';
    return true;
}

static bool copyFile(const char* src, const char* dst) {
    FILE* in = fopen(src, "rb");
    if (!in) return false;
    FILE* out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    static uint8_t buf[4096];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    fclose(in);
    fclose(out);
    if (!ok) std::remove(dst);
    return ok;
}

static void wipeTempDir(const char* dir) {
    for (int i = 0; i < AXIOM_PATCH_FILE_COUNT; i++) {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", dir, AXIOM_PATCH_FILES[i].filename);
        std::remove(path);
    }
    char manifestPath[256];
    snprintf(manifestPath, sizeof(manifestPath), "%s/manifest.json", dir);
    std::remove(manifestPath);
}

static bool verifySHA256(const char* filePath, const char* expectedHex) {
    FILE* f = fopen(filePath, "rb");
    if (!f) return false;

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    static uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        mbedtls_sha256_update(&ctx, buf, n);
    fclose(f);

    uint8_t hash[32];
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);

    char hexOut[65];
    for (int i = 0; i < 32; i++)
        snprintf(hexOut + i * 2, 3, "%02x", hash[i]);

    return strncasecmp(hexOut, expectedHex, 64) == 0;
}

static Result httpcDownloadFile(MainStruct* mainStruct, const char* url, const char* destPath) {
    httpcContext ctx;
    Result rc = httpcOpenContext(&ctx, HTTPC_METHOD_GET, url, 1);
    if (R_FAILED(rc)) {
        mainStruct->swapStatusMsg = std::format("httpcOpenContext failed: {:08X}", (uint32_t)rc);
        return rc;
    }

    httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
    httpcAddRequestHeaderField(&ctx, "User-Agent", "Axiom/1.0 (3DS)");

    rc = httpcBeginRequest(&ctx);
    if (R_FAILED(rc)) {
        mainStruct->swapStatusMsg = std::format("httpcBeginRequest failed: {:08X}", (uint32_t)rc);
        httpcCloseContext(&ctx);
        return rc;
    }

    u32 statusCode = 0;
    httpcGetResponseStatusCode(&ctx, &statusCode);
    if (statusCode != 200) {
        mainStruct->swapStatusMsg = std::format("HTTP {} for {}", statusCode, url);
        httpcCloseContext(&ctx);
        return -1;
    }

    FILE* f = fopen(destPath, "wb");
    if (!f) {
        mainStruct->swapStatusMsg = std::format("Could not open {} for writing", destPath);
        httpcCloseContext(&ctx);
        return -2;
    }

    static u8 dlbuf[4096];
    u32 dlNow = 0, dlTotal = 0, dlPrev = 0;
    Result dlrc = HTTPC_PENDING;
    while (dlrc == HTTPC_PENDING) {
        dlPrev = dlNow;
        dlrc = httpcReceiveData(&ctx, dlbuf, sizeof(dlbuf));
        httpcGetDownloadSizeState(&ctx, &dlNow, &dlTotal);
        u32 got = dlNow - dlPrev;
        if (got > 0) fwrite(dlbuf, 1, got, f);
        if (dlTotal > 0)
            mainStruct->swapStatusMsg = std::format("Downloading... {}/{} KB",
                dlNow / 1024, dlTotal / 1024);
    }
    fclose(f);
    httpcCloseContext(&ctx);

    if (R_FAILED(dlrc) && dlrc != (Result)0xD8A0A016) {
        mainStruct->swapStatusMsg = std::format("Download error: {:08X}", (uint32_t)dlrc);
        std::remove(destPath);
        return dlrc;
    }
    return 0;
}

static char* httpcDownloadString(MainStruct* mainStruct, const char* url) {
    httpcContext ctx;
    Result rc = httpcOpenContext(&ctx, HTTPC_METHOD_GET, url, 1);
    if (R_FAILED(rc)) return nullptr;

    httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
    httpcAddRequestHeaderField(&ctx, "User-Agent", "Axiom/1.0 (3DS)");

    rc = httpcBeginRequest(&ctx);
    if (R_FAILED(rc)) { httpcCloseContext(&ctx); return nullptr; }

    u32 statusCode = 0;
    httpcGetResponseStatusCode(&ctx, &statusCode);
    if (statusCode != 200) { httpcCloseContext(&ctx); return nullptr; }

    size_t cap = 4096, len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) { httpcCloseContext(&ctx); return nullptr; }

    static u8 chunk[1024];
    u32 dlNow = 0, dlPrev = 0, dlTotal = 0;
    Result dlrc = HTTPC_PENDING;
    while (dlrc == HTTPC_PENDING) {
        dlPrev = dlNow;
        dlrc = httpcReceiveData(&ctx, chunk, sizeof(chunk));
        httpcGetDownloadSizeState(&ctx, &dlNow, &dlTotal);
        u32 got = dlNow - dlPrev;
        if (len + got + 1 > cap) {
            cap *= 2;
            char* nb = (char*)realloc(buf, cap);
            if (!nb) { free(buf); httpcCloseContext(&ctx); return nullptr; }
            buf = nb;
        }
        memcpy(buf + len, chunk, got);
        len += got;
    }
    buf[len] = '\0';
    httpcCloseContext(&ctx);
    return buf;
}

namespace PatchSwap {

bool CheckFreeSpace(MainStruct* mainStruct) {
    mainStruct->swapPhase     = SwapPhase::CheckingSpace;
    mainStruct->swapStatusMsg = "Checking SD free space...";
    mainStruct->sdFreeBytes   = GetSDFreeBytes();
    return mainStruct->sdFreeBytes >= MIN_FREE_SPACE_BYTES;
}

bool HandoffExists() {
    FILE* f = fopen(AXIOM_HANDOFF_FILE, "r");
    if (!f) return false;
    fclose(f);
    return true;
}

bool WriteHandoff() {
    mkdir("/3ds", 0777);
    mkdir("/3ds/axiom", 0777);
    FILE* f = fopen(AXIOM_HANDOFF_FILE, "w");
    if (!f) return false;
    fprintf(f, "axiom_pretendo_handoff=1\n");
    fclose(f);
    return true;
}

bool DeleteHandoff() {
    return std::remove(AXIOM_HANDOFF_FILE) == 0;
}

bool BackupBrewtendoPatches(MainStruct* mainStruct) {
    mainStruct->swapPhase     = SwapPhase::BackingUp;
    mainStruct->swapStatusMsg = "Backing up Brewtendo patches...";

    mkdir("/3ds", 0777);
    mkdir("/3ds/axiom", 0777);
    mkdir(AXIOM_BACKUP_PATH, 0777);

    for (int i = 0; i < AXIOM_PATCH_FILE_COUNT; i++) {
        const PatchFile& pf = AXIOM_PATCH_FILES[i];
        char dstPath[256];
        snprintf(dstPath, sizeof(dstPath), "%s/%s", AXIOM_BACKUP_PATH, pf.filename);
        mainStruct->swapStatusMsg = std::format("Backing up {} ({}/{})",
            pf.filename, i + 1, AXIOM_PATCH_FILE_COUNT);
        copyFile(pf.lumaPath, dstPath);
    }
    return true;
}

bool DownloadPretendoPatches(MainStruct* mainStruct, Manifest* manifestOut) {
    mainStruct->swapPhase     = SwapPhase::Downloading;
    mainStruct->swapStatusMsg = "Fetching Pretendo patch manifest...";

    mkdir("/3ds", 0777);
    mkdir("/3ds/axiom", 0777);
    mkdir(AXIOM_TEMP_PATH, 0777);

    
    axiomSocInit();
    httpcInit(0);

    char* jsonBuf = httpcDownloadString(mainStruct, AXIOM_MANIFEST_URL);
    if (!jsonBuf) {
        httpcExit();
        axiomSocExit();
        mainStruct->swapStatusMsg = "Failed to fetch manifest. Check your internet connection.";
        return false;
    }

    char manifestPath[256];
    snprintf(manifestPath, sizeof(manifestPath), "%s/manifest.json", AXIOM_TEMP_PATH);
    FILE* mf = fopen(manifestPath, "w");
    if (mf) { fputs(jsonBuf, mf); fclose(mf); }

    Manifest* m = new Manifest();
    memset(m, 0, sizeof(Manifest));
    jsonGetString(jsonBuf, "source",    m->source,   sizeof(m->source));
    jsonGetString(jsonBuf, "release",   m->release,  sizeof(m->release));
    jsonGetString(jsonBuf, "commit",    m->commit,   sizeof(m->commit));
    jsonGetString(jsonBuf, "pulled_at", m->pulledAt, sizeof(m->pulledAt));

    const char* cursor = strstr(jsonBuf, "\"files\"");
    m->fileCount = 0;
    if (cursor) {
        cursor = strchr(cursor, '[');
        while (cursor && m->fileCount < 12) {
            cursor = strchr(cursor, '{');
            if (!cursor) break;
            ManifestEntry& entry = m->files[m->fileCount];
            jsonGetString(cursor, "filename", entry.filename, sizeof(entry.filename));
            jsonGetString(cursor, "dest",     entry.dest,     sizeof(entry.dest));
            jsonGetString(cursor, "url",      entry.url,      sizeof(entry.url));
            jsonGetString(cursor, "sha256",   entry.sha256,   sizeof(entry.sha256));
            m->fileCount++;
            cursor = strchr(cursor + 1, '}');
        }
    }
    free(jsonBuf);

    if (m->fileCount == 0) {
        httpcExit();
        axiomSocExit();
        mainStruct->swapStatusMsg = "Manifest contained no files.";
        wipeTempDir(AXIOM_TEMP_PATH);
        delete m;
        return false;
    }

    for (int i = 0; i < m->fileCount; i++) {
        ManifestEntry& entry = m->files[i];
        char destPath[256];
        snprintf(destPath, sizeof(destPath), "%s/%s", AXIOM_TEMP_PATH, entry.filename);

        mainStruct->swapStatusMsg = std::format("Downloading {} ({}/{})",
            entry.filename, i + 1, m->fileCount);

        Result rc = httpcDownloadFile(mainStruct, entry.url, destPath);
        if (R_FAILED(rc)) {
            httpcExit();
            axiomSocExit();
            mainStruct->swapStatusMsg = std::format(
                "Download failed: {} (error {:08X})\n\nPress start to reboot.",
                entry.filename, (uint32_t)rc);
            wipeTempDir(AXIOM_TEMP_PATH);
            delete m;
            return false;
        }

        mainStruct->swapStatusMsg = std::format("Verifying {} ({}/{})",
            entry.filename, i + 1, m->fileCount);

        if (!verifySHA256(destPath, entry.sha256)) {
            httpcExit();
            axiomSocExit();
            mainStruct->swapStatusMsg = std::format(
                "Hash mismatch: {} — possible tampering or corruption.\n\nPress start to reboot.",
                entry.filename);
            wipeTempDir(AXIOM_TEMP_PATH);
            delete m;
            return false;
        }
    }

    httpcExit();
    axiomSocExit();
    if (manifestOut) *manifestOut = *m;
    delete m;
    return true;
}

bool DownloadBrewtendoPatches(MainStruct* mainStruct) {
    mainStruct->swapPhase     = SwapPhase::Downloading;
    mainStruct->swapStatusMsg = "Fetching Brewtendo patches from CDN...";

    mkdir("/3ds", 0777);
    mkdir("/3ds/axiom", 0777);
    mkdir(AXIOM_TEMP_PATH, 0777);

    
    axiomSocInit();
    httpcInit(0);

    for (int i = 0; i < AXIOM_PATCH_FILE_COUNT; i++) {
        const PatchFile& pf = AXIOM_PATCH_FILES[i];
        char url[512], destPath[256];
        snprintf(url,      sizeof(url),      "%s/%s", AXIOM_CDN_BASE_URL, pf.filename);
        snprintf(destPath, sizeof(destPath), "%s/%s", AXIOM_TEMP_PATH,    pf.filename);

        mainStruct->swapStatusMsg = std::format("Downloading {} ({}/{})",
            pf.filename, i + 1, AXIOM_PATCH_FILE_COUNT);

        Result rc = httpcDownloadFile(mainStruct, url, destPath);
        if (R_FAILED(rc)) {
            httpcExit();
            axiomSocExit();
            mainStruct->swapStatusMsg = std::format(
                "CDN download failed: {} (error {:08X})",
                pf.filename, (uint32_t)rc);
            wipeTempDir(AXIOM_TEMP_PATH);
            return false;
        }
    }

    httpcExit();
    axiomSocExit();
    return true;
}

bool MoveTempToLuma(MainStruct* mainStruct, Manifest* manifest) {
    mainStruct->swapPhase     = SwapPhase::Moving;
    mainStruct->swapStatusMsg = "Installing patches...";

    mkdir("/luma", 0777);
    mkdir("/luma/sysmodules", 0777);
    mkdir("/luma/plugins", 0777);
    mkdir("/luma/titles", 0777);
    mkdir("/luma/titles/000400300000BC02", 0777);
    mkdir("/luma/titles/000400300000BD02", 0777);
    mkdir("/luma/titles/000400300000BE02", 0777);
    mkdir("/3ds", 0777);

    int fileCount = manifest ? manifest->fileCount : AXIOM_PATCH_FILE_COUNT;

    for (int i = 0; i < fileCount; i++) {
        const char* filename;
        const char* lumaPath;

        if (manifest) {
            filename = manifest->files[i].filename;
            lumaPath = manifest->files[i].dest;
        } else {
            filename = AXIOM_PATCH_FILES[i].filename;
            lumaPath = AXIOM_PATCH_FILES[i].lumaPath;
        }

        char srcPath[256];
        snprintf(srcPath, sizeof(srcPath), "%s/%s", AXIOM_TEMP_PATH, filename);

        mainStruct->swapStatusMsg = std::format("Installing {} ({}/{})",
            filename, i + 1, fileCount);

        std::remove(lumaPath);
        if (std::rename(srcPath, lumaPath) != 0) {
            mainStruct->swapStatusMsg = std::format(
                "Move failed for {}. Rolling back...", filename);

            for (int j = 0; j < i; j++) {
                if (manifest)
                    std::remove(manifest->files[j].dest);
                else
                    std::remove(AXIOM_PATCH_FILES[j].lumaPath);
            }

            bool restoreOK = RestoreFromBackup(mainStruct);
            if (restoreOK) {
                mainStruct->swapStatusMsg = std::format(
                    "Move failed: {}\n"
                    "Your Brewtendo patches have been restored.\n\n"
                    "Press start to reboot.", filename);
            } else {
                mainStruct->swapStatusMsg =
                    "Patches could not be installed or restored.\n"
                    "Please open a support ticket on the Brewtendo Discord.\n\nPress start to reboot after getting support.";
            }
            wipeTempDir(AXIOM_TEMP_PATH);
            return false;
        }
    }

    wipeTempDir(AXIOM_TEMP_PATH);
    return true;
}

bool RestoreFromBackup(MainStruct* mainStruct) {
    mainStruct->swapStatusMsg = "Restoring from backup...";
    int restored = 0;
    for (int i = 0; i < AXIOM_PATCH_FILE_COUNT; i++) {
        const PatchFile& pf = AXIOM_PATCH_FILES[i];
        char srcPath[256];
        snprintf(srcPath, sizeof(srcPath), "%s/%s", AXIOM_BACKUP_PATH, pf.filename);
        std::remove(pf.lumaPath);
        if (copyFile(srcPath, pf.lumaPath)) restored++;
    }
    return restored == AXIOM_PATCH_FILE_COUNT;
}

bool SwitchToPretendo(MainStruct* mainStruct, Manifest* manifestOut) {
    BackupBrewtendoPatches(mainStruct);

    Manifest* m = new Manifest();
    memset(m, 0, sizeof(Manifest));

    if (!DownloadPretendoPatches(mainStruct, m)) {
        mainStruct->swapPhase   = SwapPhase::Failed;
        mainStruct->needsReboot = true;
        delete m;
        return false;
    }

    if (!MoveTempToLuma(mainStruct, m)) {
        mainStruct->swapPhase   = SwapPhase::Failed;
        mainStruct->needsReboot = true;
        delete m;
        return false;
    }

    if (manifestOut) *manifestOut = *m;
    delete m;
    return true;
}

bool SwitchToBrewtendo(MainStruct* mainStruct) {
    if (!DownloadBrewtendoPatches(mainStruct)) {
        mainStruct->swapPhase = SwapPhase::Failed;
        return false;
    }

    if (!MoveTempToLuma(mainStruct)) {
        mainStruct->swapPhase   = SwapPhase::Failed;
        mainStruct->needsReboot = true;
        return false;
    }

    return true;
}

void DrawManifestInfo(const Manifest& manifest) {
    std::string info = std::format(
        "Pretendo patches verified\n"
        "Source:  {}\n"
        "Release: {}  Commit: {:.8}\n"
        "Pulled:  {}\n"
        "All file hashes verified. No modifications detected.",
        manifest.source, manifest.release, manifest.commit, manifest.pulledAt);
    DrawString(0.42f, C2D_Color32(30, 100, 30, 255), info, 0);
}

}
