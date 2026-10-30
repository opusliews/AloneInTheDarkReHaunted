#pragma once

// TatouAtlasLog: diagnostic log of model atlas lookups, written to atlas_log.txt
// next to the exe. Only in AITD_ATLAS_LOG builds; the macros are empty otherwise.
// Logging only: the lookup order and results are unchanged.

#ifdef AITD_ATLAS_LOG

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <set>
#include <string>

bool isHDBackgroundEnabled();

inline std::string atlasLogFullPath(const char* path)
{
    char full[MAX_PATH] = "";
    if (!GetFullPathNameA(path, MAX_PATH, full, nullptr))
        return path;
    return full;
}

inline void atlasLogHeader(FILE* f)
{
    char exe[MAX_PATH] = "";
    char cwd[MAX_PATH] = "";
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    GetCurrentDirectoryA(MAX_PATH, cwd);
    fprintf(f, "TatouAtlasLog\n");
    fprintf(f, "exe:               %s\n", exe);
    fprintf(f, "working directory: %s\n", cwd);

    const std::string folder = atlasLogFullPath("atlases\\");
    const DWORD attr = GetFileAttributesA("atlases");
    fprintf(f, "atlas folder:      %s (%s)\n", folder.c_str(),
            attr == INVALID_FILE_ATTRIBUTES ? "MISSING" :
            (attr & FILE_ATTRIBUTE_DIRECTORY) ? "exists" : "NOT A FOLDER");
    fprintf(f, "atlases.hda:       %s\n",
            GetFileAttributesA("atlases.hda") == INVALID_FILE_ATTRIBUTES ? "missing" : "present");
    fprintf(f, "backgrounds_hd.hda: %s\n",
            GetFileAttributesA("backgrounds_hd.hda") == INVALID_FILE_ATTRIBUTES ? "missing" : "present");
    fprintf(f, "HD backgrounds at start: %s\n", isHDBackgroundEnabled() ? "on" : "off");

    // Files already in the folder, with sizes
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("atlases\\*", &fd);
    int count = 0;
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            if (count < 100)
                fprintf(f, "  in folder: %s (%lu bytes)\n", fd.cFileName, fd.nFileSizeLow);
            count++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    fprintf(f, "files in atlas folder at start: %d%s\n\n", count, count > 100 ? " (first 100 listed)" : "");
}

inline void atlasLogWrite(const char* fmt, ...)
{
    static FILE* s_file = nullptr;
    static bool s_tried = false;
    if (!s_tried)
    {
        s_tried = true;
        char exe[MAX_PATH] = "";
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        std::string path(exe);
        const size_t slash = path.find_last_of("\\/");
        path = (slash == std::string::npos ? std::string() : path.substr(0, slash + 1)) + "atlas_log.txt";
        s_file = fopen(path.c_str(), "w");
        if (s_file)
            atlasLogHeader(s_file);
    }
    if (!s_file)
        return;

    fprintf(s_file, "[%8lu ms] ", (unsigned long)GetTickCount());
    va_list args;
    va_start(args, fmt);
    vfprintf(s_file, fmt, args);
    va_end(args);
    fputc('\n', s_file);
    fflush(s_file);
}

// True the first time a key is seen, so repeated lookups are logged once
inline bool atlasLogOnce(const std::string& key)
{
    static std::set<std::string> s_seen;
    return s_seen.insert(key).second;
}

inline void atlasLogArchiveHit(const std::string& path, int w, int h)
{
    if (atlasLogOnce(path + "|archive"))
        atlasLogWrite("lookup %s: loaded from atlases.hda (%dx%d), loose file not checked", path.c_str(), w, h);
}

