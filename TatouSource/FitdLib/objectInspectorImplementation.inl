#include "common.h"
#include "vars.h"
#include "room.h"
#include "osystem.h"
#include "objectInspector.h"
#include "scenarioTester.h"

#include <SDL.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace
{
struct ScreenRect { float x1 = 0, y1 = 0, x2 = 0, y2 = 0; bool valid = false; };
struct ObjectInspectorLabel { int actorId, worldObjectId, bodyId, centerX, topY; };
struct ObjectInspectorZoneLabel
{
    bool hardCollision;
    int floor, room, zoneIndex, type, parameter;
    int x1, x2, y1, y2, z1, z2;
    int screenMinX, screenMinY, screenMaxX, screenMaxY, centerX, topY;
    std::array<int, 8> projectedX{}, projectedY{};
    std::array<bool, 8> projectedValid{};
};
enum class SelectionKind { None, Object, Zone };
struct HitObject { int worldObjectId; ScreenRect rect; };
struct HitZone { ObjectInspectorZoneLabel zone; ScreenRect outlineRect, labelRect; };
struct CsvCatalog
{
    std::filesystem::path path;
    std::vector<std::vector<std::string>> rows;
    std::unordered_map<std::string, size_t> rowByKey;
    size_t descriptionColumn = 0;
    bool loadAttempted = false, valid = false;
    std::string error;
};

std::atomic_bool s_enabled{ false }, s_godModeEnabled{ false };
std::vector<ObjectInspectorLabel> s_labels;
std::vector<ObjectInspectorZoneLabel> s_zoneLabels;
std::vector<HitObject> s_objectHits;
std::vector<HitZone> s_zoneHits;
std::unordered_set<int> s_hiddenObjects;
std::unordered_set<std::string> s_hiddenZones;
SelectionKind s_selectionKind = SelectionKind::None;
int s_selectedObject = -1;
ObjectInspectorZoneLabel s_selectedZone{};
bool s_detailsVisible = true, s_focusEditor = false;
std::atomic_bool s_editorOpen{ false };
std::atomic_bool s_detailsUiActive{ false };
ScreenRect s_detailsRect;
std::array<char, 16384> s_descriptionBuffer{};
std::string s_uiError;
CsvCatalog s_objectCatalog, s_zoneCatalog;
int s_classificationObject = -1;
int s_locationChoice = 0;
int s_rotationChoice = 0;
bool s_classificationDirty = false;

constexpr std::array<const char*, 8> kLocationClasses = {
    "unknown", "ground", "elevated_surface", "wall_supported",
    "hanging_or_suspended", "scripted_or_dynamic", "off_world", "special"
};
constexpr std::array<const char*, 10> kRotationClasses = {
    "unknown", "upright", "flat", "leaning", "sideways", "inverted",
    "default_pose", "tilted", "not_applicable", "special"
};

constexpr int AITD1_PLAYER_HEALTH_VAR = 21;
constexpr int AITD1_DEFAULT_PLAYER_HEALTH = 20;

bool contains(const ScreenRect& r, float x, float y)
{ return r.valid && x >= r.x1 && x <= r.x2 && y >= r.y1 && y <= r.y2; }

const char* playerFacingDirection(int beta)
{
    // AITD angles use 1024 units per turn. The actor-facing convention used
    // by the movement/raycast code maps beta 0/256/512/768 to -Z/+X/+Z/-X.
    // Project convention: +Z is North and +X is East.
    static constexpr const char* directions[8] = {
        "South (S)", "Southeast (SE)", "East (E)", "Northeast (NE)",
        "North (N)", "Northwest (NW)", "West (W)", "Southwest (SW)"
    };
    const int normalized = beta & 0x3FF;
    return directions[((normalized + 64) / 128) & 7];
}

std::string zoneKey(const ObjectInspectorZoneLabel& z)
{
    char key[64];
    std::snprintf(key, sizeof(key), "F%d:R%d:%s%d", z.floor, z.room,
        z.hardCollision ? "HC" : "SZ", z.zoneIndex);
    return key;
}

bool isHidden(const ObjectInspectorZoneLabel& z)
{ return s_hiddenZones.find(zoneKey(z)) != s_hiddenZones.end(); }

ScreenRect drawLabel(ImDrawList* list, const ImVec2& position, const char* text,
    ImU32 color, float width, float height, bool selected)
{
    const ImVec2 size = ImGui::CalcTextSize(text);
    ImVec2 p = position;
    p.x = (std::clamp)(p.x - size.x * .5f, 3.0f, (std::max)(3.0f, width - size.x - 3.0f));
    p.y = (std::clamp)(p.y - size.y - 4.0f, 3.0f, (std::max)(3.0f, height - size.y - 3.0f));
    ScreenRect r{ p.x - 3, p.y - 2, p.x + size.x + 3, p.y + size.y + 2, true };
    list->AddRectFilled(ImVec2(r.x1, r.y1), ImVec2(r.x2, r.y2), IM_COL32(0,0,0,205), 2);
    list->AddRect(ImVec2(r.x1, r.y1), ImVec2(r.x2, r.y2),
        selected ? IM_COL32(255,255,64,255) : IM_COL32(255,220,64,210),
        2, 0, selected ? 3.0f : 1.0f);
    list->AddText(ImVec2(p.x + 1, p.y + 1), IM_COL32(0,0,0,255), text);
    list->AddText(p, color, text);
    return r;
}

bool parseCsv(std::istream& input, std::vector<std::vector<std::string>>& rows, std::string& error)
{
    rows.clear();
    std::vector<std::string> row;
    std::string field;
    bool quoted = false, haveData = false;
    char ch;
    while (input.get(ch))
    {
        haveData = true;
        if (quoted)
        {
            if (ch == '"')
            {
                if (input.peek() == '"') { input.get(ch); field.push_back('"'); }
                else quoted = false;
            }
            else field.push_back(ch);
        }
        else if (ch == '"' && field.empty()) quoted = true;
        else if (ch == ',') { row.push_back(field); field.clear(); }
        else if (ch == '\n')
        {
            if (!field.empty() && field.back() == '\r') field.pop_back();
            row.push_back(field); field.clear(); rows.push_back(row); row.clear(); haveData = false;
        }
        else field.push_back(ch);
    }
    if (quoted) { error = "unterminated quoted field"; return false; }
    if (haveData || !field.empty() || !row.empty())
    {
        if (!field.empty() && field.back() == '\r') field.pop_back();
        row.push_back(field); rows.push_back(row);
    }
    if (rows.empty()) { error = "CSV is empty"; return false; }
    return true;
}

std::string csvEscape(const std::string& value)
{
    if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
    std::string out = "\"";
    for (char c : value) { if (c == '"') out += "\"\""; else out += c; }
    out += '"';
    return out;
}

void loadCatalog(CsvCatalog& catalog, const char* filename, const char* expectedKey)
{
    if (catalog.loadAttempted) return;
    catalog.loadAttempted = true;
    const char* base = SDL_GetBasePath();
    catalog.path = std::filesystem::path(base ? base : "") / filename;
    std::ifstream input(catalog.path, std::ios::binary);
    if (!input) { catalog.error = std::string(filename) + " is missing or unreadable beside Tatou.exe"; return; }
    // PowerShell 5's UTF-8 CSV writer emits a BOM. Consume it before parsing;
    // otherwise the leading quote is seen after non-field bytes and becomes a
    // literal part of the first header name (\"obj_id\" instead of obj_id).
    char bom[3] = {};
    input.read(bom, sizeof(bom));
    if (input.gcount() != sizeof(bom) ||
        static_cast<unsigned char>(bom[0]) != 0xEF ||
        static_cast<unsigned char>(bom[1]) != 0xBB ||
        static_cast<unsigned char>(bom[2]) != 0xBF)
    {
        input.clear();
        input.seekg(0, std::ios::beg);
    }
    if (!parseCsv(input, catalog.rows, catalog.error))
    { catalog.error = std::string(filename) + ": " + catalog.error; return; }
    if (catalog.rows[0].empty() || catalog.rows[0][0] != expectedKey || catalog.rows[0].back() != "description")
    { catalog.error = std::string(filename) + " has an invalid schema (ID first, description last required)"; return; }
    catalog.descriptionColumn = catalog.rows[0].size() - 1;
    for (size_t i = 1; i < catalog.rows.size(); ++i)
    {
        if (catalog.rows[i].size() != catalog.rows[0].size() || catalog.rows[i][0].empty())
        { catalog.error = std::string(filename) + " has a malformed row " + std::to_string(i + 1); return; }
        if (!catalog.rowByKey.emplace(catalog.rows[i][0], i).second)
        { catalog.error = std::string(filename) + " has duplicate ID " + catalog.rows[i][0]; return; }
    }
    catalog.valid = true;
}

void ensureCatalogsLoaded()
{
    loadCatalog(s_objectCatalog, "AITD_objects.csv", "obj_id");
    loadCatalog(s_zoneCatalog, "AITD_zones.csv", "zone_id");
}

const std::string* descriptionFor(CsvCatalog& catalog, const std::string& key)
{
    if (!catalog.valid) return nullptr;
    const auto it = catalog.rowByKey.find(key);
    return it == catalog.rowByKey.end() ? nullptr : &catalog.rows[it->second][catalog.descriptionColumn];
}

const std::string* catalogValue(CsvCatalog& catalog, const std::string& key, const char* columnName)
{
    if (!catalog.valid) return nullptr;
    const auto row = catalog.rowByKey.find(key);
    if (row == catalog.rowByKey.end()) return nullptr;
    const auto column = std::find(catalog.rows[0].begin(), catalog.rows[0].end(), columnName);
    if (column == catalog.rows[0].end()) return nullptr;
    return &catalog.rows[row->second][static_cast<size_t>(column - catalog.rows[0].begin())];
}

size_t catalogColumn(const CsvCatalog& catalog, const char* columnName)
{
    if (!catalog.valid) return static_cast<size_t>(-1);
    const auto column = std::find(catalog.rows[0].begin(), catalog.rows[0].end(), columnName);
    return column == catalog.rows[0].end()
        ? static_cast<size_t>(-1)
        : static_cast<size_t>(column - catalog.rows[0].begin());
}

bool writeCatalog(CsvCatalog& catalog, std::string& error);

template<size_t N>
int classificationIndex(const std::string* value, const std::array<const char*, N>& choices)
{
    if (value)
        for (size_t index = 0; index < choices.size(); ++index)
            if (*value == choices[index]) return static_cast<int>(index);
    return 0;
}

void syncClassificationEditor()
{
    if (s_selectionKind != SelectionKind::Object)
    {
        s_classificationObject = -1;
        s_classificationDirty = false;
        return;
    }
    if (s_classificationObject == s_selectedObject) return;
    const std::string key = std::to_string(s_selectedObject);
    s_classificationObject = s_selectedObject;
    s_locationChoice = classificationIndex(
        catalogValue(s_objectCatalog, key, "location_class"), kLocationClasses);
    s_rotationChoice = classificationIndex(
        catalogValue(s_objectCatalog, key, "rotation_class"), kRotationClasses);
    s_classificationDirty = false;
}

void saveClassification()
{
    if (!s_objectCatalog.valid || s_selectionKind != SelectionKind::Object) return;
    const auto rowIt = s_objectCatalog.rowByKey.find(std::to_string(s_selectedObject));
    if (rowIt == s_objectCatalog.rowByKey.end())
    { s_uiError = "Selected OBJ is missing from AITD_objects.csv"; return; }
    struct Update { const char* column; const char* value; };
    const Update updates[] = {
        {"location_class", kLocationClasses[s_locationChoice]},
        {"location_confidence", "confirmed"}, {"location_source", "manual"},
        {"rotation_class", kRotationClasses[s_rotationChoice]},
        {"rotation_confidence", "confirmed"}, {"rotation_source", "manual"},
        {"classification_notes", "User-confirmed in Object Inspector."}
    };
    std::vector<std::string> oldRow = s_objectCatalog.rows[rowIt->second];
    for (const Update& update : updates)
    {
        const size_t column = catalogColumn(s_objectCatalog, update.column);
        if (column == static_cast<size_t>(-1))
        {
            s_uiError = std::string("AITD_objects.csv is missing classification column: ") + update.column;
            return;
        }
        s_objectCatalog.rows[rowIt->second][column] = update.value;
    }
    if (!writeCatalog(s_objectCatalog, s_uiError))
    { s_objectCatalog.rows[rowIt->second] = oldRow; return; }
    s_classificationDirty = false;
    s_uiError.clear();
}

bool writeCatalog(CsvCatalog& catalog, std::string& error)
{
    const std::filesystem::path temporary = catalog.path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) { error = "Cannot open temporary annotation file for writing"; return false; }
        for (const auto& row : catalog.rows)
        {
            for (size_t col = 0; col < row.size(); ++col)
            { if (col) output.put(','); output << csvEscape(row[col]); }
            output << "\r\n";
        }
        output.flush();
        if (!output) { error = "Failed while writing temporary annotation file"; return false; }
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.wstring().c_str(), catalog.path.wstring().c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        error = "Cannot replace annotation CSV (Windows error " +
            std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
        std::error_code ignored; std::filesystem::remove(temporary, ignored); return false;
    }
