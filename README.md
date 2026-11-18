# 1v1 Neon Space Arena

A fast-paced, server-authoritative 2D multiplayer space shooter built from scratch in **C++17** using **Raylib** (OpenGL 3.3 + GLSL 330 shaders) and **ENet** (low-latency UDP networking).

---

## ⚡ Quick Start (Play Immediately Without Building)

A pre-packaged, standalone Windows executable is included in the **`bin/`** directory (statically linked with zero DLL prerequisites):

1. Clone or download the repository ZIP.
2. Open the **`bin`** folder.
3. Double-click **`NeonSpaceArena.exe`** to play right away!

*(Linux & Windows builds are also automatically generated on every push via GitHub Actions under the **Actions** tab).*

---

## Team Architecture & Ownership Split

The codebase is modularized so two developers can work simultaneously in separate directories without merge conflicts:

| Module / File | Owner | Responsibilities |
| :--- | :--- | :--- |
| `src/shared/Constants.hpp` | **Shared Contract** | Arena dimensions (`1920x1080`), 60Hz tick rate, UDP port `7777`, ship/bullet/comet/power-up constants, 3 max HP. |
| `src/shared/Protocol.hpp` | **Shared Contract** | Byte-packed UDP network packets (`InputPacket`, `ShipState`, `BulletState`, `CometState`, `PowerUpState`, `GameState`). |
| `src/graphics/Renderer.hpp` / `.cpp` | **Developer A (Graphics & Shaders)** | Customizable resolution & Fullscreen, shader uniforms, vector ships, low-HP red vibration flash, comets, power-up badges, thrust particles, HUD health bars, and `RenderTexture2D` bloom pass. |
| `assets/shaders/background.fs` | **Developer A (Graphics & Shaders)** | GLSL 330 procedural parallax scrolling starfield, subtle dual-tone FBM nebula, and cyber grid (`uTime`, `uResolution`). |
| `assets/shaders/postprocess.fs` | **Developer A (Graphics & Shaders)** | GLSL 330 multi-sample neon glow/bloom, subtle CRT scanlines, radial chromatic aberration, and vignette. |
| `src/network/NetworkManager.hpp` / `.cpp` | **Developer B (Networking & Gameplay)** | ENet Host & Client UDP lifecycle on port `7777`, unsequenced low-latency packet streaming, and compact state broadcasting. |
| `src/game/Simulation.hpp` / `.cpp` | **Developer B (Networking & Gameplay)** | Server-authoritative 2D physics: ship rotation/thrust, comets, 6 random power-ups, circle collisions, 3 HP tracking, round resets, and client prediction helpers. |
| `src/main.cpp` | **Shared Integration** | Start menu (Host / Join via IP + Resolution / Fullscreen selector) and fixed-60Hz loop with Client-Side Prediction & Reconciliation. |

---

## Building the Project (Windows & Linux)

The build uses **CMake `FetchContent`** to automatically download, compile, and statically link **Raylib 5.5** and **ENet 1.3.18**—no manual library installation is required.

### Prerequisites
- **CMake** 3.16 or newer
- **C++17 Compiler** (GCC, Clang, MinGW-w64, or MSVC)
- **Git** (used by CMake `FetchContent` to pull Raylib and ENet)
- On Linux: standard X11/OpenGL development headers (`libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev`)

### Configure & Compile

```bash
# 1. Configure CMake (downloads Raylib and ENet automatically on first run)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 2. Compile the executable (and automatically copy assets/ to the build directory)
cmake --build build --config Release -j
```

---

## Running & Testing Multiplayer

### 1. Local Testing on 1 PC (Two Window Instances)

Open two terminal windows in the project root (or launch the executable twice):

**Instance 1 (Host - Player 1 Cyan):**
1. Run `./build/NeonSpaceArena` (Linux) or `.\build\NeonSpaceArena.exe` (Windows).
2. Press **`1`** on the start menu to start hosting on UDP port `7777`.
   *(Tip: You can also launch directly via CLI flag: `NeonSpaceArena --host`)*

**Instance 2 (Client - Player 2 Magenta):**
1. Run a second instance of `NeonSpaceArena`.
2. Leave the IP box at its default `127.0.0.1` and press **`2`** (or **`ENTER`**) to connect.
   *(Tip: You can also launch directly via CLI flag: `NeonSpaceArena --join 127.0.0.1`)*

### 2. Two-PC LAN Multiplayer

