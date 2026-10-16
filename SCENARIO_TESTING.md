# TatouScenario: test scenarios without replaying

`TatouScenario.exe` is a separate build of the inspector executable (F8 inspector and F9 God Mode included). It starts a new game in a state described by a small text file: player position, inventory, items placed in the world, VARS/CVARS, raw object fields and LIFE operand patches. `Tatou.exe` does not contain this feature.

## Running

| How | Scenario file used |
|---|---|
| `TatouScenario.exe` | `scenarios\default.scn` beside the exe |
| `TatouScenario.exe --scenario path\to\file.scn` | that file |
| Drag a `.scn` file onto `TatouScenario.exe` | that file |
| Drag a `.scn` file onto the **running game window** | that file, from then on. In game it is applied at once (like F10); in the menus it is used by the next New Game. |

Choose **New Game** and pick the character as usual. The intro is skipped and the game starts in the attic. On the next frame the scenario is applied and the player is moved. A green line at the bottom of the screen confirms it; a red line reports errors and warnings (details in the console window).

If the file is missing or has a syntax error, New Game starts a normal game and the error is shown.

**Continue** and loading saves work normally. Saves made in a scenario contain the scenario state.

### Checking scenario files without playing

```powershell
.\TatouScenario.exe --check-scenario scenarios\poc_a_empty_search_hc12.scn
.\TatouScenario.exe --check-scenario (Get-ChildItem scenarios\*.scn).FullName   # all of them
```

`--check-scenario` runs the real parser on every listed file and also reads any patch sets they name. It prints `OK` or `ERROR <file> line N: ...` per file and exits (code 1 if any file failed) without starting the game. Use it after editing a scenario.  Windows doesn't expand `*` for programs, so the second line lets PowerShell list the files.

## Hotkeys

