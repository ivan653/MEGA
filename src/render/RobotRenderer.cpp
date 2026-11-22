#include "render/RobotRenderer.hpp"
#include "render/WorldMesher.hpp"
#include <cmath>

namespace mega {

namespace {

// Unit cube centered at (0,0,0) from -0.5 to +0.5
void buildUnitCube(std::vector<WorldVertex>& verts, std::vector<uint32_t>& indices) {
    struct Face { float nx, ny, nz; float c[4][3]; };
    static constexpr Face FACES[6] = {
        { 1, 0, 0, {{ 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f}}},
        {-1, 0, 0, {{-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f,-0.5f}}},
        { 0, 1, 0, {{-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}}},
        { 0,-1, 0, {{-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f}}},
        { 0, 0, 1, {{-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}}},
        { 0, 0,-1, {{ 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}}}
    };
    static constexpr float UVS[4][2] = {{0,1},{1,1},{1,0},{0,0}};

    for (int f = 0; f < 6; ++f) {
        uint32_t base = static_cast<uint32_t>(verts.size());
        for (int i = 0; i < 4; ++i) {
            WorldVertex v{};
            v.x = FACES[f].c[i][0];
            v.y = FACES[f].c[i][1];
            v.z = FACES[f].c[i][2];
            v.nx = FACES[f].nx;
            v.ny = FACES[f].ny;
            v.nz = FACES[f].nz;
            v.u = UVS[i][0];
            v.v = UVS[i][1];
            v.texLayer = 10.0f;
            v.ao = 0.92f;
            v.emissive = 0.0f;
            verts.push_back(v);
        }
        indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
        indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 3);
    }
}

} // namespace

RobotRenderer::~RobotRenderer() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
}

bool RobotRenderer::init() {
    std::vector<WorldVertex> verts;
    std::vector<uint32_t> indices;
    buildUnitCube(verts, indices);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(WorldVertex)),
                 verts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                 indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WorldVertex), (void*)offsetof(WorldVertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(WorldVertex), (void*)offsetof(WorldVertex, nx));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(WorldVertex), (void*)offsetof(WorldVertex, u));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(WorldVertex), (void*)offsetof(WorldVertex, ao));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(WorldVertex), (void*)offsetof(WorldVertex, emissive));
    glBindVertexArray(0);

    const std::string vs = R"(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aUVLayer;
layout (location = 3) in float aAO;

out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vUVLayer;
out float vAO;
out float vViewDist;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform float uTexLayer;
uniform vec3 uPartScale;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = normalize(mat3(uModel) * aNormal);

    // Unified 1 meter = 32 pixels texel density on every robot part
    vec2 uvScale = vec2(1.0);
    if (abs(aNormal.x) > 0.5) uvScale = vec2(uPartScale.z, uPartScale.y);
    else if (abs(aNormal.y) > 0.5) uvScale = vec2(uPartScale.x, uPartScale.z);
    else uvScale = vec2(uPartScale.x, uPartScale.y);
    vUVLayer = vec3(aUVLayer.xy * uvScale, uTexLayer);
    vAO = aAO;

    vec4 viewPos = uView * worldPos;
    vViewDist = length(viewPos.xyz);
    gl_Position = uProj * viewPos;
})";

    const std::string fs = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vUVLayer;
in float vAO;
in float vViewDist;

out vec4 FragColor;

uniform sampler2DArray uAtlas;
uniform vec3 uCamPos;
uniform float uEmissive;
uniform vec3 uEmissiveColor;
uniform float uFogDensity;

