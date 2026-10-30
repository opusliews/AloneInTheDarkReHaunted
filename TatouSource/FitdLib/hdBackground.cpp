///////////////////////////////////////////////////////////////////////////////
// Alone In The Dark Re-Haunted
// Copyright (C) 2026 Infogrames / Spacefarer Retro Remasters LLC
// Based on FITD by yaz0r, Re-haunted is released under GPL
// Author: Jake Jackson (jake@spacefarergames.com)
//
// High-definition background image loading
///////////////////////////////////////////////////////////////////////////////

#include "common.h"
#include "hdBackground.h"
#ifdef AITD_HD_REPAINT_BUILD
#include "hdCustomModel.h"
#endif
#include "hdArchive.h"
#include "configRemaster.h"
#include "consoleLog.h"
#include "memoryManager.h"
#include "exceptionHandler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <algorithm>
#ifdef AITD_HD_REPAINT_BUILD
#include <atomic>
#include <thread>
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

// Use static linkage to avoid conflicts with bimg's stb_image implementation
// This makes the stb_image functions local to this translation unit
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "../ThirdParty/bgfx.cmake/bimg/3rdparty/stb/stb_image.h"

// Returns the folder path for HD backgrounds
const char* getHDBackgroundFolder()
{
    return "backgrounds_hd";
}

// Check if HD backgrounds are enabled in configuration
bool isHDBackgroundEnabled()
{
    return g_remasterConfig.graphics.enableHDBackgrounds;
}

// Ensure the HD archive is open (lazy init, called on first use).
// The HDArchive namespace is a singleton - only one archive can be open at a
// time.  Other subsystems (e.g. modelAtlas) may clobber it by opening a
// different archive, so we must be prepared to reopen backgrounds_hd.hda
// whenever it has been closed out from under us.
static void ensureArchiveOpen()
{
    if (HDArchive::isOpen())
        return; // still open from a previous call

    static bool s_failed = false;
    if (s_failed) return; // file does not exist - no point retrying every frame

    if (!HDArchive::open("backgrounds_hd.hda"))
        s_failed = true;
}

// Load an image from an archive entry using stb_image
static unsigned char* loadFromArchiveEntry(const HDArchiveEntry* entry, int* width, int* height, int* channels)
{
    size_t bufSize = 0;
    unsigned char* buf = HDArchive::readEntryAlloc(entry, &bufSize);
    if (!buf) return nullptr;

    unsigned char* imageData = nullptr;
    PROTECTED_CALL({
        imageData = stbi_load_from_memory(buf, (int)bufSize, width, height, channels, 0);
        if (imageData)
        {
            TRACK_ALLOCATION(imageData, (*width) * (*height) * (*channels), "stbi_load_from_memory");
        }
    }, "stbi_load_from_memory HD background (archive)");
    free(buf);
    return imageData;
}

// Helper function to scan directory for image files
static std::vector<std::string> scanAnimationDirectory(const char* dirPath)
{
    std::vector<std::string> files;

#ifdef _WIN32
    WIN32_FIND_DATAA findData;
    char searchPath[512];
    snprintf(searchPath, sizeof(searchPath), "%s/*.*", dirPath);

    HANDLE hFind = FindFirstFileA(searchPath, &findData);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        return files;
    }

    do
    {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            const char* fileName = findData.cFileName;
            size_t len = strlen(fileName);

            // Check for image extensions
            if (len > 4)
            {
                const char* ext = fileName + len - 4;
                if (_stricmp(ext, ".png") == 0 || 
                    _stricmp(ext, ".tga") == 0 || 
                    _stricmp(ext, ".bmp") == 0)
                {
                    files.push_back(std::string(fileName));
                }
            }
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
#else
    DIR* dir = opendir(dirPath);
    if (!dir)
    {
        return files;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        if (entry->d_type == DT_REG)
        {
            const char* fileName = entry->d_name;
            size_t len = strlen(fileName);

            // Check for image extensions
            if (len > 4)
            {
                const char* ext = fileName + len - 4;
                if (strcasecmp(ext, ".png") == 0 || 
                    strcasecmp(ext, ".tga") == 0 || 
                    strcasecmp(ext, ".bmp") == 0)
                {
                    files.push_back(std::string(fileName));
                }
            }
        }
    }

    closedir(dir);
#endif

    // Sort files alphabetically to ensure consistent frame order
    std::sort(files.begin(), files.end());

    return files;
}

// Helper function to load a single frame from file (optimized with memory mapping)
static unsigned char* loadSingleFrame(const char* filePath, int* width, int* height, int* channels)
{
    unsigned char* imageData = nullptr;

#ifdef _WIN32
    // Use memory-mapped file for faster loading (Windows) - ~2x faster
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hMapping = NULL;
    void* pView = nullptr;
    DWORD fileSize = 0;

    PROTECTED_CALL({
        hFile = CreateFileA(filePath, GENERIC_READ, FILE_SHARE_READ, 
                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            fileSize = GetFileSize(hFile, NULL);
            if (fileSize > 0)
            {
                hMapping = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
                if (hMapping)
                {
                    pView = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
                    if (pView)
                    {
                        // Load from memory instead of disk
                        imageData = stbi_load_from_memory((unsigned char*)pView, fileSize, 
                                                          width, height, channels, 0);

                        if (imageData)
                        {
                            TRACK_ALLOCATION(imageData, (*width) * (*height) * (*channels), "stbi_load");
                        }

                        UnmapViewOfFile(pView);
                    }
                    CloseHandle(hMapping);
                }
            }
            CloseHandle(hFile);
        }
    }, "stbi_load HD background frame (memory-mapped)");
#else
    // Fallback to regular file loading (Linux/Mac)
    PROTECTED_CALL({
        imageData = stbi_load(filePath, width, height, channels, 0);
        if (imageData)
        {
            TRACK_ALLOCATION(imageData, (*width) * (*height) * (*channels), "stbi_load");
        }
    }, "stbi_load HD background frame");
#endif

    return imageData;
}

// Load an image file from the HD backgrounds folder by filename
// Returns pixel data (caller must free with freeHDImageData), or nullptr on failure
unsigned char* loadHDImageFile(const char* filename, int* width, int* height, int* channels)
{
    // Try archive first
    ensureArchiveOpen();
    if (HDArchive::isOpen())
    {
        const HDArchiveEntry* entry = HDArchive::findEntry(filename);
        if (entry)
        {
            return loadFromArchiveEntry(entry, width, height, channels);
        }
    }

    // Filesystem fallback
    char filePath[512];
    snprintf(filePath, sizeof(filePath), "%s/%s", getHDBackgroundFolder(), filename);

    FILE* testFile = nullptr;
    fopen_s(&testFile, filePath, "rb");
    if (!testFile)
        return nullptr;
    fclose(testFile);

    return loadSingleFrame(filePath, width, height, channels);
}

// Free image data returned by loadHDImageFile
void freeHDImageData(unsigned char* data)
{
    if (data)
    {
        PROTECTED_CALL({
            UNTRACK_ALLOCATION(data, "stbi_image_free");
            stbi_image_free(data);
        }, "stbi_image_free HD image");
    }
}

// Load an image file from an arbitrary filesystem path
// Returns pixel data (caller must free with freeHDImageData), or nullptr on failure
unsigned char* loadImageFile(const char* fullPath, int* width, int* height, int* channels)
{
    return loadSingleFrame(fullPath, width, height, channels);
}

