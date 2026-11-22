#include "game/Inventory3D.hpp"
#include <algorithm>
#include <cmath>

namespace mega {

namespace {

bool rayAABB(const Vec3& orig, const Vec3& dir, const AABB& b, float maxDist, float& outT) {
    float tmin = 0.0f;
    float tmax = maxDist;
    float o[3] = {orig.x, orig.y, orig.z};
    float d[3] = {dir.x, dir.y, dir.z};
    float mn[3] = {b.min.x, b.min.y, b.min.z};
    float mx[3] = {b.max.x, b.max.y, b.max.z};

    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-6f) {
            if (o[i] < mn[i] || o[i] > mx[i]) return false;
        } else {
            float inv = 1.0f / d[i];
            float t1 = (mn[i] - o[i]) * inv;
            float t2 = (mx[i] - o[i]) * inv;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    if (tmax < 0.0f) return false;
    outT = std::max(0.0f, tmin);
    return outT <= maxDist;
}

} // namespace

Int3 itemBaseGridSize(ItemType type) {
    switch (type) {
        case ItemType::Flashlight: return {2, 2, 4}; // Flashlight: 2 x 2 x 4
        case ItemType::Food:       return {1, 1, 2}; // Energy bar: 1 x 1 x 2
        case ItemType::Water:      return {2, 3, 2}; // Water bottle: 2 x 3 x 2
        case ItemType::Battery:    return {1, 2, 1}; // Battery cell: 1 x 2 x 1
        case ItemType::Lockpick:   return {1, 1, 3}; // Lockpick tool: 1 x 1 x 3
    }
    return {1, 1, 1};
}

const char* itemDisplayName(ItemType type) {
    switch (type) {
        case ItemType::Flashlight: return "FLASHLIGHT";
        case ItemType::Battery:    return "BATTERY";
        case ItemType::Food:       return "FOOD BAR";
        case ItemType::Water:      return "WATER";
        case ItemType::Lockpick:   return "LOCKPICK";
    }
    return "ITEM";
}

Mat4 ItemOrientation::toMatrix() const {
    return Mat4::rotateY(static_cast<float>(rotY & 3) * (PI * 0.5f))
         * Mat4::rotateX(static_cast<float>(rotX & 3) * (PI * 0.5f))
         * Mat4::rotateZ(static_cast<float>(rotZ & 3) * (PI * 0.5f));
}

Int3 ItemOrientation::applyToSize(Int3 baseSize) const {
    Int3 s = baseSize;
    // Apply Z, then X, then Y (matching toMatrix() multiplication order R_y * R_x * R_z)
    if ((rotZ & 1) != 0) std::swap(s.x, s.y);
    if ((rotX & 1) != 0) std::swap(s.y, s.z);
    if ((rotY & 1) != 0) std::swap(s.x, s.z);
    return s;
}

void Backpack3D::clear() {
    items.clear();
    nextId = 1;
}

bool Backpack3D::inBounds(int gx, int gy, int gz, Int3 sz) const {
    return gx >= 0 && gy >= 0 && gz >= 0 &&
           gx + sz.x <= DIM_X &&
           gy + sz.y <= DIM_Y &&
           gz + sz.z <= DIM_Z;
}

bool Backpack3D::overlapsAny(int gx, int gy, int gz, Int3 sz, int ignoreId) const {
    for (const auto& it : items) {
        if (it.id == ignoreId) continue;
        Int3 os = it.size();
        bool ox = (gx < it.gx + os.x) && (gx + sz.x > it.gx);
        bool oy = (gy < it.gy + os.y) && (gy + sz.y > it.gy);
        bool oz = (gz < it.gz + os.z) && (gz + sz.z > it.gz);
        if (ox && oy && oz) return true;
    }
    return false;
}

bool Backpack3D::hasSupportBelow(int gx, int gy, int gz, Int3 sz, int ignoreId) const {
    if (gy == 0) return true;
    // Supported if at least one occupied voxel directly underneath (y = gy - 1) supports its X/Z footprint
    for (const auto& it : items) {
        if (it.id == ignoreId) continue;
        Int3 os = it.size();
        if (it.gy + os.y != gy) continue;
        bool ox = (gx < it.gx + os.x) && (gx + sz.x > it.gx);
        bool oz = (gz < it.gz + os.z) && (gz + sz.z > it.gz);
        if (ox && oz) return true;
    }
    return false;
}

int Backpack3D::findDropY(int gx, int gz, Int3 sz, int ignoreId) const {
    if (gx < 0 || gz < 0 || gx + sz.x > DIM_X || gz + sz.z > DIM_Z) return -1;
    // Find highest occupied cell in the footprint column, drop directly on top of it
    int restY = 0;
    for (const auto& it : items) {
        if (it.id == ignoreId) continue;
        Int3 os = it.size();
        bool ox = (gx < it.gx + os.x) && (gx + sz.x > it.gx);
        bool oz = (gz < it.gz + os.z) && (gz + sz.z > it.gz);
        if (ox && oz) {
            restY = std::max(restY, it.gy + os.y);
        }
    }
    if (restY + sz.y > DIM_Y) return -1;
    if (overlapsAny(gx, restY, gz, sz, ignoreId)) return -1;
    return restY;
}

bool Backpack3D::canPlaceAt(int gx, int gy, int gz, ItemType type, const ItemOrientation& orient,
                            int ignoreId) const {
    Int3 sz = orient.applyToSize(itemBaseGridSize(type));
    if (!inBounds(gx, gy, gz, sz)) return false;
    if (overlapsAny(gx, gy, gz, sz, ignoreId)) return false;
    if (!hasSupportBelow(gx, gy, gz, sz, ignoreId)) return false;
    return true;
}

int Backpack3D::placeAt(int gx, int gy, int gz, ItemType type, const ItemOrientation& orient) {
    if (!canPlaceAt(gx, gy, gz, type, orient)) return -1;
    PackedItem pi;
    pi.id = nextId++;
    pi.type = type;
    pi.gx = gx;
    pi.gy = gy;
    pi.gz = gz;
    pi.orient = orient;
    items.push_back(pi);
    return pi.id;
}

int Backpack3D::tryAutoAdd(ItemType type) {
    // Try natural orientation first, then rotated 90-degree orientations to pack tightly from bottom up
    static constexpr ItemOrientation CANDIDATES[6] = {
        {0, 0, 0}, {0, 1, 0}, {1, 0, 0}, {0, 0, 1}, {1, 1, 0}, {0, 1, 1}
    };
    for (int gy = 0; gy < DIM_Y; ++gy) {
        for (const auto& ori : CANDIDATES) {
            Int3 sz = ori.applyToSize(itemBaseGridSize(type));
            if (gy + sz.y > DIM_Y) continue;
            for (int gz = 0; gz <= DIM_Z - sz.z; ++gz) {
                for (int gx = 0; gx <= DIM_X - sz.x; ++gx) {
                    if (canPlaceAt(gx, gy, gz, type, ori)) {
                        return placeAt(gx, gy, gz, type, ori);
                    }
                }
            }
        }
    }
    return -1;
}

bool Backpack3D::isBlockedFromAbove(int itemId) const {
    const PackedItem* target = findById(itemId);
    if (!target) return false;
    Int3 ts = target->size();
    int topY = target->gy + ts.y;

    for (const auto& other : items) {
        if (other.id == itemId) continue;
        Int3 os = other.size();
        // Any item overlapping in X & Z that sits at or above our top face blocks pulling us out!
        if (other.gy >= topY) {
            bool ox = (target->gx < other.gx + os.x) && (target->gx + ts.x > other.gx);
            bool oz = (target->gz < other.gz + os.z) && (target->gz + ts.z > other.gz);
            if (ox && oz) return true;
        }
    }
    return false;
}

const PackedItem* Backpack3D::findById(int itemId) const {
    for (const auto& it : items) {
        if (it.id == itemId) return &it;
    }
    return nullptr;
}

bool Backpack3D::removeById(int itemId, PackedItem* outRemoved) {
    for (auto it = items.begin(); it != items.end(); ++it) {
        if (it->id == itemId) {
            if (outRemoved) *outRemoved = *it;
            items.erase(it);
            return true;
        }
    }
    return false;
}

int Backpack3D::countType(ItemType type) const {
    int n = 0;
    for (const auto& it : items) {
        if (it.type == type) n++;
    }
    return n;
}

int Backpack3D::findFirstAccessible(ItemType type) const {
    // Prefer topmost accessible item of this type
    int bestId = -1;
    int bestY = -1;
    for (const auto& it : items) {
        if (it.type != type) continue;
        if (isBlockedFromAbove(it.id)) continue;
        if (it.gy > bestY) {
            bestY = it.gy;
            bestId = it.id;
        }
    }
    return bestId;
}

AABB Backpack3D::itemLocalBounds(int gx, int gy, int gz, Int3 sz) {
    return {
        {gx * CELL_SIZE, gy * CELL_SIZE, gz * CELL_SIZE},
        {(gx + sz.x) * CELL_SIZE, (gy + sz.y) * CELL_SIZE, (gz + sz.z) * CELL_SIZE}
    };
}

AABB Backpack3D::itemLocalBounds(const PackedItem& it) {
    return itemLocalBounds(it.gx, it.gy, it.gz, it.size());
}

int Backpack3D::raycastItemsLocal(const Vec3& localOrigin, const Vec3& localDir, float maxDist,
                                  float* outT) const {
    int bestId = -1;
    float bestT = maxDist;
    for (const auto& it : items) {
        float t = 0.0f;
        if (rayAABB(localOrigin, localDir, itemLocalBounds(it), bestT, t)) {
            bestT = t;
            bestId = it.id;
        }
    }
    if (bestId >= 0 && outT) *outT = bestT;
    return bestId;
}

bool Backpack3D::raycastPlacementLocal(const Vec3& localOrigin, const Vec3& localDir, float maxDist,
                                       ItemType type, const ItemOrientation& orient,
                                       int& outGx, int& outGy, int& outGz) const {
    Int3 sz = orient.applyToSize(itemBaseGridSize(type));
    AABB innerBox{
        {0.0f, 0.0f, 0.0f},
        {DIM_X * CELL_SIZE, DIM_Y * CELL_SIZE, DIM_Z * CELL_SIZE}
    };

    float boxT = 0.0f;
    if (!rayAABB(localOrigin, localDir, innerBox, maxDist, boxT)) return false;

    // First check if ray hits an existing item inside the backpack -> stack on top of it or adjacent
    float bestT = maxDist;
    bool hitSurface = false;
    Vec3 hitPt{};

    for (const auto& it : items) {
        float t = 0.0f;
        if (rayAABB(localOrigin, localDir, itemLocalBounds(it), bestT, t)) {
            bestT = t;
            hitPt = localOrigin + localDir * t;
            hitSurface = true;
        }
    }

    // Also intersect the backpack floor plane (y = 0)
    if (std::abs(localDir.y) > 1e-5f) {
        float floorT = -localOrigin.y / localDir.y;
        if (floorT > 0.0f && floorT < bestT) {
            Vec3 fp = localOrigin + localDir * floorT;
            if (fp.x >= -0.02f && fp.x <= DIM_X * CELL_SIZE + 0.02f &&
                fp.z >= -0.02f && fp.z <= DIM_Z * CELL_SIZE + 0.02f) {
                bestT = floorT;
                hitPt = fp;
                hitSurface = true;
            }
        }
    }

    if (!hitSurface) {
        hitPt = localOrigin + localDir * boxT;
    }

    int cx = static_cast<int>(std::floor(hitPt.x / CELL_SIZE - sz.x * 0.5f + 0.5f));
    int cz = static_cast<int>(std::floor(hitPt.z / CELL_SIZE - sz.z * 0.5f + 0.5f));
    cx = std::clamp(cx, 0, std::max(0, DIM_X - sz.x));
    cz = std::clamp(cz, 0, std::max(0, DIM_Z - sz.z));

    int cy = findDropY(cx, cz, sz);
    if (cy >= 0 && canPlaceAt(cx, cy, cz, type, orient)) {
        outGx = cx;
        outGy = cy;
        outGz = cz;
        return true;
    }

    // Try tiny neighborhood shift (+-1 cell) around cursor for smooth snapping
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            int nx = std::clamp(cx + dx, 0, std::max(0, DIM_X - sz.x));
            int nz = std::clamp(cz + dz, 0, std::max(0, DIM_Z - sz.z));
            int ny = findDropY(nx, nz, sz);
            if (ny >= 0 && canPlaceAt(nx, ny, nz, type, orient)) {
                outGx = nx;
                outGy = ny;
                outGz = nz;
                return true;
            }
        }
    }
    return false;
}

} // namespace mega