void main() {
    if (uEmissive > 0.5) {
        float fog = 1.0 - exp(-max(0.0, vViewDist - 4.0) * uFogDensity);
        vec3 col = mix(uEmissiveColor, vec3(0.014, 0.018, 0.024), clamp(fog * 0.5, 0.0, 1.0));
        FragColor = vec4(col, 1.0);
        return;
    }

    vec4 tex = texture(uAtlas, vUVLayer);
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCamPos - vWorldPos);
    vec3 L = normalize(vec3(0.3, 0.85, 0.4));

    float diff = max(dot(N, L), 0.0) * 0.55 + 0.25;
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 28.0) * 0.45;

    // Rim highlight + LED self-illumination bounce
    float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0) * 0.22;
    vec3 lit = tex.rgb * (diff * vAO + uEmissiveColor * 0.14) + vec3(spec + rim) * 0.45;

    float fog = 1.0 - exp(-max(0.0, vViewDist - 4.0) * uFogDensity);
    FragColor = vec4(mix(lit, vec3(0.014, 0.018, 0.024), clamp(fog, 0.0, 1.0)), 1.0);
})";

    return robotShader_.compile(vs, fs);
}

void RobotRenderer::drawPart(const Mat4& model, const Vec3& partScale, float texLayer,
                             float emissive, const Vec3& emissiveColor) {
    robotShader_.setMat4("uModel", model);
    robotShader_.setVec3("uPartScale", partScale);
    robotShader_.setFloat("uTexLayer", texLayer);
    robotShader_.setFloat("uEmissive", emissive);
    robotShader_.setVec3("uEmissiveColor", emissiveColor);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
}

