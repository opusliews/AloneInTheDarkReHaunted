///////////////////////////////////////////////////////////////////////////////
// Alone In The Dark Re-Haunted
// Copyright (C) 2026 Infogrames / Spacefarer Retro Remasters LLC
// Based on FITD by yaz0r, Re-haunted is released under GPL
// Author: Jake Jackson (jake@spacefarergames.com)
//
// HD background renderer declarations
///////////////////////////////////////////////////////////////////////////////

#ifndef _HD_BACKGROUND_RENDERER_H_
#define _HD_BACKGROUND_RENDERER_H_

#include <bgfx/bgfx.h>
#include <vector>

struct HDBackgroundInfo; // Forward declaration

// HD background texture state
extern int g_currentBackgroundWidth;
extern int g_currentBackgroundHeight;
extern bool g_currentBackgroundIsHD;

// Separate HD background texture handle
extern bgfx::TextureHandle g_hdBackgroundTexture;

// Functions for HD background rendering
void updateBackgroundTextureHD(unsigned char* data, int width, int height, int channels);
void recreateBackgroundTexture(int width, int height);
bgfx::TextureHandle getActiveBackgroundTexture();
void cleanupHDBackgroundResources(); // Cleanup function to prevent heap corruption

// Preview data management
extern unsigned char* g_hdBackgroundPreviewData;
extern int g_hdBackgroundPreviewWidth;
extern int g_hdBackgroundPreviewHeight;
void freeHDBackgroundPreviewData();

// Animated HD background management
extern HDBackgroundInfo* g_currentAnimatedHDBackground;
extern bool g_resetAnimationTiming;
void updateAnimatedHDBackground(float deltaTime);
void setCurrentAnimatedHDBackground(HDBackgroundInfo* bgInfo);
void pauseCurrentAnimatedHDBackground();
void resumeCurrentAnimatedHDBackground();

#ifdef AITD_HD_REPAINT_BUILD
// TatouHd: the current camera's animated overlay (nullptr clears). owned: freed when
// replaced. synced: the frame comes from setHDOverlayFrame instead of its own timer.
void setCurrentHDOverlay(HDBackgroundInfo* overlay, bool owned = true, bool synced = false);
// Synced overlay: show this frame (-1: nothing).
void setHDOverlayFrame(int frame);
// false fades the overlay out; true shows it again at once.
void setHDOverlayVisible(bool visible);
bool isHDOverlayActive();
// True while a backgrounds_custom/ image is on screen: HD backgrounds are enabled
// (F1 / detail option, checked live) and the current camera loaded a custom image.
bool isCustomHDBackgroundShown();
// backgrounds_custom/no_masks_<NAME>_<idx>.txt: the camera's vanilla masks are not drawn
// while its custom image is shown (its overlay hides the actors instead). The file lists
// the mask numbers to drop (counted over the camera's viewed rooms in order); empty: all.
void setHDCustomNoMasks(bool noMasks, const std::vector<int>& numbers);
// Called for each mask as the camera's masks are built (number: as in the file).
void noteHDCameraMask(int viewedRoom, int maskIdx, int number);
bool areHDMasksHidden(int viewedRoom, int maskIdx);
// The overlay's current frame, or invalid when there is nothing to draw.
bgfx::TextureHandle getHDOverlayTexture();
// Renderer side (rendererBGFX.cpp): draws the overlay over the finished scene.
void osystem_drawHDOverlay();
// TatouHd: the current camera's background overlays, layer 0 (bgoverlay_) and
// layer 1 (bgoverlay2_, drawn over it); nullptr clears, freed when replaced. Drawn
// over the custom background only, never over actors.
constexpr int kHDBackgroundOverlayLayers = 2;
void setCurrentHDBackgroundOverlay(int layer, HDBackgroundInfo* overlay);
bool isHDBackgroundOverlayActive(int layer);
// A layer's current frame, or invalid when there is nothing to draw.
bgfx::TextureHandle getHDBackgroundOverlayTexture(int layer);
// Renderer side: draws them over the background, before the actors.
void osystem_drawHDBackgroundOverlay();
#endif

// -----------------------------------------------------------------------
// Luminance-guided lantern lighting for HD backgrounds
// Call once per frame from the lantern system with current light state.
//   screenX/Y    : lantern position in screen UV space (0..1)
//   intensity    : effective glow intensity with flicker applied (0..1)
//   lightR/G/B   : warm light color (e.g. 1.0, 0.75, 0.25 for oil lantern)
//   influence    : backgroundLightInfluence from LanternState (0..1)
//   lightRadius  : radius in UV space over which the light falls off (~0.35-0.5)
//   ambientDarken: how deeply unlit shadow areas are darkened (0=black, 1=no change)
// -----------------------------------------------------------------------
void setHDBackgroundLanternUniforms(float screenX, float screenY, float intensity,
                                    float lightR, float lightG, float lightB,
                                    float influence, float lightRadius, float ambientDarken);

#endif // _HD_BACKGROUND_RENDERER_H_