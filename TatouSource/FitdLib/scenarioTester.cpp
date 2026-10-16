#include "common.h"
#include "vars.h"
#include "main.h"
#include "life.h"
#include "inventory.h"
#include "osystem.h"
#include "scenarioTester.h"
#include "tatou.h"

#include <imgui.h>

#ifdef AITD_SCENARIO_BUILD
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#endif

// File format, commands and limits: ReHaunted/SCENARIO_TESTING.md.

#ifdef AITD_SCENARIO_BUILD
// osystemSDL.cpp (C linkage): the game folder, or empty for the working directory.
extern "C" { extern char homePath[512]; }
// main.cpp, not declared in a header.
int findObjectInInventory(int objIdx);
void DeleteObjet(int index);

namespace
{
constexpr int kActionsObject = 2;       // "Actions" pseudo-item taken by LIFE 562
constexpr s16 kFoundInInventory = static_cast<s16>(0x8000);
constexpr s16 kFoundEverTaken = 0x4000;

enum class Kind
{
    Name, Player, Give, Remove, ClearInventory, InHand, Place, Taken,
    Var, CVar, Health, Obj, LifeWord, BaselineFile
};

struct Command
{
    Kind kind;
    int line;
    std::vector<int> args;
    std::string text;
};

struct LifeWordPatch
{
    int life;
    int offset;
    int expected;
    int value;
    int line;
    bool reported;
};

struct FieldInfo
{
    const char* name;
    s16 tWorldObject::* member;
};

const FieldInfo kFields[] = {
    { "body", &tWorldObject::body },
    { "flags", &tWorldObject::flags },
    { "type_zv", &tWorldObject::typeZV },
    { "found_body", &tWorldObject::foundBody },
    { "found_name", &tWorldObject::foundName },
    { "found_flag", &tWorldObject::foundFlag },
    { "found_life", &tWorldObject::foundLife },
    { "x", &tWorldObject::x },
    { "y", &tWorldObject::y },
    { "z", &tWorldObject::z },
    { "alpha", &tWorldObject::alpha },
    { "beta", &tWorldObject::beta },
    { "gamma", &tWorldObject::gamma },
    { "stage", &tWorldObject::stage },
    { "room", &tWorldObject::room },
    { "life_mode", &tWorldObject::lifeMode },
    { "life", &tWorldObject::life },
    { "floor_life", &tWorldObject::floorLife },
    { "anim", &tWorldObject::anim },
    { "frame", &tWorldObject::frame },
    { "anim_type", &tWorldObject::animType },
    { "anim_info", &tWorldObject::animInfo },
    { "track_mode", &tWorldObject::trackMode },
    { "track_number", &tWorldObject::trackNumber },
    { "position_in_track", &tWorldObject::positionInTrack },
};
constexpr int kFieldCount = static_cast<int>(sizeof(kFields) / sizeof(kFields[0]));

bool isPlacementField(const FieldInfo& f)
{
    return f.member == &tWorldObject::x || f.member == &tWorldObject::y || f.member == &tWorldObject::z ||
        f.member == &tWorldObject::alpha || f.member == &tWorldObject::beta || f.member == &tWorldObject::gamma ||
        f.member == &tWorldObject::stage || f.member == &tWorldObject::room;
}

bool isAnimationField(const FieldInfo& f)
{
    return f.member == &tWorldObject::anim || f.member == &tWorldObject::frame ||
        f.member == &tWorldObject::animType || f.member == &tWorldObject::animInfo;
}

enum class State { Idle, WaitInit };

// Game-thread state.
State s_state = State::Idle;
int s_waitFrames = 0;
std::string s_name;
std::vector<Command> s_commands;
std::vector<LifeWordPatch> s_lifePatches;

// Baselines for F7 capture diffs.
std::vector<tWorldObject> s_fileWorld;
std::vector<s16> s_fileVars;
std::vector<s16> s_fileCVars;
bool s_haveInitBaseline = false;
std::vector<tWorldObject> s_initWorld;
std::vector<s16> s_initVars;
std::vector<s16> s_initCVars;
bool s_worldFresh = false;
bool s_sawUninitialised = false;

// Cross-thread requests.
std::atomic<bool> s_reapplyRequested{ false };
std::atomic<bool> s_captureRequested{ false };
std::atomic<int> s_nudgeRequested{ 0 };
// F5: LIFE 11 (rifle) fires while var 13 > 0, LIFE 13 (revolver) while var 92 > 0;
// each shot subtracts 1, reloading adds 8 / 6. Topped up every gameplay frame.
std::atomic_bool s_infiniteAmmo{ false };

// Patch set: replacement game files served in memory (getEmbeddedFile and
// the PAK/ITD loaders ask scenarioOverrideFile first). Replaced buffers are
// retired, never freed: callers may still hold pointers into them.
using FileBuffer = std::shared_ptr<const std::vector<unsigned char>>;
std::mutex s_overrideMutex;
std::map<std::string, FileBuffer> s_overrides;
std::vector<FileBuffer> s_retiredOverrides;
std::string s_patchsetLabel;
std::filesystem::path s_patchsetDir;    // absolute folder of the active patch set

std::string upperFileName(const std::string& name)
{
    std::string base = std::filesystem::path(name).filename().string();
    for (char& c : base)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return base;
}

// Overlay message.
std::mutex s_messageMutex;
std::string s_message;
bool s_messageIsError = false;
std::chrono::steady_clock::time_point s_messageUntil;

void showMessage(bool error, const std::string& text)
{
    std::printf("[SCENARIO] %s%s\n", error ? "ERROR: " : "", text.c_str());
    std::lock_guard<std::mutex> lock(s_messageMutex);
    s_message = text;
    s_messageIsError = error;
    s_messageUntil = std::chrono::steady_clock::now() + std::chrono::seconds(error ? 12 : 6);
}

std::filesystem::path exeDirectory()
{
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH)
        return std::filesystem::path(buffer).parent_path();
#endif
    return std::filesystem::current_path();
}

