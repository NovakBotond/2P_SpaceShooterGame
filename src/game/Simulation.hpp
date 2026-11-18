#pragma once
// =============================================================================
// OWNER: Developer B (Networking & Gameplay Logic)
// FILE:  src/game/Simulation.hpp
// DESC:  Server-authoritative 2D arena physics simulation supporting both
//        1v1 PvP Duel and 2-Player Co-op Wave Defense:
//        - Ship rotation, thrust, overheat energy bar, & blue bubble shield
//        - Screen-edge clamping & velocity bouncing
//        - Projectiles (standard, shotgun, bouncy 5s, 45-deg radial burst)
//        - Destructible 3-HP comets & 13 power-ups
//        - Co-op waves with edge-spawning enemies, 5th-wave speed scaling,
//          2nd-wave HP scaling, end-of-wave bosses, ally conversion, & co-op heal
// =============================================================================

#include "shared/Constants.hpp"
#include "shared/Protocol.hpp"
#include <random>

class Simulation {
public:
    Simulation();

    // Reset entire match for the requested GameMode (PvP or CoopWaves)
    void resetMatch(GameMode mode = GameMode::PvP);

    // Reset PvP round (preserves scores, restores 3 HP, resets entities)
    void resetRound();

    // Co-op helper: activates/deactivates Player 2 based on connection status
    void setCoopPlayer2Present(bool present);

    // Advance authoritative simulation by dt seconds
    void update(float dt, const InputPacket& hostInput, const InputPacket& clientInput);

    // Deterministic ship physics step shared between Host simulation and Client prediction
    static void stepShipPhysics(ShipState& ship, const InputPacket& input, float dt);

    // Lightweight visual extrapolation for remote entities between network snapshots
    static void extrapolateVisuals(GameState& state, float dt, bool extrapolateHostShip, bool extrapolateBullets);

    // Compute shortest signed angular difference in [-PI, PI]
    static float shortestAngleDiff(float from, float to);

    // Smoothly interpolate between two GameState snapshots for sub-tick rendering
    static GameState interpolateState(const GameState& prev, const GameState& curr, float alpha);

    // Retrieve current authoritative state snapshot for rendering & network broadcast
    const GameState& getState() const { return m_state; }

private:
    float randomRange(float minVal, float maxVal);
    void startWave(uint16_t waveNumber);
    void updateShip(int playerId, const InputPacket& input, float dt);
    void spawnBullet(int playerId, const ShipState& ship, float angleOffset, bool bouncy, bool ignoreShipVel = false);
    void spawnEnemyBullet(const EnemyState& enemy, float aimAngle, uint8_t ownerId);
    void updateBullets(float dt);
    void spawnComet();
    void updateComets(float dt);
    void spawnPowerUp();
    void updatePowerUps(float dt);
    void spawnEnemy(bool asBoss);
    void updateEnemies(float dt);
    void defeatEnemySlot(int enemyIdx, int killerPlayerId);
    bool applyDamageToShip(int targetId, float knockVx, float knockVy);
    void handlePlayerDefeat(int defeatedPlayerId);
    void resolveCollisions();

    GameState    m_state{};
    float        m_fireCooldowns[Constants::MAX_PLAYERS]{ 0.0f, 0.0f };
    float        m_contactCooldowns[Constants::MAX_PLAYERS]{ 0.0f, 0.0f };
    float        m_ramCooldowns[Constants::MAX_PLAYERS][Constants::MAX_ENEMIES]{};
    float        m_enemyFireCooldowns[Constants::MAX_ENEMIES]{};
    float        m_cometSpawnTimer   = 2.5f;
    float        m_powerUpSpawnTimer = 3.0f;

    bool         m_coopP2Present           = false;
    int          m_waveNormalToSpawn       = 0;
    int          m_bossTriggerRemaining    = 2;
    bool         m_bossSpawnedThisWave     = false;
    bool         m_waveClearPending        = false;
    float        m_waveClearTimer          = 0.0f;
    float        m_enemySpawnTimer         = 1.0f;
    std::mt19937 m_rng;
};
