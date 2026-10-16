#pragma once

// TatouScenario test-scenario loader (see ReHaunted/SCENARIO_TESTING.md).
// A scenario file sets up inventory, world objects, VARS/CVARS, LIFE operand
// patches and the player's position on top of a fresh new game, so a test can
// start anywhere without replaying. Normal builds compile no-op implementations.

// `TatouScenario.exe --check-scenario FILE...`: parses each file (and reads
// its patch set) with the real parser, prints OK/ERROR per file and exits
// with 0 / 1 without starting the game. Returns -1 when the option is absent.
int scenarioCommandLineCheck();

// AITD1 "New Game": loads the scenario file. Returns true when a scenario is
// armed; the caller then skips the intro and starts the game directly.
bool scenarioArmNewGame();

// End of LoadWorld(): keeps the pristine OBJETS/VARS/DEFINES state.
void scenarioOnWorldLoaded();

// Called once per PlayWorld frame before found-life and LIFE processing.
// `gameplay` is false for non-interactive PlayWorld runs (intro), where
// hotkey requests are dropped.
void scenarioMainLoopTick(bool gameplay);

// LIFE operand patches declared by the scenario (`lifeword`). Patched scripts
// are forced through the bytecode interpreter.
char* scenarioPatchLife(int lifeNum, char* lifeData);
bool scenarioForcesBytecode(int lifeNum);

// Hotkeys (safe to call from the input handler).
void scenarioRequestReapply(); // F10: reload the file, reset to a new game, apply
void scenarioRequestCapture(); // F7: write the current state as a scenario file
void scenarioRequestNudge(int direction); // F6: +1 forward, -1 back (Shift+F6), past actors
void scenarioToggleInfiniteAmmo();        // F5: rifle/revolver ammo never runs out

// F8 inspector colour of a search zone ("F2:R0:HC19"), from search_zones.txt in the active
// patch set (randomized run) or scenarios/search_zones_vanilla.txt (Generated/Generator/
// zone_table.py): 0 = default red, 1 = green (an item is still inside), 2 = orange (vanilla
// run only: a vanilla item zone already emptied, or a zone a seed can fill).
int scenarioZoneColor(const char* zoneKey);

// A file dropped on the game window. A .scn file becomes the active scenario
// and is applied at once in game (as F10); in the menus, by the next New Game.
void scenarioDropFile(const char* path);

// Patch set: if the active scenario's `patchset` folder holds a replacement
// for `filename` (e.g. "LISTLIFE.PAK", "OBJETS.ITD"; case-insensitive, path
// stripped), returns it. The PAK/ITD loaders then skip the disk copy and the
// embedded copy. The data stays valid for the lifetime of the process.
bool scenarioOverrideFile(const char* filename, const unsigned char** data, size_t* size);

// Status message overlay, drawn inside the ImGui frame.
void scenarioRender(int outputWidth, int outputHeight);
