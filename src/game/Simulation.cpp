// =============================================================================
// OWNER: Developer B (Networking & Gameplay Logic)
// FILE:  src/game/Simulation.cpp
// DESC:  Implementation of server-authoritative 2D physics, projectiles,
//        circle-based collisions, HP tracking, and round reset logic.
// =============================================================================

#include "game/Simulation.hpp"
#include <algorithm>
#include <cmath>

namespace {
    constexpr float PI_VAL = 3.14159265358979323846f;
    constexpr float TWO_PI = 6.28318530717958647692f;

    bool checkCircleOverlap(float x1, float y1, float r1,
                            float x2, float y2, float r2) {
        const float dx = x2 - x1;
        const float dy = y2 - y1;
        const float radiusSum = r1 + r2;
        return (dx * dx + dy * dy) <= (radiusSum * radiusSum);
    }
}

Simulation::Simulation() {
    resetMatch();
}

void Simulation::resetMatch() {
    m_state = GameState{};
    m_state.packetType = static_cast<uint8_t>(PacketType::GAME_STATE);
    m_state.tick = 0;
    m_state.lastRoundWinner = 255;
    m_state.roundBannerTimer = 0.0f;

    for (int i = 0; i < Constants::MAX_PLAYERS; ++i) {
        m_state.ships[i].score = 0;
    }

    resetRound();
}

void Simulation::resetRound() {
    // Player 0 (Host - Electric Cyan): Spawn on left side facing right
    ShipState& p0 = m_state.ships[0];
    p0.x           = Constants::ARENA_WIDTH_F * 0.20f;
    p0.y           = Constants::ARENA_HEIGHT_F * 0.50f;
    p0.vx          = 0.0f;
    p0.vy          = 0.0f;
    p0.angle       = 0.0f;
    p0.hp          = Constants::SHIP_MAX_HP;
    p0.isThrusting = 0;
    p0.active      = 1;

    // Player 1 (Client - Neon Magenta): Spawn on right side facing left
    ShipState& p1 = m_state.ships[1];
    p1.x           = Constants::ARENA_WIDTH_F * 0.80f;
    p1.y           = Constants::ARENA_HEIGHT_F * 0.50f;
    p1.vx          = 0.0f;
    p1.vy          = 0.0f;
    p1.angle       = PI_VAL;
    p1.hp          = Constants::SHIP_MAX_HP;
    p1.isThrusting = 0;
    p1.active      = 1;

    // Clear all active projectiles
    for (int i = 0; i < Constants::MAX_BULLETS; ++i) {
        m_state.bullets[i] = BulletState{};
        m_state.bullets[i].active = 0;
    }

    m_fireCooldowns[0] = 0.0f;
    m_fireCooldowns[1] = 0.0f;
}

void Simulation::update(float dt, const InputPacket& hostInput, const InputPacket& clientInput) {
    m_state.tick++;

    if (m_state.roundBannerTimer > 0.0f) {
        m_state.roundBannerTimer = std::max(0.0f, m_state.roundBannerTimer - dt);
    }

    // 1. Update both players' ships
    updateShip(0, hostInput, dt);
    updateShip(1, clientInput, dt);

    // 2. Advance active projectiles
    updateBullets(dt);

    // 3. Perform circle-based collision detection & round-end evaluation
    resolveCollisions();
}

