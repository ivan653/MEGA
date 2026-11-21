#include "render/HudRenderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace mega {

namespace {

constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(g) << 8)  |
           static_cast<uint32_t>(r);
}

// Compact 3x5 pixel font for crisp retro HUD (ASCII 32..95)
struct Glyph3x5 { char ch; uint8_t rows[5]; };
constexpr Glyph3x5 FONT[] = {
    {' ', {0b000,0b000,0b000,0b000,0b000}},
    {'-', {0b000,0b000,0b111,0b000,0b000}},
    {':', {0b000,0b010,0b000,0b010,0b000}},
    {'/', {0b001,0b001,0b010,0b100,0b100}},
    {'[', {0b110,0b100,0b100,0b100,0b110}},
    {']', {0b011,0b001,0b001,0b001,0b011}},
    {'0', {0b111,0b101,0b101,0b101,0b111}},
    {'1', {0b010,0b110,0b010,0b010,0b111}},
    {'2', {0b111,0b001,0b111,0b100,0b111}},
    {'3', {0b111,0b001,0b111,0b001,0b111}},
    {'4', {0b101,0b101,0b111,0b001,0b001}},
    {'5', {0b111,0b100,0b111,0b001,0b111}},
    {'6', {0b111,0b100,0b111,0b101,0b111}},
    {'7', {0b111,0b001,0b001,0b010,0b010}},
    {'8', {0b111,0b101,0b111,0b101,0b111}},
    {'9', {0b111,0b101,0b111,0b001,0b111}},
    {'A', {0b010,0b101,0b111,0b101,0b101}},
    {'B', {0b110,0b101,0b110,0b101,0b110}},
    {'C', {0b111,0b100,0b100,0b100,0b111}},
    {'D', {0b110,0b101,0b101,0b101,0b110}},
    {'E', {0b111,0b100,0b110,0b100,0b111}},
    {'F', {0b111,0b100,0b110,0b100,0b100}},
    {'G', {0b111,0b100,0b101,0b101,0b111}},
    {'H', {0b101,0b101,0b111,0b101,0b101}},
    {'I', {0b111,0b010,0b010,0b010,0b111}},
    {'J', {0b001,0b001,0b001,0b101,0b111}},
    {'K', {0b101,0b101,0b110,0b101,0b101}},
    {'L', {0b100,0b100,0b100,0b100,0b111}},
    {'M', {0b101,0b111,0b111,0b101,0b101}},
    {'N', {0b101,0b111,0b111,0b111,0b101}},
    {'O', {0b111,0b101,0b101,0b101,0b111}},
    {'P', {0b111,0b101,0b111,0b100,0b100}},
    {'Q', {0b111,0b101,0b101,0b111,0b001}},
    {'R', {0b111,0b101,0b110,0b101,0b101}},
    {'S', {0b111,0b100,0b111,0b001,0b111}},
    {'T', {0b111,0b010,0b010,0b010,0b010}},
    {'U', {0b101,0b101,0b101,0b101,0b111}},
    {'V', {0b101,0b101,0b101,0b101,0b010}},
    {'W', {0b101,0b101,0b111,0b111,0b101}},
    {'X', {0b101,0b101,0b010,0b101,0b101}},
    {'Y', {0b101,0b101,0b010,0b010,0b010}},
    {'Z', {0b111,0b001,0b010,0b100,0b111}},
};

const Glyph3x5* findGlyph(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    for (const auto& g : FONT) {
        if (g.ch == c) return &g;
    }
    return &FONT[0];
}

} // namespace

HudRenderer::~HudRenderer() {
    if (tex_) glDeleteTextures(1, &tex_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (program_) glDeleteProgram(program_);
}

bool HudRenderer::init(int width, int height) {
    width_ = width;
    height_ = height;
    pixels_.assign(static_cast<size_t>(width_ * height_), 0);

    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    const float quad[] = {
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 0.0f
    };

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    auto compileStage = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        return s;
    };
    const char* vs = R"(#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
out vec2 vUV;
void main() { vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); })";
    const char* fs = R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uHud;
void main() {
    vec4 c = texture(uHud, vUV);
    if (c.a < 0.05) discard;
    FragColor = c;
})";

    GLuint v = compileStage(GL_VERTEX_SHADER, vs);
    GLuint f = compileStage(GL_FRAGMENT_SHADER, fs);
    program_ = glCreateProgram();
    glAttachShader(program_, v);
    glAttachShader(program_, f);
    glLinkProgram(program_);
    glDeleteShader(v);
    glDeleteShader(f);
    return true;
}

void HudRenderer::clear() {
    std::fill(pixels_.begin(), pixels_.end(), 0u);
}

void HudRenderer::putPixel(int x, int y, uint32_t col) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    pixels_[static_cast<size_t>(y * width_ + x)] = col;
}

void HudRenderer::fillRect(int x, int y, int w, int h, uint32_t col) {
    for (int py = y; py < y + h; ++py)
        for (int px = x; px < x + w; ++px)
            putPixel(px, py, col);
}

