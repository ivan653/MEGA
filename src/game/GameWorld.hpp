#pragma once

#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/LootTable.hpp"
#include "core/Math3D.hpp"
#include "game/Inventory3D.hpp"
#include "game/LockpickMinigame.hpp"
#include "game/Player.hpp"
#include "render/BillboardRenderer.hpp"
#include "render/RobotRenderer.hpp"
#include "world/gen/MegaGenerator.hpp"
#include <cstdint>
#include <string>
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
    bool handIkActive[2] = {false, false};
    Vec3 handWorldPos[2]{};
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
    int openingDoorIdx = -1;
    float doorInteractTimer = 0.0f;
    // Cooperative robot-vs-robot passing & obstacle bypass state
    int yieldPartnerIdx = -1;
    float yieldLateralDirX = 0.0f;
    float yieldLateralDirZ = 0.0f;
    float yieldTimer = 0.0f;
    float lateralOffset = 0.0f;
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
    float size = 0.12f;
    float gravity = 10.0f;
    SpriteType sprite = SpriteType::Spark;
};

struct WorldItem {
    Vec3 pos; // World bottom-center on 3D grid
    ItemType type = ItemType::Battery;
    bool pickedUp = false;
    int regX = 0;
    int regZ = 0;
    ItemOrientation orient{};
    int durability = 0; // 1..3 hits for Broom/Mop before head breaks into Stick

    AABB worldBounds() const;
};

struct CraftOption {
    int recipeId = 0;
    ItemType result = ItemType::Spear;
    std::string label;
    std::vector<int> ingredientWorldIdx;
    Vec3 outputPos{};
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
    float creakAccum = 0.0f; // Physical friction travel accumulator for creak synthesis
    bool isCabinet = false;
    bool isRareCabinet = false;
    bool isLocker = false;
    bool isExitDoor = false; // Starter 2m heavy steel exit door (0% lockpick break chance)
    bool isHatch = false;    // Floor hatch swinging upward around horizontal hinge axis
    bool isSewerDoor = false;// Heavy metal door separating parallel sewer tunnels
    bool locked = false;
    uint64_t lockSeed = 0;
    int linkedDoorIdx = -1;  // Paired leaf index for double-door cabinets sharing 1 lock
    AABB lockerInterior{};
    int regX = 0;
    int regZ = 0;

    float currentYaw() const { return closedYaw + openSign * angle; }
    AABB worldBounds() const;
};

enum class BackpackMode : uint8_t {
    OnBack = 0,
    InHands = 1,   // Tilted in front of player's hands so you can view & pick items inside
    OnGround = 2   // Placed upright on floor/table for easy 3D sorting
};

struct Inventory {
    bool hasFlashlight = false;
    float flashlightBattery = 100.0f;
    int batteries = 0;
    int food = 0;
    int water = 0;
    int lockpicks = 0;
};

class GameWorld {
public:
    GameSettings settings;
    LootTables lootTables;
    MegaResult map;
    Player player;
    Inventory inventory;
    Backpack3D backpack;
    LockpickMinigame lockpick;
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
    float energy = 100.0f;
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

    // 3D Diegetic Backpack & Hand / Dragging / Melee / Surface Crafting state
    BackpackMode backpackMode = BackpackMode::OnBack;
    Vec3 backpackGroundPos{};
    float backpackGroundYaw = 0.0f;
    float backpackViewYaw = 0.0f;     // RMB-hold rotation around backpack vertical axis
    float backpackViewPitch = 0.0f;   // RMB-hold tilt around backpack horizontal axis
    bool hasHeldItem = false;
    bool heldPlaceMode = false; // True ONLY after pressing RMB with an item in hand to preview/place on 3D grid
    ItemType heldItemType = ItemType::Food;
    ItemOrientation heldItemOrient{};
    int heldItemDurability = 0;
    float meleeSwingTimer = 0.0f;
    float meleeCooldown = 0.0f;
    // Procedural realistic consumption state (Hold LMB with Water or Food in hand)
    bool consumeLmbHeld = false;
    float consumeOpenProgress = 0.0f;  // 0..1 cap unscrewed / wrapper peeled back
    float consumeCapSpin = 0.0f;       // radians of bottle cap twist
    float consumeTilt = 0.0f;          // bottle tilt angle (Water) or mouth lift progress (Food)
    float consumeRemaining = 1.0f;     // 0..1 remaining water/bar while held
    float consumeBiteTimer = 0.0f;     // time until next discrete bite (Food)
    float consumeBiteImpulse = 0.0f;   // 0..1 bite chew feedback pulse
    float consumePendingRestore = 0.0f;// smooth gradual hunger recovery spread across chew cycle
    float consumeDropletTimer = 0.0f;  // water droplet particle spawn cadence
    float consumeSoundTimer = 0.0f;    // gulp/sip audio cadence
    bool consumePouring = false;       // true while water stream exits bottle neck
    bool consumePlayedOpenSfx = false;
    bool draggingItem = false;        // True when RMB-dragging an item with paused camera + free mouse cursor
    ItemType dragItemType = ItemType::Food;
    ItemOrientation dragItemOrient{};
    int dragItemDurability = 0;
    int dragOriginBackpackId = -1;
    int hoveredBackpackItemId = -1;
    bool hoveredBackpackBlocked = false;
    bool hoveredGroundedBackpack = false;
    GhostPreviewState placementPreview{};
    std::vector<CraftOption> availableCrafts;

