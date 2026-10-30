///////////////////////////////////////////////////////////////////////////////
// AITD Randomizer: optional keyboard + mouse controls (see modernControls.h)
//
// Every control does what the player could do by hand: an action is chosen the
// way the inventory chooses it (action bit + executeFoundLife on the object), then
// the action key is held, with the direction the scripts read (keyboard_input)
// set for the attack. Nothing starts where the inventory or the action key could
// not be used (cutscenes, allow_inventory(0)), and one control at a time.
///////////////////////////////////////////////////////////////////////////////

#include "common.h"
#include "modernControls.h"
#include "menuMouse.h"
#include "objectInspector.h"
#include "remasterOptions.h"
#include "controlsMenu.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

extern "C" { extern char homePath[512]; }

namespace
{
const int PLAYER_OBJ = 1;           // world object of the player (LIFE 549)
const int ACTIONS_OBJ = 2;          // "Actions" (LIFE 561), in hand with bare hands
const int VAR_BARE_ACTION = 90;     // LIFE 561: the chosen bare-hand action

// action bits: 1 << (inventory message - 23)
const int ACTION_FIGHT = 16;
const int ACTION_JUMP = 32;
const int ACTION_OPEN = 64;
const int ACTION_CLOSE = 128;
const int ACTION_PUSH = 256;
const int ACTION_THROW = 512;
const int ACTION_DROP = 1024;

// keyboard_input values: 1 up, 2 down, 4 left, 8 right
const int INPUT_UP = 1;
const int INPUT_DOWN = 2;
const int INPUT_LEFT = 4;
const int INPUT_RIGHT = 8;

// Guns and bow: aimed with the action key held. The scripts (LIFE 12 rifle, 365 revolver,
// 29 bow) fire each time the aim / draw animation ends with up held, so left held = up held:
// it fires as soon as the weapon is up and keeps firing, as Space + up on the keyboard.
struct AimedWeapon
{
    int foundLife;
};
const AimedWeapon AIMED[] = {
    { 12 },     // rifle
    { 365 },    // revolver
    { 29 },     // bow
};

struct HoldKey { SDL_Scancode key; int action; };
const HoldKey HOLD_KEYS[] = {
    { SDL_SCANCODE_E, ACTION_OPEN },
    { SDL_SCANCODE_C, ACTION_CLOSE },
    { SDL_SCANCODE_R, ACTION_PUSH },
    { SDL_SCANCODE_J, ACTION_JUMP },
};

enum Hold { HOLD_NONE, HOLD_KEY, HOLD_MOUSE, HOLD_AIM, HOLD_DOOR };

Hold s_hold = HOLD_NONE;
SDL_Scancode s_holdKey = SDL_SCANCODE_UNKNOWN;
int s_forcedInput = -1;
const AimedWeapon* s_aimWeapon = nullptr;

bool s_prevKeys[SDL_SCANCODE_COUNT] = {};
bool s_prevLeft = false;
bool s_prevRight = false;
bool s_uiLeft = false;              // the press went to the F8 inspector, until released
bool s_uiRight = false;

int s_frame = 0;
int s_quickTurnFrame = -1;          // frame of the last Q press
tObject* s_turnActor = nullptr;
s16 s_turnEnd = 0;
int s_turnLastFrame = 0;

bool chooseBareAction(int actionBit);

// ---- C near an open door: walk up to its leaf and close it -------------------------------
// The door scripts close a door only when the player touches its leaf as his Close animation
// (28, a 198-unit lunge) ends (COL_BY = player, then posrel picks the side). So: turn, walk to
// a spot straight in front of the leaf, face it, walk in until touching, then the normal Close.
// Wall collision stays on; a walk that stops getting closer, takes too long, or meets the
// player's own input is given up. Research/ScriptedPositioning.md
struct DoorLeaf { int obj; int stateVar; int closedBeta; };
const DoorLeaf DOOR_LEAVES[] = {
#include "doorCloseTable.inc"
};

const int DOOR_REACH = 1200;            // player box to leaf box, room units
const int DOOR_FRONT_GAP = 120;         // the spot in front of the leaf face
const int DOOR_ARRIVE = 40;             // close enough to that spot
const int DOOR_WALK_IN_MAX = 700;       // walking into the leaf from the spot
const int WALK_UNITS_PER_TICK = 12;     // anim 254: 1369 units in 110 ticks
const int TURN_PER_TICK = 32;           // about the quick turn's speed
const int STEER_LIMIT = 96;             // more off than this: stop and turn first
const unsigned DOOR_SLACK_TICKS = 50;   // 2 s over the expected time
const unsigned DOOR_STALL_TICKS = 12;   // 0.5 s without getting 16 units closer
const unsigned DOOR_CLOSE_TICKS = 100;  // the Close animation and the door's answer

enum DoorPhase { DOOR_TURN_TO_SPOT, DOOR_WALK_TO_SPOT, DOOR_FACE_LEAF, DOOR_WALK_IN, DOOR_CLOSE };
const char* const DOOR_PHASE_NAME[] = { "turning to the spot", "walking to the spot", "facing the leaf",
                                        "walking into the leaf", "closing" };

struct DoorRun
{
    int obj = -1;               // world object of the leaf
    int stateVar = -1;
    DoorPhase phase = DOOR_TURN_TO_SPOT;
    int spotX = 0, spotZ = 0;   // in front of the leaf face
    int faceBeta = 0;           // facing the leaf
    unsigned startTick = 0, limitTicks = 0, lastTick = 0;
    unsigned progressTick = 0;  // last time the distance shrank
    int bestDist = 0;
    int walkInFromX = 0, walkInFromZ = 0;
    bool sawCloseAnim = false;
};
DoorRun s_door;

void zvCentre(const ZVStruct& zv, int* x, int* z)
{
    *x = (zv.ZVX1 + zv.ZVX2) / 2;
    *z = (zv.ZVZ1 + zv.ZVZ2) / 2;
}

// beta 0 walks towards -z, 256 towards +x, 768 towards -x (forward = (sin, -cos))
int betaTowards(int dx, int dz)
{
    const double t = atan2((double)dx, (double)-dz);
    return (int)lround(t * 512.0 / 3.14159265358979323846) & 0x3FF;
}

int angleDiff(int target, int current)
{
    int d = (target - current) & 0x3FF;
    return d > 512 ? d - 1024 : d;
}

// Turns on the spot towards `target`; true once facing it exactly
bool turnTowards(tObject* actor, int target, unsigned dt)
{
    const int d = angleDiff(target, actor->beta & 0x3FF);
    const int step = TURN_PER_TICK * (int)(dt ? dt : 1);
    actor->rotate.numSteps = 0;
    actor->direction = 0;
    if (d >= -step && d <= step)
    {
        actor->beta = (s16)target;
        return true;
    }
    actor->beta = (s16)((actor->beta + (d > 0 ? step : -step)) & 0x3FF);
    return false;
}

int doorActorIndex(int obj)
{
    return (obj >= 0 && obj < (int)ListWorldObjets.size()) ? ListWorldObjets[obj].objIndex : -1;
}

int boxGap(const ZVStruct& a, const ZVStruct& b)
{
    const int gx = (std::max)(0, (std::max)(b.ZVX1 - a.ZVX2, a.ZVX1 - b.ZVX2));
    const int gz = (std::max)(0, (std::max)(b.ZVZ1 - a.ZVZ2, a.ZVZ1 - b.ZVZ2));
    return (int)lround(sqrt((double)gx * gx + (double)gz * gz));
}

int distance2D(int x1, int z1, int x2, int z2)
{
    return (int)lround(sqrt((double)(x2 - x1) * (x2 - x1) + (double)(z2 - z1) * (z2 - z1)));
}

// The nearest open door leaf in reach, and the spot in front of the face on the player's side
bool doorFind(tObject* player)
{
    int px, pz;
    zvCentre(player->zv, &px, &pz);
    const int half = (player->zv.ZVX2 - player->zv.ZVX1) / 2;
    const DoorLeaf* best = nullptr;
    int bestGap = DOOR_REACH + 1;
    for (const DoorLeaf& d : DOOR_LEAVES)
    {
        const int idx = doorActorIndex(d.obj);
        if (idx < 0 || ListObjets[idx].room != player->room || vars[d.stateVar] == 0)
            continue;   // not here, or closed (walking into a closed door would open it)
        const int gap = boxGap(player->zv, ListObjets[idx].zv);
        if (gap < bestGap)
        {
            bestGap = gap;
            best = &d;
        }
    }
    if (!best)
        return false;

    const ZVStruct& leaf = ListObjets[doorActorIndex(best->obj)].zv;
    const int wx = leaf.ZVX2 - leaf.ZVX1;
    const int wz = leaf.ZVZ2 - leaf.ZVZ1;
    s_door = DoorRun();
    s_door.obj = best->obj;
    s_door.stateVar = best->stateVar;
    if (wx <= wz)       // thin along x: the faces look along x
    {
        const int side = px < (leaf.ZVX1 + leaf.ZVX2) / 2 ? -1 : 1;
        s_door.spotX = (side < 0 ? leaf.ZVX1 : leaf.ZVX2) + side * (half + DOOR_FRONT_GAP);
        s_door.spotZ = (wz > 2 * half) ? (std::min)((std::max)(pz, leaf.ZVZ1 + half), leaf.ZVZ2 - half)
                                       : (leaf.ZVZ1 + leaf.ZVZ2) / 2;
        s_door.faceBeta = betaTowards(-side, 0);
    }
    else                // thin along z
    {
        const int side = pz < (leaf.ZVZ1 + leaf.ZVZ2) / 2 ? -1 : 1;
        s_door.spotZ = (side < 0 ? leaf.ZVZ1 : leaf.ZVZ2) + side * (half + DOOR_FRONT_GAP);
        s_door.spotX = (wx > 2 * half) ? (std::min)((std::max)(px, leaf.ZVX1 + half), leaf.ZVX2 - half)
                                       : (leaf.ZVX1 + leaf.ZVX2) / 2;
        s_door.faceBeta = betaTowards(0, -side);
    }

    // the player must fit on the spot (no wall or furniture there)
    ZVStruct spot = player->zv;
    const int sx = s_door.spotX - px, sz = s_door.spotZ - pz;
    spot.ZVX1 += sx; spot.ZVX2 += sx; spot.ZVZ1 += sz; spot.ZVZ2 += sz;
    if (AsmCheckListCol(&spot, &roomDataTable[player->room]) > 0)
    {
        printf("Door assist: OBJ %d, no room to stand in front of the leaf\n", best->obj);
        return false;
    }

    const int walk = distance2D(px, pz, s_door.spotX, s_door.spotZ) + DOOR_FRONT_GAP + DOOR_ARRIVE;
    s_door.startTick = s_door.lastTick = s_door.progressTick = timer;
    s_door.limitTicks = (unsigned)(walk / WALK_UNITS_PER_TICK) + 2 * (1024 / TURN_PER_TICK) / 2 + DOOR_SLACK_TICKS;
    s_door.bestDist = distance2D(px, pz, s_door.spotX, s_door.spotZ);
    printf("Door assist: OBJ %d, %d units away; spot (%d, %d), facing %d; up to %u ticks\n", best->obj,
           bestGap, s_door.spotX, s_door.spotZ, s_door.faceBeta, s_door.limitTicks);
    return true;
}

// Getting closer (by 16 units at least) within DOOR_STALL_TICKS, or the walk is stuck
bool doorProgress(int dist)
{
    if (dist < s_door.bestDist - 16)
    {
        s_door.bestDist = dist;
        s_door.progressTick = timer;
        return true;
    }
    return timer - s_door.progressTick <= DOOR_STALL_TICKS;
}

void doorStartPhase(DoorPhase phase, int dist)
{
    s_door.phase = phase;
    s_door.bestDist = dist;
    s_door.progressTick = timer;
}

// One frame of the walk; false when it is over (done or given up). *forward: walk this frame
bool doorUpdate(tObject* player, bool* forward, bool* click, const char** why)
{
    *forward = false;
    *click = false;
    const unsigned dt = timer - s_door.lastTick;
    s_door.lastTick = timer;

    const int idx = doorActorIndex(s_door.obj);
    if (idx < 0 || ListObjets[idx].room != player->room)
    {
        *why = "the door is no longer here";
        return false;
    }
    if (s_door.phase == DOOR_CLOSE)
    {
        // the door's script takes the player over while it closes (trackmode 0, var_0 = 0)
        if (player->trackMode != 1 || vars[0] == 0)
        {
            *why = "closed";
            return false;
        }
        if (player->ANIM == 28)
            s_door.sawCloseAnim = true;
        else if (s_door.sawCloseAnim)
        {
            *why = "the door did not react to the Close";
            return false;
        }
        if (timer - s_door.startTick > s_door.limitTicks + DOOR_CLOSE_TICKS)
        {
            *why = "the Close took too long";
            return false;
        }
        *click = true;
        return true;
    }
    if (vars[s_door.stateVar] == 0)
    {
        *why = "the door closed meanwhile";
        return false;
    }
    if (player->trackMode != 1 || vars[0] != 1)
    {
        *why = "the game took control";
        return false;
    }
    if (timer - s_door.startTick > s_door.limitTicks)
    {
        *why = "it took too long";
        return false;
    }

    int px, pz;
    zvCentre(player->zv, &px, &pz);
    const int toSpot = distance2D(px, pz, s_door.spotX, s_door.spotZ);
    switch (s_door.phase)
    {
    case DOOR_TURN_TO_SPOT:
        if (toSpot <= DOOR_ARRIVE)
            doorStartPhase(DOOR_FACE_LEAF, 0);
        else if (turnTowards(player, betaTowards(s_door.spotX - px, s_door.spotZ - pz), dt))
            doorStartPhase(DOOR_WALK_TO_SPOT, toSpot);
        break;
    case DOOR_WALK_TO_SPOT:
    {
        if (toSpot <= DOOR_ARRIVE)
        {
            doorStartPhase(DOOR_FACE_LEAF, 0);
            break;
        }
        const int target = betaTowards(s_door.spotX - px, s_door.spotZ - pz);
        if (abs(angleDiff(target, player->beta & 0x3FF)) > STEER_LIMIT)
        {
            doorStartPhase(DOOR_TURN_TO_SPOT, toSpot);     // pushed off course: stop and turn
            break;
        }
        turnTowards(player, target, dt);
        if (!doorProgress(toSpot))
        {
            *why = "blocked on the way to the door";
            return false;
        }
        *forward = true;
        break;
    }
    case DOOR_FACE_LEAF:
        if (turnTowards(player, s_door.faceBeta, dt))
        {
            s_door.walkInFromX = px;
            s_door.walkInFromZ = pz;
            doorStartPhase(DOOR_WALK_IN, boxGap(player->zv, ListObjets[idx].zv));
        }
        break;
    case DOOR_WALK_IN:
    {
        if (ListObjets[idx].COL_BY == (s16)(player - &ListObjets[0]))
        {
            // touching the leaf: the normal Close, as chosen in the Actions menu
            if (!chooseBareAction(ACTION_CLOSE))
            {
                *why = "Close is not available";
                return false;
            }
            s_door.phase = DOOR_CLOSE;
            s_door.startTick = timer;
            s_door.limitTicks = 0;
            *click = true;
            break;
        }
        if (distance2D(s_door.walkInFromX, s_door.walkInFromZ, px, pz) > DOOR_WALK_IN_MAX
            || !doorProgress(boxGap(player->zv, ListObjets[idx].zv)))
        {
            *why = "could not reach the leaf";
            return false;
        }
        player->beta = (s16)s_door.faceBeta;
        player->rotate.numSteps = 0;
        player->direction = 0;
        *forward = true;
        break;
    }
    default:
        break;
    }
    return true;
}

bool readEnabled()
{
    // the install record, where the game runs (homePath) or next to the exe
    const char* base = SDL_GetBasePath();
    std::string text;
    for (const std::string& dir : { std::string(homePath), std::string(base ? base : "") })
    {
        std::ifstream f(dir + "AITD_Randomizer.json");
        if (f)
        {
            std::stringstream ss;
            ss << f.rdbuf();
            text = ss.str();
            break;
        }
    }
    const size_t key = text.find("\"modern_controls\"");
    if (key == std::string::npos)
        return false;
    size_t p = text.find(':', key);
    if (p == std::string::npos)
        return false;
    p = text.find_first_not_of(" \t\r\n", p + 1);
    const bool on = p != std::string::npos && text.compare(p, 4, "true") == 0;
    if (on)
        printf("AITD Randomizer: keyboard and mouse controls on\n");
    return on;
}

tObject* playerActor()
{
    if ((int)ListWorldObjets.size() <= PLAYER_OBJ)
        return nullptr;
    const int idx = ListWorldObjets[PLAYER_OBJ].objIndex;
    return (idx >= 0 && idx < NUM_MAX_OBJECT) ? &ListObjets[idx] : nullptr;
}

int inHandObject()
{
    return inHandTable[currentInventory];
}

bool objectOffers(int obj, int actionBit)
{
    return obj >= 0 && obj < (int)ListWorldObjets.size() && (ListWorldObjets[obj].foundFlag & actionBit);
}

// What the inventory does when an action is chosen for an object
void chooseAction(int obj, int actionBit)
{
    action = actionBit;
    executeFoundLife(obj);
    action = 0;
}

// Bare hands: choose an Actions entry unless it is already the chosen one
bool chooseBareAction(int actionBit)
{
    if (!objectOffers(ACTIONS_OBJ, actionBit))
        return false;   // not available now, as in the menu
    if (inHandObject() != ACTIONS_OBJ || vars[VAR_BARE_ACTION] != actionBit)
        chooseAction(ACTIONS_OBJ, actionBit);
    return true;
}

const AimedWeapon* aimedWeapon(int obj)
{
    if (obj < 0 || obj >= (int)ListWorldObjets.size())
        return nullptr;
    for (const AimedWeapon& w : AIMED)
        if (ListWorldObjets[obj].foundLife == w.foundLife)
            return &w;
    return nullptr;
}

void endHold()
{
    s_hold = HOLD_NONE;
    s_forcedInput = -1;
    s_aimWeapon = nullptr;
}
}

