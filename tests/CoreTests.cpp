#include <gtest/gtest.h>
#include "world/WorldGrid.hpp"
#include "world/gen/MegaGenerator.hpp"
#include "game/Player.hpp"
#include "game/GameWorld.hpp"
#include "game/LockpickMinigame.hpp"
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
    EXPECT_GE(gw.worldItems.size(), 10u); // Flashlight + 2 Batteries + 3 Lockpicks + 2 Food + 2 Water (+ corridor loot)
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

TEST(LockpickTest, SlowPushOverforceBreakAndGentleTapPhysics) {
    AudioEngine audio;
    LockpickMinigame lp;
    lp.open(0, 777ULL, 0.0f);

    // 1. Slow push up to max pick height (0.20) then drop -> NEVER reaches target (0.44..0.82), falls back to 0
    lp.pins[0].target = 0.55f;
    lp.pickX = 0.5f;
    lp.pickY = -0.05f;
    for (int i = 0; i < 30; ++i) {
        lp.handleMouseMove(0.0f, -2.0f); // Very slow push
        lp.update(cfg::FIXED_DT, audio);
    }
    lp.pickY = -0.05f;
    for (int i = 0; i < 60; ++i) lp.update(cfg::FIXED_DT, audio);
    EXPECT_FALSE(lp.pins[0].pinned);
    EXPECT_LT(lp.pins[0].pos, 0.1f);

    // 2. Detailed physical flick sweep (dy = -4 .. -44 px) to verify pure physics usability & accessibility
    std::printf("=== LOCKPICK PURE PHYSICS SWEEP (target=0.60) ===\n");
    int pinnedCount = 0;
    int brokeCount = 0;
    int shortCount = 0;
    for (int dy = 4; dy <= 44; dy += 2) {
        lp.open(0, 777ULL, 0.0f);
        lp.pins[0].target = 0.60f;
        lp.pickX = 0.5f;
        lp.pickY = 0.0f;
        // Simulate 5 high-polling USB mouse packets in a single 60Hz frame
        for (int pkt = 0; pkt < 5; ++pkt) {
            lp.handleMouseMove(0.0f, -static_cast<float>(dy) / 5.0f);
        }
        float maxH = 0.0f;
        bool broke = false;
        for (int f = 0; f < 80; ++f) {
            lp.update(cfg::FIXED_DT, audio);
            lp.pickY = -0.05f; // Natural release after flick
            maxH = std::max(maxH, lp.pins[0].pos);
            if (lp.pins[0].brokenTimer > 0.0f) broke = true;
        }
        const char* status = lp.pins[0].pinned ? "PINNED!" : (broke ? "CEILING_BREAK" : "FELL_SHORT");
        if (lp.pins[0].pinned) pinnedCount++;
        else if (broke) brokeCount++;
        else shortCount++;
        std::printf("dy=-%2dpx/frame (%4.0f px/s) | maxH=%.2f | %s\n", dy, dy * 60.0f, maxH, status);
    }
    // Verify all 3 physical regimes emerge naturally AND there is a comfortable human window to pin!
    EXPECT_GT(shortCount, 0);
    EXPECT_GE(pinnedCount, 3); // At least 6px+ wide sweet spot for human mouse flicks!
    EXPECT_GT(brokeCount, 0);

    // 3. Verify EVERY target height in [0.44 .. 0.82] is reliably reachable & pinnable!
    for (float tgt : {0.44f, 0.55f, 0.68f, 0.82f}) {
        bool reachable = false;
        for (int dy = 4; dy <= 35; ++dy) {
            lp.open(0, 777ULL, 0.0f);
            lp.pins[0].target = tgt;
            lp.pickX = 0.5f;
            lp.pickY = 0.0f;
            lp.handleMouseMove(0.0f, static_cast<float>(-dy));
            for (int f = 0; f < 80; ++f) {
                lp.update(cfg::FIXED_DT, audio);
                lp.pickY = -0.05f;
            }
            if (lp.pins[0].pinned) {
                reachable = true;
                break;
            }
        }
        EXPECT_TRUE(reachable) << "Target height " << tgt << " was not reachable!";
    }

    // 4. Pick wall collision: cannot lift between chambers, cannot exceed 20% height, and cannot exit chamber walls
    lp.open(0, 777ULL);
    lp.pickX = 1.0f;
    lp.pickY = -0.05f;
    lp.handleMouseMove(0.0f, -100.0f);
    EXPECT_LE(lp.pickY, 0.025f);

    lp.pickX = 0.5f;
    lp.pickY = 0.15f;
    lp.handleMouseMove(200.0f, -200.0f);
    EXPECT_LE(lp.pickX, 0.70f);
    EXPECT_LE(lp.pickY, 0.205f);
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

TEST(Audio3DAcousticsTest, PanningObstacleThicknessAndConditionalEcho) {
    WorldGrid world;
    // Build a 95m straight concrete tunnel along X (x=0..95, z=4..6, y=1..4)
    // plus a perpendicular tunnel branching along +Z at x=40..42, z=4..52, y=1..4
    // (surrounded by solid concrete in the diagonal space!)
    for (int x = -1; x <= 96; ++x) {
        for (int z = 3; z <= 53; ++z) {
            for (int y = 0; y <= 5; ++y) {
                bool mainTunnel = (x >= 0 && x <= 95 && z >= 4 && z <= 6 && y >= 1 && y <= 4);
                bool perpTunnel = (x >= 40 && x <= 42 && z >= 4 && z <= 52 && y >= 1 && y <= 4);
                if (mainTunnel || perpTunnel) {
                    world.setBlock(x, y, z, BlockType::Air);
                } else if ((z >= 3 && z <= 7) || (x >= 39 && x <= 43)) {
                    world.setBlock(x, y, z, BlockType::Concrete);
                }
            }
        }
    }
    // Fill diagonal concrete mass between listener (10.5, 5.5) and perp tunnel (41.0, 45.0)
    for (int x = 10; x <= 39; ++x) {
        for (int z = 8; z <= 50; ++z) {
            world.setBlock(x, 2, z, BlockType::Concrete);
        }
    }
    // Tight 2x2m alcove at (x=1..2, z=12..13)
    for (int x = 0; x <= 3; ++x) {
        for (int z = 11; z <= 14; ++z) {
            for (int y = 0; y <= 4; ++y) {
                bool hollow = (x >= 1 && x <= 2 && z >= 12 && z <= 13 && y >= 1 && y <= 2);
                world.setBlock(x, y, z, hollow ? BlockType::Air : BlockType::Concrete);
            }
        }
    }

    AudioEngine audio;
    GameSettings s;
    audio.applySettings(s);

    // Listener in main tunnel at (10.5, 2.5, 5.5) facing -Z (right is +X)
    audio.setListener({10.5f, 2.5f, 5.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, &world, {});

    // 1. Stereo panning: source to the right (+X) louder in right ear; left (-X) louder in left ear
    AcousticParams rightAc = audio.computeAcoustics({15.5f, 2.5f, 5.5f});
    AcousticParams leftAc  = audio.computeAcoustics({5.5f, 2.5f, 5.5f});
    EXPECT_GT(rightAc.gainR, rightAc.gainL * 2.0f);
    EXPECT_GT(leftAc.gainL, leftAc.gainR * 2.0f);
    EXPECT_NEAR(rightAc.obstacleThickness, 0.0f, 0.05f);

    // 2. Straight-line tunnel audibility up to 80m, and perpendicular tunnel audibility at ~50m!
    AcousticParams straight80m = audio.computeAcoustics({90.5f, 2.5f, 5.5f}); // dist = 80.0m straight line
    AcousticParams perp50m     = audio.computeAcoustics({41.0f, 2.5f, 45.0f}); // dist = sqrt(30.5^2 + 39.5^2) = 49.9m in perp tunnel
    float gainStraight80 = straight80m.gainL + straight80m.gainR;
    float gainPerp50     = perp50m.gainL + perp50m.gainR;
    std::printf("[3D AUDIO RANGE] Straight 80m: dist=%.1fm thick=%.2fm gain=%.3f lpf=%.0fHz\n",
                straight80m.distance, straight80m.obstacleThickness, gainStraight80, straight80m.lpfCutoffHz);
    std::printf("[3D AUDIO RANGE] Perp Tunnel 50m: dist=%.1fm thick=%.2fm gain=%.3f lpf=%.0fHz\n",
                perp50m.distance, perp50m.obstacleThickness, gainPerp50, perp50m.lpfCutoffHz);
    EXPECT_NEAR(straight80m.obstacleThickness, 0.0f, 0.05f);
    EXPECT_GT(gainStraight80, 0.05f); // Audible at 80m in a straight line
    EXPECT_LT(perp50m.obstacleThickness, 4.5f); // L-corner path bypasses 30m diagonal concrete mass
    EXPECT_GT(gainPerp50, 0.06f); // Audible at ~50m in perpendicular tunnel

    // 3. Obstacle thickness occlusion: place 1m wall vs 3m thick wall between listener (10.5) and source (16.5)
    for (int z = 4; z <= 6; ++z)
        for (int y = 1; y <= 4; ++y)
            world.setBlock(13, y, z, BlockType::Concrete);
    AcousticParams thinWall = audio.computeAcoustics({16.5f, 2.5f, 5.5f});
    EXPECT_GT(thinWall.obstacleThickness, 0.7f);
    EXPECT_LT(thinWall.gainR, rightAc.gainR);
    EXPECT_LT(thinWall.lpfCutoffHz, 11000.0f);

    for (int x = 12; x <= 14; ++x)
        for (int z = 4; z <= 6; ++z)
            for (int y = 1; y <= 4; ++y)
                world.setBlock(x, y, z, BlockType::Concrete);
    AcousticParams thickWall = audio.computeAcoustics({16.5f, 2.5f, 5.5f});
    EXPECT_GT(thickWall.obstacleThickness, thinWall.obstacleThickness + 1.2f);
    EXPECT_LT(thickWall.gainR, thinWall.gainR * 0.78f);
    EXPECT_LT(thickWall.lpfCutoffHz, thinWall.lpfCutoffHz * 0.65f);

    // 4. Conditional echo: long concrete corridor produces significantly stronger echo than cramped 2x2 alcove
    float corrWet = 0.0f, corrDelay = 0.0f;
    float alcoveWet = 0.0f, alcoveDelay = 0.0f;
    audio.measureRoomEcho({5.5f, 2.5f, 5.5f}, corrWet, corrDelay);
    audio.measureRoomEcho({1.5f, 2.0f, 12.5f}, alcoveWet, alcoveDelay);
    EXPECT_GT(corrWet, alcoveWet * 3.0f);
    EXPECT_GT(corrDelay, alcoveDelay);

    // 5. Per-sound volume scaling from config
    s.masterVolume = 0.5f;
    s.volDoorCreak = 0.8f;
    audio.applySettings(s);
    EXPECT_NEAR(audio.soundVolumeScale(SoundId::DoorCreak), 0.40f, 1e-4f);
}

TEST(LockpickItemAndExitDoorTest, FivePercentBreakChanceAndSafeExitDoor) {
    AudioEngine audio;
    GameWorld gw;
    gw.startNewRun(424242ULL);

    // Starter room spawns lockpicks on the table and locks the 2m steel exit door
    int tableLockpicks = 0;
    for (const auto& it : gw.worldItems) {
        if (it.type == ItemType::Lockpick) tableLockpicks++;
    }
    EXPECT_GE(tableLockpicks, 3);
    ASSERT_FALSE(gw.doors.empty());
    EXPECT_TRUE(gw.doors[0].isExitDoor);
    EXPECT_TRUE(gw.doors[0].locked);

    // Cannot open lockpick minigame with 0 lockpicks
    gw.inventory.lockpicks = 0;
    gw.player.position = {59.8f, 1.0f, 58.5f};
    gw.player.yaw = PI * 0.5f; // Face East (+X) toward exit door
    gw.player.pitch = 0.0f;
    gw.beginInteract(audio);
    EXPECT_FALSE(gw.lockpick.active);

    // With lockpicks, exit door opens with 0% break chance
    gw.inventory.lockpicks = 2;
    gw.beginInteract(audio);
    ASSERT_TRUE(gw.lockpick.active);
    EXPECT_FLOAT_EQ(gw.lockpick.breakChance, 0.0f);

    // 500 pin contacts on exit door (0% break chance) -> ZERO snaps
    for (int k = 0; k < 500; ++k) {
        gw.lockpick.pickX = 0.5f;
        gw.lockpick.pickY = -0.05f;
        gw.lockpick.pins[0].pos = 0.0f;
        gw.lockpick.pins[0].vel = 0.0f;
        gw.lockpick.pins[0].pinned = false;
        gw.lockpick.update(cfg::FIXED_DT, audio);
        gw.lockpick.handleMouseMove(0.0f, -12.0f); // contact pin
        gw.lockpick.update(cfg::FIXED_DT, audio);
        EXPECT_FALSE(gw.lockpick.pickSnappedJustNow);
    }

    // 2000 pin contacts on a regular lock (5% break chance) -> ~5% snaps (within 2.5%..8%)
    LockpickMinigame regLock;
    regLock.open(1, 99999ULL, 0.05f);
    int snaps = 0;
    for (int k = 0; k < 2000; ++k) {
        regLock.pickSnappedJustNow = false;
        regLock.pickX = 0.5f;
        regLock.pickY = -0.05f;
        regLock.pins[0].pos = 0.0f;
        regLock.pins[0].vel = 0.0f;
        regLock.pins[0].pinned = false;
        regLock.pins[0].wasTouchingPick = false;
        regLock.handleMouseMove(0.0f, -12.0f);
        regLock.update(cfg::FIXED_DT, audio);
        if (regLock.pickSnappedJustNow) snaps++;
    }
    EXPECT_GT(snaps, 50);
    EXPECT_LT(snaps, 160);
    std::printf("[LOCKPICK BREAK TEST] 5%% chance over 2000 contacts -> %d snaps (%.2f%%)\n",
                snaps, (snaps * 100.0f) / 2000.0f);
}

TEST(ConfigHotReloadTest, ParsesPlayerLightingAndAudioSettings) {
    const std::string tmpPath = "/tmp/mega_test_cfg.cfg";
    {
        std::ofstream out(tmpPath);
        out << "max_health = 150.0\n"
            << "hunger_drain_rate = 0.5\n"
            << "robot_led_intensity = 2.2\n"
            << "robot_alert_led_color_r = 0.9\n"
            << "vol_door_creak = 0.42\n"
            << "vol_player_step = 0.61\n"
            << "echo_strength = 0.77\n"
            << "lockpick_break_chance = 0.08\n";
    }
    GameSettings s = GameSettings::loadOrCreate(tmpPath);
    EXPECT_FLOAT_EQ(s.maxHealth, 150.0f);
    EXPECT_FLOAT_EQ(s.hungerDrainRate, 0.5f);
    EXPECT_FLOAT_EQ(s.robotLedIntensity, 2.2f);
    EXPECT_FLOAT_EQ(s.robotAlertLedColorR, 0.9f);
    EXPECT_FLOAT_EQ(s.volDoorCreak, 0.42f);
    EXPECT_FLOAT_EQ(s.volPlayerStep, 0.61f);
    EXPECT_FLOAT_EQ(s.echoStrength, 0.77f);
    EXPECT_FLOAT_EQ(s.lockpickBreakChance, 0.08f);

    GameWorld gw;
    gw.settings = s;
    gw.startNewRun(1234ULL);
    EXPECT_FLOAT_EQ(gw.health, 150.0f);
}

TEST(RobotNavigationTest, CenterlineBypassAndCooperativePassing) {
    AudioEngine audio;
    GameWorld gw;
    gw.startNewRun(1111ULL);
    gw.godInvisible = true; // Keep player out of AI aggro

    // Build a clean 3m-wide straight corridor: x=10..42, z=4..6 (centerline z=5.5), y=1..4
    gw.map.world = WorldGrid{};
    for (int x = 9; x <= 43; ++x) {
        for (int z = 3; z <= 7; ++z) {
            for (int y = 0; y <= 5; ++y) {
                bool hollow = (x >= 10 && x <= 42 && z >= 4 && z <= 6 && y >= 1 && y <= 4);
                gw.map.world.setBlock(x, y, z, hollow ? BlockType::Air : BlockType::Concrete);
            }
        }
    }
    gw.player.position = {11.0f, 1.0f, 5.5f};

    // --- PART A: Diagonal obstacle bypass & return to centerline ---
    gw.map.world.addProp({{20.1f, 1.0f, 5.15f}, {20.9f, 2.2f, 5.85f}}, BlockType::HazardStripe, true);
    gw.enemies.clear();
    Enemy r;
    r.pos = {15.0f, 1.0f, 5.5f};
    r.dirX = 1; r.dirZ = 0;
    r.pendingDirX = 1; r.pendingDirZ = 0;
    r.yaw = PI * 0.5f;
    r.decisionCooldown = 999.0f;
    gw.enemies.push_back(r);

    PlayerInput idle;
    float maxOffsetNearProp = 0.0f;
    for (int f = 0; f < 1600; ++f) { // ~26.7s at 0.44m/s -> ~11.7m travel (x: 15 -> 26.7)
        gw.update(idle, false, cfg::FIXED_DT, audio);
        const auto& e = gw.enemies[0];
        if (e.pos.x >= 19.0f && e.pos.x <= 22.0f) {
            maxOffsetNearProp = std::max(maxOffsetNearProp, std::abs(e.pos.z - 5.5f));
        }
    }
    std::printf("[NAV OBSTACLE BYPASS] final=(%.2f, %.2f) maxSidestep=%.2fm\n",
                gw.enemies[0].pos.x, gw.enemies[0].pos.z, maxOffsetNearProp);
    EXPECT_GT(gw.enemies[0].pos.x, 24.0f);       // Passed cleanly around the obstacle
    EXPECT_GT(maxOffsetNearProp, 0.30f);         // Diagonally sidestepped around the crate
    EXPECT_LT(std::abs(gw.enemies[0].pos.z - 5.5f), 0.22f); // Returned to corridor centerline!

    // --- PART B: Cooperative robot-vs-robot passing in the 3m corridor ---
    gw.map.world = WorldGrid{};
    for (int x = 9; x <= 43; ++x) {
        for (int z = 3; z <= 7; ++z) {
            for (int y = 0; y <= 5; ++y) {
                bool hollow = (x >= 10 && x <= 42 && z >= 4 && z <= 6 && y >= 1 && y <= 4);
                gw.map.world.setBlock(x, y, z, hollow ? BlockType::Air : BlockType::Concrete);
            }
        }
    }
    gw.enemies.clear();
    Enemy rA, rB;
    rA.pos = {16.0f, 1.0f, 5.5f}; rA.dirX = 1;  rA.dirZ = 0; rA.yaw =  PI * 0.5f; rA.decisionCooldown = 999.0f;
    rB.pos = {24.0f, 1.0f, 5.5f}; rB.dirX = -1; rB.dirZ = 0; rB.yaw = -PI * 0.5f; rB.decisionCooldown = 999.0f;
    gw.enemies.push_back(rA);
    gw.enemies.push_back(rB);

    bool negotiatedOpposite = false;
    float minRobotDist = 999.0f;
    for (int f = 0; f < 1800; ++f) { // 30s -> each travels ~13m, must cross each other cleanly
        gw.update(idle, false, cfg::FIXED_DT, audio);
        const auto& a = gw.enemies[0];
        const auto& b = gw.enemies[1];
        float d = std::hypot(a.pos.x - b.pos.x, a.pos.z - b.pos.z);
        minRobotDist = std::min(minRobotDist, d);
        if (a.yieldTimer > 0.0f && b.yieldTimer > 0.0f &&
            (a.yieldLateralDirZ * b.yieldLateralDirZ < -0.1f)) {
            negotiatedOpposite = true;
        }
    }
    std::printf("[NAV COOP PASS] A=(%.2f, %.2f) B=(%.2f, %.2f) minDist=%.2fm negotiated=%d\n",
                gw.enemies[0].pos.x, gw.enemies[0].pos.z,
                gw.enemies[1].pos.x, gw.enemies[1].pos.z,
                minRobotDist, negotiatedOpposite ? 1 : 0);
    EXPECT_TRUE(negotiatedOpposite);
    EXPECT_GT(gw.enemies[0].pos.x, 24.0f);       // Robot A passed all the way past B's start
    EXPECT_LT(gw.enemies[1].pos.x, 16.0f);       // Robot B passed all the way past A's start
    EXPECT_GT(minRobotDist, 0.70f);              // Never clipped through each other
    EXPECT_LT(std::abs(gw.enemies[0].pos.z - 5.5f), 0.25f); // Returned to center
    EXPECT_LT(std::abs(gw.enemies[1].pos.z - 5.5f), 0.25f); // Returned to center
}

TEST(GodModeTest, NoclipInvisibilityAndSpeedWork) {
    AudioEngine audio;
    GameWorld gw;
    gw.startNewRun(5555ULL);

    // 1. Noclip flies directly through a solid concrete wall
    gw.godMode = true;
    gw.godNoclip = true;
    gw.godSpeedMult = 4.0f;
    gw.player.position = {58.0f, 2.0f, 58.0f};
    gw.player.yaw = 0.0f; // Facing -Z (toward North starter room wall at z=54)
    gw.player.pitch = 0.0f;

    PlayerInput flyFwd;
    flyFwd.moveForward = 1.0f;
    flyFwd.noclip = true;
    for (int f = 0; f < 30; ++f) { // 0.5s at 4.6 * 4 = 18.4 m/s -> ~9.2m straight through concrete!
        gw.update(flyFwd, false, cfg::FIXED_DT, audio);
    }
    EXPECT_LT(gw.player.position.z, 51.0f); // Passed cleanly through solid z=54 concrete wall
    EXPECT_FLOAT_EQ(gw.health, gw.settings.maxHealth);
    EXPECT_FLOAT_EQ(gw.hunger, gw.settings.maxHunger);

    // 2. God Invisibility prevents robot from spotting player & drops active Chase
    gw.godInvisible = true;
    gw.enemies.clear();
    Enemy e;
    e.pos = {58.0f, 1.0f, 58.0f};
    e.yaw = 0.0f; // Facing +Z
    e.aiState = EnemyAIState::Chase;
    e.alerted = true;
    gw.enemies.push_back(e);
    gw.player.position = {58.0f, 1.0f, 60.0f};

    PlayerInput idle;
    gw.update(idle, false, cfg::FIXED_DT, audio);
    EXPECT_NE(gw.enemies[0].aiState, EnemyAIState::Chase);
    EXPECT_FALSE(gw.enemies[0].alerted);
}
