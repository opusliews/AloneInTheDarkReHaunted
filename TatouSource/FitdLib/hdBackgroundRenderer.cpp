///////////////////////////////////////////////////////////////////////////////
// Alone In The Dark Re-Haunted
// Copyright (C) 2026 Infogrames / Spacefarer Retro Remasters LLC
// Based on FITD by yaz0r, Re-haunted is released under GPL
// Author: Jake Jackson (jake@spacefarergames.com)
//
// HD background texture rendering with BGFX
///////////////////////////////////////////////////////////////////////////////

#ifndef AITD_UE4
#include "common.h"
#include "hdBackgroundRenderer.h"
#include "hdBackground.h"
#include "hdArchive.h"
#include "configRemaster.h"
#include "consoleLog.h"
#include "memoryManager.h"
#include "exceptionHandler.h"
#include "resourceGC.h"
#include <bgfx/bgfx.h>

// Global variables for HD background tracking
int g_currentBackgroundWidth = 320;
int g_currentBackgroundHeight = 200;
bool g_currentBackgroundIsHD = false;

// Separate HD background texture - NEVER recycle g_backgroundTexture handle!
bgfx::TextureHandle g_hdBackgroundTexture = BGFX_INVALID_HANDLE;

// HD background preview data (RGBA copy for menu preview)
unsigned char* g_hdBackgroundPreviewData = nullptr;
int g_hdBackgroundPreviewWidth = 0;
int g_hdBackgroundPreviewHeight = 0;

// Current animated HD background (kept alive for animation updates)
HDBackgroundInfo* g_currentAnimatedHDBackground = nullptr;

// Flag to reset animation timing on next frame (prevents speed-up after camera transitions)
bool g_resetAnimationTiming = false;

// External references to background texture
extern bgfx::TextureHandle g_backgroundTexture;
extern unsigned char physicalScreen[320 * 200];
extern bool g_bgfxMainResourcesInitialized;

// Get the appropriate background texture based on current mode
bgfx::TextureHandle getActiveBackgroundTexture()
{
    if (g_currentBackgroundIsHD && bgfx::isValid(g_hdBackgroundTexture))
    {
        return g_hdBackgroundTexture;
    }
    return g_backgroundTexture;
}

// Recreate background texture with new dimensions
void recreateBackgroundTexture(int width, int height)
{
    if (!g_bgfxMainResourcesInitialized)
    {
        g_currentBackgroundWidth = width;
        g_currentBackgroundHeight = height;
        g_currentBackgroundIsHD = (width > 320 || height > 200);
        return;
    }
    
    if (width > 320 || height > 200)
    {
        g_currentBackgroundIsHD = true;

        if (g_currentBackgroundWidth != width || g_currentBackgroundHeight != height || !bgfx::isValid(g_hdBackgroundTexture))
        {
            if (bgfx::isValid(g_hdBackgroundTexture))
            {
                bgfx::destroy(g_hdBackgroundTexture);
                g_hdBackgroundTexture = BGFX_INVALID_HANDLE; // Prevent double-free
            }

            g_hdBackgroundTexture = bgfx::createTexture2D(
                width, height, false, 1,
                bgfx::TextureFormat::RGBA8,
                BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP
            );

            printf(HDBGR_OK "HD background texture created: %dx%d\n", width, height);
        }
    }
    else
    {
        g_currentBackgroundIsHD = false;
    }
    
    g_currentBackgroundWidth = width;
    g_currentBackgroundHeight = height;
}

// Free HD background preview data
void freeHDBackgroundPreviewData()
{
    if (g_hdBackgroundPreviewData)
    {
        SAFE_FREE(g_hdBackgroundPreviewData, __FILE__, __LINE__);
    }
    g_hdBackgroundPreviewWidth = 0;
    g_hdBackgroundPreviewHeight = 0;
}