#else
    std::error_code ec; std::filesystem::rename(temporary, catalog.path, ec);
    if (ec) { error = "Cannot replace annotation CSV: " + ec.message(); return false; }
#endif
    return true;
}

std::string selectedKey()
{
    if (s_selectionKind == SelectionKind::Object) return std::to_string(s_selectedObject);
    if (s_selectionKind == SelectionKind::Zone) return zoneKey(s_selectedZone);
    return {};
}

CsvCatalog* selectedCatalog()
{
    if (s_selectionKind == SelectionKind::Object) return &s_objectCatalog;
    if (s_selectionKind == SelectionKind::Zone) return &s_zoneCatalog;
    return nullptr;
}

void openEditor()
{
    if (s_selectionKind == SelectionKind::None || s_editorOpen) return;
    ensureCatalogsLoaded();
    CsvCatalog* catalog = selectedCatalog();
    if (!catalog || !catalog->valid) { s_uiError = catalog ? catalog->error : "Catalog unavailable"; return; }
    const std::string* value = descriptionFor(*catalog, selectedKey());
    if (!value) { s_uiError = "No existing CSV record for selected ID " + selectedKey(); return; }
    if (value->size() >= s_descriptionBuffer.size()) { s_uiError = "Description is too long for the editor"; return; }
    std::fill(s_descriptionBuffer.begin(), s_descriptionBuffer.end(), '\0');
    std::copy(value->begin(), value->end(), s_descriptionBuffer.begin());
    s_uiError.clear(); s_editorOpen = true; s_focusEditor = true;
}

