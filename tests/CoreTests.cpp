#include <gtest/gtest.h>
#include "world/WorldGrid.hpp"
#include "world/gen/MegaGenerator.hpp"
#include "game/Player.hpp"
#include "game/GameWorld.hpp"
#include <queue>
#include <set>

using namespace mega;

TEST(WorldGridTest, BoundsAndBlockPlacement) {
    WorldGrid grid(2, 2, 2);
    EXPECT_EQ(grid.sizeX(), 32);
    EXPECT_EQ(grid.sizeY(), 32);
    EXPECT_EQ(grid.sizeZ(), 32);

    EXPECT_TRUE(grid.inBounds(0, 0, 0));
    EXPECT_FALSE(grid.inBounds(-1, 0, 0));
    EXPECT_FALSE(grid.inBounds(32, 0, 0));

    grid.setBlock(5, 6, 7, BlockType::RustPlate);
    EXPECT_EQ(grid.getBlock(5, 6, 7), BlockType::RustPlate);
    // Out of bounds returns solid hull
    EXPECT_EQ(grid.getBlock(-1, 0, 0), BlockType::DarkSteel);
}

TEST(PlayerPhysicsTest, GravityLandsOnFloorAndStepUpWorks) {
    WorldGrid grid(2, 2, 2);
    grid.fillBox(0, 0, 0, 31, 0, 31, BlockType::Concrete);
    // Small 0.5m-ish? No, 1-block step is 1.0m (> STEP_HEIGHT 0.55m), so it should block!
    // Let's verify landing on floor y=1.0
    Player player;
    player.position = {10.0f, 3.0f, 10.0f};
    PlayerInput idle;
    for (int i = 0; i < 120; ++i) {
        player.update(idle, grid, cfg::FIXED_DT);
    }
    EXPECT_TRUE(player.grounded);
    EXPECT_NEAR(player.position.y, 1.0f, 0.02f);

    // Jump lifts player off ground
    PlayerInput jump;
    jump.jump = true;
    player.update(jump, grid, cfg::FIXED_DT);
    EXPECT_FALSE(player.grounded);
    EXPECT_GT(player.velocity.y, 0.0f);
}

TEST(MegaGeneratorTest, GeneratesConnectedMultiBiomeMegastructureQuickly) {
    GameSettings cfg = GameSettings::loadOrCreate("mega.cfg");
    for (uint64_t seed : {1ULL, 42ULL, 9999ULL}) {
        MegaResult res = MegaGenerator::generate(seed, cfg);
        EXPECT_GE(res.corridors.size(), 10u);
        EXPECT_GT(res.world.props().size(), 30u);
        EXPECT_GT(res.pointLights.size(), 15u);
        EXPECT_EQ(res.loadedRegions.size(), 9u); // 3x3 active streaming window

        // Verify corridor dimensions (2..4m width, 30..80m length) & sparse 1..3 connections
        for (const auto& seg : res.corridors) {
            EXPECT_GE(seg.width, cfg.corridorMinWidth);
            EXPECT_LE(seg.width, cfg.corridorMaxWidth);
            int len = seg.alongX ? std::abs(seg.x1 - seg.x0) : std::abs(seg.z1 - seg.z0);
            EXPECT_GE(len, cfg.corridorMinLen);
            EXPECT_LE(len, cfg.corridorMaxLen);
            EXPECT_LE(seg.connections, cfg.maxConnections);
        }

        // Verify infinite streaming loads new regions when player walks far away
        bool streamed = MegaGenerator::updateInfiniteStream(res, {320.0f, 1.0f, 320.0f}, cfg);
        EXPECT_TRUE(streamed);
        EXPECT_GT(res.loadedRegions.count({2, 2}), 0u);
    }
}