void HudRenderer::drawChar(int x, int y, char c, uint32_t col, int scale) {
    const Glyph3x5* g = findGlyph(c);
    for (int r = 0; r < 5; ++r) {
        for (int colBit = 0; colBit < 3; ++colBit) {
            if (g->rows[r] & (1 << (2 - colBit))) {
                fillRect(x + colBit * scale, y + r * scale, scale, scale, col);
            }
        }
    }
}

void HudRenderer::drawText(int x, int y, const char* text, uint32_t col, int scale) {
    int cx = x;
    for (const char* p = text; *p; ++p) {
        drawChar(cx, y, *p, col, scale);
        cx += 4 * scale;
    }
}

void HudRenderer::drawBar(int x, int y, int w, int h, float pct, uint32_t fillCol) {
    fillRect(x, y, w, h, rgba(16, 20, 26, 210));
    int fw = static_cast<int>(std::clamp(pct, 0.0f, 1.0f) * (w - 2));
    if (fw > 0) {
        fillRect(x + 1, y + 1, fw, h - 2, fillCol);
    }
}

void HudRenderer::drawMinimap(const GameWorld& gw) {
    const int mapW = 72;
    const int mapH = 72;
    const int ox = width_ - mapW - 6;
    const int oy = 6;

    fillRect(ox - 1, oy - 1, mapW + 2, mapH + 2, rgba(20, 26, 32, 210));

    int px = static_cast<int>(std::floor(gw.player.position.x));
    int py = static_cast<int>(std::floor(gw.player.position.y + 0.5f));
    int pz = static_cast<int>(std::floor(gw.player.position.z));

    int half = mapW / 2;
    for (int dy = 0; dy < mapH; ++dy) {
        for (int dx = 0; dx < mapW; ++dx) {
            int wx = px + (dx - half);
            int wz = pz + (dy - half);
            bool wall = gw.map.world.getCell(wx, py + 1, wz).isOpaque();
            bool floor = gw.map.world.getCell(wx, py - 1, wz).isSolid();
            if (wall) {
                putPixel(ox + dx, oy + dy, rgba(85, 100, 115, 230));
            } else if (floor) {
                putPixel(ox + dx, oy + dy, rgba(28, 34, 42, 210));
            }
        }
    }

    // Enemies
    for (const auto& e : gw.enemies) {
        if (!e.alive) continue;
        int ex = static_cast<int>(std::floor(e.pos.x)) - px + half;
        int ez = static_cast<int>(std::floor(e.pos.z)) - pz + half;
        if (ex >= 0 && ex < mapW && ez >= 0 && ez < mapH) {
            fillRect(ox + ex - 1, oy + ez - 1, 2, 2, rgba(255, 65, 65, 255));
        }
    }

    // Exit Beacon
    int bx = static_cast<int>(std::floor(gw.map.exitPos.x)) - px + half;
    int bz = static_cast<int>(std::floor(gw.map.exitPos.z)) - pz + half;
    bx = std::clamp(bx, 1, mapW - 2);
    bz = std::clamp(bz, 1, mapH - 2);
    fillRect(ox + bx - 1, oy + bz - 1, 3, 3, rgba(60, 255, 235, 255));

    // Player center dot
    fillRect(ox + half - 1, oy + half - 1, 2, 2, rgba(255, 240, 80, 255));
}

