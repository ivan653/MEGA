#include "game/GameWorld.hpp"
#include "core/Rng.hpp"
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace mega {

namespace {

bool rayIntersectAABB(const Vec3& orig, const Vec3& dir, const AABB& b, float maxDist,
                      float& outT, Vec3* outNormal = nullptr) {
    float tmin = 0.0f;
    float tmax = maxDist;
    float o[3] = {orig.x, orig.y, orig.z};
    float d[3] = {dir.x, dir.y, dir.z};
    float mn[3] = {b.min.x, b.min.y, b.min.z};
    float mx[3] = {b.max.x, b.max.y, b.max.z};
    int hitAxis = -1;
    float hitSign = 1.0f;

    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-6f) {
            if (o[i] < mn[i] || o[i] > mx[i]) return false;
        } else {
            float inv = 1.0f / d[i];
            float t1 = (mn[i] - o[i]) * inv;
            float t2 = (mx[i] - o[i]) * inv;
            float s = -1.0f;
            if (t1 > t2) {
                std::swap(t1, t2);
                s = 1.0f;
            }
            if (t1 > tmin) {
                tmin = t1;
                hitAxis = i;
                hitSign = s;
            }
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    if (tmax < 0.0f) return false;
    outT = tmin;
    if (outNormal) {
        *outNormal = {0.0f, 1.0f, 0.0f};
        if (hitAxis == 0) *outNormal = {hitSign, 0.0f, 0.0f};
        else if (hitAxis == 1) *outNormal = {0.0f, hitSign, 0.0f};
        else if (hitAxis == 2) *outNormal = {0.0f, 0.0f, hitSign};
    }
    return true;
}

} // namespace

AABB WorldItem::worldBounds() const {
    Int3 sz = orient.applyToSize(itemBaseGridSize(type));
    float hx = sz.x * Backpack3D::CELL_SIZE * 0.5f;
    float hy = sz.y * Backpack3D::CELL_SIZE;
    float hz = sz.z * Backpack3D::CELL_SIZE * 0.5f;
    return {
        {pos.x - hx, pos.y, pos.z - hz},
        {pos.x + hx, pos.y + hy, pos.z + hz}
    };
}

AABB HingeDoor::worldBounds() const {
    if (isHatch) {
        Mat4 m = Mat4::translate(hingePos) * Mat4::rotateY(closedYaw) * Mat4::rotateX(-angle);
        float halfT = thickness * 0.5f + 0.02f;
        Vec3 mn{1e9f, 1e9f, 1e9f};
        Vec3 mx{-1e9f, -1e9f, -1e9f};
        for (float lx : {0.0f, width}) {
            for (float ly : {-halfT, halfT}) {
                for (float lz : {0.0f, height}) {
                    Vec3 p = m.transformPoint({lx, ly, lz});
                    mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
                    mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
                }
            }
        }
        return {mn, mx};
    }
    float y = currentYaw();
    float c = std::cos(y), s = std::sin(y);
    // Door extends along local +X (width) and local +-Z (thickness/2)
    float dx = c * width;
    float dz = -s * width;
    float pad = thickness * 0.5f + 0.03f;
    float minX = std::min(hingePos.x, hingePos.x + dx) - pad;
    float maxX = std::max(hingePos.x, hingePos.x + dx) + pad;
    float minZ = std::min(hingePos.z, hingePos.z + dz) - pad;
    float maxZ = std::max(hingePos.z, hingePos.z + dz) + pad;
    return {{minX, hingePos.y, minZ}, {maxX, hingePos.y + height, maxZ}};
}

void GameWorld::startNewRun(uint64_t seed) {
    baseSeed = seed;
    depth = 1;
    health = settings.maxHealth;
    hunger = settings.maxHunger;
    thirst = settings.maxThirst;
    energy = settings.maxEnergy;
    oxygen = 100.0f;
    ammo = 48;
    kills = 0;
    inventory = Inventory{};
    backpack.clear();
    backpackMode = BackpackMode::OnBack;
    backpackViewYaw = 0.0f;
    backpackViewPitch = 0.0f;
    hasHeldItem = false;
    heldPlaceMode = false;
    heldItemDurability = 0;
    meleeSwingTimer = 0.0f;
    meleeCooldown = 0.0f;
    consumeLmbHeld = false;
    consumePendingRestore = 0.0f;
    resetHeldConsumableState(false);
    draggingItem = false;
    dragItemDurability = 0;
    dragOriginBackpackId = -1;
    hoveredBackpackItemId = -1;
    hoveredBackpackBlocked = false;
    hoveredGroundedBackpack = false;
    placementPreview = GhostPreviewState{};
    availableCrafts.clear();
    lockpick.close();
    flashlightOn = false;
    inventoryOpen = false;
    hoveredDoorIdx = -1;
    hoveredItemIdx = -1;
    grabbedDoorIdx = -1;
    playerStepDist = 0.0f;
    playerWasGrounded = true;
    prevPlayerVelY = 0.0f;
    hudNoticeTimer = 0.0f;
    hudNoticeText.clear();
    weapon = WeaponType::Bolter;
    gameOver = false;
    damageFlash = 0.0f;
    generateLevel(seed);
}

void GameWorld::generateLevel(uint64_t seed) {
    map = MegaGenerator::generate(seed, settings);
    player.position = map.spawnPos;
    player.velocity = {0.0f, 0.0f, 0.0f};
    player.yaw = -PI * 0.5f; // Face West toward the table with Flashlight, Batteries & Lockpicks
    player.pitch = 0.0f;
    worldStreamChanged = true;

    enemies.clear();
    doors.clear();
    worldItems.clear();
    pickups.clear();
    projectiles.clear();
    particles.clear();

    // 1) 2-meter Metal Exit Door leading East out of starter room (x=61.0, z=58..59) into spineNS1
    // Locked with a 0%-break-chance lockpick cylinder
    HingeDoor exitDoor;
    exitDoor.hingePos = {61.05f, 1.0f, 58.0f};
    exitDoor.width = 1.0f;
    exitDoor.height = 2.0f;
    exitDoor.thickness = 0.09f;
    exitDoor.closedYaw = -PI * 0.5f; // +X rotated by -PI/2 lies along +Z (58.0 .. 59.0)
    exitDoor.openSign = 1.0f;        // Swings outward into corridor (+X)
    exitDoor.maxAngle = 1.85f;
    exitDoor.isCabinet = false;
    exitDoor.isExitDoor = true;
    exitDoor.locked = true;
    exitDoor.lockSeed = seed ^ 0xE17D00BULL;
    doors.push_back(exitDoor);

    // 2) Double Hinged Cabinet Doors on the Food/Water cabinet against the North wall (x=57.36..58.64, z=56.05)
    HingeDoor cabLeft;
    cabLeft.hingePos = {57.36f, 1.04f, 56.05f};
    cabLeft.width = 0.635f;
    cabLeft.height = 1.76f;
    cabLeft.thickness = 0.05f;
    cabLeft.closedYaw = 0.0f;        // Extends along +X (57.36 .. 58.00)
    cabLeft.openSign = -1.0f;        // Swings open into room (+Z)
    cabLeft.maxAngle = 1.85f;
    cabLeft.isCabinet = true;
    int cabLeftIdx = static_cast<int>(doors.size());
    doors.push_back(cabLeft);

    HingeDoor cabRight;
    cabRight.hingePos = {58.64f, 1.04f, 56.05f};
    cabRight.width = 0.635f;
    cabRight.height = 1.76f;
    cabRight.thickness = 0.05f;
    cabRight.closedYaw = PI;         // Extends along -X (58.64 .. 58.00)
    cabRight.openSign = 1.0f;        // Swings open into room (+Z)
    cabRight.maxAngle = 1.85f;
    cabRight.isCabinet = true;
    cabRight.linkedDoorIdx = cabLeftIdx;
    int cabRightIdx = static_cast<int>(doors.size());
    doors[cabLeftIdx].linkedDoorIdx = cabRightIdx;
    doors.push_back(cabRight);

    // 3) Starter Room Items on West Table (x=55.15..56.15, z=57.0..58.8, y=1.84)
    worldItems.push_back({{55.65f, 1.84f, 58.38f}, ItemType::Flashlight, false, 0, 0, {}});
    worldItems.push_back({{55.55f, 1.84f, 57.35f}, ItemType::Battery, false, 0, 0, {}});
    worldItems.push_back({{55.78f, 1.84f, 57.50f}, ItemType::Battery, false, 0, 0, {}});
    worldItems.push_back({{55.56f, 1.84f, 57.82f}, ItemType::Lockpick, false, 0, 0, {}});
    worldItems.push_back({{55.76f, 1.84f, 57.94f}, ItemType::Lockpick, false, 0, 0, {}});
    worldItems.push_back({{55.58f, 1.84f, 58.08f}, ItemType::Lockpick, false, 0, 0, {}});

    // 4) Starter Room Items inside North Cabinet (Food on y=2.16, Water on y=1.64, full 100% remaining!)
    worldItems.push_back({{57.78f, 2.16f, 55.55f}, ItemType::Food, false, 0, 0, {}, 100});
    worldItems.push_back({{58.22f, 2.16f, 55.55f}, ItemType::Food, false, 0, 0, {}, 100});
    worldItems.push_back({{57.78f, 1.64f, 55.55f}, ItemType::Water, false, 0, 0, {}, 100});
    worldItems.push_back({{58.22f, 1.64f, 55.55f}, ItemType::Water, false, 0, 0, {}, 100});

    syncLockersFromMap();
    spawnRobotsInLoadedRegions();
}

void GameWorld::syncLockersFromMap() {
    // 1) Hideable Corridor Lockers: 90% UNLOCKED (only 10% locked!)
    for (const auto& lp : map.lockers) {
        bool exists = false;
        for (const auto& d : doors) {
            if (d.isLocker && lengthSq(d.hingePos - lp.hingePos) < 0.01f) {
                exists = true;
                break;
            }
        }
        if (exists) continue;

        HingeDoor ld;
        ld.hingePos = lp.hingePos;
        ld.width = lp.width;
        ld.height = lp.height;
        ld.thickness = 0.05f;
        ld.closedYaw = lp.closedYaw;
        ld.openSign = lp.openSign;
        ld.maxAngle = 1.90f;
        ld.isCabinet = true;
        ld.isLocker = true;
        ld.lockerInterior = lp.interiorBox;
        ld.regX = lp.regX;
        ld.regZ = lp.regZ;

        uint64_t lSeed = map.seed ^
                         (static_cast<uint64_t>(std::llround(lp.hingePos.x * 100.0f)) * 73856093ULL) ^
                         (static_cast<uint64_t>(std::llround(lp.hingePos.z * 100.0f)) * 83492791ULL);
        Rng lRng(lSeed);
        ld.locked = lRng.chance(0.10f); // 90% open, 10% locked!
        ld.lockSeed = lSeed;
        doors.push_back(ld);
    }

    // 2) Metal Corridor Storage Cabinets: Double doors (2 handles) sharing 1 lock (ALWAYS LOCKED!)
    for (const auto& cp : map.cabinets) {
        bool exists = false;
        for (const auto& d : doors) {
            if (d.isCabinet && !d.isLocker && lengthSq(d.hingePos - cp.leftHingePos) < 0.01f) {
                exists = true;
                break;
            }
        }
        if (exists) continue;

        uint64_t cSeed = map.seed ^ 0xCA81E7ULL ^
                         (static_cast<uint64_t>(std::llround(cp.leftHingePos.x * 100.0f)) * 19349663ULL) ^
                         (static_cast<uint64_t>(std::llround(cp.leftHingePos.z * 100.0f)) * 83492791ULL);

        HingeDoor leftLeaf;
        leftLeaf.hingePos = cp.leftHingePos;
        leftLeaf.width = cp.leafWidth;
        leftLeaf.height = cp.height;
        leftLeaf.thickness = 0.045f;
        leftLeaf.closedYaw = cp.leftClosedYaw;
        leftLeaf.openSign = cp.leftOpenSign;
        leftLeaf.maxAngle = 1.85f;
        leftLeaf.isCabinet = true;
        leftLeaf.isRareCabinet = cp.isRare;
        leftLeaf.locked = true; // Metal cabinets always spawn locked!
        leftLeaf.lockSeed = cSeed;
        leftLeaf.regX = cp.regX;
        leftLeaf.regZ = cp.regZ;

        HingeDoor rightLeaf = leftLeaf;
        rightLeaf.hingePos = cp.rightHingePos;
        rightLeaf.closedYaw = cp.rightClosedYaw;
        rightLeaf.openSign = cp.rightOpenSign;

        int leftIdx = static_cast<int>(doors.size());
        int rightIdx = leftIdx + 1;
        leftLeaf.linkedDoorIdx = rightIdx;
        rightLeaf.linkedDoorIdx = leftIdx;
        doors.push_back(leftLeaf);
        doors.push_back(rightLeaf);
    }

    // 2.5) Hinged Metal Doors on Walled 3x4 Dead-End Rooms
    for (const auto& rd : map.roomDoors) {
        bool exists = false;
        for (const auto& d : doors) {
            if (!d.isCabinet && !d.isLocker && !d.isExitDoor && lengthSq(d.hingePos - rd.hingePos) < 0.01f) {
                exists = true;
                break;
            }
        }
        if (exists) continue;

        HingeDoor door;
        door.hingePos = rd.hingePos;
        door.width = rd.width;
        door.height = rd.height;
        door.thickness = 0.08f;
        door.closedYaw = rd.closedYaw;
        door.openSign = rd.openSign;
        door.maxAngle = 1.85f;
        door.isCabinet = false;
        door.isLocker = false;
        door.locked = false; // Room door opens freely; rare cabinet inside holds the lock
        door.regX = rd.regX;
        door.regZ = rd.regZ;
        doors.push_back(door);
    }

    // 2.6) Hinged Metal Doors on 3x3 Sewer-Hatch Dead-End Rooms
    for (const auto& rd : map.hatchRoomDoors) {
        bool exists = false;
        for (const auto& d : doors) {
            if (!d.isCabinet && !d.isLocker && !d.isExitDoor && !d.isHatch &&
                lengthSq(d.hingePos - rd.hingePos) < 0.01f) {
                exists = true;
                break;
            }
        }
        if (exists) continue;

        HingeDoor door;
        door.hingePos = rd.hingePos;
        door.width = rd.width;
        door.height = rd.height;
        door.thickness = 0.08f;
        door.closedYaw = rd.closedYaw;
        door.openSign = rd.openSign;
        door.maxAngle = 1.85f;
        door.locked = false;
        door.regX = rd.regX;
        door.regZ = rd.regZ;
        doors.push_back(door);
    }

    // 2.7) Hinged Metal Doors Separating Parallel Sewer Tunnels (10% locked!)
    for (const auto& rd : map.sewerDoors) {
        bool exists = false;
        for (const auto& d : doors) {
            if (d.isSewerDoor && lengthSq(d.hingePos - rd.hingePos) < 0.01f) {
                exists = true;
                break;
            }
        }
        if (exists) continue;

        HingeDoor door;
        door.hingePos = rd.hingePos;
        door.width = rd.width;
        door.height = rd.height;
        door.thickness = 0.08f;
        door.closedYaw = rd.closedYaw;
        door.openSign = rd.openSign;
        door.maxAngle = 1.85f;
        door.isSewerDoor = true;
        door.locked = rd.locked;
        door.lockSeed = map.seed ^ 0x5E7ED00BULL ^
                        (static_cast<uint64_t>(std::llround(rd.hingePos.x * 100.0f)) * 19349663ULL) ^
                        (static_cast<uint64_t>(std::llround(rd.hingePos.z * 100.0f)) * 83492791ULL);
        door.regX = rd.regX;
        door.regZ = rd.regZ;
        doors.push_back(door);
    }

    // 2.8) Openable Floor Hatches in 3x3 Rooms leading down 6m ladders into Sewer Biome
    for (const auto& hp : map.hatches) {
        bool exists = false;
        for (const auto& d : doors) {
            if (d.isHatch && lengthSq(d.hingePos - hp.hingePos) < 0.01f) {
                exists = true;
                break;
            }
        }
        if (exists) continue;

        HingeDoor hatch;
        hatch.hingePos = hp.hingePos;
        hatch.width = hp.width;
        hatch.height = hp.length; // Leaf span across 1x1 shaft opening
        hatch.thickness = 0.07f;
        hatch.closedYaw = hp.closedYaw;
        hatch.openSign = 1.0f;
        hatch.maxAngle = 1.72f;   // Swings ~98.5 deg upward cleanly against room back wall
        hatch.isHatch = true;
        hatch.locked = false;
        hatch.regX = hp.regX;
        hatch.regZ = hp.regZ;
        doors.push_back(hatch);
    }

    // 3) Spawn Loot on Crates, Metal Cabinet Shelves, Shelving Racks & Lockers from configurable LootTables
    //    Uses strict 3D volume fit checks so long items NEVER clip through storage walls/shelves,
    //    and falls back to placing tall items (Brooms/Mops) neatly on the adjacent floor beside storage!
    constexpr float GRID = Backpack3D::CELL_SIZE;

    auto tryPlaceInVolume = [&](const AABB& vol, ItemType itType, Rng& rng, uint64_t durSeed,
                                int regX, int regZ, bool checkWorldSolid, WorldItem& outItem) -> bool {
        float volW = vol.max.x - vol.min.x;
        float volH = vol.max.y - vol.min.y;
        float volD = vol.max.z - vol.min.z;
        if (volW <= 0.02f || volH <= 0.02f || volD <= 0.02f) return false;

        // Visual padding for flared Broom/Mop heads so bristles/cotton never poke through cabinet walls
        float extraXZ = (itType == ItemType::Broom || itType == ItemType::Mop) ? 0.03f : 0.005f;
        int startYaw = rng.rangeInt(0, 3);

        // Try upright orientations first, then horizontal orientations (rotX=1, rotZ=1) if item is taller than cavity
        ItemOrientation candidates[8] = {
            {0, (startYaw + 0) & 3, 0},
            {0, (startYaw + 1) & 3, 0},
            {0, (startYaw + 2) & 3, 0},
            {0, (startYaw + 3) & 3, 0},
            {1, (startYaw + 0) & 3, 0},
            {1, (startYaw + 1) & 3, 0},
            {0, (startYaw + 0) & 3, 1},
            {0, (startYaw + 1) & 3, 1}
        };

        for (const auto& ori : candidates) {
            // Never lay a Broom/Mop flat on the floor beside storage — keep them standing neatly upright!
            if (checkWorldSolid && (ori.rotX != 0 || ori.rotZ != 0) &&
                (itType == ItemType::Broom || itType == ItemType::Mop || itType == ItemType::Stick || itType == ItemType::Spear)) {
                continue;
            }
            Int3 sz = ori.applyToSize(itemBaseGridSize(itType));
            float sx = sz.x * GRID + extraXZ * 2.0f;
            float sy = sz.y * GRID;
            float szM = sz.z * GRID + extraXZ * 2.0f;
            if (sx > volW || sy > volH || szM > volD) continue;

            float minX = vol.min.x + sx * 0.5f;
            float maxX = vol.max.x - sx * 0.5f;
            float minZ = vol.min.z + szM * 0.5f;
            float maxZ = vol.max.z - szM * 0.5f;

            for (int attempt = 0; attempt < 5; ++attempt) {
                float px = (maxX > minX) ? (minX + rng.nextFloat() * (maxX - minX)) : 0.5f * (minX + maxX);
                float pz = (maxZ > minZ) ? (minZ + rng.nextFloat() * (maxZ - minZ)) : 0.5f * (minZ + maxZ);
                float snappedX = std::round(px / GRID) * GRID;
                float snappedZ = std::round(pz / GRID) * GRID;
                px = (snappedX >= minX && snappedX <= maxX) ? snappedX : clampf(px, minX, maxX);
                pz = (snappedZ >= minZ && snappedZ <= maxZ) ? snappedZ : clampf(pz, minZ, maxZ);

                int dur = defaultItemDurability(itType, durSeed);
                WorldItem cand{{px, vol.min.y, pz}, itType, false, regX, regZ, ori, dur};
                AABB cb = cand.worldBounds();

                // Verify strict 3D containment inside target volume
                if (cb.min.x < vol.min.x - 0.002f || cb.max.x > vol.max.x + 0.002f ||
                    cb.min.y < vol.min.y - 0.002f || cb.max.y > vol.max.y + 0.002f ||
                    cb.min.z < vol.min.z - 0.002f || cb.max.z > vol.max.z + 0.002f) {
                    continue;
                }

                // For adjacent floor spots, also ensure zero collision with walls or props
                if (checkWorldSolid) {
                    bool blocked = false;
                    int ix0 = static_cast<int>(std::floor(cb.min.x + 0.005f));
                    int ix1 = static_cast<int>(std::floor(cb.max.x - 0.005f));
                    int iy0 = static_cast<int>(std::floor(cb.min.y + 0.005f));
                    int iy1 = static_cast<int>(std::floor(cb.max.y - 0.005f));
                    int iz0 = static_cast<int>(std::floor(cb.min.z + 0.005f));
                    int iz1 = static_cast<int>(std::floor(cb.max.z - 0.005f));
                    for (int y = iy0; y <= iy1 && !blocked; ++y)
                        for (int z = iz0; z <= iz1 && !blocked; ++z)
                            for (int x = ix0; x <= ix1 && !blocked; ++x)
                                if (map.world.getCell(x, y, z).isSolid()) blocked = true;
                    if (blocked) continue;

                    for (const auto& pc : map.world.propColliders()) {
                        if (cb.intersects(pc)) { blocked = true; break; }
                    }
                    if (blocked) continue;
                }

                bool overlap = false;
                for (const auto& wi : worldItems) {
                    if (!wi.pickedUp && cb.intersects(wi.worldBounds())) {
                        overlap = true;
                        break;
                    }
                }
                if (!overlap) {
                    outItem = cand;
                    return true;
                }
            }
        }
        return false;
    };

    for (const auto& ls : map.lootSurfaces) {
        Vec3 surfCenter{
            0.5f * (ls.surfaceBox.min.x + ls.surfaceBox.max.x),
            ls.surfaceBox.min.y,
            0.5f * (ls.surfaceBox.min.z + ls.surfaceBox.max.z)
        };
        // Avoid spawning twice on the same surface across region stream ticks
        bool alreadyHasItem = false;
        for (const auto& wi : worldItems) {
            if (std::abs(wi.pos.y - surfCenter.y) < 0.08f &&
                wi.pos.x >= ls.surfaceBox.min.x - 0.04f && wi.pos.x <= ls.surfaceBox.max.x + 0.04f &&
                wi.pos.z >= ls.surfaceBox.min.z - 0.04f && wi.pos.z <= ls.surfaceBox.max.z + 0.04f) {
                alreadyHasItem = true;
                break;
            }
        }
        if (alreadyHasItem) continue;

        uint64_t sSeed = map.seed ^ 0x10075EEDULL ^
                         (static_cast<uint64_t>(std::llround(surfCenter.x * 100.0f)) * 73856093ULL) ^
                         (static_cast<uint64_t>(std::llround(surfCenter.y * 100.0f)) * 19349663ULL) ^
                         (static_cast<uint64_t>(std::llround(surfCenter.z * 100.0f)) * 83492791ULL);
        Rng sRng(sSeed);

        const auto& tbl = lootTables.get(ls.source);
        if (tbl.maxItems <= 0 || !sRng.chance(tbl.spawnChance)) continue;

        int count = sRng.rangeInt(1, tbl.maxItems);
        for (int k = 0; k < count; ++k) {
            ItemType itType = tbl.rollItem(sRng);
            uint64_t durSeed = sSeed ^ static_cast<uint64_t>((k + 1) * 2654435761ULL);
            WorldItem placed{};

            // Step A: Try fitting strictly inside the storage 3D volume (never pokes through walls/shelves!)
            if (tryPlaceInVolume(ls.surfaceBox, itType, sRng, durSeed, ls.regX, ls.regZ, false, placed)) {
                worldItems.push_back(placed);
                continue;
            }
            // Step B: If too long/tall for inside storage (e.g. Broom/Mop), stand it neatly beside storage!
            if (ls.hasAdjacentFloor &&
                tryPlaceInVolume(ls.adjacentFloorBox, itType, sRng, durSeed, ls.regX, ls.regZ, true, placed)) {
                worldItems.push_back(placed);
                continue;
            }
            // Step C: Fallback re-rolls for smaller items that DO fit inside the storage cavity
            for (int retry = 0; retry < 3; ++retry) {
                ItemType altType = tbl.rollItem(sRng);
                if (tryPlaceInVolume(ls.surfaceBox, altType, sRng, durSeed ^ static_cast<uint64_t>(retry * 97),
                                     ls.regX, ls.regZ, false, placed)) {
                    worldItems.push_back(placed);
                    break;
                }
            }
        }
    }
}

int GameWorld::playerHiddenLockerIdx() const {
    AABB pBox = player.boundingBox();
    for (size_t i = 0; i < doors.size(); ++i) {
        if (!doors[i].isLocker) continue;
        if (doors[i].angle < 0.55f && doors[i].lockerInterior.intersects(pBox)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void GameWorld::spawnRobotsInLoadedRegions() {
    // Remove robots from unloaded regions
    int keepRadius = settings.streamRadius + 1;
    enemies.erase(
        std::remove_if(enemies.begin(), enemies.end(),
                       [&](const Enemy& e) {
                           return std::abs(e.regX - map.currentRegX) > keepRadius ||
                                  std::abs(e.regZ - map.currentRegZ) > keepRadius;
                       }),
        enemies.end());

    // Spawn 2-3 robots in each newly loaded region
    for (const auto& reg : map.loadedRegions) {
        int rx = reg.first, rz = reg.second;
        int existing = 0;
        for (const auto& e : enemies) {
            if (e.regX == rx && e.regZ == rz) existing++;
        }
        if (existing > 0) continue;

        std::vector<const CorridorSegment*> regSegs;
        for (const auto& seg : map.corridors) {
            if (seg.regX == rx && seg.regZ == rz) regSegs.push_back(&seg);
        }
        if (regSegs.empty()) continue;

        uint64_t rSeed = map.seed ^ (static_cast<uint64_t>(static_cast<uint32_t>(rx)) * 0x9E3779B9ULL) ^
                         (static_cast<uint64_t>(static_cast<uint32_t>(rz)) * 0xBF58476DULL);
        Rng rng(rSeed);
        int want = rng.rangeInt(2, 3);

        for (int i = 0; i < want * 4 && existing < want; ++i) {
            const auto* seg = regSegs[rng.rangeInt(0, static_cast<int>(regSegs.size()) - 1)];
            float px, pz;
            int dx = 0, dz = 0;
            if (seg->alongX) {
                int minX = std::min(seg->x0, seg->x1) + 4;
                int maxX = std::max(seg->x0, seg->x1) - 4;
                if (maxX <= minX) continue;
                px = static_cast<float>(rng.rangeInt(minX, maxX)) + 0.5f;
                pz = static_cast<float>(seg->z0) + seg->width * 0.5f;
                dx = rng.chance(0.5f) ? 1 : -1;
            } else {
                int minZ = std::min(seg->z0, seg->z1) + 4;
                int maxZ = std::max(seg->z0, seg->z1) - 4;
                if (maxZ <= minZ) continue;
                px = static_cast<float>(seg->x0) + seg->width * 0.5f;
                pz = static_cast<float>(rng.rangeInt(minZ, maxZ)) + 0.5f;
                dz = rng.chance(0.5f) ? 1 : -1;
            }

            Vec3 cand{px, 1.0f, pz};
            // Robots ONLY spawn within 500m of the 2km^2 biome edge (max(|x-64|, |z-64|) >= 204m)
            if (!MegaGenerator::isRobotSpawnAllowed(px, pz)) continue;
            if (lengthSq(cand - map.spawnPos) < 14.0f * 14.0f) continue;

            int ix = static_cast<int>(std::floor(px));
            int iz = static_cast<int>(std::floor(pz));
            if (map.world.getBlock(ix, 1, iz) != BlockType::Air ||
                map.world.getBlock(ix, 2, iz) != BlockType::Air) {
                continue;
            }

            Enemy e;
            e.pos = cand;
            e.dirX = dx;
            e.dirZ = dz;
            e.pendingDirX = dx;
            e.pendingDirZ = dz;
            e.yaw = std::atan2(static_cast<float>(dx), static_cast<float>(dz));
            e.movePulseTimer = rng.nextFloat() * 1.5f;
            e.regX = rx;
            e.regZ = rz;
            enemies.push_back(e);
            existing++;
        }
    }
}

float GameWorld::raycastWorld(const Vec3& origin, const Vec3& dir, float maxDist,
                              Vec3* outNormal) const {
    // 3D Amanatides-Woo DDA voxel traversal
    int x = static_cast<int>(std::floor(origin.x));
    int y = static_cast<int>(std::floor(origin.y));
    int z = static_cast<int>(std::floor(origin.z));

    int stepX = (dir.x > 0.0f) ? 1 : (dir.x < 0.0f ? -1 : 0);
    int stepY = (dir.y > 0.0f) ? 1 : (dir.y < 0.0f ? -1 : 0);
    int stepZ = (dir.z > 0.0f) ? 1 : (dir.z < 0.0f ? -1 : 0);

    float tDeltaX = (stepX != 0) ? std::abs(1.0f / dir.x) : 1e30f;
    float tDeltaY = (stepY != 0) ? std::abs(1.0f / dir.y) : 1e30f;
    float tDeltaZ = (stepZ != 0) ? std::abs(1.0f / dir.z) : 1e30f;

    float tMaxX = (stepX > 0) ? (std::floor(origin.x) + 1.0f - origin.x) * tDeltaX
                              : (origin.x - std::floor(origin.x)) * tDeltaX;
    float tMaxY = (stepY > 0) ? (std::floor(origin.y) + 1.0f - origin.y) * tDeltaY
                              : (origin.y - std::floor(origin.y)) * tDeltaY;
    float tMaxZ = (stepZ > 0) ? (std::floor(origin.z) + 1.0f - origin.z) * tDeltaZ
                              : (origin.z - std::floor(origin.z)) * tDeltaZ;
    if (stepX == 0) tMaxX = 1e30f;
    if (stepY == 0) tMaxY = 1e30f;
    if (stepZ == 0) tMaxZ = 1e30f;

    float bestT = maxDist;
    Vec3 bestN{0.0f, 1.0f, 0.0f};
    float t = 0.0f;
    int lastAxis = 1;
    for (int i = 0; i < 256 && t < maxDist; ++i) {
        if (map.world.getCell(x, y, z).isOpaque()) {
            bestT = t;
            if (lastAxis == 0) bestN = {static_cast<float>(-stepX), 0.0f, 0.0f};
            else if (lastAxis == 1) bestN = {0.0f, static_cast<float>(-stepY), 0.0f};
            else bestN = {0.0f, 0.0f, static_cast<float>(-stepZ)};
            break;
        }
        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            t = tMaxX;
            tMaxX += tDeltaX;
            x += stepX;
            lastAxis = 0;
        } else if (tMaxY < tMaxZ) {
            t = tMaxY;
            tMaxY += tDeltaY;
            y += stepY;
            lastAxis = 1;
        } else {
            t = tMaxZ;
            tMaxZ += tDeltaZ;
            z += stepZ;
            lastAxis = 2;
        }
    }

    // Also test against 3D solid props (locker walls, cabinets, crates, tables, shelves)
    Vec3 endPt = origin + dir * bestT;
    AABB rayBounds{
        {std::min(origin.x, endPt.x) - 0.05f, std::min(origin.y, endPt.y) - 0.05f, std::min(origin.z, endPt.z) - 0.05f},
        {std::max(origin.x, endPt.x) + 0.05f, std::max(origin.y, endPt.y) + 0.05f, std::max(origin.z, endPt.z) + 0.05f}
    };
    for (const auto& pc : map.world.propColliders()) {
        if (!rayBounds.intersects(pc)) continue;
        float pt = 0.0f;
        Vec3 pn{};
        if (rayIntersectAABB(origin, dir, pc, bestT, pt, &pn)) {
            if (pt < bestT) {
                bestT = pt;
                bestN = pn;
            }
        }
    }
    if (outNormal) *outNormal = bestN;
    return bestT;
}

void GameWorld::fireWeapon(AudioEngine& audio) {
    if (gameOver || fireCooldown > 0.0f) return;

    int cost = (weapon == WeaponType::Bolter) ? 1 : 2;
    if (ammo < cost) return;
    ammo -= cost;

    fireCooldown = (weapon == WeaponType::Bolter) ? 0.18f : 0.62f;
    audio.play(weapon == WeaponType::Bolter ? SoundId::ShootBolter : SoundId::ShootShotgun);

    int pellets = (weapon == WeaponType::Bolter) ? 1 : 6;
    float spread = (weapon == WeaponType::Bolter) ? 0.012f : 0.075f;
    float damage = (weapon == WeaponType::Bolter) ? 22.0f : 14.0f;

    Vec3 origin = player.eyePosition();
    Vec3 fwd = player.forwardDir();
    Vec3 right = player.rightDir();
    Vec3 up = cross(right, fwd);

    Rng rng(static_cast<uint64_t>(ammo * 7919 + kills * 101 + origin.x * 100));

    for (int p = 0; p < pellets; ++p) {
        float sx = (rng.nextFloat() - 0.5f) * 2.0f * spread;
        float sy = (rng.nextFloat() - 0.5f) * 2.0f * spread;
        Vec3 dir = normalize(fwd + right * sx + up * sy);
        float wallDist = raycastWorld(origin, dir, 80.0f);

        Enemy* hitEnemy = nullptr;
        float bestT = wallDist;

        for (auto& e : enemies) {
            if (!e.alive) continue;
            Vec3 center = {e.pos.x, e.pos.y + e.height * 0.5f, e.pos.z};
            Vec3 oc = center - origin;
            float proj = dot(oc, dir);
            if (proj <= 0.0f || proj >= bestT) continue;
            Vec3 closest = origin + dir * proj;
            float dx = closest.x - center.x;
            float dz = closest.z - center.z;
            float dy = closest.y - center.y;
            if (dx * dx + dz * dz <= e.radius * e.radius * 1.6f &&
                std::abs(dy) <= e.height * 0.58f) {
                bestT = proj;
                hitEnemy = &e;
            }
        }

        Vec3 hitPos = origin + dir * bestT;
        for (int s = 0; s < 3; ++s) {
            particles.push_back({
                hitPos,
                {(rng.nextFloat() - 0.5f) * 4.0f, rng.nextFloat() * 3.0f, (rng.nextFloat() - 0.5f) * 4.0f},
                0.22f,
                hitEnemy ? Vec3{1.0f, 0.25f, 0.2f} : Vec3{1.0f, 0.85f, 0.3f}
            });
        }

        if (hitEnemy) {
            hitEnemy->hp -= damage;
            hitEnemy->hurtFlash = 0.12f;
            hitEnemy->alerted = true;
            audio.play(SoundId::HitEnemy);
            if (hitEnemy->hp <= 0.0f) {
                hitEnemy->alive = false;
                kills++;
            }
        }
    }
}

BiomeType GameWorld::currentBiome() const {
    return (player.position.y < -1.5f) ? BiomeType::CorrosionSewers : BiomeType::UtilityComplex;
}

const char* GameWorld::currentBiomeName() const {
    return getBiomePalette(currentBiome()).name;
}

void GameWorld::update(const PlayerInput& input, bool firePressed, float dt, AudioEngine& audio) {
    if (gameOver) return;

    if (fireCooldown > 0.0f) fireCooldown -= dt;
    if (meleeSwingTimer > 0.0f) meleeSwingTimer = std::max(0.0f, meleeSwingTimer - dt);
    if (meleeCooldown > 0.0f) meleeCooldown = std::max(0.0f, meleeCooldown - dt);
    if (damageFlash > 0.0f) damageFlash = std::max(0.0f, damageFlash - dt * 2.5f);
    if (hudNoticeTimer > 0.0f) hudNoticeTimer = std::max(0.0f, hudNoticeTimer - dt);

    // Collect closed/partially closed door colliders near player (for player physics) and within sound range
    std::vector<AABB> playerDoorColliders;
    std::vector<AABB> audioDoorColliders;
    float maxAudioDistSq = settings.soundMaxDist * settings.soundMaxDist;
    for (const auto& d : doors) {
        if (d.angle < 0.85f) {
            float distSq = lengthSq(d.hingePos - player.position);
            if (distSq <= 16.0f) {
                playerDoorColliders.push_back(d.worldBounds());
            }
            if (distSq <= maxAudioDistSq) {
                audioDoorColliders.push_back(d.worldBounds());
            }
        }
    }

    // Update 3D acoustic listener & obstacle snapshot for spatial audio + echo
    audio.setListener(player.eyePosition(), player.forwardDir(), player.rightDir(),
                      &map.world, audioDoorColliders);

    // Stamina/Energy & Thirst gating for sprint & jump (bypassed in God Mode / Noclip)
    PlayerInput modInput = input;
    if (godNoclip) modInput.noclip = true;
    bool tryingSprint = modInput.sprint && (std::abs(modInput.moveForward) > 0.1f || std::abs(modInput.moveRight) > 0.1f);
    if (!godMode && !modInput.noclip) {
        if (thirst <= 1.0f || energy <= 2.0f) {
            modInput.sprint = false;
            tryingSprint = false;
        }
        if (modInput.jump && energy < settings.energyJumpCost * 0.8f) {
            modInput.jump = false;
        }
    }
    bool didJump = modInput.jump && player.grounded && !modInput.noclip;

    // Build active ladder volumes (only active if floor hatch is open OR player is already inside shaft below!)
    std::vector<AABB> activeLadderVolumes;
    activeLadderVolumes.reserve(map.ladders.size());
    for (const auto& lad : map.ladders) {
        bool blockedByClosedHatch = false;
        if (player.position.y >= lad.topY - 0.18f) {
            for (const auto& d : doors) {
                if (d.isHatch && d.angle < 0.85f) {
                    AABB hb = d.worldBounds();
                    if (hb.intersects(lad.climbBox)) {
                        blockedByClosedHatch = true;
                        break;
                    }
                }
            }
        }
        if (!blockedByClosedHatch) {
            activeLadderVolumes.push_back(lad.climbBox);
        }
    }

    Vec3 prevPlayerPos = player.position;
    prevPlayerVelY = player.velocity.y;
    player.update(modInput, map.world, dt, playerDoorColliders,
                  settings.moveSpeed * godSpeedMult, settings.sprintMult, settings.jumpSpeed,
                  activeLadderVolumes);

    // Realistic player footsteps on floor, landing & metallic ladder rungs (muted while flying in Noclip)
    if (player.onLadder && !modInput.noclip) {
        float climbDelta = std::abs(player.position.y - prevPlayerPos.y);
        playerStepDist += climbDelta;
        if (playerStepDist >= 0.42f) {
            playerStepDist -= 0.42f;
            float jitter = std::fmod(std::abs(player.position.y * 11.3f), 1.0f);
            audio.play(SoundId::PlayerStep, 0.55f, jitter, 0.65f);
        }
    } else if (player.grounded && !modInput.noclip) {
        if (!playerWasGrounded && prevPlayerVelY < -2.4f) {
            float landVol = clampf((-prevPlayerVelY - 2.0f) / 7.5f, 0.35f, 1.0f);
            audio.play(SoundId::PlayerStep, landVol, 0.25f, 0.9f);
            playerStepDist = 0.0f;
        } else {
            float stepDelta = std::hypot(player.position.x - prevPlayerPos.x,
                                         player.position.z - prevPlayerPos.z);
            playerStepDist += stepDelta;
            float strideLen = tryingSprint ? 2.15f : 1.72f;
            if (playerStepDist >= strideLen) {
                playerStepDist -= strideLen;
                float jitter = std::fmod(std::abs(player.position.x * 13.7f + player.position.z * 7.3f), 1.0f);
                audio.play(SoundId::PlayerStep, tryingSprint ? 0.95f : 0.65f, jitter, tryingSprint ? 1.0f : 0.25f);
            }
        }
    }
    playerWasGrounded = player.grounded;

    // Update Hunger, Thirst, Energy (Stamina) & Flashlight Battery from GameSettings
    if (godMode) {
        health = settings.maxHealth;
        hunger = settings.maxHunger;
        thirst = settings.maxThirst;
        energy = settings.maxEnergy;
    } else {
        hunger = std::max(0.0f, hunger - settings.hungerDrainRate * dt);
        thirst = std::max(0.0f, thirst - (tryingSprint ? settings.thirstSprintDrainRate : settings.thirstDrainRate) * dt);

        if (tryingSprint) {
            energy = std::max(0.0f, energy - settings.energySprintDrainRate * dt);
        } else {
            float regenScale = (hunger > settings.maxHunger * 0.15f ? 1.0f : 0.45f) *
                               (thirst > settings.maxThirst * 0.15f ? 1.0f : 0.45f);
            energy = std::min(settings.maxEnergy, energy + settings.energyRegenRate * regenScale * dt);
        }
        if (didJump) {
            energy = std::max(0.0f, energy - settings.energyJumpCost);
        }

        if (hunger <= 0.0f) health -= settings.starvationDamageRate * dt;
        if (thirst <= 0.0f) health -= settings.dehydrationDamageRate * dt;

        if (flashlightOn) {
            if (!inventory.hasFlashlight || inventory.flashlightBattery <= 0.0f) {
                flashlightOn = false;
            } else {
                inventory.flashlightBattery = std::max(0.0f, inventory.flashlightBattery - settings.batteryDrainRate * dt);
                if (inventory.flashlightBattery <= 0.0f) flashlightOn = false;
            }
        }
    }

    // Update hinged door physics (Amnesia-style inertia, friction creaking & realistic collision sounds!)
    for (auto& d : doors) {
        if (std::abs(d.angularVel) < 1e-4f) continue;
        float prevAngle = d.angle;
        float prevVel = d.angularVel;
        float nextAngle = clampf(d.angle + d.angularVel * dt, 0.0f, d.maxAngle);
        d.angle = nextAngle;

        // Check if swinging vertical door leaf collides with any solid WorldGrid wall block (skip for floor hatches)
        bool hitWall = false;
        Vec3 impactPos = d.hingePos + Vec3{0.0f, 0.25f, 0.0f};
        if (!d.isHatch) {
            float y = d.currentYaw();
            Vec3 dir{std::cos(y), 0.0f, -std::sin(y)};
            impactPos = d.hingePos + dir * (d.width * 0.85f) + Vec3{0.0f, d.height * 0.5f, 0.0f};
            for (float frac : {0.42f, 0.70f, 0.94f}) {
                Vec3 p = d.hingePos + dir * (d.width * frac);
                int ix = static_cast<int>(std::floor(p.x));
                int iz = static_cast<int>(std::floor(p.z));
                int iyLow = static_cast<int>(std::floor(d.hingePos.y + 0.35f));
                int iyHigh = static_cast<int>(std::floor(d.hingePos.y + d.height - 0.25f));
                if (map.world.getCell(ix, iyLow, iz).isSolid() ||
                    map.world.getCell(ix, iyHigh, iz).isSolid()) {
                    hitWall = true;
                    impactPos = p + Vec3{0.0f, d.height * 0.5f, 0.0f};
                    break;
                }
            }
        }

        // Physical hinge friction creaking proportional to angular movement
        float speedAbs = std::abs(prevVel);
        if (!hitWall && speedAbs > 0.14f) {
            d.creakAccum += speedAbs * dt;
            float creakThresh = d.isCabinet ? 0.34f : 0.25f;
            if (d.creakAccum >= creakThresh) {
                d.creakAccum -= creakThresh;
                Vec3 hingeMid = d.hingePos + Vec3{0.0f, d.height * 0.5f, 0.0f};
                float cVol = clampf(speedAbs * 0.26f, 0.16f, 0.92f);
                float speedParam = clampf(speedAbs / 4.2f, 0.1f, 1.0f);
                float doorMassParam = d.isCabinet ? 0.0f : 1.0f;
                audio.play3D(SoundId::DoorCreak, hingeMid, cVol, speedParam, doorMassParam);
            }
        }

        if (hitWall) {
            d.angle = prevAngle;
            d.angularVel = 0.0f;
            if (speedAbs > 0.30f) {
                float impVol = clampf(speedAbs / 3.8f, 0.22f, 1.0f);
                audio.play3D(SoundId::DoorImpact, impactPos, impVol, d.isCabinet ? 0.0f : 1.0f, 1.0f);
            }
        } else {
            bool slammedClosed = (prevAngle > 0.02f && d.angle <= 0.0f);
            bool hitHingeStop  = (prevAngle < d.maxAngle - 0.02f && d.angle >= d.maxAngle);
            if (slammedClosed || hitHingeStop) {
                if (speedAbs > 0.35f) {
                    float impVol = clampf(speedAbs / 4.0f, 0.20f, 0.95f);
                    audio.play3D(SoundId::DoorImpact, impactPos, impVol, d.isCabinet ? 0.0f : 1.0f,
                                 slammedClosed ? 0.75f : 0.45f);
                }
                d.angularVel = 0.0f;
            }
            d.angularVel *= std::exp(-6.5f * dt);
        }
    }

    // Update lockpick minigame physics in real time
    if (lockpick.active) {
        lockpick.update(dt, audio);
        if (lockpick.pickSnappedJustNow) {
            lockpick.pickSnappedJustNow = false;
            if (hasHeldItem && heldItemType == ItemType::Lockpick) {
                hasHeldItem = false;
            } else {
                int lpId = backpack.findFirstAccessible(ItemType::Lockpick);
                if (lpId < 0) {
                    // Fallback: consume any lockpick in backpack if one was used
                    for (const auto& it : backpack.items) {
                        if (it.type == ItemType::Lockpick) { lpId = it.id; break; }
                    }
                }
                if (lpId >= 0) backpack.removeById(lpId);
                else inventory.lockpicks = std::max(0, inventory.lockpicks - 1);
            }
            syncInventoryCounts();
            if (inventory.lockpicks <= 0) {
                hudNoticeText = "OUT OF LOCKPICKS";
                hudNoticeTimer = 2.2f;
                lockpick.close();
            } else {
                hudNoticeText = "LOCKPICK SNAPPED - REPLACING";
                hudNoticeTimer = 1.5f;
            }
        } else if (lockpick.unlockedJustNow) {
            if (lockpick.targetDoorIdx >= 0 && lockpick.targetDoorIdx < static_cast<int>(doors.size())) {
                auto& d = doors[lockpick.targetDoorIdx];
                d.locked = false; // Stays unlocked permanently
                d.angularVel = 2.4f;
                // Double-door cabinets share 1 lock: unlocking one leaf unlocks both!
                if (d.linkedDoorIdx >= 0 && d.linkedDoorIdx < static_cast<int>(doors.size())) {
                    doors[d.linkedDoorIdx].locked = false;
                    doors[d.linkedDoorIdx].angularVel = 2.1f;
                }
            }
            lockpick.unlockedJustNow = false;
            lockpick.close();
        }
    }

    // Update hovered interactable object & realistic held food/water consumption
    updateHover();
    updateHeldConsumption(dt, audio);
    if (grabbedDoorIdx >= 0 && grabbedDoorIdx < static_cast<int>(doors.size())) {
        if (length(doors[grabbedDoorIdx].hingePos - player.eyePosition()) > 3.2f) {
            grabbedDoorIdx = -1;
        }
    }

    // Infinite map streaming check as player moves across regions
    int rx = WorldGrid::floorDiv(static_cast<int>(std::floor(player.position.x)), cfg::REGION_SIZE);
    int rz = WorldGrid::floorDiv(static_cast<int>(std::floor(player.position.z)), cfg::REGION_SIZE);
    if (rx != map.currentRegX || rz != map.currentRegZ) {
        if (MegaGenerator::updateInfiniteStream(map, player.position, settings)) {
            worldStreamChanged = true;
            syncLockersFromMap();
            spawnRobotsInLoadedRegions();
        }
    }

    if (firePressed) {
        fireWeapon(audio);
    }

    // Survival resource drain handled above (hunger, thirst, stamina, flashlight battery)

    // Pickups
    for (auto& p : pickups) {
        if (p.collected) continue;
        if (lengthSq(p.pos - player.position) < 1.35f * 1.35f) {
            p.collected = true;
            audio.play(SoundId::Pickup);
            switch (p.kind) {
                case SpriteType::PickupMedkit: health = std::min(100.0f, health + 28.0f); break;
                case SpriteType::PickupOxygen: oxygen = std::min(100.0f, oxygen + 38.0f); break;
                case SpriteType::PickupAmmo:   ammo += 16; break;
                case SpriteType::PickupEnergy: energy = std::min(100.0f, energy + 45.0f); break;
                default: break;
            }
        }
    }

    // Exit beacon check -> descend deeper into megastructure
    if (lengthSq(map.exitPos - player.position) < 1.8f * 1.8f) {
        depth++;
        audio.play(SoundId::LevelDescend);
        generateLevel(baseSeed + static_cast<uint64_t>(depth) * 7919ULL);
        return;
    }

    // Robot Enemies AI
    Vec3 target = player.eyePosition();
    auto isWalkable = [&](float x, float z) -> bool {
        int ix = static_cast<int>(std::floor(x));
        int iz = static_cast<int>(std::floor(z));
        return map.world.getCell(ix, 0, iz).isSolid() &&
               map.world.getBlock(ix, 1, iz) == BlockType::Air &&
               map.world.getBlock(ix, 2, iz) == BlockType::Air;
    };

    // Checks robot body footprint against both WorldGrid walls and nearby prop colliders
    auto isFootprintClear = [&](float x, float z, float rad, const std::vector<AABB>& props) -> bool {
        if (!isWalkable(x - rad, z - rad) || !isWalkable(x + rad, z - rad) ||
            !isWalkable(x - rad, z + rad) || !isWalkable(x + rad, z + rad)) {
            return false;
        }
        AABB body{{x - rad, 1.05f, z - rad}, {x + rad, 2.35f, z + rad}};
        for (const auto& pc : props) {
            if (body.intersects(pc)) return false;
        }
        return true;
    };

    // Measure exact corridor centerline & half-width along perpendicular axis (exact integer voxel walls, zero jitter)
    auto measureCorridorCenter = [&](float x, float z, int axisX, int axisZ,
                                     float& outCenter, float& outHalfWidth) -> bool {
        (void)axisX;
        bool scanX = (axisZ != 0); // Moving along Z -> corridor width is along X
        int ix = static_cast<int>(std::floor(x));
        int iz = static_cast<int>(std::floor(z));
        int base = scanX ? ix : iz;
        int minC = base;
        int maxC = base;
        while (minC > base - 4) {
            int cx = scanX ? (minC - 1) : ix;
            int cz = scanX ? iz : (minC - 1);
            if (!isWalkable(cx + 0.5f, cz + 0.5f)) break;
            minC--;
        }
        while (maxC < base + 4) {
            int cx = scanX ? (maxC + 1) : ix;
            int cz = scanX ? iz : (maxC + 1);
            if (!isWalkable(cx + 0.5f, cz + 0.5f)) break;
            maxC++;
        }
        int span = maxC - minC + 1;
        if (span > 5) return false; // Wide open intersection/hall, not a bounded corridor
        outCenter = 0.5f * static_cast<float>(minC + maxC + 1);
        outHalfWidth = 0.5f * static_cast<float>(span);
        return true;
    };

    // Center-seeking 8-neighbor weighted A* (flat local 81x81 grid, zero heap hash maps, no corner cutting)
    auto findNextStepAStar = [&](const Vec3& fromPos, const Vec3& toPos) -> Vec3 {
        int sx = static_cast<int>(std::floor(fromPos.x));
        int sz = static_cast<int>(std::floor(fromPos.z));
        int gx = static_cast<int>(std::floor(toPos.x));
        int gz = static_cast<int>(std::floor(toPos.z));
        if (sx == gx && sz == gz) return toPos;

        constexpr int R = 40;
        constexpr int W = R * 2 + 1; // 81
        constexpr int N = W * W;     // 6561

        if (std::abs(gx - sx) > R || std::abs(gz - sz) > R) {
            gx = std::clamp(gx, sx - R, sx + R);
            gz = std::clamp(gz, sz - R, sz + R);
        }

        AABB searchBounds{{fromPos.x - 42.0f, 0.5f, fromPos.z - 42.0f},
                          {fromPos.x + 42.0f, 3.0f, fromPos.z + 42.0f}};
        std::vector<AABB> localProps;
        localProps.reserve(64);
        for (const auto& pc : map.world.propColliders()) {
            if (searchBounds.intersects(pc)) localProps.push_back(pc);
        }

        // Lazy walkability cache: -1 = unknown, 0 = blocked, 1 = open
        std::array<int8_t, N> walkCache{};
        walkCache.fill(-1);

        auto isCellClear = [&](int ix, int iz) -> bool {
            int lx = ix - sx + R;
            int lz = iz - sz + R;
            if (lx < 0 || lx >= W || lz < 0 || lz >= W) return false;
            int idx = lz * W + lx;
            if (walkCache[idx] >= 0) return walkCache[idx] == 1;
            bool ok = (map.world.getBlock(ix, 1, iz) == BlockType::Air &&
                       map.world.getBlock(ix, 2, iz) == BlockType::Air);
            if (ok) {
                AABB cellBox{{ix + 0.22f, 1.0f, iz + 0.22f}, {ix + 0.78f, 2.5f, iz + 0.78f}};
                for (const auto& pc : localProps) {
                    if (cellBox.intersects(pc)) { ok = false; break; }
                }
            }
            walkCache[idx] = ok ? 1 : 0;
            return ok;
        };

        // Corridor-center preference: cells adjacent to walls/props get a higher cost so A* stays on centerline!
        auto cellCenterPenalty = [&](int ix, int iz) -> float {
            int blockedAdj = 0;
            if (!isCellClear(ix + 1, iz)) blockedAdj++;
            if (!isCellClear(ix - 1, iz)) blockedAdj++;
            if (!isCellClear(ix, iz + 1)) blockedAdj++;
            if (!isCellClear(ix, iz - 1)) blockedAdj++;
            return (blockedAdj > 0) ? (0.75f * static_cast<float>(blockedAdj)) : 0.0f;
        };

        std::array<float, N> gScore{};
        gScore.fill(1e30f);
        std::array<int16_t, N> parent{};
        parent.fill(-1);

        struct Node {
            float f;
            int16_t idx;
            bool operator>(const Node& o) const { return f > o.f; }
        };
        std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;

        int startIdx = R * W + R;
        int goalIdx = (gz - sz + R) * W + (gx - sx + R);
        gScore[startIdx] = 0.0f;
        parent[startIdx] = static_cast<int16_t>(startIdx);
        pq.push({static_cast<float>(std::hypot(gx - sx, gz - sz)), static_cast<int16_t>(startIdx)});

        static constexpr int DX[8] = {1, -1, 0, 0,  1,  1, -1, -1};
        static constexpr int DZ[8] = {0, 0, 1, -1,  1, -1,  1, -1};
        static constexpr float STEP_COST[8] = {1.0f, 1.0f, 1.0f, 1.0f, 1.4142f, 1.4142f, 1.4142f, 1.4142f};

        bool found = false;
        int explored = 0;
        while (!pq.empty() && explored < 2200) {
            auto [curF, curIdx] = pq.top();
            pq.pop();
            if (curIdx == goalIdx) {
                found = true;
                break;
            }
            int cx = (curIdx % W) - R + sx;
            int cz = (curIdx / W) - R + sz;
            explored++;

            for (int d = 0; d < 8; ++d) {
                int nx = cx + DX[d];
                int nz = cz + DZ[d];
                int nlx = nx - sx + R;
                int nlz = nz - sz + R;
                if (nlx < 0 || nlx >= W || nlz < 0 || nlz >= W) continue;
                int nIdx = nlz * W + nlx;

                bool isGoal = (nIdx == goalIdx);
                if (!isGoal && !isCellClear(nx, nz)) continue;
                // Strict no-corner-cutting for diagonal steps: BOTH adjacent cardinal cells must be open!
                if (d >= 4) {
                    if (!isCellClear(cx + DX[d], cz) || !isCellClear(cx, cz + DZ[d])) continue;
                }

                float ng = gScore[curIdx] + STEP_COST[d] + (isGoal ? 0.0f : cellCenterPenalty(nx, nz));
                if (ng < gScore[nIdx]) {
                    gScore[nIdx] = ng;
                    parent[nIdx] = curIdx;
                    float h = static_cast<float>(std::hypot(gx - nx, gz - nz));
                    pq.push({ng + h, static_cast<int16_t>(nIdx)});
                }
            }
        }

        if (!found) return toPos;

        int cur = goalIdx;
        while (parent[cur] >= 0 && parent[cur] != startIdx) {
            cur = parent[cur];
        }
        int nextX = (cur % W) - R + sx;
        int nextZ = (cur / W) - R + sz;

        // Snap waypoint cleanly to the exact corridor centerline when walking straight along a corridor
        float wx = nextX + 0.5f;
        float wz = nextZ + 0.5f;
        int stepDx = nextX - sx;
        int stepDz = nextZ - sz;
        if (stepDx == 0 || stepDz == 0) {
            float cLat = 0.0f, hW = 0.0f;
            if (measureCorridorCenter(wx, wz, stepDx, stepDz, cLat, hW)) {
                if (stepDx == 0 && isCellClear(static_cast<int>(std::floor(cLat)), nextZ)) wx = cLat;
                else if (stepDz == 0 && isCellClear(nextX, static_cast<int>(std::floor(cLat)))) wz = cLat;
            }
        }
        return {wx, 1.0f, wz};
    };

    // =========================================================================
    // COOPERATIVE ROBOT-VS-ROBOT PASSING NEGOTIATION ("Домовленість про розходження")
    // When two robots approach within 5.5m, they mutually commit to opposite sides
    // of the corridor so they smoothly pass diagonally without deadlocking or rubbing!
    // =========================================================================
    for (auto& e : enemies) {
        if (e.yieldTimer > 0.0f) {
            e.yieldTimer = std::max(0.0f, e.yieldTimer - dt);
            if (e.yieldTimer <= 0.0f) {
                e.yieldPartnerIdx = -1;
                e.yieldLateralDirX = 0.0f;
                e.yieldLateralDirZ = 0.0f;
            }
        }
    }
    for (size_t i = 0; i < enemies.size(); ++i) {
        if (!enemies[i].alive) continue;
        for (size_t j = i + 1; j < enemies.size(); ++j) {
            if (!enemies[j].alive) continue;
            float dx = enemies[j].pos.x - enemies[i].pos.x;
            float dz = enemies[j].pos.z - enemies[i].pos.z;
            float distSq = dx * dx + dz * dz;
            if (distSq > 5.5f * 5.5f || distSq < 1e-4f) continue;

            float dist = std::sqrt(distSq);
            Vec3 axis{dx / dist, 0.0f, dz / dist};
            Vec3 fwdI{std::sin(enemies[i].yaw), 0.0f, std::cos(enemies[i].yaw)};
            Vec3 fwdJ{std::sin(enemies[j].yaw), 0.0f, std::cos(enemies[j].yaw)};

            // Check if they are heading toward each other or overlapping in the same corridor
            bool approaching = (dot(fwdI, axis) > 0.15f || dot(fwdJ, Vec3{-axis.x, 0.0f, -axis.z}) > 0.15f || dist < 1.8f);
            if (!approaching) continue;

            // Keep existing negotiated sides while still passing the same partner
            if (enemies[i].yieldPartnerIdx == static_cast<int>(j) &&
                enemies[j].yieldPartnerIdx == static_cast<int>(i) &&
                enemies[i].yieldTimer > 0.0f && enemies[j].yieldTimer > 0.0f) {
                enemies[i].yieldTimer = 0.85f;
                enemies[j].yieldTimer = 0.85f;
                continue;
            }

            // Compute mutually opposite passing directions (right-hand rule, flipped if wall blocks)
            Vec3 rightOfAxis{-axis.z, 0.0f, axis.x};
            Vec3 mid = (enemies[i].pos + enemies[j].pos) * 0.5f;
            float posClear = 0.0f, negClear = 0.0f;
            for (float s = 0.3f; s <= 1.2f; s += 0.3f) {
                if (isWalkable(mid.x + rightOfAxis.x * s, mid.z + rightOfAxis.z * s)) posClear = s;
                if (isWalkable(mid.x - rightOfAxis.x * s, mid.z - rightOfAxis.z * s)) negClear = s;
            }
            float signI = (posClear >= negClear) ? 1.0f : -1.0f;

            enemies[i].yieldPartnerIdx = static_cast<int>(j);
            enemies[i].yieldLateralDirX = rightOfAxis.x * signI;
            enemies[i].yieldLateralDirZ = rightOfAxis.z * signI;
            enemies[i].yieldTimer = 1.2f;

            enemies[j].yieldPartnerIdx = static_cast<int>(i);
            enemies[j].yieldLateralDirX = -rightOfAxis.x * signI;
            enemies[j].yieldLateralDirZ = -rightOfAxis.z * signI;
            enemies[j].yieldTimer = 1.2f;
        }
    }

    // Helper lambdas for exact locker door geometry (handle, outward corridor normal, walkable stand point)
    auto doorHandlePos = [](const HingeDoor& d) -> Vec3 {
        float y = d.currentYaw();
        Vec3 leafDir{std::cos(y), 0.0f, -std::sin(y)};
        float handleX = std::max(0.07f, d.width - 0.08f);
        return d.hingePos + leafDir * handleX + Vec3{0.0f, d.height * 0.48f, 0.0f};
    };
    auto lockerDoorClosedMid = [](const HingeDoor& d) -> Vec3 {
        Vec3 closedDir{std::cos(d.closedYaw), 0.0f, -std::sin(d.closedYaw)};
        return d.hingePos + closedDir * (d.width * 0.5f);
    };
    auto lockerOutwardNormal = [&](const HingeDoor& d) -> Vec3 {
        Vec3 lc{
            0.5f * (d.lockerInterior.min.x + d.lockerInterior.max.x),
            1.0f,
            0.5f * (d.lockerInterior.min.z + d.lockerInterior.max.z)
        };
        Vec3 dMid = lockerDoorClosedMid(d);
        Vec3 n{dMid.x - lc.x, 0.0f, dMid.z - lc.z};
        float len = length(n);
        return (len > 1e-4f) ? (n / len) : Vec3{0.0f, 0.0f, 1.0f};
    };
    auto lockerStandPos = [&](const HingeDoor& d) -> Vec3 {
        Vec3 dMid = lockerDoorClosedMid(d);
        Vec3 outN = lockerOutwardNormal(d);
        for (float distOut : {0.92f, 0.78f, 1.10f, 0.65f}) {
            Vec3 cand{dMid.x + outN.x * distOut, 1.0f, dMid.z + outN.z * distOut};
            if (isWalkable(cand.x, cand.z)) return cand;
        }
        return Vec3{dMid.x + outN.x * 0.90f, 1.0f, dMid.z + outN.z * 0.90f};
    };

    // Detect if player is physically inside ANY locker interior (even while door is still closing!)
    int playerInAnyLockerIdx = -1;
    {
        AABB pBox = player.boundingBox();
        for (size_t di = 0; di < doors.size(); ++di) {
            if (doors[di].isLocker && doors[di].lockerInterior.intersects(pBox)) {
                playerInAnyLockerIdx = static_cast<int>(di);
                break;
            }
        }
    }

    Rng aiRng(static_cast<uint64_t>(player.position.x * 100.0f + player.position.z * 37.0f + enemies.size()));

    for (size_t eIdx = 0; eIdx < enemies.size(); ++eIdx) {
        auto& e = enemies[eIdx];
        if (!e.alive) continue;
        e.handIkActive[0] = false;
        e.handIkActive[1] = false;
        if (e.attackCooldown > 0.0f) e.attackCooldown -= dt;
        if (e.hurtFlash > 0.0f) e.hurtFlash -= dt;
        if (e.decisionCooldown > 0.0f) e.decisionCooldown -= dt;

        // Pre-filter nearby props for this robot's local obstacle avoidance
        AABB robotPropBounds{{e.pos.x - 4.5f, 0.5f, e.pos.z - 4.5f},
                             {e.pos.x + 4.5f, 3.0f, e.pos.z + 4.5f}};
        std::vector<AABB> nearProps;
        nearProps.reserve(16);
        for (const auto& pc : map.world.propColliders()) {
            if (robotPropBounds.intersects(pc)) nearProps.push_back(pc);
        }

        Vec3 eEye = {e.pos.x, e.pos.y + 2.12f, e.pos.z};
        Vec3 toPlayer = target - eEye;
        float dist = length(toPlayer);

        // Check line-of-sight to player (disabled when God Mode Invisibility is ON!)
        bool canSeePlayer = false;
        if (!godInvisible && dist <= 30.0f && dist > 0.1f) {
            Vec3 forward{std::sin(e.yaw), 0.0f, std::cos(e.yaw)};
            Vec3 toPlayerHoriz = normalize({toPlayer.x, 0.0f, toPlayer.z});
            bool inCone = (e.aiState == EnemyAIState::Chase || e.aiState == EnemyAIState::InvestigateLastSeen)
                              ? (dot(forward, toPlayerHoriz) > -0.2f)
                              : (dot(forward, toPlayerHoriz) > 0.35f);
            if (inCone) {
                Vec3 dir = toPlayer / dist;
                float wall = raycastWorld(eEye, dir, dist);
                bool blockedByDoor = false;
                for (const auto& d : doors) {
                    if (d.angle < 0.65f) {
                        float dtHit = 0.0f;
                        if (rayIntersectAABB(eEye, dir, d.worldBounds(), dist, dtHit)) {
                            blockedByDoor = true;
                            break;
                        }
                    }
                }
                if (wall >= dist - 0.08f && !blockedByDoor) {
                    canSeePlayer = true;
                }
            }
        }

        // If God Mode invisibility was toggled on during a chase, drop aggro immediately
        if (godInvisible && (e.aiState == EnemyAIState::Chase || e.aiState == EnemyAIState::InvestigateLastSeen)) {
            e.aiState = EnemyAIState::ReturnToAggro;
            e.alerted = false;
            e.sawPlayerEnterLockerIdx = -1;
            e.openingDoorIdx = -1;
            e.doorInteractTimer = 0.0f;
            e.pathTimer = 0.0f;
        }

        // If robot was actively chasing/seeing the player and the player enters or closes a locker in front of it,
        // check unobstructed LOS to the FRONT OUTSIDE of the locker door (so locker's own walls don't occlude it!)
        if (!godInvisible && playerInAnyLockerIdx >= 0 &&
            (e.aiState == EnemyAIState::Chase || e.aiState == EnemyAIState::InvestigateLastSeen)) {
            const auto& ld = doors[playerInAnyLockerIdx];
            Vec3 dMid = lockerDoorClosedMid(ld);
            Vec3 outN = lockerOutwardNormal(ld);
            Vec3 doorFrontEye = dMid + outN * 0.55f + Vec3{0.0f, 0.85f, 0.0f};
            float dFront = length(doorFrontEye - eEye);
            bool nearLastSeen = length({e.lastSeenPos.x - dMid.x, 0.0f, e.lastSeenPos.z - dMid.z}) < 3.5f;
            if ((e.aiState == EnemyAIState::Chase || nearLastSeen || e.sawPlayerEnterLockerIdx == playerInAnyLockerIdx) &&
                dFront <= 28.0f &&
                (dFront < 0.8f || raycastWorld(eEye, normalize(doorFrontEye - eEye), dFront) >= dFront - 0.35f)) {
                e.sawPlayerEnterLockerIdx = playerInAnyLockerIdx;
                e.lastSeenPos = lockerStandPos(ld);
            }
        }
        if (playerInAnyLockerIdx < 0 && e.openingDoorIdx < 0 && e.sawPlayerEnterLockerIdx >= 0 && canSeePlayer) {
            e.sawPlayerEnterLockerIdx = -1;
        }

        if (canSeePlayer && e.openingDoorIdx < 0) {
            if (e.aiState == EnemyAIState::Patrol || e.aiState == EnemyAIState::ReturnToAggro) {
                e.aggroOrigin = e.pos;
                e.alertPulseTimer = 0.0f;
            }
            e.aiState = EnemyAIState::Chase;
            e.alerted = true;
            e.lastSeenPos = (e.sawPlayerEnterLockerIdx >= 0 && e.sawPlayerEnterLockerIdx < static_cast<int>(doors.size()))
                                ? lockerStandPos(doors[e.sawPlayerEnterLockerIdx])
                                : player.position;
        } else if (e.aiState == EnemyAIState::Chase && !canSeePlayer) {
            e.aiState = EnemyAIState::InvestigateLastSeen;
            e.pathTimer = 0.0f;
            if (e.sawPlayerEnterLockerIdx >= 0 && e.sawPlayerEnterLockerIdx < static_cast<int>(doors.size())) {
                e.lastSeenPos = lockerStandPos(doors[e.sawPlayerEnterLockerIdx]);
            }
        }

        Vec3 prevPos = e.pos;

        if (e.aiState == EnemyAIState::Patrol) {
            e.alerted = false;
            if (e.turnPauseTimer > 0.0f) {
                e.turnPauseTimer -= dt;
                e.movePulseTimer = 0.0f;
                float targetYaw = std::atan2(static_cast<float>(e.pendingDirX),
                                             static_cast<float>(e.pendingDirZ));
                float diff = targetYaw - e.yaw;
                while (diff > PI) diff -= 2.0f * PI;
                while (diff < -PI) diff += 2.0f * PI;
                e.yaw += diff * std::min(1.0f, dt * 6.0f);
                if (e.turnPauseTimer <= 0.0f) {
                    e.dirX = e.pendingDirX;
                    e.dirZ = e.pendingDirZ;
                    e.yaw = targetYaw;
                }
            } else {
                // Steady patrol velocity from GameSettings (default 0.44 m/s -> 1 step per 1.50s)
                const float PATROL_SPEED = settings.enemyPatrolSpeed;
                int perpX = -e.dirZ, perpZ = e.dirX;

                // Measure corridor centerline & half-width
                float centerLat = (perpX != 0) ? e.pos.x : e.pos.z;
                float halfW = 1.2f;
                bool inBoundedCorridor = measureCorridorCenter(e.pos.x, e.pos.z, e.dirX, e.dirZ, centerLat, halfW);
                float maxSafeOffset = std::max(0.0f, halfW - 0.48f);

                // Compute desired lateral offset:
                // 1) Default = 0.0 (walk strictly down corridor centerline)
                // 2) If cooperative robot-vs-robot yield is active -> shift to agreed passing lane
                // 3) If an obstacle (prop/crate/locker) blocks centerline ahead -> sidestep diagonally around it!
                float desiredOffset = 0.0f;
                if (e.yieldTimer > 0.0f) {
                    float worldYieldDir = (perpX != 0) ? e.yieldLateralDirX : e.yieldLateralDirZ;
                    float ySign = (worldYieldDir >= 0.0f) ? 1.0f : -1.0f;
                    desiredOffset = clampf(ySign * 0.62f, -maxSafeOffset, maxSafeOffset);
                } else if (inBoundedCorridor) {
                    float fwdCenterX = (perpX != 0) ? centerLat : (e.pos.x + e.dirX * 1.15f);
                    float fwdCenterZ = (perpZ != 0) ? centerLat : (e.pos.z + e.dirZ * 1.15f);
                    float farCenterX = (perpX != 0) ? centerLat : (e.pos.x + e.dirX * 2.15f);
                    float farCenterZ = (perpZ != 0) ? centerLat : (e.pos.z + e.dirZ * 2.15f);

                    bool centerBlocked = !isFootprintClear(fwdCenterX, fwdCenterZ, 0.36f, nearProps) ||
                                         !isFootprintClear(farCenterX, farCenterZ, 0.36f, nearProps);
                    if (centerBlocked && maxSafeOffset > 0.25f) {
                        const float cands[] = {0.55f, -0.55f, 0.85f, -0.85f, 1.15f, -1.15f};
                        for (float c : cands) {
                            float off = clampf(c, -maxSafeOffset, maxSafeOffset);
                            float tx1 = fwdCenterX + ((perpX != 0) ? off : 0.0f);
                            float tz1 = fwdCenterZ + ((perpZ != 0) ? off : 0.0f);
                            float tx2 = farCenterX + ((perpX != 0) ? off : 0.0f);
                            float tz2 = farCenterZ + ((perpZ != 0) ? off : 0.0f);
                            if (isFootprintClear(tx1, tz1, 0.34f, nearProps) &&
                                isFootprintClear(tx2, tz2, 0.34f, nearProps)) {
                                desiredOffset = off;
                                break;
                            }
                        }
                    }
                }

                // Smoothly move diagonally toward target lateral lane (centerline or bypass lane)
                float curLat = (perpX != 0) ? e.pos.x : e.pos.z;
                float targetLat = centerLat + desiredOffset;
                float latErr = targetLat - curLat;
                float maxLatStep = PATROL_SPEED * 0.78f * dt; // ~38-degree smooth diagonal shift!
                float latDelta = inBoundedCorridor ? clampf(latErr, -maxLatStep, maxLatStep) : 0.0f;

                float stepX = e.dirX * PATROL_SPEED * dt + ((perpX != 0) ? latDelta : 0.0f);
                float stepZ = e.dirZ * PATROL_SPEED * dt + ((perpZ != 0) ? latDelta : 0.0f);
                float stepLen = std::hypot(stepX, stepZ);
                float maxStep = PATROL_SPEED * dt;
                if (stepLen > maxStep && stepLen > 1e-6f) {
                    stepX *= (maxStep / stepLen);
                    stepZ *= (maxStep / stepLen);
                }
                float nx = e.pos.x + stepX;
                float nz = e.pos.z + stepZ;

                if (isFootprintClear(nx, nz, 0.34f, nearProps)) {
                    e.pos.x = nx;
                    e.pos.z = nz;
                } else if (isFootprintClear(nx, e.pos.z, 0.32f, nearProps)) {
                    e.pos.x = nx;
                } else if (isFootprintClear(e.pos.x, nz, 0.32f, nearProps)) {
                    e.pos.z = nz;
                }

                // Smoothly face diagonal travel direction when sidestepping or returning to center
                float moveLen = std::hypot(e.pos.x - prevPos.x, e.pos.z - prevPos.z);
                if (moveLen > 1e-4f) {
                    float desiredYaw = std::atan2(e.pos.x - prevPos.x, e.pos.z - prevPos.z);
                    float yDiff = desiredYaw - e.yaw;
                    while (yDiff > PI) yDiff -= 2.0f * PI;
                    while (yDiff < -PI) yDiff += 2.0f * PI;
                    e.yaw += yDiff * std::min(1.0f, dt * 6.0f);
                }

                // Exact centerline intersection turn check:
                // NEVER turn into a perpendicular tunnel until aligned with its centerline!
                bool blockedAhead = !isWalkable(e.pos.x + e.dirX * 1.05f, e.pos.z + e.dirZ * 1.05f) ||
                                    !isFootprintClear(e.pos.x + e.dirX * 0.85f, e.pos.z + e.dirZ * 0.85f, 0.32f, nearProps);

                if (e.decisionCooldown <= 0.0f || blockedAhead) {
                    int leftX = -e.dirZ, leftZ = e.dirX;
                    int rightX = e.dirZ, rightZ = -e.dirX;

                    auto canTurnIntoBranch = [&](int bx, int bz) -> bool {
                        float probeX = e.pos.x + bx * 2.4f;
                        float probeZ = e.pos.z + bz * 2.4f;
                        if (!isWalkable(e.pos.x + bx * 1.3f, e.pos.z + bz * 1.3f) ||
                            !isWalkable(probeX, probeZ)) {
                            return false;
                        }
                        if (blockedAhead) return true; // Must turn if dead-end ahead
                        // Measure the perpendicular branch's centerline along our current forward axis
                        float branchCenter = 0.0f, branchHalfW = 0.0f;
                        if (measureCorridorCenter(probeX, probeZ, bx, bz, branchCenter, branchHalfW)) {
                            float myFwdCoord = (e.dirX != 0) ? e.pos.x : e.pos.z;
                            // Wait until robot reaches the exact centerline of the perpendicular tunnel!
                            return std::abs(myFwdCoord - branchCenter) <= 0.36f;
                        }
                        return true;
                    };

                    bool openFwd = !blockedAhead && isWalkable(e.pos.x + e.dirX * 2.0f, e.pos.z + e.dirZ * 2.0f);
                    bool openLeft = canTurnIntoBranch(leftX, leftZ);
                    bool openRight = canTurnIntoBranch(rightX, rightZ);

                    if (openLeft || openRight || blockedAhead) {
                        struct DirOpt { int dx, dz; };
                        std::vector<DirOpt> opts;
                        if (openFwd) opts.push_back({e.dirX, e.dirZ});
                        if (openLeft) opts.push_back({leftX, leftZ});
                        if (openRight) opts.push_back({rightX, rightZ});

                        DirOpt chosen{e.dirX, e.dirZ};
                        if (!opts.empty()) chosen = opts[aiRng.rangeInt(0, static_cast<int>(opts.size()) - 1)];
                        else if (blockedAhead) chosen = {-e.dirX, -e.dirZ};

                        if (chosen.dx != e.dirX || chosen.dz != e.dirZ) {
                            e.pendingDirX = chosen.dx;
                            e.pendingDirZ = chosen.dz;
                            e.turnPauseTimer = 0.9f;
                        }
                        e.decisionCooldown = 4.5f;
                    }
                }
            }
        } else if (e.aiState == EnemyAIState::Chase || e.aiState == EnemyAIState::InvestigateLastSeen) {
            float chaseSpeed = settings.enemyChaseSpeed;
            e.alerted = true;
            e.alertPulseTimer -= dt;
            if (e.alertPulseTimer <= 0.0f) {
                e.alertPulseTimer = 0.62f;
                audio.play3D(SoundId::RobotAlertPulse, eEye, 0.95f);
            }

            Vec3 goalPos = (e.aiState == EnemyAIState::Chase) ? player.position : e.lastSeenPos;

            // If robot saw player enter a locker, walk up to the walkable spot in front of it,
            // reach out with its right hand via 2-bone IK to grab the handle, and physically pull the door open!
            bool handlingLockerDoor = false;
            if (e.sawPlayerEnterLockerIdx >= 0 && e.sawPlayerEnterLockerIdx < static_cast<int>(doors.size())) {
                auto& ld = doors[e.sawPlayerEnterLockerIdx];
                Vec3 dMid = lockerDoorClosedMid(ld);
                Vec3 outN = lockerOutwardNormal(ld);
                Vec3 standPos = lockerStandPos(ld);
                if (e.aiState == EnemyAIState::InvestigateLastSeen) {
                    goalPos = standPos;
                }

                float dToDoor = length({dMid.x - e.pos.x, 0.0f, dMid.z - e.pos.z});
                float dToStand = length({standPos.x - e.pos.x, 0.0f, standPos.z - e.pos.z});
                bool closeEnoughToGrab = (e.openingDoorIdx == e.sawPlayerEnterLockerIdx) ||
                                         (e.aiState == EnemyAIState::InvestigateLastSeen &&
                                          (dToDoor < 1.50f || dToStand < 0.72f));

                if (closeEnoughToGrab) {
                    handlingLockerDoor = true;
                    e.openingDoorIdx = e.sawPlayerEnterLockerIdx;
                    e.doorInteractTimer += dt;

                    // Turn smoothly to face the locker door
                    Vec3 toDoor{dMid.x - e.pos.x, 0.0f, dMid.z - e.pos.z};
                    if (lengthSq(toDoor) > 1e-4f) {
                        float desiredYaw = std::atan2(toDoor.x, toDoor.z);
                        float yDiff = desiredYaw - e.yaw;
                        while (yDiff > PI) yDiff -= 2.0f * PI;
                        while (yDiff < -PI) yDiff += 2.0f * PI;
                        e.yaw += yDiff * std::min(1.0f, dt * 11.0f);
                    }

                    constexpr float REACH_TIME = 0.28f;
                    // Position body comfortably in front of handle, stepping slightly back as door swings open
                    if (e.doorInteractTimer < REACH_TIME && dToDoor > 1.05f) {
                        Vec3 stepDir = normalize({standPos.x - e.pos.x, 0.0f, standPos.z - e.pos.z});
                        float nx = e.pos.x + stepDir.x * std::min(chaseSpeed, 1.8f) * dt;
                        float nz = e.pos.z + stepDir.z * std::min(chaseSpeed, 1.8f) * dt;
                        if (isFootprintClear(nx, nz, 0.32f, nearProps)) {
                            e.pos.x = nx;
                            e.pos.z = nz;
                        }
                    } else if (e.doorInteractTimer >= REACH_TIME && dToDoor < 1.28f) {
                        float nx = e.pos.x + outN.x * 0.55f * dt;
                        float nz = e.pos.z + outN.z * 0.55f * dt;
                        if (isFootprintClear(nx, nz, 0.32f, nearProps)) {
                            e.pos.x = nx;
                            e.pos.z = nz;
                        }
                    }

                    // Procedural right-hand 2-bone IK target: reach smoothly to handle, then track swinging handle!
                    Vec3 handlePos = doorHandlePos(ld);
                    Vec3 fwd{std::sin(e.yaw), 0.0f, std::cos(e.yaw)};
                    Vec3 left{std::cos(e.yaw), 0.0f, -std::sin(e.yaw)};
                    Vec3 rightHandRest = Vec3{e.pos.x, e.pos.y + 1.95f, e.pos.z} + fwd * 0.26f - left * 0.38f;
                    float reachT = clampf(e.doorInteractTimer / REACH_TIME, 0.0f, 1.0f);
                    float smoothT = reachT * reachT * (3.0f - 2.0f * reachT);
                    e.handIkActive[1] = true;
                    e.handWorldPos[1] = rightHandRest + (handlePos - rightHandRest) * smoothT;

                    // Phase 2: physically drag/pull the hinged door open via angular velocity!
                    if (e.doorInteractTimer >= REACH_TIME) {
                        e.handWorldPos[1] = handlePos;
                        ld.angularVel = clampf(ld.angularVel + 16.0f * dt, -5.0f, 4.4f);
                    }

                    // Done once door is swung wide open (or safety timeout)
                    if (ld.angle >= 1.25f || e.doorInteractTimer >= 1.85f) {
                        e.handIkActive[1] = false;
                        e.openingDoorIdx = -1;
                        e.sawPlayerEnterLockerIdx = -1;
                        e.doorInteractTimer = 0.0f;
                        e.aiState = EnemyAIState::Chase;
                        e.lastSeenPos = player.position;
                    }
                }
            }

            if (handlingLockerDoor) {
                // Robot is actively grabbing and pulling the locker door open
            } else if (e.aiState == EnemyAIState::InvestigateLastSeen &&
                       e.sawPlayerEnterLockerIdx < 0 &&
                       lengthSq({goalPos.x - e.pos.x, 0.0f, goalPos.z - e.pos.z}) < 1.25f * 1.25f) {
                e.aiState = EnemyAIState::ReturnToAggro;
                e.alerted = false;
                e.pathTimer = 0.0f;
                e.movePulseTimer = 0.0f;
            } else {
                e.pathTimer -= dt;
                float wpDistSq = lengthSq({e.nextWaypoint.x - e.pos.x, 0.0f, e.nextWaypoint.z - e.pos.z});
                if (e.pathTimer <= 0.0f || !e.hasWaypoint || wpDistSq < 0.25f) {
                    e.pathTimer = 0.25f;
                    e.nextWaypoint = findNextStepAStar(e.pos, goalPos);
                    e.hasWaypoint = true;
                }

                Vec3 moveTarget = (e.aiState == EnemyAIState::Chase && canSeePlayer && dist < 3.5f)
                                      ? player.position
                                      : e.nextWaypoint;
                if (e.yieldTimer > 0.0f) {
                    moveTarget.x += e.yieldLateralDirX * 0.55f;
                    moveTarget.z += e.yieldLateralDirZ * 0.55f;
                }
                Vec3 horiz = {moveTarget.x - e.pos.x, 0.0f, moveTarget.z - e.pos.z};
                float hDist = length(horiz);
                if (hDist > 0.05f) {
                    Vec3 moveDir = horiz / hDist;
                    float desiredYaw = std::atan2(moveDir.x, moveDir.z);
                    float yDiff = desiredYaw - e.yaw;
                    while (yDiff > PI) yDiff -= 2.0f * PI;
                    while (yDiff < -PI) yDiff += 2.0f * PI;
                    e.yaw += yDiff * std::min(1.0f, dt * 10.0f);

                    if (e.aiState != EnemyAIState::Chase || dist > 1.15f) {
                        float nx = e.pos.x + moveDir.x * chaseSpeed * dt;
                        float nz = e.pos.z + moveDir.z * chaseSpeed * dt;
                        if (isFootprintClear(nx, nz, 0.34f, nearProps)) {
                            e.pos.x = nx;
                            e.pos.z = nz;
                        } else {
                            if (isFootprintClear(nx, e.pos.z, 0.32f, nearProps)) e.pos.x = nx;
                            if (isFootprintClear(e.pos.x, nz, 0.32f, nearProps)) e.pos.z = nz;
                        }
                    }
                }

                if (!godMode && !godInvisible && e.aiState == EnemyAIState::Chase && dist < 1.45f && e.attackCooldown <= 0.0f) {
                    health -= 15.0f;
                    damageFlash = 0.6f;
                    e.attackCooldown = 1.0f;
                    audio.play(SoundId::PlayerHurt);
                }
            }
        } else {
            // RETURN TO AGGRO POINT (via center-seeking weighted A*)
            e.alerted = false;
            float distAggroSq = lengthSq({e.aggroOrigin.x - e.pos.x, 0.0f, e.aggroOrigin.z - e.pos.z});
            if (distAggroSq < 1.3f * 1.3f) {
                e.aiState = EnemyAIState::Patrol;
                e.decisionCooldown = 0.5f;
            } else {
                e.pathTimer -= dt;
                float wpDistSq = lengthSq({e.nextWaypoint.x - e.pos.x, 0.0f, e.nextWaypoint.z - e.pos.z});
                if (e.pathTimer <= 0.0f || !e.hasWaypoint || wpDistSq < 0.25f) {
                    e.pathTimer = 0.35f;
                    e.nextWaypoint = findNextStepAStar(e.pos, e.aggroOrigin);
                    e.hasWaypoint = true;
                }
                const float RETURN_SPEED = settings.enemyPatrolSpeed;
                Vec3 moveTarget = e.nextWaypoint;
                if (e.yieldTimer > 0.0f) {
                    moveTarget.x += e.yieldLateralDirX * 0.55f;
                    moveTarget.z += e.yieldLateralDirZ * 0.55f;
                }
                Vec3 horiz = {moveTarget.x - e.pos.x, 0.0f, moveTarget.z - e.pos.z};
                float hDist = length(horiz);
                if (hDist > 0.05f) {
                    Vec3 moveDir = horiz / hDist;
                    float desiredYaw = std::atan2(moveDir.x, moveDir.z);
                    float yDiff = desiredYaw - e.yaw;
                    while (yDiff > PI) yDiff -= 2.0f * PI;
                    while (yDiff < -PI) yDiff += 2.0f * PI;
                    e.yaw += yDiff * std::min(1.0f, dt * 6.0f);

                    float nx = e.pos.x + moveDir.x * RETURN_SPEED * dt;
                    float nz = e.pos.z + moveDir.z * RETURN_SPEED * dt;
                    if (isFootprintClear(nx, nz, 0.34f, nearProps)) {
                        e.pos.x = nx;
                        e.pos.z = nz;
                    } else {
                        if (isFootprintClear(nx, e.pos.z, 0.32f, nearProps)) e.pos.x = nx;
                        if (isFootprintClear(e.pos.x, nz, 0.32f, nearProps)) e.pos.z = nz;
                    }
                }
            }
        }

        // Gentle cooperative physical separation so two passing robots never clip through each other
        for (size_t j = 0; j < enemies.size(); ++j) {
            if (j == eIdx || !enemies[j].alive) continue;
            float sdx = e.pos.x - enemies[j].pos.x;
            float sdz = e.pos.z - enemies[j].pos.z;
            float sdSq = sdx * sdx + sdz * sdz;
            constexpr float MIN_SEP = 0.82f;
            if (sdSq < MIN_SEP * MIN_SEP && sdSq > 1e-5f) {
                float sd = std::sqrt(sdSq);
                float push = (MIN_SEP - sd) * 0.5f;
                float px = e.pos.x + (sdx / sd) * push;
                float pz = e.pos.z + (sdz / sd) * push;
                if (isFootprintClear(px, pz, 0.32f, nearProps)) {
                    e.pos.x = px;
                    e.pos.z = pz;
                }
            }
        }

        // =========================================================================
        // RAIN WORLD-STYLE PROCEDURAL FOOT STEPPING & SPRING-DAMPER BODY DYNAMICS
        // idx 0 = Left (+X local), idx 1 = Right (-X local)
        // =========================================================================
        e.vel = (e.pos - prevPos) / std::max(dt, 1e-4f);
        float speedXZ = std::hypot(e.vel.x, e.vel.z);
        Vec3 fwdVec{std::sin(e.yaw), 0.0f, std::cos(e.yaw)};
        Vec3 leftVec{std::cos(e.yaw), 0.0f, -std::sin(e.yaw)}; // +X in robot local space

        // Initialize feet in an exact half-cycle staggered stance so Right foot (idx 1) steps forward first
        // and Left/Right alternate with symmetric ~1.50s intervals!
        if (!e.feet[0].initialized || !e.feet[1].initialized) {
            e.feet[0].worldPos = e.pos + leftVec * (0.182f) + fwdVec * (0.11f);
            e.feet[0].worldPos.y = 1.0f;
            e.feet[0].targetPos = e.feet[0].worldPos;
            e.feet[0].initialized = true;

            e.feet[1].worldPos = e.pos + leftVec * (-0.182f) - fwdVec * (0.52f); // Right trailing -> steps FIRST!
            e.feet[1].worldPos.y = 1.0f;
            e.feet[1].targetPos = e.feet[1].worldPos;
            e.feet[1].initialized = true;
        }

        for (int pass = 0; pass < 2; ++pass) {
            int i = (pass == 0) ? 1 : 0; // Right foot (1) checked first
            int localSide = (i == 0) ? 1 : -1;
            Vec3 ideal = e.pos + leftVec * (localSide * 0.182f);
            ideal.y = 1.0f;

            if (e.feet[i].stepping) {
                e.feet[i].stepT += dt / e.feet[i].stepDuration;
                if (e.feet[i].stepT >= 1.0f) {
                    e.feet[i].worldPos = e.feet[i].targetPos;
                    e.feet[i].worldPos.y = 1.0f;
                    e.feet[i].stepping = false;
                    e.feet[i].stepT = 0.0f;

                    e.pelvisVelY -= e.alerted ? 1.85f : 1.15f;
                    audio.play3D(SoundId::RobotStep, e.feet[i].worldPos, e.alerted ? 0.72f : 1.0f);
                } else {
                    float t = e.feet[i].stepT;
                    float horizT = std::pow(t, 0.65f);
                    float liftH = e.alerted ? 0.40f : 0.34f;
                    e.feet[i].worldPos.x = e.feet[i].startPos.x + (e.feet[i].targetPos.x - e.feet[i].startPos.x) * horizT;
                    e.feet[i].worldPos.z = e.feet[i].startPos.z + (e.feet[i].targetPos.z - e.feet[i].startPos.z) * horizT;
                    e.feet[i].worldPos.y = 1.0f + std::sin(t * PI) * liftH;
                }
            } else {
                bool otherBusy = e.alerted
                                     ? (e.feet[1 - i].stepping && e.feet[1 - i].stepT < 0.65f)
                                     : e.feet[1 - i].stepping;
                if (!otherBusy) {
                    Vec3 offset = e.feet[i].worldPos - ideal;
                    Vec3 moveDir = (speedXZ > 0.15f)
                                       ? Vec3{e.vel.x / speedXZ, 0.0f, e.vel.z / speedXZ}
                                       : fwdVec;
                    // Signed distance BEHIND the hip along travel direction (+ = trailing behind, - = ahead!)
                    float behindDist = -dot(offset, moveDir);
                    float lateralDist = std::abs(dot(offset, Vec3{-moveDir.z, 0.0f, moveDir.x}));

                    // Patrol: foot triggers ONLY when trailing >= 0.54m behind hip (or >0.35m sideways on turn)
                    // At v=0.44 m/s and landing at +0.56m ahead, full 1.10m stance = ~2.50s + 0.44s swing = 2.94s cycle -> 1 step / 1.47s!
                    float triggerBehind = e.alerted ? 0.34f : 0.54f;
                    bool shouldStep = (behindDist > triggerBehind) || (lateralDist > 0.52f);
                    if (shouldStep) {
                        e.feet[i].stepping = true;
                        e.feet[i].stepT = 0.0f;
                        e.feet[i].stepDuration = e.alerted ? 0.14f : 0.44f;
                        e.feet[i].startPos = e.feet[i].worldPos;

                        if (speedXZ > 0.15f) {
                            float leadAmt = e.alerted
                                                ? clampf(speedXZ * e.feet[i].stepDuration * 1.05f, 0.40f, 0.64f)
                                                : (0.56f + speedXZ * e.feet[i].stepDuration);
                            e.feet[i].targetPos = ideal + moveDir * leadAmt;
                        } else {
                            e.feet[i].targetPos = ideal;
                        }
                        e.feet[i].targetPos.y = 1.0f;
                    }
                }
            }

            // Reachability clamp (max horizontal reach = 0.68m for 1.288m leg at 1.092m hip height)
            float hdx = e.feet[i].worldPos.x - ideal.x;
            float hdz = e.feet[i].worldPos.z - ideal.z;
            float hDist = std::hypot(hdx, hdz);
            constexpr float MAX_HORIZ_REACH = 0.68f;
            if (hDist > MAX_HORIZ_REACH) {
                float scale = MAX_HORIZ_REACH / hDist;
                e.feet[i].worldPos.x = ideal.x + hdx * scale;
                e.feet[i].worldPos.z = ideal.z + hdz * scale;
            }
        }

        // Spring-damper pelvis bounce & torso lean from real velocity (or forward hunch when pulling a locker door!)
        float pelvisTarget = (e.openingDoorIdx >= 0) ? -0.11f : 0.0f;
        e.pelvisVelY += (-150.0f * (e.pelvisOffsetY - pelvisTarget) - 14.0f * e.pelvisVelY) * dt;
        e.pelvisOffsetY = clampf(e.pelvisOffsetY + e.pelvisVelY * dt, -0.15f, 0.10f);
        float targetLean = (e.openingDoorIdx >= 0)
                               ? 0.46f
                               : clampf(dot(e.vel, fwdVec) * 0.055f, -0.08f, 0.28f);
        e.torsoLeanX += (targetLean - e.torsoLeanX) * std::min(1.0f, dt * 10.0f);

        // Spring-damper arms counter-balancing actual foot positions
        for (int i = 0; i < 2; ++i) {
            int otherSide = ((1 - i) == 0) ? 1 : -1;
            Vec3 otherIdeal = e.pos + leftVec * (otherSide * 0.182f);
            float legFwd = dot(e.feet[1 - i].worldPos - otherIdeal, fwdVec);
            float targetArm = clampf(-legFwd * 1.35f, -0.85f, 0.85f);
            e.armVel[i] += (110.0f * (targetArm - e.armAngle[i]) - 11.0f * e.armVel[i]) * dt;
            e.armAngle[i] += e.armVel[i] * dt;
        }
    }

    // Projectiles
    for (auto& pr : projectiles) {
        pr.life -= dt;
        pr.pos += pr.vel * dt;
        int ix = static_cast<int>(std::floor(pr.pos.x));
        int iy = static_cast<int>(std::floor(pr.pos.y));
        int iz = static_cast<int>(std::floor(pr.pos.z));
        if (map.world.getCell(ix, iy, iz).isOpaque()) {
            pr.life = 0.0f;
            continue;
        }
        if (!pr.fromPlayer && lengthSq(pr.pos - player.eyePosition()) < 0.55f * 0.55f) {
            health -= pr.damage;
            damageFlash = 0.6f;
            pr.life = 0.0f;
            audio.play(SoundId::PlayerHurt);
        }
    }
    projectiles.erase(
        std::remove_if(projectiles.begin(), projectiles.end(),
                       [](const Projectile& p) { return p.life <= 0.0f; }),
        projectiles.end());

    // Particles
    for (auto& pt : particles) {
        pt.life -= dt;
        pt.vel.y -= pt.gravity * dt;
        pt.pos += pt.vel * dt;
    }
    particles.erase(
        std::remove_if(particles.begin(), particles.end(),
                       [](const Particle& p) { return p.life <= 0.0f; }),
        particles.end());

    if (health <= 0.0f) {
        health = 0.0f;
        gameOver = true;
    }
}

void GameWorld::gatherBillboards(std::vector<BillboardInstance>& out, float timeSec) const {
    out.clear();
    for (const auto& pt : particles) {
        out.push_back({pt.pos, pt.size, pt.size, pt.sprite, pt.color});
    }
    (void)timeSec;
}

void GameWorld::gatherRobots(std::vector<RobotRenderState>& out) const {
    out.clear();
    for (const auto& e : enemies) {
        if (!e.alive) continue;
        RobotRenderState rs;
        rs.pos = e.pos;
        rs.yaw = e.yaw;
        rs.footWorldPos[0] = e.feet[0].initialized ? e.feet[0].worldPos : e.pos;
        rs.footWorldPos[1] = e.feet[1].initialized ? e.feet[1].worldPos : e.pos;
        rs.pelvisOffsetY = e.pelvisOffsetY;
        rs.armAngle[0] = e.armAngle[0];
        rs.armAngle[1] = e.armAngle[1];
        rs.handIkActive[0] = e.handIkActive[0];
        rs.handIkActive[1] = e.handIkActive[1];
        rs.handWorldPos[0] = e.handWorldPos[0];
        rs.handWorldPos[1] = e.handWorldPos[1];
        rs.torsoLeanX = e.torsoLeanX;
        rs.alerted = e.alerted;
        rs.hurt = e.hurtFlash > 0.0f;
        out.push_back(rs);
    }
}

void GameWorld::syncInventoryCounts() {
    int nFlash = backpack.countType(ItemType::Flashlight);
    int nBatt  = backpack.countType(ItemType::Battery);
    int nFood  = backpack.countType(ItemType::Food);
    int nWater = backpack.countType(ItemType::Water);
    int nPick  = backpack.countType(ItemType::Lockpick);

    auto countExtra = [&](ItemType t) {
        switch (t) {
            case ItemType::Flashlight: nFlash++; break;
            case ItemType::Battery:    nBatt++; break;
            case ItemType::Food:       nFood++; break;
            case ItemType::Water:      nWater++; break;
            case ItemType::Lockpick:   nPick++; break;
            default: break;
        }
    };
    if (hasHeldItem) countExtra(heldItemType);
    if (draggingItem) countExtra(dragItemType);

    inventory.hasFlashlight = (nFlash > 0);
    inventory.batteries = nBatt;
    inventory.food = nFood;
    inventory.water = nWater;
    inventory.lockpicks = nPick;
    if (!inventory.hasFlashlight) flashlightOn = false;
}

Mat4 GameWorld::backpackWorldMatrix() const {
    constexpr float W = Backpack3D::DIM_X * Backpack3D::CELL_SIZE;
    constexpr float H = Backpack3D::DIM_Y * Backpack3D::CELL_SIZE;
    constexpr float D = Backpack3D::DIM_Z * Backpack3D::CELL_SIZE;

    Mat4 M = Mat4::identity();
    Vec3 bX, bY, bZ, origin;

    if (backpackMode == BackpackMode::OnGround) {
        float c = std::cos(backpackGroundYaw);
        float s = std::sin(backpackGroundYaw);
        bX = {c, 0.0f, s};
        bY = {0.0f, 1.0f, 0.0f};
        bZ = cross(bX, bY);
        origin = backpackGroundPos - bX * (W * 0.5f) - bZ * (D * 0.5f);
    } else {
        // Held in front of player's hands and tilted ~46 degrees so player looks into cavity (and can RMB-spin 360 to rear slot!)
        Vec3 eye = player.eyePosition();
        Vec3 fwd = player.forwardDir();
        Vec3 right = player.rightDir();
        Vec3 up = normalize(cross(right, fwd));

        float tilt = radians(46.0f);
        float ct = std::cos(tilt), st = std::sin(tilt);
        bX = right;
        bY = normalize(up * ct - fwd * st);
        bZ = cross(bX, bY); // Exact right-handed orthonormal frame

        Vec3 center = eye + fwd * 0.68f - up * 0.10f;
        origin = center - bX * (W * 0.5f) - bY * (H * 0.5f) - bZ * (D * 0.5f);
    }

    M.m[0] = bX.x; M.m[1] = bX.y; M.m[2]  = bX.z; M.m[3]  = 0.0f;
    M.m[4] = bY.x; M.m[5] = bY.y; M.m[6]  = bY.z; M.m[7]  = 0.0f;
    M.m[8] = bZ.x; M.m[9] = bZ.y; M.m[10] = bZ.z; M.m[11] = 0.0f;
    M.m[12] = origin.x; M.m[13] = origin.y; M.m[14] = origin.z; M.m[15] = 1.0f;

    if (backpackMode == BackpackMode::InHands &&
        (std::abs(backpackViewYaw) > 1e-4f || std::abs(backpackViewPitch) > 1e-4f)) {
        Vec3 lc{W * 0.5f, H * 0.5f, D * 0.5f};
        M = M * Mat4::translate(lc)
              * Mat4::rotateY(backpackViewYaw)
              * Mat4::rotateX(backpackViewPitch)
              * Mat4::translate({-lc.x, -lc.y, -lc.z});
    }
    return M;
}

Mat4 GameWorld::heldItemWorldMatrix() const {
    Vec3 eye = player.eyePosition();
    Vec3 fwd = player.forwardDir();
    Vec3 right = player.rightDir();
    Vec3 up = normalize(cross(right, fwd));

    // Exact right-handed camera-aligned basis: +X = right, +Y = up, +Z = -fwd (toward player)
    Mat4 camRot = Mat4::identity();
    camRot.m[0] = right.x; camRot.m[1] = right.y; camRot.m[2]  = right.z;
    camRot.m[4] = up.x;    camRot.m[5] = up.y;    camRot.m[6]  = up.z;
    camRot.m[8] = -fwd.x;  camRot.m[9] = -fwd.y;  camRot.m[10] = -fwd.z;

    // Organic fixed first-person pose per ItemType (NEVER affected by placement orientation!)
    Vec3 localOff{0.20f, -0.15f, -0.37f}; // (-Z = forward away from camera)
    Mat4 poseRot = Mat4::identity();
    float swingS = (meleeSwingTimer > 0.0f) ? std::sin((meleeSwingTimer / 0.28f) * PI) : 0.0f;

    switch (heldItemType) {
        case ItemType::Flashlight:
            // Lens (+Z in model) points cleanly forward (-Z in camera)
            localOff = {0.21f, -0.15f, -0.38f};
            poseRot = Mat4::rotateY(PI - 0.10f) * Mat4::rotateX(0.06f);
            break;
        case ItemType::Battery:
            localOff = {0.19f, -0.14f, -0.35f};
            poseRot = Mat4::rotateY(-0.35f) * Mat4::rotateZ(0.14f) * Mat4::rotateX(-0.15f);
            break;
        case ItemType::Food: {
            // Ration bar angled sideways so player clearly sees the wrapper peeling back & bites shortening the bar!
            float lift = clampf(consumeTilt, 0.0f, 1.0f);
            float openL = clampf(consumeOpenProgress, 0.0f, 1.0f);
            float biteDip = std::sin(consumeBiteImpulse * PI);
            float chewBob = (lift > 0.6f) ? std::sin(consumeBiteTimer * 18.0f) * 0.006f : 0.0f;
            float bring = std::max(openL * 0.35f, lift);
            localOff = {
                0.18f * (1.0f - bring) + 0.065f * bring,
                -0.14f * (1.0f - bring) + (-0.070f + biteDip * 0.018f + chewBob) * bring,
                -0.35f * (1.0f - bring) + (-0.25f + biteDip * 0.032f) * bring
            };
            // Sideways profile (~55 deg yaw + slight roll) makes length reduction & wrapper flaps clearly visible!
            float rx = -0.18f * (1.0f - bring) + (0.22f + biteDip * 0.18f) * bring;
            float ry = -0.96f * (1.0f - bring) + (-0.78f) * bring;
            float rz = 0.24f * (1.0f - bring) + (0.18f) * bring;
            poseRot = Mat4::rotateY(ry) * Mat4::rotateX(rx) * Mat4::rotateZ(rz);
            break;
        }
        case ItemType::Water: {
            // Bottle sits upright in right hand; lifts when unscrewing, then tilts toward mouth to pour!
            float openL = clampf(consumeOpenProgress, 0.0f, 1.0f);
            float tiltT = clampf(consumeTilt / 1.35f, 0.0f, 1.0f);
            float glugBob = consumePouring ? std::sin(consumeSoundTimer * 15.0f) * 0.008f : 0.0f;
            float lift = std::max(openL * 0.40f, tiltT);
            localOff = {
                0.20f * (1.0f - lift) + 0.035f * lift,
                -0.15f * (1.0f - lift) + (-0.065f + tiltT * 0.025f + glugBob) * lift,
                -0.37f * (1.0f - lift) + (-0.26f + tiltT * 0.03f) * lift
            };
            // Positive rotateX tilts bottle top (+Y) toward camera/mouth (+Z)!
            float rx = -0.06f * (1.0f - tiltT) + consumeTilt;
            float ry = -0.22f * (1.0f - lift) - 0.06f * lift;
            float rz = 0.08f * (1.0f - lift) + 0.05f * tiltT;
            poseRot = Mat4::rotateY(ry) * Mat4::rotateX(rx) * Mat4::rotateZ(rz);
            break;
        }
        case ItemType::Lockpick:
            localOff = {0.17f, -0.13f, -0.35f};
            poseRot = Mat4::rotateY(PI - 0.22f) * Mat4::rotateX(-0.12f) * Mat4::rotateZ(-0.18f);
            break;
        case ItemType::Broom:
        case ItemType::Mop:
        case ItemType::Stick:
        case ItemType::Spear:
            localOff = {
                0.24f - swingS * 0.14f,
                -0.28f - swingS * 0.08f,
                -(0.56f + swingS * 0.28f)
            };
            poseRot = Mat4::rotateX(-0.24f - swingS * 0.95f) * Mat4::rotateZ(0.10f + swingS * 0.35f);
            break;
        case ItemType::Tape:
            localOff = {0.19f, -0.14f, -0.36f};
            poseRot = Mat4::rotateX(0.45f) * Mat4::rotateY(-0.25f) * Mat4::rotateZ(0.15f);
            break;
        case ItemType::Knife:
            localOff = {
                0.21f - swingS * 0.12f,
                -0.14f + swingS * 0.04f,
                -(0.38f + swingS * 0.24f)
            };
            poseRot = Mat4::rotateY(-0.35f) * Mat4::rotateX(-0.30f - swingS * 1.05f) * Mat4::rotateZ(0.12f + swingS * 0.42f);
            break;
    }

    Vec3 worldPos = eye + right * localOff.x + up * localOff.y - fwd * localOff.z;
    return Mat4::translate(worldPos) * camRot * poseRot;
}

void GameWorld::resetHeldConsumableState(bool initializeFromDurability) {
    consumeCapSpin = 0.0f;
    consumeTilt = 0.0f;
    consumeBiteTimer = 0.0f;
    consumeBiteImpulse = 0.0f;
    consumeDropletTimer = 0.0f;
    consumeSoundTimer = 0.0f;
    consumePouring = false;
    consumePlayedOpenSfx = false;
    if (initializeFromDurability && hasHeldItem &&
        (heldItemType == ItemType::Water || heldItemType == ItemType::Food)) {
        if (heldItemDurability <= 0) heldItemDurability = 100;
        consumeRemaining = clampf(static_cast<float>(heldItemDurability) / 100.0f, 0.02f, 1.0f);
        consumeOpenProgress = (heldItemType == ItemType::Food && consumeRemaining < 0.99f) ? 0.75f : 0.0f;
    } else {
        consumeRemaining = 1.0f;
        consumeOpenProgress = 0.0f;
    }
}

ConsumableVisualState GameWorld::gatherHeldConsumableState() const {
    ConsumableVisualState st;
    if (hasHeldItem && (heldItemType == ItemType::Water || heldItemType == ItemType::Food)) {
        st.fill = consumeRemaining;
        st.openProgress = consumeOpenProgress;
        st.capSpin = consumeCapSpin;
        st.tiltRad = consumeTilt;
        st.biteImpulse = consumeBiteImpulse;
        st.pouring = consumePouring;
    }
    return st;
}

void GameWorld::updateHeldConsumption(float dt, AudioEngine& audio) {
    // Always flush gradual pending hunger recovery smoothly even after swallowing the final bite
    if (consumePendingRestore > 0.0f) {
        float rate = std::max(8.0f, settings.foodRestore * 0.38f);
        float delta = std::min(consumePendingRestore, rate * dt);
        hunger = std::min(settings.maxHunger, hunger + delta);
        consumePendingRestore -= delta;
    }

    if (!hasHeldItem || (heldItemType != ItemType::Water && heldItemType != ItemType::Food)) {
        consumePouring = false;
        return;
    }

    bool canConsume = !heldPlaceMode && backpackMode != BackpackMode::InHands &&
                      !draggingItem && !lockpick.active && grabbedDoorIdx < 0 &&
                      hoveredDoorIdx < 0 && hoveredItemIdx < 0;
    bool active = canConsume && consumeLmbHeld;

    if (heldItemType == ItemType::Water) {
        if (active) {
            if (consumeOpenProgress < 1.0f) {
                if (!consumePlayedOpenSfx) {
                    audio.play(SoundId::BottleCapTwist, 0.85f);
                    consumePlayedOpenSfx = true;
                }
                consumeOpenProgress = std::min(1.0f, consumeOpenProgress + dt * 1.85f);
                consumeCapSpin += dt * 24.0f;
            }
            if (consumeOpenProgress >= 0.90f) {
                // Fuller bottle pours at moderate tilt (~68 deg); emptier bottle tilts steeper (~115 deg)!
                float targetTilt = 1.18f + (1.0f - consumeRemaining) * 0.82f;
                consumeTilt += (targetTilt - consumeTilt) * std::min(1.0f, dt * 6.5f);
                float pourThresh = 0.72f + (1.0f - consumeRemaining) * 0.36f;
                consumePouring = (consumeTilt >= pourThresh && consumeRemaining > 0.001f);

                if (consumePouring) {
                    float drain = std::min(consumeRemaining, dt * 0.30f);
                    consumeRemaining -= drain;
                    heldItemDurability = std::max(1, static_cast<int>(std::round(consumeRemaining * 100.0f)));
                    thirst = std::min(settings.maxThirst, thirst + settings.waterRestore * drain);
                    energy = std::min(settings.maxEnergy, energy + 6.0f * drain);

                    // Emit blue square water droplets streaming downward from the bottle mouth!
                    consumeDropletTimer -= dt;
                    if (consumeDropletTimer <= 0.0f) {
                        consumeDropletTimer = 0.022f;
                        Mat4 hm = heldItemWorldMatrix();
                        Vec3 mouthPos = hm.transformPoint({0.0f, 0.072f, 0.010f});
                        Vec3 fwdHoriz = normalize({player.forwardDir().x, 0.0f, player.forwardDir().z});
                        Rng pRng(baseSeed ^ static_cast<uint64_t>(particles.size() * 7919 + static_cast<int>(consumeRemaining * 10000.0f)));
                        for (int k = 0; k < 2; ++k) {
                            Vec3 vel{
                                -fwdHoriz.x * (0.08f + pRng.nextFloat() * 0.08f) + (pRng.nextFloat() - 0.5f) * 0.10f,
                                -(0.55f + pRng.nextFloat() * 0.35f),
                                -fwdHoriz.z * (0.08f + pRng.nextFloat() * 0.08f) + (pRng.nextFloat() - 0.5f) * 0.10f
                            };
                            particles.push_back({
                                mouthPos,
                                vel,
                                0.24f + pRng.nextFloat() * 0.12f,
                                {0.20f, 0.66f, 1.0f},
                                0.022f,
                                9.8f,
                                SpriteType::WaterDrop
                            });
                        }
                    }

                    // Rhythmic bubbling water sip sound
                    consumeSoundTimer -= dt;
                    if (consumeSoundTimer <= 0.0f) {
                        consumeSoundTimer = 0.40f;
                        audio.play(SoundId::WaterSip, 0.85f);
                    }

                    if (consumeRemaining <= 0.001f) {
                        hasHeldItem = false;
                        heldPlaceMode = false;
                        heldItemDurability = 0;
                        consumeLmbHeld = false;
                        resetHeldConsumableState(false);
                        hudNoticeText = "BOTTLE EMPTY";
                        hudNoticeTimer = 1.6f;
                        syncInventoryCounts();
                    }
                }
            }
        } else {
            consumePouring = false;
            consumeTilt = std::max(0.0f, consumeTilt - dt * 5.2f);
            if (consumeTilt < 0.18f && consumeOpenProgress > 0.0f) {
                consumeOpenProgress = std::max(0.0f, consumeOpenProgress - dt * 2.2f);
                consumeCapSpin -= dt * 24.0f;
                if (consumeOpenProgress <= 0.0f && consumePlayedOpenSfx) {
                    audio.play(SoundId::BottleCapTwist, 0.55f);
                    consumePlayedOpenSfx = false;
                }
            }
        }
    } else if (heldItemType == ItemType::Food) {
        consumeBiteImpulse = std::max(0.0f, consumeBiteImpulse - dt * 3.4f);
        if (active) {
            if (consumeOpenProgress < 1.0f) {
                if (!consumePlayedOpenSfx) {
                    audio.play(SoundId::WrapperOpen, 0.85f);
                    consumePlayedOpenSfx = true;
                }
                consumeOpenProgress = std::min(1.0f, consumeOpenProgress + dt * 2.1f);
                consumeTilt = std::min(1.0f, consumeTilt + dt * 2.8f);
            } else {
                consumeTilt = std::min(1.0f, consumeTilt + dt * 4.5f);
                consumeBiteTimer -= dt;
                if (consumeBiteTimer <= 0.0f && consumeRemaining > 0.001f) {
                    float biteAmt = std::min(consumeRemaining, 0.20f);
                    consumeRemaining -= biteAmt;
                    heldItemDurability = std::max(1, static_cast<int>(std::round(consumeRemaining * 100.0f)));
                    consumePendingRestore += settings.foodRestore * biteAmt;
                    consumeBiteImpulse = 1.0f;
                    consumeBiteTimer = 0.62f;
                    audio.play(SoundId::FoodBite, 0.90f);

                    // Burst fine ration bar crumbs from the bitten front tip!
                    Mat4 hm = heldItemWorldMatrix();
                    float barL = itemBaseGridSize(ItemType::Food).z * Backpack3D::CELL_SIZE * 0.88f;
                    float tipZ = -barL * 0.48f + barL * (0.18f + 0.80f * consumeRemaining);
                    Vec3 bitePos = hm.transformPoint({0.0f, 0.0f, tipZ});
                    Rng pRng(baseSeed ^ static_cast<uint64_t>(particles.size() * 6151 + static_cast<int>(consumeRemaining * 10000.0f)));
                    for (int k = 0; k < 6; ++k) {
                        Vec3 vel{(pRng.nextFloat() - 0.5f) * 0.55f,
                                 0.15f + pRng.nextFloat() * 0.35f,
                                 (pRng.nextFloat() - 0.5f) * 0.55f};
                        particles.push_back({
                            bitePos,
                            vel,
                            0.32f + pRng.nextFloat() * 0.20f,
                            {0.88f, 0.65f, 0.34f},
                            0.020f,
                            7.5f
                        });
                    }

                    if (consumeRemaining <= 0.001f) {
                        hasHeldItem = false;
                        heldPlaceMode = false;
                        heldItemDurability = 0;
                        consumeLmbHeld = false;
                        resetHeldConsumableState(false);
                        hudNoticeText = "RATION FINISHED";
                        hudNoticeTimer = 1.6f;
                        syncInventoryCounts();
                    }
                }
            }
        } else {
            consumeTilt = std::max(0.0f, consumeTilt - dt * 4.5f);
            consumeBiteTimer = std::min(consumeBiteTimer, 0.14f);
            if (consumeRemaining >= 0.999f && consumeOpenProgress > 0.0f) {
                consumeOpenProgress = std::max(0.0f, consumeOpenProgress - dt * 2.0f);
                if (consumeOpenProgress <= 0.0f) consumePlayedOpenSfx = false;
            }
        }
    }
}

void GameWorld::rotateBackpackView(float dx, float dy) {
    backpackViewYaw += dx * 0.0085f;
    // Wrap yaw cleanly to [-PI, PI]
    while (backpackViewYaw > PI) backpackViewYaw -= 2.0f * PI;
    while (backpackViewYaw < -PI) backpackViewYaw += 2.0f * PI;
    backpackViewPitch = clampf(backpackViewPitch + dy * 0.0075f, -1.25f, 1.25f);
}

void GameWorld::toggleBackpackInHands() {
    if (lockpick.active) lockpick.close();
    endInteract();

    if (backpackMode == BackpackMode::OnGround) {
        if (length(backpackGroundPos - player.position) <= 3.4f) {
            backpackMode = BackpackMode::InHands;
            backpackViewYaw = 0.0f;
            backpackViewPitch = 0.0f;
            inventoryOpen = true;
            draggingItem = false;
        } else {
            hudNoticeText = "TOO FAR FROM BACKPACK";
            hudNoticeTimer = 1.6f;
        }
        return;
    }

    if (backpackMode == BackpackMode::OnBack) {
        backpackMode = BackpackMode::InHands;
        heldPlaceMode = false;
        backpackViewYaw = 0.0f;
        backpackViewPitch = 0.0f;
        inventoryOpen = true;
    } else {
        // Returning backpack to back: if currently dragging an item, put it back in backpack or hand
        if (draggingItem) {
            if (backpack.tryAutoAdd(dragItemType, dragItemDurability) < 0 && !hasHeldItem) {
                hasHeldItem = true;
                heldItemType = dragItemType;
                heldItemOrient = dragItemOrient;
                heldItemDurability = dragItemDurability;
            }
            draggingItem = false;
            dragItemDurability = 0;
            syncInventoryCounts();
        }
        backpackMode = BackpackMode::OnBack;
        backpackViewYaw = 0.0f;
        backpackViewPitch = 0.0f;
        inventoryOpen = false;
        hoveredBackpackItemId = -1;
        placementPreview = GhostPreviewState{};
    }
}

bool GameWorld::togglePlaceBackpackOnGround(AudioEngine& audio) {
    if (lockpick.active) return false;
    endInteract();

    if (backpackMode == BackpackMode::OnGround) {
        if (length(backpackGroundPos - player.position) <= 3.4f) {
            backpackMode = BackpackMode::OnBack;
            backpackViewYaw = 0.0f;
            backpackViewPitch = 0.0f;
            inventoryOpen = false;
            draggingItem = false;
            hoveredBackpackItemId = -1;
            audio.play(SoundId::Pickup, 0.85f);
            hudNoticeText = "BACKPACK EQUIPPED";
            hudNoticeTimer = 1.4f;
            return true;
        }
        hudNoticeText = "WALK CLOSER TO BACKPACK";
        hudNoticeTimer = 1.6f;
        return false;
    }

    // Place backpack on the floor / table in front of the player (snapped to 3D grid)
    Vec3 eye = player.eyePosition();
    Vec3 dir = player.forwardDir();
    Vec3 hitN{0.0f, 1.0f, 0.0f};
    float hitT = raycastWorld(eye, dir, 2.6f, &hitN);
    Vec3 placePos;
    if (hitT < 2.55f && hitN.y > 0.5f) {
        placePos = eye + dir * hitT;
    } else {
        // Fallback: directly on the floor 0.85m in front of player
        Vec3 fwdXZ = normalize({std::sin(player.yaw), 0.0f, -std::cos(player.yaw)});
        Vec3 probe = {player.position.x + fwdXZ.x * 0.85f, player.position.y + 0.6f, player.position.z + fwdXZ.z * 0.85f};
        float downT = raycastWorld(probe, {0.0f, -1.0f, 0.0f}, 2.0f);
        placePos = {probe.x, probe.y - std::min(downT, 0.6f), probe.z};
    }

    constexpr float G = Backpack3D::CELL_SIZE;
    placePos.x = std::round(placePos.x / G) * G;
    placePos.z = std::round(placePos.z / G) * G;

    if (draggingItem) {
        backpack.tryAutoAdd(dragItemType, dragItemDurability);
        draggingItem = false;
        dragItemDurability = 0;
        syncInventoryCounts();
    }

    backpackGroundPos = placePos;
    backpackGroundYaw = player.yaw;
    backpackViewYaw = 0.0f;
    backpackViewPitch = 0.0f;
    backpackMode = BackpackMode::OnGround;
    inventoryOpen = false;
    audio.play3D(SoundId::DoorImpact, placePos, 0.32f, 0.0f, 0.35f);
    hudNoticeText = "BACKPACK PLACED  [B PICK UP]";
    hudNoticeTimer = 1.8f;
    return true;
}

bool GameWorld::computeWorldGridSnap(const Vec3& rayOrig, const Vec3& rayDir,
                                     ItemType type, const ItemOrientation& orient,
                                     Vec3& outSnappedPos) const {
    constexpr float MAX_PLACE_DIST = 3.2f;
    Vec3 hitN{0.0f, 1.0f, 0.0f};
    float bestT = raycastWorld(rayOrig, rayDir, MAX_PLACE_DIST, &hitN);
    bool hitAny = (bestT < MAX_PLACE_DIST - 0.01f);

    // Also allow stacking on top of existing world items on the 3D grid!
    for (const auto& wi : worldItems) {
        if (wi.pickedUp) continue;
        float itT = 0.0f;
        Vec3 itN{};
        if (rayIntersectAABB(rayOrig, rayDir, wi.worldBounds(), bestT, itT, &itN)) {
            if (itT < bestT) {
                bestT = itT;
                hitN = itN;
                hitAny = true;
            }
        }
    }
    if (!hitAny || hitN.y < -0.3f) return false; // Cannot hang from ceiling

    Vec3 hitPt = rayOrig + rayDir * bestT;
    Int3 sz = orient.applyToSize(itemBaseGridSize(type));
    constexpr float G = Backpack3D::CELL_SIZE;
    float halfX = sz.x * G * 0.5f;
    float halfZ = sz.z * G * 0.5f;

    Vec3 pos = hitPt;
    if (hitN.y <= 0.5f) {
        // Side face hit: offset outward along wall normal so it doesn't clip into wall
        pos.x += hitN.x * (halfX + 0.01f);
        pos.z += hitN.z * (halfZ + 0.01f);
        // Drop down to floor/surface below
        float dropT = raycastWorld({pos.x, pos.y + 0.05f, pos.z}, {0.0f, -1.0f, 0.0f}, 2.2f);
        if (dropT >= 2.15f) return false;
        pos.y = pos.y + 0.05f - dropT;
    }

    // Snap X/Z cleanly to the 3D world grid (0.06m cell size)
    pos.x = std::round(pos.x / G) * G;
    pos.z = std::round(pos.z / G) * G;

    // Validate no overlap with solid WorldGrid voxels or existing items
    WorldItem probe{pos, type, false, 0, 0, orient};
    AABB box = probe.worldBounds();
    int minX = static_cast<int>(std::floor(box.min.x + 0.005f));
    int maxX = static_cast<int>(std::floor(box.max.x - 0.005f));
    int minY = static_cast<int>(std::floor(box.min.y + 0.005f));
    int maxY = static_cast<int>(std::floor(box.max.y - 0.005f));
    int minZ = static_cast<int>(std::floor(box.min.z + 0.005f));
    int maxZ = static_cast<int>(std::floor(box.max.z - 0.005f));
    for (int y = minY; y <= maxY; ++y)
        for (int z = minZ; z <= maxZ; ++z)
            for (int x = minX; x <= maxX; ++x)
                if (map.world.getCell(x, y, z).isSolid()) return false;

    for (const auto& wi : worldItems) {
        if (wi.pickedUp) continue;
        AABB wb = wi.worldBounds();
        // Allow resting directly on top (min.y == wb.max.y) without false overlap
        AABB shrunk{{box.min.x + 0.005f, box.min.y + 0.005f, box.min.z + 0.005f},
                    {box.max.x - 0.005f, box.max.y - 0.005f, box.max.z - 0.005f}};
        if (shrunk.intersects(wb)) return false;
    }

    outSnappedPos = pos;
    return true;
}

void GameWorld::updateCursorHover(const Vec3& rayOrig, const Vec3& rayDir) {
    hoveredBackpackItemId = -1;
    hoveredBackpackBlocked = false;
    hoveredGroundedBackpack = false;
    placementPreview = GhostPreviewState{};

    bool backpackVisible = (backpackMode == BackpackMode::InHands) ||
                           (backpackMode == BackpackMode::OnGround &&
                            length(backpackGroundPos - player.position) <= 3.5f);

    bool placingActive = draggingItem || hasHeldItem;
    ItemType activeType = draggingItem ? dragItemType : heldItemType;
    ItemOrientation activeOri = draggingItem ? dragItemOrient : heldItemOrient;

    // 1) Raycast into the 3D backpack (if held in hands OR placed on the ground nearby)
    if (backpackVisible) {
        Mat4 invBp = backpackWorldMatrix().inverseRigid();
        Vec3 localOrig = invBp.transformPoint(rayOrig);
        Vec3 localDir = normalize(invBp.transformDir(rayDir));

        constexpr float W = Backpack3D::DIM_X * Backpack3D::CELL_SIZE;
        constexpr float H = Backpack3D::DIM_Y * Backpack3D::CELL_SIZE;
        constexpr float D = Backpack3D::DIM_Z * Backpack3D::CELL_SIZE;
        AABB backOuter = Backpack3D::backSlotOuterBounds();
        AABB outerLocal{
            {std::min(-0.03f, backOuter.min.x - 0.03f),
             std::min(-0.03f, backOuter.min.y - 0.03f),
             std::min(-0.03f, backOuter.min.z - 0.03f)},
            {std::max(W + 0.03f, backOuter.max.x + 0.03f),
             std::max(H + 0.03f, backOuter.max.y + 0.03f),
             std::max(D + 0.03f, backOuter.max.z + 0.03f)}
        };
        float outerT = 0.0f;
        if (rayIntersectAABB(localOrig, localDir, outerLocal, 3.5f, outerT)) {
            if (backpackMode == BackpackMode::OnGround) hoveredGroundedBackpack = true;

            if (placingActive) {
                int gx = 0, gy = 0, gz = 0;
                bool inBack = false;
                if (backpack.raycastPlacementLocal(localOrig, localDir, 3.5f, activeType, activeOri, gx, gy, gz, inBack)) {
                    placementPreview.active = true;
                    placementPreview.valid = true;
                    placementPreview.inBackpack = true;
                    placementPreview.inBackSlot = inBack;
                    placementPreview.type = activeType;
                    placementPreview.orient = activeOri;
                    placementPreview.gx = gx;
                    placementPreview.gy = gy;
                    placementPreview.gz = gz;
                    return;
                }
            } else {
                int hitId = backpack.raycastItemsLocal(localOrig, localDir, 3.5f);
                if (hitId >= 0) {
                    hoveredBackpackItemId = hitId;
                    hoveredBackpackBlocked = backpack.isBlockedFromAbove(hitId);
                    return;
                }
            }
        }
    }

    // 2) Show 3D world-grid placement preview when RMB-dragging from backpack OR when RMB placement mode is active for held item!
    if (draggingItem || (hasHeldItem && heldPlaceMode && backpackMode != BackpackMode::InHands && hoveredDoorIdx < 0 && hoveredItemIdx < 0)) {
        Vec3 snapped{};
        bool ok = computeWorldGridSnap(rayOrig, rayDir, activeType, activeOri, snapped);
        placementPreview.active = ok;
        placementPreview.valid = ok;
        placementPreview.inBackpack = false;
        placementPreview.inBackSlot = false;
        placementPreview.type = activeType;
        placementPreview.orient = activeOri;
        placementPreview.worldPos = snapped;
    }
}

void GameWorld::rotateActiveItem(int axis) {
    ItemOrientation* targetOri = nullptr;
    if (draggingItem) targetOri = &dragItemOrient;
    else if (hasHeldItem && heldPlaceMode) targetOri = &heldItemOrient; // Only rotate placement ghost, NEVER casual held item!
    if (!targetOri) return;
    if (axis == 0) targetOri->rotateX();
    else if (axis == 1) targetOri->rotateY();
    else targetOri->rotateZ();
}

bool GameWorld::performMeleeAttack(AudioEngine& audio) {
    if (gameOver || !hasHeldItem || !isMeleeWeapon(heldItemType) || meleeCooldown > 0.0f) return false;

    meleeSwingTimer = 0.28f;
    meleeCooldown = 0.42f;

    float reach = 2.5f;
    float damage = 14.0f;
    switch (heldItemType) {
        case ItemType::Spear: reach = 3.0f; damage = 42.0f; break;
        case ItemType::Knife: reach = 2.0f; damage = 22.0f; break;
        case ItemType::Broom:
        case ItemType::Mop:   reach = 2.6f; damage = 16.0f; break;
        case ItemType::Stick: reach = 2.5f; damage = 14.0f; break;
        default: break;
    }

    Vec3 origin = player.eyePosition();
    Vec3 fwd = player.forwardDir();
    float wallT = raycastWorld(origin, fwd, reach);

    Enemy* hitEnemy = nullptr;
    float bestDist = std::min(wallT, reach);
    for (auto& e : enemies) {
        if (!e.alive) continue;
        Vec3 eCenter{e.pos.x, e.pos.y + e.height * 0.55f, e.pos.z};
        Vec3 toE = eCenter - origin;
        float dist = length(toE);
        if (dist > reach + e.radius || dist < 0.05f) continue;
        Vec3 dirE = toE / dist;
        if (dot(fwd, dirE) < 0.55f) continue;
        float losT = raycastWorld(origin, dirE, dist);
        if (losT >= dist - e.radius - 0.15f && dist < bestDist + e.radius) {
            bestDist = dist;
            hitEnemy = &e;
        }
    }

    bool landedHit = false;
    Rng rng(baseSeed ^ static_cast<uint64_t>(kills * 997 + worldItems.size() * 131 + static_cast<int>(origin.x * 100.0f)));
    if (hitEnemy) {
        landedHit = true;
        hitEnemy->hp -= damage;
        hitEnemy->hurtFlash = 0.18f;
        hitEnemy->alerted = true;
        hitEnemy->aiState = EnemyAIState::Chase;
        hitEnemy->lastSeenPos = player.position;
        audio.play(SoundId::HitEnemy, 1.0f);

        Vec3 hitPos = origin + fwd * std::min(bestDist, reach);
        for (int s = 0; s < 6; ++s) {
            particles.push_back({
                hitPos,
                {(rng.nextFloat() - 0.5f) * 4.5f, rng.nextFloat() * 3.2f, (rng.nextFloat() - 0.5f) * 4.5f},
                0.24f,
                {1.0f, 0.35f, 0.25f}
            });
        }
        if (hitEnemy->hp <= 0.0f) {
            hitEnemy->alive = false;
            kills++;
        }
    } else if (wallT < reach * 0.88f) {
        landedHit = true;
        Vec3 hitPos = origin + fwd * wallT;
        audio.play3D(SoundId::DoorImpact, hitPos, 0.45f, 0.0f, 0.55f);
        for (int s = 0; s < 4; ++s) {
            particles.push_back({
                hitPos,
                {(rng.nextFloat() - 0.5f) * 3.0f, rng.nextFloat() * 2.2f, (rng.nextFloat() - 0.5f) * 3.0f},
                0.18f,
                {0.95f, 0.85f, 0.45f}
            });
        }
    } else {
        // Air swing swoosh
        audio.play(SoundId::PlayerStep, 0.28f, 0.8f, 0.6f);
    }

    // Broom / Mop head breaks after 1..3 hits and transforms into a Stick!
    if (landedHit && (heldItemType == ItemType::Broom || heldItemType == ItemType::Mop)) {
        if (heldItemDurability <= 0) {
            heldItemDurability = defaultItemDurability(heldItemType, baseSeed ^ static_cast<uint64_t>(kills * 31 + 7));
        }
        heldItemDurability--;
        if (heldItemDurability <= 0) {
            heldItemType = ItemType::Stick;
            heldItemDurability = 0;
            audio.play(SoundId::LockBreak, 0.95f);
            hudNoticeText = "HEAD BROKE -> STICK";
            hudNoticeTimer = 2.0f;
            syncInventoryCounts();
        }
    }
    return true;
}

void GameWorld::updateSurfaceCrafting() {
    availableCrafts.clear();
    if (lockpick.active || backpackMode == BackpackMode::InHands || draggingItem) return;
    if (hoveredItemIdx < 0 || hoveredItemIdx >= static_cast<int>(worldItems.size())) return;

    const auto& anchor = worldItems[hoveredItemIdx];
    if (anchor.pickedUp) return;

    // Scan items placed nearby on the same 3D grid surface (within 0.65m XZ and 0.18m height)
    int stickIdx = -1, tapeIdx = -1, knifeIdx = -1;
    float bestStickD = 1e9f, bestTapeD = 1e9f, bestKnifeD = 1e9f;
    for (size_t i = 0; i < worldItems.size(); ++i) {
        const auto& wi = worldItems[i];
        if (wi.pickedUp) continue;
        if (std::abs(wi.pos.y - anchor.pos.y) > 0.18f) continue;
        float dx = wi.pos.x - anchor.pos.x;
        float dz = wi.pos.z - anchor.pos.z;
        float dSq = dx * dx + dz * dz;
        if (dSq > 0.65f * 0.65f) continue;

        if (wi.type == ItemType::Stick && dSq < bestStickD) {
            bestStickD = dSq;
            stickIdx = static_cast<int>(i);
        } else if (wi.type == ItemType::Tape && dSq < bestTapeD) {
            bestTapeD = dSq;
            tapeIdx = static_cast<int>(i);
        } else if (wi.type == ItemType::Knife && dSq < bestKnifeD) {
            bestKnifeD = dSq;
            knifeIdx = static_cast<int>(i);
        }
    }

    if (stickIdx >= 0 && tapeIdx >= 0 && knifeIdx >= 0) {
        constexpr float G = Backpack3D::CELL_SIZE;
        Vec3 outPos = worldItems[stickIdx].pos;
        outPos.x = std::round(outPos.x / G) * G;
        outPos.z = std::round(outPos.z / G) * G;
        CraftOption opt;
        opt.recipeId = 1;
        opt.result = ItemType::Spear;
        opt.label = "[1] SPEAR (STICK + TAPE + KNIFE)";
        opt.ingredientWorldIdx = {stickIdx, tapeIdx, knifeIdx};
        opt.outputPos = outPos;
        availableCrafts.push_back(opt);
    }
}

bool GameWorld::performCraft(int optionIdx, AudioEngine& audio) {
    if (optionIdx < 0 || optionIdx >= static_cast<int>(availableCrafts.size())) return false;
    CraftOption opt = availableCrafts[optionIdx];

    for (int idx : opt.ingredientWorldIdx) {
        if (idx < 0 || idx >= static_cast<int>(worldItems.size()) || worldItems[idx].pickedUp) {
            availableCrafts.clear();
            return false;
        }
    }

    // Preserve stick orientation if lying flat, else place crafted Spear neatly lying flat on the surface
    ItemOrientation outOri{};
    outOri.rotX = 1; // Lie horizontally on the table/floor surface (1x1x30)
    for (int idx : opt.ingredientWorldIdx) {
        if (worldItems[idx].type == ItemType::Stick) {
            outOri = worldItems[idx].orient;
        }
        worldItems[idx].pickedUp = true;
    }

    int rx = WorldGrid::floorDiv(static_cast<int>(std::floor(opt.outputPos.x)), cfg::REGION_SIZE);
    int rz = WorldGrid::floorDiv(static_cast<int>(std::floor(opt.outputPos.z)), cfg::REGION_SIZE);
    worldItems.push_back({opt.outputPos, opt.result, false, rx, rz, outOri, 0});

    audio.play3D(SoundId::Pickup, opt.outputPos, 1.0f);
    hudNoticeText = "CRAFTED IMPROVISED SPEAR";
    hudNoticeTimer = 2.2f;
    availableCrafts.clear();
    hoveredItemIdx = static_cast<int>(worldItems.size()) - 1;
    return true;
}

bool GameWorld::useHeldItem(AudioEngine& audio) {
    if (!hasHeldItem) return false;
    switch (heldItemType) {
        case ItemType::Battery:
            if (inventory.hasFlashlight && inventory.flashlightBattery < settings.maxBattery - 0.5f) {
                inventory.flashlightBattery = std::min(settings.maxBattery,
                                                       inventory.flashlightBattery + settings.batteryRestore);
                hasHeldItem = false;
                heldPlaceMode = false;
                syncInventoryCounts();
                audio.play(SoundId::Pickup, 0.75f);
                return true;
            }
            return false;
        case ItemType::Food:
            if (hunger < settings.maxHunger - 0.5f) {
                hunger = std::min(settings.maxHunger, hunger + settings.foodRestore);
                hasHeldItem = false;
                heldPlaceMode = false;
                syncInventoryCounts();
                audio.play(SoundId::Pickup, 0.75f);
                return true;
            }
            return false;
        case ItemType::Water:
            if (thirst < settings.maxThirst - 0.5f) {
                thirst = std::min(settings.maxThirst, thirst + settings.waterRestore);
                hasHeldItem = false;
                heldPlaceMode = false;
                syncInventoryCounts();
                audio.play(SoundId::Pickup, 0.75f);
                return true;
            }
            return false;
        case ItemType::Flashlight:
            if (inventory.flashlightBattery > 0.0f) {
                flashlightOn = !flashlightOn;
                audio.play(SoundId::FlashlightClick, 0.95f);
                return true;
            }
            return false;
        default:
            return false;
    }
}

bool GameWorld::handlePrimaryClick(const Vec3& rayOrig, const Vec3& rayDir, AudioEngine& audio) {
    updateCursorHover(rayOrig, rayDir);

    // 1) If RMB-dragging an item, LMB places it into backpack cell/rear strap slot or onto 3D world grid
    if (draggingItem) {
        if (placementPreview.active && placementPreview.valid) {
            if (placementPreview.inBackpack) {
                if (placementPreview.inBackSlot) {
                    backpack.placeInBackSlot(placementPreview.gx, placementPreview.gy, placementPreview.gz,
                                             dragItemType, dragItemOrient, dragItemDurability);
                } else {
                    backpack.placeAt(placementPreview.gx, placementPreview.gy, placementPreview.gz,
                                     dragItemType, dragItemOrient, dragItemDurability);
                }
                audio.play(SoundId::Pickup, 0.75f);
            } else {
                int rx = WorldGrid::floorDiv(static_cast<int>(std::floor(placementPreview.worldPos.x)), cfg::REGION_SIZE);
                int rz = WorldGrid::floorDiv(static_cast<int>(std::floor(placementPreview.worldPos.z)), cfg::REGION_SIZE);
                worldItems.push_back({placementPreview.worldPos, dragItemType, false, rx, rz, dragItemOrient, dragItemDurability});
                audio.play3D(SoundId::Pickup, placementPreview.worldPos, 0.8f);
            }
            draggingItem = false;
            dragItemDurability = 0;
            dragOriginBackpackId = -1;
            placementPreview = GhostPreviewState{};
            syncInventoryCounts();
            return true;
        }
        hudNoticeText = "INVALID PLACEMENT";
        hudNoticeTimer = 1.2f;
        return true;
    }

    // 2) Clicking LMB on an item inside the 3D backpack -> take into hand & return backpack to back!
    if (hoveredBackpackItemId >= 0) {
        if (hoveredBackpackBlocked) {
            hudNoticeText = "BLOCKED ABOVE - MOVE TOP ITEMS";
            hudNoticeTimer = 1.8f;
            audio.play(SoundId::DoorImpact, 0.25f, 0.0f, 0.3f);
            return true;
        }
        PackedItem removed;
        if (backpack.removeById(hoveredBackpackItemId, &removed)) {
            if (hasHeldItem) {
                // Auto-pack previously held item if possible, otherwise drop at feet
                if (backpack.tryAutoAdd(heldItemType, heldItemDurability) < 0) {
                    worldItems.push_back({player.position, heldItemType, false, map.currentRegX, map.currentRegZ, heldItemOrient, heldItemDurability});
                }
            }
            hasHeldItem = true;
            heldPlaceMode = false;
            heldItemType = removed.type;
            heldItemOrient = removed.orient;
            heldItemDurability = removed.durability;
            resetHeldConsumableState(true);
            hoveredBackpackItemId = -1;
            // Automatically put backpack back on back when taking an item into hand!
            if (backpackMode == BackpackMode::InHands) {
                backpackMode = BackpackMode::OnBack;
                backpackViewYaw = 0.0f;
                backpackViewPitch = 0.0f;
                inventoryOpen = false;
            }
            syncInventoryCounts();
            audio.play(SoundId::Pickup, 0.85f);
            return true;
        }
    }

    // 3) If holding an item and clicking a valid slot inside the backpack or rear strap slot -> pack it!
    if (hasHeldItem && placementPreview.active && placementPreview.valid && placementPreview.inBackpack) {
        if (placementPreview.inBackSlot) {
            backpack.placeInBackSlot(placementPreview.gx, placementPreview.gy, placementPreview.gz,
                                     heldItemType, heldItemOrient, heldItemDurability);
        } else {
            backpack.placeAt(placementPreview.gx, placementPreview.gy, placementPreview.gz,
                             heldItemType, heldItemOrient, heldItemDurability);
        }
        hasHeldItem = false;
        heldPlaceMode = false;
        heldItemDurability = 0;
        consumeLmbHeld = false;
        resetHeldConsumableState(false);
        placementPreview = GhostPreviewState{};
        syncInventoryCounts();
        audio.play(SoundId::Pickup, 0.8f);
        return true;
    }

    // 4) Normal world interaction when backpack is NOT in hands
    if (backpackMode != BackpackMode::InHands) {
        // If RMB placement mode is active for the held item -> LMB confirms placement on the 3D grid!
        if (hasHeldItem && heldPlaceMode) {
            if (placementPreview.active && placementPreview.valid && !placementPreview.inBackpack) {
                int rx = WorldGrid::floorDiv(static_cast<int>(std::floor(placementPreview.worldPos.x)), cfg::REGION_SIZE);
                int rz = WorldGrid::floorDiv(static_cast<int>(std::floor(placementPreview.worldPos.z)), cfg::REGION_SIZE);
                worldItems.push_back({placementPreview.worldPos, heldItemType, false, rx, rz, heldItemOrient, heldItemDurability});
                hasHeldItem = false;
                heldPlaceMode = false;
                heldItemDurability = 0;
                consumeLmbHeld = false;
                resetHeldConsumableState(false);
                placementPreview = GhostPreviewState{};
                syncInventoryCounts();
                audio.play3D(SoundId::Pickup, worldItems.back().pos, 0.8f);
                return true;
            }
            hudNoticeText = "INVALID PLACEMENT";
            hudNoticeTimer = 1.0f;
            return true;
        }
        if (beginInteract(audio)) return true;
        // If holding a melee weapon (Broom/Mop/Stick/Knife/Spear), LMB swings it as a weapon!
        if (hasHeldItem && isMeleeWeapon(heldItemType)) {
            performMeleeAttack(audio);
            return true;
        }
    }
    return false;
}

bool GameWorld::handleSecondaryClick(const Vec3& rayOrig, const Vec3& rayDir, AudioEngine& audio) {
    updateCursorHover(rayOrig, rayDir);

    // 1) If already RMB-dragging an item -> drop/place it (or cancel back into backpack if invalid)
    if (draggingItem) {
        if (placementPreview.active && placementPreview.valid) {
            return handlePrimaryClick(rayOrig, rayDir, audio);
        }
        // Cancel drag: restore item into backpack or hand
        if (backpack.tryAutoAdd(dragItemType, dragItemDurability) < 0 && !hasHeldItem) {
            hasHeldItem = true;
            heldPlaceMode = false;
            heldItemType = dragItemType;
            heldItemOrient = dragItemOrient;
            heldItemDurability = dragItemDurability;
            resetHeldConsumableState(true);
        }
        draggingItem = false;
        dragItemDurability = 0;
        dragOriginBackpackId = -1;
        placementPreview = GhostPreviewState{};
        syncInventoryCounts();
        return true;
    }

    // 2) Pressing RMB on an unblocked item in the backpack -> stop camera & drag to floor / surface / slot!
    if (hoveredBackpackItemId >= 0) {
        if (hoveredBackpackBlocked) {
            hudNoticeText = "BLOCKED ABOVE - MOVE TOP ITEMS";
            hudNoticeTimer = 1.8f;
            audio.play(SoundId::DoorImpact, 0.25f, 0.0f, 0.3f);
            return true;
        }
        PackedItem removed;
        if (backpack.removeById(hoveredBackpackItemId, &removed)) {
            draggingItem = true;
            dragItemType = removed.type;
            dragItemOrient = removed.orient;
            dragItemDurability = removed.durability;
            dragOriginBackpackId = removed.id;
            hoveredBackpackItemId = -1;
            syncInventoryCounts();
            audio.play(SoundId::Pickup, 0.7f);
            return true;
        }
    }

    // 3) If holding an item in hand -> RMB toggles 3D ground placement preview mode!
    if (hasHeldItem && backpackMode != BackpackMode::InHands) {
        heldPlaceMode = !heldPlaceMode;
        if (heldPlaceMode) {
            updateCursorHover(rayOrig, rayDir);
        } else {
            placementPreview = GhostPreviewState{};
        }
        return true;
    }
    return false;
}

void GameWorld::updateHover() {
    hoveredDoorIdx = -1;
    hoveredItemIdx = -1;
    Vec3 orig = player.eyePosition();
    Vec3 dir = player.forwardDir();
    float wallT = raycastWorld(orig, dir, 2.7f);
    float bestT = wallT;

    // Check doors first so closed cabinet door blocks grabbing items behind it
    for (size_t i = 0; i < doors.size(); ++i) {
        float t = 0.0f;
        if (rayIntersectAABB(orig, dir, doors[i].worldBounds(), bestT, t)) {
            bestT = t;
            hoveredDoorIdx = static_cast<int>(i);
            hoveredItemIdx = -1;
        }
    }

    for (size_t i = 0; i < worldItems.size(); ++i) {
        if (worldItems[i].pickedUp) continue;
        AABB itemBox = worldItems[i].worldBounds();
        // Slight generous pad so tiny lockpicks/batteries are comfortable to target
        itemBox.min -= Vec3{0.04f, 0.02f, 0.04f};
        itemBox.max += Vec3{0.04f, 0.04f, 0.04f};
        float t = 0.0f;
        if (rayIntersectAABB(orig, dir, itemBox, bestT, t)) {
            bestT = t;
            hoveredItemIdx = static_cast<int>(i);
            hoveredDoorIdx = -1;
        }
    }

    updateSurfaceCrafting();
}

bool GameWorld::beginInteract(AudioEngine& audio, bool quickToggle) {
    updateHover();
    if (hoveredItemIdx >= 0 && hoveredItemIdx < static_cast<int>(worldItems.size())) {
        auto& it = worldItems[hoveredItemIdx];
        // Auto-place picked up item into the 6x8x4 3D backpack or rear 2x30x6 strap slot!
        int placedId = backpack.tryAutoAdd(it.type, it.durability);
        if (placedId >= 0) {
            it.pickedUp = true;
            if (it.type == ItemType::Flashlight && !inventory.hasFlashlight) {
                inventory.flashlightBattery = settings.maxBattery;
            }
            syncInventoryCounts();
            audio.play3D(SoundId::Pickup, it.pos, 0.9f);
            hoveredItemIdx = -1;
            return true;
        }
        // Backpack is full: take into hand if hands are free!
        if (!hasHeldItem) {
            it.pickedUp = true;
            hasHeldItem = true;
            heldPlaceMode = false;
            heldItemType = it.type;
            heldItemOrient = it.orient;
            heldItemDurability = it.durability;
            resetHeldConsumableState(true);
            if (it.type == ItemType::Flashlight && !inventory.hasFlashlight) {
                inventory.flashlightBattery = settings.maxBattery;
            }
            syncInventoryCounts();
            audio.play3D(SoundId::Pickup, it.pos, 0.9f);
            hudNoticeText = "BACKPACK FULL - IN HAND";
            hudNoticeTimer = 1.8f;
            hoveredItemIdx = -1;
            return true;
        }
        hudNoticeText = "BACKPACK AND HANDS FULL";
        hudNoticeTimer = 1.8f;
        return true;
    }
    if (hoveredDoorIdx >= 0 && hoveredDoorIdx < static_cast<int>(doors.size())) {
        auto& d = doors[hoveredDoorIdx];
        Vec3 lockPos = d.hingePos + Vec3{0.0f, d.isHatch ? 0.05f : d.height * 0.5f, 0.0f};
        if (d.locked) {
            if (inventory.lockpicks <= 0) {
                audio.play3D(SoundId::DoorImpact, lockPos, 0.35f, d.isCabinet ? 0.0f : 1.0f, 0.35f);
                hudNoticeText = "NEED LOCKPICK";
                hudNoticeTimer = 1.8f;
                return true;
            }
            float bChance = d.isExitDoor ? 0.0f : settings.lockpickBreakChance;
            lockpick.open(hoveredDoorIdx, d.lockSeed, bChance, &lockPos);
            return true;
        }
        if (d.isHatch && quickToggle) {
            // Press E on a floor hatch -> smoothly swing all the way open or closed!
            d.angularVel = (d.angle < 0.75f) ? 6.5f : -6.5f;
            return true;
        }
        grabbedDoorIdx = hoveredDoorIdx;
        return true;
    }
    return false;
}

void GameWorld::dragGrabbedDoor(float mouseDx, float mouseDy) {
    if (grabbedDoorIdx < 0 || grabbedDoorIdx >= static_cast<int>(doors.size())) return;
    auto& d = doors[grabbedDoorIdx];
    if (d.isHatch) {
        // Pulling mouse up opens floor hatch upward; pushing down closes it
        float pull = (-mouseDy) * 0.025f;
        d.angularVel = clampf(d.angularVel + pull * 8.5f, -5.8f, 5.8f);
        return;
    }
    float y = d.currentYaw();
    Vec3 swingDir = Vec3{-std::sin(y), 0.0f, -std::cos(y)} * d.openSign;
    Vec3 mouseWorldMove = player.rightDir() * mouseDx + player.forwardDir() * (-mouseDy);
    float pull = dot(mouseWorldMove, swingDir) * 0.018f;
    d.angularVel = clampf(d.angularVel + pull * 8.5f, -5.5f, 5.5f);
}

void GameWorld::endInteract() {
    grabbedDoorIdx = -1;
}

bool GameWorld::useInventoryItem(ItemType type, AudioEngine& audio) {
    // Allow consuming from hand first, else from an UNBLOCKED slot in the 3D backpack!
    auto consumeOne = [&]() -> bool {
        if (hasHeldItem && heldItemType == type) {
            hasHeldItem = false;
            syncInventoryCounts();
            return true;
        }
        int id = backpack.findFirstAccessible(type);
        if (id >= 0) {
            backpack.removeById(id);
            syncInventoryCounts();
            return true;
        }
        if (backpack.countType(type) > 0) {
            hudNoticeText = "BLOCKED ABOVE - MOVE TOP ITEMS";
            hudNoticeTimer = 1.8f;
            return false;
        }
        // Fallback for tests that manipulate legacy inventory counts directly without backpack
        if (type == ItemType::Battery && inventory.batteries > 0) { inventory.batteries--; return true; }
        if (type == ItemType::Food && inventory.food > 0) { inventory.food--; return true; }
        if (type == ItemType::Water && inventory.water > 0) { inventory.water--; return true; }
        return false;
    };

    switch (type) {
        case ItemType::Battery:
            if (inventory.hasFlashlight && inventory.flashlightBattery < settings.maxBattery - 0.5f && consumeOne()) {
                inventory.flashlightBattery = std::min(settings.maxBattery,
                                                       inventory.flashlightBattery + settings.batteryRestore);
                audio.play(SoundId::Pickup, 0.7f);
                return true;
            }
            return false;
        case ItemType::Food:
            if (hunger < settings.maxHunger - 0.5f && consumeOne()) {
                hunger = std::min(settings.maxHunger, hunger + settings.foodRestore);
                audio.play(SoundId::Pickup, 0.7f);
                return true;
            }
            return false;
        case ItemType::Water:
            if (thirst < settings.maxThirst - 0.5f && consumeOne()) {
                thirst = std::min(settings.maxThirst, thirst + settings.waterRestore);
                audio.play(SoundId::Pickup, 0.7f);
                return true;
            }
            return false;
        default:
            return false;
    }
}

void GameWorld::gatherInteractables(std::vector<DoorRenderState>& outDoors,
                                    std::vector<ItemRenderState>& outItems) const {
    outDoors.clear();
    for (size_t i = 0; i < doors.size(); ++i) {
        const auto& d = doors[i];
        bool hl = (static_cast<int>(i) == hoveredDoorIdx || static_cast<int>(i) == grabbedDoorIdx);
        outDoors.push_back({d.hingePos, d.width, d.height, d.thickness,
                            d.isHatch ? d.closedYaw : d.currentYaw(),
                            d.isCabinet, d.isRareCabinet, hl, d.isHatch, d.angle});
    }

    outItems.clear();
    for (size_t i = 0; i < worldItems.size(); ++i) {
        if (worldItems[i].pickedUp) continue;
        bool hl = (static_cast<int>(i) == hoveredItemIdx);
        float fill = 1.0f;
        if ((worldItems[i].type == ItemType::Water || worldItems[i].type == ItemType::Food) &&
            worldItems[i].durability > 0) {
            fill = clampf(static_cast<float>(worldItems[i].durability) / 100.0f, 0.02f, 1.0f);
        }
        outItems.push_back({worldItems[i].pos, static_cast<int>(worldItems[i].type), worldItems[i].orient, hl, fill});
    }
}

BackpackRenderState GameWorld::gatherBackpackState() const {
    BackpackRenderState bs;
    bs.visible = (backpackMode == BackpackMode::InHands || backpackMode == BackpackMode::OnGround);
    if (bs.visible) {
        bs.worldMatrix = backpackWorldMatrix();
        bs.hoveredItemId = hoveredBackpackItemId;
        bs.hoveredBlocked = hoveredBackpackBlocked;
        bs.backpack = &backpack;
    }
    return bs;
}

void GameWorld::gatherRobotLights(std::vector<PointLight3D>& out) const {
    for (const auto& e : enemies) {
        if (!e.alive) continue;
        PointLight3D pl;
        pl.pos = {
            e.pos.x + std::sin(e.yaw) * 0.30f,
            e.pos.y + 2.28f,
            e.pos.z + std::cos(e.yaw) * 0.30f
        };
        pl.color = e.alerted
                       ? Vec3{settings.robotAlertLedColorR, settings.robotAlertLedColorG, settings.robotAlertLedColorB}
                       : Vec3{settings.robotLedColorR, settings.robotLedColorG, settings.robotLedColorB};
        pl.radius = e.alerted ? settings.robotAlertLedRadius : settings.robotLedRadius;
        pl.intensity = e.alerted ? settings.robotAlertLedIntensity : settings.robotLedIntensity;
        out.push_back(pl);
    }
}

} // namespace mega
