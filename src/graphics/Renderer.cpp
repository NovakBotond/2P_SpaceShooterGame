// =============================================================================
// OWNER: Developer A (Graphics & Shaders)
// FILE:  src/graphics/Renderer.cpp
// DESC:  Implementation of Raylib OpenGL rendering pipeline, GLSL 330 shader
//        management, 5-second studio splash screen, resolution/fullscreen
//        scaling, vector ships, blue bubble shield, comets, co-op enemies &
//        bosses, 13 power-ups, above-ship buff badges, and HUD telemetry.
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

    // Enemies, Allies & Shields
    constexpr Color ENEMY_CRIMSON   = { 255,  45,  70, 255 };
    constexpr Color ENEMY_BOSS_GOLD = { 255, 190,  40, 255 };
    constexpr Color ALLY_MINT       = {  70, 255, 165, 255 };
    constexpr Color SHIELD_BLUE     = {  50, 175, 255, 255 };

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

    struct PowerUpVisualInfo {
        Color       color;
        const char* symbol;
        const char* shortName;
    };

    PowerUpVisualInfo getPowerUpVisual(PowerUpType type) {
        switch (type) {
            case PowerUpType::FireRateBoost:   return { { 255, 215,  40, 255 }, "*",   "FR+" };
            case PowerUpType::SpeedBoost:      return { {  50, 255, 130, 255 }, ">",   "SPD" };
            case PowerUpType::Shotgun:         return { { 255, 120,  35, 255 }, "#",   "SGN" };
            case PowerUpType::StealthImmortal: return { { 175, 130, 255, 255 }, "@",   "INV" };
            case PowerUpType::BouncyBullets:   return { {  80, 255, 230, 255 }, "~",   "BNC" };
            case PowerUpType::ExtraLife:       return { { 255,  75, 110, 255 }, "+",   "+HP" };
            case PowerUpType::FullAmmo:        return { {  90, 210, 255, 255 }, "=",   "AMO" };
            case PowerUpType::RadialBurst:     return { { 255, 255,  90, 255 }, "+",   "360" };
            case PowerUpType::ShieldBubble:    return { {  50, 175, 255, 255 }, "(O)", "SHD" };
            case PowerUpType::EnemySlow:       return { { 115, 200, 255, 255 }, "v",   "SLW" };
            case PowerUpType::EnemyKillRandom: return { { 255,  70,  90, 255 }, "X",   "KIL" };
            case PowerUpType::EnemyFireSlow:   return { { 255, 165,  80, 255 }, "!",   "EFR" };
            case PowerUpType::EnemyAlly:       return { {  70, 255, 165, 255 }, "&",   "ALY" };
            default:                           return { WHITE,                   "?",   "PWR" };
        }
    }
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

    const std::string bgShaderPath   = resolveAssetPath("assets/shaders/background.fs");
    const std::string postShaderPath = resolveAssetPath("assets/shaders/postprocess.fs");

    m_bgShader   = LoadShader(nullptr, bgShaderPath.c_str());
    m_postShader = LoadShader(nullptr, postShaderPath.c_str());

    m_bgTimeLoc         = GetShaderLocation(m_bgShader, "uTime");
    m_bgResolutionLoc   = GetShaderLocation(m_bgShader, "uResolution");
    m_postTimeLoc       = GetShaderLocation(m_postShader, "uTime");
    m_postResolutionLoc = GetShaderLocation(m_postShader, "uResolution");

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
            smoke.ownerId = 3; // Overheat Cannon Smoke
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
            part.ownerId = 2; // Fiery Orange Comet Particle
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

    DrawRectangleLinesEx(
        { outerRect.x - 2.0f, outerRect.y - 2.0f, outerRect.width + 4.0f, outerRect.height + 4.0f },
        4.0f,
        { 30, 90, 220, 75 });
    DrawRectangleLinesEx(outerRect, 2.0f, NEON_BORDER);

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

        DrawCircleV(center, comet.radius * 1.25f, { 255, 95, 15, 85 });
        DrawPolyLinesEx(center, 6, comet.radius, elapsed * 140.0f + i * 60.0f, 3.2f, { 255, 165, 35, 255 });
        DrawPolyLinesEx(center, 6, comet.radius * 0.65f, -elapsed * 180.0f, 2.0f, { 255, 235, 180, 255 });
        DrawCircleV(center, comet.radius * 0.30f, WHITE);

        DrawRing(center, comet.radius + 4.0f, comet.radius + 6.0f,
                 0.0f, 360.0f * lifeRatio, 24, { 255, 200, 80, 170 });

        // 3-hit destructible HP pips above comet
        const float pipW = 10.0f;
        const float pipGap = 3.0f;
        const float totalW = Constants::COMET_MAX_HP * pipW + (Constants::COMET_MAX_HP - 1) * pipGap;
        const float startX = comet.x - totalW * 0.5f;
        const float pipY = comet.y - comet.radius - 14.0f;
        for (int h = 0; h < Constants::COMET_MAX_HP; ++h) {
            const Rectangle pip = { startX + h * (pipW + pipGap), pipY, pipW, 4.0f };
            DrawRectangleRec(pip, (h < comet.hp) ? Color{ 255, 195, 70, 240 } : Color{ 60, 35, 25, 150 });
        }
    }
}

