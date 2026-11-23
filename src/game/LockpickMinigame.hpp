#pragma once

#include "audio/AudioEngine.hpp"
#include "core/Math3D.hpp"
#include <array>
#include <cstdint>

namespace mega {

struct LockPin {
    float pos = 0.0f;          // 0.0 = bottom rest, 1.0 = top ceiling
    float vel = 0.0f;
    float target = 0.6f;       // Random target height in [0.44 .. 0.82]
    bool pinned = false;
    bool wasStruck = false;
    bool wasTouchingPick = false;
    float strikeStartPos = 0.0f;
    bool strikeBreakRolled = false;
    float brokenTimer = 0.0f;
    float kinkDir = 1.0f;

    // Multi-coil spring wave physics (4 intermediate coil nodes)
    std::array<float, 4> coilWave{};
    std::array<float, 4> coilWaveVel{};
};

class LockpickMinigame {
public:
    bool active = false;
    int targetDoorIdx = -1;
    std::array<LockPin, 3> pins{};

    // Physical pick state (direct 1:1 mouse binding)
    float mouseTargetX = 0.5f;
    float mouseTargetY = -0.05f;
    float pickX = 0.5f;
    float pickY = -0.05f;
    float pickVelX = 0.0f;
    float pickVelY = 0.0f;
    float accumFrameDy = 0.0f;
    float breakChance = 0.05f;      // 5% per >=5% spring-lift strike on regular locks, 0% on exit door
    bool unlockedJustNow = false;
    bool pickSnappedJustNow = false;
    // Visual snap + 1.5s replacement animation
    bool snapped = false;
    float snapTimer = 0.0f;
    float snapFreezeX = 0.5f;
    float snapFreezeY = -0.05f;
    float brokenTipX = 0.5f;
    float brokenTipY = 0.0f;
    float brokenTipVelY = 0.0f;
    float brokenTipRot = 0.0f;
    int contactCount = 0;
    Vec3 lockWorldPos{};
    bool hasWorldPos = false;
    uint64_t rngState = 0xA5A5A5A512345678ULL;

    void open(int doorIdx, uint64_t lockSeed, float pickBreakChance = 0.05f,
              const Vec3* worldPos = nullptr);
    void close();
    void handleMouseMove(float dx, float dy);
    void update(float dt, AudioEngine& audio);
};

} // namespace mega