// Load an image from a memory buffer (e.g. embedded PNG data)
// Returns pixel data (caller must free with freeHDImageData), or nullptr on failure
unsigned char* loadImageFromMemory(const unsigned char* buffer, int bufferSize, int* width, int* height, int* channels)
{
    unsigned char* imageData = nullptr;
    PROTECTED_CALL({
        imageData = stbi_load_from_memory(buffer, bufferSize, width, height, channels, 0);
        if (imageData)
        {
            TRACK_ALLOCATION(imageData, (*width) * (*height) * (*channels), "stbi_load_from_memory");
        }
    }, "stbi_load_from_memory");
    return imageData;
}

#ifdef AITD_HD_REPAINT_BUILD
bool g_hdCustomBackgroundActive = false;

// TatouHd: a custom animation shows as soon as frame 0 is decoded; a worker thread
// decodes the other frames in order. frames[0 .. ready-1] are final (a frame that
// failed stays nullptr); playback holds at the last decoded frame until more arrive.
// freeHDBackground cancels and joins the worker before freeing the frames.
struct HDFrameStreamer
{
    std::thread worker;
    std::atomic<bool> cancel{false};
    std::atomic<int> ready{1};
};

static void streamCustomFrames(HDFrameStreamer* s, unsigned char** frames, std::vector<std::string> paths,
                               int width, int height, int channels)
{
    for (size_t i = 1; i < paths.size() && !s->cancel.load(std::memory_order_relaxed); i++)
    {
        int w, h, c;
        unsigned char* data = loadSingleFrame(paths[i].c_str(), &w, &h, &c);
        if (!data)
            printf(HDBG_WARN "Failed to load frame %zu" CON_RESET "\n", i);
        else if (w != width || h != height || c != channels)
        {
            printf(HDBG_WARN "Frame %zu has inconsistent dimensions, skipping" CON_RESET "\n", i);
            stbi_image_free(data);
            data = nullptr;
        }
        frames[i] = data;
        s->ready.store((int)i + 1, std::memory_order_release);
    }
}

// TatouHd: backgrounds_custom/<name>_<idx>[_<suffix>].png overrides everything
// else (animated, archive and backgrounds_hd/), at any resolution.
static HDBackgroundInfo* loadCustomBackground(const char* backgroundName, int cameraIdx, const char* suffix)
{
    char filePath[512];
    if (suffix && suffix[0] != '\0')
        snprintf(filePath, sizeof(filePath), "backgrounds_custom/%s_%03d_%s.png", backgroundName, cameraIdx, suffix);
    else
        snprintf(filePath, sizeof(filePath), "backgrounds_custom/%s_%03d.png", backgroundName, cameraIdx);

    FILE* testFile = nullptr;
    fopen_s(&testFile, filePath, "rb");
    if (!testFile)
        return nullptr;
    fclose(testFile);

    int width, height, channels;
    unsigned char* imageData = loadSingleFrame(filePath, &width, &height, &channels);
    if (!imageData)
    {
        printf(HDBG_ERR "Failed to load custom background: %s" CON_RESET "\n", filePath);
        return nullptr;
    }

    HDBackgroundInfo* bgInfo = (HDBackgroundInfo*)SAFE_MALLOC(sizeof(HDBackgroundInfo), __FILE__, __LINE__);
    if (!bgInfo)
    {
        stbi_image_free(imageData);
        return nullptr;
    }
    memset(bgInfo, 0, sizeof(HDBackgroundInfo));
    bgInfo->data = imageData;
    bgInfo->width = width;
    bgInfo->height = height;
    bgInfo->channels = channels;
    bgInfo->frameCount = 1;
    bgInfo->frameTime = 0.08f;
    bgInfo->allFramesLoaded = true;
    bgInfo->minFramesForPlayback = 1;
    bgInfo->loadedFrameCount = 1;
    bgInfo->isCustom = true;

    printf(HDBG_OK "Loaded custom background: %s (%dx%d, %d channels)\n", filePath, width, height, channels);
    return bgInfo;
}

// TatouHd: frame files of backgrounds_custom/anim_<name>_<idx>[_<suffix>]/,
// sorted (empty if there is no such folder). dirPath receives the folder.
static const char* s_customAnimPrefix = "anim";   // "overlay" / "bgoverlay" / "bgoverlay2" inside loadHDOverlay

static std::vector<std::string> scanCustomAnimation(const char* backgroundName, int cameraIdx, const char* suffix,
                                                    char* dirPath, size_t dirPathSize)
{
    if (suffix && suffix[0] != '\0')
        snprintf(dirPath, dirPathSize, "backgrounds_custom/%s_%s_%03d_%s", s_customAnimPrefix, backgroundName, cameraIdx, suffix);
    else
        snprintf(dirPath, dirPathSize, "backgrounds_custom/%s_%s_%03d", s_customAnimPrefix, backgroundName, cameraIdx);
    return scanAnimationDirectory(dirPath);
}

