// =============================================================================
// OWNER: Developer A (Graphics & Shaders)
// FILE:  src/graphics/Renderer.cpp
// DESC:  Implementation of Raylib OpenGL rendering pipeline, GLSL 330 shader
//        management, vector ship geometry, thrust particles, and neon HUD.
// =============================================================================

#include "graphics/Renderer.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
    // Player 1: Electric Neon Cyan
    constexpr Color NEON_CYAN_CORE  = { 190, 250, 255, 255 };
    constexpr Color NEON_CYAN       = {   0, 235, 255, 255 };
    constexpr Color NEON_CYAN_GLOW  = {   0, 180, 255, 110 };

    // Player 2: Hot Neon Magenta
    constexpr Color NEON_MAG_CORE   = { 255, 200, 245, 255 };
    constexpr Color NEON_MAGENTA    = { 255,  30, 195, 255 };
    constexpr Color NEON_MAG_GLOW   = { 230,   0, 160, 110 };

    // Arena & UI Accents
    constexpr Color NEON_BORDER     = {  55, 130, 255, 180 };
    constexpr Color HUD_BG_DARK     = {  10,  12,  26, 200 };

    struct ResolutionPreset {
        int         width;
        int         height;
        const char* label;
    };

    constexpr ResolutionPreset RES_PRESETS[] = {
        { 1280,  720, "1280x720 (HD)"      },
        { 1600,  900, "1600x900 (HD+)"     },
        { 1920, 1080, "1920x1080 (Full HD)" },
        { 2560, 1440, "2560x1440 (QHD)"    }
    };
    constexpr int NUM_RES_PRESETS = static_cast<int>(sizeof(RES_PRESETS) / sizeof(RES_PRESETS[0]));
}

Renderer::~Renderer() {
    shutdown();
}

std::string Renderer::resolveAssetPath(const std::string& relativePath) const {
    if (FileExists(relativePath.c_str())) {
        return relativePath;
    }

    const std::string parentPath = "../" + relativePath;
    if (FileExists(parentPath.c_str())) {
        return parentPath;
    }

    const char* appDir = GetApplicationDirectory();
    if (appDir) {
        const std::string appRelative = std::string(appDir) + relativePath;
        if (FileExists(appRelative.c_str())) {
            return appRelative;
        }
        const std::string appParentRelative = std::string(appDir) + "../" + relativePath;
        if (FileExists(appParentRelative.c_str())) {
            return appParentRelative;
        }
    }

    return relativePath;
}

bool Renderer::initialize() {
    if (m_initialized) {
        return true;
    }

    m_resPresetIndex = 0;
    m_isFullscreen   = false;

    InitWindow(Constants::DEFAULT_WINDOW_WIDTH, Constants::DEFAULT_WINDOW_HEIGHT, "1v1 Neon Space Arena");
    SetTargetFPS(Constants::TICK_RATE);
    SetExitKey(KEY_NULL); // Let main loop handle ESC for menu/disconnect navigation

    m_sceneTarget = LoadRenderTexture(Constants::ARENA_WIDTH, Constants::ARENA_HEIGHT);
    SetTextureFilter(m_sceneTarget.texture, TEXTURE_FILTER_BILINEAR);

    // Load GLSL 330 fragment shaders
    const std::string bgShaderPath   = resolveAssetPath("assets/shaders/background.fs");
    const std::string postShaderPath = resolveAssetPath("assets/shaders/postprocess.fs");

    m_bgShader   = LoadShader(nullptr, bgShaderPath.c_str());
    m_postShader = LoadShader(nullptr, postShaderPath.c_str());

    m_bgTimeLoc         = GetShaderLocation(m_bgShader, "uTime");
    m_bgResolutionLoc   = GetShaderLocation(m_bgShader, "uResolution");
    m_postTimeLoc       = GetShaderLocation(m_postShader, "uTime");
    m_postResolutionLoc = GetShaderLocation(m_postShader, "uResolution");

    // Initialize constant resolution uniforms immediately
    const float resolution[2] = { Constants::ARENA_WIDTH_F, Constants::ARENA_HEIGHT_F };
    if (m_bgResolutionLoc >= 0) {
        SetShaderValue(m_bgShader, m_bgResolutionLoc, resolution, SHADER_UNIFORM_VEC2);
    }
    if (m_postResolutionLoc >= 0) {
        SetShaderValue(m_postShader, m_postResolutionLoc, resolution, SHADER_UNIFORM_VEC2);
    }

    m_particles.reserve(512);
    m_initialized = true;
    return true;
}

void Renderer::shutdown() {
    if (!m_initialized) {
        return;
    }

    UnloadShader(m_bgShader);
    UnloadShader(m_postShader);
    UnloadRenderTexture(m_sceneTarget);
    CloseWindow();

    m_initialized = false;
}

void Renderer::cycleResolution(int direction) {
    if (!m_initialized) {
        return;
    }

    m_resPresetIndex = (m_resPresetIndex + direction) % NUM_RES_PRESETS;
    if (m_resPresetIndex < 0) {
        m_resPresetIndex += NUM_RES_PRESETS;
    }

    if (m_isFullscreen) {
        if (IsWindowFullscreen()) {
            ToggleFullscreen();
        }
        m_isFullscreen = false;
    }

    const ResolutionPreset& preset = RES_PRESETS[m_resPresetIndex];
    SetWindowSize(preset.width, preset.height);

    const int monitor = GetCurrentMonitor();
    const int monW = GetMonitorWidth(monitor);
    const int monH = GetMonitorHeight(monitor);
    if (monW >= preset.width && monH >= preset.height) {
        SetWindowPosition((monW - preset.width) / 2, (monH - preset.height) / 2);
    }
}

