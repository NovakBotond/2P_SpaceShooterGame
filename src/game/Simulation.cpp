// =============================================================================
// OWNER: Developer B (Networking & Gameplay Logic)
// FILE:  src/game/Simulation.cpp
// DESC:  Implementation of server-authoritative 2D physics, projectiles,
//        destructible comets, 13 power-ups, Co-op wave progression & bosses,
//        ally enemy conversion, co-op teammate healing, and shared prediction.
// =============================================================================

#include "game/Simulation.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

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

    void initShipForMatchStart(ShipState& ship, float x, float y, float angle, bool active) {
        ship = ShipState{};
        ship.x      = x;
        ship.y      = y;
        ship.angle  = angle;
        ship.hp     = Constants::SHIP_MAX_HP;
        ship.maxHp  = Constants::SHIP_MAX_HP;
        ship.ammo   = 1.0f;
        ship.active = active ? 1 : 0;
    }

    void resetShipPowerUpsForRound(ShipState& ship, float x, float y, float angle) {
        if (ship.maxHp < Constants::SHIP_MAX_HP) {
            ship.maxHp = Constants::SHIP_MAX_HP;
        }
        ship.x                   = x;
        ship.y                   = y;
        ship.vx                  = 0.0f;
        ship.vy                  = 0.0f;
        ship.angle               = angle;
        ship.hp                  = ship.maxHp;
        ship.ammo                = 1.0f;
        ship.overheatSmokeTimer  = 0.0f;
        ship.fireRateTimer       = 0.0f;
        ship.speedBoostTimer     = 0.0f;
        ship.stealthTimer        = 0.0f;
        ship.shieldCooldownTimer = 0.0f;
        ship.shieldHp            = 0;
        ship.shotgunShotsLeft    = 0;
        ship.bouncyShotsLeft     = 0;
        ship.radialShotsLeft     = 0;
        ship.allyHealHits        = 0;
        ship.ammoOverheated      = 0;
        ship.isThrusting         = 0;
        ship.active              = 1;
    }
}

Simulation::Simulation()
    : m_rng(std::random_device{}()) {
    resetMatch(GameMode::PvP);
}

float Simulation::randomRange(float minVal, float maxVal) {
    std::uniform_real_distribution<float> dist(minVal, maxVal);
    return dist(m_rng);
}

void Simulation::resetMatch(GameMode mode) {
    m_state = GameState{};
    m_state.packetType       = static_cast<uint8_t>(PacketType::GAME_STATE);
    m_state.tick             = 0;
    m_state.lastClientTick   = 0;
    m_state.gameMode         = static_cast<uint8_t>(mode);
    m_state.currentWave      = 1;
    m_state.lastRoundWinner  = 255;
    m_state.roundBannerTimer = 0.0f;
    m_state.bannerCode       = 0;

    for (int i = 0; i < Constants::MAX_PLAYERS; ++i) {
        m_state.ships[i].score = 0;
    }

    m_contactCooldowns[0] = 0.0f;
    m_contactCooldowns[1] = 0.0f;
    for (int p = 0; p < Constants::MAX_PLAYERS; ++p) {
        for (int e = 0; e < Constants::MAX_ENEMIES; ++e) {
            m_ramCooldowns[p][e] = 0.0f;
        }
    }

    if (mode == GameMode::CoopWaves) {
        initShipForMatchStart(m_state.ships[0], Constants::ARENA_WIDTH_F * 0.35f, Constants::ARENA_HEIGHT_F * 0.50f, 0.0f, true);
        initShipForMatchStart(m_state.ships[1], Constants::ARENA_WIDTH_F * 0.65f, Constants::ARENA_HEIGHT_F * 0.50f, PI_VAL, m_coopP2Present);

        for (int i = 0; i < Constants::MAX_BULLETS; ++i) {
            m_state.bullets[i] = BulletState{};
        }
        for (int i = 0; i < Constants::MAX_COMETS; ++i) {
            m_state.comets[i] = CometState{};
        }
        for (int i = 0; i < Constants::MAX_MAP_POWERUPS; ++i) {
            m_state.powerUps[i] = PowerUpState{};
        }
        for (int i = 0; i < Constants::MAX_ENEMIES; ++i) {
            m_state.enemies[i] = EnemyState{};
            m_enemyFireCooldowns[i] = 0.0f;
        }

        m_fireCooldowns[0]  = 0.0f;
        m_fireCooldowns[1]  = 0.0f;
        m_cometSpawnTimer   = randomRange(Constants::COMET_MIN_SPAWN_INTERVAL, Constants::COMET_MAX_SPAWN_INTERVAL);
        m_powerUpSpawnTimer = randomRange(Constants::POWERUP_MIN_SPAWN_INTERVAL, Constants::POWERUP_MAX_SPAWN_INTERVAL);
        m_waveClearPending  = false;
        m_waveClearTimer    = 0.0f;
        startWave(1);
    } else {
        resetRound();
    }
}

