// =============================================================================
// OWNER: Developer B (Networking & Gameplay Logic)
// FILE:  src/game/Simulation.cpp
// DESC:  Implementation of server-authoritative 2D physics, projectiles,
//        circle-based collisions, HP tracking, round reset logic, and shared
//        client-side prediction helpers.
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

    void clampAndBounceShip(ShipState& ship) {
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
    }
}

Simulation::Simulation()
    : m_rng(std::random_device{}()) {
    resetMatch();
}

float Simulation::randomRange(float minVal, float maxVal) {
    std::uniform_real_distribution<float> dist(minVal, maxVal);
    return dist(m_rng);
}

void Simulation::resetMatch() {
    m_state = GameState{};
    m_state.packetType = static_cast<uint8_t>(PacketType::GAME_STATE);
    m_state.tick = 0;
    m_state.lastClientTick = 0;
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
    p0.x                  = Constants::ARENA_WIDTH_F * 0.20f;
    p0.y                  = Constants::ARENA_HEIGHT_F * 0.50f;
    p0.vx                 = 0.0f;
    p0.vy                 = 0.0f;
    p0.angle              = 0.0f;
    p0.hp                 = Constants::SHIP_MAX_HP;
    p0.ammo               = 1.0f;
    p0.overheatSmokeTimer = 0.0f;
    p0.fireRateTimer      = 0.0f;
    p0.speedBoostTimer    = 0.0f;
    p0.stealthTimer       = 0.0f;
    p0.shotgunShotsLeft   = 0;
    p0.bouncyShotsLeft    = 0;
    p0.ammoOverheated     = 0;
    p0.isThrusting        = 0;
    p0.active             = 1;

    // Player 1 (Client - Neon Magenta): Spawn on right side facing left
    ShipState& p1 = m_state.ships[1];
    p1.x                  = Constants::ARENA_WIDTH_F * 0.80f;
    p1.y                  = Constants::ARENA_HEIGHT_F * 0.50f;
    p1.vx                 = 0.0f;
    p1.vy                 = 0.0f;
    p1.angle              = PI_VAL;
    p1.hp                 = Constants::SHIP_MAX_HP;
    p1.ammo               = 1.0f;
    p1.overheatSmokeTimer = 0.0f;
    p1.fireRateTimer      = 0.0f;
    p1.speedBoostTimer    = 0.0f;
    p1.stealthTimer       = 0.0f;
    p1.shotgunShotsLeft   = 0;
    p1.bouncyShotsLeft    = 0;
    p1.ammoOverheated     = 0;
    p1.isThrusting        = 0;
    p1.active             = 1;

    // Clear all active projectiles
    for (int i = 0; i < Constants::MAX_BULLETS; ++i) {
        m_state.bullets[i] = BulletState{};
        m_state.bullets[i].active = 0;
    }

    // Clear all active comets on round reset
    for (int i = 0; i < Constants::MAX_COMETS; ++i) {
        m_state.comets[i] = CometState{};
        m_state.comets[i].active = 0;
    }

    // Clear all map power-ups on round reset
    for (int i = 0; i < Constants::MAX_MAP_POWERUPS; ++i) {
        m_state.powerUps[i] = PowerUpState{};
        m_state.powerUps[i].active = 0;
    }

    m_fireCooldowns[0]  = 0.0f;
    m_fireCooldowns[1]  = 0.0f;
    m_cometSpawnTimer   = randomRange(Constants::COMET_MIN_SPAWN_INTERVAL, Constants::COMET_MAX_SPAWN_INTERVAL);
    m_powerUpSpawnTimer = randomRange(Constants::POWERUP_MIN_SPAWN_INTERVAL, Constants::POWERUP_MAX_SPAWN_INTERVAL);
}

float Simulation::shortestAngleDiff(float from, float to) {
    float diff = std::fmod(to - from + PI_VAL, TWO_PI);
    if (diff < 0.0f) {
        diff += TWO_PI;
    }
    return diff - PI_VAL;
}

