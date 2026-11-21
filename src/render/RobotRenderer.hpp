#pragma once

#include "core/Math3D.hpp"
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
    float torsoLeanX = 0.0f;
    bool alerted = false;   // false = cyan LED, true = red LED
    bool hurt = false;
};

class RobotRenderer {
public:
    RobotRenderer() = default;
    ~RobotRenderer();

    bool init();
    void render(const Mat4& view, const Mat4& proj, const Vec3& camPos,
                const std::vector<RobotRenderState>& robots);

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    Shader robotShader_; // Shares lighting uniforms or renders with model matrix

    void drawPart(const Mat4& model, const Vec3& partScale, float texLayer, float emissive,
                  const Vec3& emissiveColor = {1.0f, 1.0f, 1.0f});
};

} // namespace mega
