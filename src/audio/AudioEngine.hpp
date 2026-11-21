#pragma once

#include <SDL3/SDL.h>
#include <vector>

namespace mega {

enum class SoundId {
    ShootBolter,
    ShootShotgun,
    HitEnemy,
    PlayerHurt,
    Pickup,
    LevelDescend,
    RobotStep,
    RobotAlertPulse
};

class AudioEngine {
public:
    AudioEngine() = default;
    ~AudioEngine();

    bool init();
    void play(SoundId id, float volume = 1.0f);
    void update();

private:
    struct Voice {
        SoundId id;
        float phase = 0.0f;
        float phase2 = 0.0f;
        float time = 0.0f;
        float duration = 0.2f;
        float volume = 1.0f;
    };

    SDL_AudioStream* stream_ = nullptr;
    std::vector<Voice> voices_;
    float ambientPhase_ = 0.0f;
    bool initialized_ = false;
};

} // namespace mega