void saveEditor()
{
    CsvCatalog* catalog = selectedCatalog();
    if (!catalog || !catalog->valid) { s_uiError = "Annotation catalog is unavailable"; return; }
    const auto it = catalog->rowByKey.find(selectedKey());
    if (it == catalog->rowByKey.end()) { s_uiError = "Selected ID is missing from CSV"; return; }
    std::string& stored = catalog->rows[it->second][catalog->descriptionColumn];
    const std::string old = stored;
    stored = s_descriptionBuffer.data();
    if (!writeCatalog(*catalog, s_uiError)) { stored = old; return; }
    s_uiError.clear(); s_editorOpen = false;
}

void validateSelection()
{
    if (s_selectionKind == SelectionKind::Object)
    {
        if (s_selectedObject < 0 || s_selectedObject >= static_cast<int>(ListWorldObjets.size()))
            s_selectionKind = SelectionKind::None;
    }
    else if (s_selectionKind == SelectionKind::Zone)
    {
        bool valid = s_selectedZone.floor == static_cast<int>(g_currentFloor) &&
            s_selectedZone.room == static_cast<int>(currentRoom) && currentRoom >= 0 &&
            currentRoom < static_cast<int>(roomDataTable.size());
        if (valid)
        {
            const roomDataStruct& room = roomDataTable[currentRoom];
            valid = s_selectedZone.zoneIndex >= 0 && (s_selectedZone.hardCollision
                ? s_selectedZone.zoneIndex < static_cast<int>(room.hardColTable.size())
                : s_selectedZone.zoneIndex < static_cast<int>(room.sceZoneTable.size()));
        }
        if (!valid) s_selectionKind = SelectionKind::None;
    }
    if (s_selectionKind == SelectionKind::None) s_editorOpen = false;
}