// Active scenario file. Initially `--scenario <file>` or a bare `*.scn`
// argument (drag and drop onto the exe), otherwise scenarios/default.scn beside
// the executable. A .scn dropped on the running window replaces it.
std::mutex s_pathMutex;
std::filesystem::path s_path;
bool s_pathResolved = false;

std::filesystem::path scenarioPath()
{
    std::lock_guard<std::mutex> lock(s_pathMutex);
    if (s_pathResolved)
        return s_path;
    s_pathResolved = true;
    std::filesystem::path path;
#ifdef _WIN32
    for (int i = 1; i < __argc && __argv; i++)
    {
        const std::string arg = __argv[i] ? __argv[i] : "";
        if (arg == "--scenario" && i + 1 < __argc)
            path = __argv[++i];
        else if (arg.size() > 4 && _stricmp(arg.c_str() + arg.size() - 4, ".scn") == 0)
            path = arg;
    }
#endif
    if (path.empty())
        path = exeDirectory() / "scenarios" / "default.scn";
    s_path = path;
    return path;
}

bool hasScnExtension(const std::filesystem::path& path)
{
    std::string ext = path.extension().string();
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".scn";
}

bool parseInt(const std::string& token, int& value)
{
    if (token.empty())
        return false;
    // Decimal, or hex with 0x (base 0 would read a leading zero as octal).
    const bool hex = token.find("0x") != std::string::npos || token.find("0X") != std::string::npos;
    char* end = nullptr;
    const long parsed = std::strtol(token.c_str(), &end, hex ? 16 : 10);
    if (*end != '\0' || parsed < -32768 || parsed > 65535)
        return false;
    value = static_cast<int>(parsed);
    return true;
}

int findField(const std::string& name)
{
    for (int i = 0; i < kFieldCount; i++)
        if (name == kFields[i].name)
            return i;
    return -1;
}