void Renderer::toggleFullscreenMode() {
    if (!m_initialized) {
        return;
    }

    const int monitor = GetCurrentMonitor();
    if (!m_isFullscreen) {
        const int monW = GetMonitorWidth(monitor);
        const int monH = GetMonitorHeight(monitor);
        SetWindowSize(monW, monH);
        if (!IsWindowFullscreen()) {
            ToggleFullscreen();
        }
        m_isFullscreen = true;
    } else {
        if (IsWindowFullscreen()) {
            ToggleFullscreen();
        }
        m_isFullscreen = false;
        const ResolutionPreset& preset = RES_PRESETS[m_resPresetIndex];
        SetWindowSize(preset.width, preset.height);
        const int monW = GetMonitorWidth(monitor);
        const int monH = GetMonitorHeight(monitor);
        if (monW >= preset.width && monH >= preset.height) {
            SetWindowPosition((monW - preset.width) / 2, (monH - preset.height) / 2);
        }
    }
}

std::string Renderer::getDisplaySettingsLabel() const {
    if (m_isFullscreen) {
        return "FULLSCREEN (" + std::to_string(GetScreenWidth()) + "x" + std::to_string(GetScreenHeight()) + ")";
    }
    return std::string("WINDOWED - ") + RES_PRESETS[m_resPresetIndex].label;
}

void Renderer::updateShaderUniforms(float elapsedTime) {
    // Background shader renders inside the 1920x1080 offscreen arena target
    const float arenaRes[2] = {
        Constants::ARENA_WIDTH_F,
        Constants::ARENA_HEIGHT_F
    };
    const float screenRes[2] = {
        static_cast<float>(GetScreenWidth()),
        static_cast<float>(GetScreenHeight())
    };

    if (m_bgTimeLoc >= 0) {
        SetShaderValue(m_bgShader, m_bgTimeLoc, &elapsedTime, SHADER_UNIFORM_FLOAT);
    }
    if (m_bgResolutionLoc >= 0) {
        SetShaderValue(m_bgShader, m_bgResolutionLoc, arenaRes, SHADER_UNIFORM_VEC2);
    }

    if (m_postTimeLoc >= 0) {
        SetShaderValue(m_postShader, m_postTimeLoc, &elapsedTime, SHADER_UNIFORM_FLOAT);
    }
    if (m_postResolutionLoc >= 0) {
        SetShaderValue(m_postShader, m_postResolutionLoc, screenRes, SHADER_UNIFORM_VEC2);
    }
}

void Renderer::updateParticles(const GameState& state, float dt) {
    // Spawn thrust particles behind active thrusting ships (suppressed while invisible)
    for (int i = 0; i < Constants::MAX_PLAYERS; ++i) {
        const ShipState& ship = state.ships[i];
        if (!ship.active || !ship.isThrusting || ship.stealthTimer > 0.0f) {
            continue;
        }

        const float backAngle = ship.angle + 3.14159265f;
        const float exhaustX = ship.x + std::cos(backAngle) * (Constants::SHIP_RADIUS * 0.85f);
        const float exhaustY = ship.y + std::sin(backAngle) * (Constants::SHIP_RADIUS * 0.85f);

        // Emit 3 particles per frame while thrusting
        for (int p = 0; p < 3; ++p) {
            const float spread = (static_cast<float>(GetRandomValue(-100, 100)) / 100.0f) * 0.45f;
            const float speed  = static_cast<float>(GetRandomValue(90, 210));
            const float pAngle = backAngle + spread;

            ThrustParticle part{};
            part.x       = exhaustX;
            part.y       = exhaustY;
            part.vx      = std::cos(pAngle) * speed + ship.vx * 0.2f;
            part.vy      = std::sin(pAngle) * speed + ship.vy * 0.2f;
            part.maxLife = static_cast<float>(GetRandomValue(18, 36)) / 100.0f;
            part.life    = part.maxLife;
            part.size    = static_cast<float>(GetRandomValue(25, 50)) / 10.0f;
            part.ownerId = static_cast<uint8_t>(i);
            m_particles.push_back(part);
        }
    }

    // Spawn overheat smoke puffs at the front nose when weapon overheats (2s)
    for (int i = 0; i < Constants::MAX_PLAYERS; ++i) {
        const ShipState& ship = state.ships[i];
        if (!ship.active || ship.overheatSmokeTimer <= 0.0f || ship.stealthTimer > 0.0f) {
            continue;
        }

        const float noseX = ship.x + std::cos(ship.angle) * (Constants::SHIP_RADIUS * 1.25f);
        const float noseY = ship.y + std::sin(ship.angle) * (Constants::SHIP_RADIUS * 1.25f);

        for (int p = 0; p < 2; ++p) {
            const float spread = (static_cast<float>(GetRandomValue(-100, 100)) / 100.0f) * 0.75f;
            const float speed  = static_cast<float>(GetRandomValue(45, 115));
            const float pAngle = ship.angle + spread;

            ThrustParticle smoke{};
            smoke.x       = noseX;
            smoke.y       = noseY;
            smoke.vx      = std::cos(pAngle) * speed + ship.vx * 0.35f;
            smoke.vy      = std::sin(pAngle) * speed + ship.vy * 0.35f;
            smoke.maxLife = static_cast<float>(GetRandomValue(42, 72)) / 100.0f;
            smoke.life    = smoke.maxLife;
            smoke.size    = static_cast<float>(GetRandomValue(55, 95)) / 10.0f;
            smoke.ownerId = 3; // 3 = Overheat Cannon Smoke
            m_particles.push_back(smoke);
        }
    }

    // Integrate and prune expired particles
    for (auto& part : m_particles) {
        part.x += part.vx * dt;
        part.y += part.vy * dt;
        part.life -= dt;
    }

    // Emit fiery trail particles behind active comets
    for (int c = 0; c < Constants::MAX_COMETS; ++c) {
        const CometState& comet = state.comets[c];
        if (!comet.active) {
            continue;
        }
        const float speed = std::sqrt(comet.vx * comet.vx + comet.vy * comet.vy);
        if (speed > 1.0f) {
            const float backAngle = std::atan2(-comet.vy, -comet.vx);
            const float spread = (static_cast<float>(GetRandomValue(-100, 100)) / 100.0f) * 0.45f;
            const float pSpeed = static_cast<float>(GetRandomValue(60, 150));

            ThrustParticle part{};
            part.x       = comet.x + std::cos(backAngle) * (comet.radius * 0.6f);
            part.y       = comet.y + std::sin(backAngle) * (comet.radius * 0.6f);
            part.vx      = std::cos(backAngle + spread) * pSpeed;
            part.vy      = std::sin(backAngle + spread) * pSpeed;
            part.maxLife = static_cast<float>(GetRandomValue(20, 42)) / 100.0f;
            part.life    = part.maxLife;
            part.size    = static_cast<float>(GetRandomValue(30, 55)) / 10.0f;
            part.ownerId = 2; // 2 = Fiery Orange Comet Particle
            m_particles.push_back(part);
        }
    }

    m_particles.erase(
        std::remove_if(m_particles.begin(), m_particles.end(),
                       [](const ThrustParticle& p) { return p.life <= 0.0f; }),
        m_particles.end());
}