    // God Mode debug/testing controls
    bool godMode = false;
    bool godNoclip = false;
    bool godInvisible = false;
    float godSpeedMult = 1.0f;
    float godTimeScale = 1.0f;

    // Amnesia-style interaction state
    int hoveredDoorIdx = -1;
    int hoveredItemIdx = -1;
    int grabbedDoorIdx = -1;

    // Player footstep & HUD feedback state
    float playerStepDist = 0.0f;
    bool playerWasGrounded = true;
    float prevPlayerVelY = 0.0f;
    float hudNoticeTimer = 0.0f;
    std::string hudNoticeText;

    void startNewRun(uint64_t seed);
    void generateLevel(uint64_t seed);
    void update(const PlayerInput& input, bool firePressed, float dt, AudioEngine& audio);
    void fireWeapon(AudioEngine& audio);

    // 3D Backpack & World-Grid Placement API
    void syncInventoryCounts();
    Mat4 backpackWorldMatrix() const;
    Mat4 heldItemWorldMatrix() const;
    bool isCameraPausedForInventory() const {
        return backpackMode == BackpackMode::InHands || draggingItem || lockpick.active;
    }
    void toggleBackpackInHands();
    bool togglePlaceBackpackOnGround(AudioEngine& audio);
    void updateCursorHover(const Vec3& rayOrig, const Vec3& rayDir);
    bool handlePrimaryClick(const Vec3& rayOrig, const Vec3& rayDir, AudioEngine& audio);
    bool handleSecondaryClick(const Vec3& rayOrig, const Vec3& rayDir, AudioEngine& audio);
    void rotateActiveItem(int axis); // 0=X, 1=Y, 2=Z
    void rotateBackpackView(float dx, float dy);
    bool useHeldItem(AudioEngine& audio);
    void resetHeldConsumableState(bool initializeFromDurability = false);
    void updateHeldConsumption(float dt, AudioEngine& audio);
    ConsumableVisualState gatherHeldConsumableState() const;
    bool performMeleeAttack(AudioEngine& audio);
    void updateSurfaceCrafting();
    bool performCraft(int optionIdx, AudioEngine& audio);
    bool computeWorldGridSnap(const Vec3& rayOrig, const Vec3& rayDir,
                              ItemType type, const ItemOrientation& orient,
                              Vec3& outSnappedPos) const;

    // Amnesia-style grab & drag interaction
    void updateHover();
    bool beginInteract(AudioEngine& audio, bool quickToggle = false);
    void dragGrabbedDoor(float mouseDx, float mouseDy);
    void endInteract();
    bool useInventoryItem(ItemType type, AudioEngine& audio);

    void gatherBillboards(std::vector<BillboardInstance>& out, float timeSec) const;
    void gatherRobots(std::vector<RobotRenderState>& out) const;
    void gatherRobotLights(std::vector<PointLight3D>& out) const;
    void gatherInteractables(std::vector<DoorRenderState>& outDoors,
                             std::vector<ItemRenderState>& outItems) const;
    BackpackRenderState gatherBackpackState() const;
    void spawnRobotsInLoadedRegions();
    void syncLockersFromMap();
    int playerHiddenLockerIdx() const;

    const char* currentBiomeName() const;
    BiomeType currentBiome() const;

    float raycastWorld(const Vec3& origin, const Vec3& dir, float maxDist,
                       Vec3* outNormal = nullptr) const;
};

} // namespace mega