bool hdCustomBackgroundExists(const char* backgroundName, int cameraIdx, const char* suffix)
{
    char path[512];
    if (!scanCustomAnimation(backgroundName, cameraIdx, suffix, path, sizeof(path)).empty())
        return true;
    if (suffix && suffix[0] != '\0')
        snprintf(path, sizeof(path), "backgrounds_custom/%s_%03d_%s.png", backgroundName, cameraIdx, suffix);
    else
        snprintf(path, sizeof(path), "backgrounds_custom/%s_%03d.png", backgroundName, cameraIdx);
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

HDBackgroundInfo* loadHDOverlay(const char* backgroundName, int cameraIdx, const char* prefix)
{
    char dir[512];
    s_customAnimPrefix = prefix;
    HDBackgroundInfo* overlay = nullptr;
    if (!scanCustomAnimation(backgroundName, cameraIdx, nullptr, dir, sizeof(dir)).empty())
        overlay = loadHDBackground(backgroundName, cameraIdx, nullptr);   // the custom animation path
    s_customAnimPrefix = "anim";
    if (overlay && (!overlay->isCustom || !overlay->isAnimated))
    {
        freeHDBackground(overlay);
        overlay = nullptr;
    }
    if (overlay && overlay->channels != 4)
        printf(HDBG_WARN "%s: frames have no alpha channel, the overlay covers the whole %s" CON_RESET "\n", dir,
               strncmp(prefix, "bgoverlay", 9) == 0 ? "background" : "scene");
    return overlay;
}

// --hd-stream: play a background in real time (80 ms steps, as the game does) until
// every frame is decoded, then load and free it 30 times after random delays, as
// fast camera switches would while frames are still decoding.
static void hdStreamTest(const char* arg, const char* name, int idx, const char* suffix)
{
    DWORD t0 = GetTickCount();
    HDBackgroundInfo* bg = loadHDBackground(name, idx, suffix);
    DWORD shown = GetTickCount() - t0;
    if (!bg || !bg->isAnimated)
    {
        printf("HDSTREAM %s not an animation\n", arg);
        if (bg)
            freeHDBackground(bg);
        return;
    }
    int steps = 0, holds = 0, backwards = 0, prev = 0;
    while (!bg->allFramesLoaded && steps < 2000)
    {
        Sleep(80);
        steps++;
        bool moved = updateHDBackgroundAnimation(bg, 0.08f);
        if (!moved)
            holds++;
        else if (bg->currentFrame < prev && bg->currentFrame != 0)
            backwards++;
        prev = bg->currentFrame;
    }
    DWORD all = GetTickCount() - t0;
    printf("HDSTREAM %s first frame shown after %lu ms; all %d frames decoded after %lu ms "
           "(%d steps of 80 ms, %d held waiting for a frame, %d out of order)\n",
           arg, shown, bg->frameCount, all, steps, holds, backwards);
    freeHDBackground(bg);
    srand(12345);
    DWORD worstFree = 0;
    for (int n = 0; n < 30; n++)
    {
        HDBackgroundInfo* b = loadHDBackground(name, idx, suffix);
        Sleep(rand() % 300);
        if (b)
            updateHDBackgroundAnimation(b, 0.08f);
        DWORD f0 = GetTickCount();
        freeHDBackground(b);
        DWORD f = GetTickCount() - f0;
        if (f > worstFree)
            worstFree = f;
    }
    printf("HDSTREAM %s 30 load/free cycles during decoding: OK, slowest free %lu ms\n", arg, worstFree);
}

// --hd-once: play a background as a one-shot animation (the floor-hole break) in
// 20 ms steps and print the frames shown, to check it stops on its last frame.
static void hdOnceTest(const char* arg, const char* name, int idx, const char* suffix)
{
    printf("HDONCE %s exists=%s\n", arg, hdCustomBackgroundExists(name, idx, suffix) ? "yes" : "no");
    HDBackgroundInfo* bg = loadHDBackground(name, idx, suffix);
    if (!bg || !bg->isAnimated)
    {
        printf("HDONCE %s not an animation\n", arg);
        if (bg)
            freeHDBackground(bg);
        return;
    }
    bg->playOnce = true;
    std::string shown = "0";
    int steps = 0;
    for (; steps < 400 && !bg->finished; steps++)
    {
        Sleep(20);
        if (updateHDBackgroundAnimation(bg, 0.02f))
            shown += " " + std::to_string(bg->currentFrame);
    }
    for (int extra = 0; extra < 10; extra++)        // must stay on the last frame
        if (updateHDBackgroundAnimation(bg, 0.08f))
            shown += " " + std::to_string(bg->currentFrame);
    printf("HDONCE %s frames=%d at %d ms shown: %s; finished=%s after %d steps of 20 ms, final frame %d\n", arg,
           bg->frameCount, (int)(bg->frameTime * 1000.0f + 0.5f), shown.c_str(), bg->finished ? "yes" : "no",
           steps, bg->currentFrame);
    freeHDBackground(bg);
}

// --hd-loop / --hd-overlay / --hd-bgoverlay / --hd-bgoverlay2: play an animation (or
// the camera overlay, or a background overlay) for three rounds of its frames once all are
// decoded and print the frames shown, to check loop_from.txt.
static void hdLoopTest(const char* arg, const char* name, int idx, const char* suffix, const char* overlay)
{
    HDBackgroundInfo* bg = overlay ? loadHDOverlay(name, idx, overlay) : loadHDBackground(name, idx, suffix);
    if (!bg || !bg->isAnimated)
    {
        printf("HDLOOP %s not an animation\n", arg);
        if (bg)
            freeHDBackground(bg);
        return;
    }
    for (int wait = 0; wait < 500 && bg->streamer && bg->streamer->ready.load() < bg->frameCount; wait++)
        Sleep(20);
    std::string shown = "0";
    for (int step = 0; step < 3 * bg->frameCount; step++)
        if (updateHDBackgroundAnimation(bg, bg->frameTime))
            shown += " " + std::to_string(bg->currentFrame);
    printf("HDLOOP %s%s%s frames=%d channels=%d size=%dx%d loopFrom=%d shown: %s\n", arg, overlay ? " " : "",
           overlay ? overlay : "", bg->frameCount, bg->channels, bg->width, bg->height, bg->loopFrom + 1,
           shown.c_str());
    freeHDBackground(bg);
}

// TatouHd: --hd-lookup NAME:IDX[:SUFFIX] ... runs the camera background lookup
// (loadHDBackground) for each argument, prints what was chosen and exits.
// --hd-preload NAME:COUNT runs the per-floor preload instead; --hd-stream
// NAME:IDX[:SUFFIX] tests background frame decoding (hdStreamTest); --hd-once
// NAME:IDX:SUFFIX tests one-shot playback (hdOnceTest). Test aid only.
void hdLookupCommandLineCheck(int argc, char* argv[])
{
    // Also a marker: the custom HD patchers find this string in the exe to tell
    // our TatouHd builds (and their flavour) from other exes of the same name.
#ifdef AITD_RANDOMIZER_EDITION
    printf(HDBG_TAG "TatouHd build: flavor=edition\n");
#else
    printf(HDBG_TAG "TatouHd build: flavor=scenario\n");
#endif
    if (argc == 0 && __argv)           // WinMain passes no arguments
    {
        argc = __argc;
        argv = __argv;
    }
    // --hd-model HQR:NUM...: check models_custom/<HQR>_<NUM>.bin (hdCustomModel.cpp) and exit
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--hd-model") != 0)
            continue;
        for (int k = i + 1; k < argc; k++)
        {
            char hqr[32] = "";
            int num = -1, v = 0, p = 0, g = 0;
            if (sscanf_s(argv[k], "%31[^:]:%d", hqr, (unsigned)sizeof(hqr), &num) != 2)
            {
                printf("HDMODEL bad argument %s (expected HQR:NUM)\n", argv[k]);
                continue;
            }
            const std::string why = hdCheckCustomModelFile(hqr, num, &v, &p, &g);
            if (why.empty())
                printf("HDMODEL %s:%d ok vertices=%d primitives=%d groups=%d\n", hqr, num, v, p, g);
            else
                printf("HDMODEL %s:%d refused: %s\n", hqr, num, why.c_str());
        }
        fflush(stdout);
        exit(0);
    }
    int first = -1;
    bool preload = false, stream = false, once = false, loop = false;
    const char* overlay = nullptr;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--hd-lookup") == 0 || strcmp(argv[i], "--hd-preload") == 0 ||
            strcmp(argv[i], "--hd-stream") == 0 || strcmp(argv[i], "--hd-once") == 0 ||
            strcmp(argv[i], "--hd-loop") == 0 || strcmp(argv[i], "--hd-overlay") == 0 ||
            strcmp(argv[i], "--hd-bgoverlay") == 0 || strcmp(argv[i], "--hd-bgoverlay2") == 0)
        {
            preload = strcmp(argv[i], "--hd-preload") == 0;
            stream = strcmp(argv[i], "--hd-stream") == 0;
            once = strcmp(argv[i], "--hd-once") == 0;
            loop = strcmp(argv[i], "--hd-loop") == 0;
            overlay = strcmp(argv[i], "--hd-overlay") == 0 ? "overlay" :
                      strcmp(argv[i], "--hd-bgoverlay") == 0 ? "bgoverlay" :
                      strcmp(argv[i], "--hd-bgoverlay2") == 0 ? "bgoverlay2" : nullptr;
            first = i + 1;
            break;
        }
    }
    if (first < 0)
        return;

    g_remasterConfig.graphics.enableHDBackgrounds = true;
    for (int i = first; i < argc; i++)
    {
        char name[64] = "", suffix[32] = "";
        int idx = -1;
        if (sscanf_s(argv[i], "%63[^:]:%d:%31s", name, (unsigned)sizeof(name), &idx, suffix, (unsigned)sizeof(suffix)) < 2)
        {
            printf("HDLOOKUP bad argument %s (expected NAME:IDX[:SUFFIX])\n", argv[i]);
            continue;
        }
        if (preload)
        {
            preloadFloorHDBackgrounds(0, idx, name);
            for (int c = 0; c < idx; c++)
            {
                HDBackgroundInfo* bg = getPreloadedHDBackground(c, nullptr);
                printf("HDLOOKUP preload %s:%d cached=%s%s\n", name, c, bg ? "yes" : "no",
                       bg && bg->isCustom ? " custom" : "");
            }
            clearPreloadedHDBackgrounds();
            continue;
        }
        if (stream)
        {
            hdStreamTest(argv[i], name, idx, suffix[0] ? suffix : nullptr);
            continue;
        }
        if (once)
        {
            hdOnceTest(argv[i], name, idx, suffix[0] ? suffix : nullptr);
            continue;
        }
        if (loop || overlay)
        {
            hdLoopTest(argv[i], name, idx, suffix[0] ? suffix : nullptr, overlay);
            continue;
        }
        DWORD t0 = GetTickCount();
        HDBackgroundInfo* bg = loadHDBackground(name, idx, suffix[0] ? suffix : nullptr);
        DWORD ms = GetTickCount() - t0;
        if (!bg)
            printf("HDLOOKUP %s none (vanilla 320x200) %lu ms\n", argv[i], ms);
        else
        {
            printf("HDLOOKUP %s %s %s %dx%dx%d frames=%d loaded=%d %lu ms\n", argv[i],
                   bg->isCustom ? "custom" : "rehaunted", bg->isAnimated ? "animated" : "still",
                   bg->width, bg->height, bg->channels, bg->frameCount, bg->loadedFrameCount, ms);
            freeHDBackground(bg);
        }
    }
    fflush(stdout);
    exit(0);
}
#endif