void Renderer::drawBackgroundShader() {
    BeginShaderMode(m_bgShader);
    DrawRectangle(0, 0, Constants::ARENA_WIDTH, Constants::ARENA_HEIGHT, WHITE);
    EndShaderMode();
}

void Renderer::drawArenaBorder() {
    const float pad = Constants::ARENA_BORDER_PADDING;
    const Rectangle outerRect = {
        pad,
        pad,
        Constants::ARENA_WIDTH_F - pad * 2.0f,
        Constants::ARENA_HEIGHT_F - pad * 2.0f
    };

    // Outer soft halo & crisp inner border line
    DrawRectangleLinesEx(
        { outerRect.x - 2.0f, outerRect.y - 2.0f, outerRect.width + 4.0f, outerRect.height + 4.0f },
        4.0f,
        { 30, 90, 220, 75 });
    DrawRectangleLinesEx(outerRect, 2.0f, NEON_BORDER);

    // Subtle center arena divider line and center circle
    const float centerX = Constants::ARENA_WIDTH_F * 0.5f;
    const float centerY = Constants::ARENA_HEIGHT_F * 0.5f;
    DrawLineEx({ centerX, pad }, { centerX, Constants::ARENA_HEIGHT_F - pad }, 1.5f, { 45, 95, 190, 65 });
    DrawRing({ centerX, centerY }, 58.0f, 60.0f, 0.0f, 360.0f, 48, { 45, 95, 190, 75 });
}

void Renderer::drawParticles() {
    for (const auto& part : m_particles) {
        const float ratio = std::clamp(part.life / part.maxLife, 0.0f, 1.0f);
        const unsigned char alpha = static_cast<unsigned char>(ratio * 240.0f);

        if (part.ownerId == 3) {
            // Overheat smoke expands as it dissipates
            const float puffRadius = part.size * (1.75f - ratio * 0.65f);
            const Color smokeOuter = { 155, 160, 175, static_cast<unsigned char>(ratio * 155.0f) };
            const Color smokeCore  = { 215, 195, 185, static_cast<unsigned char>(ratio * 105.0f) };
            DrawCircleV({ part.x, part.y }, puffRadius, smokeOuter);
            DrawCircleV({ part.x, part.y }, puffRadius * 0.52f, smokeCore);
            continue;
        }

        Color primary = (part.ownerId == 0)
            ? NEON_CYAN
            : (part.ownerId == 1 ? NEON_MAGENTA : Color{ 255, 130, 20, 255 });
        primary.a = alpha;

        Color core = WHITE;
        core.a = static_cast<unsigned char>(ratio * 255.0f);

        DrawCircleV({ part.x, part.y }, part.size * ratio * 1.6f, primary);
        DrawCircleV({ part.x, part.y }, part.size * ratio * 0.7f, core);
    }
}

void Renderer::drawComets(const GameState& state) {
    const float elapsed = static_cast<float>(GetTime());

    for (int i = 0; i < Constants::MAX_COMETS; ++i) {
        const CometState& comet = state.comets[i];
        if (!comet.active) {
            continue;
        }

        const float lifeRatio = std::clamp(comet.lifetime / std::max(comet.maxLife, 0.01f), 0.0f, 1.0f);
        const Vector2 center = { comet.x, comet.y };

        // Glowing plasma tail streaks opposite velocity vector
        const float speed = std::sqrt(comet.vx * comet.vx + comet.vy * comet.vy);
        if (speed > 1.0f) {
            const float dirX = -comet.vx / speed;
            const float dirY = -comet.vy / speed;
            const float perpX = -dirY;
            const float perpY = dirX;

            const Vector2 tailEnd   = { comet.x + dirX * (comet.radius * 3.6f), comet.y + dirY * (comet.radius * 3.6f) };
            const Vector2 tailLeft  = { comet.x + perpX * (comet.radius * 0.7f), comet.y + perpY * (comet.radius * 0.7f) };
            const Vector2 tailRight = { comet.x - perpX * (comet.radius * 0.7f), comet.y - perpY * (comet.radius * 0.7f) };

            DrawLineEx(center, tailEnd, comet.radius * 1.3f, { 255, 80, 10, 95 });
            DrawLineEx(tailLeft, tailEnd, 3.0f, { 255, 155, 30, 180 });
            DrawLineEx(tailRight, tailEnd, 3.0f, { 255, 155, 30, 180 });
            DrawLineEx(center, tailEnd, 2.5f, { 255, 230, 150, 220 });
        }

        // Outer neon orange glow & rotating hexagon core
        DrawCircleV(center, comet.radius * 1.25f, { 255, 95, 15, 85 });
        DrawPolyLinesEx(center, 6, comet.radius, elapsed * 140.0f + i * 60.0f, 3.2f, { 255, 165, 35, 255 });
        DrawPolyLinesEx(center, 6, comet.radius * 0.65f, -elapsed * 180.0f, 2.0f, { 255, 235, 180, 255 });
        DrawCircleV(center, comet.radius * 0.30f, WHITE);

        // Remaining lifetime arc around comet
        DrawRing(center, comet.radius + 4.0f, comet.radius + 6.0f,
                 0.0f, 360.0f * lifeRatio, 24, { 255, 200, 80, 170 });
    }
}

