#pragma once
// =============================================================================
// OWNER: Shared Contract — Developer A (Graphics) & Developer B (Net/Gameplay)
// FILE:  src/shared/Constants.hpp
// DESC:  Compile-time tuning constants for arena dimensions, tick rate,
//        UDP port, ship physics, bullet physics, and combat rules.
// =============================================================================

#include <cstdint>

namespace Constants {

    // -------------------------------------------------------------------------
    // Display & Arena Dimensions (Expanded 1920x1080 Logical Arena)
    // -------------------------------------------------------------------------
    constexpr int   ARENA_WIDTH           = 1920;
    constexpr int   ARENA_HEIGHT          = 1080;
    constexpr float ARENA_WIDTH_F         = 1920.0f;
    constexpr float ARENA_HEIGHT_F        = 1080.0f;
    constexpr int   DEFAULT_WINDOW_WIDTH  = 1280;
    constexpr int   DEFAULT_WINDOW_HEIGHT = 720;
    constexpr float ARENA_BORDER_PADDING  = 24.0f;

    // -------------------------------------------------------------------------
    // Networking & Simulation Timing
    // -------------------------------------------------------------------------
    constexpr int      TICK_RATE         = 60;
    constexpr float    FIXED_TIMESTEP    = 1.0f / 60.0f;
    constexpr uint16_t DEFAULT_PORT      = 7777;
    constexpr int      MAX_PLAYERS       = 2;
    constexpr int      NET_CHANNEL_COUNT = 2;

    // -------------------------------------------------------------------------
    // Ship Physics & Health
    // -------------------------------------------------------------------------
    constexpr int   SHIP_MAX_HP          = 3;
    constexpr float SHIP_SPEED           = 360.0f; // Max velocity (px/sec)
    constexpr float SHIP_THRUST_ACCEL    = 520.0f; // Forward thrust (px/sec^2)
    constexpr float SHIP_REVERSE_ACCEL   = 260.0f; // Reverse thrust (px/sec^2)
    constexpr float SHIP_ROTATION_SPEED  = 3.8f;   // Turn rate (radians/sec)
    constexpr float SHIP_DRAG            = 0.985f; // Per-tick velocity damping
    constexpr float SHIP_RADIUS          = 18.0f;  // Circle collider radius (px)
    constexpr float SHIP_BOUNCE_DAMPING  = 0.70f;  // Screen-edge bounce restitution

    // -------------------------------------------------------------------------
    // Projectile (Bullet) Physics
    // -------------------------------------------------------------------------
    constexpr int   MAX_BULLETS            = 64;
    constexpr float BULLET_SPEED           = 650.0f; // Projectile speed (px/sec)
    constexpr float BULLET_RADIUS          = 5.0f;   // Circle collider radius (px)
    constexpr float BULLET_LIFETIME        = 1.75f;  // Standard despawn time (sec)
    constexpr float BOUNCY_BULLET_LIFETIME = 5.0f;   // Wall-bouncing bullet decay (sec)
    constexpr float SHIP_FIRE_COOLDOWN     = 0.24f;  // Base delay between shots (sec)
    constexpr float BULLET_HIT_KNOCKBACK   = 95.0f;  // Impulse transferred on hit
    constexpr float SHOTGUN_SPREAD_ANGLE   = 0.18f;  // Radians between shotgun pellets

    // -------------------------------------------------------------------------
    // Shooting Energy / Overheat Bar
    // -------------------------------------------------------------------------
    constexpr float AMMO_DRAIN_RATE              = 1.0f / 4.0f; // Empty in 4.0s continuous fire
    constexpr float AMMO_BASE_RECHARGE_RATE      = 1.0f / 4.0f; // 4.0s base full recharge
    constexpr float AMMO_FAST_RECHARGE_MULT      = 1.30f;       // +30% faster if stopped early
    constexpr float AMMO_PUNISH_RECHARGE_MULT    = 0.50f;       // Slower recharge if emptied out
    constexpr float AMMO_OVERHEAT_SMOKE_DURATION = 2.0f;        // Front smoke duration (sec)

    // -------------------------------------------------------------------------
    // Power-Ups (Max 2 on map at once, despawn after 6 seconds)
    // -------------------------------------------------------------------------
    constexpr int   MAX_MAP_POWERUPS           = 2;     // Max simultaneous map pickups
    constexpr float POWERUP_MAP_LIFETIME       = 6.0f;  // Stays on map for 6 seconds
    constexpr float POWERUP_MIN_SPAWN_INTERVAL = 3.5f;  // Min seconds between spawns
    constexpr float POWERUP_MAX_SPAWN_INTERVAL = 6.5f;  // Max seconds between spawns
    constexpr float POWERUP_RADIUS             = 20.0f; // Pickup collider radius (px)

    constexpr float   POWERUP_FIRERATE_DURATION = 10.0f; // +30% fire rate for 10s
    constexpr float   POWERUP_FIRERATE_MULT     = 1.30f;
    constexpr float   POWERUP_SPEED_DURATION    = 10.0f; // +30% ship speed for 10s
    constexpr float   POWERUP_SPEED_MULT        = 1.30f;
    constexpr uint8_t POWERUP_SHOTGUN_SHOTS     = 3;     // Next 3 shots fire 3 bullets
    constexpr float   POWERUP_STEALTH_DURATION  = 6.0f;  // Invisible & immortal 6s (no fire)
    constexpr uint8_t POWERUP_BOUNCY_SHOTS      = 6;     // Next 6 shots bounce off walls (5s)

    // -------------------------------------------------------------------------
    // Round Feedback
    // -------------------------------------------------------------------------
    constexpr float ROUND_BANNER_DURATION = 2.0f;  // Seconds to show round winner banner

    // -------------------------------------------------------------------------
    // Comets (Randomly flying map hazards - capped to keep gameplay fair)
    // -------------------------------------------------------------------------
    constexpr int   MAX_COMETS               = 3;      // Max simultaneous comets
    constexpr float COMET_MIN_SPAWN_INTERVAL = 3.2f;   // Min seconds between spawns
    constexpr float COMET_MAX_SPAWN_INTERVAL = 5.8f;   // Max seconds between spawns
    constexpr float COMET_MIN_SPEED          = 180.0f; // Min speed (px/sec)
    constexpr float COMET_MAX_SPEED          = 300.0f; // Max speed (px/sec)
    constexpr float COMET_MIN_LIFETIME       = 4.5f;   // Min comet duration (sec)
    constexpr float COMET_MAX_LIFETIME       = 8.5f;   // Max comet duration (sec)
    constexpr float COMET_RADIUS             = 22.0f;  // Circle collider radius (px)
    constexpr float COMET_HIT_KNOCKBACK      = 210.0f; // Knockback on ship impact

} // namespace Constants
