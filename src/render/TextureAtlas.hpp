#pragma once

#include <epoxy/gl.h>
#include <cstdint>

namespace mega {

// Generates 32x32 pixelated procedural textures into a GL_TEXTURE_2D_ARRAY (GL_NEAREST)
class TextureAtlas {
public:
    static constexpr int TEX_SIZE = 32;
    static constexpr int LAYER_COUNT = 17;

    TextureAtlas() = default;
    ~TextureAtlas();

    bool init();
    void bind(int unit = 0) const;

private:
    GLuint texArray_ = 0;
};

} // namespace mega