void Simulation::resetRound() {
    resetShipPowerUpsForRound(m_state.ships[0], Constants::ARENA_WIDTH_F * 0.20f, Constants::ARENA_HEIGHT_F * 0.50f, 0.0f);
    resetShipPowerUpsForRound(m_state.ships[1], Constants::ARENA_WIDTH_F * 0.80f, Constants::ARENA_HEIGHT_F * 0.50f, PI_VAL);

    for (int i = 0; i < Constants::MAX_BULLETS; ++i) {
        m_state.bullets[i] = BulletState{};
    }
    for (int i = 0; i < Constants::MAX_COMETS; ++i) {
        m_state.comets[i] = CometState{};
    }
    for (int i = 0; i < Constants::MAX_MAP_POWERUPS; ++i) {
        m_state.powerUps[i] = PowerUpState{};
    }
    for (int i = 0; i < Constants::MAX_ENEMIES; ++i) {
        m_state.enemies[i] = EnemyState{};
        m_enemyFireCooldowns[i] = 0.0f;
    }

    m_fireCooldowns[0]    = 0.0f;
    m_fireCooldowns[1]    = 0.0f;
    m_contactCooldowns[0] = 0.0f;
    m_contactCooldowns[1] = 0.0f;
    for (int p = 0; p < Constants::MAX_PLAYERS; ++p) {
        for (int e = 0; e < Constants::MAX_ENEMIES; ++e) {
            m_ramCooldowns[p][e] = 0.0f;
        }
    }
    m_cometSpawnTimer     = randomRange(Constants::COMET_MIN_SPAWN_INTERVAL, Constants::COMET_MAX_SPAWN_INTERVAL);
    m_powerUpSpawnTimer   = randomRange(Constants::POWERUP_MIN_SPAWN_INTERVAL, Constants::POWERUP_MAX_SPAWN_INTERVAL);
}

void Simulation::setCoopPlayer2Present(bool present) {
    const bool wasPresent = m_coopP2Present;
    m_coopP2Present = present;

    if (m_state.gameMode != static_cast<uint8_t>(GameMode::CoopWaves)) {
        return;
    }

    if (present && !wasPresent) {
        resetShipPowerUpsForRound(m_state.ships[1], Constants::ARENA_WIDTH_F * 0.65f, Constants::ARENA_HEIGHT_F * 0.50f, PI_VAL);
    } else if (!present && wasPresent) {
        m_state.ships[1].active = 0;
    }
}

