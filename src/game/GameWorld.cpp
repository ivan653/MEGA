#include "game/GameWorld.hpp"
#include "core/Rng.hpp"
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace mega {

namespace {

bool rayIntersectAABB(const Vec3& orig, const Vec3& dir, const AABB& b, float maxDist, float& outT) {
    float tmin = 0.0f;
    float tmax = maxDist;
    float o[3] = {orig.x, orig.y, orig.z};
    float d[3] = {dir.x, dir.y, dir.z};
    float mn[3] = {b.min.x, b.min.y, b.min.z};
    float mx[3] = {b.max.x, b.max.y, b.max.z};

    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-6f) {
            if (o[i] < mn[i] || o[i] > mx[i]) return false;
        } else {
            float inv = 1.0f / d[i];
            float t1 = (mn[i] - o[i]) * inv;
            float t2 = (mx[i] - o[i]) * inv;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    outT = tmin;
    return true;
}

} // namespace

AABB HingeDoor::worldBounds() const {
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
    health = 100.0f;
    hunger = 100.0f;
    thirst = 100.0f;
    energy = 100.0f;
    oxygen = 100.0f;
    ammo = 48;
    kills = 0;
    inventory = Inventory{};
    flashlightOn = false;
    inventoryOpen = false;
    hoveredDoorIdx = -1;
    hoveredItemIdx = -1;
    grabbedDoorIdx = -1;
    weapon = WeaponType::Bolter;
    gameOver = false;
    damageFlash = 0.0f;
    generateLevel(seed);
}

void GameWorld::generateLevel(uint64_t seed) {
    map = MegaGenerator::generate(seed, settings);
    player.position = map.spawnPos;
    player.velocity = {0.0f, 0.0f, 0.0f};
    player.yaw = -PI * 0.5f; // Face West toward the table with Flashlight & Batteries
    player.pitch = 0.0f;
    worldStreamChanged = true;

    enemies.clear();
    doors.clear();
    worldItems.clear();
    pickups.clear();
    projectiles.clear();
    particles.clear();

    // 1) 2-meter Metal Exit Door leading East out of starter room (x=61.0, z=58..59) into spineNS1
    HingeDoor exitDoor;
    exitDoor.hingePos = {61.05f, 1.0f, 58.0f};
    exitDoor.width = 1.0f;
    exitDoor.height = 2.0f;
    exitDoor.thickness = 0.09f;
    exitDoor.closedYaw = -PI * 0.5f; // +X rotated by -PI/2 lies along +Z (58.0 .. 59.0)
    exitDoor.openSign = 1.0f;        // Swings outward into corridor (+X)
    exitDoor.maxAngle = 1.85f;
    exitDoor.isCabinet = false;
    doors.push_back(exitDoor);

    // 2) Hinged Cabinet Door on the Food/Water cabinet against the North wall (x=57.36..58.64, z=56.05)
    HingeDoor cabDoor;
    cabDoor.hingePos = {57.36f, 1.04f, 56.05f};
    cabDoor.width = 1.28f;
    cabDoor.height = 1.76f;
    cabDoor.thickness = 0.05f;
    cabDoor.closedYaw = 0.0f;        // Lies along +X (57.36 .. 58.64)
    cabDoor.openSign = -1.0f;        // Swings open into the room (+Z)
    cabDoor.maxAngle = 1.85f;
    cabDoor.isCabinet = true;
    doors.push_back(cabDoor);

    // 3) Starter Room Items on West Table (x=55.15..56.15, z=57.0..58.8, y=1.84)
    worldItems.push_back({{55.65f, 1.84f, 58.25f}, ItemType::Flashlight, false, 0, 0});
    worldItems.push_back({{55.55f, 1.84f, 57.45f}, ItemType::Battery, false, 0, 0});
    worldItems.push_back({{55.78f, 1.84f, 57.60f}, ItemType::Battery, false, 0, 0});

    // 4) Starter Room Items inside North Cabinet (Food on y=2.16, Water on y=1.64)
    worldItems.push_back({{57.78f, 2.16f, 55.55f}, ItemType::Food, false, 0, 0});
    worldItems.push_back({{58.22f, 2.16f, 55.55f}, ItemType::Food, false, 0, 0});
    worldItems.push_back({{57.78f, 1.64f, 55.55f}, ItemType::Water, false, 0, 0});
    worldItems.push_back({{58.22f, 1.64f, 55.55f}, ItemType::Water, false, 0, 0});

    syncLockersFromMap();
    spawnRobotsInLoadedRegions();
}

void GameWorld::syncLockersFromMap() {
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
        doors.push_back(ld);
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

float GameWorld::raycastWorld(const Vec3& origin, const Vec3& dir, float maxDist) const {
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

    float t = 0.0f;
    for (int i = 0; i < 256 && t < maxDist; ++i) {
        if (map.world.getCell(x, y, z).isOpaque()) {
            return t;
        }
        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            t = tMaxX;
            tMaxX += tDeltaX;
            x += stepX;
        } else if (tMaxY < tMaxZ) {
            t = tMaxY;
            tMaxY += tDeltaY;
            y += stepY;
        } else {
            t = tMaxZ;
            tMaxZ += tDeltaZ;
            z += stepZ;
        }
    }
    return maxDist;
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
    return BiomeType::UtilityComplex;
}

const char* GameWorld::currentBiomeName() const {
    return getBiomePalette(currentBiome()).name;
}

void GameWorld::update(const PlayerInput& input, bool firePressed, float dt, AudioEngine& audio) {
    if (gameOver) return;

    if (fireCooldown > 0.0f) fireCooldown -= dt;
    if (damageFlash > 0.0f) damageFlash = std::max(0.0f, damageFlash - dt * 2.5f);

    // Collect closed/partially closed door colliders so player cannot walk through closed doors
    std::vector<AABB> doorColliders;
    for (const auto& d : doors) {
        if (d.angle < 0.85f) {
            doorColliders.push_back(d.worldBounds());
        }
    }

    // Stamina/Energy & Thirst gating for sprint & jump
    PlayerInput modInput = input;
    bool tryingSprint = modInput.sprint && (std::abs(modInput.moveForward) > 0.1f || std::abs(modInput.moveRight) > 0.1f);
    if (thirst <= 1.0f || energy <= 2.0f) {
        modInput.sprint = false;
        tryingSprint = false;
    }
    if (modInput.jump && energy < 8.0f) {
        modInput.jump = false;
    }
    bool didJump = modInput.jump && player.grounded;

    player.update(modInput, map.world, dt, doorColliders);

    // Update Hunger, Thirst, Energy (Stamina) & Flashlight Battery
    hunger = std::max(0.0f, hunger - 0.16f * dt);
    thirst = std::max(0.0f, thirst - (tryingSprint ? 0.42f : 0.24f) * dt);

    if (tryingSprint) {
        energy = std::max(0.0f, energy - 18.0f * dt);
    } else {
        float regenScale = (hunger > 15.0f ? 1.0f : 0.45f) * (thirst > 15.0f ? 1.0f : 0.45f);
        energy = std::min(100.0f, energy + 13.0f * regenScale * dt);
    }
    if (didJump) {
        energy = std::max(0.0f, energy - 10.0f);
    }

    if (hunger <= 0.0f) health -= 1.5f * dt;
    if (thirst <= 0.0f) health -= 2.0f * dt;

    if (flashlightOn) {
        if (!inventory.hasFlashlight || inventory.flashlightBattery <= 0.0f) {
            flashlightOn = false;
        } else {
            inventory.flashlightBattery = std::max(0.0f, inventory.flashlightBattery - 1.15f * dt);
            if (inventory.flashlightBattery <= 0.0f) flashlightOn = false;
        }
    }

    // Update hinged door physics (Amnesia-style inertia, damping & wall collision!)
    for (auto& d : doors) {
        if (std::abs(d.angularVel) < 1e-4f) continue;
        float prevAngle = d.angle;
        float nextAngle = clampf(d.angle + d.angularVel * dt, 0.0f, d.maxAngle);
        d.angle = nextAngle;

        // Check if swinging door leaf collides with any solid WorldGrid wall block
        bool hitWall = false;
        float y = d.currentYaw();
        Vec3 dir{std::cos(y), 0.0f, -std::sin(y)};
        for (float frac : {0.42f, 0.70f, 0.94f}) {
            Vec3 p = d.hingePos + dir * (d.width * frac);
            int ix = static_cast<int>(std::floor(p.x));
            int iz = static_cast<int>(std::floor(p.z));
            int iyLow = static_cast<int>(std::floor(d.hingePos.y + 0.35f));
            int iyHigh = static_cast<int>(std::floor(d.hingePos.y + d.height - 0.25f));
            if (map.world.getCell(ix, iyLow, iz).isSolid() ||
                map.world.getCell(ix, iyHigh, iz).isSolid()) {
                hitWall = true;
                break;
            }
        }

        if (hitWall) {
            d.angle = prevAngle;
            d.angularVel = 0.0f;
        } else {
            if (d.angle <= 0.0f || d.angle >= d.maxAngle) {
                d.angularVel = 0.0f;
            }
            d.angularVel *= std::exp(-6.5f * dt);
        }
    }

    // Update hovered interactable object
    updateHover();
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
        return map.world.getBlock(ix, 1, iz) == BlockType::Air &&
               map.world.getBlock(ix, 2, iz) == BlockType::Air;
    };

    auto isCellClear = [&](int ix, int iz) -> bool {
        if (map.world.getBlock(ix, 1, iz) != BlockType::Air ||
            map.world.getBlock(ix, 2, iz) != BlockType::Air) {
            return false;
        }
        AABB cellBox{{ix + 0.25f, 1.0f, iz + 0.25f}, {ix + 0.75f, 2.5f, iz + 0.75f}};
        for (const auto& pc : map.world.propColliders()) {
            if (cellBox.intersects(pc)) return false;
        }
        return true;
    };

    // 2D Grid BFS pathfinder for Chase mode (avoids getting stuck on corridor walls & props)
    auto findNextStepBFS = [&](const Vec3& fromPos, const Vec3& toPos) -> Vec3 {
        int sx = static_cast<int>(std::floor(fromPos.x));
        int sz = static_cast<int>(std::floor(fromPos.z));
        int gx = static_cast<int>(std::floor(toPos.x));
        int gz = static_cast<int>(std::floor(toPos.z));
        if (sx == gx && sz == gz) return toPos;

        auto pack = [](int x, int z) -> int64_t {
            return (static_cast<int64_t>(x) << 32) | static_cast<uint32_t>(z);
        };

        std::queue<std::pair<int, int>> q;
        std::unordered_map<int64_t, int64_t> parent;
        int64_t startKey = pack(sx, sz);
        parent[startKey] = startKey;
        q.push({sx, sz});

        static constexpr int DX[4] = {1, -1, 0, 0};
        static constexpr int DZ[4] = {0, 0, 1, -1};

        bool found = false;
        int explored = 0;
        while (!q.empty() && explored < 1800) {
            auto [cx, cz] = q.front();
            q.pop();
            explored++;

            if (cx == gx && cz == gz) {
                found = true;
                break;
            }

            for (int d = 0; d < 4; ++d) {
                int nx = cx + DX[d];
                int nz = cz + DZ[d];
                if (std::abs(nx - sx) > 40 || std::abs(nz - sz) > 40) continue;
                int64_t nk = pack(nx, nz);
                if (parent.count(nk)) continue;
                if (!isCellClear(nx, nz) && !(nx == gx && nz == gz)) continue;
                parent[nk] = pack(cx, cz);
                q.push({nx, nz});
            }
        }

        if (!found) return toPos;

        // Backtrack from goal to find the immediate next cell after (sx, sz)
        int64_t cur = pack(gx, gz);
        while (parent.count(cur) && parent[cur] != startKey) {
            cur = parent[cur];
        }
        int nextX = static_cast<int>(cur >> 32);
        int nextZ = static_cast<int>(static_cast<int32_t>(cur & 0xFFFFFFFF));
        return {nextX + 0.5f, 1.0f, nextZ + 0.5f};
    };

    Rng aiRng(static_cast<uint64_t>(player.position.x * 100.0f + player.position.z * 37.0f + enemies.size()));

    for (auto& e : enemies) {
        if (!e.alive) continue;
        if (e.attackCooldown > 0.0f) e.attackCooldown -= dt;
        if (e.hurtFlash > 0.0f) e.hurtFlash -= dt;
        if (e.decisionCooldown > 0.0f) e.decisionCooldown -= dt;

        Vec3 eEye = {e.pos.x, e.pos.y + 2.12f, e.pos.z};
        Vec3 toPlayer = target - eEye;
        float dist = length(toPlayer);

        // Check line-of-sight to player (blocked by walls AND closed doors/lockers!)
        int hiddenIdx = playerHiddenLockerIdx();
        bool canSeePlayer = false;
        if (dist <= 30.0f && dist > 0.1f) {
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
                if (wall >= dist - 0.6f && !blockedByDoor) {
                    canSeePlayer = true;
                }
            }
        }

        // If robot was actively chasing/seeing the player and the player just closed a locker door in front of it,
        // the robot remembers which locker the player entered!
        if (hiddenIdx >= 0 && e.aiState == EnemyAIState::Chase) {
            Vec3 lockerCenter{
                0.5f * (doors[hiddenIdx].lockerInterior.min.x + doors[hiddenIdx].lockerInterior.max.x),
                1.0f,
                0.5f * (doors[hiddenIdx].lockerInterior.min.z + doors[hiddenIdx].lockerInterior.max.z)
            };
            float lDist = length(lockerCenter - eEye);
            if (lDist <= 28.0f && raycastWorld(eEye, normalize(lockerCenter - eEye), lDist) >= lDist - 1.2f) {
                e.sawPlayerEnterLockerIdx = hiddenIdx;
                e.lastSeenPos = lockerCenter;
            }
        }
        if (hiddenIdx < 0 && e.sawPlayerEnterLockerIdx >= 0 && canSeePlayer) {
            e.sawPlayerEnterLockerIdx = -1;
        }

        if (canSeePlayer) {
            if (e.aiState == EnemyAIState::Patrol || e.aiState == EnemyAIState::ReturnToAggro) {
                e.aggroOrigin = e.pos;
                e.alertPulseTimer = 0.0f;
            }
            e.aiState = EnemyAIState::Chase;
            e.alerted = true;
            e.lastSeenPos = player.position;
        } else if (e.aiState == EnemyAIState::Chase) {
            e.aiState = EnemyAIState::InvestigateLastSeen;
            e.pathTimer = 0.0f;
        }

        float soundVol = std::clamp(1.0f - (dist / 28.0f), 0.0f, 1.0f);
        soundVol *= soundVol;
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
                // Constant steady patrol velocity calibrated so a 1.12m wide stride = 1 step per 1.50 seconds!
                // Full 2-step cycle = 3.0s, stance duration = 2.55s -> v = 1.12m / 2.55s = 0.44 m/s
                constexpr float PATROL_SPEED = 0.44f;
                float nx = e.pos.x + e.dirX * PATROL_SPEED * dt;
                float nz = e.pos.z + e.dirZ * PATROL_SPEED * dt;

                int perpX = -e.dirZ, perpZ = e.dirX;
                int ix = static_cast<int>(std::floor(e.pos.x));
                int iz = static_cast<int>(std::floor(e.pos.z));
                int negSpan = 0, posSpan = 0;
                while (negSpan < 3 && isWalkable(ix + 0.5f - perpX * (negSpan + 1), iz + 0.5f - perpZ * (negSpan + 1))) negSpan++;
                while (posSpan < 3 && isWalkable(ix + 0.5f + perpX * (posSpan + 1), iz + 0.5f + perpZ * (posSpan + 1))) posSpan++;
                if (negSpan + posSpan <= 4) {
                    float centerOffset = (posSpan - negSpan) * 0.5f;
                    if (perpX != 0) nx += (ix + 0.5f + centerOffset * perpX - nx) * std::min(1.0f, dt * 4.0f);
                    else            nz += (iz + 0.5f + centerOffset * perpZ - nz) * std::min(1.0f, dt * 4.0f);
                }

                if (isWalkable(nx, nz)) {
                    e.pos.x = nx;
                    e.pos.z = nz;
                }

                int cx = static_cast<int>(std::floor(e.pos.x));
                int cz = static_cast<int>(std::floor(e.pos.z));
                bool blockedAhead = !isWalkable(e.pos.x + e.dirX * 1.1f, e.pos.z + e.dirZ * 1.1f);

                if (e.decisionCooldown <= 0.0f || blockedAhead) {
                    int leftX = -e.dirZ, leftZ = e.dirX;
                    int rightX = e.dirZ, rightZ = -e.dirX;
                    bool openFwd = isWalkable(cx + 0.5f + e.dirX * 1.8f, cz + 0.5f + e.dirZ * 1.8f);
                    bool openLeft = isWalkable(cx + 0.5f + leftX * 2.2f, cz + 0.5f + leftZ * 2.2f);
                    bool openRight = isWalkable(cx + 0.5f + rightX * 2.2f, cz + 0.5f + rightZ * 2.2f);

                    if (openLeft || openRight || blockedAhead) {
                        struct DirOpt { int dx, dz; };
                        std::vector<DirOpt> opts;
                        if (openFwd && !blockedAhead) opts.push_back({e.dirX, e.dirZ});
                        if (openLeft) opts.push_back({leftX, leftZ});
                        if (openRight) opts.push_back({rightX, rightZ});

                        DirOpt chosen{e.dirX, e.dirZ};
                        if (!opts.empty()) chosen = opts[aiRng.rangeInt(0, static_cast<int>(opts.size()) - 1)];
                        else if (blockedAhead) chosen = {-e.dirX, -e.dirZ};

                        if (chosen.dx != e.dirX || chosen.dz != e.dirZ) {
                            e.pendingDirX = chosen.dx;
                            e.pendingDirZ = chosen.dz;
                            e.turnPauseTimer = 1.0f;
                        }
                        e.decisionCooldown = 4.5f;
                    }
                }
            }
        } else if (e.aiState == EnemyAIState::Chase || e.aiState == EnemyAIState::InvestigateLastSeen) {
            float chaseSpeed = 4.4f;
            e.alerted = true;
            e.alertPulseTimer -= dt;
            if (e.alertPulseTimer <= 0.0f) {
                e.alertPulseTimer = 0.62f;
                if (soundVol > 0.02f) audio.play(SoundId::RobotAlertPulse, soundVol * 0.85f);
            }

            Vec3 goalPos = (e.aiState == EnemyAIState::Chase) ? player.position : e.lastSeenPos;

            // If robot saw player enter a locker, walk right up to it and rip the locker door open!
            if (e.aiState == EnemyAIState::InvestigateLastSeen && e.sawPlayerEnterLockerIdx >= 0 &&
                e.sawPlayerEnterLockerIdx < static_cast<int>(doors.size())) {
                auto& ld = doors[e.sawPlayerEnterLockerIdx];
                float dToLocker = length({ld.hingePos.x - e.pos.x, 0.0f, ld.hingePos.z - e.pos.z});
                if (dToLocker < 2.2f) {
                    ld.angularVel = 5.5f;
                    ld.angle = std::max(ld.angle, 0.75f);
                    e.sawPlayerEnterLockerIdx = -1;
                    e.aiState = EnemyAIState::Chase;
                }
            }

            if (e.aiState == EnemyAIState::InvestigateLastSeen &&
                lengthSq({goalPos.x - e.pos.x, 0.0f, goalPos.z - e.pos.z}) < 1.25f * 1.25f) {
                e.sawPlayerEnterLockerIdx = -1;
                e.aiState = EnemyAIState::ReturnToAggro;
                e.alerted = false;
                e.pathTimer = 0.0f;
                e.movePulseTimer = 0.0f;
            } else {
                e.pathTimer -= dt;
                if (e.pathTimer <= 0.0f || !e.hasWaypoint) {
                    e.pathTimer = 0.25f;
                    e.nextWaypoint = findNextStepBFS(e.pos, goalPos);
                    e.hasWaypoint = true;
                }

                Vec3 moveTarget = (e.aiState == EnemyAIState::Chase && canSeePlayer && dist < 3.5f)
                                      ? player.position
                                      : e.nextWaypoint;
                Vec3 horiz = {moveTarget.x - e.pos.x, 0.0f, moveTarget.z - e.pos.z};
                float hDist = length(horiz);
                if (hDist > 0.05f) {
                    Vec3 moveDir = horiz / hDist;
                    e.yaw = std::atan2(moveDir.x, moveDir.z);

                    if (e.aiState != EnemyAIState::Chase || dist > 1.15f) {
                        float nx = e.pos.x + moveDir.x * chaseSpeed * dt;
                        float nz = e.pos.z + moveDir.z * chaseSpeed * dt;
                        if (isWalkable(nx, e.pos.z)) e.pos.x = nx;
                        if (isWalkable(e.pos.x, nz)) e.pos.z = nz;
                    }
                }

                if (e.aiState == EnemyAIState::Chase && dist < 1.45f && e.attackCooldown <= 0.0f) {
                    health -= 15.0f;
                    damageFlash = 0.6f;
                    e.attackCooldown = 1.0f;
                    audio.play(SoundId::PlayerHurt);
                }
            }
        } else {
            // RETURN TO AGGRO POINT
            e.alerted = false;
            float distAggroSq = lengthSq({e.aggroOrigin.x - e.pos.x, 0.0f, e.aggroOrigin.z - e.pos.z});
            if (distAggroSq < 1.3f * 1.3f) {
                e.aiState = EnemyAIState::Patrol;
                e.decisionCooldown = 0.5f;
            } else {
                e.pathTimer -= dt;
                if (e.pathTimer <= 0.0f || !e.hasWaypoint) {
                    e.pathTimer = 0.35f;
                    e.nextWaypoint = findNextStepBFS(e.pos, e.aggroOrigin);
                    e.hasWaypoint = true;
                }
                constexpr float RETURN_SPEED = 0.44f;
                Vec3 horiz = {e.nextWaypoint.x - e.pos.x, 0.0f, e.nextWaypoint.z - e.pos.z};
                float hDist = length(horiz);
                if (hDist > 0.05f) {
                    Vec3 moveDir = horiz / hDist;
                    e.yaw = std::atan2(moveDir.x, moveDir.z);
                    float nx = e.pos.x + moveDir.x * RETURN_SPEED * dt;
                    float nz = e.pos.z + moveDir.z * RETURN_SPEED * dt;
                    if (isWalkable(nx, e.pos.z)) e.pos.x = nx;
                    if (isWalkable(e.pos.x, nz)) e.pos.z = nz;
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
                    if (soundVol > 0.02f) {
                        audio.play(SoundId::RobotStep, soundVol * (e.alerted ? 0.55f : 0.95f));
                    }
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
                    bool shouldStep = (behindDist > triggerBehind) || (lateralDist > 0.35f);
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

        // Spring-damper pelvis bounce & torso lean from real velocity
        e.pelvisVelY += (-150.0f * e.pelvisOffsetY - 14.0f * e.pelvisVelY) * dt;
        e.pelvisOffsetY = clampf(e.pelvisOffsetY + e.pelvisVelY * dt, -0.14f, 0.10f);
        float targetLean = clampf(dot(e.vel, fwdVec) * 0.055f, -0.08f, 0.28f);
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
        pt.vel.y -= 10.0f * dt;
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
        out.push_back({pt.pos, 0.12f, 0.12f, SpriteType::Spark, pt.color});
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
        rs.torsoLeanX = e.torsoLeanX;
        rs.alerted = e.alerted;
        rs.hurt = e.hurtFlash > 0.0f;
        out.push_back(rs);
    }
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
        AABB itemBox{
            {worldItems[i].pos.x - 0.18f, worldItems[i].pos.y - 0.05f, worldItems[i].pos.z - 0.18f},
            {worldItems[i].pos.x + 0.18f, worldItems[i].pos.y + 0.28f, worldItems[i].pos.z + 0.18f}
        };
        float t = 0.0f;
        if (rayIntersectAABB(orig, dir, itemBox, bestT, t)) {
            bestT = t;
            hoveredItemIdx = static_cast<int>(i);
            hoveredDoorIdx = -1;
        }
    }
}