// Parses the whole file into s_commands / s_lifePatches. Nothing is changed
// unless the file parses without errors. With `checkOnly` (command-line
// check) the given file is parsed, its patch set read, and a summary
// returned; no game state is touched.
bool loadScenarioFile(std::string& error, const std::filesystem::path* checkOnly = nullptr,
    std::string* summary = nullptr)
{
    const std::filesystem::path path = checkOnly ? *checkOnly : scenarioPath();
    std::ifstream file(path);
    if (!file)
    {
        error = "cannot open " + path.string();
        return false;
    }

    std::vector<Command> commands;
    std::vector<LifeWordPatch> patches;
    std::map<std::string, FileBuffer> overrides;
    std::string patchsetLabel;
    std::filesystem::path patchsetDir;
    std::string name = path.filename().string();
    std::string rawLine;
    int lineNumber = 0;
    while (std::getline(file, rawLine))
    {
        lineNumber++;
        const size_t hash = rawLine.find('#');
        const std::string line = hash == std::string::npos ? rawLine : rawLine.substr(0, hash);
        std::istringstream in(line);
        std::string keyword;
        if (!(in >> keyword))
            continue;
        for (char& c : keyword)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        std::vector<std::string> tokens;
        for (std::string token; in >> token;)
            tokens.push_back(token);

        auto fail = [&](const std::string& message)
        {
            error = path.filename().string() + " line " + std::to_string(lineNumber) + ": " + message;
            return false;
        };
        auto ints = [&](size_t first, std::vector<int>& out)
        {
            for (size_t i = first; i < tokens.size(); i++)
            {
                int value;
                if (!parseInt(tokens[i], value))
                    return false;
                out.push_back(value);
            }
            return true;
        };

        Command command{ Kind::Name, lineNumber, {}, {} };
        if (keyword == "name")
        {
            const size_t keywordEnd = line.find_first_not_of(" \t") + keyword.size();
            const size_t start = line.find_first_not_of(" \t", keywordEnd);
            name = start == std::string::npos ? "" : line.substr(start);
            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
                name.pop_back();
            continue;
        }

        if (keyword == "patchset")
        {
            const size_t keywordEnd = line.find_first_not_of(" 	") + keyword.size();
            const size_t start = line.find_first_not_of(" 	", keywordEnd);
            std::string folder = start == std::string::npos ? "" : line.substr(start);
            while (!folder.empty() && std::isspace(static_cast<unsigned char>(folder.back())))
                folder.pop_back();
            if (folder.empty())
                return fail("usage: patchset <folder>");
            if (!patchsetLabel.empty())
                return fail("only one patchset per scenario");
            std::filesystem::path dir(folder);
            if (dir.is_relative())
                dir = path.parent_path() / dir;
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec))
                return fail("patchset folder not found: " + dir.string());
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
            {
                const std::string upper = upperFileName(entry.path().string());
                const std::string ext = upper.size() > 4 ? upper.substr(upper.size() - 4) : "";
                if (!entry.is_regular_file() || (ext != ".PAK" && ext != ".ITD"))
                    continue;
                std::ifstream in(entry.path(), std::ios::binary);
                auto data = std::make_shared<std::vector<unsigned char>>(
                    std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                if (!in.good() && !in.eof())
                    return fail("cannot read " + entry.path().string());
                overrides[upper] = data;
            }
            if (overrides.empty())
                return fail("patchset folder has no .PAK/.ITD files: " + dir.string());
            patchsetDir = std::filesystem::absolute(dir, ec);
            patchsetLabel = dir.filename().string() + " (";
            for (const auto& [file, data] : overrides)
                patchsetLabel += (patchsetLabel.back() == '(' ? "" : ", ") + file;
            patchsetLabel += ")";
            continue;
        }

        if (keyword == "baseline")
        {
            if (tokens.size() != 1 || (tokens[0] != "file" && tokens[0] != "newgame"))
                return fail("usage: baseline file | baseline newgame");
            if (tokens[0] == "file")
            {
                command.kind = Kind::BaselineFile;
                commands.push_back(command);
            }
            continue;               // "newgame" is the default
        }

        if (keyword == "obj")
        {
            if (tokens.size() != 3)
                return fail("usage: obj <id> <field> <value>");
            const int field = findField(tokens[1]);
            int id, value;
            if (field < 0)
                return fail("unknown object field '" + tokens[1] + "'");
            if (!parseInt(tokens[0], id) || !parseInt(tokens[2], value))
                return fail("obj: id and value must be integers");
            command.kind = Kind::Obj;
            command.args = { id, field, value };
            commands.push_back(command);
            continue;
        }

        if (!ints(0, command.args))
            return fail("'" + keyword + "' arguments must be integers");
        const size_t n = command.args.size();

        if (keyword == "player")
        {
            if (n != 5 && n != 6)
                return fail("usage: player <floor> <room> <x> <y> <z> [beta]");
            command.kind = Kind::Player;
        }
        else if (keyword == "give")
        {
            if (n == 0)
                return fail("usage: give <obj> [<obj> ...]");
            command.kind = Kind::Give;
        }
        else if (keyword == "remove")
        {
            if (n == 0)
                return fail("usage: remove <obj> [<obj> ...]");
            command.kind = Kind::Remove;
        }
        else if (keyword == "clear_inventory")
        {
            if (n != 0)
                return fail("clear_inventory takes no arguments");
            command.kind = Kind::ClearInventory;
        }
        else if (keyword == "in_hand")
        {
            if (n != 1)
                return fail("usage: in_hand <obj>");
            command.kind = Kind::InHand;
        }
        else if (keyword == "place")
        {
            if (n != 6 && n != 7)
                return fail("usage: place <obj> <floor> <room> <x> <y> <z> [beta]");
            command.kind = Kind::Place;
        }
        else if (keyword == "taken")
        {
            if (n != 1 && n != 2)
                return fail("usage: taken <obj> [0|1]");
            command.kind = Kind::Taken;
        }
        else if (keyword == "var" || keyword == "cvar")
        {
            if (n != 2)
                return fail("usage: " + keyword + " <index> <value>");
            command.kind = keyword == "var" ? Kind::Var : Kind::CVar;
        }
        else if (keyword == "health")
        {
            if (n != 1)
                return fail("usage: health <value>");
            command.kind = Kind::Health;
        }
        else if (keyword == "lifeword")
        {
            if (n != 4)
                return fail("usage: lifeword <life> <byte offset> <expected> <new>");
            if (command.args[0] < 0 || command.args[1] < 0 || (command.args[1] & 1))
                return fail("lifeword: life must be >= 0 and the offset even and >= 0");
            patches.push_back({ command.args[0], command.args[1], command.args[2] & 0xFFFF,
                command.args[3] & 0xFFFF, lineNumber, false });
            command.kind = Kind::LifeWord;
        }
        else
        {
            return fail("unknown command '" + keyword + "'");
        }
        commands.push_back(command);
    }

    if (checkOnly)
    {
        if (summary)
            *summary = "'" + name + "': " + std::to_string(commands.size()) + " commands, " +
                std::to_string(patches.size()) + " lifeword patches" +
                (patchsetLabel.empty() ? std::string() : ", patch set " + patchsetLabel);
        return true;
    }

    s_name = name;
    s_commands = std::move(commands);
    s_lifePatches = std::move(patches);

    // Install the patch set (or remove the previous one). Text resources are
    // read once at startup, so the language PAK is reloaded when it changes.
    const std::string languageFile = upperFileName(languageNameString + ".PAK");
    bool languageChanged;
    {
        std::lock_guard<std::mutex> lock(s_overrideMutex);
        languageChanged = s_overrides.count(languageFile) || overrides.count(languageFile);
        for (auto& [file, data] : s_overrides)
            s_retiredOverrides.push_back(data);
        s_overrides = std::move(overrides);
        s_patchsetLabel = patchsetLabel;
        s_patchsetDir = patchsetDir;
    }
    if (languageChanged)
    {
        const std::string language = languageNameString;
        reloadLanguage(language.c_str());
    }
    return true;
}

bool validObject(int id)
{
    return id >= 0 && id < static_cast<int>(ListWorldObjets.size());
}

bool inInventory(int id)
{
    return findObjectInInventory(id) != -1;
}

int playerObject()
{
    return CVars[getCVarsIdx(WORLD_NUM_PERSO)];
}

std::string objectLabel(int id)
{
    if (!validObject(id) || ListWorldObjets[id].foundName < 0)
        return std::string();
    textEntryStruct* text = getTextFromIdx(ListWorldObjets[id].foundName);
    if (!text || !text->textPtr)
        return std::string();
    std::string label;
    for (const u8* p = text->textPtr; *p >= 0x20; p++)
        label.push_back(*p < 0x7F ? static_cast<char>(*p) : '?');
    return label;
}

// The world table as the engine would write it back if every active actor
// left the room now (mirrors DeleteObjet), without changing anything.
std::vector<tWorldObject> currentWorldState()
{
    std::vector<tWorldObject> world = ListWorldObjets;
    for (const tObject& actor : ListObjets)
    {
        if (actor.indexInWorld < 0 || actor.indexInWorld >= static_cast<int>(world.size()))
            continue;
        tWorldObject& o = world[actor.indexInWorld];
        o.objIndex = -1;
        o.body = actor.bodyNum;
        o.anim = actor.ANIM;
        o.frame = actor.frame;
        o.animType = actor.animType;
        o.animInfo = actor.animInfo;
        o.flags = static_cast<s16>((actor.objectType & ~AF_BOXIFY) | (AF_SPECIAL * actor.dynFlags));
        o.life = actor.life;
        o.lifeMode = actor.lifeMode;
        o.trackMode = actor.trackMode;
        if (o.trackMode)
        {
            o.trackNumber = actor.trackNumber;
            o.positionInTrack = actor.positionInTrack;
        }
        o.x = actor.roomX + actor.stepX;
        o.y = actor.roomY + actor.stepY;
        o.z = actor.roomZ + actor.stepZ;
        o.alpha = actor.alpha;
        o.beta = actor.beta;
        o.gamma = actor.gamma;
        o.stage = actor.stage;
        o.room = actor.room;
    }
    for (tWorldObject& o : world)
        o.objIndex = -1;
    return world;
}