void Renderer::drawPowerUps(const GameState& state) {
    const float elapsed = static_cast<float>(GetTime());

    for (int i = 0; i < Constants::MAX_MAP_POWERUPS; ++i) {
        const PowerUpState& pu = state.powerUps[i];
        if (!pu.active) {
            continue;
        }

        const PowerUpVisualInfo info = getPowerUpVisual(static_cast<PowerUpType>(pu.type));
        const Vector2 center = { pu.x, pu.y };
        const float pulse = 1.0f + 0.12f * std::sin(elapsed * 6.0f + i * 2.0f);
        const float r = Constants::POWERUP_RADIUS * pulse;
        const float lifeRatio = std::clamp(pu.lifetime / Constants::POWERUP_MAP_LIFETIME, 0.0f, 1.0f);

        Color glowCol = info.color;
        glowCol.a = 80;
        DrawCircleV(center, r * 1.35f, glowCol);

        DrawPolyLinesEx(center, 4, r, elapsed * 90.0f, 2.8f, info.color);
        DrawCircleLinesV(center, r * 0.78f, WHITE);

        DrawRing(center, r + 5.0f, r + 7.5f, -90.0f, -90.0f + 360.0f * lifeRatio, 28, info.color);

        const int fontSize = 14;
        const int textW = MeasureText(info.shortName, fontSize);
        DrawText(info.shortName, static_cast<int>(pu.x) - textW / 2, static_cast<int>(pu.y) - fontSize / 2, fontSize, WHITE);
    }
}

void Renderer::drawEnemies(const GameState& state) {
    const float elapsed = static_cast<float>(GetTime());

    for (int i = 0; i < Constants::MAX_ENEMIES; ++i) {
        const EnemyState& enemy = state.enemies[i];
        if (!enemy.active) {
            continue;
        }

        const bool isAlly = (enemy.allyTimer > 0.0f);
        const bool isBoss = (enemy.isBoss != 0);
        const float r = isBoss ? Constants::ENEMY_BOSS_RADIUS : Constants::ENEMY_RADIUS;

        Color mainCol = isAlly ? ALLY_MINT : (isBoss ? ENEMY_BOSS_GOLD : ENEMY_CRIMSON);
        Color glowCol = mainCol;
        glowCol.a = 100;

        const float c = std::cos(enemy.angle);
        const float s = std::sin(enemy.angle);
        auto toWorld = [&](float lx, float ly) -> Vector2 {
            return {
                enemy.x + (lx * c - ly * s),
                enemy.y + (lx * s + ly * c)
            };
        };

        if (isBoss) {
            // Larger multi-ring Heavy Boss Warship
            const Vector2 nose   = toWorld( r * 1.25f,  0.0f);
            const Vector2 lw1    = toWorld( r * 0.15f, -r * 1.05f);
            const Vector2 rw1    = toWorld( r * 0.15f,  r * 1.05f);
            const Vector2 lw2    = toWorld(-r * 0.95f, -r * 0.75f);
            const Vector2 rw2    = toWorld(-r * 0.95f,  r * 0.75f);
            const Vector2 tail   = toWorld(-r * 0.55f,  0.0f);

            DrawPolyLinesEx({ enemy.x, enemy.y }, 8, r * 1.08f, elapsed * 45.0f, 3.0f, glowCol);
            DrawLineEx(nose, lw1, 4.5f, mainCol);
            DrawLineEx(lw1, lw2,  4.5f, mainCol);
            DrawLineEx(lw2, tail, 4.5f, mainCol);
            DrawLineEx(tail, rw2, 4.5f, mainCol);
            DrawLineEx(rw2, rw1,  4.5f, mainCol);
            DrawLineEx(rw1, nose, 4.5f, mainCol);
            DrawCircleV({ enemy.x, enemy.y }, r * 0.35f, isAlly ? ALLY_MINT : ENEMY_CRIMSON);
            DrawCircleV({ enemy.x, enemy.y }, r * 0.18f, WHITE);
        } else {
            // Normal Wave Enemy Interceptor
            const Vector2 nose   = toWorld( r * 1.20f,  0.0f);
            const Vector2 leftW  = toWorld(-r * 0.85f, -r * 0.85f);
            const Vector2 rightW = toWorld(-r * 0.85f,  r * 0.85f);
            const Vector2 back   = toWorld(-r * 0.35f,  0.0f);

            DrawLineEx(nose, leftW,   5.0f, glowCol);
            DrawLineEx(leftW, back,   5.0f, glowCol);
            DrawLineEx(back, rightW,  5.0f, glowCol);
            DrawLineEx(rightW, nose,  5.0f, glowCol);

            DrawLineEx(nose, leftW,   2.5f, mainCol);
            DrawLineEx(leftW, back,   2.5f, mainCol);
            DrawLineEx(back, rightW,  2.5f, mainCol);
            DrawLineEx(rightW, nose,  2.5f, mainCol);
            DrawCircleV({ enemy.x, enemy.y }, 3.0f, WHITE);
        }

        if (isAlly) {
            DrawRing({ enemy.x, enemy.y }, r + 6.0f, r + 8.5f, 0.0f, 360.0f, 28, ALLY_MINT);
            const std::string allyTag = "ALLY " + std::to_string(static_cast<int>(std::ceil(enemy.allyTimer))) + "s";
            const int tw = MeasureText(allyTag.c_str(), 13);
            DrawText(allyTag.c_str(), static_cast<int>(enemy.x) - tw / 2, static_cast<int>(enemy.y - r - 28.0f), 13, ALLY_MINT);
        }

        // Segmented Health Lines above Enemy / Boss (shows HP lines increasing every 2nd wave)
        const int maxHp = std::max<int>(1, enemy.maxHp);
        const float segW = isBoss ? 12.0f : 10.0f;
        const float segH = isBoss ? 6.0f : 4.5f;
        const float gap  = 2.0f;
        const float barW = maxHp * segW + (maxHp - 1) * gap;
        const float startX = enemy.x - barW * 0.5f;
        const float barY   = enemy.y - r - 13.0f;

        for (int h = 0; h < maxHp; ++h) {
            const Rectangle seg = { startX + h * (segW + gap), barY, segW, segH };
            if (h < enemy.hp) {
                DrawRectangleRec(seg, mainCol);
            } else {
                DrawRectangleRec(seg, { 40, 20, 30, 160 });
            }
        }
    }
}