GameState Simulation::interpolateState(const GameState& prev, const GameState& curr, float alpha) {
    const float t = std::clamp(alpha, 0.0f, 1.0f);
    GameState out = curr;

    for (int i = 0; i < Constants::MAX_PLAYERS; ++i) {
        const ShipState& p = prev.ships[i];
        const ShipState& c = curr.ships[i];
        if (!p.active || !c.active) {
            continue;
        }
        const float dx = c.x - p.x;
        const float dy = c.y - p.y;
        if (dx * dx + dy * dy < 70.0f * 70.0f) {
            out.ships[i].x     = p.x + dx * t;
            out.ships[i].y     = p.y + dy * t;
            out.ships[i].angle = p.angle + shortestAngleDiff(p.angle, c.angle) * t;
        }
    }

    for (int i = 0; i < Constants::MAX_BULLETS; ++i) {
        const BulletState& p = prev.bullets[i];
        const BulletState& c = curr.bullets[i];
        if (!p.active || !c.active || p.ownerId != c.ownerId) {
            continue;
        }
        const float dx = c.x - p.x;
        const float dy = c.y - p.y;
        if (dx * dx + dy * dy < 60.0f * 60.0f) {
            out.bullets[i].x = p.x + dx * t;
            out.bullets[i].y = p.y + dy * t;
        }
    }

    for (int i = 0; i < Constants::MAX_COMETS; ++i) {
        const CometState& p = prev.comets[i];
        const CometState& c = curr.comets[i];
        if (!p.active || !c.active) {
            continue;
        }
        const float dx = c.x - p.x;
        const float dy = c.y - p.y;
        if (dx * dx + dy * dy < 80.0f * 80.0f) {
            out.comets[i].x = p.x + dx * t;
            out.comets[i].y = p.y + dy * t;
        }
    }

    return out;
}

void Simulation::stepShipPhysics(ShipState& ship, const InputPacket& input, float dt) {
    if (!ship.active) {
        return;
    }

    if (ship.fireRateTimer > 0.0f) {
        ship.fireRateTimer = std::max(0.0f, ship.fireRateTimer - dt);
    }
    if (ship.speedBoostTimer > 0.0f) {
        ship.speedBoostTimer = std::max(0.0f, ship.speedBoostTimer - dt);
    }
    if (ship.stealthTimer > 0.0f) {
        ship.stealthTimer = std::max(0.0f, ship.stealthTimer - dt);
    }

    const float speedMult = (ship.speedBoostTimer > 0.0f) ? Constants::POWERUP_SPEED_MULT : 1.0f;

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
        ship.vx += dirX * (Constants::SHIP_THRUST_ACCEL * speedMult) * dt;
        ship.vy += dirY * (Constants::SHIP_THRUST_ACCEL * speedMult) * dt;
        ship.isThrusting = 1;
    }
    if (input.thrustBackward) {
        ship.vx -= dirX * (Constants::SHIP_REVERSE_ACCEL * speedMult) * dt;
        ship.vy -= dirY * (Constants::SHIP_REVERSE_ACCEL * speedMult) * dt;
    }

    // Apply space drag damping
    const float dragFactor = std::pow(Constants::SHIP_DRAG, dt * Constants::TICK_RATE);
    ship.vx *= dragFactor;
    ship.vy *= dragFactor;

    // Clamp maximum speed (boosted by +30% when SpeedBoost power-up is active)
    const float maxSpeed = Constants::SHIP_SPEED * speedMult;
    const float speedSq = ship.vx * ship.vx + ship.vy * ship.vy;
    const float maxSpeedSq = maxSpeed * maxSpeed;
    if (speedSq > maxSpeedSq) {
        const float invSpeed = maxSpeed / std::sqrt(speedSq);
        ship.vx *= invSpeed;
        ship.vy *= invSpeed;
    }

    // Integrate position and bounce off arena perimeter
    ship.x += ship.vx * dt;
    ship.y += ship.vy * dt;
    clampAndBounceShip(ship);

    // Shooting energy / overheat bar state update
    if (ship.overheatSmokeTimer > 0.0f) {
        ship.overheatSmokeTimer = std::max(0.0f, ship.overheatSmokeTimer - dt);
    } else if (input.shoot && ship.stealthTimer <= 0.0f) {
        ship.ammo -= Constants::AMMO_DRAIN_RATE * dt;
        if (ship.ammo <= 0.0f) {
            ship.ammo = 0.0f;
            ship.overheatSmokeTimer = Constants::AMMO_OVERHEAT_SMOKE_DURATION;
            ship.ammoOverheated = 1;
        }
    } else {
        const float rechargeMult = ship.ammoOverheated
            ? Constants::AMMO_PUNISH_RECHARGE_MULT
            : Constants::AMMO_FAST_RECHARGE_MULT;
        ship.ammo = std::min(1.0f, ship.ammo + Constants::AMMO_BASE_RECHARGE_RATE * rechargeMult * dt);
        if (ship.ammo >= 1.0f) {
            ship.ammoOverheated = 0;
        }
    }
}

