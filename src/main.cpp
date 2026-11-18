// =============================================================================
// OWNER: Shared Integration — Developer A (Graphics) & Developer B (Net/Game)
// FILE:  src/main.cpp
// DESC:  Application entry point and 60Hz main loop connecting NetworkManager,
//        Simulation, and Renderer with an interactive Host/Join start menu.
// =============================================================================

#include "game/Simulation.hpp"
#include "graphics/Renderer.hpp"
#include "network/NetworkManager.hpp"
#include "shared/Constants.hpp"
#include "shared/Protocol.hpp"

#include <raylib.h>
#include <algorithm>
#include <cstdint>
#include <string>

enum class AppMode {
    Menu,
    InGame
};

namespace {
    // Gather local player keyboard inputs (supports both W/A/S/D and Arrow Keys)
    InputPacket sampleLocalInput(uint8_t playerId, uint32_t tick) {
        InputPacket input{};
        input.packetType     = static_cast<uint8_t>(PacketType::INPUT);
        input.playerId       = playerId;
        input.tick           = tick;
        input.thrustForward  = (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    ? 1 : 0;
        input.thrustBackward = (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  ? 1 : 0;
        input.rotateLeft     = (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  ? 1 : 0;
        input.rotateRight    = (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) ? 1 : 0;
        input.shoot          = IsKeyDown(KEY_SPACE)                       ? 1 : 0;
        return input;
    }
}

int main(int argc, char* argv[]) {
    Renderer       renderer;
    NetworkManager network;
    Simulation     simulation;

    if (!renderer.initialize()) {
        return 1;
    }

    AppMode     mode       = AppMode::Menu;
    std::string ipInput    = "127.0.0.1";
    bool        editingIp  = false;
    std::string menuStatus = "Press [1] to Host, [2] to Join, or [BACKSPACE] to edit IP.";
    uint32_t    clientTick = 0;

    // Optional CLI arguments for quick dual-instance launching:
    //   NeonSpaceArena --host
    //   NeonSpaceArena --join 127.0.0.1
    if (argc >= 2) {
        const std::string arg1 = argv[1];
        if (arg1 == "--host") {
            if (network.startHost(Constants::DEFAULT_PORT)) {
                simulation.resetMatch();
                mode = AppMode::InGame;
            }
        } else if (arg1 == "--join") {
            if (argc >= 3) {
                ipInput = argv[2];
            }
            if (network.connectToHost(ipInput, Constants::DEFAULT_PORT)) {
                simulation.resetMatch();
                mode = AppMode::InGame;
            }
        }
    }

    while (!WindowShouldClose()) {
        const float dt = std::min(GetFrameTime(), 0.1f);

        // =====================================================================
        // STATE 1: START MENU (Host / Join via IP)
        // =====================================================================
        if (mode == AppMode::Menu) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                if (editingIp) {
                    editingIp = false;
                    menuStatus = "IP ready (" + ipInput + "). Press [1] Host or [2]/[ENTER] Join.";
                } else {
                    break; // Exit application from main menu
                }
            }

            if (IsKeyPressed(KEY_TAB)) {
                editingIp = !editingIp;
                menuStatus = editingIp
                    ? "Editing IP: type address and press [ENTER] to Join."
                    : "Press [1] to Host, [2] to Join, or [BACKSPACE] to edit IP.";
            }

            // Handle Backspace (also enters IP editing mode so digits 1 & 2 can be typed)
            if (IsKeyPressed(KEY_BACKSPACE)) {
                editingIp = true;
                if (!ipInput.empty()) {
                    ipInput.pop_back();
                }
                menuStatus = "Editing IP: type address and press [ENTER] to Join.";
            }

            if (!editingIp) {
                // Quick-launch hotkeys when not actively typing an IP
                if (IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_KP_1)) {
                    if (network.startHost(Constants::DEFAULT_PORT)) {
                        simulation.resetMatch();
                        mode = AppMode::InGame;
                        continue;
                    } else {
                        menuStatus = network.getStatusMessage();
                    }
                } else if (IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_KP_2) || IsKeyPressed(KEY_ENTER)) {
                    const std::string targetIp = ipInput.empty() ? "127.0.0.1" : ipInput;
                    if (network.connectToHost(targetIp, Constants::DEFAULT_PORT)) {
                        simulation.resetMatch();
                        mode = AppMode::InGame;
                        continue;
                    } else {
                        menuStatus = network.getStatusMessage();
                    }
                }

                // If user starts typing digits (other than 1/2) or '.', automatically enter IP edit mode
                int key = GetCharPressed();
                while (key > 0) {
                    if ((key >= '0' && key <= '9' && key != '1' && key != '2') || key == '.') {
                        editingIp = true;
                        if (ipInput.size() < 32) {
                            ipInput.push_back(static_cast<char>(key));
                        }
                        menuStatus = "Editing IP: type address and press [ENTER] to Join.";
                    }
                    key = GetCharPressed();
                }
            } else {
                // Full IP editing mode: digits 0-9 (including 1 and 2) and '.' are appended to IP
                int key = GetCharPressed();
                while (key > 0) {
                    if (((key >= '0' && key <= '9') || key == '.') && ipInput.size() < 32) {
                        ipInput.push_back(static_cast<char>(key));
                    }
                    key = GetCharPressed();
                }

                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
                    editingIp = false;
                    const std::string targetIp = ipInput.empty() ? "127.0.0.1" : ipInput;
                    if (network.connectToHost(targetIp, Constants::DEFAULT_PORT)) {
                        simulation.resetMatch();
                        mode = AppMode::InGame;
                        continue;
                    } else {
                        menuStatus = network.getStatusMessage();
                    }
                }
            }

            renderer.renderMenu(ipInput, menuStatus);
        }
        // =====================================================================
        // STATE 2: ACTIVE 1v1 MULTIPLAYER MATCH
        // =====================================================================
        else if (mode == AppMode::InGame) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                network.shutdown();
                mode = AppMode::Menu;
                editingIp = false;
                menuStatus = "Disconnected. Press [1] to Host or [2] to Join.";
                continue;
            }

            network.pollEvents();

            if (network.getRole() == NetworkRole::Host) {
                // Host (Player 0): Gather local input + remote client input, step authoritative physics
                const InputPacket hostInput   = sampleLocalInput(0, simulation.getState().tick);
                const InputPacket clientInput = network.getLatestClientInput();

                simulation.update(dt, hostInput, clientInput);
                network.broadcastGameState(simulation.getState());

                renderer.renderGame(
                    simulation.getState(),
                    true,
                    network.isConnected(),
                    network.getStatusMessage(),
                    dt);
            } else if (network.getRole() == NetworkRole::Client) {
                // Client (Player 1): Send local inputs to Host and render latest authoritative GameState
                clientTick++;
                const InputPacket clientInput = sampleLocalInput(1, clientTick);
                network.sendInput(clientInput);

                const GameState& renderState = network.hasReceivedState()
                    ? network.getLatestGameState()
                    : simulation.getState();

                renderer.renderGame(
                    renderState,
                    false,
                    network.isConnected(),
                    network.getStatusMessage(),
                    dt);
            }
        }
    }

    network.shutdown();
    renderer.shutdown();
    return 0;
}