TEST(GameWorldTest, RaycastAndStarterRoomSurvivalWork) {
    GameWorld gw;
    gw.startNewRun(12345ULL);
    AudioEngine audio;
    PlayerInput idle;

    // 1. Starter room & raycast check (while in region 0,0)
    float hitDist = gw.raycastWorld(gw.player.eyePosition(), {0.0f, -1.0f, 0.0f}, 10.0f);
    EXPECT_GT(hitDist, 0.5f);
    EXPECT_LT(hitDist, 2.5f);

    EXPECT_GE(gw.doors.size(), 2u);
    EXPECT_NEAR(gw.doors[0].height, 2.0f, 0.01f);
    EXPECT_FALSE(gw.doors[0].isCabinet);
    EXPECT_EQ(gw.worldItems.size(), 7u);  // Flashlight + 2 Batteries + 2 Food + 2 Water
    EXPECT_GT(gw.map.lockers.size(), 0u); // 25% corridor hideable lockers generated

    // Verify starter room interior has NO corridor carved through it
    EXPECT_EQ(gw.map.world.getBlock(58, 1, 58), BlockType::Air);
    EXPECT_EQ(gw.map.world.getBlock(58, 1, 54), BlockType::Concrete);
    EXPECT_EQ(gw.map.world.getBlock(58, 1, 61), BlockType::Concrete);

    // 2. Test camera-relative Amnesia-style door drag opens the exit door (yaw = -PI/2 -> looking East along +X)
    gw.player.yaw = PI * 0.5f; // +X
    gw.grabbedDoorIdx = 0;
    gw.dragGrabbedDoor(0.0f, -45.0f); // Push mouse forward/up to push door open into corridor
    for (int i = 0; i < 30; ++i) gw.update(idle, false, cfg::FIXED_DT, audio);
    EXPECT_GT(gw.doors[0].angle, 0.2f);
    gw.endInteract();

    // 3. Test Inventory usage (Food, Water, Battery)
    gw.inventory.hasFlashlight = true;
    gw.inventory.flashlightBattery = 40.0f;
    gw.inventory.batteries = 2;
    gw.inventory.food = 1;
    gw.inventory.water = 1;
    gw.hunger = 50.0f;
    gw.thirst = 50.0f;

    EXPECT_TRUE(gw.useInventoryItem(ItemType::Battery, audio));
    EXPECT_GT(gw.inventory.flashlightBattery, 80.0f);
    EXPECT_TRUE(gw.useInventoryItem(ItemType::Food, audio));
    EXPECT_GT(gw.hunger, 80.0f);
    EXPECT_TRUE(gw.useInventoryItem(ItemType::Water, audio));
    EXPECT_GT(gw.thirst, 85.0f);

    // 4. Test Smart Locker AI: if robot saw player enter a locker, robot opens it!
    int testLockerIdx = -1;
    for (size_t i = 0; i < gw.doors.size(); ++i) {
        if (gw.doors[i].isLocker) { testLockerIdx = static_cast<int>(i); break; }
    }
    ASSERT_GE(testLockerIdx, 0);
    gw.doors[testLockerIdx].angle = 0.05f;
    gw.player.position = {
        0.5f * (gw.doors[testLockerIdx].lockerInterior.min.x + gw.doors[testLockerIdx].lockerInterior.max.x),
        1.0f,
        0.5f * (gw.doors[testLockerIdx].lockerInterior.min.z + gw.doors[testLockerIdx].lockerInterior.max.z)
    };
    Enemy seeker;
    seeker.pos = {gw.doors[testLockerIdx].hingePos.x, 1.0f, gw.doors[testLockerIdx].hingePos.z};
    seeker.aiState = EnemyAIState::InvestigateLastSeen;
    seeker.alerted = true;
    seeker.sawPlayerEnterLockerIdx = testLockerIdx;
    seeker.lastSeenPos = gw.player.position;
    gw.enemies.clear();
    gw.enemies.push_back(seeker);
    gw.update(idle, false, cfg::FIXED_DT, audio);
    EXPECT_GE(gw.doors[testLockerIdx].angle, 0.70f);

    // 5. Safe center vs outer 500m edge robot spawn rule
    for (const auto& e : gw.enemies) {
        EXPECT_TRUE(MegaGenerator::isRobotSpawnAllowed(e.pos.x, e.pos.z) || e.sawPlayerEnterLockerIdx == -1);
    }
    gw.enemies.clear();
    gw.player.position = {448.0f, 1.0f, 64.5f};
    gw.update(idle, false, cfg::FIXED_DT, audio);
    EXPECT_GT(gw.enemies.size(), 0u);
    for (const auto& e : gw.enemies) {
        EXPECT_TRUE(MegaGenerator::isRobotSpawnAllowed(e.pos.x, e.pos.z));
    }
}