void Simulation::startWave(uint16_t waveNumber) {
    m_state.currentWave        = waveNumber;
    m_state.bossActive         = 0;
    m_state.enemySlowTimer     = 0.0f;
    m_state.enemyFireSlowTimer = 0.0f;
    m_state.bannerCode         = 1;
    m_state.roundBannerTimer   = 2.2f;

    // Revive downed teammates between waves in Co-op
    if (!m_state.ships[0].active) {
        resetShipPowerUpsForRound(m_state.ships[0], Constants::ARENA_WIDTH_F * 0.35f, Constants::ARENA_HEIGHT_F * 0.50f, 0.0f);
    }
    if (m_coopP2Present && !m_state.ships[1].active) {
        resetShipPowerUpsForRound(m_state.ships[1], Constants::ARENA_WIDTH_F * 0.65f, Constants::ARENA_HEIGHT_F * 0.50f, PI_VAL);
    }

    for (int i = 0; i < Constants::MAX_ENEMIES; ++i) {
        m_state.enemies[i] = EnemyState{};
        m_enemyFireCooldowns[i] = 0.0f;
    }

    const int normalEnemiesTotal = 5 + (static_cast<int>(waveNumber) - 1) * 2;
    m_waveNormalToSpawn          = normalEnemiesTotal;
    m_state.waveRemainingKills   = static_cast<uint16_t>(normalEnemiesTotal + 1); // +1 for wave Boss

    std::uniform_int_distribution<int> triggerDist(2, 3);
    m_bossTriggerRemaining = triggerDist(m_rng);
    m_bossSpawnedThisWave  = false;
    m_waveClearPending     = false;
    m_waveClearTimer       = 0.0f;
    m_enemySpawnTimer      = 0.75f;
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

    for (int i = 0; i < Constants::MAX_ENEMIES; ++i) {
        const EnemyState& p = prev.enemies[i];
        const EnemyState& c = curr.enemies[i];
        if (!p.active || !c.active || p.isBoss != c.isBoss) {
            continue;
        }
        const float dx = c.x - p.x;
        const float dy = c.y - p.y;
        if (dx * dx + dy * dy < 80.0f * 80.0f) {
            out.enemies[i].x     = p.x + dx * t;
            out.enemies[i].y     = p.y + dy * t;
            out.enemies[i].angle = p.angle + shortestAngleDiff(p.angle, c.angle) * t;
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
    if (ship.shieldCooldownTimer > 0.0f) {
        ship.shieldCooldownTimer = std::max(0.0f, ship.shieldCooldownTimer - dt);
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
    if (state.enemySlowTimer > 0.0f) {
        state.enemySlowTimer = std::max(0.0f, state.enemySlowTimer - dt);
    }
    if (state.enemyFireSlowTimer > 0.0f) {
        state.enemyFireSlowTimer = std::max(0.0f, state.enemyFireSlowTimer - dt);
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

        for (int i = 0; i < Constants::MAX_ENEMIES; ++i) {
            EnemyState& e = state.enemies[i];
            if (!e.active) {
                continue;
            }
            e.x += e.vx * dt;
            e.y += e.vy * dt;
            if (e.allyTimer > 0.0f) {
                e.allyTimer = std::max(0.0f, e.allyTimer - dt);
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

    for (int i = 0; i < Constants::MAX_PLAYERS; ++i) {
        if (m_contactCooldowns[i] > 0.0f) {
            m_contactCooldowns[i] = std::max(0.0f, m_contactCooldowns[i] - dt);
        }
        for (int e = 0; e < Constants::MAX_ENEMIES; ++e) {
            if (m_ramCooldowns[i][e] > 0.0f) {
                m_ramCooldowns[i][e] = std::max(0.0f, m_ramCooldowns[i][e] - dt);
            }
        }
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

    // 5. Co-op wave enemies & bosses
    if (m_state.gameMode == static_cast<uint8_t>(GameMode::CoopWaves)) {
        updateEnemies(dt);
    }

    // 6. Perform circle-based collision detection & round/wave evaluation
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
        if (ship.radialShotsLeft > 0) {
            // Radial 360-deg burst: 8 bullets spaced every 45 degrees, all bounce for 5s
            ship.radialShotsLeft -= 1;
            constexpr float step45 = PI_VAL * 0.25f;
            for (int k = 0; k < 8; ++k) {
                spawnBullet(playerId, ship, static_cast<float>(k) * step45 - ship.angle, true, true);
            }
        } else {
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
        }

        const float baseCooldown = Constants::SHIP_FIRE_COOLDOWN;
        m_fireCooldowns[playerId] = (ship.fireRateTimer > 0.0f)
            ? (baseCooldown / Constants::POWERUP_FIRERATE_MULT)
            : baseCooldown;
    }
}

void Simulation::spawnBullet(int playerId,
                             const ShipState& ship,
                             float angleOffset,
                             bool bouncy,
                             bool ignoreShipVel) {
    for (int i = 0; i < Constants::MAX_BULLETS; ++i) {
        BulletState& bullet = m_state.bullets[i];
        if (!bullet.active) {
            const float fireAngle = ship.angle + angleOffset;
            const float dirX = std::cos(fireAngle);
            const float dirY = std::sin(fireAngle);
            const float spawnDist = Constants::SHIP_RADIUS + Constants::BULLET_RADIUS + 4.0f;

            bullet.x              = ship.x + dirX * spawnDist;
            bullet.y              = ship.y + dirY * spawnDist;
            bullet.vx             = dirX * Constants::BULLET_SPEED + (ignoreShipVel ? 0.0f : ship.vx * 0.25f);
            bullet.vy             = dirY * Constants::BULLET_SPEED + (ignoreShipVel ? 0.0f : ship.vy * 0.25f);
            bullet.lifetime       = bouncy ? Constants::BOUNCY_BULLET_LIFETIME : Constants::BULLET_LIFETIME;
            bullet.ownerId        = static_cast<uint8_t>(playerId);
            bullet.bouncesOnWalls = bouncy ? 1 : 0;
            bullet.active         = 1;
            return;
        }
    }
}

void Simulation::spawnEnemyBullet(const EnemyState& enemy, float aimAngle, uint8_t ownerId) {
    for (int i = 0; i < Constants::MAX_BULLETS; ++i) {
        BulletState& bullet = m_state.bullets[i];
        if (!bullet.active) {
            const float dirX = std::cos(aimAngle);
            const float dirY = std::sin(aimAngle);
            const float radius = enemy.isBoss ? Constants::ENEMY_BOSS_RADIUS : Constants::ENEMY_RADIUS;
            const float spawnDist = radius + Constants::BULLET_RADIUS + 6.0f;

            bullet.x              = enemy.x + dirX * spawnDist;
            bullet.y              = enemy.y + dirY * spawnDist;
            bullet.vx             = dirX * Constants::ENEMY_BULLET_SPEED;
            bullet.vy             = dirY * Constants::ENEMY_BULLET_SPEED;
            bullet.lifetime       = 3.0f;
            bullet.ownerId        = ownerId; // 2 = Hostile Enemy, 3 = Ally Enemy
            bullet.bouncesOnWalls = 0;
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

    for (int p = 0; p < Constants::MAX_PLAYERS; ++p) {
        const ShipState& ship = m_state.ships[p];
        if (ship.active && checkCircleOverlap(startX, startY, Constants::COMET_RADIUS + 110.0f,
                                              ship.x, ship.y, Constants::SHIP_RADIUS)) {
            return;
        }
    }

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
    comet.hp       = Constants::COMET_MAX_HP;
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

    for (int p = 0; p < Constants::MAX_PLAYERS; ++p) {
        const ShipState& ship = m_state.ships[p];
        if (ship.active && checkCircleOverlap(spawnX, spawnY, Constants::POWERUP_RADIUS + 85.0f,
                                              ship.x, ship.y, Constants::SHIP_RADIUS)) {
            return;
        }
    }

    const int maxTypeId = (m_state.gameMode == static_cast<uint8_t>(GameMode::CoopWaves)) ? 13 : 9;
    std::uniform_int_distribution<int> typeDist(1, maxTypeId);
    const uint8_t randomType = static_cast<uint8_t>(typeDist(m_rng));

    PowerUpState& pu = m_state.powerUps[slot];
    pu.x        = spawnX;
    pu.y        = spawnY;
    pu.lifetime = Constants::POWERUP_MAP_LIFETIME;
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

void Simulation::spawnEnemy(bool asBoss) {
    int slot = -1;
    for (int i = 0; i < Constants::MAX_ENEMIES; ++i) {
        if (!m_state.enemies[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return;
    }

    const float radius = asBoss ? Constants::ENEMY_BOSS_RADIUS : Constants::ENEMY_RADIUS;
    const float pad = Constants::ARENA_BORDER_PADDING + radius + 6.0f;

    std::uniform_int_distribution<int> edgeDist(0, 3);
    const int edge = edgeDist(m_rng);

    float startX = 0.0f;
    float startY = 0.0f;
    switch (edge) {
        case 0: startX = randomRange(pad, Constants::ARENA_WIDTH_F - pad); startY = pad; break;
        case 1: startX = randomRange(pad, Constants::ARENA_WIDTH_F - pad); startY = Constants::ARENA_HEIGHT_F - pad; break;
        case 2: startX = pad; startY = randomRange(pad, Constants::ARENA_HEIGHT_F - pad); break;
        default: startX = Constants::ARENA_WIDTH_F - pad; startY = randomRange(pad, Constants::ARENA_HEIGHT_F - pad); break;
    }

    const int normalHp = Constants::ENEMY_BASE_HP + (static_cast<int>(m_state.currentWave) - 1) / 2;
    const int hp = asBoss ? (normalHp * 2) : normalHp;

    EnemyState& enemy = m_state.enemies[slot];
    enemy.x         = startX;
    enemy.y         = startY;
    enemy.vx        = 0.0f;
    enemy.vy        = 0.0f;
    enemy.angle     = std::atan2(Constants::ARENA_HEIGHT_F * 0.5f - startY,
                                 Constants::ARENA_WIDTH_F * 0.5f - startX);
    enemy.allyTimer = 0.0f;
    enemy.hp        = static_cast<int16_t>(hp);
    enemy.maxHp     = static_cast<int16_t>(hp);
    enemy.isBoss    = asBoss ? 1 : 0;
    enemy.active    = 1;

    m_enemyFireCooldowns[slot] = randomRange(0.7f, 1.5f);
}

void Simulation::updateEnemies(float dt) {
    if (m_state.enemySlowTimer > 0.0f) {
        m_state.enemySlowTimer = std::max(0.0f, m_state.enemySlowTimer - dt);
    }
    if (m_state.enemyFireSlowTimer > 0.0f) {
        m_state.enemyFireSlowTimer = std::max(0.0f, m_state.enemyFireSlowTimer - dt);
    }

    if (m_waveClearPending) {
        m_waveClearTimer -= dt;
        if (m_waveClearTimer <= 0.0f) {
            m_waveClearPending = false;
            startWave(m_state.currentWave + 1);
        }
        return;
    }

    int activeNormal = 0;
    int activeTotal  = 0;
    for (int i = 0; i < Constants::MAX_ENEMIES; ++i) {
        if (m_state.enemies[i].active) {
            ++activeTotal;
            if (!m_state.enemies[i].isBoss) {
                ++activeNormal;
            }
        }
    }

    // Stagger normal enemy spawns from edges (capped at concurrent limit)
    const int maxConcurrent = std::min(Constants::MAX_ENEMIES - 1, 4 + static_cast<int>(m_state.currentWave));
    if (m_waveNormalToSpawn > 0) {
        m_enemySpawnTimer -= dt;
        if (m_enemySpawnTimer <= 0.0f && activeTotal < maxConcurrent) {
            spawnEnemy(false);
            --m_waveNormalToSpawn;
            m_enemySpawnTimer = std::max(0.45f, Constants::ENEMY_SPAWN_INTERVAL - 0.04f * m_state.currentWave);
            ++activeNormal;
            ++activeTotal;
        }
    }

    // Boss spawns at the end of every wave when 2 to 3 enemies remain
    if (!m_bossSpawnedThisWave && m_waveNormalToSpawn == 0 && activeNormal <= m_bossTriggerRemaining) {
        spawnEnemy(true);
        m_bossSpawnedThisWave    = true;
        m_state.bossActive       = 1;
        m_state.bannerCode       = 2;
        m_state.roundBannerTimer = 2.4f;
        ++activeTotal;
    }

    // Check wave completion
    if (m_waveNormalToSpawn == 0 && m_bossSpawnedThisWave && activeTotal == 0) {
        m_waveClearPending       = true;
        m_waveClearTimer         = Constants::WAVE_CLEAR_DELAY;
        m_state.bannerCode       = 3;
        m_state.roundBannerTimer = Constants::WAVE_CLEAR_DELAY;
        return;
    }

    // Update enemy AI movement & firing
    const int speedTier = static_cast<int>(m_state.currentWave) / 5;
    float baseSpeed = Constants::ENEMY_BASE_SPEED *
                      std::pow(Constants::ENEMY_SPEED_STEP_MULT, static_cast<float>(speedTier));
    if (m_state.enemySlowTimer > 0.0f) {
        baseSpeed *= Constants::POWERUP_ENEMY_SLOW_MULT; // -30% enemy speed power-up
    }

    const float pad = Constants::ARENA_BORDER_PADDING;

    for (int i = 0; i < Constants::MAX_ENEMIES; ++i) {
        EnemyState& enemy = m_state.enemies[i];
        if (!enemy.active) {
            continue;
        }

        if (m_enemyFireCooldowns[i] > 0.0f) {
            m_enemyFireCooldowns[i] = std::max(0.0f, m_enemyFireCooldowns[i] - dt);
        }

        const bool isAlly = (enemy.allyTimer > 0.0f);
        if (isAlly) {
            enemy.allyTimer = std::max(0.0f, enemy.allyTimer - dt);
        }

        // Find target position
        bool hasTarget = false;
        float targetX = Constants::ARENA_WIDTH_F * 0.5f;
        float targetY = Constants::ARENA_HEIGHT_F * 0.5f;
        float bestDistSq = 1e12f;

        if (isAlly) {
            // Mind-controlled ally targets nearest hostile enemy
            for (int j = 0; j < Constants::MAX_ENEMIES; ++j) {
                if (j == i || !m_state.enemies[j].active || m_state.enemies[j].allyTimer > 0.0f) {
                    continue;
                }
                const float dx = m_state.enemies[j].x - enemy.x;
                const float dy = m_state.enemies[j].y - enemy.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 < bestDistSq) {
                    bestDistSq = d2;
                    targetX = m_state.enemies[j].x;
                    targetY = m_state.enemies[j].y;
                    hasTarget = true;
                }
            }
        } else {
            // Hostile enemy targets nearest visible alive player
            for (int p = 0; p < Constants::MAX_PLAYERS; ++p) {
                const ShipState& ship = m_state.ships[p];
                if (!ship.active || ship.stealthTimer > 0.0f) {
                    continue;
                }
                const float dx = ship.x - enemy.x;
                const float dy = ship.y - enemy.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 < bestDistSq) {
                    bestDistSq = d2;
                    targetX = ship.x;
                    targetY = ship.y;
                    hasTarget = true;
                }
            }
        }

        const float desiredAngle = hasTarget
            ? std::atan2(targetY - enemy.y, targetX - enemy.x)
            : enemy.angle;

        const float diff = shortestAngleDiff(enemy.angle, desiredAngle);
        const float maxTurn = 3.2f * dt;
        enemy.angle += std::clamp(diff, -maxTurn, maxTurn);

        const float dist = std::sqrt(std::max(bestDistSq, 1.0f));
        const float preferredDist = enemy.isBoss ? 320.0f : 240.0f;

        float desiredVx = std::cos(enemy.angle) * baseSpeed;
        float desiredVy = std::sin(enemy.angle) * baseSpeed;
        if (hasTarget && dist < preferredDist) {
            // Orbit around preferred combat range instead of crashing straight into target
            desiredVx = -std::sin(enemy.angle) * (baseSpeed * 0.75f);
            desiredVy =  std::cos(enemy.angle) * (baseSpeed * 0.75f);
        }

        // Soft separation from sibling enemies so they don't stack on top of one another
        for (int j = 0; j < Constants::MAX_ENEMIES; ++j) {
            if (j == i || !m_state.enemies[j].active) {
                continue;
            }
            const float sx = enemy.x - m_state.enemies[j].x;
            const float sy = enemy.y - m_state.enemies[j].y;
            const float d2 = sx * sx + sy * sy;
            if (d2 > 0.1f && d2 < 75.0f * 75.0f) {
                const float invD = 1.0f / std::sqrt(d2);
                desiredVx += sx * invD * 95.0f;
                desiredVy += sy * invD * 95.0f;
            }
        }

        const float blend = std::min(1.0f, dt * 4.0f);
        enemy.vx += (desiredVx - enemy.vx) * blend;
        enemy.vy += (desiredVy - enemy.vy) * blend;

        enemy.x += enemy.vx * dt;
        enemy.y += enemy.vy * dt;

        const float r = enemy.isBoss ? Constants::ENEMY_BOSS_RADIUS : Constants::ENEMY_RADIUS;
        enemy.x = std::clamp(enemy.x, pad + r, Constants::ARENA_WIDTH_F - pad - r);
        enemy.y = std::clamp(enemy.y, pad + r, Constants::ARENA_HEIGHT_F - pad - r);

        // Fire when aligned with a valid target
        if (hasTarget && std::abs(diff) < 0.35f && m_enemyFireCooldowns[i] <= 0.0f) {
            spawnEnemyBullet(enemy, enemy.angle, isAlly ? 3 : 2);

            float cd = Constants::ENEMY_BASE_FIRE_COOLDOWN * (enemy.isBoss ? 0.50f : 1.0f);
            if (!isAlly && m_state.enemyFireSlowTimer > 0.0f) {
                cd /= Constants::POWERUP_ENEMY_FSLOW_MULT; // -30% enemy fire rate
            }
            m_enemyFireCooldowns[i] = cd;
        }
    }
}

void Simulation::defeatEnemySlot(int enemyIdx, int killerPlayerId) {
    EnemyState& enemy = m_state.enemies[enemyIdx];
    const bool wasBoss = (enemy.isBoss != 0);
    enemy.active = 0;

    if (wasBoss) {
        m_state.bossActive = 0;
    }
    if (m_state.waveRemainingKills > 0) {
        m_state.waveRemainingKills -= 1;
    }

    if (killerPlayerId >= 0 && killerPlayerId < Constants::MAX_PLAYERS) {
        ShipState& killer = m_state.ships[killerPlayerId];
        killer.kills += 1;
        killer.score += wasBoss ? 5 : 1;

        // Killing the Wave Boss grants +1 max HP to the killer
        if (wasBoss) {
            killer.maxHp += 1;
        }

        // Each enemy kill in Co-op grants +1 life (up to killer's maxHp)
        if (killer.active && killer.hp < killer.maxHp) {
            killer.hp = std::min(killer.maxHp, killer.hp + 1);
        }
    }
}

bool Simulation::applyDamageToShip(int targetId, float knockVx, float knockVy) {
    ShipState& targetShip = m_state.ships[targetId];
    if (!targetShip.active || targetShip.stealthTimer > 0.0f) {
        return false;
    }

    targetShip.vx += knockVx;
    targetShip.vy += knockVy;

    // Blue bubble shield absorbs up to 2 hits before breaking + starting 30s lockout
    if (targetShip.shieldHp > 0) {
        targetShip.shieldHp -= 1;
        if (targetShip.shieldHp <= 0) {
            targetShip.shieldHp = 0;
            targetShip.shieldCooldownTimer = Constants::POWERUP_SHIELD_COOLDOWN;
        }
        return false;
    }

    targetShip.hp -= 1;
    return (targetShip.hp <= 0);
}

void Simulation::handlePlayerDefeat(int defeatedPlayerId) {
    if (m_state.gameMode == static_cast<uint8_t>(GameMode::PvP)) {
        const int winnerId = (defeatedPlayerId == 0) ? 1 : 0;
        m_state.ships[winnerId].score += 1;
        m_state.lastRoundWinner        = static_cast<uint8_t>(winnerId);
        m_state.bannerCode             = 0;
        m_state.roundBannerTimer       = Constants::ROUND_BANNER_DURATION;
        resetRound();
        return;
    }

    // Co-op Waves mode: player goes down; if all players are down, restart Wave 1
    m_state.ships[defeatedPlayerId].hp     = 0;
    m_state.ships[defeatedPlayerId].active = 0;

    if (!m_state.ships[0].active && !m_state.ships[1].active) {
        resetMatch(GameMode::CoopWaves);
        m_state.bannerCode       = 4;
        m_state.roundBannerTimer = 2.6f;
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

        s0.x -= nx * (overlap * 0.5f);
        s0.y -= ny * (overlap * 0.5f);
        s1.x += nx * (overlap * 0.5f);
        s1.y += ny * (overlap * 0.5f);

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

            if (!checkCircleOverlap(pu.x, pu.y, Constants::POWERUP_RADIUS,
                                    ship.x, ship.y, Constants::SHIP_RADIUS)) {
                continue;
            }

            const PowerUpType pType = static_cast<PowerUpType>(pu.type);

            // Shield lockout rule: cannot pick up for 30s after breaking (or while active)
            if (pType == PowerUpType::ShieldBubble &&
                (ship.shieldHp > 0 || ship.shieldCooldownTimer > 0.0f)) {
                continue;
            }

            pu.active = 0;

            switch (pType) {
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
                    if (ship.hp < ship.maxHp) {
                        ship.hp += 1;
                    }
                    break;
                case PowerUpType::FullAmmo:
                    ship.ammo               = 1.0f;
                    ship.ammoOverheated     = 0;
                    ship.overheatSmokeTimer = 0.0f;
                    break;
                case PowerUpType::RadialBurst:
                    ship.radialShotsLeft = Constants::POWERUP_RADIAL_SHOTS;
                    break;
                case PowerUpType::ShieldBubble:
                    ship.shieldHp = Constants::POWERUP_SHIELD_HP;
                    break;
                case PowerUpType::EnemySlow:
                    m_state.enemySlowTimer = Constants::POWERUP_ENEMY_SLOW_DURATION;
                    break;
                case PowerUpType::EnemyFireSlow:
                    m_state.enemyFireSlowTimer = Constants::POWERUP_ENEMY_FSLOW_DURATION;
                    break;
                case PowerUpType::EnemyKillRandom: {
                    std::vector<int> candidates;
                    for (int e = 0; e < Constants::MAX_ENEMIES; ++e) {
                        if (m_state.enemies[e].active && m_state.enemies[e].allyTimer <= 0.0f) {
                            candidates.push_back(e);
                        }
                    }
                    if (!candidates.empty()) {
                        std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
                        defeatEnemySlot(candidates[pick(m_rng)], playerId);
                    }
                    break;
                }
                case PowerUpType::EnemyAlly: {
                    std::vector<int> normalHostiles;
                    std::vector<int> anyHostiles;
                    for (int e = 0; e < Constants::MAX_ENEMIES; ++e) {
                        if (m_state.enemies[e].active && m_state.enemies[e].allyTimer <= 0.0f) {
                            anyHostiles.push_back(e);
                            if (!m_state.enemies[e].isBoss) {
                                normalHostiles.push_back(e);
                            }
                        }
                    }
                    const auto& pool = !normalHostiles.empty() ? normalHostiles : anyHostiles;
                    if (!pool.empty()) {
                        std::uniform_int_distribution<size_t> pick(0, pool.size() - 1);
                        m_state.enemies[pool[pick(m_rng)]].allyTimer = Constants::POWERUP_ENEMY_ALLY_DURATION;
                    }
                    break;
                }
                default:
                    break;
            }
            break;
        }
    }

    // 3. Bullet-vs-Comet collision (disappears after 3 player shots)
    for (int i = 0; i < Constants::MAX_BULLETS; ++i) {
        BulletState& bullet = m_state.bullets[i];
        if (!bullet.active) {
            continue;
        }
        for (int c = 0; c < Constants::MAX_COMETS; ++c) {
            CometState& comet = m_state.comets[c];
            if (!comet.active) {
                continue;
            }
            if (checkCircleOverlap(bullet.x, bullet.y, Constants::BULLET_RADIUS,
                                   comet.x, comet.y, comet.radius)) {
                bullet.active = 0;
                comet.hp -= 1;
                if (comet.hp <= 0) {
                    comet.active = 0;
                }
                break;
            }
        }
    }

    // 4. Comet-vs-Ship & Comet-vs-Enemy collisions
    for (int c = 0; c < Constants::MAX_COMETS; ++c) {
        CometState& comet = m_state.comets[c];
        if (!comet.active) {
            continue;
        }

        const float cSpeed = std::sqrt(comet.vx * comet.vx + comet.vy * comet.vy);
        const float knockVx = (cSpeed > 0.001f) ? ((comet.vx / cSpeed) * Constants::COMET_HIT_KNOCKBACK) : 0.0f;
        const float knockVy = (cSpeed > 0.001f) ? ((comet.vy / cSpeed) * Constants::COMET_HIT_KNOCKBACK) : 0.0f;

        bool cometConsumed = false;
        for (int targetId = 0; targetId < Constants::MAX_PLAYERS; ++targetId) {
            ShipState& targetShip = m_state.ships[targetId];
            if (!targetShip.active || targetShip.stealthTimer > 0.0f) {
                continue;
            }
            if (checkCircleOverlap(comet.x, comet.y, comet.radius,
                                   targetShip.x, targetShip.y, Constants::SHIP_RADIUS)) {
                comet.active = 0;
                cometConsumed = true;

                if (applyDamageToShip(targetId, knockVx, knockVy)) {
                    handlePlayerDefeat(targetId);
                    if (m_state.gameMode == static_cast<uint8_t>(GameMode::PvP)) {
                        return;
                    }
                }
                break;
            }
        }

        if (cometConsumed) {
            continue;
        }

        // Comets also damage enemies (same as players)
        if (m_state.gameMode == static_cast<uint8_t>(GameMode::CoopWaves)) {
            for (int e = 0; e < Constants::MAX_ENEMIES; ++e) {
                EnemyState& enemy = m_state.enemies[e];
                if (!enemy.active) {
                    continue;
                }
                const float eRadius = enemy.isBoss ? Constants::ENEMY_BOSS_RADIUS : Constants::ENEMY_RADIUS;
                if (checkCircleOverlap(comet.x, comet.y, comet.radius,
                                       enemy.x, enemy.y, eRadius)) {
                    comet.active = 0;
                    enemy.vx += knockVx;
                    enemy.vy += knockVy;
                    enemy.hp -= 1;
                    if (enemy.hp <= 0) {
                        defeatEnemySlot(e, -1);
                    }
                    break;
                }
            }
        }
    }

    // 5. Enemy-vs-Player contact collision & Blue Shield Ramming (Co-op)
    if (m_state.gameMode == static_cast<uint8_t>(GameMode::CoopWaves)) {
        for (int e = 0; e < Constants::MAX_ENEMIES; ++e) {
            EnemyState& enemy = m_state.enemies[e];
            if (!enemy.active || enemy.allyTimer > 0.0f) {
                continue;
            }
            const float eRadius = enemy.isBoss ? Constants::ENEMY_BOSS_RADIUS : Constants::ENEMY_RADIUS;

            for (int targetId = 0; targetId < Constants::MAX_PLAYERS; ++targetId) {
                ShipState& ship = m_state.ships[targetId];
                if (!ship.active || ship.stealthTimer > 0.0f) {
                    continue;
                }

                const bool hasShield = (ship.shieldHp > 0);
                const float shipColR = hasShield ? (Constants::SHIP_RADIUS + 10.0f) : Constants::SHIP_RADIUS;

                if (!checkCircleOverlap(enemy.x, enemy.y, eRadius, ship.x, ship.y, shipColR)) {
                    continue;
                }

                float dx = ship.x - enemy.x;
                float dy = ship.y - enemy.y;
                float dist = std::sqrt(dx * dx + dy * dy);
                if (dist < 0.001f) {
                    dx = 1.0f;
                    dy = 0.0f;
                    dist = 1.0f;
                }
                const float nx = dx / dist;
                const float ny = dy / dist;

                if (hasShield) {
                    // Shielded player can ram enemies without taking damage (1 hit = -1hp)
                    if (m_ramCooldowns[targetId][e] <= 0.0f) {
                        m_ramCooldowns[targetId][e] = 0.35f;
                        enemy.vx -= nx * 280.0f;
                        enemy.vy -= ny * 280.0f;
                        enemy.hp -= 1;
                        if (enemy.hp <= 0) {
                            defeatEnemySlot(e, targetId);
                        }
                    }
                    break;
                }

                if (m_contactCooldowns[targetId] <= 0.0f) {
                    enemy.vx -= nx * 160.0f;
                    enemy.vy -= ny * 160.0f;
                    m_contactCooldowns[targetId] = 0.75f;

                    if (applyDamageToShip(targetId, nx * 240.0f, ny * 240.0f)) {
                        handlePlayerDefeat(targetId);
                    }
                }
            }
        }
    }

    // 6. Bullet-vs-Enemy & Bullet-vs-Ship collisions
    const bool isCoop = (m_state.gameMode == static_cast<uint8_t>(GameMode::CoopWaves));

    for (int i = 0; i < Constants::MAX_BULLETS; ++i) {
        BulletState& bullet = m_state.bullets[i];
        if (!bullet.active) {
            continue;
        }

        // 6a. Player bullets (0,1) & Ally Enemy bullets (3) can hit hostile enemies
        if (isCoop && (bullet.ownerId == 0 || bullet.ownerId == 1 || bullet.ownerId == 3)) {
            bool hitEnemy = false;
            for (int e = 0; e < Constants::MAX_ENEMIES; ++e) {
                EnemyState& enemy = m_state.enemies[e];
                if (!enemy.active || enemy.allyTimer > 0.0f) {
                    continue;
                }
                const float eRadius = enemy.isBoss ? Constants::ENEMY_BOSS_RADIUS : Constants::ENEMY_RADIUS;
                if (checkCircleOverlap(bullet.x, bullet.y, Constants::BULLET_RADIUS,
                                       enemy.x, enemy.y, eRadius)) {
                    bullet.active = 0;
                    enemy.hp -= 1;
                    if (enemy.hp <= 0) {
                        const int killerId = (bullet.ownerId < Constants::MAX_PLAYERS)
                            ? static_cast<int>(bullet.ownerId)
                            : -1;
                        defeatEnemySlot(e, killerId);
                    }
                    hitEnemy = true;
                    break;
                }
            }
            if (hitEnemy) {
                continue;
            }
        }

        // 6b. Ally Enemy bullets (ownerId == 3) never hit players
        if (bullet.ownerId == 3) {
            continue;
        }

        for (int targetId = 0; targetId < Constants::MAX_PLAYERS; ++targetId) {
            if (bullet.ownerId == static_cast<uint8_t>(targetId)) {
                continue;
            }

            ShipState& targetShip = m_state.ships[targetId];
            if (!targetShip.active) {
                continue;
            }

            // Co-op Friendly-Fire Healing: shooting your friend twice restores +1 HP!
            if (isCoop && bullet.ownerId < Constants::MAX_PLAYERS) {
                if (targetShip.stealthTimer > 0.0f) {
                    continue;
                }
                if (checkCircleOverlap(bullet.x, bullet.y, Constants::BULLET_RADIUS,
                                       targetShip.x, targetShip.y, Constants::SHIP_RADIUS)) {
                    bullet.active = 0;
                    targetShip.allyHealHits += 1;
                    if (targetShip.allyHealHits >= Constants::ALLY_HEAL_SHOTS_REQUIRED) {
                        targetShip.allyHealHits = 0;
                        if (targetShip.hp < targetShip.maxHp) {
                            targetShip.hp += 1;
                        }
                    }
                    break;
                }
                continue;
            }

            // Damage against player (PvP opponent bullet OR Co-op enemy bullet)
            if (targetShip.stealthTimer > 0.0f) {
                continue;
            }

            if (checkCircleOverlap(bullet.x, bullet.y, Constants::BULLET_RADIUS,
                                   targetShip.x, targetShip.y, Constants::SHIP_RADIUS)) {
                bullet.active = 0;

                float knockVx = 0.0f;
                float knockVy = 0.0f;
                const float bSpeed = std::sqrt(bullet.vx * bullet.vx + bullet.vy * bullet.vy);
                if (bSpeed > 0.001f) {
                    knockVx = (bullet.vx / bSpeed) * Constants::BULLET_HIT_KNOCKBACK;
                    knockVy = (bullet.vy / bSpeed) * Constants::BULLET_HIT_KNOCKBACK;
                }

                if (applyDamageToShip(targetId, knockVx, knockVy)) {
                    handlePlayerDefeat(targetId);
                    if (m_state.gameMode == static_cast<uint8_t>(GameMode::PvP)) {
                        return;
                    }
                }
                break;
            }
        }
    }
}