void Renderer::drawVectorShip(const ShipState& ship, int playerId, bool isLocalViewer) {
    if (!ship.active) {
        return;
    }

    const bool isStealthed = (ship.stealthTimer > 0.0f);
    if (isStealthed && !isLocalViewer) {
        return;
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
        mainColor.a = 75;
        glowColor.a = 35;
        coreColor.a = 90;
        DrawRing({ drawX, drawY }, Constants::SHIP_RADIUS + 6.0f, Constants::SHIP_RADIUS + 8.0f,
                 0.0f, 360.0f, 28, { 175, 130, 255, 110 });
    }

    const float c = std::cos(ship.angle);
    const float s = std::sin(ship.angle);

    auto toWorld = [&](float lx, float ly) -> Vector2 {
        return {
            drawX + (lx * c - ly * s),
            drawY + (lx * s + ly * c)
        };
    };

    const float r = Constants::SHIP_RADIUS;
    const Vector2 nose       = toWorld( r * 1.25f,  0.0f);
    const Vector2 leftWing   = toWorld(-r * 0.95f, -r * 0.90f);
    const Vector2 rightWing  = toWorld(-r * 0.95f,  r * 0.90f);
    const Vector2 tailIndent = toWorld(-r * 0.45f,  0.0f);
    const Vector2 cockpit    = toWorld( r * 0.35f,  0.0f);

    DrawLineEx(nose, leftWing,   6.0f, glowColor);
    DrawLineEx(leftWing, tailIndent, 6.0f, glowColor);
    DrawLineEx(tailIndent, rightWing, 6.0f, glowColor);
    DrawLineEx(rightWing, nose,  6.0f, glowColor);

    DrawLineEx(nose, leftWing,   2.8f, mainColor);
    DrawLineEx(leftWing, tailIndent, 2.8f, mainColor);
    DrawLineEx(tailIndent, rightWing, 2.8f, mainColor);
    DrawLineEx(rightWing, nose,  2.8f, mainColor);

    DrawLineEx(nose, cockpit, 1.8f, coreColor);
    DrawLineEx(cockpit, leftWing, 1.4f, glowColor);
    DrawLineEx(cockpit, rightWing, 1.4f, glowColor);
    DrawCircleV(cockpit, 3.0f, coreColor);

    if (ship.isThrusting) {
        const float flicker = static_cast<float>(GetRandomValue(85, 135)) / 100.0f;
        const Vector2 flameTip   = toWorld(-r * (1.35f * flicker), 0.0f);
        const Vector2 flameLeft  = toWorld(-r * 0.55f, -r * 0.32f);
        const Vector2 flameRight = toWorld(-r * 0.55f,  r * 0.32f);

        DrawLineEx(flameLeft, flameTip,  3.5f, mainColor);
        DrawLineEx(flameRight, flameTip, 3.5f, mainColor);
        DrawLineEx(tailIndent, flameTip, 2.0f, WHITE);
    }

    if (ship.overheatSmokeTimer > 0.0f) {
        const float glowPulse = 0.7f + 0.3f * std::sin(static_cast<float>(GetTime()) * 22.0f);
        DrawCircleV(nose, 6.5f * glowPulse, { 255, 70, 20, 170 });
        DrawCircleV(nose, 3.0f, { 255, 210, 140, 240 });
    }

    // Blue Bubble Shield (2 health lines absorbed before disappearing)
    if (ship.shieldHp > 0) {
        const float bubbleR = r + 13.0f;
        const float pulse = 1.0f + 0.05f * std::sin(static_cast<float>(GetTime()) * 7.0f);
        DrawCircleV({ drawX, drawY }, bubbleR * pulse, { 40, 155, 255, 42 });
        DrawRing({ drawX, drawY }, bubbleR * pulse - 1.5f, bubbleR * pulse + 1.0f,
                 0.0f, 360.0f, 36, SHIELD_BLUE);
        if (ship.shieldHp >= 2) {
            DrawRing({ drawX, drawY }, bubbleR * pulse + 3.5f, bubbleR * pulse + 5.0f,
                     0.0f, 360.0f, 36, { 150, 230, 255, 160 });
        }
    }

    drawShipPowerUpBadges(ship, playerId, isLocalViewer);
}