void HudRenderer::render(const GameWorld& gw) {
    clear();

    // Damage vignette border
    if (gw.damageFlash > 0.01f) {
        uint8_t a = static_cast<uint8_t>(std::min(180.0f, gw.damageFlash * 220.0f));
        uint32_t red = rgba(210, 25, 25, a);
        fillRect(0, 0, width_, 8, red);
        fillRect(0, height_ - 8, width_, 8, red);
        fillRect(0, 0, 8, height_, red);
        fillRect(width_ - 8, 0, 8, height_, red);
    }

    // Center crosshair / Amnesia-style interaction reticle & prompt
    int cx = width_ / 2;
    int cy = height_ / 2;
    if (gw.grabbedDoorIdx >= 0) {
        fillRect(cx - 5, cy - 5, 10, 10, rgba(130, 215, 255, 190));
        drawText(cx - 56, cy + 16, "DRAG MOUSE", rgba(160, 225, 255, 230), 2);
    } else if (gw.hoveredDoorIdx >= 0) {
        fillRect(cx - 4, cy - 4, 8, 8, rgba(160, 225, 255, 170));
        const char* label = gw.doors[gw.hoveredDoorIdx].isCabinet ? "[LMB DRAG] CABINET" : "[LMB DRAG] DOOR";
        drawText(cx - 72, cy + 16, label, rgba(210, 230, 245, 230), 2);
    } else if (gw.hoveredItemIdx >= 0) {
        fillRect(cx - 4, cy - 4, 8, 8, rgba(255, 230, 120, 190));
        const char* names[] = {"[LMB] FLASHLIGHT", "[LMB] BATTERY", "[LMB] FOOD", "[LMB] WATER"};
        int k = static_cast<int>(gw.worldItems[gw.hoveredItemIdx].type);
        drawText(cx - 64, cy + 16, names[k], rgba(255, 235, 150, 235), 2);
    } else if (!gw.inventoryOpen) {
        fillRect(cx - 1, cy - 1, 3, 3, rgba(200, 220, 240, 140));
    }

    // Bottom-left survival bars (HP, Hunger, Thirst, Energy, Battery)
    int bx = 18;
    int by = height_ - 116;
    fillRect(bx - 8, by - 8, 218, 108, rgba(10, 14, 18, 175));

    drawText(bx, by + 2,  "HP",  rgba(220, 225, 235, 230), 2);
    drawBar(bx + 68, by, 130, 12, gw.health / 100.0f, rgba(205, 55, 55, 230));

    drawText(bx, by + 20, "FOOD", rgba(220, 190, 130, 230), 2);
    drawBar(bx + 68, by + 18, 130, 12, gw.hunger / 100.0f, rgba(210, 145, 55, 230));

    drawText(bx, by + 38, "H2O",  rgba(120, 200, 245, 230), 2);
    drawBar(bx + 68, by + 36, 130, 12, gw.thirst / 100.0f, rgba(65, 165, 230, 230));

    drawText(bx, by + 56, "NRG",  rgba(165, 230, 145, 230), 2);
    drawBar(bx + 68, by + 54, 130, 12, gw.energy / 100.0f, rgba(110, 205, 95, 230));

    if (gw.inventory.hasFlashlight) {
        drawText(bx, by + 76, "LAMP", rgba(240, 235, 145, 230), 2);
        drawBar(bx + 68, by + 74, 130, 12, gw.inventory.flashlightBattery / 100.0f,
                gw.flashlightOn ? rgba(180, 235, 255, 240) : rgba(110, 135, 155, 210));
    } else {
        drawText(bx, by + 76, "NO FLASHLIGHT", rgba(140, 150, 165, 200), 2);
    }

    // Real-Time Inventory Overlay (Tab / I — game continues running!)
    if (gw.inventoryOpen) {
        int iw = 360, ih = 250;
        int ix = cx - iw / 2;
        int iy = cy - ih / 2;
        fillRect(ix, iy, iw, ih, rgba(12, 16, 22, 228));
        fillRect(ix, iy, iw, 2, rgba(95, 130, 160, 240));
        fillRect(ix, iy + ih - 2, iw, 2, rgba(95, 130, 160, 240));

        drawText(ix + 18, iy + 16, "INVENTORY  [TAB CLOSE]", rgba(170, 215, 245, 240), 2);

        char line[64];
        if (gw.inventory.hasFlashlight) {
            std::snprintf(line, sizeof(line), "FLASHLIGHT  %d PCT",
                          static_cast<int>(gw.inventory.flashlightBattery));
            drawText(ix + 20, iy + 52, line, rgba(200, 235, 255, 240), 2);
        } else {
            drawText(ix + 20, iy + 52, "FLASHLIGHT  NONE", rgba(130, 140, 150, 210), 2);
        }

        // Clickable / hotkeyed slots: [1] Battery, [2] Food, [3] Water
        fillRect(ix + 18, iy + 86, iw - 36, 36, rgba(24, 32, 42, 220));
        std::snprintf(line, sizeof(line), "[1] BATTERY   X%d  [USE]", gw.inventory.batteries);
        drawText(ix + 28, iy + 98, line,
                 gw.inventory.batteries > 0 ? rgba(255, 225, 100, 245) : rgba(110, 115, 125, 200), 2);

        fillRect(ix + 18, iy + 132, iw - 36, 36, rgba(24, 32, 42, 220));
        std::snprintf(line, sizeof(line), "[2] FOOD      X%d  [EAT]", gw.inventory.food);
        drawText(ix + 28, iy + 144, line,
                 gw.inventory.food > 0 ? rgba(235, 175, 95, 245) : rgba(110, 115, 125, 200), 2);

        fillRect(ix + 18, iy + 178, iw - 36, 36, rgba(24, 32, 42, 220));
        std::snprintf(line, sizeof(line), "[3] WATER     X%d  [DRINK]", gw.inventory.water);
        drawText(ix + 28, iy + 190, line,
                 gw.inventory.water > 0 ? rgba(110, 205, 255, 245) : rgba(110, 115, 125, 200), 2);
    }

    if (gw.showMinimap) {
        drawMinimap(gw);
    }

    if (gw.gameOver) {
        fillRect(cx - 150, cy - 42, 300, 84, rgba(20, 8, 8, 235));
        drawText(cx - 88, cy - 22, "SIGNAL LOST", rgba(255, 70, 70), 2);
        drawText(cx - 128, cy + 8, "PRESS R TO RESTART", rgba(230, 230, 230), 2);
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_,
                    GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());
    glUniform1i(glGetUniformLocation(program_, "uHud"), 0);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

} // namespace mega
