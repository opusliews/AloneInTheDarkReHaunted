///////////////////////////////////////////////////////////////////////////////
// Alone In The Dark Re-Haunted
// Copyright (C) 2026 Infogrames / Spacefarer Retro Remasters LLC
// Based on FITD by yaz0r, Re-haunted is released under GPL
// Author: Jake Jackson (jake@spacefarergames.com)
//
// HD background loading declarations
///////////////////////////////////////////////////////////////////////////////

#ifndef _HD_BACKGROUND_H_
#define _HD_BACKGROUND_H_

#include "common.h"

// HD Background information
struct HDBackgroundInfo
{
    unsigned char* data;          // Current frame data (for single frame) or pointer to frame array
    int width;
    int height;
    int channels;                 // 1 for indexed, 3 for RGB, 4 for RGBA
    bool isIndexed;              // true if paletted, false if RGB/RGBA

    // Animation support
    bool isAnimated;             // true if this is an animated background
    int frameCount;              // Number of frames (1 for non-animated)
    unsigned char** frames;      // Array of frame data pointers (for animated backgrounds)
    int currentFrame;            // Current frame index
    float frameTimer;            // Time accumulator for frame switching
    float frameTime;             // Time per frame (default 0.08s)
    bool isPaused;               // true if animation is paused
    bool allFramesLoaded;        // true if all animation frames are loaded and ready
    int minFramesForPlayback;    // Minimum frames needed before starting playback (default 3)
    int loadedFrameCount;        // Number of frames actually loaded
#ifdef AITD_HD_REPAINT_BUILD
    bool isCustom;               // loaded from backgrounds_custom/ (1:1 with the vanilla frame)
    struct HDFrameStreamer* streamer; // custom animation: frames 1.. decoded on a worker thread
    bool playOnce;               // stop on the last frame instead of looping
    bool finished;               // playOnce animation has shown its last frame
    int loopFrom;                // looping restarts at this frame (loop_from.txt, 1-based in the file)
#endif
};

#ifdef AITD_HD_REPAINT_BUILD
// TatouHd: true while the camera background comes from backgrounds_custom/.
// Such images match the vanilla 320x200 frame 1:1, so the vanilla masks are
// used and ReHaunted's masks_hd/ replacements are skipped.
extern bool g_hdCustomBackgroundActive;

// TatouHd test aid: --hd-lookup NAME:IDX[:SUFFIX]... / --hd-preload NAME:COUNT
// print the background lookup results and exit. No-op without those options.
void hdLookupCommandLineCheck(int argc, char* argv[]);

// TatouHd: true if backgrounds_custom/ has a still or an animation folder for
// this camera and suffix.
bool hdCustomBackgroundExists(const char* backgroundName, int cameraIdx, const char* suffix);

// TatouHd: the animated overlay backgrounds_custom/overlay_<name>_<idx>/ for this
// camera (RGBA frames drawn over the scene, after the actors), or nullptr.
// prefix "bgoverlay": the background overlay bgoverlay_<name>_<idx>/ instead
// (drawn over the background only).
HDBackgroundInfo* loadHDOverlay(const char* backgroundName, int cameraIdx, const char* prefix = "overlay");

// TatouHd: show this frame of an animation (or the nearest earlier decoded one).
// Returns true if the shown frame changed.
bool setHDBackgroundFrame(HDBackgroundInfo* bgInfo, int frame);
#endif

// Try to load HD version of background
// Returns nullptr if no HD version found
// backgroundName: e.g., "CAMERA00"
// cameraIdx: camera index within the PAK
// suffix: optional suffix (e.g., "NOTATOU"), can be nullptr for default
HDBackgroundInfo* loadHDBackground(const char* backgroundName, int cameraIdx, const char* suffix = nullptr);

// Free HD background data
void freeHDBackground(HDBackgroundInfo* bgInfo);

// Check if HD backgrounds are enabled
bool isHDBackgroundEnabled();

// Get the folder path for HD backgrounds
const char* getHDBackgroundFolder();

// Load an image file from the HD backgrounds folder by filename
// Returns pixel data (caller must free with freeHDImageData), or nullptr on failure
unsigned char* loadHDImageFile(const char* filename, int* width, int* height, int* channels);

// Free image data returned by loadHDImageFile or loadImageFile
void freeHDImageData(unsigned char* data);

// Load an image file from an arbitrary filesystem path
// Returns pixel data (caller must free with freeHDImageData), or nullptr on failure
unsigned char* loadImageFile(const char* fullPath, int* width, int* height, int* channels);

// Load an image from a memory buffer (e.g. embedded PNG data)
// Returns pixel data (caller must free with freeHDImageData), or nullptr on failure
unsigned char* loadImageFromMemory(const unsigned char* buffer, int bufferSize, int* width, int* height, int* channels);

// Frame border types
enum FrameBorderType
{
    FRAME_BORDER_TOP = 0,
    FRAME_BORDER_BOTTOM,
    FRAME_BORDER_LEFT,
    FRAME_BORDER_RIGHT,
    FRAME_BORDER_COUNT
};

// HD Frame border information
struct HDFrameBorderInfo
{
    unsigned char* data;
    int width;
    int height;
    int channels;
    bool loaded;
};

// Load HD frame border textures from PNG files
// Returns true if at least one border was loaded
bool loadHDFrameBorders();

// Free HD frame border textures
void freeHDFrameBorders();

// Check if HD frame borders are available
bool areHDFrameBordersLoaded();

// Get HD frame border info for a specific border type
HDFrameBorderInfo* getHDFrameBorder(FrameBorderType type);

// Update animated background frame based on delta time
// Returns true if frame changed
bool updateHDBackgroundAnimation(HDBackgroundInfo* bgInfo, float deltaTime);

// Get current frame data for rendering
unsigned char* getHDBackgroundCurrentFrame(HDBackgroundInfo* bgInfo);

// Pause/Resume animation
void pauseHDBackgroundAnimation(HDBackgroundInfo* bgInfo);
void resumeHDBackgroundAnimation(HDBackgroundInfo* bgInfo);

// HD background preloading per floor
// Preload all HD backgrounds for a floor (call when floor is loaded)
void preloadFloorHDBackgrounds(int floorNumber, int cameraCount, const char* backgroundName);

// Get a preloaded HD background by camera index (returns nullptr if not preloaded)
HDBackgroundInfo* getPreloadedHDBackground(int cameraIdx, const char* suffix = nullptr);

// Clear all preloaded HD backgrounds (call when changing floors)
void clearPreloadedHDBackgrounds();

// Check if HD backgrounds are preloaded for the current floor
bool areHDBackgroundsPreloaded();

#endif // _HD_BACKGROUND_H_
