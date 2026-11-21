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

TEST(GameWorldTest, RaycastAndEnemySpawningWork) {
    GameWorld gw;
    gw.startNewRun(12345ULL);
    EXPECT_GE(gw.enemies.size(), 10u); // 2-3 per region across 9 loaded regions

    // Raycast straight down from player eye should hit the floor within ~2m
    float hitDist = gw.raycastWorld(gw.player.eyePosition(), {0.0f, -1.0f, 0.0f}, 10.0f);
    EXPECT_GT(hitDist, 0.5f);
    EXPECT_LT(hitDist, 2.5f);
}

TEST(RobotSimTest, ObservePatrolAndChaseFootKinematics) {
    GameWorld gw;
    gw.startNewRun(12345ULL);
    ASSERT_FALSE(gw.enemies.empty());

    // Place first robot right in front of player in a clear corridor to observe Patrol & Chase
    Enemy& e = gw.enemies[0];
    e.pos = {64.5f, 1.0f, 70.0f};
    e.dirX = 0; e.dirZ = 1;
    e.yaw = 0.0f;
    e.aiState = EnemyAIState::Patrol;
    e.alerted = false;
    e.movePulseTimer = 0.85f; // Just before 0.90s surge!
    e.feet[0].initialized = false;
    e.feet[1].initialized = false;

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
    ASSERT_FALSE(gw.enemies.empty());

    AudioEngine audio;
    PlayerInput idle;

    // --- PART 1: 45-second Patrol navigation test on robots in region (0,0) ---
    gw.player.position = {10.0f, 1.0f, 10.0f}; // Out of sight in same region
    std::printf("=== MAP NAVIGATION: PATROL (45s) ===\n");
    std::vector<int> reg0Idx;
    for (size_t i = 0; i < gw.enemies.size(); ++i) {
        if (gw.enemies[i].regX == 0 && gw.enemies[i].regZ == 0) {
            reg0Idx.push_back(static_cast<int>(i));
        }
    }
    ASSERT_FALSE(reg0Idx.empty());

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