void Renderer::drawPowerUps(const GameState& state) {
    const float elapsed = static_cast<float>(GetTime());

    for (int i = 0; i < Constants::MAX_MAP_POWERUPS; ++i) {
        const PowerUpState& pu = state.powerUps[i];
        if (!pu.active) {
            continue;
        }

        Color puColor = WHITE;
        const char* label = "PWR";

        switch (static_cast<PowerUpType>(pu.type)) {
            case PowerUpType::FireRateBoost:
                puColor = { 255, 215,  40, 255 }; // Gold
                label   = "FR+";
                break;
            case PowerUpType::SpeedBoost:
                puColor = {  50, 255, 130, 255 }; // Neon Green
                label   = "SPD";
                break;
            case PowerUpType::Shotgun:
                puColor = { 255, 120,  35, 255 }; // Neon Orange
                label   = "SGN";
                break;
            case PowerUpType::StealthImmortal:
                puColor = { 175, 130, 255, 255 }; // Electric Violet
                label   = "INV";
                break;
            case PowerUpType::BouncyBullets:
                puColor = {  80, 255, 230, 255 }; // Aqua Lime
                label   = "BNC";
                break;
            case PowerUpType::ExtraLife:
                puColor = { 255,  75, 110, 255 }; // Crimson Pink
                label   = "+HP";
                break;
            case PowerUpType::FullAmmo:
                puColor = {  90, 210, 255, 255 }; // Sky Cyan-Gold
                label   = "AMO";
                break;
            default:
                break;
        }

        const Vector2 center = { pu.x, pu.y };
        const float pulse = 1.0f + 0.12f * std::sin(elapsed * 6.0f + i * 2.0f);
        const float r = Constants::POWERUP_RADIUS * pulse;
        const float lifeRatio = std::clamp(pu.lifetime / Constants::POWERUP_MAP_LIFETIME, 0.0f, 1.0f);

        Color glowCol = puColor;
        glowCol.a = 80;
        DrawCircleV(center, r * 1.35f, glowCol);

        // Rotating neon diamond frame
        DrawPolyLinesEx(center, 4, r, elapsed * 90.0f, 2.8f, puColor);
        DrawCircleLinesV(center, r * 0.78f, WHITE);

        // 6-second decay progress ring
        DrawRing(center, r + 5.0f, r + 7.5f, -90.0f, -90.0f + 360.0f * lifeRatio, 28, puColor);

        const int fontSize = 14;
        const int textW = MeasureText(label, fontSize);
        DrawText(label, static_cast<int>(pu.x) - textW / 2, static_cast<int>(pu.y) - fontSize / 2, fontSize, WHITE);
    }
}

void Renderer::drawVectorShip(const ShipState& ship, int playerId, bool isLocalViewer) {
    if (!ship.active) {
        return;
    }

    // Power-up #4: Invisible & Immortal for 6 seconds
    const bool isStealthed = (ship.stealthTimer > 0.0f);
    if (isStealthed && !isLocalViewer) {
        return; // Completely invisible to the opposing player
    }

    Color mainColor = (playerId == 0) ? NEON_CYAN      : NEON_MAGENTA;
    Color glowColor = (playerId == 0) ? NEON_CYAN_GLOW : NEON_MAG_GLOW;
    Color coreColor = (playerId == 0) ? NEON_CYAN_CORE : NEON_MAG_CORE;

    float drawX = ship.x;
    float drawY = ship.y;

    // Low-health critical warning (1 HP remaining): vibrate ship and flash in red
    const bool isLowHealth = (ship.hp == 1);
    if (isLowHealth) {
        const float elapsed = static_cast<float>(GetTime());
        drawX += std::sin(elapsed * 55.0f + playerId * 3.1f) * 2.6f +
                 static_cast<float>(GetRandomValue(-15, 15)) * 0.1f;
        drawY += std::cos(elapsed * 49.0f + playerId * 1.7f) * 2.6f +
                 static_cast<float>(GetRandomValue(-15, 15)) * 0.1f;

        const bool flashRed = (std::sin(elapsed * 26.0f) > -0.15f);
        if (flashRed) {
            mainColor = { 255,  25,  40, 255 };
            glowColor = { 255,   0,  20, 145 };
            coreColor = { 255, 195, 195, 255 };
        }
    }

    if (isStealthed) {
        // Local player sees a translucent cloaked ghost outline of their own ship
        mainColor.a = 75;
        glowColor.a = 35;
        coreColor.a = 90;
        DrawRing({ drawX, drawY }, Constants::SHIP_RADIUS + 6.0f, Constants::SHIP_RADIUS + 8.0f,
                 0.0f, 360.0f, 28, { 175, 130, 255, 110 });
    }

    const float c = std::cos(ship.angle);
    const float s = std::sin(ship.angle);

    // Helper lambda to transform ship-local coordinates to world space (with vibration offset)
    auto toWorld = [&](float lx, float ly) -> Vector2 {
        return {
            drawX + (lx * c - ly * s),
            drawY + (lx * s + ly * c)
        };
    };

    const float r = Constants::SHIP_RADIUS;
    const Vector2 nose      = toWorld( r * 1.25f,  0.0f);
    const Vector2 leftWing  = toWorld(-r * 0.95f, -r * 0.90f);
    const Vector2 rightWing = toWorld(-r * 0.95f,  r * 0.90f);
    const Vector2 tailIndent = toWorld(-r * 0.45f, 0.0f);
    const Vector2 cockpit   = toWorld( r * 0.35f,  0.0f);

    // 1. Outer thick glow wireframe
    DrawLineEx(nose, leftWing,   6.0f, glowColor);
    DrawLineEx(leftWing, tailIndent, 6.0f, glowColor);
    DrawLineEx(tailIndent, rightWing, 6.0f, glowColor);
    DrawLineEx(rightWing, nose,  6.0f, glowColor);

    // 2. Primary neon vector hull lines
    DrawLineEx(nose, leftWing,   2.8f, mainColor);
    DrawLineEx(leftWing, tailIndent, 2.8f, mainColor);
    DrawLineEx(tailIndent, rightWing, 2.8f, mainColor);
    DrawLineEx(rightWing, nose,  2.8f, mainColor);

    // 3. Inner bright core lines & cockpit diamond
    DrawLineEx(nose, cockpit, 1.8f, coreColor);
    DrawLineEx(cockpit, leftWing, 1.4f, glowColor);
    DrawLineEx(cockpit, rightWing, 1.4f, glowColor);
    DrawCircleV(cockpit, 3.0f, coreColor);

    // 4. Engine thruster flare when thrusting
    if (ship.isThrusting) {
        const float flicker = static_cast<float>(GetRandomValue(85, 135)) / 100.0f;
        const Vector2 flameTip  = toWorld(-r * (1.35f * flicker), 0.0f);
        const Vector2 flameLeft = toWorld(-r * 0.55f, -r * 0.32f);
        const Vector2 flameRight = toWorld(-r * 0.55f, r * 0.32f);

        DrawLineEx(flameLeft, flameTip,  3.5f, mainColor);
        DrawLineEx(flameRight, flameTip, 3.5f, mainColor);
        DrawLineEx(tailIndent, flameTip, 2.0f, WHITE);
    }

    // 5. Glowing overheated cannon tip on the nose when smoking
    if (ship.overheatSmokeTimer > 0.0f) {
        const float glowPulse = 0.7f + 0.3f * std::sin(static_cast<float>(GetTime()) * 22.0f);
        DrawCircleV(nose, 6.5f * glowPulse, { 255, 70, 20, 170 });
        DrawCircleV(nose, 3.0f, { 255, 210, 140, 240 });
    }
}