std::vector<s16> currentVars()
{
    return std::vector<s16>(vars, vars + varSize / 2);
}

// LIFE 562 (new-game setup, run by OBJ 2 on the first frame) takes OBJ 2 into
// the inventory. Until then the world is not a playable new game.
bool newGameInitDone()
{
    return validObject(kActionsObject) && (ListWorldObjets[kActionsObject].foundFlag & kFoundInInventory) &&
        currentCameraTargetActor >= 0;
}

void removeFromInventory(int id)
{
    if (inInventory(id))
        DeleteInventoryObjet(id);
    if (inHandTable[currentInventory] == id)
        inHandTable[currentInventory] = inInventory(kActionsObject) ? kActionsObject : -1;
}

// Applies the parsed scenario to the current (freshly initialised) game.
void applyScenario()
{
    int applied = 0;
    std::vector<std::string> warnings;
    auto warn = [&](const Command& c, const std::string& text)
    {
        warnings.push_back("line " + std::to_string(c.line) + ": " + text);
    };

    const int player = playerObject();
    const int playerActor = currentCameraTargetActor;
    if (playerActor < 0 || ListObjets[playerActor].indexInWorld != player)
    {
        showMessage(true, "player actor is not active; scenario not applied");
        return;
    }

    // Unload every other actor, so its world record is current and the
    // edits below are not overwritten when the actor is written back.
    // GenereActiveList() recreates the ones that belong to the room.
    for (int i = 0; i < NUM_MAX_OBJECT; i++)
        if (i != playerActor && ListObjets[i].indexInWorld != -1)
            DeleteObjet(i);

    // `baseline file` (written by F7 when no new game had been started): the
    // following values are differences from the raw OBJETS/VARS/DEFINES, so
    // first undo the new-game setup (LIFE 562) by restoring those tables.
    // The player (live actor) and OBJ 2 (Actions, held) are kept as they are.
    bool fileBaseline = false;
    for (const Command& c : s_commands)
        fileBaseline |= c.kind == Kind::BaselineFile;
    if (fileBaseline)
    {
        const size_t count = (std::min)(ListWorldObjets.size(), s_fileWorld.size());
        for (size_t i = 0; i < count; i++)
        {
            if (static_cast<int>(i) == player || static_cast<int>(i) == kActionsObject)
                continue;
            const s16 objIndex = ListWorldObjets[i].objIndex;
            ListWorldObjets[i] = s_fileWorld[i];
            ListWorldObjets[i].objIndex = objIndex;
        }
        for (size_t i = 0; i < s_fileVars.size() && i < static_cast<size_t>(varSize / 2); i++)
            vars[i] = s_fileVars[i];
        for (size_t i = 0; i < s_fileCVars.size() && i < CVars.size(); i++)
            CVars[i] = s_fileCVars[i];
        applied++;
    }

    const Command* playerCommand = nullptr;
    for (const Command& c : s_commands)
    {
        const std::vector<int>& a = c.args;
        switch (c.kind)
        {
        case Kind::Name:
        case Kind::LifeWord:
        case Kind::BaselineFile:
            break;
        case Kind::Player:
            playerCommand = &c;
            break;
        case Kind::Give:
            for (int id : a)
            {
                if (!validObject(id) || id == player)
                {
                    warn(c, "give: invalid object " + std::to_string(id));
                    continue;
                }
                if (inInventory(id))
                    continue;
                if (numObjInInventoryTable[currentInventory] >= INVENTORY_SIZE)
                {
                    warn(c, "give: inventory full");
                    break;
                }
                inventoryTable[currentInventory][numObjInInventoryTable[currentInventory]++] = static_cast<s16>(id);
                tWorldObject& o = ListWorldObjets[id];
                o.foundFlag = static_cast<s16>((o.foundFlag & ~kFoundEverTaken) | kFoundInInventory);
                o.stage = -1;
                o.room = -1;
                applied++;
            }
            break;
        case Kind::Remove:
            for (int id : a)
            {
                if (!validObject(id) || id == player)
                {
                    warn(c, "remove: invalid object " + std::to_string(id));
                    continue;
                }
                removeFromInventory(id);
                ListWorldObjets[id].stage = -1;
                ListWorldObjets[id].room = -1;
                applied++;
            }
            break;
        case Kind::ClearInventory:
            for (int i = numObjInInventoryTable[currentInventory] - 1; i >= 0; i--)
            {
                const int id = inventoryTable[currentInventory][i];
                if (id != kActionsObject)
                    removeFromInventory(id);
            }
            applied++;
            break;
        case Kind::InHand:
            if (!inInventory(a[0]))
                warn(c, "in_hand: object " + std::to_string(a[0]) + " is not in the inventory");
            else
            {
                inHandTable[currentInventory] = static_cast<s16>(a[0]);
                applied++;
            }
            break;
        case Kind::Place:
        {
            const int id = a[0];
            if (!validObject(id) || id == player)
            {
                warn(c, "place: invalid object " + std::to_string(id));
                break;
            }
            removeFromInventory(id);
            tWorldObject& o = ListWorldObjets[id];
            o.stage = static_cast<s16>(a[1]);
            o.room = static_cast<s16>(a[2]);
            o.x = static_cast<s16>(a[3]);
            o.y = static_cast<s16>(a[4]);
            o.z = static_cast<s16>(a[5]);
            if (a.size() > 6)
                o.beta = static_cast<s16>(a[6]);
            // Same as a dropped item (PutAtObjet): a loose, collision-pickup item.
            if (o.foundName >= 0 && o.foundBody >= 0)
                o.flags |= AF_FOUNDABLE;
            if (o.body < 0)
                warn(c, "place: object " + std::to_string(id) + " has no world model (body -1)");
            applied++;
            break;
        }
        case Kind::Taken:
            if (!validObject(a[0]))
                warn(c, "taken: invalid object " + std::to_string(a[0]));
            else
            {
                tWorldObject& o = ListWorldObjets[a[0]];
                if (a.size() < 2 || a[1])
                    o.foundFlag |= kFoundEverTaken;
                else
                    o.foundFlag = static_cast<s16>(o.foundFlag & ~kFoundEverTaken);
                applied++;
            }
            break;
        case Kind::Var:
        case Kind::Health:
        {
            const int index = c.kind == Kind::Health ? 21 : a[0];
            const int value = c.kind == Kind::Health ? a[0] : a[1];
            if (index < 0 || index >= varSize / 2)
                warn(c, "var index " + std::to_string(index) + " out of range");
            else
            {
                vars[index] = static_cast<s16>(value);
                applied++;
            }
            break;
        }
        case Kind::CVar:
            if (a[0] < 0 || a[0] >= static_cast<int>(CVars.size()))
                warn(c, "cvar index " + std::to_string(a[0]) + " out of range");
            else
            {
                CVars[a[0]] = static_cast<s16>(a[1]);
                applied++;
            }
            break;
        case Kind::Obj:
        {
            const FieldInfo& field = kFields[a[1]];
            if (!validObject(a[0]))
                warn(c, "obj: invalid object " + std::to_string(a[0]));
            else if (a[0] == player && (isPlacementField(field) || isAnimationField(field)))
                warn(c, "obj: use 'player' to move the player");
            else
            {
                ListWorldObjets[a[0]].*field.member = static_cast<s16>(a[2]);
                // The live player ignores its world record: apply `flags` to the
                // actor as the LIFE `flags` instruction (LM_TYPE) does. Needed for
                // starts in the caverns, where LIFE 550 sets flags 321 (can fall).
                if (a[0] == player && field.member == &tWorldObject::flags)
                {
                    tObject& p = ListObjets[playerActor];
                    p.objectType = static_cast<s16>((p.objectType & ~AF_MASK) | (a[2] & AF_MASK));
                }
                applied++;
            }
            break;
        }
        }
    }

    int weight = 0;
    for (int i = 0; i < numObjInInventoryTable[currentInventory]; i++)
        weight += ListWorldObjets[inventoryTable[currentInventory][i]].positionInTrack;
    if (weight > CVars[getCVarsIdx(MAX_WEIGHT_LOADABLE)])
        warnings.push_back("inventory weight " + std::to_string(weight) + " exceeds the limit " +
            std::to_string(CVars[getCVarsIdx(MAX_WEIGHT_LOADABLE)]) + " (allowed, but pickups will be refused)");

    if (playerCommand)
    {
        const std::vector<int>& a = playerCommand->args;
        tObject* savedActor = currentProcessedActorPtr;
        const int savedIdx = currentProcessedActorIdx;
        currentProcessedActorPtr = &ListObjets[playerActor];
        currentProcessedActorIdx = playerActor;
        // Same path as the LIFE `changeroom` of the camera target: the main
        // loop then loads the floor/room and fixPlayerStuckInWall() runs.
        setStage(a[0], a[1], a[2], a[3], a[4]);
        if (a.size() > 5)
            currentProcessedActorPtr->beta = static_cast<s16>(a[5]);
        currentProcessedActorPtr->speed = 0;
        currentProcessedActorPtr = savedActor;
        currentProcessedActorIdx = savedIdx;
        applied++;
    }

    for (const std::string& w : warnings)
        std::printf("[SCENARIO] warning: %s\n", w.c_str());
    std::string text = "'" + s_name + "' applied: " + std::to_string(applied) + " changes, " +
        std::to_string(s_lifePatches.size()) + " LIFE patches";
    if (!s_patchsetLabel.empty())
        text += ", patch set " + s_patchsetLabel;
    if (!warnings.empty())
        text += ", " + std::to_string(warnings.size()) + " warnings (first: " + warnings.front() + ")";
    text += ".  F10 reload  F7 capture";
    showMessage(!warnings.empty(), text);
}

