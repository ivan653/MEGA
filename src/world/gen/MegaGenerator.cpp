#include "world/gen/MegaGenerator.hpp"
#include "core/Rng.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace mega {

namespace {

constexpr int FLOOR_Y = 1;
constexpr int CEIL_H  = 4;

uint64_t regionSeed(uint64_t baseSeed, int rx, int rz) {
    uint64_t hx = static_cast<uint64_t>(static_cast<uint32_t>(rx)) * 0x9E3779B97F4A7C15ULL;
    uint64_t hz = static_cast<uint64_t>(static_cast<uint32_t>(rz)) * 0xBF58476D1CE4E5B9ULL;
    return baseSeed ^ hx ^ (hz << 1);
}

bool canPlaceProp(const WorldGrid& w, const AABB& box, const Vec3& spawnPos, float clearanceMargin = 0.15f) {
    float cx = 0.5f * (box.min.x + box.max.x);
    float cz = 0.5f * (box.min.z + box.max.z);
    if (std::abs(cx - spawnPos.x) < 2.6f && std::abs(cz - spawnPos.z) < 2.6f) return false;

    AABB expanded{
        {box.min.x - clearanceMargin, box.min.y, box.min.z - clearanceMargin},
        {box.max.x + clearanceMargin, box.max.y, box.max.z + clearanceMargin}
    };
    for (const auto& c : w.propColliders()) {
        if (expanded.intersects(c)) return false;
    }

    int x0 = static_cast<int>(std::floor(box.min.x + 0.05f));
    int x1 = static_cast<int>(std::floor(box.max.x - 0.05f));
    int z0 = static_cast<int>(std::floor(box.min.z + 0.05f));
    int z1 = static_cast<int>(std::floor(box.max.z - 0.05f));
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            if (w.getBlock(x, FLOOR_Y, z) != BlockType::Air) return false;
            if (!w.getCell(x, FLOOR_Y - 1, z).isSolid()) return false;
        }
    }
    return true;
}

void placeWallLamp(WorldGrid& w, std::vector<PointLight3D>& lights,
                   float wallX, float wallZ, int normalX, int normalZ,
                   int regX, int regZ, const GameSettings& settings) {
    int bx = static_cast<int>(std::floor(wallX - normalX * 0.25f));
    int bz = static_cast<int>(std::floor(wallZ - normalZ * 0.25f));
    int fx = static_cast<int>(std::floor(wallX + normalX * 0.35f));
    int fz = static_cast<int>(std::floor(wallZ + normalZ * 0.35f));
    if (w.getBlock(bx, 3, bz) == BlockType::Air) return;
    if (w.getBlock(fx, 3, fz) != BlockType::Air) return;

    Vec3 lightPos{
        wallX + normalX * 0.42f,
        static_cast<float>(FLOOR_Y) + 2.22f,
        wallZ + normalZ * 0.42f
    };
    for (const auto& l : lights) {
        if (lengthSq(l.pos - lightPos) < 7.5f * 7.5f) return;
    }

    float y0 = static_cast<float>(FLOOR_Y) + 2.12f;
    float y1 = y0 + 0.16f;
    float halfLen = 0.34f;

    if (normalX != 0) {
        float xWall = wallX;
        float xOut1 = wallX + normalX * 0.05f;
        float xOut2 = wallX + normalX * 0.12f;
        w.addProp({{std::min(xWall, xOut1), y0 - 0.03f, wallZ - halfLen - 0.04f},
                   {std::max(xWall, xOut1), y1 + 0.03f, wallZ + halfLen + 0.04f}},
                  BlockType::Grate, false, false, true, regX, regZ);
        w.addProp({{std::min(xOut1, xOut2), y0, wallZ - halfLen},
                   {std::max(xOut1, xOut2), y1, wallZ + halfLen}},
                  BlockType::ReactorGlow, false, true, true, regX, regZ);
    } else {
        float zWall = wallZ;
        float zOut1 = wallZ + normalZ * 0.05f;
        float zOut2 = wallZ + normalZ * 0.12f;
        w.addProp({{wallX - halfLen - 0.04f, y0 - 0.03f, std::min(zWall, zOut1)},
                   {wallX + halfLen + 0.04f, y1 + 0.03f, std::max(zWall, zOut1)}},
                  BlockType::Grate, false, false, true, regX, regZ);
        w.addProp({{wallX - halfLen, y0, std::min(zOut1, zOut2)},
                   {wallX + halfLen, y1, std::max(zOut1, zOut2)}},
                  BlockType::ReactorGlow, false, true, true, regX, regZ);
    }

    PointLight3D pl;
    pl.pos = lightPos;
    pl.color = {settings.lampColorR, settings.lampColorG, settings.lampColorB};
    pl.radius = settings.lampRadius;
    pl.intensity = settings.lampIntensity;
    pl.regX = regX;
    pl.regZ = regZ;
    lights.push_back(pl);
}

