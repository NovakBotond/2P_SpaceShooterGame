#pragma once
// =============================================================================
// OWNER: Shared Contract — Developer A (Graphics) & Developer B (Net/Gameplay)
// FILE:  src/shared/Protocol.hpp
// DESC:  Trivially-copyable, byte-packed network protocol packets and
//        authoritative game state structs (InputPacket, ShipState,
//        BulletState, and GameState).
// =============================================================================

#include "shared/Constants.hpp"
#include <cstdint>
#include <type_traits>

enum class PacketType : uint8_t {
    INPUT      = 1,
    GAME_STATE = 2
};

enum class GameMode : uint8_t {
    PvP       = 0, // 1v1 against human opponent
    CoopWaves = 1  // 2-player co-op fighting waves of enemies
};

enum class PowerUpType : uint8_t {
    FireRateBoost   = 1,  // 1. Fire rate increased by 30% (10s)
    SpeedBoost      = 2,  // 2. Ship speed increased by 30% (10s)
    Shotgun         = 3,  // 3. Next 3 shots are shotgun shots (3 bullets)
    StealthImmortal = 4,  // 4. Invisible & immortal for 6s (cannot fire)
    BouncyBullets   = 5,  // 5. Next 6 shots bounce off walls until 5s decay
    ExtraLife       = 6,  // 6. Restore +1 HP (capped at 3 HP; still consumable at 3 HP)
    FullAmmo        = 7,  // 7. Instant full shooting bar & clears overheat
    RadialBurst     = 8,  // 8. Next shot fires 8 bouncy 5s bullets every 45 degrees
    ShieldBubble    = 9,  // 9. Blue bubble (+2 shield HP); 30s lockout after breaking
    EnemySlow       = 10, // 10. Decreases enemy speed by 30% for 10s (Waves)
    EnemyKillRandom = 11, // 11. Kills 1 enemy randomly (Waves)
    EnemyFireSlow   = 12, // 12. Decreases enemy fire rate by 30% for 10s (Waves)
    EnemyAlly       = 13  // 13. Converts 1 enemy into an ally for 5s (Waves)
};

#pragma pack(push, 1)

// -----------------------------------------------------------------------------
// Client -> Host Input Packet (Sent every frame by the joining player)
// -----------------------------------------------------------------------------
struct InputPacket {
    uint8_t  packetType     = static_cast<uint8_t>(PacketType::INPUT);
    uint8_t  playerId       = 0;
    uint32_t tick           = 0;
    uint8_t  thrustForward  = 0; // W or Up Arrow
    uint8_t  thrustBackward = 0; // S or Down Arrow
    uint8_t  rotateLeft     = 0; // A or Left Arrow
    uint8_t  rotateRight    = 0; // D or Right Arrow
    uint8_t  shoot          = 0; // Spacebar
};

// -----------------------------------------------------------------------------
// Authoritative Per-Ship State
// -----------------------------------------------------------------------------
struct ShipState {
    float   x                   = 0.0f;
    float   y                   = 0.0f;
    float   vx                  = 0.0f;
    float   vy                  = 0.0f;
    float   angle               = 0.0f; // Facing angle in radians
    int32_t hp                  = Constants::SHIP_MAX_HP;
    int32_t score               = 0;
    float   ammo                = 1.0f; // 1.0 = full (4s fire), 0.0 = empty
    float   overheatSmokeTimer  = 0.0f; // > 0: front cannon smokes for 2s & jammed
    float   fireRateTimer       = 0.0f; // > 0: +30% fire rate
    float   speedBoostTimer     = 0.0f; // > 0: +30% max speed & thrust
    float   stealthTimer        = 0.0f; // > 0: invisible, immortal, cannot fire
    float   shieldCooldownTimer = 0.0f; // > 0: 30s lockout after blue shield breaks
    int8_t  shieldHp            = 0;    // Blue bubble absorbs up to 2 hits
    uint8_t shotgunShotsLeft    = 0;    // Remaining 3-bullet spread shots
    uint8_t bouncyShotsLeft     = 0;    // Remaining 5s wall-bouncing shots
    uint8_t radialShotsLeft     = 0;    // Remaining 45-degree 360-burst shots
    uint8_t allyHealHits        = 0;    // Co-op: 2 friendly hits restore +1 HP
    uint8_t ammoOverheated      = 0;    // 1 = emptied out -> slower recharge punishment
    uint8_t isThrusting         = 0;
    uint8_t active              = 1;
};

