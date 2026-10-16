///////////////////////////////////////////////////////////////////////////////
// Alone In The Dark Re-Haunted
// Copyright (C) 2026 Infogrames / Spacefarer Retro Remasters LLC
// Based on FITD by yaz0r, Re-haunted is released under GPL
// Author: Jake Jackson (jake@spacefarergames.com)
//
// Status screen, inventory UI, and character sheet
///////////////////////////////////////////////////////////////////////////////

// seg 4

#include "common.h"
#include "configRemaster.h"
#include <math.h>

#ifdef PCLIKE
#include "SDL.h"
#endif


//////////// stuff to move


void blitScreenTatou(void)
{
	/*
    int i;

    for(i=0;i<45120;i++)
    {
        frontBuffer[i] = backbuffer[i];
    }
	*/
}

void FastCopyScreen(void* source, void* dest)
{
	memcpy(dest, source, 64000);
}

void FadeInPhys(int step,int start)
{
    SaveTimerAnim();

    if(fadeState == 2) // only used for the ending ?
    {
    }
    else
    {
        for(int i=0;i<256;i+=step)
        {
			process_events();
            palette_t localPalette;
            SetLevelDestPal(currentGamePalette,localPalette,i);
            setPalette(localPalette);
			osystem_refreshFrontTextureBuffer();
			osystem_drawBackground();
        }
    }

    fadeState = 1;

    RestoreTimerAnim();
}

void FadeOutPhys(int step, int var2)
{
    SaveTimerAnim();

    for(int i=256;i>=0;i-=step)
    {
		process_events();
        palette_t localPalette;
		SetLevelDestPal(currentGamePalette,localPalette,i);
		setPalette(localPalette);
		osystem_refreshFrontTextureBuffer();
		osystem_drawBackground();
    }

    RestoreTimerAnim();
}

#ifdef PCLIKE
void process_events( void )
{
    /* Our SDL event placeholder. */
    SDL_Event event;

    /* Grab all the events off the queue. */
    while( SDL_PollEvent( &event ) ) {

        switch( event.type ) {
        case SDL_KEYDOWN:
            /* Handle key presses. */
            //            handle_key_down( &event.key.keysym );
            break;
        case SDL_QUIT:
            /* Handle quit requests (like Ctrl-c). */
            exit( 0 );
            break;
        }

    }

}
#else
void process_events( void )
{
    osystem_endOfFrame();
	u32 timeIncrease = osystem_startOfFrame();
    assert(timeIncrease);
#ifdef FITD_DEBUGGER
	if(debuggerVar_fastForward)
	{
		timeGlobal+=8;
	}
	else
#endif
	{
        timeGlobal += timeIncrease;
	}
	timer=timeGlobal;
}
#endif

void startChrono(unsigned int* chrono)
{
    *chrono = timer;
}

int evalChrono(unsigned int* chrono)
{
    return(timer-*chrono);
}

// bp = x, bx = y, cx = z
// out
// xOut = dx, yOut = ax
void Rotate(unsigned int x, unsigned int y, unsigned int z, int* xOut, int* yOut)
{
    if(x)
    {
        int var1 = (((cosTable[(x+0x100)&0x3FF] * y) << 1) &0xFFFF0000) -  (((cosTable[x&0x3FF] *z) <<1) & 0xFFFF0000);
        int var2 = (((cosTable[x&0x3FF] * y) << 1) & 0xFFFF0000) + (((cosTable[(x+0x100)&0x3FF] *z) <<1) & 0xFFFF0000);

        *yOut = var1>>16;
        *xOut = var2>>16;
    }
    else
    {
        *xOut = z;
        *yOut = y;
    }
}

void setCameraTarget(int x,int y,int z,int alpha,int beta,int gamma,int time)
{
    int x1;
    int y1;
    int x2;
    int y2;

    Rotate(alpha+0x200,-time,0,&x1,&y1);
    Rotate(beta+0x200,y1,0,&x2,&y2);

    SetPosCamera(x2+x,-x1+y,y2+z);
    SetAngleCamera(alpha,beta,gamma);
}

void playSound(int num)
{
    TRAIL("playSound %d", num);
	if(num == -1)
		return;

	if(!soundEnabled)
		return;

	// Skip re-triggering the same sample if it is still playing.
	// Life script opcodes (LM_SAMPLE, LM_ANIM_SAMPLE) fire every game tick,
	// so without this guard the sound restarts from the beginning each frame.
	if(num == LastSample && osystem_isSamplePlaying())
		return;

	char sampleFileName[256] = "";
	if (g_gameId == TIMEGATE)
	{
		strcpy(sampleFileName, "SAMPLES");
	}
	else
	{
		strcpy(sampleFileName, "LISTSAMP");
	}

	int size = getPakSize(sampleFileName,num);
	if(!size)
		return;

	char* ptr = HQR_Get(listSamp,num);
	if(!ptr)
		return;

	osystem_playSample(ptr, size);
	LastSample = num;
}