void placeWoodenCrates(WorldGrid& w, float wallX, float wallZ, int normalX, int normalZ,
                       int regX, int regZ, const Vec3& spawnPos, Rng& rng) {
    float depth = 0.74f + rng.nextFloat() * 0.14f;
    float width = 0.78f + rng.nextFloat() * 0.22f;
    float height = 0.76f + rng.nextFloat() * 0.14f;
    float y0 = static_cast<float>(FLOOR_Y);

    AABB baseBox;
    if (normalX != 0) {
        float xA = wallX + normalX * 0.02f;
        float xB = wallX + normalX * (0.02f + depth);
        baseBox = {{std::min(xA, xB), y0, wallZ - width * 0.5f},
                   {std::max(xA, xB), y0 + height, wallZ + width * 0.5f}};
    } else {
        float zA = wallZ + normalZ * 0.02f;
        float zB = wallZ + normalZ * (0.02f + depth);
        baseBox = {{wallX - width * 0.5f, y0, std::min(zA, zB)},
                   {wallX + width * 0.5f, y0 + height, std::max(zA, zB)}};
    }

    if (!canPlaceProp(w, baseBox, spawnPos)) return;
    w.addProp(baseBox, BlockType::HazardStripe, true, false, true, regX, regZ);

    if (rng.chance(0.55f)) {
        float shrink = 0.08f;
        float h2 = 0.64f + rng.nextFloat() * 0.12f;
        AABB topBox{
            {baseBox.min.x + shrink, baseBox.max.y, baseBox.min.z + shrink},
            {baseBox.max.x - shrink, baseBox.max.y + h2, baseBox.max.z - shrink}
        };
        w.addProp(topBox, BlockType::HazardStripe, true, false, true, regX, regZ);
    }

    if (rng.chance(0.45f)) {
        float sideW = 0.70f;
        float sideH = 0.68f;
        AABB sideBox = baseBox;
        if (normalX != 0) {
            sideBox.min.z = baseBox.max.z + 0.04f;
            sideBox.max.z = sideBox.min.z + sideW;
        } else {
            sideBox.min.x = baseBox.max.x + 0.04f;
            sideBox.max.x = sideBox.min.x + sideW;
        }
        sideBox.max.y = y0 + sideH;
        if (canPlaceProp(w, sideBox, spawnPos, 0.02f)) {
            w.addProp(sideBox, BlockType::HazardStripe, true, false, true, regX, regZ);
        }
    }
}

void placeMetalCabinet(WorldGrid& w, float wallX, float wallZ, int normalX, int normalZ,
                       int regX, int regZ, const Vec3& spawnPos, Rng& rng) {
    float depth = 0.52f;
    float width = rng.chance(0.5f) ? 0.92f : 1.45f;
    float height = 2.08f;
    float y0 = static_cast<float>(FLOOR_Y);

    AABB box;
    if (normalX != 0) {
        float xA = wallX + normalX * 0.02f;
        float xB = wallX + normalX * (0.02f + depth);
        box = {{std::min(xA, xB), y0, wallZ - width * 0.5f},
               {std::max(xA, xB), y0 + height, wallZ + width * 0.5f}};
    } else {
        float zA = wallZ + normalZ * 0.02f;
        float zB = wallZ + normalZ * (0.02f + depth);
        box = {{wallX - width * 0.5f, y0, std::min(zA, zB)},
               {wallX + width * 0.5f, y0 + height, std::max(zA, zB)}};
    }

    if (!canPlaceProp(w, box, spawnPos)) return;
    w.addProp(box, BlockType::RustPlate, true, false, true, regX, regZ);
}

