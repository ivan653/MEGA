#include "core/ObjLoader.hpp"
#include <SDL3/SDL_filesystem.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace mega {

namespace {

struct MaterialInfo {
    Vec3 kd{0.72f, 0.72f, 0.72f};
    float ks = 0.18f;
    float emissive = 0.0f;
};

void loadMtlLibrary(const std::filesystem::path& mtlPath,
                    std::unordered_map<std::string, MaterialInfo>& outMtls) {
    std::ifstream in(mtlPath);
    if (!in.good()) return;

    std::string curName;
    std::string line;
    while (std::getline(in, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        std::istringstream iss(line);
        std::string cmd;
        if (!(iss >> cmd)) continue;
        if (cmd == "newmtl") {
            iss >> curName;
            if (!curName.empty() && outMtls.find(curName) == outMtls.end()) {
                outMtls[curName] = MaterialInfo{};
            }
        } else if (!curName.empty()) {
            auto& m = outMtls[curName];
            if (cmd == "Kd") {
                iss >> m.kd.x >> m.kd.y >> m.kd.z;
            } else if (cmd == "Ks") {
                float sx = 0.0f, sy = 0.0f, sz = 0.0f;
                if (iss >> sx >> sy >> sz) {
                    m.ks = std::max({sx, sy, sz});
                }
            } else if (cmd == "Ke") {
                float ex = 0.0f, ey = 0.0f, ez = 0.0f;
                if (iss >> ex >> ey >> ez) {
                    if (std::max({ex, ey, ez}) > 0.05f) m.emissive = 1.0f;
                }
            }
        }
    }
}

struct FaceCorner {
    int vIdx = -1;
    int vtIdx = -1;
    int vnIdx = -1;
};

FaceCorner parseCorner(const std::string& tok, int vCount, int vtCount, int vnCount) {
    FaceCorner fc;
    size_t s1 = tok.find('/');
    auto fixIdx = [](int raw, int total) -> int {
        if (raw > 0) return raw - 1;
        if (raw < 0) return total + raw;
        return -1;
    };
    if (s1 == std::string::npos) {
        fc.vIdx = fixIdx(std::atoi(tok.c_str()), vCount);
        return fc;
    }
    fc.vIdx = fixIdx(std::atoi(tok.substr(0, s1).c_str()), vCount);
    size_t s2 = tok.find('/', s1 + 1);
    if (s2 == std::string::npos) {
        std::string sub = tok.substr(s1 + 1);
        if (!sub.empty()) fc.vtIdx = fixIdx(std::atoi(sub.c_str()), vtCount);
    } else {
        std::string subVt = tok.substr(s1 + 1, s2 - s1 - 1);
        std::string subVn = tok.substr(s2 + 1);
        if (!subVt.empty()) fc.vtIdx = fixIdx(std::atoi(subVt.c_str()), vtCount);
        if (!subVn.empty()) fc.vnIdx = fixIdx(std::atoi(subVn.c_str()), vnCount);
    }
    return fc;
}

} // namespace

std::string ObjLoader::resolveAssetPath(const std::string& relativePath) {
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates = {
        fs::path(relativePath),
        fs::path("..") / relativePath,
        fs::path("../..") / relativePath
    };
    if (const char* base = SDL_GetBasePath()) {
        fs::path bp(base);
        candidates.push_back(bp / relativePath);
        candidates.push_back(bp / ".." / relativePath);
    }
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c, ec) && fs::is_regular_file(c, ec)) {
            return c.string();
        }
    }
    return relativePath;
}

