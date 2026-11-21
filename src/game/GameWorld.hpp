#pragma once

#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Math3D.hpp"
#include "game/Player.hpp"
#include "render/BillboardRenderer.hpp"
#include "world/gen/MegaGenerator.hpp"
#include <cstdint>
#include <vector>

namespace mega {

enum class WeaponType : int {
    Bolter = 0,
    Scattergun = 1
};

struct Enemy {
    Vec3 pos;
    Vec3 vel;
    SpriteType kind = SpriteType::EnemyStalker;
    float hp = 40.0f;
    float maxHp = 40.0f;
    float radius = 0.35f;
    float height = 1.6f;
    float attackCooldown = 0.0f;
    float hurtFlash = 0.0f;
    bool alerted = false;
    bool alive = true;
};

struct Pickup {
    Vec3 pos;
    SpriteType kind = SpriteType::PickupMedkit;
    bool collected = false;
};

struct Projectile {
    Vec3 pos;
    Vec3 vel;
    float life = 3.0f;
    float damage = 12.0f;
    bool fromPlayer = false;
};

struct Particle {
    Vec3 pos;
    Vec3 vel;
    float life = 0.3f;
    Vec3 color{1.0f, 0.8f, 0.3f};
};

class GameWorld {
public:
    GameSettings settings;
    MegaResult map;
    Player player;
    std::vector<Enemy> enemies;
    std::vector<Pickup> pickups;
    std::vector<Projectile> projectiles;
    std::vector<Particle> particles;

    int depth = 1;
    uint64_t baseSeed = 0x4D4547410001ULL;
    float health = 100.0f;
    float oxygen = 100.0f;
    float energy = 100.0f;
    int ammo = 48;
    int kills = 0;
    WeaponType weapon = WeaponType::Bolter;
    float fireCooldown = 0.0f;
    float damageFlash = 0.0f;
    bool flashlightOn = true;
    bool showMinimap = true;
    bool gameOver = false;
    bool worldStreamChanged = false;

    void startNewRun(uint64_t seed);
    void generateLevel(uint64_t seed);
    void update(const PlayerInput& input, bool firePressed, float dt, AudioEngine& audio);
    void fireWeapon(AudioEngine& audio);
    void gatherBillboards(std::vector<BillboardInstance>& out, float timeSec) const;

    const char* currentBiomeName() const;
    BiomeType currentBiome() const;

    float raycastWorld(const Vec3& origin, const Vec3& dir, float maxDist) const;
};

} // namespace mega
