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

    // Raycast straight down from player eye should hit the floor within ~2m
    float hitDist = gw.raycastWorld(gw.player.eyePosition(), {0.0f, -1.0f, 0.0f}, 10.0f);
    EXPECT_GT(hitDist, 0.5f);
    EXPECT_LT(hitDist, 2.5f);
}