void drawDetails(int outputWidth)
{
    s_detailsRect.valid = false;
    s_detailsUiActive.store(false, std::memory_order_release);
    if (!s_detailsVisible || s_selectionKind == SelectionKind::None) return;
    ensureCatalogsLoaded();
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(outputWidth) - 10, 45), ImGuiCond_Always, ImVec2(1,0));
    ImGui::SetNextWindowSizeConstraints(ImVec2(360, 0),
        ImVec2((std::max)(360.0f, (std::min)(620.0f, static_cast<float>(outputWidth) - 20.0f)),
            FLT_MAX));
    ImGui::SetNextWindowBgAlpha(.90f);
    ImGui::Begin("Object Inspector Details", nullptr, ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextDisabled("P panel   O edit   H hide   Y restore all");
    if (s_selectionKind == SelectionKind::Object)
    {
        const tWorldObject& o = ListWorldObjets[s_selectedObject];
        ImGui::SeparatorText("Persistent object");
        ImGui::Text("OBJ ID: %d", s_selectedObject);
        const std::string* knownName = catalogValue(s_objectCatalog,
            std::to_string(s_selectedObject), "known_name");
        if (knownName && !knownName->empty()) ImGui::Text("Known name: %s", knownName->c_str());
        ImGui::Text("Body: %d   Active actor slot: %d", o.body, o.objIndex);
        ImGui::Text("Persistent floor/room: %d / %d", o.stage, o.room);
        ImGui::Text("Persistent position: %d, %d, %d", o.x, o.y, o.z);
        if (currentCameraTargetActor >= 0 && currentCameraTargetActor < NUM_MAX_OBJECT)
        {
            const tObject& player = ListObjets[currentCameraTargetActor];
            if (player.indexInWorld == s_selectedObject)
            {
                const int beta = player.beta & 0x3FF;
                ImGui::SeparatorText("Active player (live)");
                ImGui::Text("Floor/room: %d / %d", player.stage, player.room);
                ImGui::Text("Facing: %s", playerFacingDirection(beta));
                ImGui::Text("Angle beta: %d / 1024", beta);
                ImGui::Text("Room position: %d, %d, %d",
                    player.roomX + player.stepX,
                    player.roomY + player.stepY,
                    player.roomZ + player.stepZ);
                ImGui::TextDisabled("Cardinal convention: +Z North, +X East");
            }
        }
        ImGui::Text("LIFE / floor LIFE / found LIFE: %d / %d / %d", o.life, o.floorLife, o.foundLife);
        ImGui::Text("Found body/name: %d / %d", o.foundBody, o.foundName);
        ImGui::Text("Flags: 0x%04X   ZV type: %d", static_cast<unsigned>(static_cast<u16>(o.flags)), o.typeZV);
        ImGui::Text("Track mode/number/position: %d / %d / %d", o.trackMode, o.trackNumber, o.positionInTrack);
        ImGui::Text("Overlay hidden: %s", s_hiddenObjects.count(s_selectedObject) ? "yes" : "no");

        ImGui::SeparatorText("Placement classification");
        syncClassificationEditor();
        const std::string objectKey = std::to_string(s_selectedObject);
        const bool hasClassificationSchema =
            catalogColumn(s_objectCatalog, "location_class") != static_cast<size_t>(-1) &&
            catalogColumn(s_objectCatalog, "rotation_class") != static_cast<size_t>(-1) &&
            catalogColumn(s_objectCatalog, "classification_notes") != static_cast<size_t>(-1);
        if (hasClassificationSchema)
        {
            ImGui::SetNextItemWidth(230.0f);
            if (ImGui::Combo("Location/support", &s_locationChoice,
                kLocationClasses.data(), static_cast<int>(kLocationClasses.size())))
                s_classificationDirty = true;
            ImGui::SetNextItemWidth(230.0f);
            if (ImGui::Combo("Pose/orientation", &s_rotationChoice,
                kRotationClasses.data(), static_cast<int>(kRotationClasses.size())))
                s_classificationDirty = true;
            const std::string* locationConfidence = catalogValue(s_objectCatalog, objectKey, "location_confidence");
            const std::string* locationSource = catalogValue(s_objectCatalog, objectKey, "location_source");
            const std::string* rotationConfidence = catalogValue(s_objectCatalog, objectKey, "rotation_confidence");
            const std::string* rotationSource = catalogValue(s_objectCatalog, objectKey, "rotation_source");
            ImGui::TextDisabled("Location: %s / %s",
                locationConfidence ? locationConfidence->c_str() : "",
                locationSource ? locationSource->c_str() : "");
            ImGui::TextDisabled("Rotation: %s / %s",
                rotationConfidence ? rotationConfidence->c_str() : "",
                rotationSource ? rotationSource->c_str() : "");
            if (ImGui::Button(s_classificationDirty ? "Save classification *" : "Save classification"))
                saveClassification();
            ImGui::SameLine();
            ImGui::TextDisabled("Explicit save marks both fields manual/confirmed");
            const std::string* notes = catalogValue(s_objectCatalog, objectKey, "classification_notes");
            if (notes && !notes->empty()) ImGui::TextWrapped("Basis: %s", notes->c_str());
        }
        else
            ImGui::TextColored(ImVec4(1,.35f,.3f,1),
                "AITD_objects.csv lacks the placement-classification columns.");
    }
    else
    {
        ImGui::SeparatorText("Room zone");
        ImGui::Text("Zone ID: %s", zoneKey(s_selectedZone).c_str());
        ImGui::Text("Kind: %s", s_selectedZone.hardCollision ? "hard collision" : "scenario");
        ImGui::Text("Floor/room/index: %d / %d / %d", s_selectedZone.floor, s_selectedZone.room, s_selectedZone.zoneIndex);
        ImGui::Text("Type: %d   Parameter: %d", s_selectedZone.type, s_selectedZone.parameter);
        ImGui::Text("X: %d..%d   Y: %d..%d   Z: %d..%d", s_selectedZone.x1, s_selectedZone.x2,
            s_selectedZone.y1, s_selectedZone.y2, s_selectedZone.z1, s_selectedZone.z2);
        ImGui::Text("Overlay hidden: %s", isHidden(s_selectedZone) ? "yes" : "no");
    }
    CsvCatalog* catalog = selectedCatalog();
    const std::string* description = catalog ? descriptionFor(*catalog, selectedKey()) : nullptr;
    ImGui::SeparatorText("Annotation");
    if (description && !description->empty()) ImGui::TextWrapped("%s", description->c_str());
    else if (description) ImGui::TextDisabled("(empty - press O to edit)");
    else ImGui::TextColored(ImVec4(1,.35f,.3f,1), "%s", catalog ? catalog->error.c_str() : "Catalog unavailable");
    if (!s_uiError.empty()) ImGui::TextColored(ImVec4(1,.35f,.3f,1), "%s", s_uiError.c_str());
    const ImVec2 p = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    s_detailsRect = { p.x, p.y, p.x + size.x, p.y + size.y, true };
    s_detailsUiActive.store(ImGui::IsAnyItemActive() ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId), std::memory_order_release);
    ImGui::End();
}

