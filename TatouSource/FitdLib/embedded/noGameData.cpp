///////////////////////////////////////////////////////////////////////////////
// AITD randomizer edition of ReHaunted (CMake option AITD_RANDOMIZER_EDITION).
//
// Replaces embeddedData.cpp / embeddedJackData.cpp, which the edition leaves out
// together with every embedded original game file: the game reads the player's
// own PAK/ITD files instead (all loaders fall back to disk). Only ReHaunted's own
// non-game asset stays embedded.
///////////////////////////////////////////////////////////////////////////////

#ifdef AITD_RANDOMIZER_EDITION

#include "embeddedData.h"
#include <cctype>
#include <cstring>

extern const unsigned char embdata_ControllerHint_png[];
extern const unsigned long long embdata_ControllerHint_png_size;

static bool sameName(const char* path, const char* name)
{
    const char* base = path;
    for (const char* p = path; *p; p++)
        if (*p == '/' || *p == 0x5C)    // '/' or backslash
            base = p + 1;
    while (*base && *name)
    {
        if (std::tolower(static_cast<unsigned char>(*base)) != std::tolower(static_cast<unsigned char>(*name)))
            return false;
        base++;
        name++;
    }
    return *base == *name;
}

bool getEmbeddedAitd1File(const char* filename, const unsigned char** outData, size_t* outSize)
{
    if (filename && sameName(filename, "ControllerHint.png"))
    {
        if (outData) *outData = embdata_ControllerHint_png;
        if (outSize) *outSize = static_cast<size_t>(embdata_ControllerHint_png_size);
        return true;
    }
    return false;
}

bool getEmbeddedJackFile(const char*, const unsigned char**, size_t*)
{
    return false;
}

#endif
