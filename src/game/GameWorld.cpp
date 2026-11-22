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
    health = settings.maxHealth;
    hunger = settings.maxHunger;
    thirst = settings.maxThirst;
    energy = settings.maxEnergy;
    oxygen = 100.0f;
    ammo = 48;
    kills = 0;
    inventory = Inventory{};
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
    worldItems.push_back({{55.65f, 1.84f, 58.38f}, ItemType::Flashlight, false, 0, 0});
    worldItems.push_back({{55.55f, 1.84f, 57.35f}, ItemType::Battery, false, 0, 0});
    worldItems.push_back({{55.78f, 1.84f, 57.50f}, ItemType::Battery, false, 0, 0});
    worldItems.push_back({{55.56f, 1.84f, 57.82f}, ItemType::Lockpick, false, 0, 0});
    worldItems.push_back({{55.76f, 1.84f, 57.94f}, ItemType::Lockpick, false, 0, 0});
    worldItems.push_back({{55.58f, 1.84f, 58.08f}, ItemType::Lockpick, false, 0, 0});

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

        // ~45% of corridor lockers spawn locked
        uint64_t lSeed = map.seed ^
                         (static_cast<uint64_t>(std::llround(lp.hingePos.x * 100.0f)) * 73856093ULL) ^
                         (static_cast<uint64_t>(std::llround(lp.hingePos.z * 100.0f)) * 83492791ULL);
        Rng lRng(lSeed);
        ld.locked = lRng.chance(0.45f);
        ld.lockSeed = lSeed;

        doors.push_back(ld);

        // ~35% chance to find a spare lockpick inside a corridor locker
        if (lRng.chance(0.35f)) {
            Vec3 itemPos{
                0.5f * (lp.interiorBox.min.x + lp.interiorBox.max.x),
                lp.interiorBox.min.y + 0.04f,
                0.5f * (lp.interiorBox.min.z + lp.interiorBox.max.z)
            };
            worldItems.push_back({itemPos, ItemType::Lockpick, false, lp.regX, lp.regZ});
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

    Vec3 prevPlayerPos = player.position;
    prevPlayerVelY = player.velocity.y;
    player.update(modInput, map.world, dt, playerDoorColliders,
                  settings.moveSpeed * godSpeedMult, settings.sprintMult, settings.jumpSpeed);

    // Realistic player footsteps on floor & landing (muted while flying in Noclip)
    if (player.grounded && !modInput.noclip) {
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

        // Check if swinging door leaf collides with any solid WorldGrid wall block
        bool hitWall = false;
        float y = d.currentYaw();
        Vec3 dir{std::cos(y), 0.0f, -std::sin(y)};
        Vec3 impactPos = d.hingePos + dir * (d.width * 0.85f) + Vec3{0.0f, d.height * 0.5f, 0.0f};
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
            inventory.lockpicks = std::max(0, inventory.lockpicks - 1);
            if (inventory.lockpicks <= 0) {
                hudNoticeText = "OUT OF LOCKPICKS";
                hudNoticeTimer = 2.2f;
                lockpick.close();
            } else {
                hudNoticeText = "LOCKPICK SNAPPED";
                hudNoticeTimer = 1.6f;
            }
        } else if (lockpick.unlockedJustNow) {
            if (lockpick.targetDoorIdx >= 0 && lockpick.targetDoorIdx < static_cast<int>(doors.size())) {
                doors[lockpick.targetDoorIdx].locked = false; // Stays unlocked permanently
                doors[lockpick.targetDoorIdx].angularVel = 2.4f;
            }
            lockpick.unlockedJustNow = false;
            lockpick.close();
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

    Rng aiRng(static_cast<uint64_t>(player.position.x * 100.0f + player.position.z * 37.0f + enemies.size()));

    for (size_t eIdx = 0; eIdx < enemies.size(); ++eIdx) {
        auto& e = enemies[eIdx];
        if (!e.alive) continue;
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
        int hiddenIdx = playerHiddenLockerIdx();
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
                if (wall >= dist - 0.6f && !blockedByDoor) {
                    canSeePlayer = true;
                }
            }
        }

        // If God Mode invisibility was toggled on during a chase, drop aggro immediately
        if (godInvisible && (e.aiState == EnemyAIState::Chase || e.aiState == EnemyAIState::InvestigateLastSeen)) {
            e.aiState = EnemyAIState::ReturnToAggro;
            e.alerted = false;
            e.sawPlayerEnterLockerIdx = -1;
            e.pathTimer = 0.0f;
        }

        // If robot was actively chasing/seeing the player and the player just closed a locker door in front of it,
        // the robot remembers which locker the player entered!
        if (!godInvisible && hiddenIdx >= 0 && e.aiState == EnemyAIState::Chase) {
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
                constexpr float PATROL_SPEED = 0.44f;
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
            float chaseSpeed = 4.4f;
            e.alerted = true;
            e.alertPulseTimer -= dt;
            if (e.alertPulseTimer <= 0.0f) {
                e.alertPulseTimer = 0.62f;
                audio.play3D(SoundId::RobotAlertPulse, eEye, 0.95f);
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
                constexpr float RETURN_SPEED = 0.44f;
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
        audio.play3D(SoundId::Pickup, it.pos, 0.9f);
        switch (it.type) {
            case ItemType::Flashlight:
                inventory.hasFlashlight = true;
                inventory.flashlightBattery = settings.maxBattery;
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
            case ItemType::Lockpick:
                inventory.lockpicks++;
                break;
        }
        hoveredItemIdx = -1;
        return true;
    }
    if (hoveredDoorIdx >= 0 && hoveredDoorIdx < static_cast<int>(doors.size())) {
        const auto& d = doors[hoveredDoorIdx];
        Vec3 lockPos = d.hingePos + Vec3{0.0f, d.height * 0.5f, 0.0f};
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
            if (inventory.batteries > 0 && inventory.hasFlashlight &&
                inventory.flashlightBattery < settings.maxBattery - 0.5f) {
                inventory.batteries--;
                inventory.flashlightBattery = std::min(settings.maxBattery,
                                                       inventory.flashlightBattery + settings.batteryRestore);
                audio.play(SoundId::Pickup, 0.7f);
                return true;
            }
            return false;
        case ItemType::Food:
            if (inventory.food > 0 && hunger < settings.maxHunger - 0.5f) {
                inventory.food--;
                hunger = std::min(settings.maxHunger, hunger + settings.foodRestore);
                audio.play(SoundId::Pickup, 0.7f);
                return true;
            }
            return false;
        case ItemType::Water:
            if (inventory.water > 0 && thirst < settings.maxThirst - 0.5f) {
                inventory.water--;
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
        pl.color = e.alerted
                       ? Vec3{settings.robotAlertLedColorR, settings.robotAlertLedColorG, settings.robotAlertLedColorB}
                       : Vec3{settings.robotLedColorR, settings.robotLedColorG, settings.robotLedColorB};
        pl.radius = e.alerted ? settings.robotAlertLedRadius : settings.robotLedRadius;
        pl.intensity = e.alerted ? settings.robotAlertLedIntensity : settings.robotLedIntensity;
        out.push_back(pl);
    }
}

} // namespace mega
