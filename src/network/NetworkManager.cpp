// =============================================================================
// OWNER: Developer B (Networking & Gameplay Logic)
// FILE:  src/network/NetworkManager.cpp
// DESC:  Implementation of ENet Host and Client UDP session management,
//        compact wire serialization, unsequenced low-latency delivery,
//        and bounded jitter-tolerant input queuing.
// =============================================================================

#include "network/NetworkManager.hpp"
#include <enet/enet.h>
#include <cstring>
#include <iostream>

namespace {
#pragma pack(push, 1)
    struct CompactGameStateHeader {
        uint8_t      packetType;
        uint32_t     tick;
        uint32_t     lastClientTick;
        uint8_t      gameMode;
        ShipState    ships[Constants::MAX_PLAYERS];
        CometState   comets[Constants::MAX_COMETS];
        PowerUpState powerUps[Constants::MAX_MAP_POWERUPS];
        uint16_t     currentWave;
        uint16_t     waveRemainingKills;
        float        enemySlowTimer;
        float        enemyFireSlowTimer;
        uint8_t      bossActive;
        uint8_t      lastRoundWinner;
        float        roundBannerTimer;
        uint8_t      bannerCode;
        uint8_t      activeBulletCount;
        uint8_t      activeEnemyCount;
    };
#pragma pack(pop)

    void zeroGameState(GameState& s) {
        std::memset(&s, 0, sizeof(GameState));
        s.packetType      = static_cast<uint8_t>(PacketType::GAME_STATE);
        s.currentWave     = 1;
        s.lastRoundWinner = 255;
        for (int i = 0; i < Constants::MAX_PLAYERS; ++i) {
            s.ships[i].hp     = Constants::SHIP_MAX_HP;
            s.ships[i].ammo   = 1.0f;
            s.ships[i].active = 1;
        }
    }
}

NetworkManager::NetworkManager() {
    if (enet_initialize() == 0) {
        m_enetInitialized = true;
        m_statusMessage = "ENet initialized. Ready.";
    } else {
        m_enetInitialized = false;
        m_statusMessage = "ERROR: Failed to initialize ENet.";
        std::cerr << "[NetworkManager] Failed to initialize ENet!\n";
    }
}

NetworkManager::~NetworkManager() {
    shutdown();
    if (m_enetInitialized) {
        enet_deinitialize();
        m_enetInitialized = false;
    }
}

bool NetworkManager::startHost(uint16_t port) {
    if (!m_enetInitialized) {
        return false;
    }

    shutdown();

    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;

    // Allow 1 remote client peer, 2 channels, unthrottled bandwidth
    m_host = enet_host_create(&address, 1, Constants::NET_CHANNEL_COUNT, 0, 0);
    if (!m_host) {
        m_statusMessage = "ERROR: Could not bind UDP port " + std::to_string(port) + " (already in use?)";
        std::cerr << "[NetworkManager] " << m_statusMessage << "\n";
        return false;
    }

    m_role = NetworkRole::Host;
    m_isConnected = false;
    m_pingMs = 0;
    m_hasAcceptedClientInput = false;
    m_lastAcceptedClientInputTick = 0;
    m_clientInputQueue.clear();
    m_latestClientInput = InputPacket{};
    m_statusMessage = "Hosting on UDP port " + std::to_string(port) + " - Waiting for Player 2...";
    std::cout << "[NetworkManager] " << m_statusMessage << "\n";
    return true;
}

