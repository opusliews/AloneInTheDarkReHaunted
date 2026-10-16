# Object Inspector Mode

For the separately configured ReHaunted HD build and its required runtime
assets, see `TATOU_HD.md`. For `TatouScenario.exe` (start a test from a
scenario file, F10 reapply, F7 capture), see `SCENARIO_TESTING.md`.

Object Inspector Mode is a gameplay debug overlay for identifying persistent
AITD1 world objects and room interaction volumes. It is intended to help
correlate an object's appearance and location with its `OBJETS.ITD` ID,
body/model ID, and nearby search/scenario zones.

## Using the inspector

1. Build and run ReHaunted (`Tatou.exe`) with a valid AITD1 game-data working
   directory.
2. Start or load a game and enter normal 3D gameplay.
3. Press **F8** to enable Object Inspector Mode.
4. Press **F8** again to disable it.
5. Press **F9** to toggle God Mode independently of the inspector.

While the inspector is enabled:

- **Left-click** an object label or zone outline to select it. Object labels
  win over zones; overlapping labels use the topmost draw order, and
  overlapping zones use the smallest projected zone (then its stable ID).
- **P** shows or hides the pinned details panel without clearing selection.
- **H** hides only the selected debug label/outline. The game object or zone is
  untouched, and the selected entry remains in the details panel.
- **Y** restores all hidden inspector overlays.
- **O** opens the selected entry's multiline description editor. Use the
  window's **Save** or **Cancel** button.
- For a selected persistent OBJ, use the **Location/support** and
  **Pose/orientation** dropdowns in the details panel, then click **Save
  classification**. Dropdown changes remain temporary until explicitly saved.

When enabled, the top-left status line shows:

```text
OBJECT INSPECTOR [F8]  FLOOR <id> ROOM <id>   GOD [F9]: <ON|OFF>
```

Eligible objects in the current camera view are labeled:

```text
OBJ <persistent-object-id>  BODY <body/model-id>
```

`OBJ` is the object's index in the loaded `OBJETS.ITD` world-object table.
`BODY` is the body/model number used by the active actor.

When the selected persistent object is the active player character, the pinned
details panel adds a live **Active player** section. It displays the current
room-space position, raw 0..1023 `beta` angle, and nearest one of eight facing
directions: N, NE, E, SE, S, SW, W, or NW. The research project's explicit
cardinal convention is:

- `+Z` = North
- `+X` = East
- `-Z` = South
- `-X` = West

The live floor/room, position, and facing values update every frame, including
rotation while the player is standing still. The ordinary persistent-object
floor/room and position fields are labelled separately because they are stored
`OBJETS.ITD` state and need not follow an allocated actor from room to room.
The live section appears only for the active camera-target player OBJ, not for
other actors or inactive player-character records.

### Search and scenario zones

F8 also outlines relevant type-9 volumes in the current room:

- **Red `HC9` boxes** are hard-collision type-9 search candidates. Labels use
  `SEARCH? F<floor>:R<room>:HC<zone-index> TYPE 9 PARAM <value>`.
- **Cyan `SZ9` boxes** are scene type-9 scenario zones. Labels use
  `SCEN F<floor>:R<room>:SZ<zone-index> TYPE 9 PARAM <value>`.

The composite floor, room, table kind, and table index is the stable debug ID.
The original zone parameter is also preserved. `SEARCH?` is intentionally a
candidate classification: AITD1 LIFE logic determines whether a particular
type-9 collision volume is actually used for searching. The inspector does not
invent a stronger association when static zone data alone cannot prove one.
Zone outlines are rendered at 3-pixel thickness. A selected zone is highlighted
and its label is drawn last.

### CSV annotations

The development build directory contains two user-editable catalogs beside
`Tatou.exe`:

- `AITD_objects.csv`: 292 persistent `OBJETS.ITD` records. Its first column is
  `obj_id`; reliable initial object metadata, original angles, separate
  placement/pose classifications, confidence, and provenance are retained;
  `description` is the final column.
- `AITD_zones.csv`: 1,356 statically decoded hard-collision and scenario-zone
  records. Its first column is a composite `zone_id` such as `F0:R0:HC3` or
  `F0:R0:SZ2`; coordinates, dimensions, type, and parameter are retained;
  `description` is the final column.

The executable reads these files once. It never creates, regenerates, or resets
them. Pressing O and then explicitly clicking Save rewrites only the selected
record's description value, using CSV quoting for commas, quotes, and newlines.
The rewrite goes through a temporary file and an atomic replacement. Other
field values and descriptions are preserved. A missing, malformed, duplicate-ID,
or unwritable catalog produces an on-screen error instead of replacement.

Clicking **Save classification** similarly updates only the selected existing
object row. It writes the two dropdown values, marks their source `manual` and
confidence `confirmed`, and leaves the description unchanged. The executable
does not automatically reclassify or regenerate either catalog during startup.

Keep both CSV files with whichever `Tatou.exe` you launch. Normal incremental
builds do not overwrite them. If copying this debugger build to another folder,
copy the two CSVs too. The guarded development-only generator is under the
project root at `Generated/ObjectInspector/generate_catalogs.ps1`; it refuses to overwrite an
existing catalog so hand-written descriptions are not lost.

### God Mode

Press **F9** during gameplay to toggle God Mode. It can be used with F8 on or
off; an on-screen `GOD MODE [F9]: ON` indicator remains visible while enabled.

