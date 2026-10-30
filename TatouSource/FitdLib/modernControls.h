///////////////////////////////////////////////////////////////////////////////
// AITD Randomizer: optional keyboard + mouse controls (patcher option, default off)
//
// Enabled by "modern_controls": true in AITD_Randomizer.json (the patcher's
// install record). Off: none of these hooks change anything.
//   E / C / R / J held : Open/Search, Close, Push, Jump (chosen as in the Actions menu)
//   C near an open door: walks up to the leaf and closes it (gives up when blocked)
//   T / G              : Throw / Drop the held item, when it offers it
//   Left / right click : bare hands: punch / kick; melee weapons: their attacks
//                        (Ctrl gives the other two); guns and bow: right held aims,
//                        left held fires as soon as the weapon is up (as up held)
//   Q (quick turn left): one exact 180-degree turn per press (walking: stops, turns, walks on);
//                        quick turn right unused
// Research/MouseKeyboardControls.md
///////////////////////////////////////////////////////////////////////////////

#ifndef _MODERN_CONTROLS_H_
#define _MODERN_CONTROLS_H_

struct tObject;

bool modernControlsEnabled();

// readKeyboard: true for keys the scheme owns, so a menu binding to them is ignored
// (the quick turn bindings are handled here when the scheme is on)
bool modernControlsOwnsKey(int scancode);

// PlayWorld, once per frame after the input is read: may select an action like the
// inventory does, holds the action key (*localClick) for the active control
void modernControlsPlayWorld(int allowSystemMenu, char* localJoyD, char* localClick);

// keyboard_input for the scripts while a control holds the action key; -1: the real one
int modernControlsKeyboardInput();

// GereManualRot: runs the quick turn; true while it owns the rotation this frame
bool modernControlsQuickTurn(tObject* actor);
bool modernControlsQuickTurnActive(tObject* actor);

// Book, picture and item-found screens: a new left click this frame
bool modernControlsMessageClick();

// A mouse press taken by the F8 inspector: no attack or shot until it is released
void modernControlsUiMouseButton(int button);

#endif