void placeShelvingRack(WorldGrid& w, float wallX, float wallZ, int normalX, int normalZ,
                       int regX, int regZ, const Vec3& spawnPos, Rng& rng) {
    float depth = 0.54f;
    float width = 1.55f;
    float height = 2.12f;
    float y0 = static_cast<float>(FLOOR_Y);

    AABB bounds;
    if (normalX != 0) {
        float xA = wallX + normalX * 0.02f;
        float xB = wallX + normalX * (0.02f + depth);
        bounds = {{std::min(xA, xB), y0, wallZ - width * 0.5f},
                  {std::max(xA, xB), y0 + height, wallZ + width * 0.5f}};
    } else {
        float zA = wallZ + normalZ * 0.02f;
        float zB = wallZ + normalZ * (0.02f + depth);
        bounds = {{wallX - width * 0.5f, y0, std::min(zA, zB)},
                  {wallX + width * 0.5f, y0 + height, std::max(zA, zB)}};
    }

    if (!canPlaceProp(w, bounds, spawnPos)) return;

    const float postT = 0.06f;
    float xs[2] = {bounds.min.x, bounds.max.x - postT};
    float zs[2] = {bounds.min.z, bounds.max.z - postT};
    for (int ix = 0; ix < 2; ++ix) {
        for (int iz = 0; iz < 2; ++iz) {
            w.addProp({{xs[ix], y0, zs[iz]},
                       {xs[ix] + postT, y0 + height, zs[iz] + postT}},
                      BlockType::Grate, false, false, true, regX, regZ);
        }
    }

    const float shelfHeights[4] = {0.18f, 0.78f, 1.38f, 1.98f};
    for (int s = 0; s < 4; ++s) {
        float sy = y0 + shelfHeights[s];
        w.addProp({{bounds.min.x, sy, bounds.min.z},
                   {bounds.max.x, sy + 0.05f, bounds.max.z}},
                  BlockType::Grate, false, false, true, regX, regZ);

        if (s < 3 && rng.chance(0.75f)) {
            float boxH = 0.34f + rng.nextFloat() * 0.12f;
            float marginX = (bounds.max.x - bounds.min.x) * 0.14f;
            float marginZ = (bounds.max.z - bounds.min.z) * 0.14f;
            w.addProp({{bounds.min.x + marginX, sy + 0.05f, bounds.min.z + marginZ},
                       {bounds.max.x - marginX, sy + 0.05f + boxH, bounds.max.z - marginZ}},
                      BlockType::HazardStripe, false, false, true, regX, regZ);
        }
    }

    w.addProp({{bounds.min.x + 0.02f, y0, bounds.min.z + 0.02f},
               {bounds.max.x - 0.02f, y0 + 0.04f, bounds.max.z - 0.02f}},
              BlockType::Grate, true, false, true, regX, regZ);
}

void fillCorridorShell(WorldGrid& w, const CorridorSegment& seg) {
    int minX = std::min(seg.x0, seg.x1);
    int maxX = std::max(seg.x0, seg.x1);
    int minZ = std::min(seg.z0, seg.z1);
    int maxZ = std::max(seg.z0, seg.z1);

    if (seg.alongX) {
        int zStart = minZ;
        int zEnd = minZ + seg.width - 1;
        w.fillBox(minX - 1, FLOOR_Y - 1, zStart - 1, maxX + 1, FLOOR_Y + CEIL_H, zEnd + 1, BlockType::Concrete);
    } else {
        int xStart = minX;
        int xEnd = minX + seg.width - 1;
        w.fillBox(xStart - 1, FLOOR_Y - 1, minZ - 1, xEnd + 1, FLOOR_Y + CEIL_H, maxZ + 1, BlockType::Concrete);
    }
}

