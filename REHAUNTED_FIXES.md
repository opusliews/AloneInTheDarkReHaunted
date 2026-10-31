# ReHaunted fixes developed during the AITD Randomizer project

This document inventories the general ReHaunted and AITD1 compatibility fixes
developed while building the
[Alone in the Dark Randomizer](https://github.com/opusliews/AITD-Randomizer-And-ReHaunted-HD-Patcher).
It is intended to give ReHaunted maintainers enough information to reproduce,
review, and selectively reuse the fixes without having to understand the
randomizer-specific features in this fork.

The release described here is
[`randomizer-edition-2026-10-30`](https://github.com/opusliews/AloneInTheDarkReHaunted/releases/tag/randomizer-edition-2026-10-30).
Its upstream merge base is ReHaunted commit `3c230894` (the upstream `main`
revision used for the ReHaunted 2.4 rebase).

## Important scope note

This is a mixed development branch, not a minimal bug-fix branch. It also
contains the randomizer edition, scenario and inspection tools, modern controls,
custom-HD rendering, and diagnostic builds. The focused commits in the table
below are the easiest fixes to review independently. Fixes recorded in
`09915be2` and `06ba92f9` are part of larger consolidation commits and should be
extracted as individual hunks rather than cherry-picked wholesale.

Validation labels used below:

- **Runtime confirmed**: reproduced or exercised in game by the project owner.
- **Diagnostic confirmed**: the cause was caught directly by instrumentation or
  matched against the original executable, but the final gameplay regression
  test was not necessarily repeated.
- **Source/build verified**: supported by source/data analysis and successful
  builds or automated checks; direct gameplay confirmation is still pending.

## Summary

| Area | Fix | Commit | Validation |
|---|---|---|---|
| Stability | Blood and smoke particle heap overruns and effect cleanup | `c7650e96` | Diagnostic confirmed |
| Saving | Do not truncate object tables raised above 300 entries | `09915be2` | Runtime confirmed |
| Interaction | Dropped items no longer immediately reopen their pickup prompt | `09915be2` | Source/build verified |
| QoL (optional) | Takable items no longer make furniture impossible to push (changes original behavior) | `09915be2` | Runtime confirmed |
| Audio | Correct speed and pitch on 48 kHz and multichannel devices | `c696d0c6` | Runtime confirmed |
| Audio | Stop looping samples on room/floor changes, loads, and menus | `0f453779` | Runtime confirmed |
| Audio | Play the queued second sample for `sound_then` | `9c97bd44` | Source/build verified |
| Lighting | Restore original dark-room behavior | `4c5d6253` | Runtime confirmed |
| Lighting | Determine lamp glow from actual lamp state | `17d73d31`, `0a720bdb` | Runtime confirmed |
| Rendering | Restore AITD1's cave-water model cut instead of screen shake | `06ba92f9` | Runtime confirmed |
| Rendering | Hide the stale player shadow during the corridor fall | `7808c401` | Runtime confirmed |
| Gameplay | Restore the Smoking Room smoke cycle and damage | `e94e7074` | Runtime confirmed |
| Rendering | Reproduce the original Smoking Room particle motion | `d44802df` | Runtime confirmed |
| Rendering | Cap pathological model atlases that freeze on arrows | `0c5c6f93` | Runtime confirmed |
| HD mode | Apply the HD-background setting immediately | `78d05318` | Runtime confirmed |
| HD mode | Refresh the save/load thumbnail for every slot | `ea305689` | Runtime confirmed |
| Game scripts | Cellar/tunnel arrival, worm state, and TRACK fixes | `09915be2`, `06ba92f9` | Runtime confirmed |
| Paths | Use the executable directory for scenario/edition assets | `84a788b0` | Runtime confirmed |

## Stability and data integrity

### Particle effects wrote past their heap blocks

**Symptom.** Combat could corrupt the heap and cause a crash later in unrelated
rendering or mask code. Because the production exception handler continued past
heap-corruption exceptions, the eventual crash site could obscure the original
write.

**Cause.** `InitSpecialObjet` allocated 184 bytes for the blood layout but wrote
204 bytes. The older smoke layout similarly allocated 244 bytes while writing
254. Impact and muzzle-flash effects also used frame value `0` even though they
owned no particle block, allowing deletion to free HQ key 0. Save cleanup freed
only one effect type and leaked the other particle blocks.

**Fix.** Commit `c7650e96` sizes allocations from their actual layouts, gives
non-owning effects frame `-1`, and frees every effect that owns a particle block.
The key implementation is in `TatouSource/FitdLib/life.cpp` and
`TatouSource/FitdLib/save.cpp`.

**Evidence.** A strict diagnostic build checked the process heap every gameplay
frame and recorded recent engine events. It located corruption immediately after
particle creation rather than at the later crash site. The allocation sizes also
follow directly from the fields written by the loops.

### Save/load shrank expanded object tables

**Symptom.** Builds that raised AITD1's 300-object limit could lose or corrupt
the added records when saving or loading.

**Cause.** `save.cpp` unconditionally set `maxObjects = 300` and resized
`ListWorldObjets` to that value, even when the running engine intentionally used
a larger table.

**Fix.** Preserve `max(oldNumMaxObj, 300)` and grow the vector only when it is
smaller; never shrink it. The change is in the composite commit `09915be2`.

**Evidence.** Save files with more than 300 objects were exercised successfully
in game. Vanilla 300-object saves retain the original layout.

## Item interaction

### A dropped item could trap the player in its pickup prompt

**Symptom.** Dropping an item places it directly inside the player's collision
volume. Attempting to walk away could immediately reopen the modal pickup prompt,
and declining it could repeatedly reopen it later in a session.

**Cause.** `PutAtObjet` makes the dropped actor findable at the player's exact
position. Collision handling then calls `FoundObjet` automatically. The existing
cooldown reused `tWorldObject::trackNumber`, a signed 16-bit TRACK field, to hold
the 32-bit engine timer. This truncated the timestamp and overwrote unrelated
track state.

**Fix.** `pickupPromptGuard.cpp` stores transient per-object state separately.
A player-dropped object is suppressed until the player fully leaves its pickup
volume. Declined prompts use a wrap-safe 32-bit cooldown. A loaded save
reconstructs the separation guard for overlapping findable actors without
changing the save format.

**Review note.** This implementation is in `09915be2`. Its source and build
behavior were verified, but the complete gameplay matrix (large objects, long
timer values, overlapping drops, and save/load while overlapping) remains the
main outstanding runtime test.

### Quality of life: takable items no longer block furniture pushes

This is not a bug fix. The original DOS game behaves the same way as stock
ReHaunted, and the original game never places an item where it would block a
push. The change is needed when items are shuffled (the randomizer) or
dropped by the player.

**Behavior.** If a dropped or shuffled item lay under a chest or other pushable
object, the furniture could not be moved and the item could become unreachable.

**Mechanism.** The push collision path treats findable item actors as solid
blockers, even though ordinary movement treats them as pickup actors.

**Change.** `pushBlockedByActor` in `anim.cpp` ignores actors with `AF_FOUNDABLE`
when deciding whether a push is possible. It still runs the original collision
query for its script-visible side effects.

**Evidence.** A controlled pair of scenarios placed bullets under the attic
chest. The fixed build allowed the chest to move and the bullets to be taken;
the control build did not. The implementation is in `09915be2`.

## Audio

### Audio was slow and low-pitched on some output devices

**Symptom.** Narration, sound effects, and music could all run roughly 8 percent
slow and low on a 48 kHz Windows output device. Behavior varied with the active
default device.

**Cause.** The SDL3 SoLoud backend opened an application-side 44.1 kHz stereo
stream, then initialized SoLoud with the physical device's reported rate and
channel count. On a 48 kHz device, SoLoud generated 48,000 samples that SDL
interpreted as 44,100 application-side samples. This contradicted SDL3's stream
contract, under which SDL performs conversion between the application format
and device format.

**Fix.** Commit `c696d0c6` initializes SoLoud from the requested stream format,
not the physical device format, in
`TatouSource/ThirdParty/soloud.cmake/soloud/src/backend/sdl3_static/soloud_sdl3_static.cpp`.

**Evidence.** The fix was confirmed at both 44.1 kHz and 48 kHz. Voices, effects,
and synthesized music play at the same correct speed and pitch.

### Looping samples survived room changes, loads, and menus

**Symptom.** The secret-library talisman chime could continue forever after
leaving the room, loading another save, or returning to the menu.

**Cause.** `LM_REP_SAMPLE` created an indefinitely looping SoLoud voice. No AITD1
script uses `LM_STOP_SAMPLE`, and ReHaunted had no lifecycle rule that stopped
the voice. In the original DOS game, the loop continues while the player stays
in the room—even after taking the talisman—but stops on leaving it.

**Fix.** Commit `0f453779` records where a loop started and stops looping voices
when the room or floor changes, when gameplay begins or ends, and when a save is
loaded. One-shot samples are untouched.

**Evidence.** The original behavior was checked in DOS and the fixed behavior
was confirmed in ReHaunted.

### `sound_then` discarded its second sample

**Symptom.** The laugh that should follow the first-floor library doors closing
was absent. Other `LM_SAMPLE_THEN` and `LM_SAMPLE_THEN_REPEAT` sites were affected
as well.

**Cause.** ReHaunted stored `nextSample` but never consumed it. Waiting for a
global "any sample playing" flag would also be incorrect because SoLoud mixes
multiple effects.

**Fix.** Commit `9c97bd44` records the voice handle returned for the first sample.
`processNextSample` runs once per gameplay frame and starts the queued sample
when that specific voice finishes. The repeat flag is preserved for
`LM_SAMPLE_THEN_REPEAT`. Both interpreted and native LIFE paths use the helper.

**Evidence.** The behavior matches the original executable's per-frame queued
sample update. All builds and automated patcher tests passed, but the final
library-laugh gameplay test was still pending at release time.

## Lighting and rendering fidelity

### Dark rooms were incorrectly lit

**Symptom.** The two dark mansion rooms and the underground maze could remain
visible without a lit lamp. Text and messages could also be obscured by the
wrong dimming path.

**Cause.** ReHaunted's rendering path inferred darkness from lamp-object state
and palette treatment rather than honoring the AITD1 `lightOff` state.

**Fix.** Commit `4c5d6253` makes `lightOff` drive a black screen mask with the
original 30-pixel light circle and keeps interface/message rendering readable.

**Evidence.** The original executable's light routine and the relevant LIFE
scripts were inspected. Darkness was runtime-confirmed in both mansion rooms
and the maze.

### Lamp glow used the wrong state and sometimes the wrong object

**Symptom.** An unlit lamp could glow, and holding no item could be mistaken for
holding an unlit lantern. This happened because the special Actions object uses
the same found-body number as the lamp.

**Cause.** The renderer used `foundBody` as both identity and lit state. The
lamp's lit state is actually game variable 7, and AITD1's oil lamp is persistent
object 13.

**Fix.** Commit `17d73d31` keys glow to variable 7. Commit `0a720bdb` restricts
AITD1 lantern handling to object 13. Commit `3b23bb2c` additionally lets a lit
lamp placed or thrown into the room illuminate it; that last change is a useful
enhancement rather than a required minimal fix.

### AITD1 cave water was implemented as screen shake

**Symptom.** Entering the caves shook the camera, while actors standing in water
were drawn fully instead of being cut at the waterline.

**Cause.** AITD1 opcode `LM_WATER` was treated like the later games' shake
effect. In the original AITD1 executable it instead changes the model vertex
height limit from 10000 to -600. Inventory and found-item rendering temporarily
disable that cut.

**Fix.** The current tree implements the original height cut in `renderer.cpp`,
suppresses AITD1 shake in interpreted and native LIFE helpers, and uses
`WaterCutSuspend` around inventory/found-object drawing. These changes are in
the composite commit `06ba92f9`.

**Evidence.** The opcode was checked against original executable disassembly.
Runtime tests confirmed the player and monsters are cut at the waterline, item
screens remain whole, mansion rendering is unaffected, and the camera no longer
shakes on cave entry.

### The player's shadow remained above the collapsed corridor floor

**Symptom.** During the first-floor corridor fall, the player model dropped
through the hole but its planar shadow stayed on the intact floor.

**Cause.** The fall animation lowers model bones while the actor origin remains
at floor level, which is where planar shadows are projected.

**Fix.** Commit `7808c401` hides only the player's planar shadow on the affected
camera after the floor object has been deleted. Other falls and shadows retain
their existing behavior.

### Smoking Room smoke had no gameplay effect

**Symptom.** The demonic ashtray smoke never caused coughing or health loss, and
new short-lived clouds were repeatedly spawned.

**Cause.** ReHaunted's mode-4 smoke was a visual 50-frame effect and never drove
`FOG_FLAG`. LIFE 427 expects the original engine to advance that flag: 1 while a
cloud is active, 2 at the damage moment, 3 after damage, and 0 after deletion.

**Fix.** Commit `e94e7074` restores the original timer-based cycle: damage at
480 ticks, fade after 540, deletion/reset at 660. Dousing prevents the next
cloud while allowing the current cloud to finish, matching the original.

**Evidence.** LIFE 427 and the original engine's special-effect update were
cross-checked. Runtime testing confirmed one five-health cough per cycle and no
new cycle after dousing.

### Smoking Room particles did not resemble the original

**Symptom.** ReHaunted displayed a dense pink/gray blob near the ashtray rather
than a spreading smoke cloud.

**Cause.** The engine generated per-particle speed and lifetime values but did
not use them. With `FOG_FLAG` never set, a new cloud could also be created on
every script run.

**Fix.** Commit `d44802df` follows the original particle model: 20 translucent
spheres, independent X/Z targets, timer-based growth and shrink, and slow cloud
rotation. Commit `3a4a7c73` corrects the added vertical displacement so it is
relative to the ashtray. Commit `3f8f47fb` deliberately makes the cloud fuller
than the original and should be treated as optional visual tuning.

### Flat models generated enormous texture atlases

**Symptom.** With HD textures enabled, first displaying or selecting an arrow
could freeze the game for 5–10 seconds each session.

**Cause.** Projection atlas height was proportional to `rangeY / rangeX` with no
upper bound. Nearly flat arrow models had an X range clamped to 1 and generated
atlases as large as 1024x262144 (about 1 GiB), beyond D3D11's texture limit.

**Fix.** Commit `0c5c6f93` caps projection atlases at 4096, preserves aspect by
scaling both dimensions, inspects archived image headers before full extraction,
and skips/regenerates oversized cached atlases.

**Evidence.** Runtime test 43 reduced the delay to about half a second; the
archive-header follow-up in test 44 removed the remaining arrow-specific delay.

### HD background state did not follow the setting

**Symptom.** Disabling HD detail changed model textures but left the current and
subsequent HD backgrounds active until another reload path happened.

**Cause.** The current camera was not reloaded when the setting changed, and
`loadCamera` continued to consume the floor's HD preload cache while HD mode was
disabled.

**Fix.** Commit `78d05318` reloads the current camera on a setting transition and
uses cached HD backgrounds only while the setting is enabled.

**Evidence.** Runtime testing switched both standard and custom HD backgrounds
off and on immediately, including during an active Smoking Room cloud.

### Save/load previews reused the first slot's image

**Symptom.** In HD mode, moving between save slots could leave every slot showing
the first thumbnail.

**Cause.** The bgfx texture was created with initial data and therefore immutable.
Later `updateTexture2D` calls were ignored when the dimensions did not change.

**Fix.** Commit `ea305689` recreates the preview texture whenever the selected
slot changes.

**Evidence.** Runtime test 55 confirmed that each slot shows its own thumbnail.

## AITD1 LIFE and TRACK corrections

These corrections are applied defensively by `lifeBugfixes.cpp`: the expected
original bytes are checked before any edit, already-patched randomizer data is
accepted, and a mismatch is reported rather than patched blindly. The affected
scripts are forced through the bytecode interpreter so native LIFE replacements
cannot bypass the correction.

### Cellar-to-tunnel transition

- LIFE 550's arrival Z changes from 6000 to 5800. The original point was ten
  units inside the type-10 return zone and could immediately bounce the player
  back to the cellar.
- TRACK 40 and TRACK 41 move `STOP` before their first `MARK`. The old order
  allowed several more walking frames with hard collision disabled after the
  teleport, which could put the player inside a tunnel or cellar wall.

### Worm state and progression

- LIFE 550's cellar-block placement is guarded by lure flag `var_172` rather
  than unrelated `var_33`, so returning after luring the worm does not put it
  back across the route.
- LIFE 522 sets `var_172 = 1` during endgame cleanup, preventing a later cellar
  entry from recreating the blocker after the tree is burned.
- LIFE 460 restores `test_col 1` before TRACK 20. A cellar-first visit runs
  `test_col 0`; that collision state is saved with the unloaded worm. Without
  restoring collision, the later office-route chase misses its stop zone and
  walks indefinitely through the caves.

The transition, lure-state, and runaway-worm behavior were exercised in game.
The fixes originated in `09915be2`; the LIFE 460 correction and compatibility
with already-patched randomizer scripts were added in `06ba92f9`.

## Build-specific path correction

Commit `84a788b0` makes scenario and randomizer-edition builds establish the
executable directory as their working directory. This fixes missing config,
scenario, and HD assets when an executable is launched by drag-and-drop or from
another directory. The behavior was runtime-confirmed, but the commit is scoped
to these special builds; an upstream version should decide whether the same
policy is appropriate for every standard build.

## Enhancements and tools that are not general bug fixes

The following substantial parts of this fork should not be presented as fixes
for stock ReHaunted:

- the randomizer edition's no-embedded-data, steamless, and forced-bytecode
  policies;
- scenario loading/capture, strict heap diagnostics, object inspection, noclip,
  event trails, and worm/atlas logging;
- keyboard and mouse controls, maintained aspect ratio, and door-walking logic;
- `TatouHd`, custom backgrounds, overlays, custom models, and model hiding;
- randomizer drop-glow rendering and randomizer-specific pickup behavior;
- positional attenuation of looping sounds (an optional enhancement built on
  the loop-lifecycle fix);
- the fuller-than-original Smoking Room cloud (`3f8f47fb`);
- room illumination from a lit lamp that has been put down or thrown
  (`3b23bb2c`);
- takable items no longer blocking furniture pushes (`09915be2`; see
  "Quality of life" above): the original game blocks them too.

## Known issues investigated but not fixed here

- 3D actors can disappear during some fights until the camera changes. This was
  also reported against unmodified ReHaunted; the cause remains unresolved.
- The Master Bedroom werewolf can become stuck in the window wall. A proposed
  LIFE change did not solve it and was reverted.
- Some stock HD backgrounds and masks do not align exactly with original room
  geometry. The custom-HD system works around individual scenes but is not a
  general correction for the stock asset set.

## Suggested upstream review order

For small, independently reviewable changes, start with:

1. `c696d0c6` — SDL3/SoLoud stream format.
2. `0c5c6f93` — atlas-size cap and archive header check.
3. `ea305689` — save-slot preview texture lifecycle.
4. `0f453779` — looping-sample lifecycle.
5. `9c97bd44` — queued `sound_then` playback.
6. `c7650e96` — particle allocation and cleanup.
7. `e94e7074` and `d44802df` — smoke gameplay and original rendering.
8. `4c5d6253`, `17d73d31`, and `0a720bdb` — darkness and lamp state.

The save-table, pickup, cave-water, and LIFE/TRACK corrections (and the optional
furniture-push change) are
valuable too, but they need hunk-level extraction from the composite commits.

Useful local review commands:

```bash
git diff 3c230894..randomizer-edition-2026-10-30
git show <commit>
git diff 3c230894..randomizer-edition-2026-10-30 -- TatouSource/FitdLib/<file>
```

