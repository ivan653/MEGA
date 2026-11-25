#include "render/ModelRenderer.hpp"
#include <algorithm>
#include <cstddef>

namespace mega {

namespace {

static const char* MODEL_VS = R"(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec3 aColor;
layout (location = 4) in float aSpec;
layout (location = 5) in float aEmissive;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vColor;
out float vSpec;
out float vEmissive;
out float vViewDist;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = normalize(mat3(uModel) * aNormal);
    vUV = aUV;
    vColor = aColor;
    vSpec = aSpec;
    vEmissive = aEmissive;

    vec4 viewPos = uView * worldPos;
    vViewDist = length(viewPos.xyz);
    gl_Position = uProj * viewPos;
}
)";

static const char* MODEL_FS = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec3 vColor;
in float vSpec;
in float vEmissive;
in float vViewDist;

out vec4 FragColor;

uniform vec3 uCamPos;
uniform vec3 uCamDir;
uniform float uFlashlight;
uniform vec3 uTint;

uniform float uAmbientBrightness;
uniform float uFlashlightIntensity;
uniform float uFlashlightRadius;
uniform vec3 uFlashlightColor;
uniform float uObjectEmissiveScale;
uniform float uFogDensity;
uniform float uLightSteps;
uniform float uLightDitherStrength;

const int MAX_LIGHTS = 48;
uniform int uLightCount;
uniform vec4 uLightPosRadius[MAX_LIGHTS];
uniform vec4 uLightColorIntensity[MAX_LIGHTS];

float bayer2x2(vec2 p) {
    int x = int(mod(p.x, 2.0));
    int y = int(mod(p.y, 2.0));
    int idx = y * 2 + x;
    float m[4] = float[4](-0.375, 0.125, 0.375, -0.125);
    return m[idx];
}

void main() {
    if (vEmissive > 0.5) {
        vec3 emCol = vColor * uTint * 1.35 * uObjectEmissiveScale;
        float fog = 1.0 - exp(-max(0.0, vViewDist - 4.0) * uFogDensity);
        FragColor = vec4(mix(emCol, vec3(0.014, 0.018, 0.024), clamp(fog, 0.0, 1.0)), 1.0);
        return;
    }

    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCamPos - vWorldPos);

    // Subtle procedural micro-fur grain at 64px/m so organic surfaces match MEGA's crisp pixel aesthetic
    vec3 gridCell = floor(vWorldPos * 128.0);
    float grain = fract(sin(dot(gridCell, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
    vec3 baseAlbedo = vColor * uTint * (0.94 + 0.12 * grain);

    vec3 totalDiffuse = vec3(0.85, 0.92, 1.0) * max(uAmbientBrightness * 1.45, 0.045);
    vec3 totalSpec = vec3(0.0);

    bool fragUpper = (vWorldPos.y > 0.25);
    for (int i = 0; i < uLightCount; ++i) {
        vec3 lPos = uLightPosRadius[i].xyz;
        bool lightUpper = (lPos.y > 0.25);
        if (fragUpper != lightUpper) continue;

        float radius = uLightPosRadius[i].w;
        vec3 toLight = lPos - vWorldPos;
        float distSq = dot(toLight, toLight);
        if (distSq > radius * radius) continue;

        float dist = sqrt(distSq);
        vec3 L = toLight / max(dist, 0.001);
        float normDist = dist / radius;
        float window = clamp(1.0 - normDist * normDist, 0.0, 1.0);
        float atten = (window * window) / (1.0 + 0.20 * dist + 0.08 * distSq);

        vec3 lCol = uLightColorIntensity[i].rgb * uLightColorIntensity[i].w;
        float ndotl = max(dot(N, L), 0.0);
        float wrapDiff = max(dot(N, L) * 0.65 + 0.35, 0.0) * 0.45 + ndotl * 0.65;
        totalDiffuse += lCol * wrapDiff * atten;

        if (ndotl > 0.0) {
            vec3 H = normalize(L + V);
            float spec = pow(max(dot(N, H), 0.0), 28.0) * vSpec;
            totalSpec += lCol * spec * atten;
        }
    }

    if (uFlashlight > 0.5) {
        vec3 toCam = uCamPos - vWorldPos;
        float fDist = length(toCam);
        if (fDist < uFlashlightRadius) {
            vec3 L = toCam / max(fDist, 0.001);
            float spotCos = dot(-L, normalize(uCamDir));
            float cone = smoothstep(0.84, 0.955, spotCos);
            if (cone > 0.0) {
                float fAtten = cone * clamp(1.0 - fDist / uFlashlightRadius, 0.0, 1.0)
                               / (1.0 + 0.08 * fDist + 0.015 * fDist * fDist);
                vec3 fCol = uFlashlightColor * uFlashlightIntensity;
                float ndotl = max(dot(N, L), 0.0);
                float wrapF = max(dot(N, L) * 0.55 + 0.45, 0.0) * 0.30 + ndotl * 0.75;
                totalDiffuse += fCol * wrapF * fAtten;

                vec3 H = normalize(L + V);
                float spec = pow(max(dot(N, H), 0.0), 32.0) * vSpec;
                totalSpec += fCol * spec * fAtten;
            }
        }
    }

    if (uLightSteps > 1.0) {
        float dither = bayer2x2(floor(gl_FragCoord.xy * 0.5)) * uLightDitherStrength;
        float diffLum = max(max(totalDiffuse.r, totalDiffuse.g), totalDiffuse.b);
        if (diffLum > 0.0001) {
            float qLum = max(0.0, floor(diffLum * uLightSteps + dither + 0.5) / uLightSteps);
            totalDiffuse *= (qLum / diffLum);
        }
    }

    vec3 litColor = baseAlbedo * totalDiffuse + totalSpec;
    float fogFactor = 1.0 - exp(-max(0.0, vViewDist - 4.0) * uFogDensity);
    vec3 finalCol = mix(litColor, vec3(0.014, 0.018, 0.024), clamp(fogFactor, 0.0, 1.0));
    FragColor = vec4(finalCol, 1.0);
}
)";

} // namespace

