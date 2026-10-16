///////////////////////////////////////////////////////////////////////////////
// Alone In The Dark Re-Haunted
// Copyright (C) 2026 Infogrames / Spacefarer Retro Remasters LLC
// Based on FITD by yaz0r, Re-haunted is released under GPL
// Author: Jake Jackson (jake@spacefarergames.com)
//
// ReShade compatibility diagnostics
///////////////////////////////////////////////////////////////////////////////

#pragma once

// ReShade Compatibility Diagnostics
// Include this in main.cpp or osystemSDL.cpp before initialization

#ifdef WIN32
#include <windows.h>
#include <stdio.h>

// Call this early in main() or FitdInit()
inline void InitReShadeCompatibility()
{
    // Crash reporting is owned by ExceptionHandler. Installing another
    // top-level filter here used to replace the detailed handler and overwrite
    // its report with generic ReShade advice.

    // A local ReShade installation normally includes ReShade.ini. Merely
    // finding dxgi.dll/d3d11.dll loaded is not evidence of ReShade because the
    // system Direct3D runtime loads those modules too.
    DWORD reshadeAttributes = GetFileAttributesA("ReShade.ini");
    bool foundReShadeConfig = reshadeAttributes != INVALID_FILE_ATTRIBUTES &&
        !(reshadeAttributes & FILE_ATTRIBUTE_DIRECTORY);

    if (foundReShadeConfig)
    {
        printf("=== ReShade Detected ===\n");
        printf("Found: ReShade.ini\n");
        printf("ReShade compatibility mode enabled.\n");
        printf("If crashes occur, check crash_log.txt and crash_dump_*.dmp\n");
        printf("========================\n");
    }
    
    // Set environment variables that can help ReShade
    SetEnvironmentVariableA("RESHADE_DISABLE_PERFORMANCE_MODE", "0");
    SetEnvironmentVariableA("RESHADE_DISABLE_DEPTH", "0"); // Try 1 if crashes
}

#else
// Non-Windows platforms
inline void InitReShadeCompatibility() 
{
    // ReShade is Windows-only
}
#endif
