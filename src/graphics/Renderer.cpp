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
    // Spawn thrust particles behind active thrusting ships
    for (int i = 0; i < Constants::MAX_PLAYERS; ++i) {
        const ShipState& ship = state.ships[i];
        if (!ship.active || !ship.isThrusting) {
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

    // Integrate and prune expired particles
    for (auto& part : m_particles) {
        part.x += part.vx * dt;
        part.y += part.vy * dt;
        part.life -= dt;
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

        Color primary = (part.ownerId == 0) ? NEON_CYAN : NEON_MAGENTA;
        primary.a = alpha;

        Color core = WHITE;
        core.a = static_cast<unsigned char>(ratio * 255.0f);

        DrawCircleV({ part.x, part.y }, part.size * ratio * 1.6f, primary);
        DrawCircleV({ part.x, part.y }, part.size * ratio * 0.7f, core);
    }
}

void Renderer::drawVectorShip(const ShipState& ship, int playerId) {
    if (!ship.active) {
        return;
    }

    const Color mainColor = (playerId == 0) ? NEON_CYAN      : NEON_MAGENTA;
    const Color glowColor = (playerId == 0) ? NEON_CYAN_GLOW : NEON_MAG_GLOW;
    const Color coreColor = (playerId == 0) ? NEON_CYAN_CORE : NEON_MAG_CORE;

    const float c = std::cos(ship.angle);
    const float s = std::sin(ship.angle);

    // Helper lambda to transform ship-local coordinates to world space
    auto toWorld = [&](float lx, float ly) -> Vector2 {
        return {
            ship.x + (lx * c - ly * s),
            ship.y + (lx * s + ly * c)
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
    }
}

void Renderer::drawHUD(const GameState& state,
                       bool isHost,
                       bool isConnected,
                       const std::string& statusMessage,
                       uint32_t pingMs) {
    const ShipState& p1 = state.ships[0];
    const ShipState& p2 = state.ships[1];

    // -------------------------------------------------------------------------
    // Player 1 (Left - Cyan) Health Bar & Label
    // -------------------------------------------------------------------------
    const int hudY = 28;
    const int p1X  = 36;
    DrawText("P1 [HOST - CYAN]", p1X, hudY, 18, NEON_CYAN);

    for (int hp = 0; hp < Constants::SHIP_MAX_HP; ++hp) {
        const Rectangle seg = {
            static_cast<float>(p1X + hp * 52),
            static_cast<float>(hudY + 26),
            44.0f,
            14.0f
        };
        if (hp < p1.hp) {
            DrawRectangleRec(seg, NEON_CYAN);
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
    DrawText(p2Label, p2RightEdge - p2LabelWidth, hudY, 18, NEON_MAGENTA);

    for (int hp = 0; hp < Constants::SHIP_MAX_HP; ++hp) {
        const Rectangle seg = {
            static_cast<float>(p2RightEdge - (Constants::SHIP_MAX_HP - hp) * 52 + 8),
            static_cast<float>(hudY + 26),
            44.0f,
            14.0f
        };
        if (hp < p2.hp) {
            DrawRectangleRec(seg, NEON_MAGENTA);
            DrawRectangleLinesEx(seg, 1.5f, WHITE);
        } else {
            DrawRectangleRec(seg, { 55, 20, 45, 140 });
            DrawRectangleLinesEx(seg, 1.5f, { 180, 0, 130, 120 });
        }
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

    // 1c. Engine thrust particles
    drawParticles();

    // 1d. Projectiles
    drawBullets(state);

    // 1e. Vector-style neon ships (Player 1 Cyan, Player 2 Magenta)
    drawVectorShip(state.ships[0], 0);
    drawVectorShip(state.ships[1], 1);

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