bool NetworkManager::connectToHost(const std::string& ipAddress, uint16_t port) {
    if (!m_enetInitialized) {
        return false;
    }

    shutdown();

    m_host = enet_host_create(nullptr, 1, Constants::NET_CHANNEL_COUNT, 0, 0);
    if (!m_host) {
        m_statusMessage = "ERROR: Failed to create ENet client host.";
        std::cerr << "[NetworkManager] " << m_statusMessage << "\n";
        return false;
    }

    ENetAddress address{};
    if (enet_address_set_host(&address, ipAddress.c_str()) != 0) {
        m_statusMessage = "ERROR: Invalid host IP address: " + ipAddress;
        enet_host_destroy(m_host);
        m_host = nullptr;
        return false;
    }
    address.port = port;

    m_remotePeer = enet_host_connect(m_host, &address, Constants::NET_CHANNEL_COUNT, 0);
    if (!m_remotePeer) {
        m_statusMessage = "ERROR: No available ENet peers for connection.";
        enet_host_destroy(m_host);
        m_host = nullptr;
        return false;
    }

    m_role = NetworkRole::Client;
    m_isConnected = false;
    m_hasReceivedState = false;
    m_pingMs = 0;
    zeroGameState(m_latestGameState);
    m_statusMessage = "Connecting to " + ipAddress + ":" + std::to_string(port) + "...";
    std::cout << "[NetworkManager] " << m_statusMessage << "\n";
    return true;
}

void NetworkManager::shutdown() {
    if (m_remotePeer) {
        enet_peer_disconnect_now(m_remotePeer, 0);
        m_remotePeer = nullptr;
    }
    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
    }
    m_role = NetworkRole::None;
    m_isConnected = false;
    m_hasReceivedState = false;
    m_pingMs = 0;
    m_hasAcceptedClientInput = false;
    m_lastAcceptedClientInputTick = 0;
    m_clientInputQueue.clear();
    m_latestClientInput = InputPacket{};
    zeroGameState(m_latestGameState);
    m_statusMessage = "Disconnected.";
}

