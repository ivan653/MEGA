#pragma once

#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Math3D.hpp"
#include "game/Player.hpp"
#include "render/BillboardRenderer.hpp"
#include "render/RobotRenderer.hpp"
#include "world/gen/MegaGenerator.hpp"
#include <cstdint>
#include <vector>

namespace mega {

enum class WeaponType : int {
    Bolter = 0,
    Scattergun = 1
};

enum class EnemyAIState : uint8_t {
    Patrol = 0,
    Chase,
    InvestigateLastSeen,
    ReturnToAggro
};

struct FootState {
    Vec3 worldPos{};
    Vec3 startPos{};
    Vec3 targetPos{};
    bool stepping = false;
    float stepT = 0.0f;
    float stepDuration = 0.35f;
    bool initialized = false;
};

struct Enemy {
    Vec3 pos;
    Vec3 vel;
    SpriteType kind = SpriteType::EnemyStalker;
    EnemyAIState aiState = EnemyAIState::Patrol;
    Vec3 aggroOrigin{};
    Vec3 lastSeenPos{};
    FootState feet[2];
    float pelvisOffsetY = 0.0f;
    float pelvisVelY = 0.0f;
    float armAngle[2] = {0.0f, 0.0f};
    float armVel[2] = {0.0f, 0.0f};
    float torsoLeanX = 0.0f;
    float hp = 60.0f;
    float maxHp = 60.0f;
    float radius = 0.38f;
    float height = 2.5f;
    float yaw = 0.0f;
    int dirX = 0;
    int dirZ = 1;
    int pendingDirX = 0;
    int pendingDirZ = 1;
    float turnPauseTimer = 0.0f;
    float movePulseTimer = 0.0f;
    float alertPulseTimer = 0.0f;
    float decisionCooldown = 0.0f;
    float pathTimer = 0.0f;
    Vec3 nextWaypoint{};
    bool hasWaypoint = false;
    float attackCooldown = 0.0f;
    float hurtFlash = 0.0f;
    int regX = 0;
    int regZ = 0;
    int sawPlayerEnterLockerIdx = -1;
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

enum class ItemType : int {
    Flashlight = 0,
    Battery    = 1,
    Food       = 2,
    Water      = 3
};

struct WorldItem {
    Vec3 pos;
    ItemType type = ItemType::Battery;
    bool pickedUp = false;
    int regX = 0;
    int regZ = 0;
};

struct HingeDoor {
    Vec3 hingePos;
    float width = 1.0f;
    float height = 2.8f;
    float thickness = 0.08f;
    float closedYaw = 0.0f;
    float angle = 0.0f;      // 0..maxAngle
    float maxAngle = 1.85f;  // ~106 degrees
    float angularVel = 0.0f;
    float openSign = 1.0f;
    bool isCabinet = false;
    bool isLocker = false;
    AABB lockerInterior{};
    int regX = 0;
    int regZ = 0;

    float currentYaw() const { return closedYaw + openSign * angle; }
    AABB worldBounds() const;
};

struct Inventory {
    bool hasFlashlight = false;
    float flashlightBattery = 100.0f;
    int batteries = 0;
    int food = 0;
    int water = 0;
};

class GameWorld {
public:
    GameSettings settings;
    MegaResult map;
    Player player;
    Inventory inventory;
    std::vector<Enemy> enemies;
    std::vector<HingeDoor> doors;
    std::vector<WorldItem> worldItems;
    std::vector<Pickup> pickups;
    std::vector<Projectile> projectiles;
    std::vector<Particle> particles;

    int depth = 1;
    uint64_t baseSeed = 0x4D4547410001ULL;
    float health = 100.0f;
    float hunger = 100.0f;
    float thirst = 100.0f;
    float energy = 100.0f; // Stamina for sprint & jump
    float oxygen = 100.0f;
    int ammo = 48;
    int kills = 0;
    WeaponType weapon = WeaponType::Bolter;
    float fireCooldown = 0.0f;
    float damageFlash = 0.0f;
    bool flashlightOn = false;
    bool showMinimap = true;
    bool inventoryOpen = false;
    bool gameOver = false;
    bool worldStreamChanged = false;

    // Amnesia-style interaction state
    int hoveredDoorIdx = -1;
    int hoveredItemIdx = -1;
    int grabbedDoorIdx = -1;

    void startNewRun(uint64_t seed);
    void generateLevel(uint64_t seed);
    void update(const PlayerInput& input, bool firePressed, float dt, AudioEngine& audio);
    void fireWeapon(AudioEngine& audio);

    // Amnesia-style grab & drag interaction
    void updateHover();
    bool beginInteract(AudioEngine& audio);
    void dragGrabbedDoor(float mouseDx, float mouseDy);
    void endInteract();
    bool useInventoryItem(ItemType type, AudioEngine& audio);

    void gatherBillboards(std::vector<BillboardInstance>& out, float timeSec) const;
    void gatherRobots(std::vector<RobotRenderState>& out) const;
    void gatherRobotLights(std::vector<PointLight3D>& out) const;
    void gatherInteractables(std::vector<DoorRenderState>& outDoors,
                             std::vector<ItemRenderState>& outItems) const;
    void spawnRobotsInLoadedRegions();
    void syncLockersFromMap();
    int playerHiddenLockerIdx() const;

    const char* currentBiomeName() const;
    BiomeType currentBiome() const;

    float raycastWorld(const Vec3& origin, const Vec3& dir, float maxDist) const;
};

} // namespace mega