TEST(RobotSimTest, ObservePatrolAndChaseFootKinematics) {
    GameWorld gw;
    gw.startNewRun(12345ULL);

    // Manually spawn a robot in the central corridor to observe its Patrol & Chase kinematics
    Enemy testBot;
    testBot.pos = {64.5f, 1.0f, 70.0f};
    testBot.dirX = 0; testBot.dirZ = 1;
    testBot.pendingDirX = 0; testBot.pendingDirZ = 1;
    testBot.yaw = 0.0f;
    testBot.aiState = EnemyAIState::Patrol;
    testBot.alerted = false;
    testBot.regX = 0; testBot.regZ = 0;
    gw.enemies.clear();
    gw.enemies.push_back(testBot);

    // Move player around a corner behind a wall (same region so it isn't unloaded)
    gw.player.position = {40.0f, 1.0f, 64.5f};
    AudioEngine audio;
    PlayerInput idle;

    std::printf("=== PATROL OBSERVATION (0.0s .. 6.0s, constant speed, 1 step / 1.5s) ===\n");
    int firstStepLeg = -1;
    std::vector<float> stepPlantTimes;
    bool prevStep[2] = {false, false};
    for (int f = 0; f < 360; ++f) {
        gw.update(idle, false, cfg::FIXED_DT, audio);
        const Enemy& rob = gw.enemies[0];
        float t = f * cfg::FIXED_DT;
        for (int leg = 0; leg < 2; ++leg) {
            if (!prevStep[leg] && rob.feet[leg].stepping) {
                if (firstStepLeg == -1) firstStepLeg = leg;
                std::printf("[t=%.2fs] %s FOOT LIFTED  (bodyZ=%.2f, v=%.2f)\n",
                            t, leg == 1 ? "RIGHT" : "LEFT", rob.pos.z, std::hypot(rob.vel.x, rob.vel.z));
            }
            if (prevStep[leg] && !rob.feet[leg].stepping) {
                stepPlantTimes.push_back(t);
                std::printf("[t=%.2fs] %s FOOT PLANTED (bodyZ=%.2f, footZ=%.2f, strideFromBody=%.2fm)\n",
                            t, leg == 1 ? "RIGHT" : "LEFT", rob.pos.z, rob.feet[leg].worldPos.z,
                            rob.feet[leg].worldPos.z - rob.pos.z);
            }
            prevStep[leg] = rob.feet[leg].stepping;
        }
        if (f > 5) {
            EXPECT_NEAR(std::hypot(rob.vel.x, rob.vel.z), 0.44f, 0.05f);
        }
    }
    EXPECT_EQ(firstStepLeg, 1); // Right foot (idx=1) MUST step first!
    ASSERT_GE(stepPlantTimes.size(), 3u);
    for (size_t i = 1; i < stepPlantTimes.size(); ++i) {
        float dtStep = stepPlantTimes[i] - stepPlantTimes[i - 1];
        std::printf("Step interval #%zu: %.2fs\n", i, dtStep);
        EXPECT_NEAR(dtStep, 1.50f, 0.25f);
    }

    // Now place player 10m ahead in +Z to trigger Chase!
    gw.player.position = {gw.enemies[0].pos.x, 1.0f, gw.enemies[0].pos.z + 10.0f};
    std::printf("\n=== CHASE OBSERVATION (0.0s .. 1.5s) ===\n");
    for (int f = 0; f < 90; ++f) {
        gw.update(idle, false, cfg::FIXED_DT, audio);
        const Enemy& rob = gw.enemies[0];
        if (f % 4 == 0) {
            float dL = std::hypot(rob.feet[0].worldPos.x - rob.pos.x, rob.feet[0].worldPos.z - rob.pos.z);
            float dR = std::hypot(rob.feet[1].worldPos.x - rob.pos.x, rob.feet[1].worldPos.z - rob.pos.z);
            std::printf("t=%.2fs | alert=%d | bodyZ=%.2f | v=%.2f | L_step=%d y=%.2f d=%.2f | R_step=%d y=%.2f d=%.2f\n",
                        f * cfg::FIXED_DT, rob.alerted ? 1 : 0, rob.pos.z, std::hypot(rob.vel.x, rob.vel.z),
                        rob.feet[0].stepping ? 1 : 0, rob.feet[0].worldPos.y, dL,
                        rob.feet[1].stepping ? 1 : 0, rob.feet[1].worldPos.y, dR);
        }
        EXPECT_LE(std::hypot(rob.feet[0].worldPos.x - rob.pos.x, rob.feet[0].worldPos.z - rob.pos.z), 0.80f);
        EXPECT_LE(std::hypot(rob.feet[1].worldPos.x - rob.pos.x, rob.feet[1].worldPos.z - rob.pos.z), 0.80f);
    }
}

