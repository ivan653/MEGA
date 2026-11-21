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

void HudRenderer::drawChar(int x, int y, char c, uint32_t col) {
    const Glyph3x5* g = findGlyph(c);
    for (int r = 0; r < 5; ++r) {
        for (int colBit = 0; colBit < 3; ++colBit) {
            if (g->rows[r] & (1 << (2 - colBit))) {
                putPixel(x + colBit, y + r, col);
            }
        }
    }
}

void HudRenderer::drawText(int x, int y, const char* text, uint32_t col) {
    int cx = x;
    for (const char* p = text; *p; ++p) {
        drawChar(cx, y, *p, col);
        cx += 4;
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

    // Subtle center dot crosshair
    fillRect(width_ / 2 - 1, height_ / 2 - 1, 2, 2, rgba(200, 220, 240, 140));

    if (gw.showMinimap) {
        drawMinimap(gw);
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