| Key | Action |
|---|---|
| **F10** | Re-read the active scenario file (the last one dropped, or the startup one) from disk, reset to a fresh new game and apply it. Use it after dying, after editing the file, or to retry. Works from any game, including a loaded save. |
| **F8 zone colours** | (added 2026-10-15) Search zones (HC9) in the F8 inspector are **green** while an item is still inside (the game's own `object()` test: world object `foundFlag & 0xC000 == 0`). With a seed patch set that has `search_zones.txt`, every other zone stays red. Otherwise (vanilla, or a PoC patch set) `scenarios/search_zones_vanilla.txt` is used: **orange** = a vanilla item zone already emptied, or a zone a seed can fill (Many Search Places), and red = never an item. Tables come from `Generated/Generator/zone_table.py`, and `generate.py` writes `search_zones.txt` into every new seed's patch set. Containers opened through an object rather than a zone (C:0, C:4, C:36, C:45 cabinet, C:267 chest) are listed as `OBJ<n>`; their `OBJ n` label gets the same green/orange. |
| **F5** | Infinite ammo on/off (added 2026-10-15). While on, the rifle (`var 13`) and revolver (`var 92`) counts are topped up to at least 8 / 6 every frame, so they never run out. A yellow "INFINITE AMMO [F5]" label shows at the top right. F7 captures made while it is on record the topped-up `var 13` / `var 92`. Not in the randomizer edition. |
| **F7** | Capture the current state into `scenarios\capture_<date>_<time>.scn` (see below). |
| **F6** / **Shift+F6** | Nudge the player up to 800 units along the facing axis (hold to repeat). User-tested 2026-10-14: it works, but F6 moves the player **backwards** (the intended direction is inverted); Shift+F6 is therefore presumably forward. Left as is by user decision. For getting past a monster that pins a god-mode player. Monsters and other actors are ignored; only the room's walls stop it, checked every 100 units so it never passes through a wall. The overlay shows the distance moved, or "wall ahead/behind". |
| F8 / F9 | Object Inspector / God Mode, as in `Tatou.exe`. F8 shows the player's live position and angle. |

## File format

One command per line. `#` starts a comment. Numbers are decimal (or `0x` hex). Object IDs are `OBJETS.ITD` record numbers, the `OBJ` numbers shown by the F8 inspector. Floors are engine floors (0 = attic … 4 = cellar, 5/6 = caverns), rooms are engine room numbers, as in `F<floor>:R<room>`. Coordinates are room-local, as shown by the inspector's *Active player* panel. Angles are 0..1023.

| Command | Effect |
|---|---|
| `name <text>` | Name shown in the confirmation message. |
| `player <floor> <room> <x> <y> <z> [beta]` | Move the player there (applied last, whatever its position in the file). Uses the same path as the LIFE `changeroom` command, so the floor and room load normally and `fixPlayerStuckInWall()` runs. |
| `give <obj> [<obj> ...]` | Put objects in the inventory silently: in-inventory flag set, removed from the world. The object's found-LIFE is **not** run. |
| `remove <obj> [...]` | Take objects out of the inventory and the world (limbo, floor/room -1). |
| `clear_inventory` | Remove everything except the "Actions" entry (OBJ 2). |
| `in_hand <obj>` | Select an inventory object as the item in hand. |
| `place <obj> <floor> <room> <x> <y> <z> [beta]` | Put an object in the world. Objects with found presentation also get `AF_FOUNDABLE`, exactly like a dropped item, so they can be picked up by walking into them. |
| `taken <obj> [0\|1]` | Set (default) or clear the "ever taken" bit `0x4000` (LIFE `object(x)` / `found` treat the object as already taken). |
| `var <index> <value>` | Set `VARS` entry (`var_<index>` in LifeDISA output). |
| `health <value>` | Same as `var 21 <value>`. |
| `cvar <index> <value>` | Set a `DEFINES`/CVar entry. |
| `obj <id> <field> <value>` | Set a raw world-object field. For the **player**, `flags` also applies to the live actor, like the LIFE `flags` instruction (build `FA3B9DB5` or later). Starts in the caverns need `obj 1 flags 321` (can fall) and `obj 2 found_flag 496` (Jump in the action menu), which LIFE 550 sets on a normal arrival. Fields use the `objects.csv` names: `body flags type_zv found_body found_name found_flag found_life x y z alpha beta gamma stage room life_mode life floor_life anim frame anim_type anim_info track_mode track_number position_in_track`. |
| `patchset <folder>` | Load replacement game files (`*.PAK`, `*.ITD`, e.g. a modified `LISTLIFE.PAK`) from the folder, relative to the scenario file. They are served **in memory** instead of the game's own files. Nothing on disk is changed, and other executables are unaffected. Build folders with `Generated/Patcher/make_patchset.py`. One per scenario; a scenario without it uses the vanilla files again. |
| `baseline file` | Before the other commands, restore all world objects (except the player and OBJ 2), VARS and CVARS to the raw OBJETS/VARS/DEFINES values. This undoes the new-game setup (LIFE 562). F7 writes it when no new game had been started in the session, because the capture's differences are then relative to the raw files. `baseline newgame` (the default) does nothing. |
| `lifeword <life> <byte offset> <expected> <new>` | Patch a 16-bit operand in a LIFE script, e.g. retarget a search zone's award. The patch is applied only if the word currently equals `<expected>`; otherwise it is skipped with an error. Patched scripts are forced through the bytecode interpreter. |

Commands run in file order (the `player` move happens last), so later lines override earlier ones.

### Test header (convention)

Every scenario starts with comment lines that say what it tests and what should happen:

```text
# TEST:   Can we unlock the attic chest after taking the key from the wolf spider in the caves?
# EXPECT: The key opens the chest and the chest gives its item.
```

`TEST` is a question with a yes/no answer. `EXPECT` is the observable result that counts as a pass. The loader ignores these lines; they are for the person running the test and for keeping a list of tests and results.

### Example

```text
name Ground floor with the lamp kit
player 3 5 -400 0 2700 0
clear_inventory
give 13 16 121        # oil lamp, oil can, matchbox
health 20
place 126 3 5 0 0 2700  # record 126 on the floor nearby
```

More examples are in `scenarios\` beside the exe. The source copies are in `Generated/Scenarios/`; copy new ones from there if the build folder is recreated.

### Patch sets

A patch set is a folder of replacement game files produced by the patch engine (`Generated/Patcher/make_patchset.py`). It always includes a `manifest.json` listing what changed and the check results. With `patchset <folder>` in a scenario:
- the files are read when the scenario is loaded (New Game, F10, or a drop on the window) and served by the engine's file loaders instead of the disk or built-in copies;
- they take effect from the reset that follows: scripts, objects and variables are reloaded by the new game, and the text PAK, if included, is reloaded at once;
- `lifeword` patches still apply on top;
- the engine's own LIFE fixes (LIFE 550/522, TRACK 40/41) check the original bytes and skip themselves with a console message if the patch set moved that code.

The patch set stays active for the rest of the session, including for **Continue** and loaded saves, until a scenario without `patchset` is loaded.

## Capture (F7)

The capture file contains:
- the player's floor, room, position and angle;
- the inventory (`clear_inventory` plus `give` lines with item names) and the item in hand;
- every VAR and CVAR that differs from the new-game state;
- every world-object field that differs from the new-game state, including objects in rooms other than the current one. Active actors are included as if they had just left the room.

If a patch set is active, the capture also writes its `patchset` line, so the captured state is reloaded with the same patched files.

If the game folder holds a randomizer install marker (`AITD_Randomizer.json`, written by `Generated/Generator/install.py`), the capture header copies it as `#| ` lines between `# randomizer-marker-begin` and `# randomizer-marker-end`. The AITD Randomizer edition of ReHaunted keeps F7 only, for bug reports. `Generated/Generator/reproduce.py capture.scn` regenerates that seed, checks its hash, and writes the capture with the right `patchset` line (details: `Research/ReHauntedDataHandling.md`, "Bug-report capture").

Capturing works after loading a save as well. Fixed 2026-10-13: it used to fail with "no baseline world table", because loading a save pads the object table to the save format's 300 records. Only the 292 real objects are compared.

Edit it (move an item, drop a key) and load it with `--scenario`, or copy its lines into another file.

The diff baseline is the state right after the game's own new-game setup (LIFE 562). It is recorded the first time a new game starts in the session. If you only loaded a save, the capture diffs against the raw `OBJETS/VARS/DEFINES` files instead and writes `baseline file`. Loading it then restores the raw values before applying the differences.

Captures made before 2026-10-13 build `9653C2F7` lack that line. They lose any value that LIFE 562 changes and that the game later set back to its file value. Seen in practice: door OBJ 22 in F1:R0 came back closed (angle 256) while `var 23` said it was open, so it could not be opened. Add `baseline file` to such captures.

## How it works

1. **New Game** with a valid file arms the scenario and skips the intro. F10 instead resets the running game exactly as `startGame(0, 0)` would, without leaving the game loop (the same in-loop reload that loading a save from the system menu does).
2. The game runs its first frame normally. OBJ 2 executes LIFE 562, the new-game setup (camera target, door states, medusa positions, ...). It takes OBJ 2 ("Actions") into the inventory.
3. Once OBJ 2 is in the inventory, every actor except the player is unloaded, so its world record is written back. The commands then edit the world records, inventory and variables.
4. The player is moved with `setStage()`, the routine behind LIFE `changeroom`. The main loop then loads the floor and room and recreates the actors from the edited world records.

Code: `TatouSource/FitdLib/scenarioTester.cpp`. Hooks:
- `main.cpp` (end of `LoadWorld`);
- `mainLoop.cpp` (per-frame tick before found-life processing);
- `life.cpp` (LIFE patch and native bypass);
- `AITD1.cpp` (New Game);
- `input.cpp` (F6 nudge, F7/F10, file drop);
- `pak.cpp`, `fileAccess.cpp`, `embedded/embeddedDataDispatch.cpp` (patch-set files served before disk and built-in copies);
- `bgfxGlue.cpp` (message overlay).

Normal builds compile no-op versions of every hook.

## Limits

- **State consistency is your job.** Vars, object fields and inventory are set exactly as written; nothing checks that they fit together. For a door to be open, set the vars and object fields the game uses (a capture of a real playthrough shows which).
- `give` does not run the object's found-LIFE. Pickup side effects scripted there must be set with `var`/`obj` lines.
- Monsters are restored from their world records (position, LIFE, animation). A monster in the middle of an attack or a TRACK path resumes from its saved fields, which may not look exactly as it did.
- The first new-game frame always runs in the attic. Its one-time effects (LIFE 562, the first tick of the attic scripts) are part of every scenario.
- Positions are not validated beyond the engine's own unstick routine, which handles walls but not objects. Use F8 or F7 to get real coordinates. Arrival points taken from game scripts are only safe together with what that script does around them. Example: the tunnel->cellar arrival (6800, 0, 4400) lies inside barrel OBJ 201 until the script rolls the barrels away.
- An invalid floor or room makes the engine print an error and can leave the game in a broken state; press F10 after fixing the file.
- Named event macros ("knight defeated", ...) are planned. TRACK changes go through patch sets.

## To-do

1. ~~Switch scenario while running~~: done 2026-10-13 (drop a `.scn` on the game window).
2. **Named events and presets**: `event knight_defeated`, bundles such as "mansion done". Each is derived from the scripts and verified against an F7 capture from a real playthrough.
3. ~~`patchset <folder>`~~: done 2026-10-13.
4. **In-game picker** (later): Shift+F10 lists `scenarios\*.scn` with their `TEST:` lines.
5. Optional: F7 captures start with an empty `# TEST:` / `# EXPECT:` header.

## Build

`AITD_SCENARIO_BUILD=ON` in the `object-inspector` tree produces `TatouScenario.exe`. Afterwards set the option back to `OFF` so the tree builds `Tatou.exe` again:

```powershell
cmd.exe /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake -DAITD_SCENARIO_BUILD=ON C:\Projects\AITD\ReHaunted\TatouSource\build\object-inspector && cmake --build C:\Projects\AITD\ReHaunted\TatouSource\build\object-inspector --target Fitd -j 4'
cmake -DAITD_SCENARIO_BUILD=OFF C:\Projects\AITD\ReHaunted\TatouSource\build\object-inspector
```

## Item reference (objects with found presentation)

| OBJ | Item | Vanilla start (floor/room) | Weight |
|---:|---|---|---:|
| 3 | Rifle | chest LIFE 0 | 30 |
| 5 | Old Indian Cover (rug) | dresser LIFE 1 | 50 |
| 12, 71, 72, 74, 76, 102, 107, 115, 116, 148, 185, 200, 204, 269 | Books | various | 30–50 |
| 13 | Oil Lamp | 0/0 | 30 |
| 15 | Letter | search F0:R0:HC18 | 10 |
| 16, 17 | Oil Can | –, 3/10 | 30 |
| 18 | Box of Cartridges | – | 20 |
| 25 | Bow | 1/0 | 40 |
| 26, 27, 28 | Arrows | – | 30 |
| 37, 53, 108, 141, 144, 163 | Keys | –, –, –, –, 3/3, 3/7 | 10 |
| 38 | Old Cavalry Saber | – | 30 |
| 41 | Saber Blade | – | 15 |
| 46, 94 | First Aid Case | – | 40 |
| 48, 95 | Flask | – | 0 |
| 54, 55 | Small Mirror | – | 20 |
| 56 | Vase | 1/4 | 30 |
| 78, 111, 112 | Daggers | – | 20 |
| 81 | Sword | knight drop | 30 |
| 83 | Very Heavy Statuette | 2/6 | 100 |
| 86 | Notebook | 2/3 | 20 |
| 96 | Jug | 2/5 | 30 |
| 101, 182 | Bullets | –, 4/0 | 10 |
| 103 | Revolver | – | 0 |
| 109, 113, 114 | Parchments | – | 10 |
| 110 | Talisman | 2/8 | 20 |
| 120 | Poker | – | 30 |
| 121, 174 | Matchbox | 2/10, – | 10 |
| 123 | Gramophone | 2/10 | 200 |
| 125, 126, 127 | Records | 3/3, –, – | 10–20 |
| 167 | Biscuit Box | – | 30 |
| 171 | Pot of Soup | 3/8 | 100 |
| 172, 173 | Knives | – | 20 |
| 181 | Box of Shoes | – | 40 |
| 199 | Lighter | 3/13 | 10 |
| 205 | Block (wedge) | 4/0 | 20 |
| 232 | Hook | 6/6 | 20 |
| 268 | Gem | – | 50 |

"–" means the item starts off-world and is awarded by a script. The source is `Generated/ResearchRun/ObjectVars/output/objects.csv`. Weight is `position_in_track`; the carry limit is CVar 2 = 700.
