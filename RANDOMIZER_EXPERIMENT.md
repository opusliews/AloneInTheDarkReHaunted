# TatouTest item-substitution experiment

`TatouTest.exe` is an isolated feasibility build. The normal `Tatou.exe`, the
original game resources, and the inspector CSV catalogs are not altered.

## Running the test

Run:

`TatouSource/build/object-inspector/Fitd/TatouTest.exe`

Start a new game (or use a save made specifically for this experiment). Existing
saves can already contain object positions, inventory/found flags, or opened
container state that mask the substitutions.

The build applies these in-memory AITD1-only changes:

- OBJ 13 (oil lamp) and OBJ 25 (bow) exchange world placements. The original
  lamp-table controller now awards/removes OBJ 25. The lamp at the original
  loose-bow location uses the bow slot's original model-derived pickup volume,
  implemented as a separate non-blocking trigger, and awards/removes OBJ 13.
- OBJ 3 (rifle) moves to OBJ 56's (vase) world placement. The original
  vase-location controller now tests, awards, and removes OBJ 3, while the
  chest gives OBJ 56 instead of OBJ 3.
- The dresser (OBJ 4 controller) gives OBJ 15 (letter) instead of OBJ 5
  (Indian rug); search area `F0:R0:HC18` gives OBJ 5 instead of OBJ 15.
- The first-aid kit (OBJ 46) gives OBJ 120 (poker) instead of OBJ 48 (flask).
- Breaking OBJ 56 places OBJ 78 (dagger) instead of OBJ 53 (key).

The experiment swaps only loaded world-object placement fields, preserves the
original bow pickup volume in memory, and patches validated object-ID operands
in LIFE 0, 1, 7, 8, 57, 66, and 68 in the resource cache. Those seven LIFE
scripts use the original interpreter in this test build so that the patched
bytecode, rather than ReHaunted's generated native copies, executes.
The original PAK/ITD resources are never written. Runtime changes disappear when
the process exits, but saving from `TatouTest.exe` can serialize experimental
object state into that save slot, so use a dedicated test save.

## Returning to the normal debugger

Launch `Tatou.exe` from the same directory. It does not contain or enable the
experiment. Both executables use the existing inspector assets and CSV catalogs
beside them.

## Validation status

The experiment build compiles and links successfully. The operand offsets were
checked against the extracted original AITD1 LIFE resources. Interactive game
behavior still needs to be exercised manually, especially the complete
open/search/break sequences and behavior after loading a save.

The first manual run confirmed the scripted dresser/search, chest,
first-aid-kit, and broken-vase substitutions. It also showed that transform-only
world swaps do not transfer pickup mechanisms. This revised build applies the
pickup-slot design from `../Research/WorldPickupSlotDesign.md`: scripted slots
own their controller mechanism, while a direct loose slot retains its original
proven pickup volume independently of the replacement model. Runtime validation
is still required for the corrected lamp/bow and rifle/vase cases.

Built artifact SHA-256:
`0AE50C835BD0AB158CE631A36399C438736F675191BE74911C9089A491065212`