void drawEditor(int outputWidth, int outputHeight)
{
    if (!s_editorOpen) return;
    ImGui::SetNextWindowPos(ImVec2(outputWidth * .5f, outputHeight * .5f), ImGuiCond_Always, ImVec2(.5f,.5f));
    ImGui::SetNextWindowSize(ImVec2((std::min)(620.0f, outputWidth - 30.0f), 300), ImGuiCond_Always);
    ImGui::Begin("Edit inspector description", nullptr, ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::Text("Selected: %s", selectedKey().c_str());
    ImGui::TextWrapped("Save updates only this existing CSV record. Cancel leaves the file unchanged.");
    if (s_focusEditor) { ImGui::SetKeyboardFocusHere(); s_focusEditor = false; }
    ImGui::InputTextMultiline("##description", s_descriptionBuffer.data(), s_descriptionBuffer.size(), ImVec2(-1,190));
    if (ImGui::Button("Save")) saveEditor();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) { s_editorOpen = false; s_uiError.clear(); }
    if (!s_uiError.empty()) ImGui::TextColored(ImVec4(1,.35f,.3f,1), "%s", s_uiError.c_str());
    ImGui::End();
}
}

void objectInspectorToggle()
{
    const bool enabled = !s_enabled.load(std::memory_order_relaxed);
    s_enabled.store(enabled, std::memory_order_relaxed);
    if (!enabled) s_editorOpen = false;
    std::printf("[OBJECT INSPECTOR] %s (F8 toggles)\n", enabled ? "enabled" : "disabled");
}
bool objectInspectorIsEnabled() { return s_enabled.load(std::memory_order_relaxed); }

