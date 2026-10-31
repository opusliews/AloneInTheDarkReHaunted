# Bug fixes for ReHaunted 2.4 (AITD1)

This branch, `upstream-bug-fixes`, holds only general bug fixes for
ReHaunted, one commit per fix, on top of upstream ReHaunted 2.4
(`3c230894`). They were developed while building the
[Alone in the Dark Randomizer](https://github.com/opusliews/AITD-Randomizer-And-Rehaunted-HD-Patcher),
whose ReHaunted edition is the fork's `main` branch. None of that
edition's randomizer, HD-repaint, controls or diagnostic features are here.

Each commit can be reviewed and cherry-picked on its own (the two
Smoking Room commits and the two lamp commits build on each other). Every
commit message explains the symptom, the cause and the fix.

Validation labels:

- **Runtime confirmed**: tested in game, in the randomizer's ReHaunted edition.
- **Diagnostic confirmed**: the cause was caught by instrumentation; the
  gameplay regression test was not repeated.
- **Source/build verified**: checked against the original executable or data
  and built; the in-game test is still pending.

All commits build together on upstream 2.4 (Windows, MSVC 2022, Ninja,
RelWithDebInfo). The in-game tests were made with the fork's edition, not with
this branch's build.

## Summary

| Area | Fix | Commit | Validation |
|---|---|---|---|
| Audio | Looping samples stop on room/floor change, save load and leaving gameplay | `bbb0e68e` | Runtime confirmed |
| Lighting | Dark rooms: `lightOff` drives a black mask with the original light circle | `6b60de2b` | Runtime confirmed |
| Lighting | Lamp glow follows var 7 (lit), not the lamp's found body | `79ea7695` | Runtime confirmed |
| Lighting | Only the oil lamp (OBJ 13) is a lantern | `d5b847b7` | Runtime confirmed |
| Stability | Heap overrun in blood and smoke effects; effect cleanup | `68c8faf7` | Diagnostic confirmed |
| Audio | Slow, low-pitched audio on devices not running at 44.1 kHz | `49d54cc4` | Runtime confirmed |
| Rendering | Player shadow left above the collapsed corridor floor | `dd2d068e` | Runtime confirmed |
| Gameplay | Smoking Room smoke: the original cycle and its damage | `74f325b1` | Runtime confirmed |
| Rendering | Smoking Room smoke drawn as the original engine does | `a8035980` | Runtime confirmed |
| HD mode | HD backgrounds follow the setting at once | `56e5707e` | Runtime confirmed |
| Rendering | Model atlases capped (freeze when an arrow is shown) | `1e47e191` | Runtime confirmed |
| HD mode | Save/load screen shows each slot's own thumbnail | `c4f2cdbe` | Runtime confirmed |
| Audio | `sound_then` plays its queued second sample | `7052ad90` | Source/build verified |
| Saving | Object tables larger than 300 are not truncated | `efe72b04` | Runtime confirmed |
| Interaction | Takable items no longer block furniture pushes | `afbfd4cb` | Runtime confirmed |
| Interaction | Dropped items do not reopen their pickup prompt at once; 32-bit cooldown | `4cedd4a8` | Source/build verified |
| Rendering | AITD1 cave water cuts models at the waterline instead of shaking | `436344cd` | Runtime confirmed |
| Game scripts | Cellar/tunnel transition, TRACK 40/41, worm soft-locks | `efdedeff` | Runtime confirmed, except LIFE 550 @1694 |

`7cb14f5d` only changes comments.

## Notes per fix

### Particle effects

`InitSpecialObjet` allocated 184 bytes for the blood spray but wrote
4 + 20 × (6 + 4) = 204, and 244 for smoke while writing 254. Each hit overran
the heap block by 20 bytes, which led to random crashes later (seen in
`createAITD1Mask`). Effects without particle data now use frame -1, so
`DeleteObjet` cannot free HQ key 0. The save cleanup frees every effect's
block, not only smoke's. A build that checked the heap every frame caught the
overrun in the frame of a punch.

### Dark rooms

The two dark mansion rooms and the underground maze stayed visible without a
lit lamp. The original drives darkness from `lightOff` and copies a 30-pixel
light circle around the lamp. ReHaunted guessed darkness from the lamp object,
which misses LIFE 172's rooms. Checked against the original executable's light
routine and the LIFE scripts.

### Audio device rate

The SDL3 SoLoud backend opened a 44.1 kHz application stream but initialized
SoLoud with the device's rate. On a 48 kHz device everything played about 8 %
slow and low. SoLoud now uses the requested stream format, and SDL converts.
The fix is in the bundled SoLoud SDL3 backend.

### Looping samples and `sound_then`

`LM_REP_SAMPLE` loops (for example the talisman chime) never stopped. No AITD1
script stops one, and the original stops it when the player leaves the room
(tested in the original game under DOSBox). `LM_SAMPLE_THEN` stored its second
sample but never played it, so for example the laugh after the library doors
close was missing. Following the original's per-frame sound update, the queued
sample now starts when the first sample's own voice ends (SoLoud mixes several
effects, so "no sound playing" is the wrong test).

