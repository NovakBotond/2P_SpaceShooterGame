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
    constexpr int   MAX_BULLETS          = 32;
    constexpr float BULLET_SPEED         = 650.0f; // Projectile speed (px/sec)
    constexpr float BULLET_RADIUS        = 5.0f;   // Circle collider radius (px)
    constexpr float BULLET_LIFETIME      = 1.75f;  // Time before despawn (sec)
    constexpr float SHIP_FIRE_COOLDOWN   = 0.24f;  // Minimum delay between shots (sec)
    constexpr float BULLET_HIT_KNOCKBACK = 95.0f;  // Impulse transferred on hit

    // -------------------------------------------------------------------------
    // Round Feedback
    // -------------------------------------------------------------------------
    constexpr float ROUND_BANNER_DURATION = 2.0f;  // Seconds to show round winner banner

} // namespace Constants
