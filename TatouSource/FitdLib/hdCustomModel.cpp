///////////////////////////////////////////////////////////////////////////////
// TatouHd custom models (see hdCustomModel.h)
///////////////////////////////////////////////////////////////////////////////
#include "common.h"

#ifdef AITD_HD_REPAINT_BUILD
#include "hdCustomModel.h"
#include "hdBackground.h"
#include "modelAtlas.h"
#include "consoleLog.h"
#include "anim.h"
#include "renderer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

sBody* createBodyFromPtr(void* ptr);

namespace
{
// Engine limits (renderer.cpp): pointBuffer NUM_MAX_POINT_IN_POINT_BUFFER (ASSERT size < 800),
// primTable NUM_MAX_PRIM_ENTRY 500 (one entry per primitive), NUM_MAX_VERTEX_IN_PRIM 64.
// They are only ASSERTs there, so a model over them must be refused here.
const int kMaxVertices = 799;
const int kMaxPrimitives = 500;
const int kMaxPolyPoints = 64;
const long kMaxFileSize = 1 << 20;

struct CustomModel
{
    bool loaded = false;      // file looked at (once per session)
    sBody* body = nullptr;    // nullptr: no file, or refused
};

std::unordered_map<std::string, CustomModel> s_models;

std::string modelPath(const char* hqrName, int bodyNum)
{
    char path[160];
    snprintf(path, sizeof(path), "models_custom/%s_%03d.bin", hqrName, bodyNum);
    return path;
}

// Walks the AITD1 body format as createBodyFromPtr reads it (that parser trusts its input).
std::string checkBytes(const u8* p, long size, int* outVerts, int* outPrims, int* outGroups)
{
    long o = 0;
    auto need = [&](long n) { return o + n <= size; };
    auto u8at = [&](long at) { return p[at]; };
    auto u16at = [&](long at) { return (int)(p[at] | (p[at + 1] << 8)); };
    if (!need(16))
        return "file too short";
    const int flags = u16at(0);
    if (flags & (INFO_TORTUE | INFO_OPTIMISE))
        return "not an AITD1 model (flags)";
    o = 14;
    const int scratch = u16at(o); o += 2;
    if (scratch < 6)
        return "scratch buffer under 6 bytes (the animation timer lives there)";
    if (!need(scratch + 2))
        return "file too short (scratch)";
    o += scratch;
    const int numVerts = u16at(o); o += 2;
    if (numVerts > kMaxVertices)
        return "more than 799 vertices (engine limit)";
    if (!need(numVerts * 6L))
        return "file too short (vertices)";
    o += numVerts * 6L;
    int numGroups = 0;
    if (flags & INFO_ANIM)
    {
        if (!need(2))
            return "file too short (groups)";
        numGroups = u16at(o); o += 2;
        if (!need(numGroups * 18L))
            return "file too short (groups)";
        for (int i = 0; i < numGroups; i++, o += 2)
            if (u16at(o) % 0x10 || u16at(o) / 0x10 >= numGroups)
                return "bad group order table";
        for (int i = 0; i < numGroups; i++, o += 16)
        {
            const int start = (s16)u16at(o), count = (s16)u16at(o + 2), base = (s16)u16at(o + 4);
            const int parent = (s8)u8at(o + 6);
            if (start < 0 || start % 6 || count < 0 || start / 6 + count > numVerts)
                return "group " + std::to_string(i) + ": vertex range outside the model";
            if (base < 0 || base % 6 || base / 6 >= numVerts)
                return "group " + std::to_string(i) + ": bad pivot vertex";
            if (parent < -1 || parent >= numGroups)
                return "group " + std::to_string(i) + ": bad parent";
        }
    }
    if (!need(2))
        return "file too short (primitives)";
    const int numPrims = u16at(o); o += 2;
    if (numPrims > kMaxPrimitives)
        return "more than 500 primitives (engine limit)";
    for (int i = 0; i < numPrims; i++)
    {
        if (!need(1))
            return "file too short (primitive " + std::to_string(i) + ")";
        const int type = u8at(o); o += 1;
        int points = 0;
        if (type == primTypeEnum_Line)
        {
            if (!need(3)) return "file too short";
            o += 3; points = 2;
        }
        else if (type == primTypeEnum_Poly)
        {
            if (!need(3)) return "file too short";
            points = u8at(o); o += 3;
            if (points < 3 || points > kMaxPolyPoints)
                return "primitive " + std::to_string(i) + ": a polygon needs 3 to 64 points";
        }
        else if (type == primTypeEnum_Point || type == primTypeEnum_BigPoint || type == primTypeEnum_Zixel)
        {
            if (!need(3)) return "file too short";
            o += 3; points = 1;
        }
        else if (type == primTypeEnum_Sphere)
        {
            if (!need(5)) return "file too short";
            o += 5; points = 1;
        }
        else
            return "primitive " + std::to_string(i) + ": unsupported type " + std::to_string(type);
        if (!need(points * 2L))
            return "file too short (primitive " + std::to_string(i) + ")";
        for (int k = 0; k < points; k++, o += 2)
            if (u16at(o) % 6 || u16at(o) / 6 >= numVerts)
                return "primitive " + std::to_string(i) + ": point outside the model";
    }
    if (outVerts) *outVerts = numVerts;
    if (outPrims) *outPrims = numPrims;
    if (outGroups) *outGroups = numGroups;
    return "";
}

// Reads and checks the file; on success *body is the parsed model (its buffer stays allocated:
// createBodyFromPtr keeps a pointer to it).
std::string readModel(const char* hqrName, int bodyNum, sBody** body, int* v, int* p, int* g)
{
    const std::string path = modelPath(hqrName, bodyNum);
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return "no file";
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > kMaxFileSize)
    {
        fclose(f);
        return "bad file size";
    }
    u8* buf = (u8*)malloc(size);
    const bool ok = buf && fread(buf, 1, size, f) == (size_t)size;
    fclose(f);
    if (!ok)
    {
        free(buf);
        return "read error";
    }
    std::string why = checkBytes(buf, size, v, p, g);
    if (!why.empty())
    {
        free(buf);
        return why;
    }
    if (body)
        *body = createBodyFromPtr(buf);
    else
        free(buf);
    return "";
}