// -----------------------------------------------------------------------------
// Authoritative Per-Bullet State
// -----------------------------------------------------------------------------
struct BulletState {
    float   x              = 0.0f;
    float   y              = 0.0f;
    float   vx             = 0.0f;
    float   vy             = 0.0f;
    float   lifetime       = 0.0f;
    uint8_t ownerId        = 0; // 0 = Host, 1 = Client, 2 = Hostile Enemy, 3 = Ally Enemy
    uint8_t bouncesOnWalls = 0; // 1 = Bounces off arena walls for 5 seconds
    uint8_t active         = 0;
};

// -----------------------------------------------------------------------------
// Authoritative Per-Comet State (Randomly flying map hazards, 3 HP)
// -----------------------------------------------------------------------------
struct CometState {
    float   x        = 0.0f;
    float   y        = 0.0f;
    float   vx       = 0.0f;
    float   vy       = 0.0f;
    float   radius   = Constants::COMET_RADIUS;
    float   lifetime = 0.0f;
    float   maxLife  = 1.0f;
    int8_t  hp       = Constants::COMET_MAX_HP;
    uint8_t active   = 0;
};

// -----------------------------------------------------------------------------
// Authoritative Co-op Enemy / Boss State
// -----------------------------------------------------------------------------
struct EnemyState {
    float   x         = 0.0f;
    float   y         = 0.0f;
    float   vx        = 0.0f;
    float   vy        = 0.0f;
    float   angle     = 0.0f;
    float   allyTimer = 0.0f; // > 0: fights hostile enemies for 5 seconds
    int16_t hp        = Constants::ENEMY_BASE_HP;
    int16_t maxHp     = Constants::ENEMY_BASE_HP;
    uint8_t isBoss    = 0;
    uint8_t active    = 0;
};

// -----------------------------------------------------------------------------
// Authoritative Map Power-Up Pickup State (Max 2 active, 6s map lifetime)
// -----------------------------------------------------------------------------
struct PowerUpState {
    float   x        = 0.0f;
    float   y        = 0.0f;
    float   lifetime = 0.0f; // Counts down from 6.0s to 0.0s
    uint8_t type     = static_cast<uint8_t>(PowerUpType::FireRateBoost);
    uint8_t active   = 0;
};

// -----------------------------------------------------------------------------
// Host -> Client Full Authoritative Game State Snapshot
// -----------------------------------------------------------------------------
struct GameState {
    uint8_t      packetType         = static_cast<uint8_t>(PacketType::GAME_STATE);
    uint32_t     tick               = 0;
    uint32_t     lastClientTick     = 0;    // Latest Client InputPacket::tick processed by Host
    uint8_t      gameMode           = static_cast<uint8_t>(GameMode::PvP);
    ShipState    ships[Constants::MAX_PLAYERS]{};
    BulletState  bullets[Constants::MAX_BULLETS]{};
    CometState   comets[Constants::MAX_COMETS]{};
    PowerUpState powerUps[Constants::MAX_MAP_POWERUPS]{};
    EnemyState   enemies[Constants::MAX_ENEMIES]{};
    uint16_t     currentWave        = 1;
    uint16_t     waveRemainingKills = 0;    // Remaining enemies to kill to clear wave
    float        enemySlowTimer     = 0.0f; // > 0: enemies move 30% slower
    float        enemyFireSlowTimer = 0.0f; // > 0: enemies shoot 30% slower
    uint8_t      bossActive         = 0;    // 1 when wave boss is alive
    uint8_t      lastRoundWinner    = 255;  // 0 = P1, 1 = P2, 255 = None yet
    float        roundBannerTimer   = 0.0f; // > 0 displays round/wave banner
    uint8_t      bannerCode         = 0;    // 0=PvP score, 1=Wave start, 2=Boss, 3=Wave clear, 4=Mission fail
};

#pragma pack(pop)

static_assert(std::is_trivially_copyable<InputPacket>::value,
              "InputPacket must remain trivially copyable for UDP serialization.");
static_assert(std::is_trivially_copyable<GameState>::value,
              "GameState must remain trivially copyable for UDP serialization.");