void carveCorridor(WorldGrid& w, const CorridorSegment& seg) {
    int minX = std::min(seg.x0, seg.x1);
    int maxX = std::max(seg.x0, seg.x1);
    int minZ = std::min(seg.z0, seg.z1);
    int maxZ = std::max(seg.z0, seg.z1);

    if (seg.alongX) {
        int zStart = minZ;
        int zEnd = minZ + seg.width - 1;
        w.fillBox(minX, FLOOR_Y, zStart, maxX, FLOOR_Y + CEIL_H - 1, zEnd, BlockType::Air);
        w.fillBox(minX, FLOOR_Y - 1, zStart, maxX, FLOOR_Y - 1, zEnd, BlockType::TechPanel);
        w.fillBox(minX, FLOOR_Y + CEIL_H, zStart, maxX, FLOOR_Y + CEIL_H, zEnd, BlockType::DarkSteel);
    } else {
        int xStart = minX;
        int xEnd = minX + seg.width - 1;
        w.fillBox(xStart, FLOOR_Y, minZ, xEnd, FLOOR_Y + CEIL_H - 1, maxZ, BlockType::Air);
        w.fillBox(xStart, FLOOR_Y - 1, minZ, xEnd, FLOOR_Y - 1, maxZ, BlockType::TechPanel);
        w.fillBox(xStart, FLOOR_Y + CEIL_H, minZ, xEnd, FLOOR_Y + CEIL_H, maxZ, BlockType::DarkSteel);
    }
}

void populateCorridor(WorldGrid& w, const CorridorSegment& seg,
                      std::vector<PointLight3D>& lights, const Vec3& spawnPos,
                      const GameSettings& settings, Rng& rng) {
    int minX = std::min(seg.x0, seg.x1);
    int maxX = std::max(seg.x0, seg.x1);
    int minZ = std::min(seg.z0, seg.z1);
    int maxZ = std::max(seg.z0, seg.z1);

    if (seg.alongX) {
        float zNorthWall = static_cast<float>(minZ);
        float zSouthWall = static_cast<float>(minZ + seg.width);

        int lampStep = rng.rangeInt(12, 17);
        bool side = rng.chance(0.5f);
        for (int x = minX + 4; x <= maxX - 4; x += lampStep) {
            if (side) placeWallLamp(w, lights, x + 0.5f, zNorthWall, 0, 1, seg.regX, seg.regZ, settings);
            else      placeWallLamp(w, lights, x + 0.5f, zSouthWall, 0, -1, seg.regX, seg.regZ, settings);
            if (rng.chance(0.35f)) side = !side;
        }

        int step = (seg.width >= 3) ? rng.rangeInt(5, 8) : rng.rangeInt(9, 14);
        for (int x = minX + 3; x <= maxX - 3; x += step) {
            bool north = rng.chance(0.5f);
            float wz = north ? zNorthWall : zSouthWall;
            int nz = north ? 1 : -1;
            int wallCheckZ = north ? (minZ - 1) : (minZ + seg.width);
            if (w.getBlock(x, FLOOR_Y, wallCheckZ) == BlockType::Air ||
                w.getBlock(x - 1, FLOOR_Y, wallCheckZ) == BlockType::Air ||
                w.getBlock(x + 1, FLOOR_Y, wallCheckZ) == BlockType::Air) {
                continue;
            }

            int roll = rng.rangeInt(0, 9);
            if (seg.width == 2) {
                if (roll < 5) placeMetalCabinet(w, x + 0.5f, wz, 0, nz, seg.regX, seg.regZ, spawnPos, rng);
                else placeShelvingRack(w, x + 0.5f, wz, 0, nz, seg.regX, seg.regZ, spawnPos, rng);
            } else {
                if (roll < 4) placeWoodenCrates(w, x + 0.5f, wz, 0, nz, seg.regX, seg.regZ, spawnPos, rng);
                else if (roll < 7) placeMetalCabinet(w, x + 0.5f, wz, 0, nz, seg.regX, seg.regZ, spawnPos, rng);
                else placeShelvingRack(w, x + 0.5f, wz, 0, nz, seg.regX, seg.regZ, spawnPos, rng);
            }
        }
    } else {
        float xWestWall = static_cast<float>(minX);
        float xEastWall = static_cast<float>(minX + seg.width);

        int lampStep = rng.rangeInt(12, 17);
        bool side = rng.chance(0.5f);
        for (int z = minZ + 4; z <= maxZ - 4; z += lampStep) {
            if (side) placeWallLamp(w, lights, xWestWall, z + 0.5f, 1, 0, seg.regX, seg.regZ, settings);
            else      placeWallLamp(w, lights, xEastWall, z + 0.5f, -1, 0, seg.regX, seg.regZ, settings);
            if (rng.chance(0.35f)) side = !side;
        }

        int step = (seg.width >= 3) ? rng.rangeInt(5, 8) : rng.rangeInt(9, 14);
        for (int z = minZ + 3; z <= maxZ - 3; z += step) {
            bool west = rng.chance(0.5f);
            float wx = west ? xWestWall : xEastWall;
            int nx = west ? 1 : -1;
            int wallCheckX = west ? (minX - 1) : (minX + seg.width);
            if (w.getBlock(wallCheckX, FLOOR_Y, z) == BlockType::Air ||
                w.getBlock(wallCheckX, FLOOR_Y, z - 1) == BlockType::Air ||
                w.getBlock(wallCheckX, FLOOR_Y, z + 1) == BlockType::Air) {
                continue;
            }

            int roll = rng.rangeInt(0, 9);
            if (seg.width == 2) {
                if (roll < 5) placeMetalCabinet(w, wx, z + 0.5f, nx, 0, seg.regX, seg.regZ, spawnPos, rng);
                else placeShelvingRack(w, wx, z + 0.5f, nx, 0, seg.regX, seg.regZ, spawnPos, rng);
            } else {
                if (roll < 4) placeWoodenCrates(w, wx, z + 0.5f, nx, 0, seg.regX, seg.regZ, spawnPos, rng);
                else if (roll < 7) placeMetalCabinet(w, wx, z + 0.5f, nx, 0, seg.regX, seg.regZ, spawnPos, rng);
                else placeShelvingRack(w, wx, z + 0.5f, nx, 0, seg.regX, seg.regZ, spawnPos, rng);
            }
        }
    }
}