1. On **PC 1 (Host)**, find your local LAN IP address (`ipconfig` on Windows or `ip a` on Linux, e.g., `192.168.1.42`).
2. Launch `NeonSpaceArena` on **PC 1** and press **`1`** to Host on UDP port `7777` (allow UDP port `7777` through your firewall if prompted).
3. Launch `NeonSpaceArena` on **PC 2 (Client)**, press **`BACKSPACE`** (or **`TAB`**) to edit the IP text box, type the Host's LAN IP (e.g., `192.168.1.42`), and press **`ENTER`** to join.

---

## Controls, Power-Ups & Arena Hazards

### Controls
- **Switch Game Mode (Menu):** `M` or `Up Arrow` / `Down Arrow` (`1v1 PvP Duel` vs `2P Co-op Waves`)
- **Thrust Forward / Reverse:** `W` / `S` or `Up Arrow` / `Down Arrow`
- **Rotate Left / Right:** `A` / `D` or `Left Arrow` / `Right Arrow`
- **Fire Projectile:** `SPACE`
- **Change Resolution (Menu):** `R` or `Left Arrow` / `Right Arrow` (`1280x720`, `1600x900`, `1920x1080`, `2560x1440`)
- **Toggle Fullscreen / Windowed:** `F11` (anytime) or `F` (on start menu)
- **Disconnect / Return to Menu:** `ESC`

### Game Modes, Waves, Hazards & Shooting Energy
- **5-Second Intro Splash:** Launches with a 5-second black loading screen fading `"NOVAK AND ERIK PRODUCTIONS"` in and out.
- **1v1 PvP Duel vs. 2-Player Co-op Waves:** Choose on the main menu to duel head-to-head or team up with a friend against escalating enemy waves!
- **Co-op Wave Progression & Bosses:**
  - Enemies spawn randomly from all 4 screen edges; each wave increases total enemies.
  - Every **2nd wave**, normal enemy health increases by **+1 HP line**.
  - Every **5th wave**, enemy movement speed increases by **+10%**.
  - When **2–3 enemies remain** at the end of a wave, a larger **Wave Boss** spawns with **2× health** and **2× fire rate**.
  - **Friendly-Fire Teammate Healing (Co-op):** Shooting your teammate **twice** restores **+1 HP line** to help keep the mission going!
- **Expanded 1920x1080 Arena:** Full HD virtual coordinate space scaled cleanly to any windowed or fullscreen display.
- **Shooting Bar & Overheat:** Displayed beneath each player's health bar. Empties after **4 seconds** of continuous firing. Releasing fire early recharges **30% faster**; emptying it completely causes the front of the ship to **smoke for 2 seconds** (cannot fire) and applies a **slower recharge penalty**.
- **Destructible Comets:** Fiery comets periodically traverse the arena along random vectors (capped at max 3 simultaneously). Shooting a comet **3 times** destroys it; colliding with one deals 1 HP damage.
- **Low-Health Warning:** At `1 HP`, a player's ship vibrates rapidly and flashes in neon crimson red.

### Power-Ups (Max 2 on Map, 6-Second Map Lifetime, Short Name + Symbol Above Ship)
1. **`* FR+` — Fire Rate Boost:** Increases firing rate by **+30%** for **10 seconds**.
2. **`> SPD` — Ship Speed Boost:** Increases max ship speed & thrust by **+30%** for **10 seconds**.
3. **`# SGN` — Shotgun:** The next **3 shots** fire a 3-bullet spread fan.
4. **`@ INV` — Stealth & Immortal:** Ship becomes **invisible** and **immortal** for **6 seconds**, but **cannot fire** while cloaked.
5. **`~ BNC` — Bouncy Bullets:** In the next **6 shots**, bullets ricochet off arena walls until they decay after **5 seconds**.
6. **`+ +HP` — Extra Life:** Restores **+1 HP** (if already at `3 HP`, it can still be picked up without exceeding `3 HP`).
7. **`= AMO` — Instant Full Ammo:** Immediately refills the shooting bar to **100%** and clears any overheat smoke or recharge penalty.
8. **`+ 360` — 45° Radial Burst:** Allows firing **1 omnidirectional burst** (8 bullets every **45°**) that bounce and remain for **5 seconds**.
9. **`(O) SHD` — Blue Bubble Shield:** Grants a blue bubble with **2 shield health lines**; after absorbing 2 hits it disappears and cannot be picked up again for **30 seconds**.
10. **`v SLW` — Enemy Speed Down (Waves):** Decreases enemy movement speed by **30%** for **10 seconds**.
11. **`X KIL` — Random Enemy Smite (Waves):** Instantly kills **1 random active enemy**.
12. **`! EFR` — Enemy Fire Rate Down (Waves):** Decreases enemy firing rate by **30%** for **10 seconds**.
13. **`& ALY` — Mind-Control Ally (Waves):** Converts **1 enemy** into an ally that shoots hostile enemies for **5 seconds**.