// Try to load HD version of background
// Returns nullptr if no HD version found
HDBackgroundInfo* loadHDBackground(const char* backgroundName, int cameraIdx, const char* suffix)
{
    TRAIL("loadHDBackground %s cam %d %s", backgroundName ? backgroundName : "?", cameraIdx, suffix ? suffix : "");
#ifdef AITD_HD_REPAINT_BUILD
    // TatouHd overlays load whatever the setting (they are only drawn while HD
    // backgrounds are on), so a synced one is ready at the right frame when HD
    // backgrounds are switched on mid-cycle.
    if (!isHDBackgroundEnabled() && strcmp(s_customAnimPrefix, "overlay") != 0)
#else
    if (!isHDBackgroundEnabled())
#endif
    {
        return nullptr;
    }

    // Try archive first, then fall back to filesystem
    ensureArchiveOpen();

    // Build the archive-relative animation prefix (e.g. "anim_CAMERA03_015/")
    char animPrefix[256];
    if (suffix && suffix[0] != '\0')
    {
        snprintf(animPrefix, sizeof(animPrefix), "anim_%s_%03d_%s/",
                 backgroundName, cameraIdx, suffix);
    }
    else
    {
        snprintf(animPrefix, sizeof(animPrefix), "anim_%s_%03d/",
                 backgroundName, cameraIdx);
    }

    // Also build the filesystem directory path for fallback
    char animDirPath[512];
    snprintf(animDirPath, sizeof(animDirPath), "%s/%.*s",
             getHDBackgroundFolder(),
             (int)(strlen(animPrefix) - 1), animPrefix); // strip trailing '/'

#ifdef AITD_HD_REPAINT_BUILD
    // TatouHd: the custom animation folder comes first, then the custom still,
    // then ReHaunted's own lookups. Custom frames load through ReHaunted's
    // filesystem path below, pointed at backgrounds_custom/.
    char customAnimDir[512];
    const bool isCustomAnim = !scanCustomAnimation(backgroundName, cameraIdx, suffix,
                                                   customAnimDir, sizeof(customAnimDir)).empty();
    if (isCustomAnim)
        snprintf(animDirPath, sizeof(animDirPath), "%s", customAnimDir);
    else if (HDBackgroundInfo* custom = loadCustomBackground(backgroundName, cameraIdx, suffix))
        return custom;
#endif

    printf(HDBG_TAG "Looking for animated background: %s (name='%s', camera=%d)\n", 
           animDirPath, backgroundName, cameraIdx);

    // ---- Try animated background from archive ----
    std::vector<const HDArchiveEntry*> archiveAnimEntries;
#ifdef AITD_HD_REPAINT_BUILD
    if (HDArchive::isOpen() && !isCustomAnim)
#else
    if (HDArchive::isOpen())
#endif
    {
        archiveAnimEntries = HDArchive::listByPrefix(animPrefix);
    }

    // Choose source: archive entries or filesystem scan
    size_t totalFrames = 0;
    std::vector<std::string> animFiles; // only used for filesystem fallback

    if (!archiveAnimEntries.empty())
    {
        totalFrames = archiveAnimEntries.size();
        printf(HDBG_OK "Found animated background in archive: %s with %zu frames\n",
               animPrefix, totalFrames);
    }
    else
    {
        // Filesystem fallback for animation directory
        animFiles = scanAnimationDirectory(animDirPath);
        totalFrames = animFiles.size();
        if (totalFrames > 0)
        {
            printf(HDBG_OK "Found animated background directory: %s with %zu frames\n",
                   animDirPath, totalFrames);
        }
    }

    if (totalFrames > 0)
    {
        // Allocate frame pointers array
        unsigned char** frames = (unsigned char**)malloc(sizeof(unsigned char*) * totalFrames);
        if (!frames)
        {
            return nullptr;
        }

        // Initialize all frame pointers to nullptr
        for (size_t i = 0; i < totalFrames; i++)
        {
            frames[i] = nullptr;
        }

        int width = 0, height = 0, channels = 0;
        int loadedFrames = 0;

        // Determine minimum frames needed for smooth playback
        size_t thirtyPercent = totalFrames * 3 / 10;
        size_t minFramesToLoad = (totalFrames < 10) ? totalFrames : 
                                  (thirtyPercent > 3 ? thirtyPercent : 3);
#ifdef AITD_HD_REPAINT_BUILD
        if (isCustomAnim)
            minFramesToLoad = 1;            // the rest streams in (streamCustomFrames)
#endif

        printf(HDBG_TAG "Pre-loading first %zu frames for immediate playback...\n", minFramesToLoad);

        for (size_t i = 0; i < minFramesToLoad && i < totalFrames; i++)
        {
            int frameWidth, frameHeight, frameChannels;
            unsigned char* frameData = nullptr;

            if (!archiveAnimEntries.empty())
            {
                frameData = loadFromArchiveEntry(archiveAnimEntries[i],
                                                 &frameWidth, &frameHeight, &frameChannels);
            }
            else
            {
                char frameFilePath[512];
                snprintf(frameFilePath, sizeof(frameFilePath), "%s/%s",
                         animDirPath, animFiles[i].c_str());
                frameData = loadSingleFrame(frameFilePath,
                                            &frameWidth, &frameHeight, &frameChannels);
            }

            if (!frameData)
            {
                printf(HDBG_WARN "Failed to load initial frame %zu" CON_RESET "\n", i);
                continue;
            }

            if (loadedFrames == 0)
            {
                width = frameWidth;
                height = frameHeight;
                channels = frameChannels;
            }
            else if (frameWidth != width || frameHeight != height || frameChannels != channels)
            {
                printf(HDBG_WARN "Frame %zu has inconsistent dimensions (%dx%d, %d channels), expected (%dx%d, %d channels)" CON_RESET "\n",
                       i, frameWidth, frameHeight, frameChannels, width, height, channels);
                stbi_image_free(frameData);
                continue;
            }

            frames[i] = frameData;
            loadedFrames++;
        }

        if (loadedFrames == 0 || frames[0] == nullptr)
        {
            for (size_t i = 0; i < totalFrames; i++)
            {
                if (frames[i])
                {
                    stbi_image_free(frames[i]);
                }
            }
            free(frames);
            printf(HDBG_ERR "Failed to load any frames from animated background" CON_RESET "\n");
        }
        else
        {
            printf(HDBG_OK "Loaded %d initial frames, starting playback...\n", loadedFrames);

            bool allInitialFramesLoaded = (loadedFrames >= (int)totalFrames);

#ifdef AITD_HD_REPAINT_BUILD
            if (!allInitialFramesLoaded && !isCustomAnim)
#else
            if (!allInitialFramesLoaded)
#endif
            {
                printf(HDBG_TAG "Loading remaining %zu frames...\n", totalFrames - loadedFrames);

                for (size_t i = minFramesToLoad; i < totalFrames; i++)
                {
                    int frameWidth, frameHeight, frameChannels;
                    unsigned char* frameData = nullptr;

                    if (!archiveAnimEntries.empty())
                    {
                        frameData = loadFromArchiveEntry(archiveAnimEntries[i],
                                                         &frameWidth, &frameHeight, &frameChannels);
                    }
                    else
                    {
                        char frameFilePath[512];
                        snprintf(frameFilePath, sizeof(frameFilePath), "%s/%s",
                                 animDirPath, animFiles[i].c_str());
                        frameData = loadSingleFrame(frameFilePath,
                                                    &frameWidth, &frameHeight, &frameChannels);
                    }

                    if (!frameData)
                    {
                        printf(HDBG_WARN "Failed to load frame %zu" CON_RESET "\n", i);
                        continue;
                    }

                    if (frameWidth != width || frameHeight != height || frameChannels != channels)
                    {
                        printf(HDBG_WARN "Frame %zu has inconsistent dimensions, skipping" CON_RESET "\n", i);
                        stbi_image_free(frameData);
                        continue;
                    }

                    frames[i] = frameData;
                    loadedFrames++;
                }
            }

            HDBackgroundInfo* bgInfo = (HDBackgroundInfo*)SAFE_MALLOC(sizeof(HDBackgroundInfo), __FILE__, __LINE__);
            if (!bgInfo)
            {
                for (size_t i = 0; i < totalFrames; i++)
                {
                    if (frames[i])
                    {
                        stbi_image_free(frames[i]);
                    }
                }
                free(frames);
                return nullptr;
            }

            bgInfo->data = frames[0];
            bgInfo->width = width;
            bgInfo->height = height;
            bgInfo->channels = channels;
            bgInfo->isIndexed = false;
            bgInfo->isAnimated = true;
            bgInfo->frameCount = static_cast<int>(totalFrames);
            bgInfo->frames = frames;
            bgInfo->currentFrame = 0;
            bgInfo->frameTimer = 0.0f;
            bgInfo->frameTime = 0.08f;
            bgInfo->isPaused = false;
            bgInfo->minFramesForPlayback = static_cast<int>(minFramesToLoad);
            bgInfo->loadedFrameCount = loadedFrames;
            bgInfo->allFramesLoaded = (loadedFrames >= static_cast<int>(totalFrames));
#ifdef AITD_HD_REPAINT_BUILD
            bgInfo->isCustom = isCustomAnim;
            bgInfo->streamer = nullptr;
            bgInfo->playOnce = false;
            bgInfo->loopFrom = 0;
            bgInfo->finished = false;
            if (isCustomAnim)
            {
                // Optional frame_ms.txt in the folder: milliseconds per frame (default 80).
                char msPath[600];
                snprintf(msPath, sizeof(msPath), "%s/frame_ms.txt", animDirPath);
                FILE* msFile = nullptr;
                fopen_s(&msFile, msPath, "r");
                if (msFile)
                {
                    int ms = 0;
                    if (fscanf_s(msFile, "%d", &ms) == 1 && ms >= 10 && ms <= 2000)
                        bgInfo->frameTime = ms / 1000.0f;
                    else
                        printf(HDBG_WARN "%s: expected a number of milliseconds (10-2000)" CON_RESET "\n", msPath);
                    fclose(msFile);
                }
                // Optional loop_from.txt: the frame (1 = first) the loop restarts at;
                // the frames before it play once (an intro).
                char loopPath[600];
                snprintf(loopPath, sizeof(loopPath), "%s/loop_from.txt", animDirPath);
                FILE* loopFile = nullptr;
                fopen_s(&loopFile, loopPath, "r");
                if (loopFile)
                {
                    int from = 0;
                    if (fscanf_s(loopFile, "%d", &from) == 1 && from >= 1 && from <= (int)totalFrames)
                        bgInfo->loopFrom = from - 1;
                    else
                        printf(HDBG_WARN "%s: expected a frame number (1-%d)" CON_RESET "\n", loopPath, (int)totalFrames);
                    fclose(loopFile);
                }
            }
            if (isCustomAnim)
                printf(HDBG_OK "Loaded custom animated background: %s (%d/%d frames, %dx%d, %d ms per frame, loop from frame %d)\n",
                       animDirPath, loadedFrames, (int)totalFrames, width, height,
                       (int)(bgInfo->frameTime * 1000.0f + 0.5f), bgInfo->loopFrom + 1);
            if (isCustomAnim && !bgInfo->allFramesLoaded)
            {
                std::vector<std::string> paths;
                for (const std::string& f : animFiles)
                    paths.push_back(std::string(animDirPath) + "/" + f);
                bgInfo->streamer = new HDFrameStreamer();
                bgInfo->streamer->worker = std::thread(streamCustomFrames, bgInfo->streamer, frames,
                                                       std::move(paths), width, height, channels);
                printf(HDBG_TAG "Decoding %d more frames in the background\n", (int)totalFrames - loadedFrames);
            }
#endif

            if (bgInfo->allFramesLoaded)
            {
                printf(HDBG_OK "Loaded animated HD background: %s (%dx%d, %d channels, %d/%d frames - READY)\n", 
                       animDirPath, width, height, channels, loadedFrames, (int)totalFrames);
            }
            else
            {
                printf(HDBG_OK "Loaded animated HD background: %s (%dx%d, %d channels, %d/%d frames - STREAMING)\n", 
                       animDirPath, width, height, channels, loadedFrames, (int)totalFrames);
            }

            return bgInfo;
        }
    }

    // No animated background found or failed to load, try static background
    const char* extensions[] = { ".png", ".tga", ".bmp" };

    // ---- Try static background from archive ----
    if (HDArchive::isOpen())
    {
        for (int i = 0; i < 3; i++)
        {
            char entryName[256];
            if (suffix && suffix[0] != '\0')
            {
                snprintf(entryName, sizeof(entryName), "%s_%03d_%s%s",
                         backgroundName, cameraIdx, suffix, extensions[i]);
            }
            else
            {
                snprintf(entryName, sizeof(entryName), "%s_%03d%s",
                         backgroundName, cameraIdx, extensions[i]);
            }

            const HDArchiveEntry* entry = HDArchive::findEntry(entryName);
            if (!entry)
                continue;

            int width, height, channels;
            unsigned char* imageData = loadFromArchiveEntry(entry, &width, &height, &channels);
            if (!imageData)
            {
                printf(HDBG_ERR "Failed to load HD background from archive: %s" CON_RESET "\n", entryName);
                continue;
            }

            int scale = g_remasterConfig.graphics.backgroundScale;
            int expectedWidth = 320 * scale;
            int expectedHeight = 200 * scale;

            if (width != expectedWidth || height != expectedHeight)
            {
                printf(HDBG_WARN "HD background %s has unexpected resolution %dx%d (expected %dx%d)" CON_RESET "\n",
                       entryName, width, height, expectedWidth, expectedHeight);
            }

            HDBackgroundInfo* bgInfo = (HDBackgroundInfo*)SAFE_MALLOC(sizeof(HDBackgroundInfo), __FILE__, __LINE__);
            if (!bgInfo)
            {
                stbi_image_free(imageData);
                return nullptr;
            }

            bgInfo->data = imageData;
            bgInfo->width = width;
            bgInfo->height = height;
            bgInfo->channels = channels;
            bgInfo->isIndexed = false;
            bgInfo->isAnimated = false;
            bgInfo->frameCount = 1;
            bgInfo->frames = nullptr;
            bgInfo->currentFrame = 0;
            bgInfo->frameTimer = 0.0f;
            bgInfo->frameTime = 0.08f;
            bgInfo->isPaused = false;
            bgInfo->allFramesLoaded = true;
            bgInfo->minFramesForPlayback = 1;
            bgInfo->loadedFrameCount = 1;
#ifdef AITD_HD_REPAINT_BUILD
            bgInfo->isCustom = false;
            bgInfo->streamer = nullptr;
            bgInfo->playOnce = false;
            bgInfo->loopFrom = 0;
            bgInfo->finished = false;
#endif

            printf(HDBG_OK "Loaded HD background from archive: %s (%dx%d, %d channels)\n",
                   entryName, width, height, channels);

            return bgInfo;
        }
    }

    // ---- Filesystem fallback for static background ----
    char filePath[512];

    for (int i = 0; i < 3; i++)
    {
        if (suffix && suffix[0] != '\0')
        {
            snprintf(filePath, sizeof(filePath), "%s/%s_%03d_%s%s", 
                     getHDBackgroundFolder(), backgroundName, cameraIdx, suffix, extensions[i]);
        }
        else
        {
            snprintf(filePath, sizeof(filePath), "%s/%s_%03d%s", 
                     getHDBackgroundFolder(), backgroundName, cameraIdx, extensions[i]);
        }

        FILE* testFile = nullptr;
        fopen_s(&testFile, filePath, "rb");
        if (!testFile)
        {
            continue;
        }
        fclose(testFile);

        int width, height, channels;
        unsigned char* imageData = loadSingleFrame(filePath, &width, &height, &channels);

        if (!imageData)
        {
            printf(HDBG_ERR "Failed to load HD background: %s" CON_RESET "\n", filePath);
            continue;
        }

        int scale = g_remasterConfig.graphics.backgroundScale;
        int expectedWidth = 320 * scale;
        int expectedHeight = 200 * scale;

        if (width != expectedWidth || height != expectedHeight)
        {
            printf(HDBG_WARN "HD background %s has unexpected resolution %dx%d (expected %dx%d)" CON_RESET "\n",
                   filePath, width, height, expectedWidth, expectedHeight);
        }

        HDBackgroundInfo* bgInfo = (HDBackgroundInfo*)SAFE_MALLOC(sizeof(HDBackgroundInfo), __FILE__, __LINE__);
        if (!bgInfo)
        {
            stbi_image_free(imageData);
            return nullptr;
        }

        bgInfo->data = imageData;
        bgInfo->width = width;
        bgInfo->height = height;
        bgInfo->channels = channels;
        bgInfo->isIndexed = false;
        bgInfo->isAnimated = false;
        bgInfo->frameCount = 1;
        bgInfo->frames = nullptr;
        bgInfo->currentFrame = 0;
        bgInfo->frameTimer = 0.0f;
        bgInfo->frameTime = 0.08f;
        bgInfo->isPaused = false;
        bgInfo->allFramesLoaded = true;
        bgInfo->minFramesForPlayback = 1;
        bgInfo->loadedFrameCount = 1;
#ifdef AITD_HD_REPAINT_BUILD
        bgInfo->isCustom = false;
        bgInfo->streamer = nullptr;
        bgInfo->playOnce = false;
        bgInfo->loopFrom = 0;
        bgInfo->finished = false;
#endif

        printf(HDBG_OK "Loaded HD background: %s (%dx%d, %d channels)\n", 
               filePath, width, height, channels);

        return bgInfo;
    }

    // No HD background found
    return nullptr;
}
// Free HD background data
void freeHDBackground(HDBackgroundInfo* bgInfo)
{
    if (bgInfo)
    {
#ifdef AITD_HD_REPAINT_BUILD
        // stop the frame decoder before the frames go (waits for at most one frame)
        if (bgInfo->streamer)
        {
            bgInfo->streamer->cancel.store(true, std::memory_order_relaxed);
            if (bgInfo->streamer->worker.joinable())
                bgInfo->streamer->worker.join();
            delete bgInfo->streamer;
            bgInfo->streamer = nullptr;
        }
#endif
        if (bgInfo->isAnimated && bgInfo->frames)
        {
            // Free all animation frames
            for (int i = 0; i < bgInfo->frameCount; i++)
            {
                if (bgInfo->frames[i])
                {
                    PROTECTED_CALL({
                        UNTRACK_ALLOCATION(bgInfo->frames[i], "stbi_image_free");
                        stbi_image_free(bgInfo->frames[i]);
                    }, "stbi_image_free HD background frame");
                    bgInfo->frames[i] = nullptr; // Explicitly null each frame
                }
            }
            free(bgInfo->frames);
            bgInfo->frames = nullptr;
            bgInfo->data = nullptr; // data points to first frame, already freed
            bgInfo->frameCount = 0; // Reset frame count
            bgInfo->currentFrame = 0; // Reset current frame
            bgInfo->isAnimated = false; // Clear animation flag
        }
        else if (bgInfo->data)
        {
            // Free single frame
            PROTECTED_CALL({
                UNTRACK_ALLOCATION(bgInfo->data, "stbi_image_free");
                stbi_image_free(bgInfo->data);
                bgInfo->data = nullptr; // Prevent double-free
            }, "stbi_image_free HD background");
        }

        // Clear all remaining fields before freeing the structure
        bgInfo->width = 0;
        bgInfo->height = 0;
        bgInfo->channels = 0;
        bgInfo->allFramesLoaded = false;
        bgInfo->isPaused = false;
        bgInfo->frameTimer = 0.0f;

        SAFE_FREE(bgInfo, __FILE__, __LINE__);
    }
}