// A looping sample (LM_REP_SAMPLE, e.g. the talisman chimes of LIFE 186) belongs to the
// room it was started in. The scripts never stop one (no script uses LM_STOP_SAMPLE); the
// original DOS game stops it when the player leaves the room, not when the item is taken
// (user test in vanilla DOSBox, 2026-10-16; Research/TalismanChimeLoop.md, option B).
static int s_lastLoopingSample = -1;
static int s_loopFloor = -1;
static int s_loopRoom = -1;
static int s_loopActor = -1;          // ListObjets index of the actor whose script started the loop

// Positional volume (option audio.positional_loops): full within LOOP_NEAR world units of the
// source, fading linearly to LOOP_MIN at LOOP_FAR. Unknown source: the original volume.
static const float LOOP_NEAR = 1500.0f;
static const float LOOP_FAR = 8000.0f;
static const float LOOP_MIN = 0.3f;

// The talisman chimes (sample 89) are started by LIFE 186, which runs on the invisible
// F2:R8 room controller OBJ 70 at the room origin; the sound belongs to the talisman, OBJ 110.
static int loopSourceWorldObject(int sample)
{
	return sample == 89 ? 110 : -1;
}

// Source position in room coordinates of the current room; false if unknown or elsewhere.
static bool loopSourcePosition(float& x, float& y, float& z)
{
	const int worldObj = loopSourceWorldObject(s_lastLoopingSample);
	int actor = s_loopActor;
	if (worldObj >= 0)
	{
		if (worldObj >= (int)ListWorldObjets.size())
			return false;
		const tWorldObject& wo = ListWorldObjets[worldObj];
		if (wo.objIndex >= 0)
			actor = wo.objIndex;
		else
		{
			if (wo.stage != g_currentFloor || wo.room != currentRoom || (wo.foundFlag & 0xC000))
				return false;
			x = wo.x; y = wo.y; z = wo.z;
			return true;
		}
	}
	if (actor < 0 || actor >= (int)ListObjets.size())
		return false;
	const tObject& a = ListObjets[actor];
	if (a.indexInWorld < 0 || a.bodyNum < 0 || a.room != currentRoom)
		return false;
	x = a.roomX + a.stepX; y = a.roomY + a.stepY; z = a.roomZ + a.stepZ;
	return true;
}

void stopLoopingSounds()
{
	osystem_stopLoopingSamples();
	s_lastLoopingSample = -1;
	s_loopFloor = s_loopRoom = -1;
	s_loopActor = -1;
}

// Called every gameplay frame: room-change stop, then the positional volume.
void stopLoopingSoundsOnRoomChange()
{
	if (s_lastLoopingSample == -1)
		return;
	if (g_currentFloor != s_loopFloor || currentRoom != s_loopRoom)
	{
		stopLoopingSounds();
		return;
	}
	float factor = 1.0f;
	float sx, sy, sz;
	if (g_remasterConfig.music.positionalLoops && currentCameraTargetActor >= 0 &&
		currentCameraTargetActor < (int)ListObjets.size() && loopSourcePosition(sx, sy, sz))
	{
		const tObject& p = ListObjets[currentCameraTargetActor];
		const float dx = (p.roomX + p.stepX) - sx;
		const float dz = (p.roomZ + p.stepZ) - sz;
		const float d = sqrtf(dx * dx + dz * dz);
		if (d > LOOP_NEAR)
		{
			const float t = (d - LOOP_NEAR) / (LOOP_FAR - LOOP_NEAR);
			factor = t >= 1.0f ? LOOP_MIN : 1.0f - (1.0f - LOOP_MIN) * t;
		}
	}
	osystem_setLoopingVolume(factor);
}

void playSoundLooping(int num)
{
	if(num == -1)
		return;

	if(!soundEnabled)
		return;

	// Skip re-triggering the same looping sample if it is still playing.
	if(num == s_lastLoopingSample && osystem_isSamplePlaying())
		return;

	char sampleFileName[256] = "";
	if (g_gameId == TIMEGATE)
	{
		strcpy(sampleFileName, "SAMPLES");
	}
	else
	{
		strcpy(sampleFileName, "LISTSAMP");
	}

	int size = getPakSize(sampleFileName,num);
	if(!size)
		return;

	char* ptr = HQR_Get(listSamp,num);
	if(!ptr)
		return;

	osystem_playLoopingSample(ptr, size);
	s_lastLoopingSample = num;
	s_loopFloor = g_currentFloor;
	s_loopRoom = currentRoom;
	s_loopActor = currentProcessedActorIdx;
	stopLoopingSoundsOnRoomChange();   // start at the positional volume, not at full volume
}

////////////////////////

