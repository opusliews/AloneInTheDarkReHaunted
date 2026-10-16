///////////////////////////////////////////////////////////////////////////////
// Alone In The Dark Re-Haunted
// Object inspector overlay for associating rendered actors with OBJETS.ITD.
///////////////////////////////////////////////////////////////////////////////

#if 0 // Superseded by the selection/annotation implementation included below.
#include "common.h"
#include "vars.h"
#include "osystem.h"
#include "objectInspector.h"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <vector>

namespace
{
struct ObjectInspectorLabel
{
    int actorId;
    int worldObjectId;
    int bodyId;
    int centerX;
    int topY;
};

struct ObjectInspectorZoneLabel
{
    bool hardCollision;
    int zoneIndex;
    int type;
    int parameter;
    int centerX;
    int topY;
};

std::atomic_bool s_enabled{ false };
std::atomic_bool s_godModeEnabled{ false };
std::vector<ObjectInspectorLabel> s_labels;
std::vector<ObjectInspectorZoneLabel> s_zoneLabels;

constexpr int AITD1_PLAYER_HEALTH_VAR = 21;
constexpr int AITD1_DEFAULT_PLAYER_HEALTH = 20;

void drawLabel(ImDrawList* drawList, const ImVec2& position, const char* text,
    ImU32 textColor, float viewportWidth, float viewportHeight)
{
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    ImVec2 clamped = position;
    clamped.x = (std::clamp)(clamped.x - textSize.x * 0.5f, 3.0f,
        (std::max)(3.0f, viewportWidth - textSize.x - 3.0f));
    clamped.y = (std::clamp)(clamped.y - textSize.y - 4.0f, 3.0f,
        (std::max)(3.0f, viewportHeight - textSize.y - 3.0f));

    const ImVec2 padding(3.0f, 2.0f);
    drawList->AddRectFilled(
        ImVec2(clamped.x - padding.x, clamped.y - padding.y),
        ImVec2(clamped.x + textSize.x + padding.x, clamped.y + textSize.y + padding.y),
        IM_COL32(0, 0, 0, 190), 2.0f);
    drawList->AddRect(
        ImVec2(clamped.x - padding.x, clamped.y - padding.y),
        ImVec2(clamped.x + textSize.x + padding.x, clamped.y + textSize.y + padding.y),
        IM_COL32(255, 220, 64, 210), 2.0f);
    drawList->AddText(ImVec2(clamped.x + 1.0f, clamped.y + 1.0f),
        IM_COL32(0, 0, 0, 255), text);
    drawList->AddText(clamped, textColor, text);
}
}

void objectInspectorToggle()
{
    const bool enabled = !s_enabled.load(std::memory_order_relaxed);
    s_enabled.store(enabled, std::memory_order_relaxed);
    std::printf("[OBJECT INSPECTOR] %s (F8 toggles)\n", enabled ? "enabled" : "disabled");
}

bool objectInspectorIsEnabled()
{
    return s_enabled.load(std::memory_order_relaxed);
}

void objectInspectorToggleGodMode()
{
    const bool enabled = !s_godModeEnabled.load(std::memory_order_relaxed);
    s_godModeEnabled.store(enabled, std::memory_order_relaxed);
    if (enabled)
        objectInspectorProtectPlayer();
    std::printf("[GOD MODE] %s (F9 toggles)\n", enabled ? "enabled" : "disabled");
}

bool objectInspectorIsGodModeEnabled()
{
    return s_godModeEnabled.load(std::memory_order_relaxed);
}

void objectInspectorProtectPlayer()
{
    if (!objectInspectorIsGodModeEnabled() || g_gameId != AITD1)
        return;

    if (vars[AITD1_PLAYER_HEALTH_VAR] < AITD1_DEFAULT_PLAYER_HEALTH)
        vars[AITD1_PLAYER_HEALTH_VAR] = AITD1_DEFAULT_PLAYER_HEALTH;

    if (currentCameraTargetActor >= 0 && currentCameraTargetActor < NUM_MAX_OBJECT)
        ListObjets[currentCameraTargetActor].hitForce = 0;
}

int objectInspectorFilterVarWrite(int variableIndex, int oldValue, int newValue)
{
    if (objectInspectorIsGodModeEnabled() && g_gameId == AITD1 &&
        variableIndex == AITD1_PLAYER_HEALTH_VAR && newValue < oldValue)
        return oldValue;

    return newValue;
}

void objectInspectorBeginFrame()
{
    s_labels.clear();
    s_zoneLabels.clear();
}

