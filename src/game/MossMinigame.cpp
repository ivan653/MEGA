#include "game/MossMinigame.hpp"
#include "core/Rng.hpp"
#include <algorithm>
#include <cmath>

namespace mega {

void MossMinigame::open(int mossIdx, uint64_t seed, const Vec3* worldPos, const Vec3* normal) {
    active = true;
    targetMossIdx = mossIdx;
    completedJustNow = false;
    scrapingHeld = false;
    cellsScrapedThisTick = 0;
    finishTimer = 0.0f;
    finishing = false;
    chunks.clear();

    targetX = 0.75f;
    targetY = 0.72f;
    knifeX = 0.75f;
    knifeY = 0.72f;
    rngState = seed ^ 0x9E3779B97F4A7C15ULL;
    if (worldPos) {
        patchWorldPos = *worldPos;
        hasWorldPos = true;
    } else {
        hasWorldPos = false;
    }
    if (normal) patchNormal = *normal;

    // Generate organic irregular moss patch silhouette (ragged edges + hanging lower tendrils like sketch!)
    cells.fill(0);
    initialMossCells = 0;
    Rng rng(seed ^ 0x4D4F5353BEEFULL);
    float phase1 = rng.nextFloat() * 6.28f;
    float phase2 = rng.nextFloat() * 6.28f;
    float phase3 = rng.nextFloat() * 6.28f;

    for (int gy = 0; gy < GRID_H; ++gy) {
        for (int gx = 0; gx < GRID_W; ++gx) {
            float nx = (gx + 0.5f) / static_cast<float>(GRID_W); // 0..1
            float ny = (gy + 0.5f) / static_cast<float>(GRID_H); // 0..1

            float leftEdge   = 0.11f + 0.05f * std::sin(ny * 11.0f + phase1);
            float rightEdge  = 0.89f + 0.05f * std::cos(ny * 9.5f + phase2);
            float topEdge    = 0.09f + 0.04f * std::sin(nx * 10.0f + phase3);
            // Jagged hanging tendrils along bottom edge
            float tendril    = 0.09f * std::sin(nx * 17.0f + phase1) + 0.05f * std::cos(nx * 31.0f + phase2);
            float bottomEdge = 0.84f + tendril;

            if (nx >= leftEdge && nx <= rightEdge && ny >= topEdge && ny <= bottomEdge) {
                uint8_t shade = static_cast<uint8_t>(1 + ((gx * 7 + gy * 13 + rng.rangeInt(0, 5)) % 3));
                cells[static_cast<size_t>(gy * GRID_W + gx)] = shade;
                initialMossCells++;
            }
        }
    }
    remainingMossCells = initialMossCells;
}

void MossMinigame::close() {
    active = false;
    scrapingHeld = false;
    finishing = false;
    chunks.clear();
}

void MossMinigame::handleMouseMove(float dx, float dy) {
    if (!active) return;
    targetX = clampf(targetX + dx * 0.0026f, -0.12f, 1.12f);
    targetY = clampf(targetY + dy * 0.0026f, -0.12f, 1.12f);
}

float MossMinigame::progress() const {
    if (initialMossCells <= 0) return 1.0f;
    return clampf(1.0f - static_cast<float>(remainingMossCells) / static_cast<float>(initialMossCells), 0.0f, 1.0f);
}

void MossMinigame::update(float dt, AudioEngine& audio) {
    if (!active) return;
    cellsScrapedThisTick = 0;

    Rng rng(rngState);
    rngState = rngState * 6364136223846793005ULL + 1442695040888963407ULL;

    // Update falling moss chunks (gravity + slight tumble)
    for (auto& c : chunks) {
        c.life -= dt;
        c.vy += 1.95f * dt;
        c.x += c.vx * dt;
        c.y += c.vy * dt;
        c.rot += c.vrot * dt;
    }
    chunks.erase(
        std::remove_if(chunks.begin(), chunks.end(),
                       [](const MossFallingChunk& c) { return c.life <= 0.0f || c.y > 1.45f; }),
        chunks.end());

    if (finishing) {
        finishTimer -= dt;
        if (finishTimer <= 0.0f) {
            completedJustNow = true;
        }
        return;
    }

    // Move knife smoothly toward target; cap speed while scraping so player scrapes methodically!
    float dx = targetX - knifeX;
    float dy = targetY - knifeY;
    float dist = std::hypot(dx, dy);
    float maxSpeed = scrapingHeld ? 1.55f : 5.5f; // Methodical scraping speed when LMB pressed against wall
    float maxStep = maxSpeed * dt;

    float prevX = knifeX;
    float prevY = knifeY;
    if (dist > 1e-5f) {
        float step = std::min(dist, maxStep);
        knifeX += (dx / dist) * step;
        knifeY += (dy / dist) * step;
    }

    float movedDist = std::hypot(knifeX - prevX, knifeY - prevY);

    // Scrape moss cells along the angled steel blade ONLY when holding LMB and moving the blade!
    if (scrapingHeld && movedDist > 0.0008f) {
        // Blade axis: angled ~32 deg from vertical (pointing up-left from bolster to tip, matching sketch!)
        // Bolster at (+0.07, +0.11) from blade center; tip at (-0.07, -0.11) -> total blade length ~0.26
        constexpr float BLADE_DX = -0.068f;
        constexpr float BLADE_DY = -0.112f;

        int subSteps = std::max(1, static_cast<int>(std::ceil(movedDist / 0.012f)));
        int scrapedNow = 0;

        for (int s = 1; s <= subSteps; ++s) {
            float t = static_cast<float>(s) / static_cast<float>(subSteps);
            float cx = prevX + (knifeX - prevX) * t;
            float cy = prevY + (knifeY - prevY) * t;

            // Sample along the cutting edge from bolster to tip
            for (int b = -5; b <= 5; ++b) {
                float bf = static_cast<float>(b) / 5.0f;
                float bx = cx + BLADE_DX * bf;
                float by = cy + BLADE_DY * bf;

                int gx = static_cast<int>(std::floor(bx * GRID_W));
                int gy = static_cast<int>(std::floor(by * GRID_H));
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        if (ox * ox + oy * oy > 1) continue; // Crisp narrow blade swath (~3 cells wide)
                        int tx = gx + ox;
                        int ty = gy + oy;
                        if (tx < 0 || tx >= GRID_W || ty < 0 || ty >= GRID_H) continue;
                        size_t idx = static_cast<size_t>(ty * GRID_W + tx);
                        uint8_t oldShade = cells[idx];
                        if (oldShade != 0) {
                            cells[idx] = 0;
                            remainingMossCells = std::max(0, remainingMossCells - 1);
                            scrapedNow++;

                            // Spawn falling moss chunks & crumbs right beneath the scraping blade!
                            if (chunks.size() < 160 && (scrapedNow % 2 == 1 || rng.chance(0.45f))) {
                                MossFallingChunk ch;
                                ch.x = (tx + 0.5f) / static_cast<float>(GRID_W) + (rng.nextFloat() - 0.5f) * 0.018f;
                                ch.y = (ty + 0.5f) / static_cast<float>(GRID_H) + 0.01f;
                                ch.vx = (rng.nextFloat() - 0.5f) * 0.22f;
                                ch.vy = 0.08f + rng.nextFloat() * 0.24f;
                                // Mix of small crumbs (4..6px) and larger torn moss clumps (8..14px) like the sketch!
                                ch.size = rng.chance(0.28f) ? (8.5f + rng.nextFloat() * 5.5f)
                                                            : (4.0f + rng.nextFloat() * 3.5f);
                                ch.rot = rng.nextFloat() * 6.28f;
                                ch.vrot = (rng.nextFloat() - 0.5f) * 7.0f;
                                ch.shade = static_cast<uint8_t>(std::clamp<int>(oldShade - 1, 0, 2));
                                ch.life = 0.95f + rng.nextFloat() * 0.45f;
                                chunks.push_back(ch);
                            }
                        }
                    }
                }
            }
        }