void objectInspectorToggleGodMode()
{
    const bool enabled = !s_godModeEnabled.load(std::memory_order_relaxed);
    s_godModeEnabled.store(enabled, std::memory_order_relaxed);
    if (enabled) objectInspectorProtectPlayer();
    std::printf("[GOD MODE] %s (F9 toggles)\n", enabled ? "enabled" : "disabled");
}
bool objectInspectorIsGodModeEnabled() { return s_godModeEnabled.load(std::memory_order_relaxed); }

void objectInspectorProtectPlayer()
{
    if (!objectInspectorIsGodModeEnabled() || g_gameId != AITD1) return;
    // vars[21] is protected only while the active player actor's LIFE runs.
    // tObject::hitForce is deliberately left untouched: the engine reuses it
    // for both received damage and the player's outgoing attack strength.
    if (currentProcessedActorIdx != currentCameraTargetActor) return;
    // Enemy attacks set HIT_BY on their target during the animation pass.
    // Player LIFE 549 uses that marker to switch to damage LIFE 553, which
    // starts the backward hit-reaction animation. Consume only this incoming
    // player event in God Mode; do not suppress COL_BY or outgoing HIT state.
    currentProcessedActorPtr->HIT_BY = -1;
    if (vars[AITD1_PLAYER_HEALTH_VAR] < AITD1_DEFAULT_PLAYER_HEALTH)
        vars[AITD1_PLAYER_HEALTH_VAR] = AITD1_DEFAULT_PLAYER_HEALTH;
}

int objectInspectorFilterVarWrite(int variableIndex, int oldValue, int newValue)
{
    return objectInspectorIsGodModeEnabled() && g_gameId == AITD1 &&
        currentProcessedActorIdx == currentCameraTargetActor &&
        variableIndex == AITD1_PLAYER_HEALTH_VAR && newValue < oldValue ? oldValue : newValue;
}

void objectInspectorBeginFrame() { s_labels.clear(); s_zoneLabels.clear(); }

void objectInspectorCaptureActor(int actorId, int worldObjectId, int bodyId,
    int minX, int minY, int maxX, int maxY)
{
    if (!objectInspectorIsEnabled() || minX > 319 || minY > 199 || maxX < 0 || maxY < 0 || minX > maxX || minY > maxY) return;
    for (const auto& label : s_labels) if (label.actorId == actorId) return;
    s_labels.push_back({ actorId, worldObjectId, bodyId,
        ((std::clamp)(minX,0,319) + (std::clamp)(maxX,0,319)) / 2, (std::clamp)(minY,0,199) });
}

void objectInspectorCaptureZone(bool hardCollision, int zoneIndex, int type, int parameter,
    int x1, int x2, int y1, int y2, int z1, int z2, int minX, int minY, int maxX, int maxY,
    const int* projectedX, const int* projectedY, const bool* projectedValid)
{
    if (!objectInspectorIsEnabled() || minX > 319 || minY > 199 || maxX < 0 || maxY < 0 || minX > maxX || minY > maxY) return;
    ObjectInspectorZoneLabel zone{};
    zone.hardCollision = hardCollision;
    zone.floor = static_cast<int>(g_currentFloor); zone.room = static_cast<int>(currentRoom);
    zone.zoneIndex = zoneIndex; zone.type = type; zone.parameter = parameter;
    zone.x1 = x1; zone.x2 = x2; zone.y1 = y1; zone.y2 = y2; zone.z1 = z1; zone.z2 = z2;
    zone.screenMinX = minX; zone.screenMinY = minY; zone.screenMaxX = maxX; zone.screenMaxY = maxY;
    zone.centerX = ((std::clamp)(minX,0,319) + (std::clamp)(maxX,0,319)) / 2;
    zone.topY = (std::clamp)(minY,0,199);
    for (int i = 0; i < 8; ++i)
    {
        zone.projectedX[i] = projectedX[i]; zone.projectedY[i] = projectedY[i];
        zone.projectedValid[i] = projectedValid[i];
    }
    s_zoneLabels.push_back(zone);
}

