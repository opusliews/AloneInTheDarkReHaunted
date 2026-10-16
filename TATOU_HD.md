# TatouHD Object Inspector

`TatouHD.exe` is the Object Inspector build using ReHaunted's available HD
runtime features. It contains the same inspector, annotations, god mode, and
crash diagnostics as `Tatou.exe`.

## Independent configuration

`TatouHD.exe` reads and writes `aitd_remaster_hd.cfg`. The normal `Tatou.exe`
continues to use `aitd_remaster.cfg`, so both executables can coexist in one
game directory without changing each other's graphics settings.

The supplied HD configuration enables HD backgrounds, texture filtering, TTF
text, bloom, film grain, SSAO, light probes, and blurred menus. SSGI remains
disabled by default because of its higher cost. These settings can be edited in
`aitd_remaster_hd.cfg`; settings exposed by ReHaunted's system menu are also
saved there when running `TatouHD.exe`.

## Required files

Keep these beside `TatouHD.exe` in the directory from which the game is run:

- `aitd_remaster_hd.cfg`
- `BLKCHCRY.TTF`
- `backgrounds_hd.hda`
- `atlases.hda`
- `masks_hd/`
- `AITD_objects.csv`
- `AITD_zones.csv`

The original AITD game data must also be available in the working directory,
as required by ReHaunted. Do not overwrite newer annotated CSVs when copying an
updated executable; descriptions and manual placement classifications live in
those files.

The packaged HDA archives were generated from the existing files under
`ReHaunted/Assets`; no original game resources were modified.

## Inspector controls

- `F8`: toggle Object Inspector
- Left click: select an object label or zone
- `P`: show/hide selected-entry details
- `H`: hide selected overlay
- `Y`: restore hidden overlays
- `O`: edit the selected CSV description
- `F9`: toggle player-only god mode

See `OBJECT_INSPECTOR.md` for complete instructions.

## Known source limitation

The local ReHaunted documentation mentions a separate Home/F1 Options Dialog,
but that dialog and its Home-key handler are not present in this source tree.
`TatouHD.exe` includes the enhanced system menu and all remaster facilities
that are actually present in the available source; it does not recreate that
missing standalone dialog.