void objectInspectorCaptureActor(int actorId, int worldObjectId, int bodyId,
    int screenMinX, int screenMinY, int screenMaxX, int screenMaxY)
{
    if (!objectInspectorIsEnabled())
        return;

    // AffObjet uses sentinel bounds for rejected/off-camera geometry. Keep only
    // projected boxes that intersect the original 320x200 game viewport.
    if (screenMinX > 319 || screenMinY > 199 || screenMaxX < 0 || screenMaxY < 0 ||
        screenMinX > screenMaxX || screenMinY > screenMaxY)
        return;

    // Guard against accidental duplicate capture if a render path submits the
    // same active actor more than once in a frame.
    for (const ObjectInspectorLabel& label : s_labels)
    {
        if (label.actorId == actorId)
            return;
    }

    ObjectInspectorLabel label;
    label.actorId = actorId;
    label.worldObjectId = worldObjectId;
    label.bodyId = bodyId;
    label.centerX = ((std::clamp)(screenMinX, 0, 319) + (std::clamp)(screenMaxX, 0, 319)) / 2;
    label.topY = (std::clamp)(screenMinY, 0, 199);
    s_labels.push_back(label);
}

void objectInspectorCaptureZone(bool hardCollision, int zoneIndex, int type,
    int parameter, int screenMinX, int screenMinY, int screenMaxX, int screenMaxY)
{
    if (!objectInspectorIsEnabled())
        return;

    if (screenMinX > 319 || screenMinY > 199 || screenMaxX < 0 || screenMaxY < 0 ||
        screenMinX > screenMaxX || screenMinY > screenMaxY)
        return;

    ObjectInspectorZoneLabel label;
    label.hardCollision = hardCollision;
    label.zoneIndex = zoneIndex;
    label.type = type;
    label.parameter = parameter;
    label.centerX = ((std::clamp)(screenMinX, 0, 319) + (std::clamp)(screenMaxX, 0, 319)) / 2;
    label.topY = (std::clamp)(screenMinY, 0, 199);
    s_zoneLabels.push_back(label);
}

void objectInspectorRender(int outputWidth, int outputHeight)
{
    const bool inspectorEnabled = objectInspectorIsEnabled();
    const bool godModeEnabled = objectInspectorIsGodModeEnabled();
    if ((!inspectorEnabled && !godModeEnabled) || !g_playWorldActive || g_menuActive ||
        outputWidth <= 0 || outputHeight <= 0)
        return;

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const float scaleX = static_cast<float>(outputWidth) / 320.0f;
    const float scaleY = static_cast<float>(outputHeight) / 200.0f;

    char status[128];
    if (inspectorEnabled)
    {
        std::snprintf(status, sizeof(status),
            "OBJECT INSPECTOR [F8]  FLOOR %d ROOM %d   GOD [F9]: %s",
            static_cast<int>(g_currentFloor), static_cast<int>(currentRoom),
            godModeEnabled ? "ON" : "OFF");
    }
    else
    {
        std::snprintf(status, sizeof(status), "GOD MODE [F9]: ON");
    }
    const ImVec2 statusSize = ImGui::CalcTextSize(status);
    drawList->AddRectFilled(ImVec2(6.0f, 6.0f),
        ImVec2(14.0f + statusSize.x, 12.0f + statusSize.y),
        IM_COL32(0, 0, 0, 210), 3.0f);
    drawList->AddText(ImVec2(10.0f, 9.0f), IM_COL32(255, 235, 96, 255), status);

    if (!inspectorEnabled)
        return;

    const char* legend = "RED HC9 = SEARCH CANDIDATE   CYAN SZ9 = SCENARIO ZONE";
    const ImVec2 legendSize = ImGui::CalcTextSize(legend);
    drawList->AddRectFilled(ImVec2(6.0f, 15.0f + statusSize.y),
        ImVec2(14.0f + legendSize.x, 21.0f + statusSize.y + legendSize.y),
        IM_COL32(0, 0, 0, 190), 3.0f);
    drawList->AddText(ImVec2(10.0f, 18.0f + statusSize.y),
        IM_COL32(180, 235, 255, 255), legend);

    char text[96];
    for (const ObjectInspectorZoneLabel& label : s_zoneLabels)
    {
        std::snprintf(text, sizeof(text), "%s F%d:R%d:%s%d  TYPE %d  PARAM %d",
            label.hardCollision ? "SEARCH?" : "SCEN",
            static_cast<int>(g_currentFloor), static_cast<int>(currentRoom),
            label.hardCollision ? "HC" : "SZ", label.zoneIndex,
            label.type, label.parameter);
        drawLabel(drawList,
            ImVec2(label.centerX * scaleX, label.topY * scaleY), text,
            label.hardCollision ? IM_COL32(255, 120, 100, 255) : IM_COL32(130, 225, 255, 255),
            static_cast<float>(outputWidth), static_cast<float>(outputHeight));
    }

    for (const ObjectInspectorLabel& label : s_labels)
    {
        std::snprintf(text, sizeof(text), "OBJ %d  BODY %d",
            label.worldObjectId, label.bodyId);
        drawLabel(drawList,
            ImVec2(label.centerX * scaleX, label.topY * scaleY),
            text, IM_COL32(255, 255, 255, 255),
            static_cast<float>(outputWidth), static_cast<float>(outputHeight));
    }
}
#endif

#include "objectInspectorImplementation.inl"