// Checks if two 1D intervals [a0, a1] and [b0, b1] overlap with a margin
bool intervalsOverlap(int a0, int a1, int b0, int b1, int margin = 0) {
    return (std::min(a0, a1) - margin <= std::max(b0, b1)) &&
           (std::max(a0, a1) + margin >= std::min(b0, b1));
}

} // namespace

void MegaGenerator::generateRegion(MegaResult& state, int regX, int regZ,
                                   const GameSettings& settings) {
    if (state.loadedRegions.count({regX, regZ}) > 0) return;
    state.loadedRegions.insert({regX, regZ});

    Rng rng(regionSeed(state.seed, regX, regZ));
    const int baseX = regX * cfg::REGION_SIZE;
    const int baseZ = regZ * cfg::REGION_SIZE;

    std::vector<CorridorSegment> regCorridors;

    // 1. Primary North-South transit corridor at local X = 63 (72m long, reaching South border at 128)
    // and connecting to previous region's South portal at local Z = 0 via a 2-segment spine
    CorridorSegment spineNS1;
    spineNS1.x0 = baseX + 63;
    spineNS1.z0 = baseZ + 0;
    spineNS1.x1 = baseX + 63;
    spineNS1.z1 = baseZ + 68;
    spineNS1.width = 3;
    spineNS1.alongX = false;
    spineNS1.connections = 2; // Connects to north region + spineNS2
    spineNS1.targetConnections = rng.rangeInt(settings.minConnections, settings.maxConnections);
    spineNS1.regX = regX;
    spineNS1.regZ = regZ;
    regCorridors.push_back(spineNS1);

    CorridorSegment spineNS2;
    spineNS2.x0 = baseX + 63;
    spineNS2.z0 = baseZ + 65;
    spineNS2.x1 = baseX + 63;
    spineNS2.z1 = baseZ + cfg::REGION_SIZE;
    spineNS2.width = 3;
    spineNS2.alongX = false;
    spineNS2.connections = 2; // Connects to spineNS1 + south region
    spineNS2.targetConnections = rng.rangeInt(settings.minConnections, settings.maxConnections);
    spineNS2.regX = regX;
    spineNS2.regZ = regZ;
    regCorridors.push_back(spineNS2);

    // 2. Primary West-East transit corridors connecting West border (X=0) and East border (X=128)
    CorridorSegment spineWE1;
    spineWE1.x0 = baseX + 0;
    spineWE1.z0 = baseZ + 63;
    spineWE1.x1 = baseX + 66;
    spineWE1.z1 = baseZ + 63;
    spineWE1.width = 3;
    spineWE1.alongX = true;
    spineWE1.connections = 2; // West border + central junction
    spineWE1.targetConnections = rng.rangeInt(settings.minConnections, settings.maxConnections);
    spineWE1.regX = regX;
    spineWE1.regZ = regZ;
    regCorridors.push_back(spineWE1);

    CorridorSegment spineWE2;
    spineWE2.x0 = baseX + 63;
    spineWE2.z0 = baseZ + 63;
    spineWE2.x1 = baseX + cfg::REGION_SIZE;
    spineWE2.z1 = baseZ + 63;
    spineWE2.width = 3;
    spineWE2.alongX = true;
    spineWE2.connections = 2; // Central junction + East border
    spineWE2.targetConnections = rng.rangeInt(settings.minConnections, settings.maxConnections);
    spineWE2.regX = regX;
    spineWE2.regZ = regZ;
    regCorridors.push_back(spineWE2);

    // 3. Grow sparse branching corridors where each corridor connects to ONLY 2..3 others
    for (int attempt = 0; attempt < 40 && regCorridors.size() < 9; ++attempt) {
        int pIdx = rng.rangeInt(0, static_cast<int>(regCorridors.size()) - 1);
        auto& parent = regCorridors[pIdx];
        if (parent.connections >= parent.targetConnections) continue;

        bool childAlongX = !parent.alongX;
        int w = rng.rangeInt(settings.corridorMinWidth, settings.corridorMaxWidth);
        int len = rng.rangeInt(settings.corridorMinLen, settings.corridorMaxLen);
        int dir = rng.chance(0.5f) ? 1 : -1;

        CorridorSegment cand;
        cand.width = w;
        cand.alongX = childAlongX;
        cand.connections = 1;
        cand.targetConnections = rng.rangeInt(settings.minConnections, settings.maxConnections);
        cand.regX = regX;
        cand.regZ = regZ;

        if (childAlongX) {
            int pMinZ = std::min(parent.z0, parent.z1) + 6;
            int pMaxZ = std::max(parent.z0, parent.z1) - 6;
            if (pMaxZ <= pMinZ) continue;
            int branchZ = rng.rangeInt(pMinZ, pMaxZ);
            int startX = parent.x0;
            int endX = std::clamp(startX + dir * len, baseX + 6, baseX + cfg::REGION_SIZE - 6);
            if (std::abs(endX - startX) < settings.corridorMinLen) continue;
            cand.x0 = std::min(startX, endX);
            cand.x1 = std::max(startX, endX);
            cand.z0 = cand.z1 = branchZ;
        } else {
            int pMinX = std::min(parent.x0, parent.x1) + 6;
            int pMaxX = std::max(parent.x0, parent.x1) - 6;
            if (pMaxX <= pMinX) continue;
            int branchX = rng.rangeInt(pMinX, pMaxX);
            int startZ = parent.z0;
            int endZ = std::clamp(startZ + dir * len, baseZ + 6, baseZ + cfg::REGION_SIZE - 6);
            if (std::abs(endZ - startZ) < settings.corridorMinLen) continue;
            cand.x0 = cand.x1 = branchX;
            cand.z0 = std::min(startZ, endZ);
            cand.z1 = std::max(startZ, endZ);
        }

        // Validate against all existing corridors in this region:
        // - Must maintain minParallelSpacing from parallel corridors
        // - Must NOT cross any perpendicular corridor other than pIdx
        bool valid = true;
        for (size_t j = 0; j < regCorridors.size(); ++j) {
            if (static_cast<int>(j) == pIdx) continue;
            const auto& other = regCorridors[j];
            if (other.alongX == cand.alongX) {
                if (cand.alongX) {
                    if (std::abs(cand.z0 - other.z0) < settings.minParallelSpacing &&
                        intervalsOverlap(cand.x0, cand.x1, other.x0, other.x1, 4)) {
                        valid = false;
                        break;
                    }
                } else {
                    if (std::abs(cand.x0 - other.x0) < settings.minParallelSpacing &&
                        intervalsOverlap(cand.z0, cand.z1, other.z0, other.z1, 4)) {
                        valid = false;
                        break;
                    }
                }
            } else {
                // Perpendicular: do not intersect other corridors
                bool crossX = cand.alongX
                                  ? intervalsOverlap(cand.x0, cand.x1, other.x0, other.x0 + other.width, 2)
                                  : intervalsOverlap(other.x0, other.x1, cand.x0, cand.x0 + cand.width, 2);
                bool crossZ = cand.alongX
                                  ? intervalsOverlap(other.z0, other.z1, cand.z0, cand.z0 + cand.width, 2)
                                  : intervalsOverlap(cand.z0, cand.z1, other.z0, other.z0 + other.width, 2);
                if (crossX && crossZ) {
                    valid = false;
                    break;
                }
            }
        }

        if (!valid) continue;

        parent.connections++;
        regCorridors.push_back(cand);
    }

    // Pass 1: Place outer concrete shells for all corridors (and any already-loaded overlapping corridors)
    for (const auto& seg : regCorridors) {
        fillCorridorShell(state.world, seg);
    }
    // Pass 2: Carve air/floor/ceiling for all corridors so no shell ever seals a junction!
    for (const auto& seg : regCorridors) {
        carveCorridor(state.world, seg);
    }
    for (const auto& existing : state.corridors) {
        if (std::abs(existing.regX - regX) <= 1 && std::abs(existing.regZ - regZ) <= 1) {
            carveCorridor(state.world, existing);
        }
    }

    // Populate lamps and 3D furniture
    for (const auto& seg : regCorridors) {
        populateCorridor(state.world, seg, state.pointLights, state.spawnPos, settings, rng);
        state.corridors.push_back(seg);
    }
}

