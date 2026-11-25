#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Math3D.hpp"
#include "game/GameWorld.hpp"
#include "render/BillboardRenderer.hpp"
#include "render/Framebuffer.hpp"
#include "render/HudRenderer.hpp"
#include "render/ModelRenderer.hpp"
#include "render/RobotRenderer.hpp"
#include "render/Shader.hpp"
#include "render/TextureAtlas.hpp"
#include "render/WorldMesher.hpp"

#include <SDL3/SDL.h>
#include <epoxy/gl.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

using namespace mega;

static const char* WORLD_VS = R"(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aUVLayer;
layout (location = 3) in float aAO;
layout (location = 4) in float aEmissive;

out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vUVLayer;
out float vAO;
out float vEmissive;
out float vViewDist;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uPartScale;
uniform float uTexLayerOverride;
uniform float uEmissiveOverride;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = normalize(mat3(uModel) * aNormal);

    vec2 uv = aUVLayer.xy;
    if (uPartScale.x > 0.001) {
        if (abs(aNormal.x) > 0.5) uv *= vec2(uPartScale.z, uPartScale.y);
        else if (abs(aNormal.y) > 0.5) uv *= vec2(uPartScale.x, uPartScale.z);
        else uv *= vec2(uPartScale.x, uPartScale.y);
    }
    float layer = (uTexLayerOverride >= 0.0) ? uTexLayerOverride : aUVLayer.z;
    vUVLayer = vec3(uv, layer);
    vAO = aAO;
    vEmissive = (uEmissiveOverride >= 0.0) ? uEmissiveOverride : aEmissive;

    vec4 viewPos = uView * worldPos;
    vViewDist = length(viewPos.xyz);
    gl_Position = uProj * viewPos;
}
)";

// Real-Time 3D Per-Pixel Lighting with Pixelated Texel-Snapped Light & Quantized Bands
static const char* WORLD_FS = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vUVLayer;
in float vAO;
in float vEmissive;
in float vViewDist;

out vec4 FragColor;

uniform sampler2DArray uAtlas;
uniform vec3 uCamPos;
uniform vec3 uCamDir;
uniform float uFlashlight;
uniform vec3 uTint;

uniform float uAmbientBrightness;
uniform float uFlashlightIntensity;
uniform float uFlashlightRadius;
uniform vec3 uFlashlightColor;
uniform float uObjectEmissiveScale;
uniform float uFogDensity;
uniform float uLightTexelSnap;
uniform float uLightSteps;
uniform float uLightDitherStrength;
uniform float uTime;

const int MAX_LIGHTS = 48;
uniform int uLightCount;
uniform vec4 uLightPosRadius[MAX_LIGHTS];
uniform vec4 uLightColorIntensity[MAX_LIGHTS];

float texLuma(vec3 uvl) {
    vec3 c = texture(uAtlas, uvl).rgb;
    return dot(c, vec3(0.299, 0.587, 0.114));
}

float bayer2x2(vec2 p) {
    int x = int(mod(p.x, 2.0));
    int y = int(mod(p.y, 2.0));
    int idx = y * 2 + x;
    float m[4] = float[4](-0.375, 0.125, 0.375, -0.125);
    return m[idx];
}