// Cleanup HD background resources
void cleanupHDBackgroundResources()
{
#ifdef AITD_HD_REPAINT_BUILD
    setCurrentHDOverlay(nullptr);
#endif
    PROTECTED_CALL({
        // Flush any deferred GC items before doing direct cleanup
        ResourceGC::flush();

        // Clean up animated background first
        if (g_currentAnimatedHDBackground)
        {
            freeHDBackground(g_currentAnimatedHDBackground);
            g_currentAnimatedHDBackground = nullptr;
        }

        // Clean up HD texture
        if (bgfx::isValid(g_hdBackgroundTexture))
        {
            bgfx::destroy(g_hdBackgroundTexture);
            g_hdBackgroundTexture = BGFX_INVALID_HANDLE;
        }

        // Clean up preview data
        freeHDBackgroundPreviewData();

        // Close HD archive
        HDArchive::close();

        // Reset state
        g_currentBackgroundIsHD = false;
        g_currentBackgroundWidth = 320;
        g_currentBackgroundHeight = 200;
    }, "cleanup HD background resources");
}

// Update background texture with HD data
void updateBackgroundTextureHD(unsigned char* data, int width, int height, int channels)
{
    if (!data || !g_bgfxMainResourcesInitialized)
        return;
    
    if (width != g_currentBackgroundWidth || height != g_currentBackgroundHeight)
    {
        recreateBackgroundTexture(width, height);
    }
    
    if (g_currentBackgroundIsHD)
    {
        if (!bgfx::isValid(g_hdBackgroundTexture))
            return;
        
        // Free previous preview data
        freeHDBackgroundPreviewData();
        
        if (channels == 3)
        {
            unsigned char* rgbaData = (unsigned char*)SAFE_MALLOC((size_t)width * height * 4, __FILE__, __LINE__);
            if (rgbaData)
            {
                for (size_t i = 0; i < (size_t)width * height; i++)
                {
                    rgbaData[i * 4 + 0] = data[i * 3 + 0];
                    rgbaData[i * 4 + 1] = data[i * 3 + 1];
                    rgbaData[i * 4 + 2] = data[i * 3 + 2];
                    rgbaData[i * 4 + 3] = 255;
                }
                
                bgfx::updateTexture2D(g_hdBackgroundTexture, 0, 0, 0, 0, width, height, 
                                      bgfx::copy(rgbaData, (uint32_t)((size_t)width * height * 4)));
                
                // Store copy for menu preview
                g_hdBackgroundPreviewData = rgbaData;
                g_hdBackgroundPreviewWidth = width;
                g_hdBackgroundPreviewHeight = height;
            }
        }
        else if (channels == 4)
        {
            bgfx::updateTexture2D(g_hdBackgroundTexture, 0, 0, 0, 0, width, height, 
                                  bgfx::copy(data, (uint32_t)((size_t)width * height * 4)));

            // Store copy for menu preview
            g_hdBackgroundPreviewData = (unsigned char*)SAFE_MALLOC((size_t)width * height * 4, __FILE__, __LINE__);
            if (g_hdBackgroundPreviewData)
            {
                memcpy(g_hdBackgroundPreviewData, data, (size_t)width * height * 4);
                g_hdBackgroundPreviewWidth = width;
                g_hdBackgroundPreviewHeight = height;
            }
        }
    }
}

// Set the current animated HD background
void setCurrentAnimatedHDBackground(HDBackgroundInfo* bgInfo)
{
    // Free the previous animated background if it exists
    if (g_currentAnimatedHDBackground)
    {
        // Ensure we don't try to set the same background
        if (g_currentAnimatedHDBackground == bgInfo)
        {
            return; // Already set, nothing to do
        }

        // Schedule for deferred free rather than freeing immediately.
        // The caller should have already called ResourceGC::flush() before loading
        // the new background, ensuring this item is freed before the new load begins.
        ResourceGC::scheduleHDBackground(g_currentAnimatedHDBackground);
        g_currentAnimatedHDBackground = nullptr; // Explicitly null before setting new one
    }

    // Set the new background (can be nullptr to clear)
    g_currentAnimatedHDBackground = bgInfo;

    // Reset animation timing so the first frame displays without speed-up
    if (bgInfo && bgInfo->isAnimated)
    {
        bgInfo->frameTimer = 0.0f;
        bgInfo->currentFrame = 0;
        if (bgInfo->frames && bgInfo->frames[0])
        {
            bgInfo->data = bgInfo->frames[0];
        }
        g_resetAnimationTiming = true;
    }
}

