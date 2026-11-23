#pragma once

#include "core/Config.hpp"
#include "core/Math3D.hpp"
#include "game/Inventory3D.hpp"
#include "render/Shader.hpp"
#include <epoxy/gl.h>
#include <vector>

namespace mega {

struct RobotRenderState {
    Vec3 pos;
    float yaw = 0.0f;       // Facing direction (0 = +Z)
    Vec3 footWorldPos[2];   // World-space anchored/stepping foot positions (Left=0, Right=1)
    float pelvisOffsetY = 0.0f;
    float armAngle[2] = {0.0f, 0.0f};
    bool handIkActive[2] = {false, false};
    Vec3 handWorldPos[2]{};
    float torsoLeanX = 0.0f;
    bool alerted = false;   // false = cyan LED, true = red LED
    bool hurt = false;
};

struct DoorRenderState {
    Vec3 hingePos;
    float width = 1.0f;
    float height = 2.2f;
    float thickness = 0.08f;
    float yaw = 0.0f; // closedYaw + openSign * angle (or closedYaw for hatch)
    bool isCabinet = false;
    bool isRareCabinet = false;
    bool highlighted = false;
    bool isHatch = false;
    float hatchAngle = 0.0f; // Upward swing angle (0 = flat closed, ~1.72 = open)
};

struct ItemRenderState {
    Vec3 pos;               // World bottom-center or min corner of item
    int itemKind = 0;       // ItemType index (0..Count-1)
    ItemOrientation orient{};
    bool highlighted = false;
    float fill = 1.0f;      // 0..1 remaining fill for Water/Food
};

struct ConsumableVisualState {
    float fill = 1.0f;         // 0..1 remaining water in bottle or ration bar length
    float openProgress = 0.0f; // 0 = sealed, 1 = cap unscrewed / wrapper peeled back
    float capSpin = 0.0f;      // radians of cap rotation during unscrew
    float tiltRad = 0.0f;      // bottle tilt angle (drives liquid meniscus shift!)
    float biteImpulse = 0.0f;  // 0..1 bite feedback pulse
    bool pouring = false;      // true while water stream exits bottle neck
};

struct GhostPreviewState {
    bool active = false;
    bool valid = false;
    bool inBackpack = false;
    bool inBackSlot = false; // True when snapping to rear 2x30x6 strap slot
    ItemType type = ItemType::Food;
    ItemOrientation orient{};
    Vec3 worldPos{};        // World bottom-center when !inBackpack
    int gx = 0, gy = 0, gz = 0; // Backpack cell coords when inBackpack
};

struct BackpackRenderState {
    bool visible = false;
    Mat4 worldMatrix = Mat4::identity(); // Local [0..DIM_X*CELL, 0..DIM_Y*CELL, 0..DIM_Z*CELL] -> World
    int hoveredItemId = -1;
    bool hoveredBlocked = false;
    const Backpack3D* backpack = nullptr;
};

class RobotRenderer {
public:
    RobotRenderer() = default;
    ~RobotRenderer();

    bool init();
    void render(const Mat4& view, const Mat4& proj, const Vec3& camPos,
                const std::vector<RobotRenderState>& robots,
                const GameSettings& settings = GameSettings{});
    void renderInteractables(const Shader& worldShader, const Mat4& view, const Mat4& proj,
                             const Vec3& camPos,
                             const std::vector<DoorRenderState>& doors,
                             const std::vector<ItemRenderState>& items,
                             const BackpackRenderState& backpackState = {},
                             const GhostPreviewState& ghost = {},
                             bool hasHeldItem = false,
                             ItemType heldType = ItemType::Food,
                             const ItemOrientation& heldOrient = {},
                             const Mat4& heldMatrix = Mat4::identity(),
                             const ConsumableVisualState& heldConsumable = {});

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    Shader robotShader_;

    void drawPart(const Mat4& model, const Vec3& partScale, float texLayer, float emissive,
                  const Vec3& emissiveColor = {1.0f, 1.0f, 1.0f});
};

} // namespace mega
