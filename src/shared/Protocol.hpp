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

enum class PowerUpType : uint8_t {
    FireRateBoost   = 1, // 1. Fire rate increased by 30% (for 10 sec)
    SpeedBoost      = 2, // 2. Ship speed increased by 30% (for 10 sec)
    Shotgun         = 3, // 3. Next 3 shots are shotgun shots (3 bullets)
    StealthImmortal = 4, // 4. Invisible & immortal for 6 sec (cannot fire)
    BouncyBullets   = 5, // 5. Next 6 shots bounce off walls until 5s decay
    ExtraLife       = 6  // 6. Restore +1 HP (capped at 3 max HP; still consumable at 3 HP)
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
    float   x                = 0.0f;
    float   y                = 0.0f;
    float   vx               = 0.0f;
    float   vy               = 0.0f;
    float   angle            = 0.0f; // Facing angle in radians
    int32_t hp               = Constants::SHIP_MAX_HP;
    int32_t score            = 0;
    float   fireRateTimer    = 0.0f; // > 0: +30% fire rate
    float   speedBoostTimer  = 0.0f; // > 0: +30% max speed & thrust
    float   stealthTimer     = 0.0f; // > 0: invisible, immortal, cannot fire
    uint8_t shotgunShotsLeft = 0;    // Remaining 3-bullet spread shots
    uint8_t bouncyShotsLeft  = 0;    // Remaining 5s wall-bouncing shots
    uint8_t isThrusting      = 0;
    uint8_t active           = 1;
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
    uint8_t ownerId        = 0; // 0 = Host (Cyan), 1 = Client (Magenta)
    uint8_t bouncesOnWalls = 0; // 1 = Bounces off arena walls for 5 seconds
    uint8_t active         = 0;
};

// -----------------------------------------------------------------------------
// Authoritative Per-Comet State (Randomly flying map hazards)
// -----------------------------------------------------------------------------
struct CometState {
    float   x        = 0.0f;
    float   y        = 0.0f;
    float   vx       = 0.0f;
    float   vy       = 0.0f;
    float   radius   = Constants::COMET_RADIUS;
    float   lifetime = 0.0f;
    float   maxLife  = 1.0f;
    uint8_t active   = 0;
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
    uint8_t      packetType       = static_cast<uint8_t>(PacketType::GAME_STATE);
    uint32_t     tick             = 0;
    uint32_t     lastClientTick   = 0;    // Latest Client InputPacket::tick processed by Host
    ShipState    ships[Constants::MAX_PLAYERS]{};
    BulletState  bullets[Constants::MAX_BULLETS]{};
    CometState   comets[Constants::MAX_COMETS]{};
    PowerUpState powerUps[Constants::MAX_MAP_POWERUPS]{};
    uint8_t      lastRoundWinner  = 255;  // 0 = P1, 1 = P2, 255 = None yet
    float        roundBannerTimer = 0.0f; // > 0 displays round winner banner
};

#pragma pack(pop)

static_assert(std::is_trivially_copyable<InputPacket>::value,
              "InputPacket must remain trivially copyable for UDP serialization.");
static_assert(std::is_trivially_copyable<GameState>::value,
              "GameState must remain trivially copyable for UDP serialization.");
