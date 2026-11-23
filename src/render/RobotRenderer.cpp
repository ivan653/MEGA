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
                                        const std::vector<ItemRenderState>& items,
                                        const BackpackRenderState& backpackState,
                                        const GhostPreviewState& ghost,
                                        bool hasHeldItem,
                                        ItemType heldType,
                                        const ItemOrientation& heldOrient,
                                        const Mat4& heldMatrix,
                                        const ConsumableVisualState& heldConsumable) {
    if (doors.empty() && items.empty() && !backpackState.visible && !ghost.active && !hasHeldItem) return;
    (void)view; (void)proj; (void)camPos; (void)heldOrient;

    // Render doors, items & 3D backpack using the exact same real-time 3D world lighting shader!
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

    // Unified 3D item geometry matching exact (X, Y, Z) * CELL_SIZE grid dimensions!
    auto drawItemCentered = [&](const Mat4& centerM, ItemType type, const Vec3& tint,
                                float forceEmissive = 0.0f, const ConsumableVisualState* cons = nullptr) {
        constexpr float C = Backpack3D::CELL_SIZE;
        Int3 bsz = itemBaseGridSize(type);
        float sx = bsz.x * C;
        float sy = bsz.y * C;
        float sz = bsz.z * C;

        if (type == ItemType::Flashlight) { // 2 x 2 x 4
            drawLitPart(centerM * Mat4::translate({0.0f, 0.0f, -0.02f}) * Mat4::scale({sx * 0.65f, sy * 0.65f, sz * 0.78f}),
                        {sx * 0.65f, sy * 0.65f, sz * 0.78f}, 10.0f, forceEmissive, tint);
            drawLitPart(centerM * Mat4::translate({0.0f, 0.0f, sz * 0.38f}) * Mat4::scale({sx * 0.85f, sy * 0.85f, sz * 0.22f}),
                        {sx * 0.85f, sy * 0.85f, sz * 0.22f}, 6.0f, 1.0f, {0.85f, 0.96f, 1.15f});
        } else if (type == ItemType::Battery) { // 1 x 2 x 1
            drawLitPart(centerM * Mat4::translate({0.0f, -0.008f, 0.0f}) * Mat4::scale({sx * 0.82f, sy * 0.84f, sz * 0.82f}),
                        {sx * 0.82f, sy * 0.84f, sz * 0.82f}, 3.0f, forceEmissive, Vec3{1.25f, 1.02f, 0.35f} * tint.x);
            drawLitPart(centerM * Mat4::translate({0.0f, sy * 0.44f, 0.0f}) * Mat4::scale({sx * 0.48f, sy * 0.14f, sz * 0.48f}),
                        {sx * 0.48f, sy * 0.14f, sz * 0.48f}, 4.0f, forceEmissive, tint);
        } else if (type == ItemType::Food) { // 1 x 1 x 2 ration bar with peelable wrapper & discrete bitten bar!
            float fill = cons ? clampf(cons->fill, 0.0f, 1.0f) : 1.0f;
            float openP = cons ? clampf(cons->openProgress, 0.0f, 1.0f) : 0.0f;
            float barW = sx * 0.80f;
            float barH = sy * 0.66f;
            float barL = sz * 0.88f;

            // Lower tactical foil wrapper sleeve (stays around base half -Z..0)
            float sleeveL = barL * (0.58f - 0.14f * openP);
            float sleeveZ = -barL * 0.5f + sleeveL * 0.5f;
            Vec3 wrapTint = Vec3{0.82f, 0.64f, 0.34f} * tint.x;
            Vec3 foilTint = Vec3{1.15f, 1.18f, 1.24f} * tint.x;
            drawLitPart(centerM * Mat4::translate({0.0f, 0.0f, sleeveZ}) * Mat4::scale({barW * 1.08f, barH * 1.12f, sleeveL}),
                        {barW, barH, sleeveL}, 4.0f, forceEmissive, wrapTint);

            // Upper wrapper flaps: sealed when openP==0, peel back & fold outward along +Y/-Y as openP -> 1!
            float flapL = barL * 0.44f;
            float hingeZ = -barL * 0.5f + sleeveL;
            float peelAngle = openP * 2.15f; // Folds cleanly back ~123 degrees!
            for (int side : {1, -1}) {
                Mat4 flapPivot = centerM
                               * Mat4::translate({0.0f, side * barH * 0.52f, hingeZ})
                               * Mat4::rotateX(-side * peelAngle);
                drawLitPart(flapPivot * Mat4::translate({0.0f, 0.0f, flapL * 0.5f}) * Mat4::scale({barW * 1.06f, 0.005f, flapL}),
                            {barW, 0.005f, flapL}, 4.0f, forceEmissive, (openP > 0.35f) ? foilTint : wrapTint);
            }

            // Remaining edible oat/chocolate ration bar (shortens from +Z tip as bites are taken!)
            if (fill > 0.02f) {
                float remL = barL * (0.18f + 0.80f * fill);
                float remZ = -barL * 0.48f + remL * 0.5f;
                drawLitPart(centerM * Mat4::translate({0.0f, 0.0f, remZ}) * Mat4::scale({barW * 0.94f, barH * 0.94f, remL}),
                            {barW, barH, remL}, 3.0f, forceEmissive, Vec3{0.68f, 0.44f, 0.24f} * tint.x);
                // Lighter bitten cross-section texture on exposed front end when partially eaten
                if (fill < 0.98f || openP > 0.3f) {
                    float tipZ = -barL * 0.48f + remL;
                    drawLitPart(centerM * Mat4::translate({0.0f, 0.0f, tipZ}) * Mat4::scale({barW * 0.86f, barH * 0.84f, 0.006f}),
                                {barW, barH, 0.006f}, 3.0f, forceEmissive, Vec3{0.92f, 0.72f, 0.42f} * tint.x);
                }
            }
        } else if (type == ItemType::Water) { // 2 x 3 x 2 bottle with visible draining sloshing water + unscrewing cap!
            float fill = cons ? clampf(cons->fill, 0.0f, 1.0f) : 1.0f;
            float openP = cons ? clampf(cons->openProgress, 0.0f, 1.0f) : 0.0f;
            float capSpin = cons ? cons->capSpin : 0.0f;
            float tiltRad = cons ? cons->tiltRad : 0.0f;

            float bw = sx * 0.78f;
            float bh = sy * 0.74f;
            float bd = sz * 0.78f;
            float bodyBotY = -sy * 0.46f;
            float bodyTopY = bodyBotY + bh;
            Vec3 plasticTint = Vec3{0.78f, 0.90f, 0.98f} * tint.x;

            // Bottle base plate & 4 translucent-style corner ribs so player clearly sees water level inside!
            drawLitPart(centerM * Mat4::translate({0.0f, bodyBotY + 0.006f, 0.0f}) * Mat4::scale({bw, 0.012f, bd}),
                        {bw, 0.012f, bd}, 4.0f, forceEmissive, plasticTint);
            drawLitPart(centerM * Mat4::translate({0.0f, bodyTopY + 0.006f, 0.0f}) * Mat4::scale({bw * 0.74f, 0.014f, bd * 0.74f}),
                        {bw * 0.74f, 0.014f, bd * 0.74f}, 4.0f, forceEmissive, plasticTint);
            // Narrow cylindrical-style neck
            float neckH = sy * 0.14f;
            float neckY = bodyTopY + 0.012f + neckH * 0.5f;
            drawLitPart(centerM * Mat4::translate({0.0f, neckY, 0.0f}) * Mat4::scale({sx * 0.36f, neckH, sz * 0.36f}),
                        {sx * 0.36f, neckH, sz * 0.36f}, 4.0f, forceEmissive, plasticTint);

            // 4 corner structural ribs (leaves clear viewing windows on all 4 sides to see the water!)
            float ribT = 0.010f;
            for (float rx : {-0.46f, 0.46f}) {
                for (float rz : {-0.46f, 0.46f}) {
                    drawLitPart(centerM * Mat4::translate({rx * bw, bodyBotY + bh * 0.5f, rz * bd})
                                        * Mat4::scale({ribT, bh, ribT}),
                                {ribT, bh, ribT}, 4.0f, forceEmissive, plasticTint);
                }
            }
            // Mid-body label band on lower-middle ring
            drawLitPart(centerM * Mat4::translate({0.0f, bodyBotY + bh * 0.22f, 0.0f}) * Mat4::scale({bw * 1.02f, bh * 0.14f, bd * 1.02f}),
                        {bw, bh * 0.14f, bd}, 1.0f, forceEmissive, Vec3{0.22f, 0.58f, 0.88f} * tint.x);

            // Dynamic interior WATER volume — drains smoothly with `fill` & shifts/slants with bottle `tiltRad`!
            if (fill > 0.015f) {
                float usableH = bh - 0.022f;
                float waterH = usableH * fill;
                float tiltFactor = clampf(std::abs(tiltRad) / 1.4f, 0.0f, 1.0f);
                // As bottle tilts toward the player (+Z local), water slides up along the +Z wall toward neck!
                float slideUp = tiltFactor * (usableH - waterH) * 0.72f;
                float slideZ  = tiltFactor * (bd * 0.18f);
                float waterY  = bodyBotY + 0.012f + waterH * 0.5f + slideUp;
                float sloshRotX = -tiltRad * 0.38f * (1.0f - 0.45f * fill);

                Mat4 waterM = centerM
                            * Mat4::translate({0.0f, waterY, slideZ})
                            * Mat4::rotateX(sloshRotX)
                            * Mat4::scale({bw * 0.84f, waterH, bd * (0.84f - 0.14f * tiltFactor)});
                drawLitPart(waterM, {bw * 0.84f, waterH, bd * 0.84f}, 9.0f,
                            std::max(forceEmissive, 0.35f), Vec3{0.38f, 0.86f, 1.32f} * tint.x);

                // Brighter water meniscus shimmer line on top surface
                Mat4 surfM = centerM
                           * Mat4::translate({0.0f, waterY + waterH * 0.48f, slideZ})
                           * Mat4::rotateX(sloshRotX)
                           * Mat4::scale({bw * 0.82f, 0.006f, bd * 0.78f});
                drawLitPart(surfM, {bw * 0.82f, 0.006f, bd * 0.78f}, 6.0f, 1.0f, {0.65f, 0.96f, 1.25f});
            }

            // Animated screw cap: spins around Y & lifts off neck as openP: 0 -> 1, then moves aside while drinking!
            float capBaseY = bodyTopY + 0.012f + neckH + 0.008f;
            float liftPhase = clampf(openP * 1.45f, 0.0f, 1.0f);
            float asidePhase = clampf((openP - 0.55f) / 0.45f, 0.0f, 1.0f);
            Vec3 capPos{
                -asidePhase * sx * 0.75f,
                capBaseY + liftPhase * 0.032f - asidePhase * 0.025f,
                -asidePhase * sz * 0.25f
            };
            Mat4 capM = centerM
                      * Mat4::translate(capPos)
                      * Mat4::rotateY(capSpin)
                      * Mat4::rotateZ(asidePhase * 0.55f)
                      * Mat4::scale({sx * 0.42f, sy * 0.13f, sz * 0.42f});
            drawLitPart(capM, {sx * 0.42f, sy * 0.13f, sz * 0.42f}, 1.0f, forceEmissive, Vec3{0.26f, 0.62f, 1.05f} * tint.x);
        } else if (type == ItemType::Lockpick) { // 1 x 1 x 3 L-shaped steel pick
            drawLitPart(centerM * Mat4::translate({0.0f, 0.0f, -sz * 0.30f}) * Mat4::scale({sx * 0.40f, sy * 0.35f, sz * 0.38f}),
                        {sx * 0.40f, sy * 0.35f, sz * 0.38f}, 10.0f, forceEmissive, Vec3{0.55f, 0.68f, 0.85f} * tint.x);
            drawLitPart(centerM * Mat4::translate({0.0f, 0.0f, sz * 0.12f}) * Mat4::scale({sx * 0.18f, sy * 0.18f, sz * 0.66f}),
                        {sx * 0.18f, sy * 0.18f, sz * 0.66f}, 4.0f, forceEmissive, Vec3{1.25f, 1.30f, 1.38f} * tint.x);
            drawLitPart(centerM * Mat4::translate({sx * 0.25f, 0.0f, sz * 0.42f}) * Mat4::scale({sx * 0.50f, sy * 0.18f, sz * 0.10f}),
                        {sx * 0.50f, sy * 0.18f, sz * 0.10f}, 4.0f, forceEmissive, Vec3{1.25f, 1.30f, 1.38f} * tint.x);
        } else if (type == ItemType::Broom) { // 2 x 30 x 2 -> wooden pole + wide straw bristle head
            drawLitPart(centerM * Mat4::translate({0.0f, sy * 0.11f, 0.0f}) * Mat4::scale({0.032f, sy * 0.78f, 0.032f}),
                        {0.032f, sy * 0.78f, 0.032f}, 3.0f, forceEmissive, Vec3{0.82f, 0.58f, 0.34f} * tint.x);
            drawLitPart(centerM * Mat4::translate({0.0f, -sy * 0.29f, 0.0f}) * Mat4::scale({sx * 0.72f, sy * 0.045f, sz * 0.55f}),
                        {sx * 0.72f, sy * 0.045f, sz * 0.55f}, 10.0f, forceEmissive, Vec3{0.42f, 0.45f, 0.48f} * tint.x);
            drawLitPart(centerM * Mat4::translate({0.0f, -sy * 0.40f, 0.0f}) * Mat4::scale({sx * 1.48f, sy * 0.20f, sz * 0.72f}),
                        {sx * 1.48f, sy * 0.20f, sz * 0.72f}, 3.0f, forceEmissive, Vec3{0.96f, 0.80f, 0.38f} * tint.x);
        } else if (type == ItemType::Mop) { // 2 x 30 x 2 -> long pole + clamp + cotton mop head
            drawLitPart(centerM * Mat4::translate({0.0f, sy * 0.09f, 0.0f}) * Mat4::scale({0.032f, sy * 0.82f, 0.032f}),
                        {0.032f, sy * 0.82f, 0.032f}, 3.0f, forceEmissive, Vec3{0.72f, 0.68f, 0.60f} * tint.x);
            drawLitPart(centerM * Mat4::translate({0.0f, -sy * 0.33f, 0.0f}) * Mat4::scale({sx * 1.38f, sy * 0.04f, sz * 0.46f}),
                        {sx * 1.38f, sy * 0.04f, sz * 0.46f}, 9.0f, forceEmissive, Vec3{0.34f, 0.54f, 0.78f} * tint.x);
            drawLitPart(centerM * Mat4::translate({0.0f, -sy * 0.42f, 0.0f}) * Mat4::scale({sx * 1.56f, sy * 0.16f, sz * 0.90f}),
                        {sx * 1.56f, sy * 0.16f, sz * 0.90f}, 1.0f, forceEmissive, Vec3{0.90f, 0.90f, 0.84f} * tint.x);
        } else if (type == ItemType::Tape) { // 2 x 1 x 2 -> electrical tape roll
            drawLitPart(centerM * Mat4::scale({sx * 0.90f, sy * 0.84f, sz * 0.90f}),
                        {sx * 0.90f, sy * 0.84f, sz * 0.90f}, 9.0f, forceEmissive, Vec3{0.20f, 0.46f, 0.96f} * tint.x);
            drawLitPart(centerM * Mat4::scale({sx * 0.42f, sy * 0.90f, sz * 0.42f}),
                        {sx * 0.42f, sy * 0.90f, sz * 0.42f}, 3.0f, forceEmissive, Vec3{0.72f, 0.58f, 0.40f} * tint.x);
        } else if (type == ItemType::Knife) { // 1 x 6 x 1 -> handle + guard + steel blade
            drawLitPart(centerM * Mat4::translate({0.0f, -sy * 0.31f, 0.0f}) * Mat4::scale({sx * 0.62f, sy * 0.36f, sz * 0.52f}),
                        {sx * 0.62f, sy * 0.36f, sz * 0.52f}, 10.0f, forceEmissive, Vec3{0.32f, 0.30f, 0.28f} * tint.x);
            drawLitPart(centerM * Mat4::translate({0.0f, -sy * 0.11f, 0.0f}) * Mat4::scale({sx * 0.92f, sy * 0.05f, sz * 0.64f}),
                        {sx * 0.92f, sy * 0.05f, sz * 0.64f}, 4.0f, forceEmissive, Vec3{0.68f, 0.72f, 0.78f} * tint.x);
            drawLitPart(centerM * Mat4::translate({0.0f, sy * 0.20f, 0.0f}) * Mat4::scale({sx * 0.44f, sy * 0.58f, sz * 0.18f}),
                        {sx * 0.44f, sy * 0.58f, sz * 0.18f}, 4.0f, forceEmissive, Vec3{1.28f, 1.34f, 1.42f} * tint.x);
        } else if (type == ItemType::Stick) { // 1 x 26 x 1 -> wooden pole left after broom/mop head breaks
            drawLitPart(centerM * Mat4::scale({0.032f, sy * 0.96f, 0.032f}),
                        {0.032f, sy * 0.96f, 0.032f}, 3.0f, forceEmissive, Vec3{0.78f, 0.54f, 0.32f} * tint.x);
            drawLitPart(centerM * Mat4::translate({0.0f, -sy * 0.48f, 0.0f}) * Mat4::scale({0.022f, sy * 0.04f, 0.022f}),
                        {0.022f, sy * 0.04f, 0.022f}, 3.0f, forceEmissive, Vec3{0.92f, 0.76f, 0.54f} * tint.x);
        } else { // ItemType::Spear: 1 x 30 x 1 -> wooden pole + blue electrical tape binding + knife blade tip!
            drawLitPart(centerM * Mat4::translate({0.0f, -sy * 0.10f, 0.0f}) * Mat4::scale({0.032f, sy * 0.80f, 0.032f}),
                        {0.032f, sy * 0.80f, 0.032f}, 3.0f, forceEmissive, Vec3{0.78f, 0.54f, 0.32f} * tint.x);
            drawLitPart(centerM * Mat4::translate({0.0f, sy * 0.27f, 0.0f}) * Mat4::scale({0.046f, sy * 0.075f, 0.046f}),
                        {0.046f, sy * 0.075f, 0.046f}, 9.0f, forceEmissive, Vec3{0.20f, 0.48f, 0.98f} * tint.x);
            drawLitPart(centerM * Mat4::translate({0.0f, sy * 0.40f, 0.0f}) * Mat4::scale({0.026f, sy * 0.20f, 0.012f}),
                        {0.026f, sy * 0.20f, 0.012f}, 4.0f, forceEmissive, Vec3{1.35f, 1.40f, 1.48f} * tint.x);
        }
    };

    // 1) Render hinged doors (2m metal exit door + double-door cabinets + rare cabinets + hideable lockers)
    for (const auto& d : doors) {
        Vec3 baseTint = d.isRareCabinet ? Vec3{0.62f, 0.70f, 0.82f} : Vec3{1.0f, 1.0f, 1.0f};
        Vec3 tint = d.highlighted ? (baseTint * 1.32f) : baseTint;
        float layer = d.isCabinet ? 11.0f : 12.0f;
        Mat4 pivot = Mat4::translate(d.hingePos) * Mat4::rotateY(d.yaw);

        Vec3 leafSize{d.width, d.height, d.thickness};
        Mat4 leafM = pivot * Mat4::translate({d.width * 0.5f, d.height * 0.5f, 0.0f}) * Mat4::scale(leafSize);
        drawLitPart(leafM, {1.0f, 1.0f, 1.0f}, layer, 0.0f, tint);

        // Handle placed near latch edge so double-door cabinets show two handles meeting at center!
        Vec3 handleSize{0.045f, 0.14f, d.thickness + 0.075f};
        float handleX = std::max(0.07f, d.width - 0.08f);
        Mat4 handleM = pivot * Mat4::translate({handleX, d.height * 0.48f, 0.0f}) * Mat4::scale(handleSize);
        Vec3 handleTint = d.isRareCabinet ? Vec3{1.35f, 1.12f, 0.52f} : tint;
        drawLitPart(handleM, handleSize, 4.0f, 0.0f, handleTint);

        if (d.isRareCabinet) {
            // Reinforced hazard/gold trim strip on rare 3x4 room cabinets
            Vec3 trimSize{d.width * 0.86f, 0.06f, d.thickness + 0.02f};
            Mat4 trimM = pivot * Mat4::translate({d.width * 0.5f, d.height * 0.82f, 0.0f}) * Mat4::scale(trimSize);
            drawLitPart(trimM, trimSize, 7.0f, 0.0f, tint);
        }
    }

    // 2) Render 3D world pickup items with their exact 3D orientation & dimensions
    for (const auto& it : items) {
        Vec3 tint = it.highlighted ? Vec3{1.40f, 1.50f, 1.65f} : Vec3{1.0f, 1.0f, 1.0f};
        ItemType t = static_cast<ItemType>(std::clamp(it.itemKind, 0, ITEM_TYPE_COUNT - 1));
        Int3 osz = it.orient.applyToSize(itemBaseGridSize(t));
        float cy = osz.y * Backpack3D::CELL_SIZE * 0.5f;
        Mat4 centerM = Mat4::translate({it.pos.x, it.pos.y + cy, it.pos.z}) * it.orient.toMatrix();
        ConsumableVisualState wCons;
        wCons.fill = it.fill;
        if (t == ItemType::Food && it.fill < 0.99f) wCons.openProgress = 0.75f;
        drawItemCentered(centerM, t, tint, 0.0f, &wCons);
    }

    // 3) Render Diegetic 3D Backpack (6 x 8 x 4 cavity + rear 2 x 30 x 6 dual-strap slot!)
    if (backpackState.visible && backpackState.backpack) {
        constexpr float C = Backpack3D::CELL_SIZE;
        constexpr float W = Backpack3D::DIM_X * C;
        constexpr float H = Backpack3D::DIM_Y * C;
        constexpr float D = Backpack3D::DIM_Z * C;
        constexpr float WT = 0.014f;
        const Mat4& B = backpackState.worldMatrix;
        Vec3 shellTint{0.42f, 0.48f, 0.40f}; // Olive-slate tactical pack
        Vec3 trimTint{0.25f, 0.30f, 0.34f};
        Vec3 strapTint{0.22f, 0.20f, 0.18f};
        Vec3 buckleTint{0.78f, 0.80f, 0.84f};

        // Bottom, back, left, right, and slightly lower front lip so interior layers are clearly visible
        drawLitPart(B * Mat4::translate({W * 0.5f, -WT * 0.5f, D * 0.5f}) * Mat4::scale({W + WT * 2, WT, D + WT * 2}),
                    {W, WT, D}, 3.0f, 0.0f, shellTint);
        drawLitPart(B * Mat4::translate({W * 0.5f, H * 0.5f, -WT * 0.5f}) * Mat4::scale({W + WT * 2, H, WT}),
                    {W, H, WT}, 3.0f, 0.0f, shellTint);
        drawLitPart(B * Mat4::translate({-WT * 0.5f, H * 0.5f, D * 0.5f}) * Mat4::scale({WT, H, D}),
                    {WT, H, D}, 3.0f, 0.0f, shellTint);
        drawLitPart(B * Mat4::translate({W + WT * 0.5f, H * 0.5f, D * 0.5f}) * Mat4::scale({WT, H, D}),
                    {WT, H, D}, 3.0f, 0.0f, shellTint);
        drawLitPart(B * Mat4::translate({W * 0.5f, H * 0.34f, D + WT * 0.5f}) * Mat4::scale({W + WT * 2, H * 0.68f, WT}),
                    {W, H * 0.68f, WT}, 3.0f, 0.0f, trimTint);

        // Rear 2 x 30 x 6 dual-strap attachment harness on the back spine (z = BACK_OFFSET_Z .. -WT)
        constexpr float BZ = Backpack3D::BACK_OFFSET_Z;
        constexpr float BD = Backpack3D::BACK_DIM_Z * C;
        for (float strapY : {H * 0.24f, H * 0.76f}) {
            // Outer horizontal tightening strap band
            drawLitPart(B * Mat4::translate({W * 0.5f, strapY, BZ - 0.006f}) * Mat4::scale({W + 0.012f, 0.028f, 0.010f}),
                        {W, 0.028f, 0.010f}, 10.0f, 0.0f, strapTint);
            // Left and right strap side loops connecting back to the pack spine
            drawLitPart(B * Mat4::translate({-0.004f, strapY, BZ + BD * 0.5f}) * Mat4::scale({0.010f, 0.028f, BD + 0.014f}),
                        {0.010f, 0.028f, BD}, 10.0f, 0.0f, strapTint);
            drawLitPart(B * Mat4::translate({W + 0.004f, strapY, BZ + BD * 0.5f}) * Mat4::scale({0.010f, 0.028f, BD + 0.014f}),
                        {0.010f, 0.028f, BD}, 10.0f, 0.0f, strapTint);
            // Metallic quick-release buckle in center of strap
            drawLitPart(B * Mat4::translate({W * 0.5f, strapY, BZ - 0.011f}) * Mat4::scale({0.042f, 0.036f, 0.010f}),
                        {0.042f, 0.036f, 0.010f}, 4.0f, 0.0f, buckleTint);
        }
        // Subtle glowing rear slot guide rails so player clearly sees the 6-wide x 2-deep back harness grid
        for (int gx = 0; gx <= Backpack3D::BACK_DIM_X; gx += 2) {
            drawLitPart(B * Mat4::translate({gx * C, H * 0.5f, BZ + 0.004f}) * Mat4::scale({0.003f, H * 0.88f, 0.003f}),
                        {0.01f, H, 0.01f}, 6.0f, 1.0f, {0.26f, 0.44f, 0.52f});
        }

        // Subtle 3D floor grid guide lines (6x4 grid cells) so player can clearly align items
        for (int gx = 1; gx < Backpack3D::DIM_X; ++gx) {
            drawLitPart(B * Mat4::translate({gx * C, 0.002f, D * 0.5f}) * Mat4::scale({0.003f, 0.003f, D}),
                        {0.01f, 0.01f, D}, 6.0f, 1.0f, {0.22f, 0.36f, 0.44f});
        }
        for (int gz = 1; gz < Backpack3D::DIM_Z; ++gz) {
            drawLitPart(B * Mat4::translate({W * 0.5f, 0.002f, gz * C}) * Mat4::scale({W, 0.003f, 0.003f}),
                        {W, 0.01f, 0.01f}, 6.0f, 1.0f, {0.22f, 0.36f, 0.44f});
        }

        // Render every 3D item packed inside the backpack (main cavity AND rear 2x30x6 strap slot!)
        for (const auto& pi : backpackState.backpack->items) {
            AABB lb = Backpack3D::itemLocalBounds(pi);
            Vec3 localCenter = (lb.min + lb.max) * 0.5f;
            bool isHov = (pi.id == backpackState.hoveredItemId);
            bool isBlk = backpackState.backpack->isBlockedFromAbove(pi.id);
            Vec3 tint = {1.15f, 1.15f, 1.15f};
            float em = 0.0f;
            if (isHov) {
                tint = isBlk ? Vec3{1.9f, 0.45f, 0.40f} : Vec3{1.65f, 1.95f, 2.15f};
                em = 1.0f;
            }
            Mat4 itemM = B * Mat4::translate(localCenter) * pi.orient.toMatrix();
            ConsumableVisualState pCons;
            if ((pi.type == ItemType::Water || pi.type == ItemType::Food) && pi.durability > 0) {
                pCons.fill = clampf(static_cast<float>(pi.durability) / 100.0f, 0.02f, 1.0f);
                if (pi.type == ItemType::Food && pCons.fill < 0.99f) pCons.openProgress = 0.75f;
            }
            drawItemCentered(itemM, pi.type, tint, em, &pCons);
        }
    }

    // 4) Render 3D Grid Placement Ghost Preview (either inside backpack/back slot or on world floor/table/shelf)
    if (ghost.active) {
        constexpr float C = Backpack3D::CELL_SIZE;
        Int3 osz = ghost.orient.applyToSize(itemBaseGridSize(ghost.type));
        Vec3 gCol = ghost.valid ? Vec3{0.35f, 1.35f, 1.55f} : Vec3{1.55f, 0.35f, 0.35f};
        Vec3 gScale{osz.x * C * 0.96f, osz.y * C * 0.96f, osz.z * C * 0.96f};

        Mat4 gCenterM;
        if (ghost.inBackpack && backpackState.visible) {
            AABB lb = Backpack3D::itemLocalBounds(ghost.gx, ghost.gy, ghost.gz, osz, ghost.inBackSlot);
            Vec3 lc = (lb.min + lb.max) * 0.5f;
            gCenterM = backpackState.worldMatrix * Mat4::translate(lc);
        } else {
            gCenterM = Mat4::translate({ghost.worldPos.x, ghost.worldPos.y + osz.y * C * 0.5f, ghost.worldPos.z});
        }
        drawLitPart(gCenterM * Mat4::scale(gScale), gScale, 6.0f, 1.0f, gCol * 0.55f);
        drawItemCentered(gCenterM * ghost.orient.toMatrix(), ghost.type, gCol, 1.0f);
    }

    // 5) Render Item Currently Held in Player's Right Hand (organic fixed pose + procedural consumption animation!)
    if (hasHeldItem) {
        drawItemCentered(heldMatrix, heldType, {1.15f, 1.15f, 1.15f}, 0.0f, &heldConsumable);
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