void NetworkManager::pollEvents() {
    if (!m_host) {
        return;
    }

    ENetEvent event{};

    while (enet_host_service(m_host, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                m_remotePeer = event.peer;
                m_isConnected = true;
                m_pingMs = event.peer->roundTripTime;

                // Configure quicker ping/timeout for responsive LAN/Wi-Fi play
                enet_peer_ping_interval(event.peer, 250);
                enet_peer_timeout(event.peer, 16, 2000, 5000);
                // Disable ENet's unreliable packet-drop throttling so Wi-Fi RTT jitter
                // never causes ENet to silently drop state snapshots or inputs.
                enet_peer_throttle_configure(event.peer, 5000, 32, 0);

                m_hasAcceptedClientInput = false;
                m_lastAcceptedClientInputTick = 0;
                m_clientInputQueue.clear();
                m_latestClientInput = InputPacket{};
                m_hasReceivedState = false;
                zeroGameState(m_latestGameState);

                if (m_role == NetworkRole::Host) {
                    m_statusMessage = "Player 2 Connected! Match Live.";
                } else {
                    m_statusMessage = "Connected to Host! Match Live.";
                }
                std::cout << "[NetworkManager] " << m_statusMessage << "\n";
                break;
            }

            case ENET_EVENT_TYPE_RECEIVE: {
                if (event.packet->dataLength >= 1) {
                    const uint8_t type = event.packet->data[0];

                    if (m_role == NetworkRole::Host &&
                        type == static_cast<uint8_t>(PacketType::INPUT) &&
                        event.packet->dataLength == sizeof(InputPacket)) {

                        InputPacket incoming{};
                        std::memcpy(&incoming, event.packet->data, sizeof(InputPacket));

                        const bool isFirst = !m_hasAcceptedClientInput;
                        const bool isNewer = incoming.tick > m_lastAcceptedClientInputTick;
                        const bool isClientReset = (m_lastAcceptedClientInputTick > incoming.tick) &&
                                                   ((m_lastAcceptedClientInputTick - incoming.tick) > 300);

                        if (isFirst || isNewer || isClientReset) {
                            m_hasAcceptedClientInput = true;
                            m_lastAcceptedClientInputTick = incoming.tick;
                            m_clientInputQueue.push_back(incoming);

                            // Cap input queue depth to prevent latency buildup after Wi-Fi bursts
                            while (m_clientInputQueue.size() > 3) {
                                const uint8_t discardedShoot = m_clientInputQueue.front().shoot;
                                m_clientInputQueue.pop_front();
                                if (discardedShoot && !m_clientInputQueue.empty()) {
                                    m_clientInputQueue.front().shoot = 1;
                                }
                            }
                        }
                    } else if (m_role == NetworkRole::Client &&
                               type == static_cast<uint8_t>(PacketType::GAME_STATE) &&
                               event.packet->dataLength >= sizeof(CompactGameStateHeader)) {

                        CompactGameStateHeader header{};
                        std::memcpy(&header, event.packet->data, sizeof(CompactGameStateHeader));

                        if (header.activeBulletCount <= Constants::MAX_BULLETS &&
                            header.activeEnemyCount <= Constants::MAX_ENEMIES) {
                            const size_t bulletsBytes = static_cast<size_t>(header.activeBulletCount) * sizeof(BulletState);
                            const size_t enemiesBytes = static_cast<size_t>(header.activeEnemyCount) * sizeof(EnemyState);
                            const size_t expectedSize = sizeof(CompactGameStateHeader) + bulletsBytes + enemiesBytes;

                            if (event.packet->dataLength == expectedSize) {
                                const bool isFirst = !m_hasReceivedState;
                                const bool isNewer = header.tick > m_latestGameState.tick;
                                const bool isMatchRestart = (m_latestGameState.tick > header.tick) &&
                                                            ((m_latestGameState.tick - header.tick) > 300);

                                if (isFirst || isNewer || isMatchRestart) {
                                    GameState reconstructed;
                                    std::memset(&reconstructed, 0, sizeof(GameState));
                                    reconstructed.packetType         = header.packetType;
                                    reconstructed.tick               = header.tick;
                                    reconstructed.lastClientTick     = header.lastClientTick;
                                    reconstructed.gameMode           = header.gameMode;
                                    std::memcpy(reconstructed.ships, header.ships, sizeof(header.ships));
                                    std::memcpy(reconstructed.comets, header.comets, sizeof(header.comets));
                                    std::memcpy(reconstructed.powerUps, header.powerUps, sizeof(header.powerUps));
                                    reconstructed.currentWave        = header.currentWave;
                                    reconstructed.waveRemainingKills = header.waveRemainingKills;
                                    reconstructed.enemySlowTimer     = header.enemySlowTimer;
                                    reconstructed.enemyFireSlowTimer = header.enemyFireSlowTimer;
                                    reconstructed.bossActive         = header.bossActive;
                                    reconstructed.lastRoundWinner    = header.lastRoundWinner;
                                    reconstructed.roundBannerTimer   = header.roundBannerTimer;
                                    reconstructed.bannerCode         = header.bannerCode;

                                    const uint8_t* payload = event.packet->data + sizeof(CompactGameStateHeader);
                                    if (bulletsBytes > 0) {
                                        std::memcpy(reconstructed.bullets, payload, bulletsBytes);
                                    }
                                    if (enemiesBytes > 0) {
                                        std::memcpy(reconstructed.enemies, payload + bulletsBytes, enemiesBytes);
                                    }

                                    std::memcpy(&m_latestGameState, &reconstructed, sizeof(GameState));
                                    m_hasReceivedState = true;
                                }
                            }
                        }
                    }
                }
                enet_packet_destroy(event.packet);
                break;
            }

            case ENET_EVENT_TYPE_DISCONNECT: {
                m_isConnected = false;
                m_remotePeer = nullptr;
                m_pingMs = 0;
                m_hasAcceptedClientInput = false;
                m_lastAcceptedClientInputTick = 0;
                m_clientInputQueue.clear();
                m_latestClientInput = InputPacket{};

                if (m_role == NetworkRole::Host) {
                    m_statusMessage = "Player 2 Disconnected. Waiting for reconnect...";
                } else {
                    m_statusMessage = "Lost connection to Host.";
                }
                std::cout << "[NetworkManager] " << m_statusMessage << "\n";
                break;
            }

            case ENET_EVENT_TYPE_NONE:
            default:
                break;
        }
    }

    if (m_remotePeer && m_isConnected) {
        m_pingMs = m_remotePeer->roundTripTime;
    }
}