bool objectInspectorHandleKeyDown(int scancode, bool repeat)
{
    if (s_editorOpen) return true;
    if (s_detailsUiActive.load(std::memory_order_acquire)) return true;
    if (!objectInspectorIsEnabled() || repeat) return false;
    switch (scancode)
    {
    case SDL_SCANCODE_P: s_detailsVisible = !s_detailsVisible; return true;
    case SDL_SCANCODE_H:
        if (s_selectionKind == SelectionKind::Object)
        {
            s_hiddenObjects.insert(s_selectedObject);
            s_objectHits.erase(std::remove_if(s_objectHits.begin(), s_objectHits.end(),
                [](const HitObject& hit) { return hit.worldObjectId == s_selectedObject; }), s_objectHits.end());
        }
        else if (s_selectionKind == SelectionKind::Zone)
        {
            const std::string hiddenKey = zoneKey(s_selectedZone);
            s_hiddenZones.insert(hiddenKey);
            s_zoneHits.erase(std::remove_if(s_zoneHits.begin(), s_zoneHits.end(),
                [&](const HitZone& hit) { return zoneKey(hit.zone) == hiddenKey; }), s_zoneHits.end());
        }
        return s_selectionKind != SelectionKind::None;
    case SDL_SCANCODE_Y: s_hiddenObjects.clear(); s_hiddenZones.clear(); return true;
    case SDL_SCANCODE_O:
        if (s_selectionKind != SelectionKind::None) { openEditor(); return true; }
        return false;
    default: return false;
    }
}

bool objectInspectorHandleMouseButton(int button, float x, float y)
{
    if (!objectInspectorIsEnabled()) return false;
    if (s_editorOpen) return true;
    if (s_detailsUiActive.load(std::memory_order_acquire)) return true;
    if (button != SDL_BUTTON_LEFT) return contains(s_detailsRect, x, y);
    if (contains(s_detailsRect, x, y)) return true;
    for (auto it = s_objectHits.rbegin(); it != s_objectHits.rend(); ++it)
        if (contains(it->rect, x, y))
        { s_selectionKind = SelectionKind::Object; s_selectedObject = it->worldObjectId; s_uiError.clear(); return true; }
    const HitZone* best = nullptr;
    float bestArea = 0;
    for (const HitZone& hit : s_zoneHits)
    {
        if (!contains(hit.outlineRect,x,y) && !contains(hit.labelRect,x,y)) continue;
        const float area = (hit.outlineRect.x2-hit.outlineRect.x1) * (hit.outlineRect.y2-hit.outlineRect.y1);
        if (!best || area < bestArea || (area == bestArea && zoneKey(hit.zone) < zoneKey(best->zone)))
        { best = &hit; bestArea = area; }
    }
    if (best)
    { s_selectionKind = SelectionKind::Zone; s_selectedZone = best->zone; s_uiError.clear(); return true; }
    return false;
}

bool objectInspectorBlocksGameplayInput()
{
    return s_editorOpen.load(std::memory_order_acquire) ||
        s_detailsUiActive.load(std::memory_order_acquire);
}
bool objectInspectorWantsTextInput() { return s_editorOpen.load(std::memory_order_acquire); }