        cellsScrapedThisTick = scrapedNow;
        if (scrapedNow > 0 && rng.chance(0.45f)) {
            if (hasWorldPos) {
                audio.play3D(SoundId::WrapperOpen, patchWorldPos, 0.42f, 0.7f, 0.4f);
            } else {
                audio.play(SoundId::WrapperOpen, 0.42f, 0.7f, 0.4f);
            }
        }

        // Once >= 92% of the moss is scraped clean, automatically crumble off the last tiny stray flecks!
        if (initialMossCells > 0 && remainingMossCells <= initialMossCells * 8 / 100) {
            for (int gy = 0; gy < GRID_H; ++gy) {
                for (int gx = 0; gx < GRID_W; ++gx) {
                    size_t idx = static_cast<size_t>(gy * GRID_W + gx);
                    if (cells[idx] != 0) {
                        MossFallingChunk ch;
                        ch.x = (gx + 0.5f) / static_cast<float>(GRID_W);
                        ch.y = (gy + 0.5f) / static_cast<float>(GRID_H);
                        ch.vx = (rng.nextFloat() - 0.5f) * 0.20f;
                        ch.vy = 0.12f + rng.nextFloat() * 0.20f;
                        ch.size = 5.0f + rng.nextFloat() * 4.0f;
                        ch.shade = static_cast<uint8_t>(std::clamp<int>(cells[idx] - 1, 0, 2));
                        ch.life = 0.8f;
                        if (chunks.size() < 180) chunks.push_back(ch);
                        cells[idx] = 0;
                    }
                }
            }
            remainingMossCells = 0;
            finishing = true;
            finishTimer = 0.28f; // Brief moment to watch final moss clumps fall before closing!
        }
    }
}

} // namespace mega