bool GameWorld::beginInteract(AudioEngine& audio) {
    updateHover();
    if (hoveredItemIdx >= 0 && hoveredItemIdx < static_cast<int>(worldItems.size())) {
        auto& it = worldItems[hoveredItemIdx];
        it.pickedUp = true;
        audio.play(SoundId::Pickup, 0.9f);
        switch (it.type) {
            case ItemType::Flashlight:
                inventory.hasFlashlight = true;
                inventory.flashlightBattery = 100.0f;
                break;
            case ItemType::Battery:
                inventory.batteries++;
                break;
            case ItemType::Food:
                inventory.food++;
                break;
            case ItemType::Water:
                inventory.water++;
                break;
        }
        hoveredItemIdx = -1;
        return true;
    }
    if (hoveredDoorIdx >= 0 && hoveredDoorIdx < static_cast<int>(doors.size())) {
        grabbedDoorIdx = hoveredDoorIdx;
        return true;
    }
    return false;
}

void GameWorld::dragGrabbedDoor(float mouseDx, float mouseDy) {
    if (grabbedDoorIdx < 0 || grabbedDoorIdx >= static_cast<int>(doors.size())) return;
    auto& d = doors[grabbedDoorIdx];
    // True 3D camera-relative door drag:
    // Derivative of latch direction (cos(y), 0, -sin(y)) w.r.t. angle is openSign * (-sin(y), 0, -cos(y))
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
    switch (type) {
        case ItemType::Battery:
            if (inventory.batteries > 0 && inventory.hasFlashlight && inventory.flashlightBattery < 99.5f) {
                inventory.batteries--;
                inventory.flashlightBattery = std::min(100.0f, inventory.flashlightBattery + 45.0f);
                audio.play(SoundId::Pickup, 0.7f);
                return true;
            }
            return false;
        case ItemType::Food:
            if (inventory.food > 0 && hunger < 99.5f) {
                inventory.food--;
                hunger = std::min(100.0f, hunger + 35.0f);
                audio.play(SoundId::Pickup, 0.7f);
                return true;
            }
            return false;
        case ItemType::Water:
            if (inventory.water > 0 && thirst < 99.5f) {
                inventory.water--;
                thirst = std::min(100.0f, thirst + 40.0f);
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
        outDoors.push_back({d.hingePos, d.width, d.height, d.thickness, d.currentYaw(), d.isCabinet, hl});
    }

    outItems.clear();
    for (size_t i = 0; i < worldItems.size(); ++i) {
        if (worldItems[i].pickedUp) continue;
        bool hl = (static_cast<int>(i) == hoveredItemIdx);
        outItems.push_back({worldItems[i].pos, static_cast<int>(worldItems[i].type), hl});
    }
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
        pl.color = e.alerted ? Vec3{1.0f, 0.12f, 0.10f} : Vec3{0.25f, 0.80f, 1.0f};
        pl.radius = e.alerted ? 9.5f : 7.0f;
        pl.intensity = e.alerted ? 1.35f : 0.90f;
        out.push_back(pl);
    }
}

} // namespace mega
