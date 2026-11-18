// =============================================================================
// OWNER: Shared Integration — Developer A (Graphics) & Developer B (Net/Game)
// FILE:  src/main.cpp
// DESC:  Application entry point with 5-second studio splash screen, Main Menu
//        (1v1 PvP vs 2P Co-op Waves + Display/Resolution settings), and fixed
//        60Hz simulation loop with Client-Side Prediction & Reconciliation.
// =============================================================================

#include "game/Simulation.hpp"
#include "graphics/Renderer.hpp"
#include "network/NetworkManager.hpp"
#include "shared/Constants.hpp"
#include "shared/Protocol.hpp"

#include <raylib.h>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>

enum class AppMode {
    Intro,
    Menu,
    InGame
};

namespace {
    // Gather local player keyboard inputs (supports both W/A/S/D and Arrow Keys)
    InputPacket sampleLocalInput(uint8_t playerId, uint32_t tick, bool shootLatch) {
        InputPacket input{};
        input.packetType     = static_cast<uint8_t>(PacketType::INPUT);
        input.playerId       = playerId;
        input.tick           = tick;
        input.thrustForward  = (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    ? 1 : 0;
        input.thrustBackward = (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  ? 1 : 0;
        input.rotateLeft     = (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  ? 1 : 0;
        input.rotateRight    = (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) ? 1 : 0;
        input.shoot          = (IsKeyDown(KEY_SPACE) || shootLatch)       ? 1 : 0;
        return input;
    }

    struct ClientPredictionState {
        bool                    initialized        = false;
        uint32_t                lastReconciledTick = 0;
        uint32_t                clientTick         = 0;
        float                   timeSinceSnapshot  = 0.0f;
        std::deque<InputPacket> pendingInputs;
        GameState               prevVisualState{};
        GameState               visualState{};
        ShipState               prevPredictedShip{};
        ShipState               predictedShip{};

        void reset(const GameState& baseState) {
            initialized        = false;
            lastReconciledTick = 0;
            clientTick         = 0;
            timeSinceSnapshot  = 0.0f;
            pendingInputs.clear();
            prevVisualState    = baseState;
            visualState        = baseState;
            prevPredictedShip  = baseState.ships[1];
            predictedShip      = baseState.ships[1];
        }

        void reconcileWithServerState(const GameState& serverState) {
            const bool isMatchRestart = initialized &&
                                        (lastReconciledTick > serverState.tick) &&
                                        ((lastReconciledTick - serverState.tick) > 300);

            if (!initialized || isMatchRestart) {
                initialized        = true;
                lastReconciledTick = serverState.tick;
                timeSinceSnapshot  = 0.0f;
                visualState        = serverState;
                prevVisualState    = serverState;
                predictedShip      = serverState.ships[1];
                prevPredictedShip  = serverState.ships[1];

                while (!pendingInputs.empty() && pendingInputs.front().tick <= serverState.lastClientTick) {
                    pendingInputs.pop_front();
                }
                return;
            }

            if (serverState.tick == lastReconciledTick) {
                return;
            }

            lastReconciledTick = serverState.tick;
            timeSinceSnapshot  = 0.0f;

            // Discard inputs already acknowledged by the Host
            while (!pendingInputs.empty() && pendingInputs.front().tick <= serverState.lastClientTick) {
                pendingInputs.pop_front();
            }

            // Re-simulate unacknowledged client inputs atop authoritative server state
            ShipState reconciled = serverState.ships[1];
            for (const InputPacket& pending : pendingInputs) {
                Simulation::stepShipPhysics(reconciled, pending, Constants::FIXED_TIMESTEP);
            }

            const float dx = reconciled.x - predictedShip.x;
            const float dy = reconciled.y - predictedShip.y;
            const float distSq = dx * dx + dy * dy;

            const bool isPvp = (serverState.gameMode == static_cast<uint8_t>(GameMode::PvP));
            const bool snapTeleport = (distSq > 70.0f * 70.0f) ||
                                      (reconciled.active != predictedShip.active) ||
                                      (isPvp && (serverState.ships[0].score != visualState.ships[0].score ||
                                                 serverState.ships[1].score != visualState.ships[1].score));

            if (snapTeleport) {
                predictedShip     = reconciled;
                prevPredictedShip = reconciled;
            } else {
                const float oldX        = predictedShip.x;
                const float oldY        = predictedShip.y;
                const float oldVx       = predictedShip.vx;
                const float oldVy       = predictedShip.vy;
                const float oldAngle    = predictedShip.angle;
                const uint8_t oldThrust = predictedShip.isThrusting;

                predictedShip = reconciled;

                if (distSq <= 0.25f) {
                    predictedShip.x           = oldX;
                    predictedShip.y           = oldY;
                    predictedShip.vx          = oldVx;
                    predictedShip.vy          = oldVy;
                    predictedShip.angle       = oldAngle;
                    predictedShip.isThrusting = oldThrust;
                } else {
                    predictedShip.x     = oldX + (reconciled.x - oldX) * 0.30f;
                    predictedShip.y     = oldY + (reconciled.y - oldY) * 0.30f;
                    predictedShip.vx    = oldVx + (reconciled.vx - oldVx) * 0.35f;
                    predictedShip.vy    = oldVy + (reconciled.vy - oldVy) * 0.35f;
                    predictedShip.angle = oldAngle + Simulation::shortestAngleDiff(oldAngle, reconciled.angle) * 0.35f;
                }
            }

            // Update remote Host ship, enemies, comets & bullets with smooth blending
            const ShipState prevHostShip = visualState.ships[0];
            visualState = serverState;

            if (!snapTeleport && prevHostShip.active && serverState.ships[0].active) {
                const float hdx = serverState.ships[0].x - prevHostShip.x;
                const float hdy = serverState.ships[0].y - prevHostShip.y;
                if (hdx * hdx + hdy * hdy < 70.0f * 70.0f) {
                    visualState.ships[0].x     = prevHostShip.x + hdx * 0.55f;
                    visualState.ships[0].y     = prevHostShip.y + hdy * 0.55f;
                    visualState.ships[0].angle = prevHostShip.angle +
                        Simulation::shortestAngleDiff(prevHostShip.angle, serverState.ships[0].angle) * 0.55f;
                }
            } else if (snapTeleport) {
                prevVisualState = visualState;
            }
        }
    };
}

int main(int argc, char* argv[]) {
    Renderer       renderer;
    NetworkManager network;
    Simulation     simulation;

    if (!renderer.initialize()) {
        return 1;
    }

    AppMode               mode          = AppMode::Intro;
    float                 introTimer    = 0.0f;
    GameMode              selectedMode  = GameMode::PvP;
    std::string           ipInput       = "127.0.0.1";
    bool                  editingIp     = false;
    std::string           menuStatus    = "Press [M] to switch Mode, [1] to Host, [2] to Join.";
    float                 accumulator   = 0.0f;
    bool                  shootLatch    = false;
    GameState             prevHostState = simulation.getState();
    ClientPredictionState clientPred;
    clientPred.reset(simulation.getState());

    auto enterGameSession = [&](GameMode matchMode) {
        simulation.resetMatch(matchMode);
        prevHostState = simulation.getState();
        clientPred.reset(simulation.getState());
        accumulator = 0.0f;
        shootLatch  = false;
        mode        = AppMode::InGame;
    };

    // Optional CLI arguments for quick dual-instance launching:
    //   NeonSpaceArena --host
    //   NeonSpaceArena --host-coop
    //   NeonSpaceArena --join 127.0.0.1
    if (argc >= 2) {
        const std::string arg1 = argv[1];
        if (arg1 == "--host") {
            if (network.startHost(Constants::DEFAULT_PORT)) {
                enterGameSession(GameMode::PvP);
            }
        } else if (arg1 == "--host-coop") {
            selectedMode = GameMode::CoopWaves;
            if (network.startHost(Constants::DEFAULT_PORT)) {
                enterGameSession(GameMode::CoopWaves);
            }
        } else if (arg1 == "--join") {
            if (argc >= 3) {
                ipInput = argv[2];
            }
            if (network.connectToHost(ipInput, Constants::DEFAULT_PORT)) {
                enterGameSession(GameMode::PvP);
            }
        }
    }

    while (!WindowShouldClose()) {
        const float dt = std::min(GetFrameTime(), Constants::FIXED_TIMESTEP * 4.0f);

        // Global Fullscreen toggle key [F11] works across Intro, Menu, and In-Game
        if (IsKeyPressed(KEY_F11)) {
            renderer.toggleFullscreenMode();
        }

        // =====================================================================
        // STATE 0: 5-SECOND STUDIO SPLASH / LOADING SCREEN
        // =====================================================================
        if (mode == AppMode::Intro) {
            introTimer += dt;
            renderer.renderIntro(introTimer);
            if (introTimer >= Constants::INTRO_DURATION) {
                mode = AppMode::Menu;
            }
            continue;
        }

        // =====================================================================
        // STATE 1: START MENU (Mode Select / Host / Join via IP / Display)
        // =====================================================================
        if (mode == AppMode::Menu) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                if (editingIp) {
                    editingIp = false;
                    menuStatus = "IP ready (" + ipInput + "). Press [1] Host or [2]/[ENTER] Join.";
                } else {
                    break;
                }
            }

            if (IsKeyPressed(KEY_TAB)) {
                editingIp = !editingIp;
                menuStatus = editingIp
                    ? "Editing IP: type address and press [ENTER] to Join."
                    : "Press [M] to switch Mode, [1] to Host, [2] to Join.";
            }

            if (IsKeyPressed(KEY_BACKSPACE)) {
                editingIp = true;
                if (!ipInput.empty()) {
                    ipInput.pop_back();
                }
                menuStatus = "Editing IP: type address and press [ENTER] to Join.";
            }

            if (!editingIp) {
                // Game Mode Toggle (1v1 PvP vs 2P Co-op Waves)
                if (IsKeyPressed(KEY_M) || IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_DOWN)) {
                    selectedMode = (selectedMode == GameMode::PvP) ? GameMode::CoopWaves : GameMode::PvP;
                    menuStatus = (selectedMode == GameMode::CoopWaves)
                        ? "Mode: 2P Co-op Waves selected! Press [1] to Host."
                        : "Mode: 1v1 PvP Duel selected! Press [1] to Host.";
                }

                // Display & resolution controls on the start menu
                if (IsKeyPressed(KEY_F)) {
                    renderer.toggleFullscreenMode();
                    menuStatus = "Display changed to " + renderer.getDisplaySettingsLabel();
                } else if (IsKeyPressed(KEY_R) || IsKeyPressed(KEY_RIGHT)) {
                    renderer.cycleResolution(1);
                    menuStatus = "Resolution set to " + renderer.getDisplaySettingsLabel();
                } else if (IsKeyPressed(KEY_LEFT)) {
                    renderer.cycleResolution(-1);
                    menuStatus = "Resolution set to " + renderer.getDisplaySettingsLabel();
                }

                if (IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_KP_1)) {
                    if (network.startHost(Constants::DEFAULT_PORT)) {
                        enterGameSession(selectedMode);
                        continue;
                    } else {
                        menuStatus = network.getStatusMessage();
                    }
                } else if (IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_KP_2) || IsKeyPressed(KEY_ENTER)) {
                    const std::string targetIp = ipInput.empty() ? "127.0.0.1" : ipInput;
                    if (network.connectToHost(targetIp, Constants::DEFAULT_PORT)) {
                        enterGameSession(GameMode::PvP);
                        continue;
                    } else {
                        menuStatus = network.getStatusMessage();
                    }
                }

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
                        enterGameSession(GameMode::PvP);
                        continue;
                    } else {
                        menuStatus = network.getStatusMessage();
                    }
                }
            }

            renderer.renderMenu(ipInput, selectedMode, menuStatus);
        }
        // =====================================================================
        // STATE 2: ACTIVE MULTIPLAYER MATCH (Fixed 60Hz Tick + Prediction)
        // =====================================================================
        else if (mode == AppMode::InGame) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                network.shutdown();
                mode = AppMode::Menu;
                editingIp = false;
                menuStatus = "Disconnected. Press [M] Switch Mode, [1] Host, or [2] Join.";
                continue;
            }

            shootLatch |= (IsKeyDown(KEY_SPACE) || IsKeyPressed(KEY_SPACE));
            accumulator += dt;

            if (network.getRole() == NetworkRole::Host) {
                network.pollEvents();
                simulation.setCoopPlayer2Present(network.isConnected());

                while (accumulator >= Constants::FIXED_TIMESTEP) {
                    accumulator -= Constants::FIXED_TIMESTEP;
                    network.pollEvents();
                    simulation.setCoopPlayer2Present(network.isConnected());

                    prevHostState = simulation.getState();
                    const InputPacket hostInput   = sampleLocalInput(0, simulation.getState().tick, shootLatch);
                    shootLatch = false;
                    const InputPacket clientInput = network.consumeClientInput();

                    simulation.update(Constants::FIXED_TIMESTEP, hostInput, clientInput);
                    network.broadcastGameState(simulation.getState());
                }

                const float alpha = std::clamp(accumulator / Constants::FIXED_TIMESTEP, 0.0f, 1.0f);
                const GameState renderState = Simulation::interpolateState(prevHostState, simulation.getState(), alpha);

                renderer.renderGame(
                    renderState,
                    true,
                    network.isConnected(),
                    network.getStatusMessage(),
                    network.getPingMs(),
                    dt);
            } else if (network.getRole() == NetworkRole::Client) {
                network.pollEvents();

                if (network.hasReceivedState()) {
                    clientPred.reconcileWithServerState(network.getLatestGameState());
                }

                while (accumulator >= Constants::FIXED_TIMESTEP) {
                    accumulator -= Constants::FIXED_TIMESTEP;

                    clientPred.clientTick++;
                    const InputPacket clientInput = sampleLocalInput(1, clientPred.clientTick, shootLatch);
                    shootLatch = false;
                    network.sendInput(clientInput);

                    if (clientPred.initialized) {
                        clientPred.prevPredictedShip = clientPred.predictedShip;
                        clientPred.prevVisualState   = clientPred.visualState;

                        clientPred.pendingInputs.push_back(clientInput);
                        if (clientPred.pendingInputs.size() > 120) {
                            clientPred.pendingInputs.pop_front();
                        }

                        Simulation::stepShipPhysics(clientPred.predictedShip, clientInput, Constants::FIXED_TIMESTEP);

                        if (clientPred.timeSinceSnapshot < 0.12f) {
                            Simulation::extrapolateVisuals(clientPred.visualState, Constants::FIXED_TIMESTEP, true, true);
                            clientPred.timeSinceSnapshot += Constants::FIXED_TIMESTEP;
                        }
                    }
                }

                GameState renderState = simulation.getState();
                if (clientPred.initialized) {
                    const float alpha = std::clamp(accumulator / Constants::FIXED_TIMESTEP, 0.0f, 1.0f);
                    GameState prevCombined = clientPred.prevVisualState;
                    prevCombined.ships[1]  = clientPred.prevPredictedShip;
                    GameState currCombined = clientPred.visualState;
                    currCombined.ships[1]  = clientPred.predictedShip;
                    renderState = Simulation::interpolateState(prevCombined, currCombined, alpha);
                }

                renderer.renderGame(
                    renderState,
                    false,
                    network.isConnected(),
                    network.getStatusMessage(),
                    network.getPingMs(),
                    dt);
            }
        }
    }

    network.shutdown();
    renderer.shutdown();
    return 0;
}