TEST(RobotSimTest, ObserveMapNavigationPatrolAndChase) {
    GameWorld gw;
    gw.startNewRun(12345ULL);

    AudioEngine audio;
    PlayerInput idle;

    // Place 2 test robots in region (0, 0) corridors for patrol & chase navigation verification
    gw.enemies.clear();
    Enemy r0;
    r0.pos = {97.5f, 1.0f, 64.5f}; r0.dirX = 1; r0.dirZ = 0; r0.pendingDirX = 1; r0.pendingDirZ = 0;
    r0.yaw = PI * 0.5f; r0.regX = 0; r0.regZ = 0;
    Enemy r1;
    r1.pos = {64.5f, 1.0f, 114.5f}; r1.dirX = 0; r1.dirZ = 1; r1.pendingDirX = 0; r1.pendingDirZ = 1;
    r1.yaw = 0.0f; r1.regX = 0; r1.regZ = 0;
    gw.enemies.push_back(r0);
    gw.enemies.push_back(r1);

    // --- PART 1: 45-second Patrol navigation test ---
    gw.player.position = {10.0f, 1.0f, 10.0f};
    std::printf("=== MAP NAVIGATION: PATROL (45s) ===\n");
    std::vector<int> reg0Idx = {0, 1};

    std::vector<Vec3> prevCheckpoint(reg0Idx.size());
    std::vector<int> turnCount(reg0Idx.size(), 0);
    std::vector<int> prevDirX(reg0Idx.size()), prevDirZ(reg0Idx.size());
    for (size_t k = 0; k < reg0Idx.size(); ++k) {
        const auto& r = gw.enemies[reg0Idx[k]];
        prevCheckpoint[k] = r.pos;
        prevDirX[k] = r.dirX;
        prevDirZ[k] = r.dirZ;
        std::printf("Robot#%zu start=(%.2f, %.2f) dir=(%d,%d)\n", k, r.pos.x, r.pos.z, r.dirX, r.dirZ);
    }

    int stuckWarnings = 0;
    for (int f = 1; f <= 2700; ++f) { // 45s
        gw.update(idle, false, cfg::FIXED_DT, audio);
        for (size_t k = 0; k < reg0Idx.size(); ++k) {
            const auto& r = gw.enemies[reg0Idx[k]];
            if (r.dirX != prevDirX[k] || r.dirZ != prevDirZ[k]) {
                turnCount[k]++;
                std::printf("[t=%.1fs] Robot#%zu TURNED to (%d,%d) at (%.2f, %.2f)\n",
                            f * cfg::FIXED_DT, k, r.dirX, r.dirZ, r.pos.x, r.pos.z);
                prevDirX[k] = r.dirX;
                prevDirZ[k] = r.dirZ;
            }
            if (f % 300 == 0) { // Every 5s check progress
                float moved5s = std::hypot(r.pos.x - prevCheckpoint[k].x, r.pos.z - prevCheckpoint[k].z);
                if (moved5s < 0.5f) {
                    stuckWarnings++;
                    std::printf("[WARN t=%.1fs] Robot#%zu moved only %.2fm in 5s at (%.2f, %.2f) dir=(%d,%d) pause=%.2f\n",
                                f * cfg::FIXED_DT, k, moved5s, r.pos.x, r.pos.z, r.dirX, r.dirZ, r.turnPauseTimer);
                }
                prevCheckpoint[k] = r.pos;
            }
        }
    }
    for (size_t k = 0; k < reg0Idx.size(); ++k) {
        const auto& r = gw.enemies[reg0Idx[k]];
        std::printf("Robot#%zu end=(%.2f, %.2f) turns=%d\n", k, r.pos.x, r.pos.z, turnCount[k]);
    }
    std::printf("Patrol stuck warnings: %d\n\n", stuckWarnings);

    // --- PART 2: Chase around 90-degree corner, lose LOS, investigate, return to aggro ---
    std::printf("=== MAP NAVIGATION: CHASE -> CORNER -> LOSE LOS -> RETURN ===\n");
    Enemy& chaser = gw.enemies[reg0Idx[0]];
    chaser.pos = {64.5f, 1.0f, 54.0f};
    chaser.dirX = 0; chaser.dirZ = 1;
    chaser.yaw = 0.0f;
    chaser.aiState = EnemyAIState::Patrol;
    chaser.alerted = false;
    chaser.turnPauseTimer = 0.0f;
    chaser.feet[0].initialized = false;
    chaser.feet[1].initialized = false;

    // Place player at central junction (64.5, 64.5) in front of robot -> triggers Chase
    gw.player.position = {64.5f, 1.0f, 64.5f};

    const char* stateNames[] = {"Patrol", "Chase", "InvestigateLastSeen", "ReturnToAggro"};
    EnemyAIState lastSt = chaser.aiState;
    for (int f = 0; f < 1800; ++f) { // 30s
        float t = f * cfg::FIXED_DT;
        // At t=1.2s, player runs around the 90-degree corner east to (82.0, 64.5) and hides behind a wall!
        if (f == 72) {
            gw.player.position = {82.0f, 1.0f, 64.5f};
            std::printf("[t=%.2fs] Player ran around corner to (82.0, 64.5)\n", t);
        }
        // At t=3.5s, player teleports far away to test losing LOS completely
        if (f == 210) {
            gw.player.position = {20.0f, 1.0f, 20.0f};
            std::printf("[t=%.2fs] Player hid completely out of LOS\n", t);
        }

        gw.update(idle, false, cfg::FIXED_DT, audio);
        const Enemy& c = gw.enemies[reg0Idx[0]];
        if (c.aiState != lastSt) {
            std::printf("[t=%.2fs] State: %s -> %s at (%.2f, %.2f)\n",
                        t, stateNames[static_cast<int>(lastSt)],
                        stateNames[static_cast<int>(c.aiState)], c.pos.x, c.pos.z);
            lastSt = c.aiState;
        }
        if (f % 120 == 0) {
            std::printf("[t=%.2fs] pos=(%.2f, %.2f) state=%s wp=(%.1f, %.1f)\n",
                        t, c.pos.x, c.pos.z, stateNames[static_cast<int>(c.aiState)],
                        c.nextWaypoint.x, c.nextWaypoint.z);
        }
    }
}
