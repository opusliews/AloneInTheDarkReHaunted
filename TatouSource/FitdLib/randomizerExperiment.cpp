#include "common.h"
#include "vars.h"
#include "randomizerExperiment.h"
#include "zv.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

namespace
{
#ifdef AITD_RANDOMIZER_EXPERIMENT
struct LifeWordPatch
{
    int life;
    int byteOffset;
    unsigned short originalObject;
    unsigned short replacementObject;
    const char* reason;
};

// These offsets point at object-ID operands, not opcode offsets. They were
// verified against the authoritative AITD1 LISTLIFE resources and the existing
// LifeDISA/independent disassembly. Patches affect HQR cache memory only.
constexpr std::array<LifeWordPatch, 12> kLifePatches = {{
    { 0, 160,  3,  56, "chest state: rifle -> vase" },
    { 0, 170,  3,  56, "chest reward: rifle -> vase" },
    { 1, 156,  5,  15, "dresser state: rug -> letter" },
    { 1, 166,  5,  15, "dresser reward: rug -> letter" },
    { 7,  84, 13,  25, "lamp table reward: lamp -> bow" },
    { 8,  58, 15,   5, "HC18 state: letter -> rug" },
    { 8,  68, 15,   5, "HC18 reward: letter -> rug" },
    {57, 128, 48, 120, "first-aid reward: flask -> poker" },
    {57, 136, 48, 120, "first-aid state: flask -> poker" },
    {68,  42, 53,  78, "broken vase placement: key -> dagger" },
    {66, 214, 56,   3, "vase-location reward: vase -> rifle" },
    {66, 220, 56,   3, "vase-location state: vase -> rifle" },
}};

// LIFE 57 also changes the contained object's found weight after it has been
// taken. Redirect that target so it applies to the poker rather than the flask.
constexpr LifeWordPatch kFirstAidWeightPatch =
    {57, 158, 48, 120, "first-aid contained-object metadata: flask -> poker"};

unsigned short readWord(const char* data, int offset)
{
    return static_cast<unsigned short>(
        static_cast<unsigned char>(data[offset]) |
        (static_cast<unsigned short>(static_cast<unsigned char>(data[offset + 1])) << 8));
}

void writeWord(char* data, int offset, unsigned short value)
{
    data[offset] = static_cast<char>(value & 0xFF);
    data[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
}

void applyPatch(char* data, const LifeWordPatch& patch)
{
    const unsigned short found = readWord(data, patch.byteOffset);
    if (found == patch.replacementObject)
        return; // HQR cache entry was already patched.
    if (found != patch.originalObject)
    {
        std::printf("[RANDOMIZER TEST] LIFE %d offset %d mismatch: expected OBJ %u, found %u; patch skipped\n",
            patch.life, patch.byteOffset, patch.originalObject, found);
        return;
    }
    writeWord(data, patch.byteOffset, patch.replacementObject);
}

void swapPlacement(int firstId, int secondId)
{
    if (firstId < 0 || secondId < 0 ||
        firstId >= static_cast<int>(ListWorldObjets.size()) ||
        secondId >= static_cast<int>(ListWorldObjets.size()))
    {
        std::printf("[RANDOMIZER TEST] Cannot swap OBJ %d and OBJ %d: ID out of range\n",
            firstId, secondId);
        return;
    }

    tWorldObject& first = ListWorldObjets[firstId];
    tWorldObject& second = ListWorldObjets[secondId];
    std::swap(first.x, second.x);
    std::swap(first.y, second.y);
    std::swap(first.z, second.z);
    std::swap(first.alpha, second.alpha);
    std::swap(first.beta, second.beta);
    std::swap(first.gamma, second.gamma);
    std::swap(first.stage, second.stage);
    std::swap(first.room, second.room);
}

struct PickupSlot
{
    int body = -1;
    int typeZv = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int alpha = 0;
    int beta = 0;
    int gamma = 0;
    int stage = -1;
    int room = -1;
};

PickupSlot gOriginalBowPickupSlot;
bool gOriginalBowPickupSlotValid = false;

void capturePickupSlot(const tWorldObject& object, PickupSlot& slot)
{
    slot.body = object.body;
    slot.typeZv = object.typeZV;
    slot.x = object.x;
    slot.y = object.y;
    slot.z = object.z;
    slot.alpha = object.alpha;
    slot.beta = object.beta;
    slot.gamma = object.gamma;
    slot.stage = object.stage;
    slot.room = object.room;
}

bool buildPickupSlotZv(const PickupSlot& slot, ZVStruct& zv)
{
    if (slot.body < 0 || !HQ_Bodys)
        return false;

    sBody* body = HQR_Get(HQ_Bodys, slot.body);
    if (!body)
        return false;

    switch (slot.typeZv)
    {
    case 0: getZvMax(body, &zv); break;
    case 1: GiveZVObjet(body, &zv); break;
    case 2: getZvCube(body, &zv); break;
    case 3: getZvRot(body, &zv, slot.alpha, slot.beta, slot.gamma); break;
    default: return false;
    }

    zv.ZVX1 += slot.x;
    zv.ZVX2 += slot.x;
    zv.ZVY1 += slot.y;
    zv.ZVY2 += slot.y;
    zv.ZVZ1 += slot.z;
    zv.ZVZ2 += slot.z;
    return true;
}

bool lampIsAssignedToOriginalBowSlot()
{
    constexpr int kLampObject = 13;
    if (!gOriginalBowPickupSlotValid ||
        kLampObject >= static_cast<int>(ListWorldObjets.size()))
        return false;

    const tWorldObject& lamp = ListWorldObjets[kLampObject];
    return lamp.stage == gOriginalBowPickupSlot.stage &&
        lamp.room == gOriginalBowPickupSlot.room &&
        lamp.x == gOriginalBowPickupSlot.x &&
        lamp.y == gOriginalBowPickupSlot.y &&
        lamp.z == gOriginalBowPickupSlot.z;
}
#endif
}

void randomizerExperimentApplyWorldSwaps()
{
#ifdef AITD_RANDOMIZER_EXPERIMENT
    if (g_gameId != AITD1)
        return;

    // Preserve the original loose bow slot before changing any world record.
    // Its original model-derived collision volume is a proven pickup volume.
    if (25 < static_cast<int>(ListWorldObjets.size()))
    {
        capturePickupSlot(ListWorldObjets[25], gOriginalBowPickupSlot);
        gOriginalBowPickupSlotValid = true;
    }

    // 1a: exchange the world placements of the oil lamp and bow.
    swapPlacement(13, 25);
    // 1b: put the rifle at the vase's world placement. The vase inherits the
    // rifle's off-world placement and is instead awarded by LIFE 0 (chest).
    swapPlacement(3, 56);
    std::printf("[RANDOMIZER TEST] Active: slot-aware OBJ 13<->25 and OBJ 3<->56; patched LIFE 0,1,7,8,57,66,68\n");
#endif
}

char* randomizerExperimentPatchLife(int lifeNum, char* lifeData)
{
#ifdef AITD_RANDOMIZER_EXPERIMENT
    if (g_gameId != AITD1 || !lifeData)
        return lifeData;
    for (const LifeWordPatch& patch : kLifePatches)
        if (patch.life == lifeNum)
            applyPatch(lifeData, patch);
    if (kFirstAidWeightPatch.life == lifeNum)
        applyPatch(lifeData, kFirstAidWeightPatch);
#else
    (void)lifeNum;
#endif
    return lifeData;
}

bool randomizerExperimentUsesPatchedLife(int lifeNum)
{
#ifdef AITD_RANDOMIZER_EXPERIMENT
    if (g_gameId != AITD1)
        return false;
    return lifeNum == 0 || lifeNum == 1 || lifeNum == 7 || lifeNum == 8 ||
        lifeNum == 57 || lifeNum == 66 || lifeNum == 68;
#else
    (void)lifeNum;
    return false;
#endif
}

bool randomizerExperimentHandleDirectPickup(const tObject* movingActor, const ZVStruct* proposedZv)
{
#ifdef AITD_RANDOMIZER_EXPERIMENT
    constexpr int kLampObject = 13;
    if (g_gameId != AITD1 || !movingActor || !proposedZv ||
        movingActor->trackMode != 1 ||
        g_currentFloor != gOriginalBowPickupSlot.stage ||
        movingActor->room != gOriginalBowPickupSlot.room ||
        !lampIsAssignedToOriginalBowSlot())
        return false;

    ZVStruct pickupZv;
    if (!buildPickupSlotZv(gOriginalBowPickupSlot, pickupZv))
        return false;

    ZVStruct movingZv = *proposedZv;
    if (CubeIntersect(&movingZv, &pickupZv))
    {
        FoundObjet(kLampObject, 0);
        return true;
    }
#else
    (void)movingActor;
    (void)proposedZv;
#endif
    return false;
}

bool randomizerExperimentIsNonBlockingPickupActor(const tObject* actor)
{
#ifdef AITD_RANDOMIZER_EXPERIMENT
    return g_gameId == AITD1 && actor && actor->indexInWorld == 13 &&
        lampIsAssignedToOriginalBowSlot();
#else
    (void)actor;
    return false;
#endif
}
