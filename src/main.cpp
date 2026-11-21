#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Math3D.hpp"
#include "game/GameWorld.hpp"
#include "render/BillboardRenderer.hpp"
#include "render/Framebuffer.hpp"
#include "render/HudRenderer.hpp"
#include "render/RobotRenderer.hpp"
#include "render/Shader.hpp"
#include "render/TextureAtlas.hpp"
#include "render/WorldMesher.hpp"

#include <SDL3/SDL.h>
#include <epoxy/gl.h>
#include <algorithm>
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
uniform float uFogDensity;
uniform float uLightTexelSnap;
uniform float uLightSteps;
uniform float uLightDitherStrength;

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
    vec4 tex = texture(uAtlas, vUVLayer);
    if (tex.a < 0.5) discard;

    if (vEmissive > 0.5) {
        vec3 lampCol = tex.rgb * vec3(1.55, 1.75, 2.05);
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
    vec2 snappedUV = floor(vUVLayer.xy * 32.0) / 32.0 + vec2(0.5 / 32.0);
    float h0 = texLuma(vec3(snappedUV, vUVLayer.z));
    float hx = texLuma(vec3(snappedUV + vec2(eps, 0.0), vUVLayer.z)) - h0;
    float hy = texLuma(vec3(snappedUV + vec2(0.0, eps), vUVLayer.z)) - h0;
    N = normalize(N - (T * hx + B * hy) * 1.45);

    int layerId = int(vUVLayer.z + 0.5);
    float specStrength = 0.15;
    float shininess = 16.0;
    if (layerId == 2 || layerId == 4) {
        specStrength = 0.65;
        shininess = 32.0;
    } else if (layerId == 5) {
        specStrength = 0.42;
        shininess = 24.0;
    } else if (layerId == 3) {
        specStrength = 0.12;
        shininess = 12.0;
    }

    vec3 V = normalize(uCamPos - samplePos);
    vec3 totalDiffuse = vec3(0.82, 0.90, 1.0) * uAmbientBrightness * vAO;
    vec3 totalSpec = vec3(0.0);

    for (int i = 0; i < uLightCount; ++i) {
        vec3 lPos = uLightPosRadius[i].xyz;
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
                vec3 fCol = vec3(0.88, 0.93, 1.0) * uFlashlightIntensity;
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

int main(int, char**) {
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
    SDL_SetWindowRelativeMouseMode(window, true);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

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

    HudRenderer hud;
    if (!hud.init(cfg::RETRO_WIDTH, cfg::RETRO_HEIGHT)) return 1;

    AudioEngine audio;
    audio.init();

    GameWorld gw;
    gw.settings = GameSettings::loadOrCreate("mega.cfg");
    uint64_t worldSeed = 0x4D4547410001ULL;
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
    bool mouseCaptured = true;
    bool keyDown[SDL_SCANCODE_COUNT] = {false};
    uint64_t prevTicks = SDL_GetTicks();
    float accumulator = 0.0f;
    float totalTime = 0.0f;

    int winW = cfg::WINDOW_WIDTH;
    int winH = cfg::WINDOW_HEIGHT;
    std::vector<BillboardInstance> spriteQueue;
    std::vector<RobotRenderState> robotQueue;
    std::vector<DoorRenderState> doorQueue;
    std::vector<ItemRenderState> itemQueue;

    auto reloadConfig = [&]() {
        gw.settings = GameSettings::loadOrCreate("mega.cfg");
        for (auto& l : gw.map.pointLights) {
            l.color = {gw.settings.lampColorR, gw.settings.lampColorG, gw.settings.lampColorB};
            l.intensity = gw.settings.lampIntensity;
            l.radius = gw.settings.lampRadius;
        }
        std::cout << "Reloaded mega.cfg (ambient=" << gw.settings.ambientBrightness
                  << " lampIntensity=" << gw.settings.lampIntensity
                  << " lampRadius=" << gw.settings.lampRadius << ")\n";
    };

    while (running) {
        uint64_t nowTicks = SDL_GetTicks();
        float frameDt = static_cast<float>(nowTicks - prevTicks) / 1000.0f;
        prevTicks = nowTicks;
        if (frameDt > 0.1f) frameDt = 0.1f;
        accumulator += frameDt;
        totalTime += frameDt;

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
                    if (gw.inventoryOpen) {
                        gw.inventoryOpen = false;
                        mouseCaptured = true;
                        SDL_SetWindowRelativeMouseMode(window, true);
                    } else {
                        mouseCaptured = !mouseCaptured;
                        SDL_SetWindowRelativeMouseMode(window, mouseCaptured);
                    }
                } else if (ev.key.key == SDLK_TAB || ev.key.key == SDLK_I) {
                    gw.inventoryOpen = !gw.inventoryOpen;
                    gw.endInteract();
                    mouseCaptured = !gw.inventoryOpen;
                    SDL_SetWindowRelativeMouseMode(window, mouseCaptured);
                } else if (ev.key.key == SDLK_F) {
                    if (gw.inventory.hasFlashlight && gw.inventory.flashlightBattery > 0.0f) {
                        gw.flashlightOn = !gw.flashlightOn;
                    }
                } else if (ev.key.key == SDLK_E && !gw.inventoryOpen) {
                    gw.beginInteract(audio);
                    gw.endInteract();
                } else if (ev.key.key == SDLK_1) {
                    gw.useInventoryItem(ItemType::Battery, audio);
                } else if (ev.key.key == SDLK_2) {
                    gw.useInventoryItem(ItemType::Food, audio);
                } else if (ev.key.key == SDLK_3) {
                    gw.useInventoryItem(ItemType::Water, audio);
                } else if (ev.key.key == SDLK_M) {
                    gw.showMinimap = !gw.showMinimap;
                } else if (ev.key.key == SDLK_F5) {
                    reloadConfig();
                } else if (ev.key.key == SDLK_R) {
                    reloadConfig();
                    worldSeed += 1337ULL;
                    gw.startNewRun(worldSeed);
                    mesher.syncWorld(gw.map.world);
                    gw.worldStreamChanged = false;
                } else if (ev.key.key == SDLK_Q && (ev.key.mod & SDL_KMOD_CTRL)) {
                    running = false;
                }
            } else if (ev.type == SDL_EVENT_MOUSE_MOTION && mouseCaptured) {
                if (gw.grabbedDoorIdx >= 0) {
                    gw.dragGrabbedDoor(ev.motion.xrel, ev.motion.yrel);
                } else {
                    yawDelta += ev.motion.xrel * cfg::MOUSE_SENS;
                    pitchDelta -= ev.motion.yrel * cfg::MOUSE_SENS;
                }
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    gw.endInteract();
                }
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (gw.inventoryOpen && ev.button.button == SDL_BUTTON_LEFT) {
                    // Convert click to HUD coordinates (1280x720)
                    float mx = ev.button.x * (static_cast<float>(cfg::RETRO_WIDTH) / std::max(1, winW));
                    float my = ev.button.y * (static_cast<float>(cfg::RETRO_HEIGHT) / std::max(1, winH));
                    int ix = cfg::RETRO_WIDTH / 2 - 180;
                    int iy = cfg::RETRO_HEIGHT / 2 - 125;
                    if (mx >= ix + 18 && mx <= ix + 342) {
                        if (my >= iy + 86 && my <= iy + 122) gw.useInventoryItem(ItemType::Battery, audio);
                        else if (my >= iy + 132 && my <= iy + 168) gw.useInventoryItem(ItemType::Food, audio);
                        else if (my >= iy + 178 && my <= iy + 214) gw.useInventoryItem(ItemType::Water, audio);
                    }
                } else if (!mouseCaptured) {
                    mouseCaptured = true;
                    SDL_SetWindowRelativeMouseMode(window, true);
                } else if (ev.button.button == SDL_BUTTON_LEFT) {
                    if (!gw.beginInteract(audio)) {
                        fireClicked = true;
                    }
                }
            }
        }

        const bool* sdlKeys = SDL_GetKeyboardState(nullptr);
        auto isHeld = [&](SDL_Scancode sc) {
            return keyDown[sc] || (sdlKeys && sdlKeys[sc]);
        };

        PlayerInput input;
        if (isHeld(SDL_SCANCODE_W)) input.moveForward += 1.0f;
        if (isHeld(SDL_SCANCODE_S)) input.moveForward -= 1.0f;
        if (isHeld(SDL_SCANCODE_D)) input.moveRight += 1.0f;
        if (isHeld(SDL_SCANCODE_A)) input.moveRight -= 1.0f;
        input.jump = isHeld(SDL_SCANCODE_SPACE);
        input.sprint = isHeld(SDL_SCANCODE_LSHIFT);
        input.yawDelta = yawDelta;
        input.pitchDelta = pitchDelta;

        while (accumulator >= cfg::FIXED_DT) {
            gw.update(input, fireClicked, cfg::FIXED_DT, audio);
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
        worldShader.setFloat("uFogDensity", gw.settings.fogDensity);
        worldShader.setFloat("uLightTexelSnap", gw.settings.lightTexelSnap);
        worldShader.setFloat("uLightSteps", gw.settings.lightSteps);
        worldShader.setFloat("uLightDitherStrength", gw.settings.lightDitherStrength);

        auto sortedLights = gw.map.pointLights;
        gw.gatherRobotLights(sortedLights);
        std::sort(sortedLights.begin(), sortedLights.end(),
                  [&](const PointLight3D& a, const PointLight3D& b) {
                      return lengthSq(a.pos - camPos) < lengthSq(b.pos - camPos);
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
        robotRenderer.render(view, proj, camPos, robotQueue);

        gw.gatherInteractables(doorQueue, itemQueue);
        robotRenderer.renderInteractables(worldShader, view, proj, camPos, doorQueue, itemQueue);

        gw.gatherBillboards(spriteQueue, totalTime);
        billboards.render(view, proj, camPos, spriteQueue);

        hud.render(gw);

        fbo.present(winW, winH);
        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DestroyContext(glCtx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