void Renderer::drawBullets(const GameState& state) {
    for (int i = 0; i < Constants::MAX_BULLETS; ++i) {
        const BulletState& b = state.bullets[i];
        if (!b.active) {
            continue;
        }

        const Color mainColor = (b.ownerId == 0) ? NEON_CYAN      : NEON_MAGENTA;
        const Color glowColor = (b.ownerId == 0) ? NEON_CYAN_GLOW : NEON_MAG_GLOW;

        // Velocity trail vector
        const Vector2 head = { b.x, b.y };
        const Vector2 tail = { b.x - b.vx * 0.028f, b.y - b.vy * 0.028f };

        DrawLineEx(tail, head, Constants::BULLET_RADIUS * 2.2f, glowColor);
        DrawLineEx(tail, head, Constants::BULLET_RADIUS * 1.1f, mainColor);
        DrawCircleV(head, Constants::BULLET_RADIUS * 1.3f, mainColor);
        DrawCircleV(head, Constants::BULLET_RADIUS * 0.65f, WHITE);

        if (b.bouncesOnWalls) {
            DrawRing(head, Constants::BULLET_RADIUS * 1.4f, Constants::BULLET_RADIUS * 1.85f,
                     0.0f, 360.0f, 12, { 80, 255, 230, 220 });
        }
    }
}

