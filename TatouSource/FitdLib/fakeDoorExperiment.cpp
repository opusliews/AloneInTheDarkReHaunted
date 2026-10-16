#include "fakeDoorExperiment.h"

#ifdef AITD_FAKE_DOOR_EXPERIMENT
#include "nativeLifeHelpers.h"

#include <cstdio>

namespace
{
// LIFE 208 (OBJ 128, BODY 34) and LIFE 209 (OBJ 129, BODY 25) only say
// "It's locked!". Replace them with the opening motion used by the real
// double door 137/138 (LIFE 241/242): same leaf layout, closed at beta 512,
// BODY 34 swings to beta 0 and BODY 25 to beta 1023. State is runtime only.
bool gFakeDoorOpening = false;
}

bool fakeDoorExperimentHandleLife(int lifeNum)
{
    if (g_gameId != AITD1 || (lifeNum != 208 && lifeNum != 209))
        return false;

    if (!gFakeDoorOpening)
    {
        if (life_GetCOL_BY() != 1) // persistent OBJ 1 is the player
            return true;
        gFakeDoorOpening = true;
        life_Sample(4); // door sound used by the real door scripts
        std::printf("[FAKE DOOR TEST] Opening false double door OBJ 128/129 in room 3/0\n");
    }

    const int target = (lifeNum == 208) ? 0 : 1023;
    if (life_GetBETA() != target)
    {
        life_Type(AF_ANIMATED);
        life_SetBeta(target, 120);
        life_DoRotZv();
        if (life_GetBETA() == target)
            life_Type(0);
    }
    return true;
}
#else
bool fakeDoorExperimentHandleLife(int)
{
    return false;
}
#endif