inline void atlasLogLoose(const std::string& path, const char* archiveState,
                          const unsigned char* data, int w, int h, int channels,
                          bool is16Bit, const char* failureReason)
{
    const std::string full = atlasLogFullPath(path.c_str());
    WIN32_FILE_ATTRIBUTE_DATA fa;
    const bool exists = GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fa) != 0;

    char outcome[256];
    if (data)
        snprintf(outcome, sizeof(outcome), "LOADED loose file (%dx%d, %d channels in file%s)",
                 w, h, channels, is16Bit ? ", 16-bit" : "");
    else if (exists)
        snprintf(outcome, sizeof(outcome), "loose file EXISTS (%lu bytes) but FAILED to decode: %s",
                 fa.nFileSizeLow, failureReason ? failureReason : "unknown");
    else
        snprintf(outcome, sizeof(outcome), "no loose file");

    if (atlasLogOnce(path + "|" + outcome))
        atlasLogWrite("lookup %s: %s; %s [full path %s]", path.c_str(), archiveState, outcome, full.c_str());
}

inline void atlasLogPngWrite(const std::string& path, int result)
{
    atlasLogWrite("auto-generated %s: %s [full path %s]", path.c_str(),
                  result ? "written" : "WRITE FAILED", atlasLogFullPath(path.c_str()).c_str());
}

// Once per body / list / HD state: whether an atlas is bound, and the body's primitives
inline void atlasLogBody(int bodyNum, const std::string& hqrName, bool hd, bool hasAtlas,
                         bool hasMainTexture, const sBody* pBody)
{
    char key[160];
    snprintf(key, sizeof(key), "body|%s|%d|%d|%d", hqrName.c_str(), bodyNum, hd ? 1 : 0, hasAtlas ? 1 : 0);
    if (!atlasLogOnce(key))
        return;

    int byType[16] = {};
    int byMaterial[16] = {};
    int numPrims = 0;
    if (pBody)
    {
        for (const sPrimitive& prim : pBody->m_primitives)
        {
            byType[(unsigned)prim.m_type & 15]++;
            byMaterial[prim.m_material & 15]++;
            numPrims++;
        }
    }

    std::string types, materials;
    char buf[32];
    for (int i = 0; i < 16; i++)
    {
        if (byType[i]) { snprintf(buf, sizeof(buf), " t%d:%d", i, byType[i]); types += buf; }
        if (byMaterial[i]) { snprintf(buf, sizeof(buf), " m%d:%d", i, byMaterial[i]); materials += buf; }
    }

    atlasLogWrite("draw body %d (%s), HD %s: atlas %s%s; %d primitives, types%s, materials%s",
                  bodyNum, hqrName.c_str(), hd ? "on" : "off",
                  hasAtlas ? "bound" : "NONE",
                  hasAtlas ? (hasMainTexture ? ", body texture valid" : ", NO body texture") : "",
                  numPrims, types.c_str(), materials.c_str());
}

#define ATLAS_LOG_STATE_DECL const char* atlasLogState = "no atlases.hda";
#define ATLAS_LOG_STATE(s) atlasLogState = (s);
#define ATLAS_LOG_ARCHIVE_HIT(path, w, h) atlasLogArchiveHit((path), (w), (h));
// stb_image is compiled static in each atlas file, so the caller passes its answers in
#define ATLAS_LOG_LOOSE(path, data, w, h, ch) atlasLogLoose((path), atlasLogState, (data), (w), (h), (ch), \
    (data) ? stbi_is_16_bit((path).c_str()) != 0 : false, stbi_failure_reason());
#define ATLAS_LOG_PNG_WRITE(path, result) atlasLogPngWrite((path), (result));
#define ATLAS_LOG_BODY(body, hqr, hd, hasAtlas, hasTex, pBody) atlasLogBody((body), (hqr), (hd), (hasAtlas), (hasTex), (pBody));

#else

#define ATLAS_LOG_STATE_DECL
#define ATLAS_LOG_STATE(s)
#define ATLAS_LOG_ARCHIVE_HIT(path, w, h)
#define ATLAS_LOG_LOOSE(path, data, w, h, ch)
#define ATLAS_LOG_PNG_WRITE(path, result)
#define ATLAS_LOG_BODY(body, hqr, hd, hasAtlas, hasTex, pBody)

#endif