void Renderer::drawHUD(const GameState& state,
                       bool isHost,
                       bool isConnected,
                       const std::string& statusMessage,
                       uint32_t pingMs) {
    const ShipState& p1 = state.ships[0];
    const ShipState& p2 = state.ships[1];
    const bool redAlertPulse = (std::sin(static_cast<float>(GetTime()) * 24.0f) > 0.0f);

    // -------------------------------------------------------------------------
    // Player 1 (Left - Cyan) Health Bar & Label
    // -------------------------------------------------------------------------
    const int hudY = 28;
    const int p1X  = 36;
    const Color p1HudCol = (p1.hp == 1 && redAlertPulse) ? Color{ 255, 35, 50, 255 } : NEON_CYAN;
    DrawText("P1 [HOST - CYAN]", p1X, hudY, 18, p1HudCol);

    for (int hp = 0; hp < Constants::SHIP_MAX_HP; ++hp) {
        const Rectangle seg = {
            static_cast<float>(p1X + hp * 52),
            static_cast<float>(hudY + 26),
            44.0f,
            14.0f
        };
        if (hp < p1.hp) {
            DrawRectangleRec(seg, p1HudCol);
            DrawRectangleLinesEx(seg, 1.5f, WHITE);
        } else {
            DrawRectangleRec(seg, { 20, 40, 55, 140 });
            DrawRectangleLinesEx(seg, 1.5f, { 0, 140, 180, 120 });
        }
    }

    // -------------------------------------------------------------------------
    // Player 2 (Right - Magenta) Health Bar & Label
    // -------------------------------------------------------------------------
    const char* p2Label = "P2 [CLIENT - MAGENTA]";
    const int p2LabelWidth = MeasureText(p2Label, 18);
    const int p2RightEdge = Constants::ARENA_WIDTH - 36;
    const Color p2HudCol = (p2.hp == 1 && redAlertPulse) ? Color{ 255, 35, 50, 255 } : NEON_MAGENTA;
    DrawText(p2Label, p2RightEdge - p2LabelWidth, hudY, 18, p2HudCol);

    for (int hp = 0; hp < Constants::SHIP_MAX_HP; ++hp) {
        const Rectangle seg = {
            static_cast<float>(p2RightEdge - (Constants::SHIP_MAX_HP - hp) * 52 + 8),
            static_cast<float>(hudY + 26),
            44.0f,
            14.0f
        };
        if (hp < p2.hp) {
            DrawRectangleRec(seg, p2HudCol);
            DrawRectangleLinesEx(seg, 1.5f, WHITE);
        } else {
            DrawRectangleRec(seg, { 55, 20, 45, 140 });
            DrawRectangleLinesEx(seg, 1.5f, { 180, 0, 130, 120 });
        }
    }

    // -------------------------------------------------------------------------
    // Shooting Energy / Overheat Bars (directly beneath health bars)
    // -------------------------------------------------------------------------
    auto drawAmmoBar = [&](const ShipState& ship, float x, float y, float width, bool rightAligned, Color baseColor) {
        const float ratio = std::clamp(ship.ammo, 0.0f, 1.0f);
        const Rectangle bgRect = { x, y, width, 10.0f };
        DrawRectangleRec(bgRect, { 15, 22, 38, 190 });

        Color fillColor = baseColor;
        if (ship.overheatSmokeTimer > 0.0f) {
            fillColor = redAlertPulse ? Color{ 255, 45, 55, 255 } : Color{ 190, 30, 35, 255 };
        } else if (ship.ammoOverheated) {
            fillColor = { 255, 110, 45, 255 }; // Orange-red slow-recharge penalty indicator
        } else if (ratio < 0.28f) {
            fillColor = { 255, 200, 60, 255 }; // Low ammo warning
        }

        const float fillW = width * ratio;
        if (fillW > 0.5f) {
            const float fillX = rightAligned ? (x + width - fillW) : x;
            DrawRectangleRec({ fillX, y, fillW, 10.0f }, fillColor);
        }

        const Color borderCol = (ship.overheatSmokeTimer > 0.0f || ship.ammoOverheated)
            ? Color{ 255, 80, 70, 220 }
            : Color{ 180, 220, 255, 160 };
        DrawRectangleLinesEx(bgRect, 1.2f, borderCol);

        if (ship.overheatSmokeTimer > 0.0f) {
            const char* warn = "OVERHEAT!";
            const int warnW = MeasureText(warn, 13);
            const int textX = rightAligned ? static_cast<int>(x + width - warnW) : static_cast<int>(x);
            DrawText(warn, textX, static_cast<int>(y + 13.0f), 13, { 255, 70, 70, 255 });
        } else if (ship.ammoOverheated) {
            const char* warn = "SLOW RECHARGE";
            const int warnW = MeasureText(warn, 13);
            const int textX = rightAligned ? static_cast<int>(x + width - warnW) : static_cast<int>(x);
            DrawText(warn, textX, static_cast<int>(y + 13.0f), 13, { 255, 155, 70, 240 });
        }
    };

    const float barWidth = 148.0f;
    const float barY = static_cast<float>(hudY + 46);
    drawAmmoBar(p1, static_cast<float>(p1X), barY, barWidth, false, { 90, 245, 255, 255 });
    drawAmmoBar(p2, static_cast<float>(p2RightEdge) - barWidth, barY, barWidth, true, { 255, 100, 225, 255 });

    // Active Power-Up Status Lists under Shooting Bars
    auto formatActivePowerUps = [](const ShipState& s) -> std::string {
        std::string list;
        if (s.fireRateTimer > 0.0f) {
            list += "[FR+ " + std::to_string(static_cast<int>(std::ceil(s.fireRateTimer))) + "s] ";
        }
        if (s.speedBoostTimer > 0.0f) {
            list += "[SPD+ " + std::to_string(static_cast<int>(std::ceil(s.speedBoostTimer))) + "s] ";
        }
        if (s.stealthTimer > 0.0f) {
            list += "[STEALTH " + std::to_string(static_cast<int>(std::ceil(s.stealthTimer))) + "s] ";
        }
        if (s.shotgunShotsLeft > 0) {
            list += "[SHOTGUN x" + std::to_string(s.shotgunShotsLeft) + "] ";
        }
        if (s.bouncyShotsLeft > 0) {
            list += "[BOUNCE x" + std::to_string(s.bouncyShotsLeft) + "] ";
        }
        return list;
    };

    const int buffOffsetY = (p1.overheatSmokeTimer > 0.0f || p1.ammoOverheated) ? 76 : 64;
    const std::string p1Buffs = formatActivePowerUps(p1);
    if (!p1Buffs.empty()) {
        DrawText(p1Buffs.c_str(), p1X, hudY + buffOffsetY, 15, { 120, 255, 190, 240 });
    }

    const int p2BuffOffsetY = (p2.overheatSmokeTimer > 0.0f || p2.ammoOverheated) ? 76 : 64;
    const std::string p2Buffs = formatActivePowerUps(p2);
    if (!p2Buffs.empty()) {
        const int p2BuffW = MeasureText(p2Buffs.c_str(), 15);
        DrawText(p2Buffs.c_str(), p2RightEdge - p2BuffW, hudY + p2BuffOffsetY, 15, { 255, 160, 235, 240 });
    }

    // -------------------------------------------------------------------------
    // Center Scoreboard
    // -------------------------------------------------------------------------
    const std::string scoreText = std::to_string(p1.score) + "   :   " + std::to_string(p2.score);
    const int scoreFontSize = 36;
    const int scoreWidth = MeasureText(scoreText.c_str(), scoreFontSize);
    const int centerX = Constants::ARENA_WIDTH / 2;

    DrawRectangleRounded(
        { static_cast<float>(centerX - 95), 22.0f, 190.0f, 52.0f },
        0.25f, 8, HUD_BG_DARK);
    DrawRectangleRoundedLinesEx(
        { static_cast<float>(centerX - 95), 22.0f, 190.0f, 52.0f },
        0.25f, 8, 1.5f, NEON_BORDER);
    DrawText(scoreText.c_str(), centerX - scoreWidth / 2, 31, scoreFontSize, WHITE);

    // Role & Network Status Bar at bottom
    const std::string roleBadge = isHost ? "[ROLE: HOST (P1 - CYAN)]  " : "[ROLE: CLIENT (P2 - MAGENTA)]  ";
    const std::string footer = roleBadge + statusMessage + "   |   [ESC] Menu";
    const Color statusCol = isConnected ? Color{ 120, 255, 180, 220 } : Color{ 255, 215, 100, 220 };
    DrawText(footer.c_str(), 36, Constants::ARENA_HEIGHT - 44, 16, statusCol);

    // Performance & Latency Telemetry (bottom right)
    const std::string telemetry = isConnected
        ? ("FPS: " + std::to_string(GetFPS()) + "  |  PING: " + std::to_string(pingMs) + " ms")
        : ("FPS: " + std::to_string(GetFPS()));
    const int telWidth = MeasureText(telemetry.c_str(), 16);
    const Color telColor = (!isConnected || pingMs < 60)
        ? Color{ 140, 240, 255, 220 }
        : (pingMs < 130 ? Color{ 255, 215, 100, 220 } : Color{ 255, 110, 110, 220 });
    DrawText(telemetry.c_str(), Constants::ARENA_WIDTH - 36 - telWidth, Constants::ARENA_HEIGHT - 44, 16, telColor);

    // -------------------------------------------------------------------------
    // Round Winner Announcement Banner
    // -------------------------------------------------------------------------
    if (state.roundBannerTimer > 0.0f && state.lastRoundWinner < 2) {
        const bool p1Won = (state.lastRoundWinner == 0);
        const char* bannerMsg = p1Won
            ? "PLAYER 1 (CYAN) SCORES!  -  NEXT ROUND"
            : "PLAYER 2 (MAGENTA) SCORES!  -  NEXT ROUND";
        const Color bannerCol = p1Won ? NEON_CYAN : NEON_MAGENTA;

        const int bFontSize = 28;
        const int bWidth = MeasureText(bannerMsg, bFontSize);
        const float boxW = static_cast<float>(bWidth + 48);
        const float boxX = (Constants::ARENA_WIDTH_F - boxW) * 0.5f;
        const float boxY = 100.0f;

        DrawRectangleRounded({ boxX, boxY, boxW, 50.0f }, 0.2f, 8, HUD_BG_DARK);
        DrawRectangleRoundedLinesEx({ boxX, boxY, boxW, 50.0f }, 0.2f, 8, 2.0f, bannerCol);
        DrawText(bannerMsg, centerX - bWidth / 2, static_cast<int>(boxY + 11.0f), bFontSize, bannerCol);
    }
}