bool MegaGenerator::updateInfiniteStream(MegaResult& state, const Vec3& playerPos,
                                         const GameSettings& settings) {
    int rx = WorldGrid::floorDiv(static_cast<int>(std::floor(playerPos.x)), cfg::REGION_SIZE);
    int rz = WorldGrid::floorDiv(static_cast<int>(std::floor(playerPos.z)), cfg::REGION_SIZE);
    state.currentRegX = rx;
    state.currentRegZ = rz;

    bool changed = false;
    int R = settings.streamRadius;
    for (int dz = -R; dz <= R; ++dz) {
        for (int dx = -R; dx <= R; ++dx) {
            int nx = rx + dx;
            int nz = rz + dz;
            if (state.loadedRegions.count({nx, nz}) == 0) {
                generateRegion(state, nx, nz, settings);
                changed = true;
            }
        }
    }

    // Unload regions outside keepRadius = R + 1
    int keepRadius = R + 1;
    std::vector<std::pair<int, int>> toUnload;
    for (const auto& reg : state.loadedRegions) {
        if (std::abs(reg.first - rx) > keepRadius || std::abs(reg.second - rz) > keepRadius) {
            toUnload.push_back(reg);
        }
    }

    if (!toUnload.empty()) {
        for (const auto& reg : toUnload) {
            state.loadedRegions.erase(reg);
        }
        state.world.unloadFarRegions(rx, rz, keepRadius);
        state.pointLights.erase(
            std::remove_if(state.pointLights.begin(), state.pointLights.end(),
                           [&](const PointLight3D& l) {
                               return std::abs(l.regX - rx) > keepRadius ||
                                      std::abs(l.regZ - rz) > keepRadius;
                           }),
            state.pointLights.end());
        state.corridors.erase(
            std::remove_if(state.corridors.begin(), state.corridors.end(),
                           [&](const CorridorSegment& c) {
                               return std::abs(c.regX - rx) > keepRadius ||
                                      std::abs(c.regZ - rz) > keepRadius;
                           }),
            state.corridors.end());
        changed = true;
    }

    return changed;
}

MegaResult MegaGenerator::generate(uint64_t seed, const GameSettings& settings) {
    auto t0 = std::chrono::steady_clock::now();
    MegaResult res;
    res.seed = seed;
    res.spawnPos = {64.5f, static_cast<float>(FLOOR_Y), 64.5f};

    // Generate initial active region window around spawn
    updateInfiniteStream(res, res.spawnPos, settings);

    // Guarantee a wall lamp right near spawn
    placeWallLamp(res.world, res.pointLights, 63.0f, 67.5f, 1, 0, 0, 0, settings);

    auto t1 = std::chrono::steady_clock::now();
    res.generationMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return res;
}

} // namespace mega
