#pragma once

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace mega::cfg {

// Native 1280x720 resolution
inline constexpr int WINDOW_WIDTH  = 1280;
inline constexpr int WINDOW_HEIGHT = 720;
inline constexpr int RETRO_WIDTH   = 1280;
inline constexpr int RETRO_HEIGHT  = 720;

// World & Chunk dimensions (1 unit = 1 meter)
inline constexpr int CHUNK_SIZE = 16;
inline constexpr int REGION_SIZE = 128; // 128x128m streaming region

// Physics & Player
inline constexpr float FIXED_DT        = 1.0f / 60.0f;
inline constexpr float GRAVITY         = 20.0f;
inline constexpr float JUMP_SPEED      = 6.8f;
inline constexpr float MOVE_SPEED      = 4.6f;
inline constexpr float SPRINT_MULT     = 1.45f;
inline constexpr float GROUND_ACCEL    = 12.0f;
inline constexpr float AIR_ACCEL       = 3.5f;
inline constexpr float GROUND_FRICTION = 10.0f;
inline constexpr float STEP_HEIGHT     = 0.52f;

inline constexpr float PLAYER_RADIUS   = 0.30f;
inline constexpr float PLAYER_HEIGHT   = 1.75f;
inline constexpr float PLAYER_EYE_Y    = 1.56f;

inline constexpr float MOUSE_SENS      = 0.0020f;
inline constexpr float FOV_DEGREES     = 70.0f;
inline constexpr float NEAR_PLANE      = 0.08f;
inline constexpr float FAR_PLANE       = 140.0f;

} // namespace mega::cfg

namespace mega {

struct GameSettings {
    // Lighting settings
    float ambientBrightness   = 0.036f;
    float lampIntensity       = 1.45f;
    float lampRadius          = 18.0f;
    float lampColorR          = 0.72f;
    float lampColorG          = 0.88f;
    float lampColorB          = 1.00f;
    float flashlightIntensity = 1.50f;
    float flashlightRadius    = 28.0f;
    float fogDensity          = 0.032f;

    // Pixelated lighting effect settings
    float lightTexelSnap      = 32.0f; // Snaps lighting calculation to 32x32 texture pixels
    float lightSteps          = 14.0f; // Quantized lighting bands (0 = smooth)
    float lightDitherStrength = 0.35f; // Ordered 2x2 dither between lighting bands

    // Corridor generation settings
    int corridorMinLen        = 30;
    int corridorMaxLen        = 80;
    int corridorMinWidth      = 2;
    int corridorMaxWidth      = 4;
    int minConnections        = 2;
    int maxConnections        = 3;
    int minParallelSpacing    = 22;

    // Infinite map streaming radius in regions (1 = 3x3 regions = 384x384m active window)
    int streamRadius          = 1;

    static void writeDefaultFile(const std::string& path) {
        std::ofstream out(path);
        if (!out) return;
        out << "# MEGA // Configuration File\n"
            << "# Press F5 in-game to hot-reload lighting settings, or R to regenerate world.\n\n"
            << "# --- Lighting ---\n"
            << "ambient_brightness = 0.036\n"
            << "lamp_intensity = 1.45\n"
            << "lamp_radius = 18.0\n"
            << "lamp_color_r = 0.72\n"
            << "lamp_color_g = 0.88\n"
            << "lamp_color_b = 1.00\n"
            << "flashlight_intensity = 1.50\n"
            << "flashlight_radius = 28.0\n"
            << "fog_density = 0.032\n\n"
            << "# --- Pixelated 3D Lighting Effect ---\n"
            << "light_texel_snap = 32.0\n"
            << "light_steps = 14.0\n"
            << "light_dither_strength = 0.35\n\n"
            << "# --- Utility Complex Corridor Generator ---\n"
            << "corridor_min_len = 30\n"
            << "corridor_max_len = 80\n"
            << "corridor_min_width = 2\n"
            << "corridor_max_width = 4\n"
            << "min_connections = 2\n"
            << "max_connections = 3\n"
            << "min_parallel_spacing = 22\n"
            << "stream_radius = 1\n";
    }

    static GameSettings loadOrCreate(const std::string& path = "mega.cfg") {
        GameSettings s;
        std::ifstream in(path);
        if (!in.good()) {
            writeDefaultFile(path);
            return s;
        }

        std::string line;
        while (std::getline(in, line)) {
            auto hashPos = line.find('#');
            if (hashPos != std::string::npos) line = line.substr(0, hashPos);
            auto eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;

            auto trim = [](std::string str) {
                size_t a = str.find_first_not_of(" \t\r\n");
                if (a == std::string::npos) return std::string{};
                size_t b = str.find_last_not_of(" \t\r\n");
                return str.substr(a, b - a + 1);
            };

            std::string key = trim(line.substr(0, eqPos));
            std::string val = trim(line.substr(eqPos + 1));
            if (key.empty() || val.empty()) continue;

            try {
                if (key == "ambient_brightness") s.ambientBrightness = std::stof(val);
                else if (key == "lamp_intensity") s.lampIntensity = std::stof(val);
                else if (key == "lamp_radius") s.lampRadius = std::stof(val);
                else if (key == "lamp_color_r") s.lampColorR = std::stof(val);
                else if (key == "lamp_color_g") s.lampColorG = std::stof(val);
                else if (key == "lamp_color_b") s.lampColorB = std::stof(val);
                else if (key == "flashlight_intensity") s.flashlightIntensity = std::stof(val);
                else if (key == "flashlight_radius") s.flashlightRadius = std::stof(val);
                else if (key == "fog_density") s.fogDensity = std::stof(val);
                else if (key == "light_texel_snap") s.lightTexelSnap = std::stof(val);
                else if (key == "light_steps") s.lightSteps = std::stof(val);
                else if (key == "light_dither_strength") s.lightDitherStrength = std::stof(val);
                else if (key == "corridor_min_len") s.corridorMinLen = std::clamp(std::stoi(val), 16, 120);
                else if (key == "corridor_max_len") s.corridorMaxLen = std::clamp(std::stoi(val), 20, 120);
                else if (key == "corridor_min_width") s.corridorMinWidth = std::clamp(std::stoi(val), 2, 6);
                else if (key == "corridor_max_width") s.corridorMaxWidth = std::clamp(std::stoi(val), 2, 6);
                else if (key == "min_connections") s.minConnections = std::clamp(std::stoi(val), 1, 5);
                else if (key == "max_connections") s.maxConnections = std::clamp(std::stoi(val), 2, 6);
                else if (key == "min_parallel_spacing") s.minParallelSpacing = std::clamp(std::stoi(val), 10, 50);
                else if (key == "stream_radius") s.streamRadius = std::clamp(std::stoi(val), 1, 3);
            } catch (...) {
            }
        }
        return s;
    }
};

} // namespace mega
