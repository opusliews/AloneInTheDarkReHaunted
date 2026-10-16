#pragma once

// Isolated curiosity build (TatouFake): makes the permanently locked false
// double door OBJ 128/129 in room 3/0 swing open like a real double door.
// Normal builds compile a no-op that never handles anything.
bool fakeDoorExperimentHandleLife(int lifeNum);
