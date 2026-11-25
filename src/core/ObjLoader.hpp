#pragma once

#include "core/Math3D.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace mega {

struct ObjVertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    float r, g, b;     // Material / vertex diffuse color
    float spec = 0.18f;// Material specular strength
    float emissive = 0.0f;
};

struct ObjModelData {
    std::vector<ObjVertex> vertices;
    std::vector<uint32_t> indices;
    AABB bounds{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};

    bool empty() const { return indices.empty() || vertices.empty(); }
    size_t triangleCount() const { return indices.size() / 3; }
    Vec3 size() const { return bounds.max - bounds.min; }
};

class ObjLoader {
public:
    // Resolves a relative asset path (e.g. "assets/models/horned_rat.obj") across working/executable directories
    static std::string resolveAssetPath(const std::string& relativePath);

    // Loads a Wavefront .obj file (+ optional .mtl material library in the same directory) into CPU memory
    static bool loadFromFile(const std::string& objPath, ObjModelData& outData);
};

} // namespace mega
