///////////////////////////////////////////////////////////////////////////////
// AITD Randomizer: optional "maintain aspect ratio" (see keepAspect.h)
///////////////////////////////////////////////////////////////////////////////

#include "keepAspect.h"
#include <SDL.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

extern "C" { extern char homePath[512]; }
extern SDL_Window* gWindowBGFX;

namespace
{
// the game's 320x200 screen
const int ASPECT_W = 16;
const int ASPECT_H = 10;

bool readEnabled()
{
    // the install record, where the game runs (homePath) or next to the exe
    const char* base = SDL_GetBasePath();
    std::string text;
    for (const std::string& dir : { std::string(homePath), std::string(base ? base : "") })
    {
        std::ifstream f(dir + "AITD_Randomizer.json");
        if (f)
        {
            std::stringstream ss;
            ss << f.rdbuf();
            text = ss.str();
            break;
        }
    }
    const size_t key = text.find("\"keep_aspect\"");
    if (key == std::string::npos)
        return false;
    size_t p = text.find(':', key);
    if (p == std::string::npos)
        return false;
    p = text.find_first_not_of(" \t\r\n", p + 1);
    const bool on = p != std::string::npos && text.compare(p, 4, "true") == 0;
    if (on)
        printf("AITD Randomizer: maintain aspect ratio on (16:10, black bars)\n");
    return on;
}
}

bool keepAspectEnabled()
{
    static const bool s_enabled = readEnabled();
    return s_enabled;
}

void keepAspectRect(int winW, int winH, int& x, int& y, int& w, int& h)
{
    x = 0;
    y = 0;
    w = winW;
    h = winH;
    if (!keepAspectEnabled() || winW <= 0 || winH <= 0)
        return;
    if (winW * ASPECT_H > winH * ASPECT_W)
        w = winH * ASPECT_W / ASPECT_H;     // too wide: bars left and right
    else
        h = winW * ASPECT_H / ASPECT_W;     // too tall: bars above and below
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    x = (winW - w) / 2;
    y = (winH - h) / 2;
}

void keepAspectMapMouse(float& x, float& y)
{
    if (!keepAspectEnabled() || !gWindowBGFX)
        return;
    // the rectangle is in pixels, mouse positions are in window points
    int pw = 0, ph = 0, ww = 0, wh = 0;
    SDL_GetWindowSizeInPixels(gWindowBGFX, &pw, &ph);
    SDL_GetWindowSize(gWindowBGFX, &ww, &wh);
    if (pw <= 0 || ph <= 0 || ww <= 0 || wh <= 0)
        return;
    int rx, ry, rw, rh;
    keepAspectRect(pw, ph, rx, ry, rw, rh);
    x -= rx * (float)ww / pw;
    y -= ry * (float)wh / ph;
}

void keepAspectMapEvent(SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_MOTION:
        keepAspectMapMouse(event.motion.x, event.motion.y);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        keepAspectMapMouse(event.button.x, event.button.y);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        keepAspectMapMouse(event.wheel.mouse_x, event.wheel.mouse_y);
        break;
    default:
        break;
    }
}