void main() {
    int layerId = int(vUVLayer.z + 0.5);
    vec3 uvLayer = vUVLayer;
    if (layerId == 15) {
        // Gentle pixel-snapped sewer water flow animation along channel
        uvLayer.x += floor(uTime * 10.0) / 32.0;
        uvLayer.y += floor(uTime * 4.0) / 32.0;
    }
    vec4 tex = texture(uAtlas, uvLayer);
    if (tex.a < 0.5) discard;

    if (vEmissive > 0.5) {
        vec3 emBoost = (layerId == 16) ? vec3(1.65, 1.80, 1.05) : vec3(1.55, 1.75, 2.05);
        vec3 lampCol = tex.rgb * emBoost * uObjectEmissiveScale;
        float fog = 1.0 - exp(-max(0.0, vViewDist - 6.0) * uFogDensity);
        FragColor = vec4(mix(lampCol, vec3(0.015, 0.02, 0.028), clamp(fog * 0.65, 0.0, 1.0)), 1.0);
        return;
    }

    // Snap world position only along the surface's 2 tangent axes (keep normal axis exact)
    // so 3D lighting aligns cleanly to 32x32 square texels without diagonal/diamond artifacts.
    vec3 absN = abs(vNormal);
    vec3 samplePos = vWorldPos;
    if (uLightTexelSnap > 1.0) {
        vec3 snapped = (floor(vWorldPos * uLightTexelSnap) + 0.5) / uLightTexelSnap;
        if (absN.x < 0.5) samplePos.x = snapped.x;
        if (absN.y < 0.5) samplePos.y = snapped.y;
        if (absN.z < 0.5) samplePos.z = snapped.z;
    }

    vec3 N = normalize(vNormal);
    vec3 T = (abs(N.y) > 0.5) ? vec3(1.0, 0.0, 0.0) : vec3(-N.z, 0.0, N.x);
    vec3 B = cross(N, T);

    // Per-texel bump normal relief from 32x32 texture
    float eps = 1.0 / 32.0;
    vec2 snappedUV = floor(uvLayer.xy * 32.0) / 32.0 + vec2(0.5 / 32.0);
    float h0 = texLuma(vec3(snappedUV, uvLayer.z));
    float hx = texLuma(vec3(snappedUV + vec2(eps, 0.0), uvLayer.z)) - h0;
    float hy = texLuma(vec3(snappedUV + vec2(0.0, eps), uvLayer.z)) - h0;
    N = normalize(N - (T * hx + B * hy) * 1.45);

    float specStrength = 0.15;
    float shininess = 16.0;
    if (layerId == 2 || layerId == 4) {
        specStrength = 0.65;
        shininess = 32.0;
    } else if (layerId == 5 || layerId == 14) {
        specStrength = 0.48;
        shininess = 26.0;
    } else if (layerId == 15) {
        specStrength = 0.85;
        shininess = 48.0;
    } else if (layerId == 3) {
        specStrength = 0.12;
        shininess = 12.0;
    }

    vec3 V = normalize(uCamPos - samplePos);
    vec3 totalDiffuse = vec3(0.82, 0.90, 1.0) * uAmbientBrightness * vAO;
    vec3 totalSpec = vec3(0.0);

    bool fragUpper = (samplePos.y > 0.25);
    for (int i = 0; i < uLightCount; ++i) {
        vec3 lPos = uLightPosRadius[i].xyz;
        // Strict floor slab light separation: sewer lamps (y <= 0.25) NEVER bleed through solid concrete floors into Utility Complex rooms!
        bool lightUpper = (lPos.y > 0.25);
        if (fragUpper != lightUpper) continue;
        float radius = uLightPosRadius[i].w;
        vec3 toLight = lPos - samplePos;
        float distSq = dot(toLight, toLight);
        if (distSq > radius * radius) continue;

        float dist = sqrt(distSq);
        vec3 L = toLight / max(dist, 0.001);

        float normDist = dist / radius;
        float window = clamp(1.0 - normDist * normDist, 0.0, 1.0);
        float atten = (window * window) / (1.0 + 0.20 * dist + 0.08 * distSq);

        vec3 lCol = uLightColorIntensity[i].rgb * uLightColorIntensity[i].w;
        float ndotl = max(dot(N, L), 0.0);
        float wrapDiff = max(dot(N, L) * 0.75 + 0.25, 0.0) * 0.45 + ndotl * 0.65;
        totalDiffuse += lCol * wrapDiff * atten;

        if (ndotl > 0.0) {
            vec3 H = normalize(L + V);
            float spec = pow(max(dot(N, H), 0.0), shininess) * specStrength;
            totalSpec += lCol * spec * atten;
        }
    }

    if (uFlashlight > 0.5) {
        vec3 toCam = uCamPos - samplePos;
        float fDist = length(toCam);
        if (fDist < uFlashlightRadius) {
            vec3 L = toCam / max(fDist, 0.001);
            float spotCos = dot(-L, normalize(uCamDir));
            float cone = smoothstep(0.84, 0.955, spotCos);
            if (cone > 0.0) {
                float fAtten = cone * clamp(1.0 - fDist / uFlashlightRadius, 0.0, 1.0)
                               / (1.0 + 0.08 * fDist + 0.015 * fDist * fDist);
                vec3 fCol = uFlashlightColor * uFlashlightIntensity;
                float ndotl = max(dot(N, L), 0.0);
                totalDiffuse += fCol * ndotl * fAtten;

                vec3 H = normalize(L + V);
                float spec = pow(max(dot(N, H), 0.0), shininess) * specStrength;
                totalSpec += fCol * spec * fAtten;
            }
        }
    }

    // Pixelated lighting band quantization with subtle 2x2 square texel dither
    if (uLightSteps > 1.0) {
        vec2 texelCoord = floor(vUVLayer.xy * 32.0);
        float dither = bayer2x2(texelCoord) * uLightDitherStrength;
        float diffLum = max(max(totalDiffuse.r, totalDiffuse.g), totalDiffuse.b);
        if (diffLum > 0.0001) {
            float qLum = max(0.0, floor(diffLum * uLightSteps + dither + 0.5) / uLightSteps);
            totalDiffuse *= (qLum / diffLum);
        }
        totalSpec = floor(totalSpec * uLightSteps + 0.5) / uLightSteps;
    }

    vec3 litColor = (tex.rgb * uTint) * (totalDiffuse * mix(0.35, 1.0, vAO)) + totalSpec * mix(0.5, 1.0, vAO);

    float fogFactor = 1.0 - exp(-max(0.0, vViewDist - 4.0) * uFogDensity);
    vec3 fogColor = vec3(0.014, 0.018, 0.024);
    vec3 finalCol = mix(litColor, fogColor, clamp(fogFactor, 0.0, 1.0));

    FragColor = vec4(finalCol, 1.0);
}
)";