// F6 / Shift+F6: moves the player up to kNudgeMax units forward / back along
// the facing direction, to get past a monster that pins a god-mode player.
// Only the room's walls (hard collisions) stop it, checked every kNudgeStep
// units so it cannot jump through a wall; actors are ignored. The offset is
// added like the engine's fixPlayerStuckInWall(): position and box together,
// the animation step is left alone.
constexpr int kNudgeStep = 100;
constexpr int kNudgeMax = 800;

void nudgePlayer(int direction)
{
    const int actorIdx = currentCameraTargetActor;
    if (actorIdx < 0 || actorIdx >= NUM_MAX_OBJECT || ListObjets[actorIdx].indexInWorld < 0)
    {
        showMessage(true, "nudge: no active player");
        return;
    }
    tObject& p = ListObjets[actorIdx];
    if (p.room < 0 || p.room >= static_cast<int>(roomDataTable.size()))
    {
        showMessage(true, "nudge: player is not in a loaded room");
        return;
    }

    int dx = 0;
    int dz = 0;
    int moved = 0;
    for (int distance = kNudgeStep; distance <= kNudgeMax; distance += kNudgeStep)
    {
        int stepZ = 0;
        int stepX = 0;
        Rotate(p.beta, 0, direction * distance, &stepZ, &stepX); // as walkStep()
        ZVStruct tryZv = p.zv;
        tryZv.ZVX1 += stepX; tryZv.ZVX2 += stepX;
        tryZv.ZVZ1 += stepZ; tryZv.ZVZ2 += stepZ;
        if (AsmCheckListCol(&tryZv, &roomDataTable[p.room]))
            break;
        dx = stepX;
        dz = stepZ;
        moved = distance;
    }
    if (moved == 0)
    {
        showMessage(true, std::string("nudge: wall ") + (direction > 0 ? "ahead" : "behind"));
        return;
    }

    p.roomX = static_cast<s16>(p.roomX + dx);
    p.roomZ = static_cast<s16>(p.roomZ + dz);
    p.worldX = static_cast<s16>(p.worldX + dx);
    p.worldZ = static_cast<s16>(p.worldZ + dz);
    p.zv.ZVX1 += dx; p.zv.ZVX2 += dx;
    p.zv.ZVZ1 += dz; p.zv.ZVZ2 += dz;
    showMessage(false, "nudged " + std::to_string(moved) + (direction > 0 ? " forward" : " back") +
        " to (" + std::to_string(p.roomX + p.stepX) + ", " + std::to_string(p.roomZ + p.stepZ) + ")");
}

