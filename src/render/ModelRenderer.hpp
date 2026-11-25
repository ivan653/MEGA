#pragma once

#include "core/Config.hpp"
#include "core/Math3D.hpp"
#include "core/ObjLoader.hpp"
#include "render/Shader.hpp"
#include "world/gen/MegaGenerator.hpp"
#include <epoxy/gl.h>
#include <string>
#include <vector>

namespace mega {

struct ModelRenderInstance {
    Mat4 modelMatrix = Mat4::identity();
    Vec3 tint{1.0f, 1.0f, 1.0f};
};

// Loads Wavefront .obj + .mtl 3D models onto GPU and renders them with MEGA's pixelated 3D lighting
class ModelRenderer {
public:
    ModelRenderer() = default;
    ~ModelRenderer();

    bool init();
    bool loadModel(const std::string& objPath);
    const ObjModelData& data() const { return cpuData_; }

    void renderInstances(const Mat4& view, const Mat4& proj,
                         const Vec3& camPos, const Vec3& camDir,
                         bool flashlightOn,
                         const GameSettings& settings,
                         const std::vector<PointLight3D>& sortedLights,
                         const std::vector<ModelRenderInstance>& instances) const;

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLsizei indexCount_ = 0;
    Shader shader_;
    ObjModelData cpuData_;

    void destroyGpuMesh();
};

} // namespace mega