void Simulation::updateShip(int playerId, const InputPacket& input, float dt) {
    if (playerId < 0 || playerId >= Constants::MAX_PLAYERS) {
        return;
    }

    ShipState& ship = m_state.ships[playerId];
    if (!ship.active) {
        return;
    }

    if (m_fireCooldowns[playerId] > 0.0f) {
        m_fireCooldowns[playerId] = std::max(0.0f, m_fireCooldowns[playerId] - dt);
    }

    // Rotation (A/D or Left/Right Arrow)
    if (input.rotateLeft) {
        ship.angle -= Constants::SHIP_ROTATION_SPEED * dt;
    }
    if (input.rotateRight) {
        ship.angle += Constants::SHIP_ROTATION_SPEED * dt;
    }

    // Wrap angle to [0, 2*PI)
    ship.angle = std::fmod(ship.angle, TWO_PI);
    if (ship.angle < 0.0f) {
        ship.angle += TWO_PI;
    }

    const float dirX = std::cos(ship.angle);
    const float dirY = std::sin(ship.angle);

    // Thrust (W/S or Up/Down Arrow)
    ship.isThrusting = 0;
    if (input.thrustForward) {
        ship.vx += dirX * Constants::SHIP_THRUST_ACCEL * dt;
        ship.vy += dirY * Constants::SHIP_THRUST_ACCEL * dt;
        ship.isThrusting = 1;
    }
    if (input.thrustBackward) {
        ship.vx -= dirX * Constants::SHIP_REVERSE_ACCEL * dt;
        ship.vy -= dirY * Constants::SHIP_REVERSE_ACCEL * dt;
    }

    // Apply space drag damping
    const float dragFactor = std::pow(Constants::SHIP_DRAG, dt * Constants::TICK_RATE);
    ship.vx *= dragFactor;
    ship.vy *= dragFactor;

    // Clamp maximum speed
    const float speedSq = ship.vx * ship.vx + ship.vy * ship.vy;
    const float maxSpeedSq = Constants::SHIP_SPEED * Constants::SHIP_SPEED;
    if (speedSq > maxSpeedSq) {
        const float invSpeed = Constants::SHIP_SPEED / std::sqrt(speedSq);
        ship.vx *= invSpeed;
        ship.vy *= invSpeed;
    }

    // Integrate position
    ship.x += ship.vx * dt;
    ship.y += ship.vy * dt;

    // Screen-edge clamping & velocity bouncing inside the arena perimeter
    const float minX = Constants::ARENA_BORDER_PADDING + Constants::SHIP_RADIUS;
    const float maxX = Constants::ARENA_WIDTH_F - Constants::ARENA_BORDER_PADDING - Constants::SHIP_RADIUS;
    const float minY = Constants::ARENA_BORDER_PADDING + Constants::SHIP_RADIUS;
    const float maxY = Constants::ARENA_HEIGHT_F - Constants::ARENA_BORDER_PADDING - Constants::SHIP_RADIUS;

    if (ship.x < minX) {
        ship.x = minX;
        ship.vx = std::abs(ship.vx) * Constants::SHIP_BOUNCE_DAMPING;
    } else if (ship.x > maxX) {
        ship.x = maxX;
        ship.vx = -std::abs(ship.vx) * Constants::SHIP_BOUNCE_DAMPING;
    }

    if (ship.y < minY) {
        ship.y = minY;
        ship.vy = std::abs(ship.vy) * Constants::SHIP_BOUNCE_DAMPING;
    } else if (ship.y > maxY) {
        ship.y = maxY;
        ship.vy = -std::abs(ship.vy) * Constants::SHIP_BOUNCE_DAMPING;
    }

    // Weapon firing (Spacebar)
    if (input.shoot && m_fireCooldowns[playerId] <= 0.0f) {
        spawnBullet(playerId, ship);
        m_fireCooldowns[playerId] = Constants::SHIP_FIRE_COOLDOWN;
    }
}

void Simulation::spawnBullet(int playerId, const ShipState& ship) {
    for (int i = 0; i < Constants::MAX_BULLETS; ++i) {
        BulletState& bullet = m_state.bullets[i];
        if (!bullet.active) {
            const float dirX = std::cos(ship.angle);
            const float dirY = std::sin(ship.angle);
            const float spawnDist = Constants::SHIP_RADIUS + Constants::BULLET_RADIUS + 4.0f;

            bullet.x        = ship.x + dirX * spawnDist;
            bullet.y        = ship.y + dirY * spawnDist;
            bullet.vx       = dirX * Constants::BULLET_SPEED + ship.vx * 0.25f;
            bullet.vy       = dirY * Constants::BULLET_SPEED + ship.vy * 0.25f;
            bullet.lifetime = Constants::BULLET_LIFETIME;
            bullet.ownerId  = static_cast<uint8_t>(playerId);
            bullet.active   = 1;
            return;
        }
    }
}