void Simulation::extrapolateVisuals(GameState& state,
                                      float dt,
                                      bool extrapolateHostShip,
                                      bool extrapolateBullets) {
    if (state.roundBannerTimer > 0.0f) {
        state.roundBannerTimer = std::max(0.0f, state.roundBannerTimer - dt);
    }

    if (extrapolateHostShip && state.ships[0].active) {
        ShipState& hostShip = state.ships[0];
        const float dragFactor = std::pow(Constants::SHIP_DRAG, dt * Constants::TICK_RATE);
        hostShip.vx *= dragFactor;
        hostShip.vy *= dragFactor;
        hostShip.x += hostShip.vx * dt;
        hostShip.y += hostShip.vy * dt;
        clampAndBounceShip(hostShip);
    }

    if (extrapolateBullets) {
        const float minX = Constants::ARENA_BORDER_PADDING;
        const float maxX = Constants::ARENA_WIDTH_F - Constants::ARENA_BORDER_PADDING;
        const float minY = Constants::ARENA_BORDER_PADDING;
        const float maxY = Constants::ARENA_HEIGHT_F - Constants::ARENA_BORDER_PADDING;

        for (int i = 0; i < Constants::MAX_BULLETS; ++i) {
            BulletState& b = state.bullets[i];
            if (!b.active) {
                continue;
            }
            b.x += b.vx * dt;
            b.y += b.vy * dt;
            b.lifetime -= dt;

            if (b.lifetime <= 0.0f) {
                b.active = 0;
                continue;
            }

            if (b.bouncesOnWalls) {
                if (b.x - Constants::BULLET_RADIUS < minX) {
                    b.x = minX + Constants::BULLET_RADIUS;
                    b.vx = std::abs(b.vx);
                } else if (b.x + Constants::BULLET_RADIUS > maxX) {
                    b.x = maxX - Constants::BULLET_RADIUS;
                    b.vx = -std::abs(b.vx);
                }
                if (b.y - Constants::BULLET_RADIUS < minY) {
                    b.y = minY + Constants::BULLET_RADIUS;
                    b.vy = std::abs(b.vy);
                } else if (b.y + Constants::BULLET_RADIUS > maxY) {
                    b.y = maxY - Constants::BULLET_RADIUS;
                    b.vy = -std::abs(b.vy);
                }
            } else if (b.x < minX || b.x > maxX || b.y < minY || b.y > maxY) {
                b.active = 0;
            }
        }

        for (int i = 0; i < Constants::MAX_MAP_POWERUPS; ++i) {
            PowerUpState& pu = state.powerUps[i];
            if (!pu.active) {
                continue;
            }
            pu.lifetime -= dt;
            if (pu.lifetime <= 0.0f) {
                pu.active = 0;
            }
        }

        for (int i = 0; i < Constants::MAX_COMETS; ++i) {
            CometState& c = state.comets[i];
            if (!c.active) {
                continue;
            }
            c.x += c.vx * dt;
            c.y += c.vy * dt;
            c.lifetime -= dt;
            if (c.lifetime <= 0.0f || c.x < minX || c.x > maxX || c.y < minY || c.y > maxY) {
                c.active = 0;
            }
        }
    }
}