// Back to a vanilla new game in the attic (startGame() without the intro and
// without leaving PlayWorld). LIFE 562 then runs on the next frame.
void resetToNewGame()
{
    LoadWorld();
    initVars();
    if (g_roomIsDark)
    {
        g_roomIsDark = false;
        osystem_setDarkRoom(false);
    }
    LoadEtage(0);
    NumCamera = -1;
    ChangeSalle(0);
    NewNumCamera = 0;
    FlagInitView = 2;
}

void writeCapture()
{
    if (currentCameraTargetActor < 0)
    {
        showMessage(true, "capture: no active player");
        return;
    }

    const std::vector<tWorldObject> world = currentWorldState();
    const std::vector<s16> varsNow = currentVars();
    const bool init = s_haveInitBaseline;
    const std::vector<tWorldObject>& baseWorld = init ? s_initWorld : s_fileWorld;
    const std::vector<s16>& baseVars = init ? s_initVars : s_fileVars;
    const std::vector<s16>& baseCVars = init ? s_initCVars : s_fileCVars;
    // Loading a save resizes the table to the save format's 300 records
    // (save.cpp); records past the OBJETS.ITD count are padding.
    const size_t objectCount = (std::min)(world.size(), baseWorld.size());
    if (objectCount == 0)
    {
        showMessage(true, "capture: no baseline world table");
        return;
    }

    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &local);

    const std::filesystem::path dir = exeDirectory() / "scenarios";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path path = dir / ("capture_" + std::string(stamp) + ".scn");
    std::ofstream out(path);
    if (!out)
    {
        showMessage(true, "capture: cannot write " + path.string());
        return;
    }

    const tObject& p = ListObjets[currentCameraTargetActor];
    const int player = playerObject();
    char line[256];
#ifdef AITD_SCENARIO_CAPTURE_ONLY
    out << "# Captured by the AITD Randomizer edition of ReHaunted (F7) " << stamp << "\n";
    out << "# Please send this file with your bug report.\n";
#else
    out << "# Captured by TatouScenario (F7) " << stamp << "\n";
#endif
    {
        // The randomizer seed installed in the game folder (install.py marker), copied
        // verbatim so the seed can be regenerated and checked (Generated/Generator/reproduce.py).
        std::ifstream marker(std::filesystem::path(homePath) / "AITD_Randomizer.json");
        if (!marker)
            marker.open(exeDirectory() / "AITD_Randomizer.json");
        std::string markerLine;
        if (marker)
        {
            out << "# randomizer-marker-begin\n";
            while (std::getline(marker, markerLine))
                out << "#| " << markerLine << "\n";
            out << "# randomizer-marker-end\n";
        }
    }
    out << "# Baseline: " << (init ? "new-game state after LIFE 562"
        : "raw OBJETS/VARS/DEFINES (no new game was started in this session). 'baseline file'\n"
          "# makes the scenario restore those values before applying the differences below") << "\n";
    out << "name capture " << stamp << " F" << p.stage << ":R" << p.room << "\n\n";
    if (!init)
        out << "baseline file\n\n";
    if (!s_patchsetDir.empty())
    {
        // The captured state belongs to the patched game: keep using its files.
        std::error_code relEc;
        const std::filesystem::path rel = std::filesystem::relative(s_patchsetDir, dir, relEc);
        out << "patchset " << (relEc || rel.empty() ? s_patchsetDir : rel).generic_string() << "\n\n";
    }
    std::snprintf(line, sizeof(line), "player %d %d %d %d %d %d\n\n", p.stage, p.room,
        p.roomX + p.stepX, p.roomY + p.stepY, p.roomZ + p.stepZ, p.beta);
    out << line;

    out << "clear_inventory\n";
    for (int i = 0; i < numObjInInventoryTable[currentInventory]; i++)
    {
        const int id = inventoryTable[currentInventory][i];
        if (id == kActionsObject)
            continue;
        out << "give " << id << "    # " << objectLabel(id) << "\n";
    }
    if (inHandTable[currentInventory] >= 0 && inHandTable[currentInventory] != kActionsObject)
        out << "in_hand " << inHandTable[currentInventory] << "\n";
    out << "\n";

    for (size_t i = 0; i < varsNow.size() && i < baseVars.size(); i++)
        if (varsNow[i] != baseVars[i])
            out << "var " << i << " " << varsNow[i] << "\n";
    for (size_t i = 0; i < CVars.size() && i < baseCVars.size(); i++)
        if (CVars[i] != baseCVars[i])
            out << "cvar " << i << " " << CVars[i] << "\n";
    out << "\n";

    for (size_t id = 0; id < objectCount; id++)
    {
        const bool held = inInventory(static_cast<int>(id));
        bool first = true;
        for (const FieldInfo& f : kFields)
        {
            if (static_cast<int>(id) == player && (isPlacementField(f) || isAnimationField(f)))
                continue;
            if (held && isPlacementField(f))
                continue;
            if (held && f.member == &tWorldObject::foundFlag)
                continue;
            const s16 value = world[id].*f.member;
            if (value == baseWorld[id].*f.member)
                continue;
            if (first)
            {
                const std::string label = objectLabel(static_cast<int>(id));
                if (!label.empty())
                    out << "# OBJ " << id << " " << label << "\n";
                first = false;
            }
            out << "obj " << id << " " << f.name << " " << value << "\n";
        }
    }

    for (const LifeWordPatch& patch : s_lifePatches)
        out << "lifeword " << patch.life << " " << patch.offset << " " << patch.expected << " " << patch.value << "\n";

    showMessage(false, "captured to scenarios/" + path.filename().string());
}
} // namespace
#endif