void Simulation::updateBullets(float dt) {
    const float minX = Constants::ARENA_BORDER_PADDING;
    const float maxX = Constants::ARENA_WIDTH_F - Constants::ARENA_BORDER_PADDING;
    const float minY = Constants::ARENA_BORDER_PADDING;
    const float maxY = Constants::ARENA_HEIGHT_F - Constants::ARENA_BORDER_PADDING;

    for (int i = 0; i < Constants::MAX_BULLETS; ++i) {
        BulletState& bullet = m_state.bullets[i];
        if (!bullet.active) {
            continue;
        }

        bullet.x += bullet.vx * dt;
        bullet.y += bullet.vy * dt;
        bullet.lifetime -= dt;

        // Deactivate expired or out-of-bounds projectiles
        if (bullet.lifetime <= 0.0f ||
            bullet.x < minX || bullet.x > maxX ||
            bullet.y < minY || bullet.y > maxY) {
            bullet.active = 0;
        }
    }
}

void Simulation::resolveCollisions() {
    ShipState& s0 = m_state.ships[0];
    ShipState& s1 = m_state.ships[1];

    // 1. Ship-vs-Ship elastic circle collision
    if (s0.active && s1.active &&
        checkCircleOverlap(s0.x, s0.y, Constants::SHIP_RADIUS,
                           s1.x, s1.y, Constants::SHIP_RADIUS)) {
        float dx = s1.x - s0.x;
        float dy = s1.y - s0.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 0.001f) {
            dx = 1.0f;
            dy = 0.0f;
            dist = 1.0f;
        }
        const float nx = dx / dist;
        const float ny = dy / dist;
        const float overlap = (Constants::SHIP_RADIUS * 2.0f) - dist;

        // Separate overlapping ships equally
        s0.x -= nx * (overlap * 0.5f);
        s0.y -= ny * (overlap * 0.5f);
        s1.x += nx * (overlap * 0.5f);
        s1.y += ny * (overlap * 0.5f);

        // Apply elastic separation impulse along collision normal
        const float rvx = s1.vx - s0.vx;
        const float rvy = s1.vy - s0.vy;
        const float velAlongNormal = rvx * nx + rvy * ny;
        if (velAlongNormal < 0.0f) {
            const float impulse = -0.85f * velAlongNormal;
            s0.vx -= impulse * nx;
            s0.vy -= impulse * ny;
            s1.vx += impulse * nx;
            s1.vy += impulse * ny;
        }
    }

    // 2. Bullet-vs-Ship circle collision & HP tracking
    for (int i = 0; i < Constants::MAX_BULLETS; ++i) {
        BulletState& bullet = m_state.bullets[i];
        if (!bullet.active) {
            continue;
        }

        for (int targetId = 0; targetId < Constants::MAX_PLAYERS; ++targetId) {
            // Friendly fire prevention: bullets only hit the opponent
            if (bullet.ownerId == static_cast<uint8_t>(targetId)) {
                continue;
            }

            ShipState& targetShip = m_state.ships[targetId];
            if (!targetShip.active) {
                continue;
            }

            if (checkCircleOverlap(bullet.x, bullet.y, Constants::BULLET_RADIUS,
                                   targetShip.x, targetShip.y, Constants::SHIP_RADIUS)) {
                // Deactivate bullet on impact
                bullet.active = 0;

                // Apply knockback in direction of bullet travel
                const float bSpeed = std::sqrt(bullet.vx * bullet.vx + bullet.vy * bullet.vy);
                if (bSpeed > 0.001f) {
                    targetShip.vx += (bullet.vx / bSpeed) * Constants::BULLET_HIT_KNOCKBACK;
                    targetShip.vy += (bullet.vy / bSpeed) * Constants::BULLET_HIT_KNOCKBACK;
                }

                // Decrement HP
                targetShip.hp -= 1;

                // Round over check: when a ship hits 0 HP, increment winner's score and reset
                if (targetShip.hp <= 0) {
                    const int winnerId = (targetId == 0) ? 1 : 0;
                    m_state.ships[winnerId].score += 1;
                    m_state.lastRoundWinner = static_cast<uint8_t>(winnerId);
                    m_state.roundBannerTimer = Constants::ROUND_BANNER_DURATION;
                    resetRound();
                    return;
                }
                break;
            }
        }
    }
}