bool modernControlsEnabled()
{
    static const bool s_enabled = readEnabled();
    return s_enabled;
}

bool modernControlsOwnsKey(int scancode)
{
    if (!modernControlsEnabled())
        return false;
    switch (scancode)
    {
    case SDL_SCANCODE_E: case SDL_SCANCODE_C: case SDL_SCANCODE_R: case SDL_SCANCODE_J:
    case SDL_SCANCODE_T: case SDL_SCANCODE_G:
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL:
        return true;
    default:
        return false;
    }
}

void modernControlsPlayWorld(int allowSystemMenu, char* localJoyD, char* localClick)
{
    if (!modernControlsEnabled())
        return;
    s_frame++;

    int numKeys = 0;
    const bool* keys = SDL_GetKeyboardState(&numKeys);
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(nullptr, nullptr);
    bool left = (buttons & SDL_BUTTON_LMASK) != 0;
    bool right = (buttons & SDL_BUTTON_RMASK) != 0;
    if (!left)
        s_uiLeft = false;
    if (!right)
        s_uiRight = false;
    left = left && !s_uiLeft;
    right = right && !s_uiRight;
    const bool blocked = remasterOptionsIsOpen() || objectInspectorBlocksGameplayInput();
    if (blocked)
        left = right = false;

    auto down = [&](SDL_Scancode k) { return !blocked && (int)k < numKeys && keys[k]; };
    auto pressed = [&](SDL_Scancode k) { return down(k) && !s_prevKeys[k]; };
    const bool leftPressed = left && !s_prevLeft;
    const bool rightPressed = right && !s_prevRight;
    const bool ctrl = down(SDL_SCANCODE_LCTRL) || down(SDL_SCANCODE_RCTRL);

    tObject* player = playerActor();
    // Where the inventory and the action key work (an action key press with
    // allowSystemMenu 0 would end a cutscene); Space held starts nothing new
    const bool allowed = allowSystemMenu && statusScreenAllowed && player;
    const bool canStart = allowed && !*localClick;

    // quick turn: ReHaunted's "quick turn left" key (Q), now one turn per press
    if (pressed((SDL_Scancode)getKeyBinding(ACTION_QUICK_TURN_LEFT).keyboard))
        s_quickTurnFrame = s_frame;

    // the active control, while its key or button is held
    if (s_hold != HOLD_NONE && !allowed)
        endHold();
    switch (s_hold)
    {
    case HOLD_KEY:
        if (!down(s_holdKey))
            endHold();
        break;
    case HOLD_MOUSE:
        if (!left && !right)
            endHold();
        else if (left)  // left wins while both are held
            s_forcedInput = ctrl ? INPUT_LEFT : INPUT_RIGHT;
        else
            s_forcedInput = ctrl ? INPUT_DOWN : INPUT_UP;
        break;
    case HOLD_DOOR:
    {
        // any input of the player's own stops the walk
        bool input = (*localJoyD & 0x3F) || *localClick || leftPressed || rightPressed;
        for (const HoldKey& k : HOLD_KEYS)
            input = input || pressed(k.key);
        input = input || pressed(SDL_SCANCODE_T) || pressed(SDL_SCANCODE_G)
            || pressed((SDL_Scancode)getKeyBinding(ACTION_QUICK_TURN_LEFT).keyboard);
        bool forward = false, click = false;
        const char* why = "your input";
        if (input || !doorUpdate(player, &forward, &click, &why))
        {
            printf("Door assist: OBJ %d, %s (%s, %u ticks)\n", s_door.obj, why,
                   DOOR_PHASE_NAME[s_door.phase], timer - s_door.startTick);
            endHold();
            *localJoyD = 0;
            break;
        }
        *localJoyD = forward ? 1 : 0;
        if (!click)
            *localClick = 0;
        s_forcedInput = -1;
        break;
    }
    case HOLD_AIM:
        if (!right || aimedWeapon(inHandObject()) != s_aimWeapon)
        {
            endHold();
            break;
        }
        s_forcedInput = left ? INPUT_UP : 0;
        break;
    default:
        break;
    }

    // a new control, only when none is active
    if (s_hold == HOLD_NONE && canStart)
    {
        const int hand = inHandObject();
        const AimedWeapon* aimed = aimedWeapon(hand);
        bool started = false;

        for (const HoldKey& k : HOLD_KEYS)
        {
            if (started || !pressed(k.key))
                continue;
            if (k.action == ACTION_CLOSE && objectOffers(ACTIONS_OBJ, ACTION_CLOSE) && doorFind(player))
            {
                s_hold = HOLD_DOOR;     // walks up to the open door; the Close comes at the leaf
                s_forcedInput = -1;
                started = true;
                continue;
            }
            if (chooseBareAction(k.action))
            {
                s_hold = HOLD_KEY;
                s_holdKey = k.key;
                s_forcedInput = -1;
                started = true;
            }
        }
        if (!started && (pressed(SDL_SCANCODE_T) || pressed(SDL_SCANCODE_G)))
        {
            // one shot, like Throw / Drop on the held item in the inventory
            const int bit = pressed(SDL_SCANCODE_T) ? ACTION_THROW : ACTION_DROP;
            if (hand != ACTIONS_OBJ && objectOffers(hand, bit))
                chooseAction(hand, bit);
            started = true;
        }
        if (!started && aimed)
        {
            if (rightPressed)
            {
                s_hold = HOLD_AIM;
                s_aimWeapon = aimed;
                s_forcedInput = left ? INPUT_UP : 0;
            }
        }
        else if (!started && (leftPressed || rightPressed))
        {
            // bare hands: Fight (as chosen in the menu); a melee weapon: its own attacks
            if (hand != ACTIONS_OBJ || chooseBareAction(ACTION_FIGHT))
            {
                s_hold = HOLD_MOUSE;
                if (left)
                    s_forcedInput = ctrl ? INPUT_LEFT : INPUT_RIGHT;
                else
                    s_forcedInput = ctrl ? INPUT_DOWN : INPUT_UP;
            }
        }
    }

    if (s_hold == HOLD_DOOR)
        *localClick = s_door.phase == DOOR_CLOSE ? 1 : 0;
    else if (s_hold != HOLD_NONE)
        *localClick = 1;

    for (int k = 0; k < SDL_SCANCODE_COUNT && k < numKeys; k++)
        s_prevKeys[k] = down((SDL_Scancode)k);
    s_prevLeft = left;
    s_prevRight = right;

    // The gameplay cursor stays hidden. ImGui's SDL backend shows the OS cursor on every
    // frame unless ImGui wants none (imgui_impl_sdl3.cpp UpdateMouseCursor), so ask for
    // none each gameplay frame; menus and the F1 window don't, and get the cursor back.
    // With the F8 inspector on, the cursor stays for its labels and panels.
    if (!remasterOptionsIsOpen() && !objectInspectorIsEnabled())
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
}

