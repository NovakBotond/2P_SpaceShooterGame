#pragma once
// =============================================================================
// OWNER: Developer A (Graphics & Shaders)
// FILE:  src/graphics/Renderer.hpp
// DESC:  Raylib OpenGL 3.3 renderer managing window lifecycle, GLSL 330 shaders
//        (background.fs & postprocess.fs), RenderTexture2D post-processing,
//        5-second studio intro screen, resolution/fullscreen customization,
//        vector ships, blue bubble shield, comets, co-op enemies & bosses,
//        13 power-ups, active above-ship buff badges, and HUD bars.
// =============================================================================

#include "shared/Constants.hpp"
#include "shared/Protocol.hpp"
#include <raylib.h>
#include <string>
#include <vector>

struct ThrustParticle {
    float   x       = 0.0f;
    float   y       = 0.0f;
    float   vx      = 0.0f;
    float   vy      = 0.0f;
    float   life    = 0.0f;
    float   maxLife = 0.35f;
    float   size    = 3.0f;
    uint8_t ownerId = 0; // 0 = Cyan, 1 = Magenta, 2 = Comet, 3 = Overheat Smoke
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Initialize Raylib window, RenderTexture2D, and compile GLSL 330 shaders
    bool initialize();

    // Unload shaders, RenderTexture2D, and close Raylib window
    void shutdown();

    // Cycle through available windowed resolutions (direction: +1 or -1)
    void cycleResolution(int direction = 1);

    // Toggle between Windowed and Fullscreen display modes
    void toggleFullscreenMode();

    // Human-readable label of current display mode & resolution
    std::string getDisplaySettingsLabel() const;

    // Render 5-second black intro screen with fade-in/fade-out studio title
    void renderIntro(float elapsedSeconds);

    // Render the interactive start menu (Mode, Host / Join by IP, Display)
    void renderMenu(const std::string& ipInput, GameMode selectedMode, const std::string& statusMessage);

    // Render active match state (PvP Duel or Co-op Waves)
    void renderGame(const GameState& state,
                    bool isHost,
                    bool isConnected,
                    const std::string& statusMessage,
                    uint32_t pingMs,
                    float dt);

private:
    std::string resolveAssetPath(const std::string& relativePath) const;
    void updateShaderUniforms(float elapsedTime);
    void updateParticles(const GameState& state, float dt);

    void drawBackgroundShader();
    void drawArenaBorder();
    void drawParticles();
    void drawComets(const GameState& state);
    void drawPowerUps(const GameState& state);
    void drawEnemies(const GameState& state);
    void drawVectorShip(const ShipState& ship, int playerId, bool isLocalViewer = true);
    void drawShipPowerUpBadges(const ShipState& ship, int playerId, bool isLocalViewer);
    void drawBullets(const GameState& state);
    void drawHUD(const GameState& state, bool isHost, bool isConnected, const std::string& statusMessage, uint32_t pingMs);

    bool            m_initialized       = false;
    int             m_resPresetIndex    = 0;
    bool            m_isFullscreen      = false;
    RenderTexture2D m_sceneTarget{};

    Shader          m_bgShader{};
    int             m_bgTimeLoc        = -1;
    int             m_bgResolutionLoc  = -1;

    Shader          m_postShader{};
    int             m_postTimeLoc      = -1;
    int             m_postResolutionLoc = -1;

    std::vector<ThrustParticle> m_particles;
};