#ifdef AITD_HD_REPAINT_BUILD
// TatouHd: animated overlay of the current camera (backgrounds_custom/overlay_*),
// drawn over the scene by osystem_drawHDOverlay. It plays on its own timer and fades
// out over kOverlayFadeTime once setHDOverlayVisible(false) is called; or, synced, it
// shows the frame given by setHDOverlayFrame (nothing for -1).
static HDBackgroundInfo* g_currentHDOverlay = nullptr;
static bool g_hdOverlayOwned = true;     // freed when replaced (false: the caller keeps it)
static bool g_hdOverlaySynced = false;
static bool g_hdOverlayHidden = false;   // synced, no frame to show
static bgfx::TextureHandle g_hdOverlayTexture = BGFX_INVALID_HANDLE;
static float g_hdOverlayAlpha = 1.0f;
static bool g_hdOverlayVisible = true;
static bool g_hdOverlayDirty = true;
static const float kOverlayFadeTime = 0.5f;

// alpha: the fade, multiplied into the frame's alpha
static void uploadOverlayFrame(HDBackgroundInfo* o, bgfx::TextureHandle texture, float alpha)
{
    if (!o || !o->data || !bgfx::isValid(texture))
        return;
    const size_t pixels = (size_t)o->width * o->height;
    const bgfx::Memory* mem = bgfx::alloc((uint32_t)(pixels * 4));
    const int alphaScale = (int)(alpha * 256.0f);
    for (size_t i = 0; i < pixels; i++)
    {
        const unsigned char* src = o->data + i * o->channels;
        unsigned char* dst = mem->data + i * 4;
        dst[0] = src[0];
        dst[1] = src[o->channels > 1 ? 1 : 0];
        dst[2] = src[o->channels > 2 ? 2 : 0];
        dst[3] = (unsigned char)(((o->channels == 4 ? src[3] : 255) * alphaScale) >> 8);
    }
    bgfx::updateTexture2D(texture, 0, 0, 0, 0, (uint16_t)o->width, (uint16_t)o->height, mem);
}

static void uploadHDOverlayFrame()
{
    uploadOverlayFrame(g_currentHDOverlay, g_hdOverlayTexture, g_hdOverlayAlpha);
}

// TatouHd: background overlays of the current camera (backgrounds_custom/bgoverlay_*
// and bgoverlay2_*), drawn over the custom background only: before the actors, and
// again inside the masks that repaint the background over them
// (osystem_drawHDBackgroundOverlay), layer 2 over layer 1. Each plays on its own timer.
static HDBackgroundInfo* g_currentHDBgOverlay[kHDBackgroundOverlayLayers] = {};
static bgfx::TextureHandle g_hdBgOverlayTexture[kHDBackgroundOverlayLayers] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
static bool g_hdBgOverlayDirty[kHDBackgroundOverlayLayers] = { true, true };