void RobotRenderer::render(const Mat4& view, const Mat4& proj, const Vec3& camPos,
                           const std::vector<RobotRenderState>& robots,
                           const GameSettings& settings) {
    if (robots.empty()) return;

    robotShader_.use();
    robotShader_.setMat4("uView", view);
    robotShader_.setMat4("uProj", proj);
    robotShader_.setVec3("uCamPos", camPos);
    robotShader_.setInt("uAtlas", 0);
    robotShader_.setFloat("uFogDensity", settings.fogDensity);

    glBindVertexArray(vao_);

    constexpr float LAYER_BLUE = 9.0f;
    constexpr float LAYER_SKEL = 10.0f;

    float glowBoost = settings.robotLedGlowBoost * settings.objectEmissiveScale;
    for (const auto& r : robots) {
        Vec3 baseLed = r.alerted
                           ? Vec3{settings.robotAlertLedColorR, settings.robotAlertLedColorG, settings.robotAlertLedColorB}
                           : Vec3{settings.robotLedColorR, settings.robotLedColorG, settings.robotLedColorB};
        Vec3 ledColor = baseLed * glowBoost;
        if (r.hurt) ledColor = {2.5f, 2.5f, 2.5f};

        // 2.50m tall skeleton (pelvis 0.80 + headTop 0.98 = 1.78 * 1.40 = 2.49m)
        // Leg geometry: hip at 0.78 * 1.40 = 1.092m above floor;
        // thigh (0.46) + shin (0.46) = 0.92 * 1.40 = 1.288m -> max horizontal reach = 0.68m (1.36m full stride!)
        constexpr float H_SCALE = 1.40f;
        constexpr float THIGH_LEN = 0.46f * H_SCALE; // 0.644m
        constexpr float SHIN_LEN  = 0.46f * H_SCALE; // 0.644m

        auto dp = [&](const Mat4& m, const Vec3& s, float layer, float em) {
            drawPart(m * Mat4::scale(s), s * H_SCALE, layer, em, ledColor);
        };

        Mat4 root = Mat4::translate({r.pos.x, r.pos.y + r.pelvisOffsetY, r.pos.z})
                  * Mat4::rotateY(r.yaw)
                  * Mat4::scale({H_SCALE, H_SCALE, H_SCALE});
        Mat4 pelvis = root * Mat4::translate({0.0f, 0.80f, 0.0f});

        // Pelvis crossbar (dark skeleton)
        dp(pelvis, {0.34f, 0.06f, 0.12f}, LAYER_SKEL, 0.0f);

        // Torso / Spine
        Mat4 torso = pelvis * Mat4::rotateX(r.torsoLeanX);
        dp(torso * Mat4::translate({0.0f, 0.34f, 0.0f}), {0.06f, 0.66f, 0.07f}, LAYER_SKEL, 0.0f);

        // 5 horizontal rib bones
        for (int i = 0; i < 5; ++i) {
            float ry = 0.12f + i * 0.11f;
            float rw = 0.28f + (i >= 3 ? 0.08f : 0.0f);
            dp(torso * Mat4::translate({0.0f, ry, 0.0f}), {rw, 0.035f, 0.11f}, LAYER_SKEL, 0.0f);
        }

        // Shoulder bar
        Mat4 shoulders = torso * Mat4::translate({0.0f, 0.64f, 0.0f});
        dp(shoulders, {0.56f, 0.055f, 0.09f}, LAYER_SKEL, 0.0f);

        // Head + Chipped Blue Armor Mask + Glowing LED Diode
        Mat4 head = torso * Mat4::translate({0.0f, 0.84f, 0.02f});
        dp(head, {0.24f, 0.28f, 0.24f}, LAYER_BLUE, 0.0f);
        dp(torso * Mat4::translate({0.0f, 0.70f, 0.0f}), {0.05f, 0.10f, 0.05f}, LAYER_SKEL, 0.0f);
        drawPart(head * Mat4::translate({0.0f, 0.02f, 0.125f}) * Mat4::scale({0.085f, 0.085f, 0.03f}),
                 {1.0f, 1.0f, 1.0f}, LAYER_BLUE, 1.0f, ledColor);

        // Arms: idx 0 = Left (+X local), idx 1 = Right (-X local)
        float elbowBend = r.alerted ? -0.95f : -0.18f;
        for (int idx = 0; idx < 2; ++idx) {
            int localSide = (idx == 0) ? 1 : -1;
            float sx = localSide * 0.29f;
            Mat4 shoulderJoint = shoulders * Mat4::translate({sx, 0.0f, 0.0f}) * Mat4::rotateX(r.armAngle[idx]);

            dp(shoulderJoint * Mat4::translate({0.0f, -0.17f, 0.0f}), {0.13f, 0.34f, 0.13f}, LAYER_BLUE, 0.0f);

            Mat4 elbow = shoulderJoint * Mat4::translate({0.0f, -0.34f, 0.0f}) * Mat4::rotateX(elbowBend);
            dp(elbow * Mat4::translate({0.0f, -0.15f, 0.0f}), {0.045f, 0.30f, 0.045f}, LAYER_SKEL, 0.0f);
            dp(elbow * Mat4::translate({0.0f, -0.32f, 0.02f}), {0.09f, 0.10f, 0.09f}, LAYER_SKEL, 0.0f);
        }

        // Legs solved via 2-Bone Analytical IK: idx 0 = Left (+X local), idx 1 = Right (-X local)
        Vec3 fwdVec{std::sin(r.yaw), 0.0f, std::cos(r.yaw)};
        Vec3 leftVec{std::cos(r.yaw), 0.0f, -std::sin(r.yaw)}; // +X in robot local space when facing +Z

        for (int idx = 0; idx < 2; ++idx) {
            int localSide = (idx == 0) ? 1 : -1;
            float lx = localSide * 0.13f;

            Vec3 hipWorld = Vec3{r.pos.x, r.pos.y + r.pelvisOffsetY + 0.78f * H_SCALE, r.pos.z}
                          + leftVec * (lx * H_SCALE);
            Vec3 toFoot = r.footWorldPos[idx] - hipWorld;

            float fwdDot = dot(toFoot, fwdVec);
            float leftDot = dot(toFoot, leftVec);
            float downDot = -toFoot.y;

            float dist2D = std::sqrt(fwdDot * fwdDot + downDot * downDot);
            float d = clampf(dist2D, 0.22f, THIGH_LEN + SHIN_LEN - 0.015f);

            float cosKnee = clampf((THIGH_LEN * THIGH_LEN + SHIN_LEN * SHIN_LEN - d * d) /
                                   (2.0f * THIGH_LEN * SHIN_LEN), -1.0f, 1.0f);
            float kneeBend = 3.14159265f - std::acos(cosKnee);

            float cosAlpha = clampf((THIGH_LEN * THIGH_LEN + d * d - SHIN_LEN * SHIN_LEN) /
                                    (2.0f * THIGH_LEN * d), -1.0f, 1.0f);
            float alpha = std::acos(cosAlpha);

            float baseAngle = std::atan2(fwdDot, std::max(0.05f, downDot));
            float hipPitch = -(baseAngle + alpha);
            float kneePitch = kneeBend;
            float hipRoll = std::atan2(leftDot, std::max(0.25f, downDot));

            Mat4 hip = pelvis * Mat4::translate({lx, -0.02f, 0.0f})
                     * Mat4::rotateZ(hipRoll)
                     * Mat4::rotateX(hipPitch);

            dp(hip * Mat4::translate({0.0f, -0.23f, 0.0f}), {0.055f, 0.46f, 0.055f}, LAYER_SKEL, 0.0f);
            dp(hip * Mat4::translate({0.0f, -0.23f, 0.01f}), {0.14f, 0.36f, 0.15f}, LAYER_BLUE, 0.0f);

            Mat4 kneeJoint = hip * Mat4::translate({0.0f, -0.46f, 0.0f}) * Mat4::rotateX(kneePitch);
            dp(kneeJoint, {0.09f, 0.09f, 0.10f}, LAYER_SKEL, 0.0f);

            dp(kneeJoint * Mat4::translate({0.0f, -0.23f, 0.0f}), {0.048f, 0.46f, 0.048f}, LAYER_SKEL, 0.0f);
            dp(kneeJoint * Mat4::translate({0.0f, -0.23f, 0.01f}), {0.12f, 0.32f, 0.13f}, LAYER_BLUE, 0.0f);
            Mat4 footMat = kneeJoint * Mat4::translate({0.0f, -0.46f, 0.0f})
                         * Mat4::rotateX(-(hipPitch + kneePitch));
            dp(footMat * Mat4::translate({0.0f, -0.01f, 0.04f}), {0.08f, 0.05f, 0.18f}, LAYER_SKEL, 0.0f);
        }
    }

    glBindVertexArray(0);
}