void Simulation::update(float dt, const InputPacket& hostInput, const InputPacket& clientInput) {
    m_state.tick++;
    if (clientInput.tick > 0) {
        m_state.lastClientTick = clientInput.tick;
    }

    if (m_state.roundBannerTimer > 0.0f) {
        m_state.roundBannerTimer = std::max(0.0f, m_state.roundBannerTimer - dt);
    }

    // 1. Update both players' ships
    updateShip(0, hostInput, dt);
    updateShip(1, clientInput, dt);

    // 2. Advance active projectiles
    updateBullets(dt);

    // 3. Spawn & advance random flying comets
    updateComets(dt);

    // 4. Spawn & advance random map power-ups (max 2, 6s lifetime)
    updatePowerUps(dt);

    // 5. Perform circle-based collision detection & round-end evaluation
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

    const bool canFire = (ship.stealthTimer <= 0.0f &&
                          ship.overheatSmokeTimer <= 0.0f &&
                          ship.ammo > 0.0f);

    stepShipPhysics(ship, input, dt);

    // Weapon firing (Spacebar) — blocked while Stealth/Immortal or Overheated
    if (input.shoot && m_fireCooldowns[playerId] <= 0.0f && canFire) {
        const bool isBouncy = (ship.bouncyShotsLeft > 0);
        if (isBouncy) {
            ship.bouncyShotsLeft -= 1;
        }

        if (ship.shotgunShotsLeft > 0) {
            ship.shotgunShotsLeft -= 1;
            spawnBullet(playerId, ship, -Constants::SHOTGUN_SPREAD_ANGLE, isBouncy);
            spawnBullet(playerId, ship,  0.0f,                            isBouncy);
            spawnBullet(playerId, ship,  Constants::SHOTGUN_SPREAD_ANGLE, isBouncy);
        } else {
            spawnBullet(playerId, ship, 0.0f, isBouncy);
        }

        const float baseCooldown = Constants::SHIP_FIRE_COOLDOWN;
        m_fireCooldowns[playerId] = (ship.fireRateTimer > 0.0f)
            ? (baseCooldown / Constants::POWERUP_FIRERATE_MULT)
            : baseCooldown;
    }
}