void Renderer::drawShipPowerUpBadges(const ShipState& ship, int /*playerId*/, bool isLocalViewer) {
    if (!ship.active) {
        return;
    }
    if (ship.stealthTimer > 0.0f && !isLocalViewer) {
        return;
    }

    struct BadgeEntry {
        Color       color;
        std::string text;
    };
    std::vector<BadgeEntry> badges;
    badges.reserve(7);

    auto addBadge = [&](PowerUpType type, const std::string& suffix) {
        const PowerUpVisualInfo info = getPowerUpVisual(type);
        badges.push_back({ info.color, std::string(info.symbol) + " " + info.shortName + suffix });
    };

    if (ship.fireRateTimer > 0.0f) {
        addBadge(PowerUpType::FireRateBoost, " " + std::to_string(static_cast<int>(std::ceil(ship.fireRateTimer))) + "s");
    }
    if (ship.speedBoostTimer > 0.0f) {
        addBadge(PowerUpType::SpeedBoost, " " + std::to_string(static_cast<int>(std::ceil(ship.speedBoostTimer))) + "s");
    }
    if (ship.stealthTimer > 0.0f) {
        addBadge(PowerUpType::StealthImmortal, " " + std::to_string(static_cast<int>(std::ceil(ship.stealthTimer))) + "s");
    }
    if (ship.shotgunShotsLeft > 0) {
        addBadge(PowerUpType::Shotgun, " x" + std::to_string(ship.shotgunShotsLeft));
    }
    if (ship.bouncyShotsLeft > 0) {
        addBadge(PowerUpType::BouncyBullets, " x" + std::to_string(ship.bouncyShotsLeft));
    }
    if (ship.radialShotsLeft > 0) {
        addBadge(PowerUpType::RadialBurst, " x" + std::to_string(ship.radialShotsLeft));
    }
    if (ship.shieldHp > 0) {
        addBadge(PowerUpType::ShieldBubble, " x" + std::to_string(ship.shieldHp));
    }

    if (badges.empty()) {
        return;
    }

    const int fontSize = 14;
    const float baseOffset = Constants::SHIP_RADIUS + ((ship.shieldHp > 0) ? 26.0f : 18.0f);
    float y = ship.y - baseOffset;

    for (const BadgeEntry& b : badges) {
        const int tw = MeasureText(b.text.c_str(), fontSize);
        const int tx = static_cast<int>(ship.x) - tw / 2;
        const int ty = static_cast<int>(y) - fontSize;

        DrawRectangleRounded(
            { static_cast<float>(tx - 5), static_cast<float>(ty - 2), static_cast<float>(tw + 10), static_cast<float>(fontSize + 4) },
            0.3f, 4, { 8, 12, 26, 175 });
        DrawText(b.text.c_str(), tx, ty, fontSize, b.color);
        y -= (fontSize + 6.0f);
    }
}

