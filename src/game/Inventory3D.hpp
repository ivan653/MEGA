#pragma once

#include "core/LootTable.hpp"
#include "core/Math3D.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace mega {

struct Int3 {
    int x = 0;
    int y = 0;
    int z = 0;
};

// Discrete 90-degree 3D rotation state (rotX, rotY, rotZ in 0..3 quarter-turns)
struct ItemOrientation {
    int rotX = 0; // Pitch quarter-turns (0..3)
    int rotY = 0; // Yaw quarter-turns (0..3)
    int rotZ = 0; // Roll quarter-turns (0..3)

    void rotateX() { rotX = (rotX + 1) & 3; }
    void rotateY() { rotY = (rotY + 1) & 3; }
    void rotateZ() { rotZ = (rotZ + 1) & 3; }

    Mat4 toMatrix() const;
    Int3 applyToSize(Int3 baseSize) const;
};

// Base 3D grid dimensions (X, Y, Z) in backpack cells (1 cell = 0.06m = 6cm in world)
Int3 itemBaseGridSize(ItemType type);
const char* itemDisplayName(ItemType type);

struct PackedItem {
    int id = 0;
    ItemType type = ItemType::Food;
    int gx = 0; // 0 .. DIM_X-1 (width)
    int gy = 0; // 0 .. DIM_Y-1 (height from bottom of backpack upward)
    int gz = 0; // 0 .. DIM_Z-1 (depth)
    ItemOrientation orient{};

    Int3 size() const { return orient.applyToSize(itemBaseGridSize(type)); }
};

class Backpack3D {
public:
    static constexpr int DIM_X = 6;
    static constexpr int DIM_Y = 8;
    static constexpr int DIM_Z = 4;
    static constexpr float CELL_SIZE = 0.06f; // 6cm per 3D grid cell -> 0.36m x 0.48m x 0.24m backpack

    std::vector<PackedItem> items;
    int nextId = 1;

    void clear();

    bool inBounds(int gx, int gy, int gz, Int3 sz) const;
    bool overlapsAny(int gx, int gy, int gz, Int3 sz, int ignoreId = -1) const;
    // Gravity / support inside backpack: rests on bottom (gy == 0) OR on top of another item
    bool hasSupportBelow(int gx, int gy, int gz, Int3 sz, int ignoreId = -1) const;
    // Drop item straight down to lowest resting Y from startGy
    int findDropY(int gx, int gz, Int3 sz, int ignoreId = -1) const;

    bool canPlaceAt(int gx, int gy, int gz, ItemType type, const ItemOrientation& orient,
                    int ignoreId = -1) const;
    int placeAt(int gx, int gy, int gz, ItemType type, const ItemOrientation& orient);

    // Automatically tries supported placements (preferring lowest Y, testing base & rotated orientations)
    int tryAutoAdd(ItemType type);

    // Physical top-occlusion rule: true if ANY other item lies above this item's X/Z footprint
    bool isBlockedFromAbove(int itemId) const;

    const PackedItem* findById(int itemId) const;
    bool removeById(int itemId, PackedItem* outRemoved = nullptr);

    // Queries
    int countType(ItemType type) const;
    int findFirstAccessible(ItemType type) const; // Returns id of first unblocked item of type, or -1

    // Local AABB inside backpack coordinates: [0..DIM_X*CELL_SIZE, 0..DIM_Y*CELL_SIZE, 0..DIM_Z*CELL_SIZE]
    static AABB itemLocalBounds(const PackedItem& it);
    static AABB itemLocalBounds(int gx, int gy, int gz, Int3 sz);

    // Raycast in backpack-local space against packed items (returns item id or -1)
    int raycastItemsLocal(const Vec3& localOrigin, const Vec3& localDir, float maxDist,
                          float* outT = nullptr) const;
    // Raycast in backpack-local space to find a valid placement cell (gx, gy, gz) for an item
    bool raycastPlacementLocal(const Vec3& localOrigin, const Vec3& localDir, float maxDist,
                               ItemType type, const ItemOrientation& orient,
                               int& outGx, int& outGy, int& outGz) const;
};

} // namespace mega