void Renderer::renderMenu(const std::string& ipInput, const std::string& statusMessage) {
    const float elapsed = static_cast<float>(GetTime());
    updateShaderUniforms(elapsed);

    // 1. Render background shader & decorative orbiting ships to offscreen RenderTexture2D
    BeginTextureMode(m_sceneTarget);
    ClearBackground(BLACK);

    drawBackgroundShader();
    drawArenaBorder();

    // Decorative orbiting preview ships on the menu background
    ShipState demoCyan{};
    demoCyan.x = Constants::ARENA_WIDTH_F * 0.22f + std::cos(elapsed * 1.4f) * 35.0f;
    demoCyan.y = Constants::ARENA_HEIGHT_F * 0.52f + std::sin(elapsed * 1.4f) * 35.0f;
    demoCyan.angle = elapsed * 1.4f + 1.5708f;
    demoCyan.isThrusting = 1;
    demoCyan.active = 1;
    drawVectorShip(demoCyan, 0);

    ShipState demoMagenta{};
    demoMagenta.x = Constants::ARENA_WIDTH_F * 0.78f + std::cos(-elapsed * 1.4f) * 35.0f;
    demoMagenta.y = Constants::ARENA_HEIGHT_F * 0.52f + std::sin(-elapsed * 1.4f) * 35.0f;
    demoMagenta.angle = -elapsed * 1.4f - 1.5708f;
    demoMagenta.isThrusting = 1;
    demoMagenta.active = 1;
    drawVectorShip(demoMagenta, 1);

    EndTextureMode();

    // 2. Composite scene through post-processing bloom/CRT shader & overlay crisp menu text
    const float screenW = static_cast<float>(GetScreenWidth());
    const float screenH = static_cast<float>(GetScreenHeight());
    const float scale   = std::min(screenW / Constants::ARENA_WIDTH_F, screenH / Constants::ARENA_HEIGHT_F);
    const float offsetX = (screenW - Constants::ARENA_WIDTH_F * scale) * 0.5f;
    const float offsetY = (screenH - Constants::ARENA_HEIGHT_F * scale) * 0.5f;

    BeginDrawing();
    ClearBackground(BLACK);

    BeginShaderMode(m_postShader);
    const Rectangle srcRect = {
        0.0f,
        0.0f,
        static_cast<float>(m_sceneTarget.texture.width),
        -static_cast<float>(m_sceneTarget.texture.height)
    };
    const Rectangle dstRect = {
        offsetX,
        offsetY,
        Constants::ARENA_WIDTH_F * scale,
        Constants::ARENA_HEIGHT_F * scale
    };
    DrawTexturePro(m_sceneTarget.texture, srcRect, dstRect, { 0.0f, 0.0f }, 0.0f, WHITE);
    EndShaderMode();

    Camera2D uiCamera{};
    uiCamera.offset = { offsetX, offsetY };
    uiCamera.target = { 0.0f, 0.0f };
    uiCamera.rotation = 0.0f;
    uiCamera.zoom = scale;
    BeginMode2D(uiCamera);

    // Menu central panel
    const float panelW = 740.0f;
    const float panelH = 540.0f;
    const float panelX = (Constants::ARENA_WIDTH_F - panelW) * 0.5f;
    const float panelY = (Constants::ARENA_HEIGHT_F - panelH) * 0.5f;

    DrawRectangleRounded({ panelX, panelY, panelW, panelH }, 0.06f, 12, { 8, 10, 24, 225 });
    DrawRectangleRoundedLinesEx({ panelX, panelY, panelW, panelH }, 0.06f, 12, 2.5f, NEON_CYAN);

    const int centerX = Constants::ARENA_WIDTH / 2;

    const char* title = "1V1 NEON SPACE ARENA";
    const int titleW = MeasureText(title, 44);
    DrawText(title, centerX - titleW / 2, static_cast<int>(panelY + 28), 44, NEON_CYAN);

    const char* subtitle = "C++17  |  RAYLIB GLSL 330 SHADERS  |  ENET UDP MULTIPLAYER";
    const int subW = MeasureText(subtitle, 16);
    DrawText(subtitle, centerX - subW / 2, static_cast<int>(panelY + 80), 16, NEON_MAGENTA);

    // Host Option [1]
    const float optX = panelX + 55.0f;
    DrawText("[1]  HOST GAME  (UDP Port 7777 - Player 1 Cyan)",
             static_cast<int>(optX), static_cast<int>(panelY + 130), 24, NEON_CYAN);

    // Join Option [2] or [ENTER]
    DrawText("[2] or [ENTER]  JOIN GAME  (Player 2 Magenta)",
             static_cast<int>(optX), static_cast<int>(panelY + 172), 24, NEON_MAGENTA);

    // Host IP Input Box
    DrawText("TARGET HOST IP (Type IP, Backspace to edit):",
             static_cast<int>(optX), static_cast<int>(panelY + 218), 17, LIGHTGRAY);

    const Rectangle ipBox = { optX, panelY + 244.0f, panelW - 110.0f, 46.0f };
    DrawRectangleRec(ipBox, { 16, 22, 46, 255 });
    DrawRectangleLinesEx(ipBox, 2.0f, NEON_MAGENTA);

    const bool cursorBlink = ( static_cast<int>(elapsed * 2.5f) % 2 ) == 0;
    const std::string displayIp = ipInput + (cursorBlink ? "_" : "");
    DrawText(displayIp.c_str(),
             static_cast<int>(ipBox.x + 16.0f),
             static_cast<int>(ipBox.y + 11.0f),
             24,
             WHITE);

    // Display & Resolution Selector
    const std::string dispLabel = "DISPLAY:  " + getDisplaySettingsLabel();
    DrawText(dispLabel.c_str(),
             static_cast<int>(optX), static_cast<int>(panelY + 312), 20, NEON_CYAN);
    DrawText("[R] or [LEFT/RIGHT] Change Resolution   |   [F] or [F11] Toggle Fullscreen",
             static_cast<int>(optX), static_cast<int>(panelY + 340), 16, LIGHTGRAY);

    // Controls Reference
    DrawText("CONTROLS:  [W/A/S/D] or [ARROW KEYS] Thrust & Rotate   |   [SPACE] Fire",
             static_cast<int>(optX), static_cast<int>(panelY + 388), 16, { 180, 220, 255, 255 });
    DrawText("ARENA:     1920x1080 Map   |   3 HP per Round   |   [F11] Fullscreen In-Game",
             static_cast<int>(optX), static_cast<int>(panelY + 414), 16, { 180, 220, 255, 255 });

    // Status message line
    const std::string statusLine = "STATUS: " + statusMessage;
    DrawText(statusLine.c_str(),
             static_cast<int>(optX), static_cast<int>(panelY + 475), 17, YELLOW);

    EndMode2D();
    EndDrawing();
}

