#pragma once

#include "audio/AudioEngine.hpp"
#include "core/Math3D.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace mega {

struct MossFallingChunk {
    float x = 0.5f;   // Normalized [0..1] in patch canvas
    float y = 0.5f;   // Normalized [0..1+] (falls below 1.0 as it drops!)
    float vx = 0.0f;
    float vy = 0.0f;
    float size = 6.0f; // Pixel radius/width (mix of crumbs and chunks!)
    float rot = 0.0f;
    float vrot = 0.0f;
    uint8_t shade = 1; // 0..2 moss green shade
    float life = 1.0f;
};

class MossMinigame {
public:
    static constexpr int GRID_W = 44;
    static constexpr int GRID_H = 44;

    bool active = false;
    int targetMossIdx = -1;
    // 0 = bare wall, 1..3 = moss thickness/shade
    std::array<uint8_t, GRID_W * GRID_H> cells{};
    int initialMossCells = 0;
    int remainingMossCells = 0;

    // Knife state in normalized [0..1] patch space (blade center/edge)
    float targetX = 0.72f;
    float targetY = 0.68f;
    float knifeX = 0.72f;
    float knifeY = 0.68f;
    bool scrapingHeld = false;      // True while holding LMB
    bool completedJustNow = false;
    int cellsScrapedThisTick = 0;   // Used by GameWorld to emit 3D wall moss particles
    float finishTimer = 0.0f;
    bool finishing = false;

    Vec3 patchWorldPos{};
    Vec3 patchNormal{0.0f, 0.0f, 1.0f};
    bool hasWorldPos = false;
    uint64_t rngState = 0x4D4F5353ULL;

    std::vector<MossFallingChunk> chunks;

    void open(int mossIdx, uint64_t seed, const Vec3* worldPos = nullptr, const Vec3* normal = nullptr);
    void close();
    void handleMouseMove(float dx, float dy);
    void update(float dt, AudioEngine& audio);
    float progress() const;
};

} // namespace mega