void modernControlsUiMouseButton(int button)
{
    if (button == SDL_BUTTON_LEFT)
        s_uiLeft = true;
    else if (button == SDL_BUTTON_RIGHT)
        s_uiRight = true;
}

int modernControlsKeyboardInput()
{
    return (modernControlsEnabled() && s_hold != HOLD_NONE) ? s_forcedInput : -1;
}

bool modernControlsQuickTurn(tObject* actor)
{
    if (!modernControlsEnabled())
        return false;

    if (s_turnActor)
    {
        if (s_turnActor != actor)
            return false;
        if (s_frame - s_turnLastFrame > 1)
        {
            s_turnActor = nullptr;      // the game took control mid-turn: cancelled
        }
        else
        {
            s_turnLastFrame = s_frame;
            actor->beta = updateActorRotation(&actor->rotate);
            if (actor->rotate.numSteps == 0)
            {
                actor->beta = s_turnEnd;    // exactly 180 degrees
                s_turnActor = nullptr;
            }
            return true;
        }
    }

    // one half-turn per Q press (presses during a turn are dropped). Walking or running, he stops
    // for the turn; with W/S still held he walks on afterwards as before (now the other way)
    if (s_quickTurnFrame == s_frame && actor == playerActor())
    {
        s_quickTurnFrame = -1;
        actor->speed = 0;
        s_turnEnd = (actor->beta + 0x200) & 0x3FF;
        InitRealValue(actor->beta, s_turnEnd, 15, &actor->rotate);
        actor->direction = 2;
        s_turnActor = actor;
        s_turnLastFrame = s_frame;
        actor->beta = updateActorRotation(&actor->rotate);
        return true;
    }
    return false;
}

bool modernControlsQuickTurnActive(tObject* actor)
{
    return modernControlsEnabled() && s_turnActor == actor;
}

bool modernControlsMessageClick()
{
    return modernControlsEnabled() && menuMouseClicked();
}