void Renderer::renderGame(const GameState& state,
                          bool isHost,
                          bool isConnected,
                          const std::string& statusMessage,
                          uint32_t pingMs,
                          float dt) {
    const float elapsed = static_cast<float>(GetTime());
    updateShaderUniforms(elapsed);
    updateParticles(state, dt);

    // -------------------------------------------------------------------------
    // Pass 1: Render scene into offscreen RenderTexture2D (1920x1080 logical map)
    // -------------------------------------------------------------------------
    BeginTextureMode(m_sceneTarget);
    ClearBackground(BLACK);

    // 1a. Procedural starfield & nebula shader background
    drawBackgroundShader();

    // 1b. Neon arena perimeter & markings
    drawArenaBorder();

    // 1c. Engine thrust particles & comet trails
    drawParticles();

    // 1d. Randomly flying comets & map power-ups
    drawComets(state);
    drawPowerUps(state);

    // 1e. Projectiles
    drawBullets(state);

    // 1f. Vector-style neon ships (Player 1 Cyan, Player 2 Magenta)
    drawVectorShip(state.ships[0], 0, isHost);
    drawVectorShip(state.ships[1], 1, !isHost);

    EndTextureMode();

    // -------------------------------------------------------------------------
    // Pass 2: Draw RenderTexture2D through GLSL 330 post-processing bloom/CRT
    //         shader scaled to active resolution, then render crisp HUD overlay
    // -------------------------------------------------------------------------
    const float screenW = static_cast<float>(GetScreenWidth());
    const float screenH = static_cast<float>(GetScreenHeight());
    const float scale   = std::min(screenW / Constants::ARENA_WIDTH_F, screenH / Constants::ARENA_HEIGHT_F);
    const float offsetX = (screenW - Constants::ARENA_WIDTH_F * scale) * 0.5f;
    const float offsetY = (screenH - Constants::ARENA_HEIGHT_F * scale) * 0.5f;

    BeginDrawing();
    ClearBackground(BLACK);

    BeginShaderMode(m_postShader);
    // Note: OpenGL render textures have inverted Y in texture coordinates
    const Rectangle srcRect = {
        0.0f,
        0.0f,
        static_cast<float>(m_sceneTarget.texture.width),
        -static_cast<float>(m_sceneTarget.texture.height)
    };
    const Rectangle dstRect = {
        offsetX,
        offsetY,
        Constants::ARENA_WIDTH_F * scale,
        Constants::ARENA_HEIGHT_F * scale
    };
    DrawTexturePro(m_sceneTarget.texture, srcRect, dstRect, { 0.0f, 0.0f }, 0.0f, WHITE);
    EndShaderMode();

    Camera2D uiCamera{};
    uiCamera.offset = { offsetX, offsetY };
    uiCamera.target = { 0.0f, 0.0f };
    uiCamera.rotation = 0.0f;
    uiCamera.zoom = scale;
    BeginMode2D(uiCamera);

    drawHUD(state, isHost, isConnected, statusMessage, pingMs);

    EndMode2D();
    EndDrawing();
}