void Simulation::spawnBullet(int playerId, const ShipState& ship, float angleOffset, bool bouncy) {
    for (int i = 0; i < Constants::MAX_BULLETS; ++i) {
        BulletState& bullet = m_state.bullets[i];
        if (!bullet.active) {
            const float fireAngle = ship.angle + angleOffset;
            const float dirX = std::cos(fireAngle);
            const float dirY = std::sin(fireAngle);
            const float spawnDist = Constants::SHIP_RADIUS + Constants::BULLET_RADIUS + 4.0f;

            bullet.x              = ship.x + dirX * spawnDist;
            bullet.y              = ship.y + dirY * spawnDist;
            bullet.vx             = dirX * Constants::BULLET_SPEED + ship.vx * 0.25f;
            bullet.vy             = dirY * Constants::BULLET_SPEED + ship.vy * 0.25f;
            bullet.lifetime       = bouncy ? Constants::BOUNCY_BULLET_LIFETIME : Constants::BULLET_LIFETIME;
            bullet.ownerId        = static_cast<uint8_t>(playerId);
            bullet.bouncesOnWalls = bouncy ? 1 : 0;
            bullet.active         = 1;
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

        if (bullet.lifetime <= 0.0f) {
            bullet.active = 0;
            continue;
        }

        if (bullet.bouncesOnWalls) {
            // Power-up #5: Bounce off arena perimeter walls until 5s decay
            if (bullet.x - Constants::BULLET_RADIUS < minX) {
                bullet.x = minX + Constants::BULLET_RADIUS;
                bullet.vx = std::abs(bullet.vx);
            } else if (bullet.x + Constants::BULLET_RADIUS > maxX) {
                bullet.x = maxX - Constants::BULLET_RADIUS;
                bullet.vx = -std::abs(bullet.vx);
            }

            if (bullet.y - Constants::BULLET_RADIUS < minY) {
                bullet.y = minY + Constants::BULLET_RADIUS;
                bullet.vy = std::abs(bullet.vy);
            } else if (bullet.y + Constants::BULLET_RADIUS > maxY) {
                bullet.y = maxY - Constants::BULLET_RADIUS;
                bullet.vy = -std::abs(bullet.vy);
            }
        } else if (bullet.x < minX || bullet.x > maxX ||
                   bullet.y < minY || bullet.y > maxY) {
            bullet.active = 0;
        }
    }
}

void Simulation::spawnComet() {
    int slot = -1;
    for (int i = 0; i < Constants::MAX_COMETS; ++i) {
        if (!m_state.comets[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return;
    }

    const float pad = Constants::ARENA_BORDER_PADDING + Constants::COMET_RADIUS + 10.0f;
    const float minX = pad;
    const float maxX = Constants::ARENA_WIDTH_F - pad;
    const float minY = pad;
    const float maxY = Constants::ARENA_HEIGHT_F - pad;

    // Pick a random edge (0=Top, 1=Bottom, 2=Left, 3=Right) to enter from
    std::uniform_int_distribution<int> edgeDist(0, 3);
    const int edge = edgeDist(m_rng);

    float startX = 0.0f;
    float startY = 0.0f;
    switch (edge) {
        case 0: startX = randomRange(minX, maxX); startY = minY; break;
        case 1: startX = randomRange(minX, maxX); startY = maxY; break;
        case 2: startX = minX; startY = randomRange(minY, maxY); break;
        default: startX = maxX; startY = randomRange(minY, maxY); break;
    }

    // Ensure comet doesn't spawn directly on top of a player ship
    for (int p = 0; p < Constants::MAX_PLAYERS; ++p) {
        const ShipState& ship = m_state.ships[p];
        if (ship.active && checkCircleOverlap(startX, startY, Constants::COMET_RADIUS + 110.0f,
                                              ship.x, ship.y, Constants::SHIP_RADIUS)) {
            return;
        }
    }

    // Aim across the interior of the arena with a random trajectory vector & speed
    const float targetX = randomRange(Constants::ARENA_WIDTH_F * 0.22f, Constants::ARENA_WIDTH_F * 0.78f);
    const float targetY = randomRange(Constants::ARENA_HEIGHT_F * 0.22f, Constants::ARENA_HEIGHT_F * 0.78f);
    float dx = targetX - startX;
    float dy = targetY - startY;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0f) {
        dx = 1.0f;
        dy = 0.0f;
        len = 1.0f;
    }

    const float speed = randomRange(Constants::COMET_MIN_SPEED, Constants::COMET_MAX_SPEED);
    const float life  = randomRange(Constants::COMET_MIN_LIFETIME, Constants::COMET_MAX_LIFETIME);

    CometState& comet = m_state.comets[slot];
    comet.x        = startX;
    comet.y        = startY;
    comet.vx       = (dx / len) * speed;
    comet.vy       = (dy / len) * speed;
    comet.radius   = Constants::COMET_RADIUS;
    comet.lifetime = life;
    comet.maxLife  = life;
    comet.active   = 1;
}

void Simulation::updateComets(float dt) {
    m_cometSpawnTimer -= dt;
    if (m_cometSpawnTimer <= 0.0f) {
        spawnComet();
        m_cometSpawnTimer = randomRange(Constants::COMET_MIN_SPAWN_INTERVAL, Constants::COMET_MAX_SPAWN_INTERVAL);
    }

    const float minX = Constants::ARENA_BORDER_PADDING;
    const float maxX = Constants::ARENA_WIDTH_F - Constants::ARENA_BORDER_PADDING;
    const float minY = Constants::ARENA_BORDER_PADDING;
    const float maxY = Constants::ARENA_HEIGHT_F - Constants::ARENA_BORDER_PADDING;

    for (int i = 0; i < Constants::MAX_COMETS; ++i) {
        CometState& comet = m_state.comets[i];
        if (!comet.active) {
            continue;
        }

        comet.x += comet.vx * dt;
        comet.y += comet.vy * dt;
        comet.lifetime -= dt;

        if (comet.lifetime <= 0.0f ||
            comet.x < minX || comet.x > maxX ||
            comet.y < minY || comet.y > maxY) {
            comet.active = 0;
        }
    }
}

void Simulation::spawnPowerUp() {
    int slot = -1;
    for (int i = 0; i < Constants::MAX_MAP_POWERUPS; ++i) {
        if (!m_state.powerUps[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return;
    }

    const float margin = Constants::ARENA_BORDER_PADDING + 120.0f;
    const float spawnX = randomRange(margin, Constants::ARENA_WIDTH_F - margin);
    const float spawnY = randomRange(margin, Constants::ARENA_HEIGHT_F - margin);

    // Avoid spawning directly on top of a ship
    for (int p = 0; p < Constants::MAX_PLAYERS; ++p) {
        const ShipState& ship = m_state.ships[p];
        if (ship.active && checkCircleOverlap(spawnX, spawnY, Constants::POWERUP_RADIUS + 85.0f,
                                              ship.x, ship.y, Constants::SHIP_RADIUS)) {
            return;
        }
    }

    std::uniform_int_distribution<int> typeDist(1, 7);
    const uint8_t randomType = static_cast<uint8_t>(typeDist(m_rng));

    PowerUpState& pu = m_state.powerUps[slot];
    pu.x        = spawnX;
    pu.y        = spawnY;
    pu.lifetime = Constants::POWERUP_MAP_LIFETIME; // Stays on map for 6 seconds
    pu.type     = randomType;
    pu.active   = 1;
}

void Simulation::updatePowerUps(float dt) {
    m_powerUpSpawnTimer -= dt;
    if (m_powerUpSpawnTimer <= 0.0f) {
        spawnPowerUp();
        m_powerUpSpawnTimer = randomRange(Constants::POWERUP_MIN_SPAWN_INTERVAL, Constants::POWERUP_MAX_SPAWN_INTERVAL);
    }

    for (int i = 0; i < Constants::MAX_MAP_POWERUPS; ++i) {
        PowerUpState& pu = m_state.powerUps[i];
        if (!pu.active) {
            continue;
        }
        pu.lifetime -= dt;
        if (pu.lifetime <= 0.0f) {
            pu.active = 0;
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

    // 2. Ship-vs-PowerUp pickup collision
    for (int i = 0; i < Constants::MAX_MAP_POWERUPS; ++i) {
        PowerUpState& pu = m_state.powerUps[i];
        if (!pu.active) {
            continue;
        }

        for (int playerId = 0; playerId < Constants::MAX_PLAYERS; ++playerId) {
            ShipState& ship = m_state.ships[playerId];
            if (!ship.active) {
                continue;
            }

            if (checkCircleOverlap(pu.x, pu.y, Constants::POWERUP_RADIUS,
                                   ship.x, ship.y, Constants::SHIP_RADIUS)) {
                pu.active = 0;

                switch (static_cast<PowerUpType>(pu.type)) {
                    case PowerUpType::FireRateBoost:
                        ship.fireRateTimer = Constants::POWERUP_FIRERATE_DURATION;
                        break;
                    case PowerUpType::SpeedBoost:
                        ship.speedBoostTimer = Constants::POWERUP_SPEED_DURATION;
                        break;
                    case PowerUpType::Shotgun:
                        ship.shotgunShotsLeft = Constants::POWERUP_SHOTGUN_SHOTS;
                        break;
                    case PowerUpType::StealthImmortal:
                        ship.stealthTimer = Constants::POWERUP_STEALTH_DURATION;
                        break;
                    case PowerUpType::BouncyBullets:
                        ship.bouncyShotsLeft = Constants::POWERUP_BOUNCY_SHOTS;
                        break;
                    case PowerUpType::ExtraLife:
                        if (ship.hp < Constants::SHIP_MAX_HP) {
                            ship.hp += 1;
                        }
                        break;
                    case PowerUpType::FullAmmo:
                        ship.ammo               = 1.0f;
                        ship.ammoOverheated     = 0;
                        ship.overheatSmokeTimer = 0.0f;
                        break;
                    default:
                        break;
                }
                break;
            }
        }
    }

    // 3. Bullet-vs-Comet collision (dense comet core absorbs projectiles)
    for (int i = 0; i < Constants::MAX_BULLETS; ++i) {
        BulletState& bullet = m_state.bullets[i];
        if (!bullet.active) {
            continue;
        }
        for (int c = 0; c < Constants::MAX_COMETS; ++c) {
            const CometState& comet = m_state.comets[c];
            if (!comet.active) {
                continue;
            }
            if (checkCircleOverlap(bullet.x, bullet.y, Constants::BULLET_RADIUS,
                                   comet.x, comet.y, comet.radius)) {
                bullet.active = 0;
                break;
            }
        }
    }

    // 4. Comet-vs-Ship collision (hazards deal 1 HP damage unless ship is immortal via Power-Up #4)
    for (int c = 0; c < Constants::MAX_COMETS; ++c) {
        CometState& comet = m_state.comets[c];
        if (!comet.active) {
            continue;
        }
        for (int targetId = 0; targetId < Constants::MAX_PLAYERS; ++targetId) {
            ShipState& targetShip = m_state.ships[targetId];
            if (!targetShip.active || targetShip.stealthTimer > 0.0f) {
                continue;
            }
            if (checkCircleOverlap(comet.x, comet.y, comet.radius,
                                   targetShip.x, targetShip.y, Constants::SHIP_RADIUS)) {
                comet.active = 0;

                const float cSpeed = std::sqrt(comet.vx * comet.vx + comet.vy * comet.vy);
                if (cSpeed > 0.001f) {
                    targetShip.vx += (comet.vx / cSpeed) * Constants::COMET_HIT_KNOCKBACK;
                    targetShip.vy += (comet.vy / cSpeed) * Constants::COMET_HIT_KNOCKBACK;
                }

                targetShip.hp -= 1;
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

    // 5. Bullet-vs-Ship circle collision & HP tracking (ignores immortal ships via Power-Up #4)
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
            if (!targetShip.active || targetShip.stealthTimer > 0.0f) {
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