InputPacket NetworkManager::consumeClientInput() {
    if (!m_clientInputQueue.empty()) {
        InputPacket next = m_clientInputQueue.front();
        m_clientInputQueue.pop_front();
        m_latestClientInput = next;
        return next;
    }

    // No new packet this tick: repeat held movement without re-triggering shoot
    InputPacket held = m_latestClientInput;
    held.shoot = 0;
    return held;
}

void NetworkManager::sendInput(const InputPacket& input) {
    if (m_role != NetworkRole::Client || !m_host || !m_remotePeer || !m_isConnected) {
        return;
    }

    // Channel 0, unsequenced/unreliable low-latency UDP stream for 60Hz inputs
    ENetPacket* packet = enet_packet_create(&input, sizeof(InputPacket), ENET_PACKET_FLAG_UNSEQUENCED);
    enet_peer_send(m_remotePeer, 0, packet);
    enet_host_flush(m_host);
}

void NetworkManager::broadcastGameState(const GameState& state) {
    if (m_role != NetworkRole::Host || !m_host || !m_isConnected) {
        return;
    }

    // Serialize compact snapshot containing only active bullets and active enemies
    uint8_t wireBuffer[sizeof(CompactGameStateHeader) +
                       Constants::MAX_BULLETS * sizeof(BulletState) +
                       Constants::MAX_ENEMIES * sizeof(EnemyState)]{};

    CompactGameStateHeader header{};
    header.packetType         = static_cast<uint8_t>(PacketType::GAME_STATE);
    header.tick               = state.tick;
    header.lastClientTick     = state.lastClientTick;
    header.gameMode           = state.gameMode;
    std::memcpy(header.ships, state.ships, sizeof(header.ships));
    std::memcpy(header.comets, state.comets, sizeof(header.comets));
    std::memcpy(header.powerUps, state.powerUps, sizeof(header.powerUps));
    header.currentWave        = state.currentWave;
    header.waveRemainingKills = state.waveRemainingKills;
    header.enemySlowTimer     = state.enemySlowTimer;
    header.enemyFireSlowTimer = state.enemyFireSlowTimer;
    header.bossActive         = state.bossActive;
    header.lastRoundWinner    = state.lastRoundWinner;
    header.roundBannerTimer   = state.roundBannerTimer;
    header.bannerCode         = state.bannerCode;

    size_t writeOffset = sizeof(CompactGameStateHeader);
    uint8_t activeBullets = 0;
    for (int i = 0; i < Constants::MAX_BULLETS; ++i) {
        if (state.bullets[i].active) {
            std::memcpy(wireBuffer + writeOffset, &state.bullets[i], sizeof(BulletState));
            writeOffset += sizeof(BulletState);
            ++activeBullets;
        }
    }

    uint8_t activeEnemies = 0;
    for (int i = 0; i < Constants::MAX_ENEMIES; ++i) {
        if (state.enemies[i].active) {
            std::memcpy(wireBuffer + writeOffset, &state.enemies[i], sizeof(EnemyState));
            writeOffset += sizeof(EnemyState);
            ++activeEnemies;
        }
    }

    header.activeBulletCount = activeBullets;
    header.activeEnemyCount  = activeEnemies;
    std::memcpy(wireBuffer, &header, sizeof(CompactGameStateHeader));

    ENetPacket* packet = enet_packet_create(wireBuffer, writeOffset, ENET_PACKET_FLAG_UNSEQUENCED);
    enet_host_broadcast(m_host, 0, packet);
    enet_host_flush(m_host);
}