void setCurrentHDBackgroundOverlay(int layer, HDBackgroundInfo* overlay)
{
    if (g_currentHDBgOverlay[layer] == overlay)
        return;
    if (g_currentHDBgOverlay[layer])
        freeHDBackground(g_currentHDBgOverlay[layer]);
    if (bgfx::isValid(g_hdBgOverlayTexture[layer]))
    {
        bgfx::destroy(g_hdBgOverlayTexture[layer]);
        g_hdBgOverlayTexture[layer] = BGFX_INVALID_HANDLE;
    }
    g_currentHDBgOverlay[layer] = overlay;
    g_hdBgOverlayDirty[layer] = true;
    if (overlay && g_bgfxMainResourcesInitialized)
        g_hdBgOverlayTexture[layer] = bgfx::createTexture2D((uint16_t)overlay->width, (uint16_t)overlay->height, false, 1,
                                                            bgfx::TextureFormat::RGBA8,
                                                            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
}

bool isHDBackgroundOverlayActive(int layer)
{
    return g_currentHDBgOverlay[layer] != nullptr;
}

bgfx::TextureHandle getHDBackgroundOverlayTexture(int layer)
{
    // only over the custom image, never over the standard background
    if (!isCustomHDBackgroundShown() || !g_currentHDBgOverlay[layer])
        return BGFX_INVALID_HANDLE;
    return g_hdBgOverlayTexture[layer];
}

static void updateHDBackgroundOverlays(float deltaTime)
{
    for (int layer = 0; layer < kHDBackgroundOverlayLayers; layer++)
    {
        HDBackgroundInfo* o = g_currentHDBgOverlay[layer];
        if (!o)
            continue;
        if (updateHDBackgroundAnimation(o, deltaTime))
            g_hdBgOverlayDirty[layer] = true;
        if (g_hdBgOverlayDirty[layer])
        {
            uploadOverlayFrame(o, g_hdBgOverlayTexture[layer], 1.0f);
            g_hdBgOverlayDirty[layer] = false;
        }
    }
}

void setCurrentHDOverlay(HDBackgroundInfo* overlay, bool owned, bool synced)
{
    if (g_currentHDOverlay == overlay)
        return;
    if (g_currentHDOverlay && g_hdOverlayOwned)
        freeHDBackground(g_currentHDOverlay);    // stops its frame decoder first
    if (bgfx::isValid(g_hdOverlayTexture))
    {
        bgfx::destroy(g_hdOverlayTexture);
        g_hdOverlayTexture = BGFX_INVALID_HANDLE;
    }
    g_currentHDOverlay = overlay;
    g_hdOverlayOwned = owned;
    g_hdOverlaySynced = synced;
    g_hdOverlayHidden = synced;              // until the first setHDOverlayFrame
    g_hdOverlayAlpha = 1.0f;
    g_hdOverlayVisible = true;
    g_hdOverlayDirty = true;
    if (overlay && g_bgfxMainResourcesInitialized)
        g_hdOverlayTexture = bgfx::createTexture2D((uint16_t)overlay->width, (uint16_t)overlay->height, false, 1,
                                                   bgfx::TextureFormat::RGBA8,
                                                   BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
}

void setHDOverlayVisible(bool visible)
{
    if (visible && !g_hdOverlayVisible)
    {
        g_hdOverlayAlpha = 1.0f;
        g_hdOverlayDirty = true;
    }
    g_hdOverlayVisible = visible;
}

void setHDOverlayFrame(int frame)
{
    if (!g_currentHDOverlay || !g_hdOverlaySynced)
        return;
    g_hdOverlayHidden = frame < 0;
    if (frame >= 0 && setHDBackgroundFrame(g_currentHDOverlay, frame))
        g_hdOverlayDirty = true;
}

bool isHDOverlayActive()
{
    return g_currentHDOverlay != nullptr;
}

bool isCustomHDBackgroundShown()
{
    return g_remasterConfig.graphics.enableHDBackgrounds && g_currentBackgroundIsHD && g_hdCustomBackgroundActive;
}

static bool g_hdCustomNoMasks = false;
static std::vector<int> g_hdNoMaskNumbers;                  // empty: every mask
static std::vector<std::pair<int, int>> g_hdHiddenMasks;    // (viewed room, mask) of listed ones

void setHDCustomNoMasks(bool noMasks, const std::vector<int>& numbers)
{
    g_hdCustomNoMasks = noMasks;
    g_hdNoMaskNumbers = numbers;
    g_hdHiddenMasks.clear();
}

void noteHDCameraMask(int viewedRoom, int maskIdx, int number)
{
    for (int n : g_hdNoMaskNumbers)
        if (n == number)
            g_hdHiddenMasks.push_back({ viewedRoom, maskIdx });
}

bool areHDMasksHidden(int viewedRoom, int maskIdx)
{
    // with HD off the vanilla background is shown, and it needs its masks
    if (!g_hdCustomNoMasks || !isCustomHDBackgroundShown())
        return false;
    if (g_hdNoMaskNumbers.empty())
        return true;
    for (const auto& m : g_hdHiddenMasks)
        if (m.first == viewedRoom && m.second == maskIdx)
            return true;
    return false;
}

bgfx::TextureHandle getHDOverlayTexture()
{
    // the overlay belongs to the custom image: not over the standard background
    if (!isCustomHDBackgroundShown())
        return BGFX_INVALID_HANDLE;
    if (!g_currentHDOverlay || g_hdOverlayAlpha <= 0.0f || g_hdOverlayHidden)
        return BGFX_INVALID_HANDLE;
    return g_hdOverlayTexture;
}

static void updateHDOverlay(float deltaTime)
{
    HDBackgroundInfo* o = g_currentHDOverlay;
    if (!o)
        return;
    if (!g_hdOverlayVisible && g_hdOverlayAlpha > 0.0f)
    {
        g_hdOverlayAlpha -= deltaTime / kOverlayFadeTime;
        if (g_hdOverlayAlpha < 0.0f)
            g_hdOverlayAlpha = 0.0f;
        g_hdOverlayDirty = true;
    }
    if (g_hdOverlayAlpha <= 0.0f || g_hdOverlayHidden)
        return;
    if (!g_hdOverlaySynced && updateHDBackgroundAnimation(o, deltaTime))
        g_hdOverlayDirty = true;
    if (g_hdOverlayDirty)
    {
        uploadHDOverlayFrame();
        g_hdOverlayDirty = false;
    }
}
#endif

// Update animated HD background each frame
void updateAnimatedHDBackground(float deltaTime)
{
#ifdef AITD_HD_REPAINT_BUILD
    updateHDOverlay(deltaTime);
    updateHDBackgroundOverlays(deltaTime);
#endif
    // Safety check: ensure background is valid before accessing
    if (!g_currentAnimatedHDBackground || !g_currentAnimatedHDBackground->isAnimated)
    {
        return;
    }

    // Additional safety: verify frames array is still valid
    if (!g_currentAnimatedHDBackground->frames || 
        g_currentAnimatedHDBackground->currentFrame >= g_currentAnimatedHDBackground->frameCount)
    {
        return;
    }

    // Update the animation frame
    if (updateHDBackgroundAnimation(g_currentAnimatedHDBackground, deltaTime))
    {
        // Frame changed, verify data is valid before updating texture
        if (g_currentAnimatedHDBackground->data)
        {
            updateBackgroundTextureHD(
                g_currentAnimatedHDBackground->data,
                g_currentAnimatedHDBackground->width,
                g_currentAnimatedHDBackground->height,
                g_currentAnimatedHDBackground->channels
            );
        }
    }
}

// Pause the current animated HD background
void pauseCurrentAnimatedHDBackground()
{
    if (g_currentAnimatedHDBackground && g_currentAnimatedHDBackground->isAnimated)
    {
        pauseHDBackgroundAnimation(g_currentAnimatedHDBackground);
    }
}

// Resume the current animated HD background
void resumeCurrentAnimatedHDBackground()
{
    if (g_currentAnimatedHDBackground && g_currentAnimatedHDBackground->isAnimated)
    {
        resumeHDBackgroundAnimation(g_currentAnimatedHDBackground);
    }
}

#endif // AITD_UE4