void RobotRenderer::renderInteractables(const Shader& worldShader, const Mat4& view, const Mat4& proj,
                                        const Vec3& camPos,
                                        const std::vector<DoorRenderState>& doors,
                                        const std::vector<ItemRenderState>& items) {
    if (doors.empty() && items.empty()) return;
    (void)view; (void)proj; (void)camPos;

    // Render doors & items using the exact same real-time 3D world lighting shader!
    worldShader.use();
    glBindVertexArray(vao_);

    auto drawLitPart = [&](const Mat4& model, const Vec3& partScale, float texLayer,
                           float emissive, const Vec3& tint) {
        worldShader.setMat4("uModel", model);
        worldShader.setVec3("uPartScale", partScale);
        worldShader.setFloat("uTexLayerOverride", texLayer);
        worldShader.setFloat("uEmissiveOverride", emissive);
        worldShader.setVec3("uTint", tint);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
    };

    // Render hinged doors (2m metal exit door + cabinet/locker doors)
    for (const auto& d : doors) {
        Vec3 tint = d.highlighted ? Vec3{1.25f, 1.35f, 1.50f} : Vec3{1.0f, 1.0f, 1.0f};
        float layer = d.isCabinet ? 11.0f : 12.0f;
        Mat4 pivot = Mat4::translate(d.hingePos) * Mat4::rotateY(d.yaw);

        Vec3 leafSize{d.width, d.height, d.thickness};
        Mat4 leafM = pivot * Mat4::translate({d.width * 0.5f, d.height * 0.5f, 0.0f}) * Mat4::scale(leafSize);
        // Use 1:1 non-repeating door panel UVs (partScale = 1,1,1) so door pattern never duplicates!
        drawLitPart(leafM, {1.0f, 1.0f, 1.0f}, layer, 0.0f, tint);

        Vec3 handleSize{0.05f, 0.14f, d.thickness + 0.08f};
        Mat4 handleM = pivot * Mat4::translate({d.width - 0.12f, d.height * 0.48f, 0.0f}) * Mat4::scale(handleSize);
        drawLitPart(handleM, handleSize, 4.0f, 0.0f, tint);
    }

    // Render 3D world pickup items (Flashlight, Battery, Food, Water)
    for (const auto& it : items) {
        Vec3 tint = it.highlighted ? Vec3{1.35f, 1.45f, 1.55f} : Vec3{1.0f, 1.0f, 1.0f};
        Mat4 base = Mat4::translate(it.pos);
        if (it.itemKind == 0) {
            drawLitPart(base * Mat4::translate({0.0f, 0.04f, 0.0f}) * Mat4::scale({0.26f, 0.07f, 0.07f}),
                        {0.26f, 0.07f, 0.07f}, 10.0f, 0.0f, tint);
            drawLitPart(base * Mat4::translate({0.14f, 0.04f, 0.0f}) * Mat4::scale({0.06f, 0.09f, 0.09f}),
                        {0.06f, 0.09f, 0.09f}, 6.0f, 1.0f, {0.8f, 0.95f, 1.15f});
        } else if (it.itemKind == 1) {
            drawLitPart(base * Mat4::translate({0.0f, 0.06f, 0.0f}) * Mat4::scale({0.07f, 0.12f, 0.07f}),
                        {0.07f, 0.12f, 0.07f}, 3.0f, 0.0f, Vec3{1.2f, 1.0f, 0.35f} * tint.x);
            drawLitPart(base * Mat4::translate({0.0f, 0.125f, 0.0f}) * Mat4::scale({0.04f, 0.02f, 0.04f}),
                        {0.04f, 0.02f, 0.04f}, 4.0f, 0.0f, tint);
        } else if (it.itemKind == 2) {
            drawLitPart(base * Mat4::translate({0.0f, 0.07f, 0.0f}) * Mat4::scale({0.18f, 0.14f, 0.14f}),
                        {0.18f, 0.14f, 0.14f}, 3.0f, 0.0f, Vec3{0.95f, 0.65f, 0.45f} * tint.x);
        } else if (it.itemKind == 3) {
            drawLitPart(base * Mat4::translate({0.0f, 0.12f, 0.0f}) * Mat4::scale({0.09f, 0.24f, 0.09f}),
                        {0.09f, 0.24f, 0.09f}, 9.0f, 0.0f, Vec3{0.55f, 0.95f, 1.35f} * tint.x);
            drawLitPart(base * Mat4::translate({0.0f, 0.25f, 0.0f}) * Mat4::scale({0.05f, 0.04f, 0.05f}),
                        {0.05f, 0.04f, 0.05f}, 1.0f, 0.0f, tint);
        } else {
            // ItemType::Lockpick: steel L-shaped lockpick + dark grip handle
            drawLitPart(base * Mat4::translate({-0.05f, 0.015f, 0.0f}) * Mat4::scale({0.07f, 0.018f, 0.022f}),
                        {0.07f, 0.018f, 0.022f}, 10.0f, 0.0f, Vec3{0.55f, 0.65f, 0.78f} * tint.x);
            drawLitPart(base * Mat4::translate({0.04f, 0.015f, 0.0f}) * Mat4::scale({0.13f, 0.010f, 0.010f}),
                        {0.13f, 0.010f, 0.010f}, 4.0f, 0.0f, Vec3{1.20f, 1.25f, 1.32f} * tint.x);
            drawLitPart(base * Mat4::translate({0.10f, 0.015f, 0.022f}) * Mat4::scale({0.010f, 0.010f, 0.045f}),
                        {0.010f, 0.010f, 0.045f}, 4.0f, 0.0f, Vec3{1.20f, 1.25f, 1.32f} * tint.x);
        }
    }

    // Reset worldShader overrides for next frame's chunks
    worldShader.setMat4("uModel", Mat4::identity());
    worldShader.setVec3("uPartScale", {0.0f, 0.0f, 0.0f});
    worldShader.setFloat("uTexLayerOverride", -1.0f);
    worldShader.setFloat("uEmissiveOverride", -1.0f);
    worldShader.setVec3("uTint", {1.0f, 1.0f, 1.0f});
    glBindVertexArray(0);
}

} // namespace mega