bool ObjLoader::loadFromFile(const std::string& objPath, ObjModelData& outData) {
    outData = ObjModelData{};
    std::string resolved = resolveAssetPath(objPath);
    std::ifstream in(resolved);
    if (!in.good()) return false;

    std::filesystem::path baseDir = std::filesystem::path(resolved).parent_path();
    std::unordered_map<std::string, MaterialInfo> materials;
    MaterialInfo currentMtl{};

    std::vector<Vec3> positions;
    std::vector<Vec3> vColors;
    std::vector<bool> hasVColor;
    std::vector<std::pair<float, float>> texcoords;
    std::vector<Vec3> normals;

    positions.reserve(4096);
    texcoords.reserve(4096);
    normals.reserve(4096);

    outData.bounds.min = {1e30f, 1e30f, 1e30f};
    outData.bounds.max = {-1e30f, -1e30f, -1e30f};

    std::string line;
    std::vector<FaceCorner> polygon;
    polygon.reserve(8);

    while (std::getline(in, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        std::istringstream iss(line);
        std::string cmd;
        if (!(iss >> cmd)) continue;

        if (cmd == "mtllib") {
            std::string mtlFile;
            if (iss >> mtlFile) {
                loadMtlLibrary(baseDir / mtlFile, materials);
            }
        } else if (cmd == "usemtl") {
            std::string mName;
            if (iss >> mName) {
                auto it = materials.find(mName);
                if (it != materials.end()) currentMtl = it->second;
            }
        } else if (cmd == "v") {
            Vec3 p{};
            if (iss >> p.x >> p.y >> p.z) {
                positions.push_back(p);
                outData.bounds.min.x = std::min(outData.bounds.min.x, p.x);
                outData.bounds.min.y = std::min(outData.bounds.min.y, p.y);
                outData.bounds.min.z = std::min(outData.bounds.min.z, p.z);
                outData.bounds.max.x = std::max(outData.bounds.max.x, p.x);
                outData.bounds.max.y = std::max(outData.bounds.max.y, p.y);
                outData.bounds.max.z = std::max(outData.bounds.max.z, p.z);

                Vec3 vc{1.0f, 1.0f, 1.0f};
                if (iss >> vc.x >> vc.y >> vc.z) {
                    vColors.push_back(vc);
                    hasVColor.push_back(true);
                } else {
                    vColors.push_back(vc);
                    hasVColor.push_back(false);
                }
            }
        } else if (cmd == "vt") {
            float u = 0.0f, v = 0.0f;
            iss >> u >> v;
            texcoords.push_back({u, v});
        } else if (cmd == "vn") {
            Vec3 n{};
            if (iss >> n.x >> n.y >> n.z) {
                normals.push_back(normalize(n));
            }
        } else if (cmd == "f") {
            polygon.clear();
            std::string tok;
            while (iss >> tok) {
                FaceCorner fc = parseCorner(tok,
                                            static_cast<int>(positions.size()),
                                            static_cast<int>(texcoords.size()),
                                            static_cast<int>(normals.size()));
                if (fc.vIdx < 0 || fc.vIdx >= static_cast<int>(positions.size())) continue;
                polygon.push_back(fc);
            }
            if (polygon.size() < 3) continue;

            // Compute fallback flat normal in case .obj face omits vn
            Vec3 p0 = positions[polygon[0].vIdx];
            Vec3 p1 = positions[polygon[1].vIdx];
            Vec3 p2 = positions[polygon[2].vIdx];
            Vec3 faceN = cross(p1 - p0, p2 - p0);
            if (lengthSq(faceN) > 1e-12f) faceN = normalize(faceN);
            else faceN = {0.0f, 1.0f, 0.0f};

            auto emitVert = [&](const FaceCorner& fc) -> uint32_t {
                const Vec3& pos = positions[fc.vIdx];
                Vec3 nrm = (fc.vnIdx >= 0 && fc.vnIdx < static_cast<int>(normals.size()))
                               ? normals[fc.vnIdx]
                               : faceN;
                float u = 0.0f, v = 0.0f;
                if (fc.vtIdx >= 0 && fc.vtIdx < static_cast<int>(texcoords.size())) {
                    u = texcoords[fc.vtIdx].first;
                    v = texcoords[fc.vtIdx].second;
                }
                Vec3 col = hasVColor[fc.vIdx] ? vColors[fc.vIdx] : currentMtl.kd;
                ObjVertex ov{
                    pos.x, pos.y, pos.z,
                    nrm.x, nrm.y, nrm.z,
                    u, v,
                    col.x, col.y, col.z,
                    currentMtl.ks,
                    currentMtl.emissive
                };
                uint32_t idx = static_cast<uint32_t>(outData.vertices.size());
                outData.vertices.push_back(ov);
                return idx;
            };

            uint32_t i0 = emitVert(polygon[0]);
            uint32_t prev = emitVert(polygon[1]);
            for (size_t k = 2; k < polygon.size(); ++k) {
                uint32_t cur = emitVert(polygon[k]);
                outData.indices.push_back(i0);
                outData.indices.push_back(prev);
                outData.indices.push_back(cur);
                prev = cur;
            }
        }
    }

    if (outData.empty()) {
        outData.bounds = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
        return false;
    }
    return true;
}

} // namespace mega
