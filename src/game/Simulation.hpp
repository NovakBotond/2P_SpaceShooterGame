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
// =============================================================================

#include "shared/Constants.hpp"
#include "shared/Protocol.hpp"

class Simulation {
public:
    Simulation();

    // Reset entire match (scores = 0, positions & HP reset)
    void resetMatch();

    // Reset round (preserves scores, restores 3 HP, resets ship positions & bullets)
    void resetRound();

    // Advance authoritative simulation by dt seconds
    void update(float dt, const InputPacket& hostInput, const InputPacket& clientInput);

    // Retrieve current authoritative state snapshot for rendering & network broadcast
    const GameState& getState() const { return m_state; }

private:
    void updateShip(int playerId, const InputPacket& input, float dt);
    void spawnBullet(int playerId, const ShipState& ship);
    void updateBullets(float dt);
    void resolveCollisions();

    GameState m_state{};
    float     m_fireCooldowns[Constants::MAX_PLAYERS]{ 0.0f, 0.0f };
};