// Update animated background frame based on delta time
// Returns true if frame changed
bool updateHDBackgroundAnimation(HDBackgroundInfo* bgInfo, float deltaTime)
{
    // Don't animate if not ready or paused
    // Allow animation once minimum frames are loaded (not just all frames)
    if (!bgInfo || !bgInfo->isAnimated || bgInfo->frameCount <= 1 || bgInfo->isPaused)
    {
        return false;
    }

    // Check if we have enough frames loaded to start animating
    if (bgInfo->loadedFrameCount < (int)bgInfo->minFramesForPlayback)
    {
        return false;
    }

    // Cap deltaTime to prevent speed-up when the game catches up after loading
    // or when frames are delayed (e.g., camera transition). This ensures the
    // animation never advances more than one frame per update.
    if (deltaTime > bgInfo->frameTime)
    {
        deltaTime = bgInfo->frameTime;
    }

    bgInfo->frameTimer += deltaTime;

    if (bgInfo->frameTimer >= bgInfo->frameTime)
    {
#ifdef AITD_HD_REPAINT_BUILD
        // Custom animation still decoding: play what is decoded, hold at its end.
        if (bgInfo->streamer)
        {
            const int ready = bgInfo->streamer->ready.load(std::memory_order_acquire);
            bgInfo->loadedFrameCount = ready;
            if (ready < bgInfo->frameCount)
            {
                int next = bgInfo->currentFrame + 1;
                while (next < ready && bgInfo->frames[next] == nullptr)
                    next++;
                if (next >= ready)
                    return false;                 // timer stays due: advance once it is decoded
                bgInfo->frameTimer = 0.0f;
                bgInfo->currentFrame = next;
                bgInfo->data = bgInfo->frames[next];
                return true;
            }
            bgInfo->allFramesLoaded = true;       // all decoded: the normal loop below
        }
#endif
        // Advance to next frame - reset timer cleanly to avoid drift
        bgInfo->frameTimer = 0.0f;

        int nextFrame = (bgInfo->currentFrame + 1) % bgInfo->frameCount;
#ifdef AITD_HD_REPAINT_BUILD
        // One-shot animation: hold the last frame once it has had its time.
        if (bgInfo->playOnce && nextFrame == 0)
        {
            bgInfo->finished = true;
            return false;
        }
        // loop_from.txt: the loop restarts after the intro frames.
        if (nextFrame == 0)
            nextFrame = bgInfo->loopFrom;
#endif

        // Find next valid frame (skip null frames)
        int attempts = 0;
        while (bgInfo->frames[nextFrame] == nullptr && attempts < bgInfo->frameCount)
        {
            nextFrame = (nextFrame + 1) % bgInfo->frameCount;
#ifdef AITD_HD_REPAINT_BUILD
            if (nextFrame == 0)
                nextFrame = bgInfo->loopFrom;
#endif
            attempts++;
        }

        // Only update if we found a valid frame
        if (bgInfo->frames[nextFrame] != nullptr)
        {
            bgInfo->currentFrame = nextFrame;
            bgInfo->data = bgInfo->frames[nextFrame];
            return true;
        }
    }

    return false;
}

