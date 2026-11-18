#pragma once
// =============================================================================
// OWNER: Developer B (Networking & Gameplay Logic)
// FILE:  src/game/Simulation.hpp
// DESC:  Server-authoritative 2D arena physics simulation:
//        - Ship rotation & forward/reverse thrust
//        - Screen-edge clamping & velocity bouncing
//        - Projectile spawning & movement
//        - Circle-based collision detection (Bullet-vs-Ship & Ship-vs-Ship)
//        - HP tracking (3 HP per ship) and round score/reset logic
//        - Shared deterministic ship-step helper for client-side prediction
// =============================================================================

#include "shared/Constants.hpp"
#include "shared/Protocol.hpp"
#include <random>

class Simulation {
public:
    Simulation();

    // Reset entire match (scores = 0, positions & HP reset)
    void resetMatch();

    // Reset round (preserves scores, restores 3 HP, resets ship positions & bullets)
    void resetRound();

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
    void updateShip(int playerId, const InputPacket& input, float dt);
    void spawnBullet(int playerId, const ShipState& ship);
    void updateBullets(float dt);
    void spawnComet();
    void updateComets(float dt);
    void resolveCollisions();

    GameState    m_state{};
    float        m_fireCooldowns[Constants::MAX_PLAYERS]{ 0.0f, 0.0f };
    float        m_cometSpawnTimer = 2.5f;
    std::mt19937 m_rng;
};
