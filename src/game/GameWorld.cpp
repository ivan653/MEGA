#include "game/GameWorld.hpp"
#include "core/Rng.hpp"
#include <algorithm>
#include <cmath>

namespace mega {

void GameWorld::startNewRun(uint64_t seed) {
    baseSeed = seed;
    depth = 1;
    health = 100.0f;
    oxygen = 100.0f;
    energy = 100.0f;
    ammo = 48;
    kills = 0;
    weapon = WeaponType::Bolter;
    gameOver = false;
    damageFlash = 0.0f;
    generateLevel(seed);
}

void GameWorld::generateLevel(uint64_t seed) {
    map = MegaGenerator::generate(seed, settings);
    player.position = map.spawnPos;
    player.velocity = {0.0f, 0.0f, 0.0f};
    player.yaw = 0.0f;
    player.pitch = 0.0f;
    worldStreamChanged = true;

    enemies.clear();
    pickups.clear();
    projectiles.clear();
    particles.clear();
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

    player.update(input, map.world, dt);

    // Infinite map streaming check as player moves across regions
    int rx = WorldGrid::floorDiv(static_cast<int>(std::floor(player.position.x)), cfg::REGION_SIZE);
    int rz = WorldGrid::floorDiv(static_cast<int>(std::floor(player.position.z)), cfg::REGION_SIZE);
    if (rx != map.currentRegX || rz != map.currentRegZ) {
        if (MegaGenerator::updateInfiniteStream(map, player.position, settings)) {
            worldStreamChanged = true;
        }
    }

    if (firePressed) {
        fireWeapon(audio);
    }

    // Survival resource drain
    BiomeType b = currentBiome();
    float o2Drain = 0.55f; // Baseline atmospheric leak
    if (b == BiomeType::CorrosionSewers || b == BiomeType::BioMechanicalRift) {
        o2Drain = 1.65f; // Hazardous atmosphere
    }
    oxygen = std::max(0.0f, oxygen - o2Drain * dt);
    if (oxygen <= 0.0f) {
        health -= 6.0f * dt;
    }

    if (flashlightOn) {
        energy = std::max(0.0f, energy - 1.8f * dt);
        if (energy <= 0.0f) flashlightOn = false;
    } else {
        energy = std::min(100.0f, energy + 2.5f * dt);
    }

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

    // Enemies AI
    Vec3 target = player.eyePosition();
    for (auto& e : enemies) {
        if (!e.alive) continue;
        if (e.attackCooldown > 0.0f) e.attackCooldown -= dt;
        if (e.hurtFlash > 0.0f) e.hurtFlash -= dt;

        Vec3 eEye = {e.pos.x, e.pos.y + e.height * 0.7f, e.pos.z};
        Vec3 toPlayer = target - eEye;
        float dist = length(toPlayer);

        if (dist < 22.0f) {
            Vec3 dir = toPlayer / std::max(dist, 1e-4f);
            float wall = raycastWorld(eEye, dir, dist);
            if (wall >= dist - 0.5f) {
                e.alerted = true;
            }
        }

        if (e.alerted && dist > 0.1f) {
            Vec3 horiz = {target.x - e.pos.x, 0.0f, target.z - e.pos.z};
            float hDist = length(horiz);
            if (hDist > 1.2f) {
                Vec3 moveDir = horiz / hDist;
                float spd = (e.kind == SpriteType::EnemyStalker) ? 4.2f :
                            (e.kind == SpriteType::EnemyDrone ? 3.1f : 2.3f);
                Vec3 nextPos = e.pos + moveDir * (spd * dt);
                int nx = static_cast<int>(std::floor(nextPos.x));
                int ny = static_cast<int>(std::floor(nextPos.y + 0.3f));
                int nz = static_cast<int>(std::floor(nextPos.z));
                if (map.world.getBlock(nx, ny, nz) == BlockType::Air) {
                    e.pos.x = nextPos.x;
                    e.pos.z = nextPos.z;
                }
            }

            // Attack
            if (e.kind == SpriteType::EnemyStalker && dist < 1.6f && e.attackCooldown <= 0.0f) {
                health -= 12.0f;
                damageFlash = 0.6f;
                e.attackCooldown = 1.0f;
                audio.play(SoundId::PlayerHurt);
            } else if ((e.kind == SpriteType::EnemyDrone || e.kind == SpriteType::EnemyHeavy) &&
                       dist < 20.0f && e.attackCooldown <= 0.0f) {
                Vec3 dir = normalize(target - eEye);
                projectiles.push_back({eEye, dir * 13.0f, 3.0f,
                                       (e.kind == SpriteType::EnemyHeavy) ? 16.0f : 9.0f, false});
                e.attackCooldown = (e.kind == SpriteType::EnemyHeavy) ? 1.5f : 1.9f;
            }
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

} // namespace mega