#ifdef AITD_HD_REPAINT_BUILD
bool setHDBackgroundFrame(HDBackgroundInfo* bgInfo, int frame)
{
    if (!bgInfo || !bgInfo->isAnimated || !bgInfo->frames || bgInfo->frameCount <= 0)
        return false;
    int ready = bgInfo->frameCount;
    if (bgInfo->streamer)
        ready = bgInfo->streamer->ready.load(std::memory_order_acquire);
    if (frame >= ready)
        frame = ready - 1;              // not decoded yet: the nearest earlier frame
    if (frame < 0)
        frame = 0;
    while (frame > 0 && bgInfo->frames[frame] == nullptr)
        frame--;
    if (frame == bgInfo->currentFrame || bgInfo->frames[frame] == nullptr)
        return false;
    bgInfo->currentFrame = frame;
    bgInfo->data = bgInfo->frames[frame];
    return true;
}
#endif

// Get current frame data for rendering
unsigned char* getHDBackgroundCurrentFrame(HDBackgroundInfo* bgInfo)
{
    if (!bgInfo)
    {
        return nullptr;
    }

    if (bgInfo->isAnimated && bgInfo->frames && bgInfo->currentFrame < bgInfo->frameCount)
    {
        return bgInfo->frames[bgInfo->currentFrame];
    }

    return bgInfo->data;
}