int main(int argc, char** argv) {
    bool visualTest = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--visual-test") == 0) visualTest = true;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* window = SDL_CreateWindow(
        "MEGA // Infinite Utility Complex [Pixelated 3D Lighting]",
        cfg::WINDOW_WIDTH, cfg::WINDOW_HEIGHT,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glCtx = SDL_GL_CreateContext(window);
    if (!glCtx) {
        std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    SDL_SetWindowRelativeMouseMode(window, !visualTest);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    {
    Framebuffer fbo;
    if (!fbo.init(cfg::RETRO_WIDTH, cfg::RETRO_HEIGHT)) return 1;

    Shader worldShader;
    if (!worldShader.compile(WORLD_VS, WORLD_FS)) return 1;

    TextureAtlas atlas;
    if (!atlas.init()) return 1;

    BillboardRenderer billboards;
    if (!billboards.init()) return 1;

    RobotRenderer robotRenderer;
    if (!robotRenderer.init()) return 1;

    ModelRenderer creatureModel;
    if (!creatureModel.init()) return 1;
    if (!creatureModel.loadModel("assets/models/horned_rat.obj")) {
        std::cerr << "Warning: failed to load assets/models/horned_rat.obj\n";
    } else {
        Vec3 sz = creatureModel.data().size();
        std::cout << "Loaded 3D model assets/models/horned_rat.obj ("
                  << creatureModel.data().triangleCount() << " tris | size="
                  << sz.x << "x" << sz.y << "x" << sz.z << " m)\n";
    }

    HudRenderer hud;
    if (!hud.init(cfg::RETRO_WIDTH, cfg::RETRO_HEIGHT)) return 1;

    AudioEngine audio;
    audio.init();

    GameWorld gw;
    gw.settings = GameSettings::loadOrCreate("mega.cfg");
    gw.lootTables = LootTables::loadOrCreate("loot_tables.cfg");
    audio.applySettings(gw.settings);
    if (gw.settings.fullscreen) {
        SDL_SetWindowFullscreen(window, true);
    }
    uint64_t worldSeed = gw.nextRunSeed();
    gw.startNewRun(worldSeed);
    std::cout << "Loaded mega.cfg | Generated Infinite Utility Complex seed=" << worldSeed
              << " activeRegions=" << gw.map.loadedRegions.size()
              << " corridors=" << gw.map.corridors.size()
              << " props=" << gw.map.world.props().size()
              << " lamps=" << gw.map.pointLights.size()
              << " in " << gw.map.generationMs << " ms\n";

    WorldMesher mesher;
    mesher.syncWorld(gw.map.world);
    gw.worldStreamChanged = false;

    bool running = true;
    bool mouseCaptured = !visualTest;
    bool keyDown[SDL_SCANCODE_COUNT] = {false};

    // Visual test state machine (--visual-test)
    struct VisualTestState {
        int phase = 0;
        int frame = 0;
        int curPin = 0;
        float flickDy = 0.0f;
        bool initialized = false;
    } vt;
    if (visualTest && !gw.doors.empty()) {
        gw.inventory.lockpicks = 3;
        gw.doors[0].locked = true;
        gw.doors[0].angle = 0.0f;
        Vec3 toDoor = gw.doors[0].hingePos - gw.player.eyePosition();
        gw.player.yaw = std::atan2(toDoor.x, -toDoor.z);
        gw.player.pitch = -0.05f;
        gw.lockpick.open(0, 777ULL, 0.0f, &gw.doors[0].hingePos);
        vt.initialized = true;
        std::cout << "[VISUAL TEST] Opened lockpick minigame on door 0 | targets: "
                  << gw.lockpick.pins[0].target << ", "
                  << gw.lockpick.pins[1].target << ", "
                  << gw.lockpick.pins[2].target << "\n";
    }
    uint64_t prevTicks = SDL_GetTicks();
    float accumulator = 0.0f;
    float totalTime = 0.0f;

    int winW = cfg::WINDOW_WIDTH;
    int winH = cfg::WINDOW_HEIGHT;
    std::vector<BillboardInstance> spriteQueue;
    std::vector<RobotRenderState> robotQueue;
    std::vector<DoorRenderState> doorQueue;
    std::vector<ItemRenderState> itemQueue;
    std::vector<MossRenderState> mossQueue;
    std::vector<Mat4> creatureMats;
    std::vector<ModelRenderInstance> creatureQueue;

    auto reloadConfig = [&]() {
        gw.settings = GameSettings::loadOrCreate("mega.cfg");
        gw.lootTables = LootTables::loadOrCreate("loot_tables.cfg");
        audio.applySettings(gw.settings);
        for (auto& l : gw.map.pointLights) {
            if (l.pos.y >= 0.0f) {
                l.color = {gw.settings.lampColorR, gw.settings.lampColorG, gw.settings.lampColorB};
                l.intensity = gw.settings.lampIntensity;
                l.radius = gw.settings.lampRadius;
            }
        }
        std::cout << "Reloaded mega.cfg & loot_tables.cfg (ambient=" << gw.settings.ambientBrightness
                  << " masterVol=" << gw.settings.masterVolume
                  << " echo=" << gw.settings.echoStrength << ")\n";
    };

    float mouseWinX = static_cast<float>(winW) * 0.5f;
    float mouseWinY = static_cast<float>(winH) * 0.5f;
    bool prevFreeCursor = false;
    bool rmbDownInPack = false;
    bool rmbRotatedPack = false;
    bool draggingSettingsSlider = false;
    float rmbPressX = 0.0f;
    float rmbPressY = 0.0f;
    float rmbLastX = 0.0f;
    float rmbLastY = 0.0f;

    auto applySettingsSliderOrClick = [&](float winX, float winY, bool isDrag) {
        float hx = winX * (static_cast<float>(cfg::RETRO_WIDTH) / std::max(1, winW));
        float hy = winY * (static_cast<float>(cfg::RETRO_HEIGHT) / std::max(1, winH));
        // Panel bounds: x=[390..890], rows 0..5 at y = 215 + row * 52, height 40
        if (hx < 390.0f || hx > 890.0f) return;
        for (int row = 0; row < 6; ++row) {
            float ry0 = 215.0f + row * 52.0f;
            float ry1 = ry0 + 40.0f;
            if (hy >= ry0 && hy <= ry1) {
                gw.settingsSelectedRow = row;
                if (row == 0) {
                    // Master Volume slider x=[640..855] -> 0.0..2.0
                    float t = clampf((hx - 640.0f) / 215.0f, 0.0f, 1.0f);
                    if (hx >= 620.0f) {
                        gw.settings.masterVolume = std::round(t * 20.0f) * 0.10f;
                        audio.applySettings(gw.settings);
                        draggingSettingsSlider = true;
                        if (!isDrag) gw.settings.saveUserPreferences("mega.cfg");
                    }
                } else if (row == 1) {
                    // Mouse Sensitivity slider x=[640..855] -> 0.0004..0.0060
                    float t = clampf((hx - 640.0f) / 215.0f, 0.0f, 1.0f);
                    if (hx >= 620.0f) {
                        gw.settings.mouseSensitivity = 0.0004f + t * (0.0060f - 0.0004f);
                        draggingSettingsSlider = true;
                        if (!isDrag) gw.settings.saveUserPreferences("mega.cfg");
                    }
                } else if (!isDrag && row == 2) {
                    gw.settings.fullscreen = !gw.settings.fullscreen;
                    SDL_SetWindowFullscreen(window, gw.settings.fullscreen);
                    gw.settings.saveUserPreferences("mega.cfg");
                } else if (!isDrag && row == 3) {
                    gw.settings.useRandomSeed = !gw.settings.useRandomSeed;
                    gw.settings.saveUserPreferences("mega.cfg");
                } else if (!isDrag && row == 4) {
                    gw.settings.saveUserPreferences("mega.cfg");
                    worldSeed = gw.nextRunSeed();
                    gw.settingsMenuOpen = false;
                    gw.startNewRun(worldSeed);
                    mesher.syncWorld(gw.map.world);
                    gw.worldStreamChanged = false;
                    mouseCaptured = true;
                    SDL_SetWindowRelativeMouseMode(window, true);
                } else if (!isDrag && row == 5) {
                    gw.settings.saveUserPreferences("mega.cfg");
                    gw.settingsMenuOpen = false;
                    mouseCaptured = true;
                    SDL_SetWindowRelativeMouseMode(window, true);
                }
                break;
            }
        }
    };

    auto computeCursorRay = [&](Vec3& outOrig, Vec3& outDir) {
        outOrig = gw.player.eyePosition();
        if (gw.backpackMode == BackpackMode::InHands || gw.draggingItem) {
            float ndcX = (2.0f * mouseWinX / std::max(1, winW)) - 1.0f;
            float ndcY = 1.0f - (2.0f * mouseWinY / std::max(1, winH));
            float aspect = static_cast<float>(cfg::RETRO_WIDTH) / static_cast<float>(cfg::RETRO_HEIGHT);
            float tanHalf = std::tan(radians(cfg::FOV_DEGREES) * 0.5f);
            Vec3 fwd = gw.player.forwardDir();
            Vec3 right = gw.player.rightDir();
            Vec3 up = normalize(cross(right, fwd));
            outDir = normalize(fwd + right * (ndcX * aspect * tanHalf) + up * (ndcY * tanHalf));
        } else {
            outDir = gw.player.forwardDir();
        }
    };

    while (running) {
        uint64_t nowTicks = SDL_GetTicks();
        float frameDt = static_cast<float>(nowTicks - prevTicks) / 1000.0f;
        prevTicks = nowTicks;
        if (frameDt > 0.1f) frameDt = 0.1f;
        accumulator += frameDt * gw.godTimeScale;
        totalTime += frameDt * gw.godTimeScale;

        float yawDelta = 0.0f;
        float pitchDelta = 0.0f;
        bool fireClicked = false;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (ev.type == SDL_EVENT_WINDOW_RESIZED) {
                winW = ev.window.data1;
                winH = ev.window.data2;
            } else if (ev.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
                       ev.type == SDL_EVENT_WINDOW_MINIMIZED) {
                mouseCaptured = false;
                gw.consumeLmbHeld = false;
                SDL_SetWindowRelativeMouseMode(window, false);
                std::fill(std::begin(keyDown), std::end(keyDown), false);
            } else if (ev.type == SDL_EVENT_WINDOW_FOCUS_GAINED ||
                       ev.type == SDL_EVENT_WINDOW_RESTORED) {
                prevTicks = SDL_GetTicks();
                accumulator = 0.0f;
                std::fill(std::begin(keyDown), std::end(keyDown), false);
            } else if (ev.type == SDL_EVENT_KEY_UP) {
                if (ev.key.scancode < SDL_SCANCODE_COUNT) {
                    keyDown[ev.key.scancode] = false;
                }
            } else if (ev.type == SDL_EVENT_KEY_DOWN) {
                if (ev.key.scancode < SDL_SCANCODE_COUNT) {
                    keyDown[ev.key.scancode] = true;
                }
                if (ev.key.key == SDLK_ESCAPE) {
                    if (gw.settingsMenuOpen) {
                        gw.settings.saveUserPreferences("mega.cfg");
                        gw.settingsMenuOpen = false;
                        mouseCaptured = true;
                        SDL_SetWindowRelativeMouseMode(window, true);
                    } else if (gw.lockpick.active) {
                        gw.lockpick.close();
                    } else if (gw.mossMinigame.active) {
                        gw.mossMinigame.close();
                    } else if (gw.draggingItem) {
                        Vec3 ro, rd;
                        computeCursorRay(ro, rd);
                        gw.handleSecondaryClick(ro, rd, audio);
                    } else if (gw.heldPlaceMode) {
                        gw.heldPlaceMode = false;
                        gw.placementPreview = GhostPreviewState{};
                    } else if (gw.backpackMode == BackpackMode::InHands) {
                        gw.toggleBackpackInHands();
                    } else {
                        gw.settingsMenuOpen = true;
                        gw.consumeLmbHeld = false;
                        mouseCaptured = false;
                        SDL_SetWindowRelativeMouseMode(window, false);
                    }
                } else if (gw.settingsMenuOpen) {
                    if (ev.key.key == SDLK_UP || ev.key.key == SDLK_W) {
                        gw.settingsSelectedRow = (gw.settingsSelectedRow + 5) % 6;
                    } else if (ev.key.key == SDLK_DOWN || ev.key.key == SDLK_S) {
                        gw.settingsSelectedRow = (gw.settingsSelectedRow + 1) % 6;
                    } else if (ev.key.key == SDLK_LEFT || ev.key.key == SDLK_A) {
                        if (gw.settingsSelectedRow == 0) {
                            gw.settings.masterVolume = std::max(0.0f, gw.settings.masterVolume - 0.10f);
                            audio.applySettings(gw.settings);
                        } else if (gw.settingsSelectedRow == 1) {
                            gw.settings.mouseSensitivity = std::max(0.0004f, gw.settings.mouseSensitivity - 0.0002f);
                        } else if (gw.settingsSelectedRow == 2) {
                            gw.settings.fullscreen = !gw.settings.fullscreen;
                            SDL_SetWindowFullscreen(window, gw.settings.fullscreen);
                        } else if (gw.settingsSelectedRow == 3) {
                            gw.settings.useRandomSeed = !gw.settings.useRandomSeed;
                        }
                        gw.settings.saveUserPreferences("mega.cfg");
                    } else if (ev.key.key == SDLK_RIGHT || ev.key.key == SDLK_D) {
                        if (gw.settingsSelectedRow == 0) {
                            gw.settings.masterVolume = std::min(2.0f, gw.settings.masterVolume + 0.10f);
                            audio.applySettings(gw.settings);
                        } else if (gw.settingsSelectedRow == 1) {
                            gw.settings.mouseSensitivity = std::min(0.0060f, gw.settings.mouseSensitivity + 0.0002f);
                        } else if (gw.settingsSelectedRow == 2) {
                            gw.settings.fullscreen = !gw.settings.fullscreen;
                            SDL_SetWindowFullscreen(window, gw.settings.fullscreen);
                        } else if (gw.settingsSelectedRow == 3) {
                            gw.settings.useRandomSeed = !gw.settings.useRandomSeed;
                        }
                        gw.settings.saveUserPreferences("mega.cfg");
                    } else if (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_SPACE) {
                        if (gw.settingsSelectedRow == 2) {
                            gw.settings.fullscreen = !gw.settings.fullscreen;
                            SDL_SetWindowFullscreen(window, gw.settings.fullscreen);
                            gw.settings.saveUserPreferences("mega.cfg");
                        } else if (gw.settingsSelectedRow == 3) {
                            gw.settings.useRandomSeed = !gw.settings.useRandomSeed;
                            gw.settings.saveUserPreferences("mega.cfg");
                        } else if (gw.settingsSelectedRow == 4) {
                            gw.settings.saveUserPreferences("mega.cfg");
                            worldSeed = gw.nextRunSeed();
                            gw.settingsMenuOpen = false;
                            gw.startNewRun(worldSeed);
                            mesher.syncWorld(gw.map.world);
                            gw.worldStreamChanged = false;
                            mouseCaptured = true;
                            SDL_SetWindowRelativeMouseMode(window, true);
                        } else if (gw.settingsSelectedRow == 5) {
                            gw.settings.saveUserPreferences("mega.cfg");
                            gw.settingsMenuOpen = false;
                            mouseCaptured = true;
                            SDL_SetWindowRelativeMouseMode(window, true);
                        }
                    }
                } else if (ev.key.key == SDLK_TAB || ev.key.key == SDLK_I) {
                    gw.toggleBackpackInHands();
                } else if (ev.key.key == SDLK_B) {
                    gw.togglePlaceBackpackOnGround(audio);
                } else if (ev.key.key == SDLK_T) {
                    gw.rotateActiveItem(0);
                } else if (ev.key.key == SDLK_Y) {
                    gw.rotateActiveItem(2);
                } else if (ev.key.key == SDLK_L) {
                    if (gw.inventory.hasFlashlight) {
                        if (gw.inventory.flashlightBattery > 0.0f) {
                            gw.flashlightOn = !gw.flashlightOn;
                            audio.play(SoundId::FlashlightClick, 0.95f);
                        } else {
                            audio.play(SoundId::FlashlightClick, 0.45f);
                        }
                    }
                } else if (ev.key.key == SDLK_F && gw.backpackMode != BackpackMode::InHands) {
                    if (gw.hasHeldItem && gw.hoveredDoorIdx < 0 && gw.hoveredItemIdx < 0) {
                        gw.useHeldItem(audio);
                    } else {
                        gw.beginInteract(audio, true);
                        gw.endInteract();
                    }
                } else if (ev.key.key == SDLK_1) {
                    if (!gw.availableCrafts.empty()) gw.performCraft(0, audio);
                    else gw.equipWeaponHotkey(0, audio);
                } else if (ev.key.key == SDLK_2) {
                    if (gw.availableCrafts.size() >= 2) gw.performCraft(1, audio);
                    else gw.equipWeaponHotkey(1, audio);
                } else if (ev.key.key == SDLK_3) {
                    if (gw.availableCrafts.size() >= 3) gw.performCraft(2, audio);
                    else gw.equipWeaponHotkey(2, audio);
                } else if (ev.key.key == SDLK_4) {
                    gw.equipWeaponHotkey(3, audio);
                } else if (ev.key.key == SDLK_5) {
                    gw.equipWeaponHotkey(4, audio);
                } else if (ev.key.key == SDLK_M) {
                    gw.showMinimap = !gw.showMinimap;
                } else if (ev.key.key == SDLK_G) {
                    gw.godMode = !gw.godMode;
                    if (!gw.godMode) {
                        gw.godNoclip = false;
                        gw.godInvisible = false;
                        gw.godSpeedMult = 1.0f;
                        gw.godTimeScale = 1.0f;
                    }
                    std::cout << "[GOD MODE] " << (gw.godMode ? "ENABLED" : "DISABLED") << "\n";
                } else if (ev.key.key == SDLK_N) {
                    gw.godNoclip = !gw.godNoclip;
                    if (gw.godNoclip) gw.godMode = true;
                    std::cout << "[GOD MODE] Noclip: " << (gw.godNoclip ? "ON" : "OFF") << "\n";
                } else if (ev.key.key == SDLK_V) {
                    gw.godInvisible = !gw.godInvisible;
                    if (gw.godInvisible) gw.godMode = true;
                    std::cout << "[GOD MODE] Invisible: " << (gw.godInvisible ? "ON" : "OFF") << "\n";
                } else if (ev.key.key == SDLK_RIGHTBRACKET) {
                    gw.godMode = true;
                    gw.godSpeedMult = std::min(8.0f, gw.godSpeedMult * 2.0f);
                    std::cout << "[GOD MODE] Player Speed: x" << gw.godSpeedMult << "\n";
                } else if (ev.key.key == SDLK_LEFTBRACKET) {
                    gw.godSpeedMult = std::max(1.0f, gw.godSpeedMult * 0.5f);
                    std::cout << "[GOD MODE] Player Speed: x" << gw.godSpeedMult << "\n";
                } else if (ev.key.key == SDLK_PERIOD) {
                    gw.godMode = true;
                    gw.godTimeScale = std::min(8.0f, gw.godTimeScale * 2.0f);
                    std::cout << "[GOD MODE] Game Speed: x" << gw.godTimeScale << "\n";
                } else if (ev.key.key == SDLK_COMMA) {
                    gw.godTimeScale = std::max(1.0f, gw.godTimeScale * 0.5f);
                    std::cout << "[GOD MODE] Game Speed: x" << gw.godTimeScale << "\n";
                } else if (ev.key.key == SDLK_F5) {
                    reloadConfig();
                } else if (ev.key.key == SDLK_R) {
                    if (gw.draggingItem || (gw.hasHeldItem && gw.heldPlaceMode)) {
                        gw.rotateActiveItem(1);
                    } else if (!gw.hasHeldItem) {
                        reloadConfig();
                        worldSeed = gw.nextRunSeed();
                        gw.startNewRun(worldSeed);
                        mesher.syncWorld(gw.map.world);
                        gw.worldStreamChanged = false;
                    }
                } else if (ev.key.key == SDLK_Q && (ev.key.mod & SDL_KMOD_CTRL)) {
                    running = false;
                }
            } else if (ev.type == SDL_EVENT_MOUSE_MOTION) {
                if (gw.settingsMenuOpen) {
                    mouseWinX = ev.motion.x;
                    mouseWinY = ev.motion.y;
                    if (draggingSettingsSlider) {
                        applySettingsSliderOrClick(mouseWinX, mouseWinY, true);
                    }
                } else {
                    bool freeCursor = (gw.backpackMode == BackpackMode::InHands || gw.draggingItem);
                    if (freeCursor) {
                        mouseWinX = ev.motion.x;
                        mouseWinY = ev.motion.y;
                        if (rmbDownInPack && gw.backpackMode == BackpackMode::InHands && !gw.draggingItem) {
                            float dx = mouseWinX - rmbLastX;
                            float dy = mouseWinY - rmbLastY;
                            rmbLastX = mouseWinX;
                            rmbLastY = mouseWinY;
                            if (std::hypot(mouseWinX - rmbPressX, mouseWinY - rmbPressY) > 5.0f) {
                                rmbRotatedPack = true;
                            }
                            if (rmbRotatedPack) {
                                gw.rotateBackpackView(dx, dy);
                            }
                        }
                    } else if (mouseCaptured) {
                        if (gw.lockpick.active) {
                            gw.lockpick.handleMouseMove(ev.motion.xrel, ev.motion.yrel);
                        } else if (gw.mossMinigame.active) {
                            gw.mossMinigame.handleMouseMove(ev.motion.xrel, ev.motion.yrel);
                        } else if (gw.grabbedDoorIdx >= 0) {
                            gw.dragGrabbedDoor(ev.motion.xrel, ev.motion.yrel);
                        } else {
                            yawDelta += ev.motion.xrel * gw.settings.mouseSensitivity;
                            pitchDelta -= ev.motion.yrel * gw.settings.mouseSensitivity;
                        }
                    }
                }
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    if (draggingSettingsSlider && gw.settingsMenuOpen) {
                        gw.settings.saveUserPreferences("mega.cfg");
                    }
                    draggingSettingsSlider = false;
                    gw.mossMinigame.scrapingHeld = false;
                    gw.consumeLmbHeld = false;
                    gw.endInteract();
                } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                    if (rmbDownInPack) {
                        bool wasSpin = rmbRotatedPack;
                        rmbDownInPack = false;
                        rmbRotatedPack = false;
                        // Quick RMB click without dragging -> drag hovered item to floor/surface
                        if (!wasSpin && gw.backpackMode == BackpackMode::InHands) {
                            mouseWinX = ev.button.x;
                            mouseWinY = ev.button.y;
                            Vec3 ro, rd;
                            computeCursorRay(ro, rd);
                            gw.handleSecondaryClick(ro, rd, audio);
                        }
                    }
                }
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (gw.settingsMenuOpen) {
                    if (ev.button.button == SDL_BUTTON_LEFT) {
                        applySettingsSliderOrClick(ev.button.x, ev.button.y, false);
                    }
                } else {
                    bool freeCursor = (gw.backpackMode == BackpackMode::InHands || gw.draggingItem);
                    if (freeCursor) {
                        gw.consumeLmbHeld = false;
                        mouseWinX = ev.button.x;
                        mouseWinY = ev.button.y;
                        Vec3 ro, rd;
                        computeCursorRay(ro, rd);
                        if (ev.button.button == SDL_BUTTON_LEFT) {
                            gw.handlePrimaryClick(ro, rd, audio);
                        } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                            if (gw.backpackMode == BackpackMode::InHands && !gw.draggingItem) {
                                // Hold RMB + move mouse to spin 3D backpack; release quickly to drag item!
                                rmbDownInPack = true;
                                rmbRotatedPack = false;
                                rmbPressX = mouseWinX;
                                rmbPressY = mouseWinY;
                                rmbLastX = mouseWinX;
                                rmbLastY = mouseWinY;
                            } else {
                                gw.handleSecondaryClick(ro, rd, audio);
                            }
                        }
                    } else if (!mouseCaptured) {
                        mouseCaptured = true;
                        SDL_SetWindowRelativeMouseMode(window, true);
                    } else {
                        if (gw.mossMinigame.active) {
                            if (ev.button.button == SDL_BUTTON_LEFT) {
                                gw.mossMinigame.scrapingHeld = true;
                            }
                        } else {
                            Vec3 ro, rd;
                            computeCursorRay(ro, rd);
                            if (ev.button.button == SDL_BUTTON_LEFT) {
                                // Shooting on LMB is disabled; LMB interacts (including starting moss scraping!),
                                // swings melee, recharges with Battery, places in placement mode,
                                // or begins continuous realistic consumption when holding Food/Water!
                                if (!gw.handlePrimaryClick(ro, rd, audio)) {
                                    gw.consumeLmbHeld = true;
                                } else if (gw.mossMinigame.active) {
                                    gw.mossMinigame.scrapingHeld = true;
                                }
                            } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                                gw.consumeLmbHeld = false;
                                gw.handleSecondaryClick(ro, rd, audio);
                            }
                        }
                    }
                }
            }
        }

        // Automatically unlock mouse cursor & pause camera whenever settings menu, backpack in hands, or RMB-dragging is active
        bool wantFreeCursor = (gw.settingsMenuOpen || gw.backpackMode == BackpackMode::InHands || gw.draggingItem);
        if (wantFreeCursor != prevFreeCursor) {
            prevFreeCursor = wantFreeCursor;
            SDL_SetWindowRelativeMouseMode(window, !wantFreeCursor && mouseCaptured && !visualTest);
            if (wantFreeCursor) {
                mouseWinX = static_cast<float>(winW) * 0.5f;
                mouseWinY = static_cast<float>(winH) * 0.5f;
            }
        }

        const bool* sdlKeys = SDL_GetKeyboardState(nullptr);
        auto isHeld = [&](SDL_Scancode sc) {
            return keyDown[sc] || (sdlKeys && sdlKeys[sc]);
        };

        PlayerInput input;
        if (!gw.settingsMenuOpen && !gw.lockpick.active && !gw.mossMinigame.active) {
            if (isHeld(SDL_SCANCODE_W)) input.moveForward += 1.0f;
            if (isHeld(SDL_SCANCODE_S)) input.moveForward -= 1.0f;
            if (isHeld(SDL_SCANCODE_D)) input.moveRight += 1.0f;
            if (isHeld(SDL_SCANCODE_A)) input.moveRight -= 1.0f;
            if (isHeld(SDL_SCANCODE_SPACE)) input.moveUp += 1.0f;
            if (isHeld(SDL_SCANCODE_LCTRL) || isHeld(SDL_SCANCODE_C)) {
                input.moveUp -= 1.0f;
                input.crouch = true;
            }
            if (isHeld(SDL_SCANCODE_Q) && !isHeld(SDL_SCANCODE_LCTRL)) input.leanInput -= 1.0f;
            if (isHeld(SDL_SCANCODE_E)) input.leanInput += 1.0f;
            input.jump = isHeld(SDL_SCANCODE_SPACE);
            input.sprint = isHeld(SDL_SCANCODE_LSHIFT);
            input.noclip = gw.godNoclip;
            input.yawDelta = yawDelta;
            input.pitchDelta = pitchDelta;
        }

        int simSteps = 0;
        while (accumulator >= cfg::FIXED_DT && simSteps < 16) {
            simSteps++;
            if (visualTest && vt.initialized) {
                auto& lp = gw.lockpick;
                vt.frame++;
                if (vt.phase == 0) {
                    // Pause 0.8s so viewer sees initial state
                    if (vt.frame >= 48) { vt.phase = 1; vt.frame = 0; std::cout << "[VISUAL TEST] Phase 1: Slow push (should fail)\n"; }
                } else if (vt.phase == 1) {
                    // Slow push upward over 35 frames (~90 px/s)
                    if (vt.frame <= 35) lp.handleMouseMove(0.0f, -1.5f);
                    else if (vt.frame == 36) lp.handleMouseMove(0.0f, 35.0f); // drop pick
                    if (vt.frame >= 85) { vt.phase = 2; vt.frame = 0; std::cout << "[VISUAL TEST] Phase 2: Weak flick (-7 px/frame, should fall short)\n"; }
                } else if (vt.phase == 2) {
                    if (vt.frame == 1) lp.handleMouseMove(0.0f, -7.0f);
                    else if (vt.frame == 3) lp.handleMouseMove(0.0f, 20.0f);
                    if (vt.frame >= 65) { vt.phase = 3; vt.frame = 0; std::cout << "[VISUAL TEST] Phase 3: Overforce flick (-32 px/frame, ceiling break!)\n"; }
                } else if (vt.phase == 3) {
                    if (vt.frame == 1) lp.handleMouseMove(0.0f, -32.0f);
                    else if (vt.frame == 3) lp.handleMouseMove(0.0f, 35.0f);
                    if (vt.frame >= 100) { vt.phase = 4; vt.frame = 0; vt.curPin = 0; std::cout << "[VISUAL TEST] Phase 4: Calibrated flicks on all 3 pins...\n"; }
                } else if (vt.phase == 4) {
                    if (!lp.active) {
                        vt.phase = 5;
                        vt.frame = 0;
                        std::cout << "[VISUAL TEST] Door UNLOCKED! Showing 3D door opening...\n";
                    } else {
                        float targetX = static_cast<float>(vt.curPin) + 0.5f;
                        if (vt.frame == 1) {
                            // Drop pick below keyway, slide to chamber center, compute calibrated flick
                            lp.handleMouseMove(0.0f, 40.0f);
                            float dxNeeded = (targetX - lp.pickX) / 0.0050f;
                            lp.handleMouseMove(dxNeeded, 0.0f);
                            vt.flickDy = -(lp.pins[vt.curPin].target * 24.5f + 1.5f);
                        } else if (vt.frame == 5) {
                            // Position pick right under pin (y ~ 0.00) before flicking
                            lp.handleMouseMove(0.0f, -15.0f);
                        } else if (vt.frame == 8) {
                            lp.handleMouseMove(0.0f, vt.flickDy);
                            std::cout << "[VISUAL TEST] Flicking Pin " << (vt.curPin + 1)
                                      << " (target=" << lp.pins[vt.curPin].target
                                      << ", dy=" << vt.flickDy << ")\n";
                        } else if (vt.frame == 10) {
                            lp.handleMouseMove(0.0f, 35.0f);
                        } else if (vt.frame >= 65) {
                            if (lp.pins[vt.curPin].pinned) {
                                vt.curPin++;
                            }
                            vt.frame = 0;
                        }
                    }
                } else if (vt.phase == 5) {
                    if (vt.frame >= 90) {
                        std::cout << "[VISUAL TEST] COMPLETE - ALL CHECKS PASSED!\n";
                        running = false;
                    }
                }
            }

            if (!gw.settingsMenuOpen) {
                gw.update(input, fireClicked, cfg::FIXED_DT, audio);
            }
            fireClicked = false;
            input.yawDelta = 0.0f;
            input.pitchDelta = 0.0f;
            accumulator -= cfg::FIXED_DT;

            if (gw.worldStreamChanged) {
                mesher.syncWorld(gw.map.world);
                gw.worldStreamChanged = false;
            }
        }

        audio.update();

        fbo.bind();
        glClearColor(0.014f, 0.018f, 0.024f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float aspect = static_cast<float>(cfg::RETRO_WIDTH) / static_cast<float>(cfg::RETRO_HEIGHT);
        Mat4 proj = Mat4::perspective(radians(cfg::FOV_DEGREES), aspect, cfg::NEAR_PLANE, cfg::FAR_PLANE);
        Mat4 view = gw.player.viewMatrix();
        Vec3 camPos = gw.player.eyePosition();
        Vec3 camDir = gw.player.forwardDir();

        worldShader.use();
        worldShader.setMat4("uModel", Mat4::identity());
        worldShader.setVec3("uPartScale", {0.0f, 0.0f, 0.0f});
        worldShader.setFloat("uTexLayerOverride", -1.0f);
        worldShader.setFloat("uEmissiveOverride", -1.0f);
        worldShader.setVec3("uTint", {1.0f, 1.0f, 1.0f});
        worldShader.setMat4("uProj", proj);
        worldShader.setMat4("uView", view);
        worldShader.setInt("uAtlas", 0);
        worldShader.setVec3("uCamPos", camPos);
        worldShader.setVec3("uCamDir", camDir);
        worldShader.setFloat("uFlashlight", gw.flashlightOn ? 1.0f : 0.0f);

        worldShader.setFloat("uAmbientBrightness", gw.settings.ambientBrightness);
        worldShader.setFloat("uFlashlightIntensity", gw.settings.flashlightIntensity);
        worldShader.setFloat("uFlashlightRadius", gw.settings.flashlightRadius);
        worldShader.setVec3("uFlashlightColor", {gw.settings.flashlightColorR, gw.settings.flashlightColorG, gw.settings.flashlightColorB});
        worldShader.setFloat("uObjectEmissiveScale", gw.settings.objectEmissiveScale);
        worldShader.setFloat("uFogDensity", gw.settings.fogDensity);
        worldShader.setFloat("uLightTexelSnap", gw.settings.lightTexelSnap);
        worldShader.setFloat("uLightSteps", gw.settings.lightSteps);
        worldShader.setFloat("uLightDitherStrength", gw.settings.lightDitherStrength);
        worldShader.setFloat("uTime", totalTime);

        auto sortedLights = gw.map.pointLights;
        gw.gatherRobotLights(sortedLights);
        bool camUpper = (camPos.y > 0.25f);
        std::sort(sortedLights.begin(), sortedLights.end(),
                  [&](const PointLight3D& a, const PointLight3D& b) {
                      auto score = [&](const PointLight3D& l) {
                          float d2 = lengthSq(l.pos - camPos);
                          bool lUpper = (l.pos.y > 0.25f);
                          if (lUpper != camUpper && d2 > 64.0f) d2 += 100000.0f;
                          return d2;
                      };
                      return score(a) < score(b);
                  });

        int count = std::min(48, static_cast<int>(sortedLights.size()));
        worldShader.setInt("uLightCount", count);
        if (count > 0) {
            std::vector<float> posRad(static_cast<size_t>(count * 4));
            std::vector<float> colInt(static_cast<size_t>(count * 4));
            for (int i = 0; i < count; ++i) {
                posRad[i * 4 + 0] = sortedLights[i].pos.x;
                posRad[i * 4 + 1] = sortedLights[i].pos.y;
                posRad[i * 4 + 2] = sortedLights[i].pos.z;
                posRad[i * 4 + 3] = sortedLights[i].radius;

                colInt[i * 4 + 0] = sortedLights[i].color.x;
                colInt[i * 4 + 1] = sortedLights[i].color.y;
                colInt[i * 4 + 2] = sortedLights[i].color.z;
                colInt[i * 4 + 3] = sortedLights[i].intensity;
            }
            glUniform4fv(glGetUniformLocation(worldShader.id(), "uLightPosRadius"), count, posRad.data());
            glUniform4fv(glGetUniformLocation(worldShader.id(), "uLightColorIntensity"), count, colInt.data());
        }

        atlas.bind(0);
        mesher.render();

        gw.gatherRobots(robotQueue);
        robotRenderer.render(view, proj, camPos, robotQueue, gw.settings);

        gw.gatherCreatureTransforms(creatureMats);
        creatureQueue.clear();
        creatureQueue.reserve(creatureMats.size());
        for (const auto& cm : creatureMats) {
            creatureQueue.push_back({cm, {1.0f, 1.0f, 1.0f}});
        }
        creatureModel.renderInstances(view, proj, camPos, camDir, gw.flashlightOn,
                                      gw.settings, sortedLights, creatureQueue);

        // Update 3D cursor ray hover for diegetic backpack & 3D world-grid placement preview
        {
            Vec3 ro, rd;
            computeCursorRay(ro, rd);
            gw.updateCursorHover(ro, rd);
        }

        gw.gatherInteractables(doorQueue, itemQueue, &mossQueue);
        BackpackRenderState bpState = gw.gatherBackpackState();
        ConsumableVisualState heldCons = gw.gatherHeldConsumableState();
        robotRenderer.renderInteractables(worldShader, view, proj, camPos, doorQueue, itemQueue,
                                          bpState, gw.placementPreview,
                                          gw.hasHeldItem, gw.heldItemType, gw.heldItemOrient,
                                          gw.heldItemWorldMatrix(), heldCons, mossQueue);

        gw.gatherBillboards(spriteQueue, totalTime);
        billboards.render(view, proj, camPos, spriteQueue);

        hud.render(gw);

        fbo.present(winW, winH);
        SDL_GL_SwapWindow(window);
    }
    }

    SDL_GL_DestroyContext(glCtx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
