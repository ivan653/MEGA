#pragma once

#include "core/Config.hpp"
#include "core/Math3D.hpp"
#include "world/WorldGrid.hpp"
#include <vector>

namespace mega {

struct PlayerInput {
    float moveForward = 0.0f; // -1..1
    float moveRight   = 0.0f; // -1..1
    float moveUp      = 0.0f; // -1..1 (used in noclip flight & ladder)
    float yawDelta    = 0.0f;
    float pitchDelta  = 0.0f;
    bool jump         = false;
    bool sprint       = false;
    bool crouch       = false;
    float leanInput   = 0.0f; // -1 (Q left), 0 (center), +1 (E right)
    bool noclip       = false;
};

class Player {
public:
    Vec3 position{6.0f, 3.0f, 6.0f}; // Feet position (NEVER shifts laterally when peeking!)
    Vec3 velocity{0.0f, 0.0f, 0.0f};
    float yaw   = 0.0f; // Radians (0 = -Z)
    float pitch = 0.0f; // Radians
    bool grounded = false;
    bool onLadder = false;
    bool crouched = false;
    float currentHeight = cfg::PLAYER_HEIGHT;
    float currentEyeY   = cfg::PLAYER_EYE_Y;
    float leanAmount    = 0.0f; // Smoothed -1..+1
    float currentLeanOffset = 0.0f; // Wall-clamped lateral camera offset (meters)

    void update(const PlayerInput& input, const WorldGrid& world, float dt,
                const std::vector<AABB>& dynamicColliders = {},
                float moveSpeed = cfg::MOVE_SPEED,
                float sprintMult = cfg::SPRINT_MULT,
                float jumpSpeed = cfg::JUMP_SPEED,
                const std::vector<AABB>& ladderVolumes = {});

    // Un-leaned body eye point used by enemy detection / AI LOS so peeking around corners never triggers enemies!
    Vec3 detectionEyePosition() const {
        return {position.x, position.y + currentEyeY, position.z};
    }

    // Visual camera eye point including smooth Q/E corner peek offset (clamped against walls)
    Vec3 eyePosition() const {
        Vec3 r = rightDir();
        float drop = std::abs(currentLeanOffset) * 0.10f;
        return {
            position.x + r.x * currentLeanOffset,
            position.y + currentEyeY - drop,
            position.z + r.z * currentLeanOffset
        };
    }

    Vec3 forwardDir() const;
    Vec3 rightDir() const;
    Mat4 viewMatrix() const;
    AABB boundingBox() const { return boxAt(position, currentHeight); }

private:
    static AABB boxAt(const Vec3& pos, float height = cfg::PLAYER_HEIGHT);
    bool collides(const WorldGrid& world, const AABB& box,
                  const std::vector<AABB>& dynamicColliders) const;
    void moveAndSlide(const WorldGrid& world, const Vec3& delta,
                      const std::vector<AABB>& dynamicColliders);
};

} // namespace mega