// HD Frame border storage
static HDFrameBorderInfo g_hdFrameBorders[FRAME_BORDER_COUNT] = {};
static bool g_hdFrameBordersInitialized = false;

// Frame border file names (expected in game folder)
static const char* g_frameBorderFileNames[FRAME_BORDER_COUNT] = {
    "frame_top.png",
    "frame_bottom.png",
    "frame_left.png",
    "frame_right.png"
};

// Load HD frame border textures from PNG files
bool loadHDFrameBorders()
{
    if (!isHDBackgroundEnabled())
    {
        return false;
    }
    
    // Free any previously loaded borders
    freeHDFrameBorders();
    
    bool anyLoaded = false;
    
    for (int i = 0; i < FRAME_BORDER_COUNT; i++)
    {
        g_hdFrameBorders[i].loaded = false;
        g_hdFrameBorders[i].data = nullptr;
        
        // Try to load from game folder
        char filePath[512];
        snprintf(filePath, sizeof(filePath), "%s", g_frameBorderFileNames[i]);
        
        // Check if file exists
        FILE* testFile = nullptr;
        fopen_s(&testFile, filePath, "rb");
        if (!testFile)
        {
            // Try in backgrounds_hd folder
            snprintf(filePath, sizeof(filePath), "%s/%s", getHDBackgroundFolder(), g_frameBorderFileNames[i]);
            fopen_s(&testFile, filePath, "rb");
            if (!testFile)
            {
                continue;
            }
        }
        fclose(testFile);
        
        // Load the image using stb_image
        int width, height, channels;
        unsigned char* imageData = nullptr;

        PROTECTED_CALL({
            imageData = stbi_load(filePath, &width, &height, &channels, 4); // Force RGBA
            if (imageData)
            {
                // Track this allocation for debugging
                TRACK_ALLOCATION(imageData, width * height * 4, "stbi_load");
            }
        }, "stbi_load HD frame border");

        if (!imageData)
        {
            printf(HDBG_ERR "Failed to load HD frame border: %s" CON_RESET "\n", filePath);
            continue;
        }
        
        g_hdFrameBorders[i].data = imageData;
        g_hdFrameBorders[i].width = width;
        g_hdFrameBorders[i].height = height;
        g_hdFrameBorders[i].channels = 4; // We forced RGBA
        g_hdFrameBorders[i].loaded = true;
        
        printf(HDBG_OK "Loaded HD frame border: %s (%dx%d)\n", filePath, width, height);
        anyLoaded = true;
    }
    
    g_hdFrameBordersInitialized = anyLoaded;
    return anyLoaded;
}