void Renderer::drawBullets(const GameState& state) {
    for (int i = 0; i < Constants::MAX_BULLETS; ++i) {
        const BulletState& b = state.bullets[i];
        if (!b.active) {
            continue;
        }

        Color mainColor = NEON_CYAN;
        Color glowColor = NEON_CYAN_GLOW;
        if (b.ownerId == 1) {
            mainColor = NEON_MAGENTA;
            glowColor = NEON_MAG_GLOW;
        } else if (b.ownerId == 2) {
            mainColor = ENEMY_CRIMSON;
            glowColor = { 255, 50, 30, 120 };
        } else if (b.ownerId == 3) {
            mainColor = ALLY_MINT;
            glowColor = { 60, 255, 160, 120 };
        }

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
    const bool isCoop = (state.gameMode == static_cast<uint8_t>(GameMode::CoopWaves));
    const bool redAlertPulse = (std::sin(static_cast<float>(GetTime()) * 24.0f) > 0.0f);

    // -------------------------------------------------------------------------
    // Player 1 (Left - Cyan) Health Bar & Shield Pips
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

    if (p1.shieldHp > 0) {
        for (int s = 0; s < p1.shieldHp; ++s) {
            const Rectangle sSeg = { static_cast<float>(p1X + 162 + s * 24), static_cast<float>(hudY + 26), 18.0f, 14.0f };
            DrawRectangleRec(sSeg, SHIELD_BLUE);
            DrawRectangleLinesEx(sSeg, 1.5f, WHITE);
        }
    }

    // -------------------------------------------------------------------------
    // Player 2 (Right - Magenta) Health Bar & Shield Pips
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

    if (p2.shieldHp > 0) {
        for (int s = 0; s < p2.shieldHp; ++s) {
            const Rectangle sSeg = { static_cast<float>(p2RightEdge - 180 - s * 24), static_cast<float>(hudY + 26), 18.0f, 14.0f };
            DrawRectangleRec(sSeg, SHIELD_BLUE);
            DrawRectangleLinesEx(sSeg, 1.5f, WHITE);
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
            fillColor = { 255, 110, 45, 255 };
        } else if (ratio < 0.28f) {
            fillColor = { 255, 200, 60, 255 };
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

    auto formatActivePowerUps = [&](const ShipState& s) -> std::string {
        std::string list;
        if (s.shieldHp > 0) {
            list += "[SHD x" + std::to_string(s.shieldHp) + "] ";
        } else if (s.shieldCooldownTimer > 0.0f) {
            list += "[SHD CD " + std::to_string(static_cast<int>(std::ceil(s.shieldCooldownTimer))) + "s] ";
        }
        if (s.fireRateTimer > 0.0f) {
            list += "[FR+ " + std::to_string(static_cast<int>(std::ceil(s.fireRateTimer))) + "s] ";
        }
        if (s.speedBoostTimer > 0.0f) {
            list += "[SPD+ " + std::to_string(static_cast<int>(std::ceil(s.speedBoostTimer))) + "s] ";
        }
        if (s.stealthTimer > 0.0f) {
            list += "[INV " + std::to_string(static_cast<int>(std::ceil(s.stealthTimer))) + "s] ";
        }
        if (s.shotgunShotsLeft > 0) {
            list += "[SGN x" + std::to_string(s.shotgunShotsLeft) + "] ";
        }
        if (s.bouncyShotsLeft > 0) {
            list += "[BNC x" + std::to_string(s.bouncyShotsLeft) + "] ";
        }
        if (s.radialShotsLeft > 0) {
            list += "[360 x" + std::to_string(s.radialShotsLeft) + "] ";
        }
        if (isCoop && s.allyHealHits > 0) {
            list += "[ALLY HEAL " + std::to_string(s.allyHealHits) + "/2] ";
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
    // Center Scoreboard / Co-op Wave Counter
    // -------------------------------------------------------------------------
    const int centerX = Constants::ARENA_WIDTH / 2;
    if (isCoop) {
        const std::string waveText = "WAVE " + std::to_string(state.currentWave) +
                                     "   |   REMAINING: " + std::to_string(state.waveRemainingKills);
        const int wFont = 26;
        const int wTextW = MeasureText(waveText.c_str(), wFont);
        const float boxW = static_cast<float>(wTextW + 52);

        DrawRectangleRounded(
            { static_cast<float>(centerX) - boxW * 0.5f, 22.0f, boxW, 50.0f },
            0.25f, 8, HUD_BG_DARK);
        DrawRectangleRoundedLinesEx(
            { static_cast<float>(centerX) - boxW * 0.5f, 22.0f, boxW, 50.0f },
            0.25f, 8, 1.5f, state.bossActive ? ENEMY_BOSS_GOLD : NEON_CYAN);
        DrawText(waveText.c_str(), centerX - wTextW / 2, 34, wFont, state.bossActive ? ENEMY_BOSS_GOLD : WHITE);

        std::string globalDebuffs;
        if (state.enemySlowTimer > 0.0f) {
            globalDebuffs += "[ENEMIES -30% SPD " + std::to_string(static_cast<int>(std::ceil(state.enemySlowTimer))) + "s]  ";
        }
        if (state.enemyFireSlowTimer > 0.0f) {
            globalDebuffs += "[ENEMIES -30% FIRE " + std::to_string(static_cast<int>(std::ceil(state.enemyFireSlowTimer))) + "s]  ";
        }
        if (!globalDebuffs.empty()) {
            const int gdW = MeasureText(globalDebuffs.c_str(), 16);
            DrawText(globalDebuffs.c_str(), centerX - gdW / 2, 78, 16, { 120, 225, 255, 245 });
        }
    } else {
        const std::string scoreText = std::to_string(p1.score) + "   :   " + std::to_string(p2.score);
        const int scoreFontSize = 36;
        const int scoreWidth = MeasureText(scoreText.c_str(), scoreFontSize);

        DrawRectangleRounded(
            { static_cast<float>(centerX - 95), 22.0f, 190.0f, 52.0f },
            0.25f, 8, HUD_BG_DARK);
        DrawRectangleRoundedLinesEx(
            { static_cast<float>(centerX - 95), 22.0f, 190.0f, 52.0f },
            0.25f, 8, 1.5f, NEON_BORDER);
        DrawText(scoreText.c_str(), centerX - scoreWidth / 2, 31, scoreFontSize, WHITE);
    }

    // Role & Network Status Bar at bottom
    const std::string roleBadge = isHost ? "[ROLE: HOST (P1 - CYAN)]  " : "[ROLE: CLIENT (P2 - MAGENTA)]  ";
    const std::string footer = roleBadge + statusMessage + "   |   [ESC] Menu";
    const Color statusCol = isConnected ? Color{ 120, 255, 180, 220 } : Color{ 255, 215, 100, 220 };
    DrawText(footer.c_str(), 36, Constants::ARENA_HEIGHT - 44, 16, statusCol);

    const std::string telemetry = isConnected
        ? ("FPS: " + std::to_string(GetFPS()) + "  |  PING: " + std::to_string(pingMs) + " ms")
        : ("FPS: " + std::to_string(GetFPS()));
    const int telWidth = MeasureText(telemetry.c_str(), 16);
    const Color telColor = (!isConnected || pingMs < 60)
        ? Color{ 140, 240, 255, 220 }
        : (pingMs < 130 ? Color{ 255, 215, 100, 220 } : Color{ 255, 110, 110, 220 });
    DrawText(telemetry.c_str(), Constants::ARENA_WIDTH - 36 - telWidth, Constants::ARENA_HEIGHT - 44, 16, telColor);

    // -------------------------------------------------------------------------
    // Center Round / Wave Announcement Banner
    // -------------------------------------------------------------------------
    if (state.roundBannerTimer > 0.0f) {
        std::string bannerMsg;
        Color bannerCol = NEON_CYAN;

        if (state.bannerCode == 0 && state.lastRoundWinner < 2) {
            const bool p1Won = (state.lastRoundWinner == 0);
            bannerMsg = p1Won
                ? "PLAYER 1 (CYAN) SCORES!  -  NEXT ROUND"
                : "PLAYER 2 (MAGENTA) SCORES!  -  NEXT ROUND";
            bannerCol = p1Won ? NEON_CYAN : NEON_MAGENTA;
        } else if (state.bannerCode == 1) {
            bannerMsg = "WAVE " + std::to_string(state.currentWave) + "  -  INCOMING HOSTILES!";
            bannerCol = NEON_CYAN;
        } else if (state.bannerCode == 2) {
            bannerMsg = "WARNING: WAVE BOSS APPROACHING!";
            bannerCol = ENEMY_BOSS_GOLD;
        } else if (state.bannerCode == 3) {
            bannerMsg = "WAVE " + std::to_string(state.currentWave) + " CLEARED!";
            bannerCol = ALLY_MINT;
        } else if (state.bannerCode == 4) {
            bannerMsg = "SQUAD WIPED  -  RESTARTING WAVE 1";
            bannerCol = ENEMY_CRIMSON;
        }

        if (!bannerMsg.empty()) {
            const int bFontSize = 28;
            const int bWidth = MeasureText(bannerMsg.c_str(), bFontSize);
            const float boxW = static_cast<float>(bWidth + 48);
            const float boxX = (Constants::ARENA_WIDTH_F - boxW) * 0.5f;
            const float boxY = 104.0f;

            DrawRectangleRounded({ boxX, boxY, boxW, 50.0f }, 0.2f, 8, HUD_BG_DARK);
            DrawRectangleRoundedLinesEx({ boxX, boxY, boxW, 50.0f }, 0.2f, 8, 2.0f, bannerCol);
            DrawText(bannerMsg.c_str(), centerX - bWidth / 2, static_cast<int>(boxY + 11.0f), bFontSize, bannerCol);
        }
    }
}

void Renderer::renderIntro(float elapsedSeconds) {
    const float t = std::clamp(elapsedSeconds, 0.0f, Constants::INTRO_DURATION);

    // Smooth fade in (0.0s -> 1.7s), hold (1.7s -> 3.3s), fade out (3.3s -> 5.0s)
    float alpha = 1.0f;
    if (t < 1.7f) {
        const float u = t / 1.7f;
        alpha = u * u * (3.0f - 2.0f * u);
    } else if (t > 3.3f) {
        const float u = std::clamp((Constants::INTRO_DURATION - t) / 1.7f, 0.0f, 1.0f);
        alpha = u * u * (3.0f - 2.0f * u);
    }

    const unsigned char aByte = static_cast<unsigned char>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    const unsigned char glowByte = static_cast<unsigned char>(std::clamp(alpha, 0.0f, 1.0f) * 140.0f);

    const float screenW = static_cast<float>(GetScreenWidth());
    const float screenH = static_cast<float>(GetScreenHeight());
    const float scale   = std::min(screenW / Constants::ARENA_WIDTH_F, screenH / Constants::ARENA_HEIGHT_F);
    const float offsetX = (screenW - Constants::ARENA_WIDTH_F * scale) * 0.5f;
    const float offsetY = (screenH - Constants::ARENA_HEIGHT_F * scale) * 0.5f;

    BeginDrawing();
    ClearBackground(BLACK);

    Camera2D uiCamera{};
    uiCamera.offset = { offsetX, offsetY };
    uiCamera.target = { 0.0f, 0.0f };
    uiCamera.rotation = 0.0f;
    uiCamera.zoom = scale;
    BeginMode2D(uiCamera);

    const char* studioTitle = "NOVAK AND ERIK PRODUCTIONS";
    const int fontSize = 54;
    const int textW = MeasureText(studioTitle, fontSize);
    const int centerX = Constants::ARENA_WIDTH / 2;
    const int centerY = Constants::ARENA_HEIGHT / 2;

    DrawText(studioTitle, centerX - textW / 2 + 2, centerY - 26 + 2, fontSize, { 0, 200, 255, glowByte });
    DrawText(studioTitle, centerX - textW / 2, centerY - 26, fontSize, { 240, 250, 255, aByte });

    const char* presents = "PRESENTS";
    const int presW = MeasureText(presents, 22);
    DrawText(presents, centerX - presW / 2, centerY + 42, 22, { 255, 60, 200, aByte });

    EndMode2D();
    EndDrawing();
}

void Renderer::renderMenu(const std::string& ipInput, GameMode selectedMode, const std::string& statusMessage) {
    const float elapsed = static_cast<float>(GetTime());
    updateShaderUniforms(elapsed);

    BeginTextureMode(m_sceneTarget);
    ClearBackground(BLACK);

    drawBackgroundShader();
    drawArenaBorder();

    ShipState demoCyan{};
    demoCyan.x = Constants::ARENA_WIDTH_F * 0.22f + std::cos(elapsed * 1.4f) * 35.0f;
    demoCyan.y = Constants::ARENA_HEIGHT_F * 0.52f + std::sin(elapsed * 1.4f) * 35.0f;
    demoCyan.angle = elapsed * 1.4f + 1.5708f;
    demoCyan.isThrusting = 1;
    demoCyan.active = 1;
    drawVectorShip(demoCyan, 0, true);

    ShipState demoMagenta{};
    demoMagenta.x = Constants::ARENA_WIDTH_F * 0.78f + std::cos(-elapsed * 1.4f) * 35.0f;
    demoMagenta.y = Constants::ARENA_HEIGHT_F * 0.52f + std::sin(-elapsed * 1.4f) * 35.0f;
    demoMagenta.angle = -elapsed * 1.4f - 1.5708f;
    demoMagenta.isThrusting = 1;
    demoMagenta.active = 1;
    drawVectorShip(demoMagenta, 1, true);

    EndTextureMode();

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

    const float panelW = 800.0f;
    const float panelH = 610.0f;
    const float panelX = (Constants::ARENA_WIDTH_F - panelW) * 0.5f;
    const float panelY = (Constants::ARENA_HEIGHT_F - panelH) * 0.5f;

    DrawRectangleRounded({ panelX, panelY, panelW, panelH }, 0.06f, 12, { 8, 10, 24, 228 });
    DrawRectangleRoundedLinesEx({ panelX, panelY, panelW, panelH }, 0.06f, 12, 2.5f, NEON_CYAN);

    const int centerX = Constants::ARENA_WIDTH / 2;

    const char* title = "1V1 NEON SPACE ARENA";
    const int titleW = MeasureText(title, 44);
    DrawText(title, centerX - titleW / 2, static_cast<int>(panelY + 26), 44, NEON_CYAN);

    const char* subtitle = "NOVAK AND ERIK PRODUCTIONS  |  PVP DUEL & 2P CO-OP WAVES";
    const int subW = MeasureText(subtitle, 16);
    DrawText(subtitle, centerX - subW / 2, static_cast<int>(panelY + 76), 16, NEON_MAGENTA);

    const float optX = panelX + 55.0f;

    // Game Mode Selector Box
    const bool isCoop = (selectedMode == GameMode::CoopWaves);
    const Rectangle modeBox = { optX, panelY + 112.0f, panelW - 110.0f, 56.0f };
    DrawRectangleRounded(modeBox, 0.18f, 8, { 14, 24, 52, 235 });
    DrawRectangleRoundedLinesEx(modeBox, 0.18f, 8, 2.0f, isCoop ? ALLY_MINT : NEON_CYAN);

    const std::string modeText = isCoop
        ? "HOST MODE:  [ CO-OP WAVES - 2 PLAYERS VS ENEMY WAVES ]"
        : "HOST MODE:  [ 1V1 PVP DUEL - HUMAN VS HUMAN ]";
    DrawText(modeText.c_str(), static_cast<int>(optX + 18.0f), static_cast<int>(panelY + 120), 20, isCoop ? ALLY_MINT : NEON_CYAN);
    DrawText("Press [M] or [UP/DOWN] to switch Game Mode",
             static_cast<int>(optX + 18.0f), static_cast<int>(panelY + 145), 14, LIGHTGRAY);

    // Host & Join Controls
    DrawText("[1]  HOST GAME  (UDP Port 7777 - Launches Selected Mode)",
             static_cast<int>(optX), static_cast<int>(panelY + 190), 23, NEON_CYAN);
    DrawText("[2] or [ENTER]  JOIN FRIEND BY IP  (Auto-syncs Host Mode)",
             static_cast<int>(optX), static_cast<int>(panelY + 230), 23, NEON_MAGENTA);

    // Host IP Input Box
    DrawText("TARGET HOST IP (Type IP, Backspace to edit):",
             static_cast<int>(optX), static_cast<int>(panelY + 276), 17, LIGHTGRAY);

    const Rectangle ipBox = { optX, panelY + 302.0f, panelW - 110.0f, 46.0f };
    DrawRectangleRec(ipBox, { 16, 22, 46, 255 });
    DrawRectangleLinesEx(ipBox, 2.0f, NEON_MAGENTA);

    const bool cursorBlink = (static_cast<int>(elapsed * 2.5f) % 2) == 0;
    const std::string displayIp = ipInput + (cursorBlink ? "_" : "");
    DrawText(displayIp.c_str(),
             static_cast<int>(ipBox.x + 16.0f),
             static_cast<int>(ipBox.y + 11.0f),
             24,
             WHITE);

    // Display & Resolution Selector
    const std::string dispLabel = "DISPLAY:  " + getDisplaySettingsLabel();
    DrawText(dispLabel.c_str(),
             static_cast<int>(optX), static_cast<int>(panelY + 370), 20, NEON_CYAN);
    DrawText("[R] or [LEFT/RIGHT] Change Resolution   |   [F] or [F11] Toggle Fullscreen",
             static_cast<int>(optX), static_cast<int>(panelY + 398), 16, LIGHTGRAY);

    // Controls Reference
    DrawText("CONTROLS:  [W/A/S/D] or [ARROW KEYS] Thrust & Rotate   |   [SPACE] Fire",
             static_cast<int>(optX), static_cast<int>(panelY + 444), 16, { 180, 220, 255, 255 });
    DrawText("CO-OP TIP: Shoot your friend twice to heal +1 HP! Shoot comets 3x to break them.",
             static_cast<int>(optX), static_cast<int>(panelY + 470), 16, ALLY_MINT);

    const std::string statusLine = "STATUS: " + statusMessage;
    DrawText(statusLine.c_str(),
             static_cast<int>(optX), static_cast<int>(panelY + 536), 17, YELLOW);

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

    BeginTextureMode(m_sceneTarget);
    ClearBackground(BLACK);

    drawBackgroundShader();
    drawArenaBorder();
    drawParticles();
    drawComets(state);
    drawPowerUps(state);
    drawEnemies(state);
    drawBullets(state);

    drawVectorShip(state.ships[0], 0, isHost);
    drawVectorShip(state.ships[1], 1, !isHost);

    EndTextureMode();

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

    drawHUD(state, isHost, isConnected, statusMessage, pingMs);

    EndMode2D();
    EndDrawing();
}