// Same skeleton: the pose is copied group by group from the game's model.
std::string checkSkeleton(const sBody* custom, const sBody* game)
{
    if ((custom->m_flags & INFO_ANIM) != (game->m_flags & INFO_ANIM))
        return "animated flag differs from the game's model";
    if (custom->m_groups.size() != game->m_groups.size())
        return "has " + std::to_string(custom->m_groups.size()) + " groups, the game's model " +
               std::to_string(game->m_groups.size());
    if (custom->m_groupOrder != game->m_groupOrder)
        return "group order differs from the game's model";
    for (size_t i = 0; i < custom->m_groups.size(); i++)
        if (custom->m_groups[i].m_orgGroup != game->m_groups[i].m_orgGroup)
            return "group " + std::to_string(i) + " has another parent than in the game's model";
    return "";
}

sBody* findCustomModel(const char* hqrName, int bodyNum, const sBody* gameBody)
{
    std::string key = std::string(hqrName) + "_" + std::to_string(bodyNum);
    CustomModel& m = s_models[key];
    if (m.loaded)
        return m.body;
    m.loaded = true;
    sBody* body = nullptr;
    int v = 0, p = 0, g = 0;
    std::string why = readModel(hqrName, bodyNum, &body, &v, &p, &g);
    if (why == "no file")
        return nullptr;
    if (why.empty())
        why = checkSkeleton(body, gameBody);
    if (!why.empty())
    {
        printf(HDBG_TAG "TatouHd custom model %s refused: %s; the game's model is drawn\n",
               modelPath(hqrName, bodyNum).c_str(), why.c_str());
        return nullptr;               // a parsed but refused body is leaked: once per session
    }
    printf(HDBG_TAG "TatouHd custom model: %s (%d vertices, %d primitives, %d groups), drawn while HD is on\n",
           modelPath(hqrName, bodyNum).c_str(), v, p, g);
    m.body = body;
    return body;
}
}

sBody* hdSetCurrentBody(int bodyNum, sBody* gameBody, const std::string& hqrName)
{
    sBody* custom = nullptr;
    if (g_gameId == AITD1 && gameBody && isHDBackgroundEnabled())
        custom = findCustomModel(hqrName.c_str(), bodyNum, gameBody);
    if (!custom)
    {
        setCurrentBodyNum(bodyNum, gameBody, hqrName);
        return gameBody;
    }
    // The animation code keeps the pose in the game's model (sGroup::m_state); the renderer
    // reads only that from a model (AnimNuage), so copying it poses the custom one.
    for (size_t i = 0; i < custom->m_groups.size(); i++)
        custom->m_groups[i].m_state = gameBody->m_groups[i].m_state;
    g_modelAtlasCustomFolder = true;
    setCurrentBodyNum(bodyNum, custom, hqrName);
    g_modelAtlasCustomFolder = false;
    return custom;
}

std::string hdCheckCustomModelFile(const char* hqrName, int bodyNum, int* vertices, int* primitives, int* groups)
{
    return readModel(hqrName, bodyNum, nullptr, vertices, primitives, groups);
}
#endif
