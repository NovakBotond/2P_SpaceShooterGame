# 1v1 Neon Space Arena

A fast-paced, server-authoritative 2D multiplayer space shooter built from scratch in **C++17** using **Raylib** (OpenGL 3.3 + GLSL 330 shaders) and **ENet** (low-latency UDP networking).

---

## Team Architecture & Ownership Split

The codebase is modularized so two developers can work simultaneously in separate directories without merge conflicts:

| Module / File | Owner | Responsibilities |
| :--- | :--- | :--- |
| `src/shared/Constants.hpp` | **Shared Contract** | Arena dimensions (`1280x720`), 60Hz tick rate, UDP port `7777`, ship/bullet speeds, 3 max HP. |
| `src/shared/Protocol.hpp` | **Shared Contract** | Byte-packed UDP network packets (`InputPacket`, `ShipState`, `BulletState`, `GameState`). |
| `src/graphics/Renderer.hpp` / `.cpp` | **Developer A (Graphics & Shaders)** | Raylib window setup, shader uniforms, vector-style neon ships, engine thrust particles, HUD health bars, and `RenderTexture2D` post-processing pipeline. |
| `assets/shaders/background.fs` | **Developer A (Graphics & Shaders)** | GLSL 330 procedural parallax scrolling starfield, subtle dual-tone FBM nebula, and cyber grid (`uTime`, `uResolution`). |
| `assets/shaders/postprocess.fs` | **Developer A (Graphics & Shaders)** | GLSL 330 multi-sample neon glow/bloom, subtle CRT scanlines, radial chromatic aberration, and vignette. |
| `src/network/NetworkManager.hpp` / `.cpp` | **Developer B (Networking & Gameplay)** | ENet Host & Client UDP lifecycle on port `7777`, non-blocking packet polling, input streaming, and state broadcasting. |
| `src/game/Simulation.hpp` / `.cpp` | **Developer B (Networking & Gameplay)** | Server-authoritative 2D physics: ship rotation/thrust, screen-edge bounce/clamping, projectile spawning, circle collisions, 3 HP tracking, and round resets. |
| `src/main.cpp` | **Shared Integration** | Start menu (Host / Join via IP) and 60Hz main loop bridging `NetworkManager`, `Simulation`, and `Renderer`. |

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

## Controls & Rules

- **Thrust Forward / Reverse:** `W` / `S` or `Up Arrow` / `Down Arrow`
- **Rotate Left / Right:** `A` / `D` or `Left Arrow` / `Right Arrow`
- **Fire Projectile:** `SPACE`
- **Disconnect / Return to Menu:** `ESC`
- **Match Rules:** Each ship starts a round with **3 HP**. Hitting the opponent 3 times scores **1 point** for the attacker and automatically resets both ships and restores full HP for the next round.
