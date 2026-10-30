#pragma once

// TatouWormLog: diagnostic log of the tunnel worm (OBJ 218), written to worm_log.txt
// next to the exe. Only in AITD_WORM_LOG builds; the macros are empty otherwise.
// Logging only: game behaviour is unchanged (Research/WormRunaway_2026-10-26.md).

#ifdef AITD_WORM_LOG

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>

static const int kWormObject = 218;

inline unsigned long& wormLogFrameCounter()
{
    static unsigned long s_frame = 0;
    return s_frame;
}

inline void wormLogWrite(const char* fmt, ...)
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
        path = (slash == std::string::npos ? std::string() : path.substr(0, slash + 1)) + "worm_log.txt";
        s_file = fopen(path.c_str(), "w");
        if (s_file)
            fprintf(s_file, "TatouWormLog: OBJ 218 (worm), its controllers 219-221, vars 169-172, player room\n\n");
    }
    if (!s_file)
        return;

    fprintf(s_file, "[%8lu ms f%7lu] ", (unsigned long)GetTickCount(), wormLogFrameCounter());
    va_list args;
    va_start(args, fmt);
    vfprintf(s_file, fmt, args);
    va_end(args);
    fputc('\n', s_file);
    fflush(s_file);
}

// Everything that should be logged when it changes (positions and angles excepted)
struct WormLogState
{
    int floor, playerRoom, playerSlot;
    int v169, v170, v171, v172, v33;
    int slot;                                   // live actor slot, -1 when not loaded
    int wStage, wRoom, wLife, wLifeMode, wTrackMode, wTrackNumber, wPos, wFlags, wBody, wAnim;
    int room, life, lifeMode, anim, trackMode, trackNumber, posInTrack, dynFlags, speed;
    int col0, col1, col2, colBy, hit, hitBy, actionType;
    int c219, c220, c221;                       // live slot of the controllers, -1 when not loaded
    int r219, r220, r221;                       // their world room (-1 = deleted)
};

inline int wormLogVar(int i)
{
    return vars ? vars[i] : -9999;
}

inline int wormLogWorldIdx(int slot)
{
    return (slot >= 0 && slot < (int)ListObjets.size()) ? ListObjets[slot].indexInWorld : -1;
}

// Once per game frame, after all actors ran
inline void wormLogFrame()
{
    if (g_gameId != AITD1 || (int)ListWorldObjets.size() <= 221)
        return;
    unsigned long& frame = wormLogFrameCounter();
    frame++;

    const tWorldObject& w = ListWorldObjets[kWormObject];
    WormLogState s;
    memset(&s, 0, sizeof(s));
    s.floor = g_currentFloor;
    s.playerSlot = currentCameraTargetActor;
    s.playerRoom = currentRoom;
    s.v169 = wormLogVar(169); s.v170 = wormLogVar(170); s.v171 = wormLogVar(171);
    s.v172 = wormLogVar(172); s.v33 = wormLogVar(33);
    s.slot = w.objIndex;
    s.wStage = w.stage; s.wRoom = w.room; s.wLife = w.life; s.wLifeMode = w.lifeMode;
    s.wTrackMode = w.trackMode; s.wTrackNumber = w.trackNumber; s.wPos = w.positionInTrack;
    s.wFlags = w.flags; s.wBody = w.body; s.wAnim = w.anim;
    s.c219 = ListWorldObjets[219].objIndex; s.r219 = ListWorldObjets[219].room;
    s.c220 = ListWorldObjets[220].objIndex; s.r220 = ListWorldObjets[220].room;
    s.c221 = ListWorldObjets[221].objIndex; s.r221 = ListWorldObjets[221].room;

    const tObject* a = nullptr;
    if (s.slot >= 0 && s.slot < (int)ListObjets.size() && ListObjets[s.slot].indexInWorld == kWormObject)
        a = &ListObjets[s.slot];
    if (a)
    {
        s.room = a->room; s.life = a->life; s.lifeMode = a->lifeMode; s.anim = a->ANIM;
        s.trackMode = a->trackMode; s.trackNumber = a->trackNumber; s.posInTrack = a->positionInTrack;
        s.dynFlags = a->dynFlags; s.speed = a->speed;
        s.col0 = wormLogWorldIdx(a->COL[0]); s.col1 = wormLogWorldIdx(a->COL[1]); s.col2 = wormLogWorldIdx(a->COL[2]);
        s.colBy = wormLogWorldIdx(a->COL_BY); s.hit = wormLogWorldIdx(a->HIT); s.hitBy = wormLogWorldIdx(a->HIT_BY);
        s.actionType = a->animActionType;
    }
    else
    {
        s.slot = -1;
    }

    static WormLogState s_last;
    static bool s_haveLast = false;
    static unsigned long s_lastPosFrame = 0;
    const bool changed = !s_haveLast || memcmp(&s, &s_last, sizeof(s)) != 0;

    // Nothing to say away from the caves while the worm is off-world and untouched
    if (!changed && !(a && frame - s_lastPosFrame >= 30))
        return;

    if (changed)
    {
        wormLogWrite("STATE floor %d player room %d | var169 %d var170 %d var171 %d var172 %d var33 %d | "
                     "world: stage %d room %d life %d lifeMode %d track %d/%d pos %d flags 0x%X body %d anim %d | "
                     "ctrl 219 slot %d room %d, 220 slot %d room %d, 221 slot %d room %d",
                     s.floor, s.playerRoom, s.v169, s.v170, s.v171, s.v172, s.v33,
                     s.wStage, s.wRoom, s.wLife, s.wLifeMode, s.wTrackMode, s.wTrackNumber, s.wPos, s.wFlags, s.wBody, s.wAnim,
                     s.c219, s.r219, s.c220, s.r220, s.c221, s.r221);
        if (a)
            wormLogWrite("      LIVE slot %d room %d life %d lifeMode %d anim %d track mode %d num %d pos %d "
                         "dynFlags %d (wall col %s) speed %d | COL %d %d %d COL_BY %d HIT %d HIT_BY %d action %d",
                         s.slot, s.room, s.life, s.lifeMode, s.anim, s.trackMode, s.trackNumber, s.posInTrack,
                         s.dynFlags, (s.dynFlags & 1) ? "on" : "off", s.speed,
                         s.col0, s.col1, s.col2, s.colBy, s.hit, s.hitBy, s.actionType);
        else
            wormLogWrite("      not loaded (world x %d y %d z %d beta %d)", w.x, w.y, w.z, w.beta);
    }
    if (a)
    {
        int px = 0, pz = 0;
        if (s.playerSlot >= 0 && s.playerSlot < (int)ListObjets.size())
        {
            px = ListObjets[s.playerSlot].roomX;
            pz = ListObjets[s.playerSlot].roomZ;
        }
        wormLogWrite("      POS worm room %d (%d, %d, %d) world (%d, %d) beta %d | player room %d (%d, %d)",
                     a->room, a->roomX, a->roomY, a->roomZ, a->worldX, a->worldZ, a->beta, s.playerRoom, px, pz);
        s_lastPosFrame = frame;
    }
    s_last = s;
    s_haveLast = true;
}

#define WORM_LOG_FRAME() wormLogFrame();
#define WORM_LOG_TRACK(actor, ...) do { if ((actor)->indexInWorld == kWormObject) wormLogWrite(__VA_ARGS__); } while (0)

#else

#define WORM_LOG_FRAME()
#define WORM_LOG_TRACK(actor, ...) do { } while (0)

#endif