// Free HD frame border textures
void freeHDFrameBorders()
{
    for (int i = 0; i < FRAME_BORDER_COUNT; i++)
    {
        if (g_hdFrameBorders[i].data)
        {
            PROTECTED_CALL({
                UNTRACK_ALLOCATION(g_hdFrameBorders[i].data, "stbi_image_free");
                stbi_image_free(g_hdFrameBorders[i].data);
                g_hdFrameBorders[i].data = nullptr; // Prevent double-free
            }, "stbi_image_free frame border");
        }
        g_hdFrameBorders[i].loaded = false;
    }
    g_hdFrameBordersInitialized = false;
}

// Check if HD frame borders are available
bool areHDFrameBordersLoaded()
{
    return g_hdFrameBordersInitialized;
}

// Get HD frame border info for a specific border type
HDFrameBorderInfo* getHDFrameBorder(FrameBorderType type)
{
    if (type >= 0 && type < FRAME_BORDER_COUNT && g_hdFrameBorders[type].loaded)
    {
        return &g_hdFrameBorders[type];
    }
    return nullptr;
}

// Pause HD background animation
void pauseHDBackgroundAnimation(HDBackgroundInfo* bgInfo)
{
    if (bgInfo && bgInfo->isAnimated)
    {
        bgInfo->isPaused = true;
    }
}

// Resume HD background animation
void resumeHDBackgroundAnimation(HDBackgroundInfo* bgInfo)
{
    if (bgInfo && bgInfo->isAnimated)
    {
        bgInfo->isPaused = false;
    }
}

//////////////////////////////////////////////////////////////////////////////
// HD Background Preloading Cache
//////////////////////////////////////////////////////////////////////////////

// Cache structure for preloaded HD backgrounds per floor
static struct {
    std::vector<HDBackgroundInfo*> backgrounds;  // Indexed by camera index
    std::vector<HDBackgroundInfo*> darkBackgrounds; // DARK variants for dark rooms
    int floorNumber;
    char backgroundName[64];
    bool initialized;
} g_hdBackgroundCache = { {}, {}, -1, "", false };

// Preload all HD backgrounds for a floor
// Preload all HD backgrounds for a floor
// NOTE: Only static backgrounds are cached. Animated backgrounds have mutable state
// (currentFrame, frameTimer) that cannot be safely shared between camera switches.
void preloadFloorHDBackgrounds(int floorNumber, int cameraCount, const char* backgroundName)
{
    if (!isHDBackgroundEnabled())
    {
        return;
    }

    // Clear any existing preloaded backgrounds
    clearPreloadedHDBackgrounds();

    if (cameraCount <= 0)
    {
        return;
    }

    printf(HDBG_TAG "Preloading HD backgrounds for floor %d (%d cameras)...\n", floorNumber, cameraCount);

    g_hdBackgroundCache.floorNumber = floorNumber;
    strncpy_s(g_hdBackgroundCache.backgroundName, sizeof(g_hdBackgroundCache.backgroundName), backgroundName, sizeof(g_hdBackgroundCache.backgroundName) - 1);
    g_hdBackgroundCache.backgrounds.resize(cameraCount, nullptr);
    g_hdBackgroundCache.darkBackgrounds.resize(cameraCount, nullptr);
    g_hdBackgroundCache.initialized = true;

    int loadedCount = 0;
    int darkLoadedCount = 0;
    int skippedAnimated = 0;

    for (int i = 0; i < cameraCount; i++)
    {
#ifdef AITD_HD_REPAINT_BUILD
        // TatouHd: a custom animation would be fully decoded here only to be
        // freed again (animations are not cached), so skip it without loading.
        char customAnimDir[512];
        const bool customAnim = !scanCustomAnimation(backgroundName, i, nullptr, customAnimDir, sizeof(customAnimDir)).empty();
        const bool customDarkAnim = !scanCustomAnimation(backgroundName, i, "DARK", customAnimDir, sizeof(customAnimDir)).empty();
        if (customAnim)
            skippedAnimated++;
        HDBackgroundInfo* bg = customAnim ? nullptr : loadHDBackground(backgroundName, i, nullptr);
#else
        // Load normal background
        HDBackgroundInfo* bg = loadHDBackground(backgroundName, i, nullptr);
#endif
        if (bg)
        {
            // Only cache static backgrounds - animated ones have mutable state
            if (bg->isAnimated)
            {
                freeHDBackground(bg);
                skippedAnimated++;
            }
            else
            {
                g_hdBackgroundCache.backgrounds[i] = bg;
                loadedCount++;
            }
        }

        // Also preload DARK variant if it exists (for AITD1 dark rooms)
#ifdef AITD_HD_REPAINT_BUILD
        HDBackgroundInfo* darkBg = customDarkAnim ? nullptr : loadHDBackground(backgroundName, i, "DARK");
#else
        HDBackgroundInfo* darkBg = loadHDBackground(backgroundName, i, "DARK");
#endif
        if (darkBg)
        {
            if (darkBg->isAnimated)
            {
                freeHDBackground(darkBg);
            }
            else
            {
                g_hdBackgroundCache.darkBackgrounds[i] = darkBg;
                darkLoadedCount++;
            }
        }
    }

    printf(HDBG_OK "Preloaded %d/%d HD backgrounds (%d DARK, %d animated skipped) for floor %d\n", 
           loadedCount, cameraCount, darkLoadedCount, skippedAnimated, floorNumber);
}

// Get a preloaded HD background by camera index
HDBackgroundInfo* getPreloadedHDBackground(int cameraIdx, const char* suffix)
{
    if (!g_hdBackgroundCache.initialized)
    {
        return nullptr;
    }

    // Check for DARK variant request
    bool wantDark = (suffix != nullptr && strcmp(suffix, "DARK") == 0);

    if (wantDark)
    {
        if (cameraIdx >= 0 && cameraIdx < (int)g_hdBackgroundCache.darkBackgrounds.size())
        {
            return g_hdBackgroundCache.darkBackgrounds[cameraIdx];
        }
    }
    else
    {
        if (cameraIdx >= 0 && cameraIdx < (int)g_hdBackgroundCache.backgrounds.size())
        {
            return g_hdBackgroundCache.backgrounds[cameraIdx];
        }
    }

    return nullptr;
}

// Clear all preloaded HD backgrounds
void clearPreloadedHDBackgrounds()
{
    if (!g_hdBackgroundCache.initialized)
    {
        return;
    }

    printf(HDBG_TAG "Clearing preloaded HD backgrounds for floor %d\n", g_hdBackgroundCache.floorNumber);

    // Free all normal backgrounds
    for (HDBackgroundInfo* bg : g_hdBackgroundCache.backgrounds)
    {
        if (bg)
        {
            freeHDBackground(bg);
        }
    }
    g_hdBackgroundCache.backgrounds.clear();

    // Free all dark backgrounds
    for (HDBackgroundInfo* bg : g_hdBackgroundCache.darkBackgrounds)
    {
        if (bg)
        {
            freeHDBackground(bg);
        }
    }
    g_hdBackgroundCache.darkBackgrounds.clear();

    g_hdBackgroundCache.floorNumber = -1;
    g_hdBackgroundCache.backgroundName[0] = '\0';
    g_hdBackgroundCache.initialized = false;
}

// Check if HD backgrounds are preloaded for the current floor
bool areHDBackgroundsPreloaded()
{
    return g_hdBackgroundCache.initialized && !g_hdBackgroundCache.backgrounds.empty();
}