ModelRenderer::~ModelRenderer() {
    destroyGpuMesh();
}

void ModelRenderer::destroyGpuMesh() {
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (ebo_) { glDeleteBuffers(1, &ebo_); ebo_ = 0; }
    indexCount_ = 0;
}

bool ModelRenderer::init() {
    return shader_.compile(MODEL_VS, MODEL_FS);
}

bool ModelRenderer::loadModel(const std::string& objPath) {
    if (!ObjLoader::loadFromFile(objPath, cpuData_)) {
        return false;
    }

    if (vao_ == 0) {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ebo_);
    }

    indexCount_ = static_cast<GLsizei>(cpuData_.indices.size());
    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(cpuData_.vertices.size() * sizeof(ObjVertex)),
                 cpuData_.vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(cpuData_.indices.size() * sizeof(uint32_t)),
                 cpuData_.indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, nx));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, u));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, r));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, spec));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, emissive));

    glBindVertexArray(0);
    return true;
}

void ModelRenderer::renderInstances(const Mat4& view, const Mat4& proj,
                                    const Vec3& camPos, const Vec3& camDir,
                                    bool flashlightOn,
                                    const GameSettings& settings,
                                    const std::vector<PointLight3D>& sortedLights,
                                    const std::vector<ModelRenderInstance>& instances) const {
    if (vao_ == 0 || indexCount_ <= 0 || instances.empty()) return;

    shader_.use();
    shader_.setMat4("uView", view);
    shader_.setMat4("uProj", proj);
    shader_.setVec3("uCamPos", camPos);
    shader_.setVec3("uCamDir", camDir);
    shader_.setFloat("uFlashlight", flashlightOn ? 1.0f : 0.0f);
    shader_.setFloat("uAmbientBrightness", settings.ambientBrightness);
    shader_.setFloat("uFlashlightIntensity", settings.flashlightIntensity);
    shader_.setFloat("uFlashlightRadius", settings.flashlightRadius);
    shader_.setVec3("uFlashlightColor", {settings.flashlightColorR, settings.flashlightColorG, settings.flashlightColorB});
    shader_.setFloat("uObjectEmissiveScale", settings.objectEmissiveScale);
    shader_.setFloat("uFogDensity", settings.fogDensity);
    shader_.setFloat("uLightSteps", settings.lightSteps);
    shader_.setFloat("uLightDitherStrength", settings.lightDitherStrength);

    int count = std::min(48, static_cast<int>(sortedLights.size()));
    shader_.setInt("uLightCount", count);
    if (count > 0) {
        std::vector<float> posRad(static_cast<size_t>(count * 4));
        std::vector<float> colInt(static_cast<size_t>(count * 4));
        for (int i = 0; i < count; ++i) {
            posRad[i * 4 + 0] = sortedLights[i].pos.x;
            posRad[i * 4 + 1] = sortedLights[i].pos.y;
            posRad[i * 4 + 2] = sortedLights[i].pos.z;
            posRad[i * 4 + 3] = sortedLights[i].radius;

            colInt[i * 4 + 0] = sortedLights[i].color.x;
            colInt[i * 4 + 1] = sortedLights[i].color.y;
            colInt[i * 4 + 2] = sortedLights[i].color.z;
            colInt[i * 4 + 3] = sortedLights[i].intensity;
        }
        glUniform4fv(glGetUniformLocation(shader_.id(), "uLightPosRadius"), count, posRad.data());
        glUniform4fv(glGetUniformLocation(shader_.id(), "uLightColorIntensity"), count, colInt.data());
    }

    glBindVertexArray(vao_);
    for (const auto& inst : instances) {
        shader_.setMat4("uModel", inst.modelMatrix);
        shader_.setVec3("uTint", inst.tint);
        glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
}

} // namespace mega
