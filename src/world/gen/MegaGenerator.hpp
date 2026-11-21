#pragma once

#include "core/Config.hpp"
#include "core/Math3D.hpp"
#include "world/Biome.hpp"
#include "world/WorldGrid.hpp"
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace mega {

struct PointLight3D {
    Vec3 pos;
    Vec3 color{0.72f, 0.88f, 1.0f};
    float radius = 18.0f;
    float intensity = 1.45f;
    int regX = 0;
    int regZ = 0;
};

struct CorridorSegment {
    int x0, z0, x1, z1;
    int width = 3;         // 2..4 meters
    bool alongX = false;
    int connections = 0;   // Strictly 2..3 connections
    int targetConnections = 2;
    int regX = 0;
    int regZ = 0;
};

struct MegaResult {
    WorldGrid world;
    uint64_t seed = 0x4D4547410001ULL;
    Vec3 spawnPos{64.5f, 1.0f, 64.5f};
    Vec3 exitPos{120.0f, 1.0f, 120.0f};
    std::vector<CorridorSegment> corridors;
    std::vector<PointLight3D> pointLights;
    std::set<std::pair<int, int>> loadedRegions;
    int currentRegX = 0;
    int currentRegZ = 0;
    double generationMs = 0.0;

    MegaResult() : world(WorldGrid::createInfinite(BlockType::Concrete)) {}
};

class MegaGenerator {
public:
    static MegaResult generate(uint64_t seed, const GameSettings& settings = GameSettings{});

    // Streams new regions into the infinite world as the player moves, and unloads far regions.
    // Returns true if any region was generated or unloaded.
    static bool updateInfiniteStream(MegaResult& state, const Vec3& playerPos,
                                     const GameSettings& settings);

private:
    static void generateRegion(MegaResult& state, int regX, int regZ,
                               const GameSettings& settings);
};

} // namespace mega