### Smoking Room smoke

ReHaunted's mode-4 smoke never drove `FOG_FLAG`, which LIFE 427 needs for the
cough damage. `74f325b1` restores the original timer cycle: damage at 480
ticks, fade after 540, reset at 660, and dousing stops the next cloud.
`a8035980` draws the cloud like the original: 20 translucent spheres with their
own X/Z targets, timer-based growth and shrinking, slow rotation. The fork's
fuller-than-original cloud is not part of this branch.

### Lamp glow

`79ea7695` takes the lamp's lit state from variable 7. The found body is shared
by the Actions object, so an empty hand looked like an unlit lamp. `d5b847b7`
limits AITD1's lantern handling to OBJ 13. The fork's extra light from a lit lamp
that has been put down or thrown is an enhancement and is not included.

### Saves with more than 300 objects

`loadSave` and `makeSaveFile` forced `maxObjects` to 300 and resized the table.
This only matters for game data with more than 300 objects (the randomizer adds
objects); vanilla saves keep the original 300-record layout.

### Furniture pushes

An item under or beside a chest made the chest unpushable. The original DOS
executable behaves the same way, so this is a quality-of-life change rather
than a restoration.

### Pickup prompt

Dropping an item puts it inside the player's collision volume, so walking away
could reopen its prompt at once. The decline cooldown also kept the 32-bit timer
in the signed 16-bit `trackNumber`. The new guard keeps that state outside the
world objects. The save format is unchanged. Still to test in game: large
objects, long sessions, and save/load while standing on a dropped item.

### Cave water

AITD1's `LM_WATER` was handled like the later games' screen shake. In the
original it only moves the model vertex height limit from 10000 to -600
(INDARK.EXE 2493:10ef), so actors in the cave water are cut at the waterline.
The inventory and found-object screens switch the cut off. The later games keep
their shake.

### Game scripts

`lifeBugfixes.cpp` patches the loaded LIFE/TRACK bytecode. Each patch checks
the original bytes first and leaves unknown or already patched data alone. The
patched scripts always go through the bytecode interpreter.

- LIFE 550 @848: cellar → tunnel arrival z 6000 → 5800 (6000 is inside the
  return zone F5:R3:SZ1, which could bounce the player back).
- TRACK 40/41: STOP before MARK 1, so the player does not walk on with
  collision off into a wall after the teleport.
- LIFE 550 @1694/@1810: both worm placements (entering from Jeremy's Study,
  from the cellar) test the lure flag `var_172` instead of the endgame flag
  `var_33`. Without this, returning through the cellar re-blocks 5/3 after the
  worm was lured. After the corridor bridge has collapsed, that is a soft-lock.
  Re-entering from the Study can likewise bring a lured worm back in front of
  the cellar passage.
- LIFE 522: the endgame also sets `var_172`.
- LIFE 460: `test_col 1` before the worm's chase track. A cellar-first visit
  otherwise saves the worm with collision off, and the later chase walks
  through the caves forever.

@1694 is the newest part: the randomizer patches it in the game data, and its
in-game test is still pending. The rest was tested in game.

## Investigated, not fixed here

- 3D actors sometimes disappear during fights until the camera changes (also
  reported for unmodified ReHaunted).
- The Master Bedroom werewolf can get stuck in the window wall.
- Some stock HD backgrounds and masks do not line up exactly with the original
  room geometry.
