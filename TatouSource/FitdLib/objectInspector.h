///////////////////////////////////////////////////////////////////////////////
// Alone In The Dark Re-Haunted
// Object inspector overlay for associating rendered actors with OBJETS.ITD.
///////////////////////////////////////////////////////////////////////////////

#pragma once

// Toggle/query the inspector. The toggle is safe to call from the SDL event
// thread; label collection and rendering happen on the game/render thread.
void objectInspectorToggle();
bool objectInspectorIsEnabled();

// F9 gameplay invulnerability. AITD1 stores player health in VAR 21; these
// helpers keep that value from decreasing and neutralize incoming hit force.
void objectInspectorToggleGodMode();
bool objectInspectorIsGodModeEnabled();
void objectInspectorProtectPlayer();
int objectInspectorFilterVarWrite(int variableIndex, int oldValue, int newValue);

// Called once at the start of each rendered frame.
void objectInspectorBeginFrame();

// Record an ordinary 3D actor only after AffObjet() has successfully projected
// it. worldObjectId must already be validated against ListWorldObjets.
void objectInspectorCaptureActor(int actorId, int worldObjectId, int bodyId,
    int screenMinX, int screenMinY, int screenMaxX, int screenMaxY);

// Record the game-space bounds and projected rectangle of a current-room
// debug volume. The inspector draws its selectable 3-pixel overlay in ImGui.
void objectInspectorCaptureZone(bool hardCollision, int zoneIndex, int type,
    int parameter, int zoneX1, int zoneX2, int zoneY1, int zoneY2,
    int zoneZ1, int zoneZ2, int screenMinX, int screenMinY,
    int screenMaxX, int screenMaxY, const int* projectedX,
    const int* projectedY, const bool* projectedValid);

// Input hooks. A true return means the inspector consumed the event and it
// must not be forwarded to gameplay/fullscreen handling.
bool objectInspectorHandleKeyDown(int scancode, bool repeat);
bool objectInspectorHandleMouseButton(int button, float x, float y);
bool objectInspectorBlocksGameplayInput();
bool objectInspectorWantsTextInput();

// Draw the current floor/room and captured labels in the active ImGui frame.
void objectInspectorRender(int outputWidth, int outputHeight);
