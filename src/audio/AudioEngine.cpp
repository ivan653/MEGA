#include "audio/AudioEngine.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mega {

AudioEngine::~AudioEngine() {
    if (stream_) SDL_DestroyAudioStream(stream_);
}

bool AudioEngine::init() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return false;
    }

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 1;
    spec.freq = 22050;

    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream_) return false;
    SDL_ResumeAudioStreamDevice(stream_);
    initialized_ = true;
    return true;
}

void AudioEngine::play(SoundId id, float volume) {
    if (!initialized_ || volume <= 0.01f) return;
    Voice v;
    v.id = id;
    v.volume = std::clamp(volume, 0.0f, 1.5f);
    switch (id) {
        case SoundId::ShootBolter:     v.duration = 0.14f; break;
        case SoundId::ShootShotgun:    v.duration = 0.28f; break;
        case SoundId::HitEnemy:        v.duration = 0.10f; break;
        case SoundId::PlayerHurt:      v.duration = 0.22f; break;
        case SoundId::Pickup:          v.duration = 0.18f; break;
        case SoundId::LevelDescend:    v.duration = 0.55f; break;
        case SoundId::RobotStep:       v.duration = 0.26f; break;
        case SoundId::RobotAlertPulse: v.duration = 0.34f; break;
    }
    voices_.push_back(v);
}

void AudioEngine::update() {
    if (!initialized_ || !stream_) return;

    const int queued = SDL_GetAudioStreamQueued(stream_);
    const int targetBytes = 512 * static_cast<int>(sizeof(float));
    if (queued >= targetBytes) return;

    const int samples = 512;
    std::vector<float> buf(samples, 0.0f);
    const float dt = 1.0f / 22050.0f;

    uint32_t noiseState = 0x12345678u;
    auto nextNoise = [&]() -> float {
        noiseState ^= noiseState << 13;
        noiseState ^= noiseState >> 17;
        noiseState ^= noiseState << 5;
        return (static_cast<float>(noiseState & 0xFFFF) / 32768.0f) - 1.0f;
    };

    for (int i = 0; i < samples; ++i) {
        ambientPhase_ += dt * 55.0f * 6.283185f;
        if (ambientPhase_ > 6.283185f) ambientPhase_ -= 6.283185f;
        float out = std::sin(ambientPhase_) * 0.022f;

        for (auto& v : voices_) {
            float env = std::max(0.0f, 1.0f - (v.time / v.duration));
            float s = 0.0f;
            switch (v.id) {
                case SoundId::ShootBolter: {
                    float freq = 320.0f * env + 70.0f;
                    v.phase += dt * freq * 6.283185f;
                    s = (std::sin(v.phase) * 0.5f + nextNoise() * 0.6f) * env * 0.35f;
                    break;
                }
                case SoundId::ShootShotgun: {
                    float freq = 180.0f * env + 45.0f;
                    v.phase += dt * freq * 6.283185f;
                    s = (std::sin(v.phase) * 0.4f + nextNoise() * 0.9f) * env * 0.45f;
                    break;
                }
                case SoundId::HitEnemy: {
                    v.phase += dt * 440.0f * 6.283185f;
                    s = std::sin(v.phase) * env * 0.25f;
                    break;
                }
                case SoundId::PlayerHurt: {
                    v.phase += dt * 110.0f * 6.283185f;
                    s = ((std::sin(v.phase) > 0.0f ? 1.0f : -1.0f) * 0.5f + nextNoise() * 0.3f) * env * 0.3f;
                    break;
                }
                case SoundId::Pickup: {
                    float freq = 520.0f + (v.time / v.duration) * 440.0f;
                    v.phase += dt * freq * 6.283185f;
                    s = std::sin(v.phase) * env * 0.22f;
                    break;
                }
                case SoundId::LevelDescend: {
                    float freq = 140.0f + (v.time / v.duration) * 260.0f;
                    v.phase += dt * freq * 6.283185f;
                    s = std::sin(v.phase) * env * 0.3f;
                    break;
                }
                case SoundId::RobotStep: {
                    // Resonant metallic clank + high-pitched ring
                    v.phase += dt * 880.0f * 6.283185f;
                    v.phase2 += dt * 1410.0f * 6.283185f;
                    float ring = (std::sin(v.phase) * 0.55f + std::sin(v.phase2) * 0.45f) * std::pow(env, 1.8f);
                    float thud = (v.time < 0.045f) ? nextNoise() * 0.45f : 0.0f;
                    s = (ring * 0.32f + thud) * 0.9f;
                    break;
                }
                case SoundId::RobotAlertPulse: {
                    // Monotonous repeating mechanical alarm tone
                    v.phase += dt * 520.0f * 6.283185f;
                    v.phase2 += dt * 260.0f * 6.283185f;
                    float sq = (std::sin(v.phase) > 0.0f ? 0.6f : -0.6f) + std::sin(v.phase2) * 0.4f;
                    float gate = (v.time < 0.18f) ? 1.0f : std::max(0.0f, 1.0f - (v.time - 0.18f) / 0.16f);
                    s = sq * gate * 0.22f;
                    break;
                }
            }
            out += s * v.volume;
            v.time += dt;
        }
        buf[i] = std::clamp(out, -0.85f, 0.85f);
    }

    voices_.erase(
        std::remove_if(voices_.begin(), voices_.end(),
                       [](const Voice& v) { return v.time >= v.duration; }),
        voices_.end());

    SDL_PutAudioStreamData(stream_, buf.data(), samples * static_cast<int>(sizeof(float)));
}

} // namespace mega
