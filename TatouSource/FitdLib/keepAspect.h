///////////////////////////////////////////////////////////////////////////////
// AITD Randomizer: optional "maintain aspect ratio" (patcher option, default off)
//
// Enabled by "keep_aspect": true in AITD_Randomizer.json (the patcher's install
// record). On: the whole frame (scene, menus, text, ImGui) is drawn at the size of
// the largest 16:10 rectangle that fits the window, then copied into that rectangle
// with black bars around it, so resizing, maximizing or fullscreen never stretch it.
// Mouse positions are mapped into the rectangle. Off: nothing changes.
// Research/KeepAspect.md
///////////////////////////////////////////////////////////////////////////////

#ifndef _KEEP_ASPECT_H_
#define _KEEP_ASPECT_H_

union SDL_Event;

bool keepAspectEnabled();

// The game rectangle in a window of winW x winH pixels: the centred 16:10 one when the
// option is on, the whole window when it is off
void keepAspectRect(int winW, int winH, int& x, int& y, int& w, int& h);

// Window mouse coordinates (SDL, window points) -> coordinates in the game rectangle
void keepAspectMapMouse(float& x, float& y);

// The same for the position carried by an SDL mouse event (motion, button, wheel)
void keepAspectMapEvent(SDL_Event& event);

#endif