void objectInspectorRender(int outputWidth, int outputHeight)
{
    const bool inspector = objectInspectorIsEnabled(), god = objectInspectorIsGodModeEnabled();
    if ((!inspector && !god) || !g_playWorldActive || g_menuActive || outputWidth <= 0 || outputHeight <= 0) return;
    ImDrawList* list = ImGui::GetForegroundDrawList();
    const float sx = static_cast<float>(outputWidth) / 320, sy = static_cast<float>(outputHeight) / 200;
    char status[192];
    if (inspector) std::snprintf(status, sizeof(status),
        "OBJECT INSPECTOR [F8] FLOOR %d ROOM %d  GOD [F9]: %s  P panel O edit H hide Y restore",
        static_cast<int>(g_currentFloor), static_cast<int>(currentRoom), god ? "ON" : "OFF");
    else std::snprintf(status, sizeof(status), "GOD MODE [F9]: ON");
    const ImVec2 statusSize = ImGui::CalcTextSize(status);
    list->AddRectFilled(ImVec2(6,6), ImVec2(14+statusSize.x,12+statusSize.y), IM_COL32(0,0,0,210),3);
    list->AddText(ImVec2(10,9), IM_COL32(255,235,96,255), status);
    if (!inspector) return;

    validateSelection(); ensureCatalogsLoaded(); s_objectHits.clear(); s_zoneHits.clear();
#ifdef AITD_SCENARIO_BUILD
    const char* legend = "RED HC9 = SEARCH CANDIDATE   GREEN = ITEM INSIDE   ORANGE = ITEM SPOT (VANILLA OR RANDO)   (ZONES AND CONTAINER OBJ LABELS)   CYAN SZ9 = SCENARIO ZONE";
#else
    const char* legend = "RED HC9 = SEARCH CANDIDATE   CYAN SZ9 = SCENARIO ZONE";
#endif
    const ImVec2 legendSize = ImGui::CalcTextSize(legend);
    list->AddRectFilled(ImVec2(6,15+statusSize.y), ImVec2(14+legendSize.x,21+statusSize.y+legendSize.y), IM_COL32(0,0,0,190),3);
    list->AddText(ImVec2(10,18+statusSize.y), IM_COL32(180,235,255,255), legend);

    for (const auto& z : s_zoneLabels)
    {
        if (isHidden(z)) continue;
        ScreenRect r{ z.screenMinX*sx, z.screenMinY*sy, z.screenMaxX*sx, z.screenMaxY*sy, true };
        r.x1=(std::clamp)(r.x1,0.0f,(float)outputWidth); r.x2=(std::clamp)(r.x2,0.0f,(float)outputWidth);
        r.y1=(std::clamp)(r.y1,0.0f,(float)outputHeight); r.y2=(std::clamp)(r.y2,0.0f,(float)outputHeight);
        const bool selected = s_selectionKind == SelectionKind::Zone && zoneKey(z) == zoneKey(s_selectedZone);
        const int zoneColor = z.hardCollision ? scenarioZoneColor(zoneKey(z).c_str()) : 0;
        const ImU32 color = selected ? IM_COL32(255,255,64,255) :
            (!z.hardCollision ? IM_COL32(60,220,255,230) : zoneColor == 1 ? IM_COL32(70,230,90,230) :
             zoneColor == 2 ? IM_COL32(255,160,40,230) : IM_COL32(255,80,70,230));
        static constexpr int edges[12][2] = {
            {0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{5,7},
            {0,4},{1,5},{2,6},{3,7}
        };
        for (const auto& edge : edges)
        {
            if (!z.projectedValid[edge[0]] || !z.projectedValid[edge[1]]) continue;
            list->AddLine(ImVec2(z.projectedX[edge[0]] * sx, z.projectedY[edge[0]] * sy),
                ImVec2(z.projectedX[edge[1]] * sx, z.projectedY[edge[1]] * sy), color, 3.0f);
        }
    }

    char text[128];
    auto drawZone = [&](const ObjectInspectorZoneLabel& z)
    {
        if (isHidden(z)) return;
        const bool selected = s_selectionKind == SelectionKind::Zone && zoneKey(z) == zoneKey(s_selectedZone);
        std::snprintf(text,sizeof(text),"%s %s TYPE %d PARAM %d", z.hardCollision?"SEARCH?":"SCEN", zoneKey(z).c_str(), z.type,z.parameter);
        const int zoneColor = z.hardCollision ? scenarioZoneColor(zoneKey(z).c_str()) : 0;
        const ScreenRect label = drawLabel(list,ImVec2(z.centerX*sx,z.topY*sy),text,
            !z.hardCollision ? IM_COL32(130,225,255,255) : zoneColor == 1 ? IM_COL32(120,255,140,255) :
            zoneColor == 2 ? IM_COL32(255,185,90,255) : IM_COL32(255,120,100,255),
            static_cast<float>(outputWidth), static_cast<float>(outputHeight), selected);
        s_zoneHits.push_back({z,{z.screenMinX*sx,z.screenMinY*sy,z.screenMaxX*sx,z.screenMaxY*sy,true},label});
    };
    auto drawObject = [&](const ObjectInspectorLabel& o)
    {
        if (s_hiddenObjects.count(o.worldObjectId)) return;
        const bool selected = s_selectionKind == SelectionKind::Object && s_selectedObject == o.worldObjectId;
        std::snprintf(text,sizeof(text),"OBJ %d  BODY %d",o.worldObjectId,o.bodyId);
        char objectKey[16];              // containers (cabinet OBJ 45...) share the zone colours
        std::snprintf(objectKey,sizeof(objectKey),"OBJ%d",o.worldObjectId);
        const int objectColor = scenarioZoneColor(objectKey);
        s_objectHits.push_back({o.worldObjectId,drawLabel(list,ImVec2(o.centerX*sx,o.topY*sy),text,
            objectColor == 1 ? IM_COL32(120,255,140,255) : objectColor == 2 ? IM_COL32(255,185,90,255) :
            IM_COL32(255,255,255,255), static_cast<float>(outputWidth),
            static_cast<float>(outputHeight), selected)});
    };
    for (const auto& z:s_zoneLabels) if (!(s_selectionKind==SelectionKind::Zone && zoneKey(z)==zoneKey(s_selectedZone))) drawZone(z);
    for (const auto& o:s_labels) if (!(s_selectionKind==SelectionKind::Object && s_selectedObject==o.worldObjectId)) drawObject(o);
    if (s_selectionKind==SelectionKind::Zone) for (const auto& z:s_zoneLabels) if (zoneKey(z)==zoneKey(s_selectedZone)) drawZone(z);
    if (s_selectionKind==SelectionKind::Object) for (const auto& o:s_labels) if (o.worldObjectId==s_selectedObject) drawObject(o);
    drawDetails(outputWidth);
    drawEditor(outputWidth,outputHeight);
}