int scenarioCommandLineCheck()
{
#if defined(AITD_SCENARIO_BUILD) && !defined(AITD_SCENARIO_CAPTURE_ONLY)
#ifdef _WIN32
    int first = -1;
    for (int i = 1; i < __argc && __argv; i++)
        if (__argv[i] && std::string(__argv[i]) == "--check-scenario")
            first = i + 1;
    if (first < 0)
        return -1;
    // main() hides the console at startup; when started from a terminal that
    // is the user's own window, so show it again before printing.
    if (HWND console = GetConsoleWindow())
        ShowWindow(console, SW_SHOW);
    int failures = 0;
    for (int i = first; i < __argc; i++)
    {
        const std::filesystem::path file(__argv[i]);
        std::string error, summary;
        if (loadScenarioFile(error, &file, &summary))
            std::printf("OK    %s  %s\n", file.filename().string().c_str(), summary.c_str());
        else
        {
            std::printf("ERROR %s\n", error.c_str());
            failures++;
        }
    }
    std::fflush(stdout);
    std::exit(failures ? 1 : 0);
#endif
#endif
    return -1;
}

bool scenarioArmNewGame()
{
#if defined(AITD_SCENARIO_BUILD) && !defined(AITD_SCENARIO_CAPTURE_ONLY)
    std::string error;
    if (!loadScenarioFile(error))
    {
        showMessage(true, error + " (normal new game)");
        s_state = State::Idle;
        return false;
    }
    s_state = State::WaitInit;
    s_waitFrames = 0;
    s_reapplyRequested = false; // a file dropped in the menu is applied by this new game
    return true;
#else
    return false;
#endif
}

void scenarioOnWorldLoaded()
{
#ifdef AITD_SCENARIO_BUILD
    s_fileWorld = ListWorldObjets;
    s_fileVars = currentVars();
    s_fileCVars = CVars;
    s_worldFresh = true;
    s_sawUninitialised = false;
#endif
}

void scenarioMainLoopTick(bool gameplay)
{
#ifdef AITD_SCENARIO_BUILD
    if (g_gameId != AITD1)
        return;
    if (!gameplay)
    {
        s_reapplyRequested = false;
        s_captureRequested = false;
        s_nudgeRequested = 0;
        return;
    }

    // The new-game baseline is taken once per process: LIFE 562 and the first
    // attic frame are deterministic, so it stays valid for later games.
    if (s_worldFresh)
    {
        if (!newGameInitDone())
            s_sawUninitialised = true;
        else
        {
            if (s_sawUninitialised && !s_haveInitBaseline)
            {
                s_initWorld = currentWorldState();
                s_initVars = currentVars();
                s_initCVars = CVars;
                s_haveInitBaseline = true;
            }
            s_worldFresh = false; // a loaded save is already initialised
        }
    }

    if (s_captureRequested.exchange(false))
        writeCapture();

    if (const int direction = s_nudgeRequested.exchange(0))
        nudgePlayer(direction);

    if (s_infiniteAmmo.load())
    {
        if (vars[13] < 8)
            vars[13] = 8;
        if (vars[92] < 6)
            vars[92] = 6;
    }

    if (s_reapplyRequested.exchange(false))
    {
        std::string error;
        if (!loadScenarioFile(error))
        {
            showMessage(true, error);
            return;
        }
        resetToNewGame();
        s_state = State::WaitInit;
        s_waitFrames = 0;
        return;
    }

    if (s_state == State::WaitInit)
    {
        if (newGameInitDone())
        {
            s_state = State::Idle;
            applyScenario();
        }
        else if (++s_waitFrames > 600)
        {
            s_state = State::Idle;
            showMessage(true, "new-game setup (LIFE 562) did not run; scenario not applied");
        }
    }
#else
    (void)gameplay;
#endif
}

char* scenarioPatchLife(int lifeNum, char* lifeData)
{
#ifdef AITD_SCENARIO_BUILD
    if (!lifeData)
        return lifeData;
    for (LifeWordPatch& patch : s_lifePatches)
    {
        if (patch.life != lifeNum)
            continue;
        unsigned char* site = reinterpret_cast<unsigned char*>(lifeData) + patch.offset;
        const int found = site[0] | (site[1] << 8);
        if (found == patch.value)
            continue; // HQR cache entry already patched.
        if (found != patch.expected)
        {
            if (!patch.reported)
            {
                patch.reported = true;
                showMessage(true, "lifeword line " + std::to_string(patch.line) + ": LIFE " +
                    std::to_string(lifeNum) + " @" + std::to_string(patch.offset) + " holds " +
                    std::to_string(found) + ", expected " + std::to_string(patch.expected) + "; skipped");
            }
            continue;
        }
        site[0] = static_cast<unsigned char>(patch.value & 0xFF);
        site[1] = static_cast<unsigned char>(patch.value >> 8);
    }
#else
    (void)lifeNum;
#endif
    return lifeData;
}

bool scenarioForcesBytecode(int lifeNum)
{
#ifdef AITD_SCENARIO_BUILD
    for (const LifeWordPatch& patch : s_lifePatches)
        if (patch.life == lifeNum)
            return true;
#else
    (void)lifeNum;
#endif
    return false;
}

