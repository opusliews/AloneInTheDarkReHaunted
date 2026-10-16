#pragma once

// Isolated, compile-time AITD1 randomizer feasibility experiment. The normal
// build supplies no-op implementations and retains original behavior.
void randomizerExperimentApplyWorldSwaps();
char* randomizerExperimentPatchLife(int lifeNum, char* lifeData);
bool randomizerExperimentUsesPatchedLife(int lifeNum);

// The experiment assigns OBJ 13 to OBJ 25's original loose-pickup slot.  The
// replacement model must not block the player, and pickup must be tested
// against the original slot's known-good volume rather than the new model.
bool randomizerExperimentHandleDirectPickup(const tObject* movingActor, const ZVStruct* proposedZv);
bool randomizerExperimentIsNonBlockingPickupActor(const tObject* actor);
