#pragma once

#include "game/GameWorld.hpp"
#include <epoxy/gl.h>
#include <cstdint>
#include <vector>

namespace mega {

class HudRenderer {
public:
    HudRenderer() = default;
    ~HudRenderer();

    bool init(int width, int height);
    void render(const GameWorld& gw);

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<uint32_t> pixels_; // RGBA8888
    GLuint tex_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint program_ = 0;

    void clear();
    void putPixel(int x, int y, uint32_t rgba);
    void fillRect(int x, int y, int w, int h, uint32_t rgba);
    void drawLine(int x0, int y0, int x1, int y1, uint32_t rgba, int thickness = 2);
    void drawChar(int x, int y, char c, uint32_t rgba, int scale = 1);
    void drawText(int x, int y, const char* text, uint32_t rgba, int scale = 1);
    void drawBar(int x, int y, int w, int h, float pct, uint32_t fillCol);
    void drawLockpickMinigame(const GameWorld& gw);
    void drawMossMinigame(const GameWorld& gw);
    void drawMinimap(const GameWorld& gw);
    void drawSettingsMenu(const GameWorld& gw);
};

} // namespace mega