bool scenarioOverrideFile(const char* filename, const unsigned char** data, size_t* size)
{
#ifdef AITD_SCENARIO_BUILD
    if (!filename)
        return false;
    std::lock_guard<std::mutex> lock(s_overrideMutex);
    if (s_overrides.empty())
        return false;
    const auto it = s_overrides.find(upperFileName(filename));
    if (it == s_overrides.end())
        return false;
    *data = it->second->data();
    *size = it->second->size();
    return true;
#else
    (void)filename;
    (void)data;
    (void)size;
    return false;
#endif
}

void scenarioRequestReapply()
{
#if defined(AITD_SCENARIO_BUILD) && !defined(AITD_SCENARIO_CAPTURE_ONLY)
    s_reapplyRequested = true;
#endif
}

void scenarioDropFile(const char* path)
{
#if defined(AITD_SCENARIO_BUILD) && !defined(AITD_SCENARIO_CAPTURE_ONLY)
    if (!path || !*path)
        return;
    // SDL passes UTF-8.
    const std::filesystem::path dropped(std::u8string(reinterpret_cast<const char8_t*>(path)));
    if (!hasScnExtension(dropped))
    {
        showMessage(true, "dropped file is not a .scn scenario: " + dropped.filename().string());
        return;
    }
    scenarioPath(); // resolve the startup choice first so it cannot override the drop
    {
        std::lock_guard<std::mutex> lock(s_pathMutex);
        s_path = dropped;
    }
    showMessage(false, "active scenario: " + dropped.filename().string() +
        (g_playWorldActive ? " (applying)" : " (start a New Game, or press F10 in game)"));
    s_reapplyRequested = true;
#else
    (void)path;
#endif
}

void scenarioRequestCapture()
{
#ifdef AITD_SCENARIO_BUILD
    s_captureRequested = true;
#endif
}

void scenarioRequestNudge(int direction)
{
#if defined(AITD_SCENARIO_BUILD) && !defined(AITD_SCENARIO_CAPTURE_ONLY)
    s_nudgeRequested = direction > 0 ? 1 : -1;
#else
    (void)direction;
#endif
}

int scenarioZoneColor(const char* zoneKey)
{
#if defined(AITD_SCENARIO_BUILD) && !defined(AITD_SCENARIO_CAPTURE_ONLY)
    struct ZoneEntry { bool seed; std::vector<int> items; };
    static std::filesystem::path s_zonePath;
    static std::map<std::string, ZoneEntry> s_zones;
    std::filesystem::path path;
    std::error_code ec;
    if (!s_patchsetDir.empty() && std::filesystem::exists(s_patchsetDir / "search_zones.txt", ec))
        path = s_patchsetDir / "search_zones.txt";
    else
        path = exeDirectory() / "scenarios" / "search_zones_vanilla.txt";
    if (path != s_zonePath)
    {
        s_zonePath = path;
        s_zones.clear();
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#')
                continue;
            std::istringstream words(line);
            std::string key, tag;
            int item;
            if (!(words >> key >> tag))
                continue;
            ZoneEntry entry{ tag == "seed", {} };
            while (words >> item)
                entry.items.push_back(item);
            s_zones[key] = entry;
        }
    }
    const auto it = s_zones.find(zoneKey ? zoneKey : "");
    if (it == s_zones.end())
        return 0;
    for (const int item : it->second.items)  // the game's object() test: not found yet
        if (item >= 0 && item < static_cast<int>(ListWorldObjets.size()) &&
            (ListWorldObjets[item].foundFlag & 0xC000) == 0)
            return 1;
    return it->second.seed ? 0 : 2;
#else
    (void)zoneKey;
    return 0;
#endif
}

void scenarioToggleInfiniteAmmo()
{
#if defined(AITD_SCENARIO_BUILD) && !defined(AITD_SCENARIO_CAPTURE_ONLY)
    const bool on = !s_infiniteAmmo.load();
    s_infiniteAmmo = on;
    showMessage(false, std::string("infinite ammo ") + (on ? "ON (var 13 >= 8, var 92 >= 6)" : "OFF"));
#endif
}

void scenarioRender(int outputWidth, int outputHeight)
{
#ifdef AITD_SCENARIO_BUILD
    if (s_infiniteAmmo.load() && outputWidth > 0)
    {
        const char* label = "INFINITE AMMO [F5]";
        const ImVec2 size = ImGui::CalcTextSize(label);
        const float x = static_cast<float>(outputWidth) - size.x - 12.0f;
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        fg->AddRectFilled(ImVec2(x - 4, 6), ImVec2(x + size.x + 4, 10 + size.y), IM_COL32(0, 0, 0, 215), 3);
        fg->AddText(ImVec2(x, 8), IM_COL32(255, 220, 90, 255), label);
    }
    std::string text;
    bool error;
    {
        std::lock_guard<std::mutex> lock(s_messageMutex);
        if (s_message.empty() || std::chrono::steady_clock::now() > s_messageUntil)
            return;
        text = "SCENARIO: " + s_message;
        error = s_messageIsError;
    }
    if (outputWidth <= 0 || outputHeight <= 0)
        return;
    ImDrawList* list = ImGui::GetForegroundDrawList();
    const float wrap = static_cast<float>(outputWidth) - 28.0f;
    const ImVec2 size = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap);
    const float y = static_cast<float>(outputHeight) - size.y - 16.0f;
    list->AddRectFilled(ImVec2(6, y - 4), ImVec2(14 + size.x, y + size.y + 4), IM_COL32(0, 0, 0, 215), 3);
    list->AddText(nullptr, 0.0f, ImVec2(10, y), error ? IM_COL32(255, 110, 90, 255) : IM_COL32(140, 255, 140, 255),
        text.c_str(), nullptr, wrap);
#else
    (void)outputWidth;
    (void)outputHeight;
#endif
}