For AITD1, God Mode protects player-health variable `VAR 21` only while the
active player's LIFE script is executing: decreases are ignored and health is
restored to at least its original value of 20. The actor's `hitForce` is not
modified because AITD1 reuses that field for both received damage and outgoing
attack strength; clearing it would also make the player's attacks harmless.
God Mode instead consumes the active player's incoming `HIT_BY` event before
player LIFE 549 can switch to damage LIFE 553, preventing the backward
hit-reaction animation without suppressing ordinary collision or the player's
attacks. Healing and other variable increases remain allowed. Press F9 again
to restore normal damage and hit reactions. A reaction already started before
God Mode is enabled may finish its current animation once.

## What is labeled

The first version labels an object only when all of these are true:

- it is an active ordinary 3D actor submitted by the current camera's object
  draw loop;
- its model was successfully projected by `AffObjet` during the current frame;
- its projected bounding box intersects the original 320x200 game viewport;
- its `indexInWorld` is a valid index in the loaded world-object table.

This deliberately omits actors with no verified persistent-object association.
It does not guess or display an `OBJ` ID for those actors. It also omits rejected
or off-screen objects, menu frames, special 2D animation paths, and hybrid
objects that do not pass through the ordinary 3D-body path.

## Known limitations

- Labels may overlap when several objects occupy a small screen area; selection
  resolves this deterministically and the selected label is redrawn last.
- Label placement uses the actor's projected 320x200 bounding box scaled to the
  output resolution.
- Capture is based on successful render submission and screen intersection; it
  does not test final per-pixel visibility behind every foreground mask.
- Runtime zone outlines show projected 3D boxes for type-9 hard-collision and
  scene zones only. The CSV catalogs all 1,356 statically decoded room zones,
  including types not displayed, but cannot catalog zones created only by
  dynamic code unless they also exist in the decoded room tables.
- A red `SEARCH?` box is a candidate, not proof that its parameter is globally
  unique or that every LIFE script treats it as searchable.
- God Mode protects AITD1 health damage. It does not suppress deliberately
  scripted endings or unconditional `GAME_OVER` events unrelated to health.
- Selection, focus routing, and description saving were build-validated but
  have not yet been visually exercised in an interactive gameplay session.
- Dropped-item prompt suppression is transient and does not alter the save
  format. On load, overlap with active foundable actors is detected and a safe
  until-separation state is reconstructed; this edge case still needs
  interactive validation.

## Dropped-item pickup guard

The normal `Tatou.exe` prevents a dropped item from immediately reopening its
pickup prompt and trapping the player in a prompt loop. After dropping an
item, move completely clear of its pickup volume; approaching it again restores
the normal prompt. Declining a pickup also applies a per-object five-second
cooldown and requires separation for collision-triggered pickups. This changes
only prompt/collision handling: it does not hide, delete, or relocate the item.

## Implementation and validation handoff

The feature is isolated in `TatouSource/FitdLib/objectInspector.cpp` and
`objectInspector.h`. Dropped-item prompt handling is separately isolated in
`pickupPromptGuard.cpp` and `pickupPromptGuard.h`. Integration points are:

- `input.cpp`: handles non-repeating SDL hotkeys, overlay clicks, and suppresses
  gameplay keyboard input while the description editor owns focus;
- `main.cpp`: captures successfully drawn 3D actors with a valid
  `indexInWorld` and projects current-room type-9 zones;
- `life.cpp`: filters AITD1 `VAR 21` writes while God Mode is active;
- `bgfxGlue.cpp`: resets captured labels at frame start and renders the ImGui
  overlay immediately before `imguiEndFrame()`.

`objectInspectorImplementation.inl` contains selection, ImGui rendering,
catalog parsing/saving, and annotation editor state. Inspector state is kept
separate from game state. The overlay is suppressed unless the play world is
active and the menu is closed. When F8 is disabled, actor/zone capture returns
immediately and no inspector geometry is submitted. The small F9 status remains
visible only while God Mode is enabled.

On 2026-10-12, the `Fitd` target was successfully configured and built with
MSVC 2022, CMake, and Ninja using `RelWithDebInfo`. The resulting local artifact
was:

```text
TatouSource/build/object-inspector/Fitd/Tatou.exe
Size: 18,785,280 bytes
SHA-256: 25A1C1DEDA5317AEC454C42DD54F066EDFC90CBF8FC890A1EA2D14DB01CD51B1
```

No automated CTest tests are registered by this project. The build completed
with existing third-party/project warnings and no inspector-related warnings or
link errors.

To rebuild the existing configured tree from PowerShell without installing
anything:

```powershell
cmd.exe /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build C:\Projects\AITD\ReHaunted\TatouSource\build\object-inspector --target Fitd -j 4'
```

Run `Tatou.exe` with the same game-data working-directory rules described in
the main README and BUILDING documentation. Do not use the pristine
`OriginalGame` directory as a writable test location.

## Engine bug fixes included in `Tatou.exe` (2026-10-13)

`FitdLib/lifeBugfixes.cpp` patches the loaded AITD1 script data in memory, exactly as the randomizer will patch the game files later. LIFE 550 and 522 are forced through the bytecode interpreter so ReHaunted's native copies cannot bypass the patches. Each patch first checks the original bytes and skips with a `[LIFE BUGFIX]` console message on a mismatch.

- **LIFE 550 @848:** the cellar → tunnels arrival `z` changes from 6000 to 5800, out of the return zone.
- **LIFE 550 @1810:** the worm's cellar-block placement is guarded by the lure flag `var_172` instead of `var_33`.
- **LIFE 522:** `var var_172 1` is inserted before `endlife` (the endgame also counts as lured); both early jumps are fixed up.
- **TRACK 40 / 41:** `STOP` moved before `MARK 1`, so Carnby stops before the teleport and no longer slides into a wall in 5/3 or 4/0.

Details and verification: `Research/CellarTunnelTransition_4-0_5-3.md`. `TatouHD.exe` and `TatouTest.exe` were built earlier and do not contain these fixes. A rebuild of either from the current source would include them.
