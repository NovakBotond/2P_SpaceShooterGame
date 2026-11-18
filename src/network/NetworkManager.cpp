// =============================================================================
// OWNER: Developer B (Networking & Gameplay Logic)
// FILE:  src/network/NetworkManager.cpp
// DESC:  Implementation of ENet Host and Client UDP session management,
//        packet serialization, and non-blocking event polling.
// =============================================================================

#include "network/NetworkManager.hpp"
#include <enet/enet.h>
#include <cstring>
#include <iostream>

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
    m_latestClientInput = InputPacket{};
    m_statusMessage = "Disconnected.";
}

void NetworkManager::pollEvents() {
    if (!m_host) {
        return;
    }

    bool shootLatchedThisFrame = false;
    ENetEvent event{};

    while (enet_host_service(m_host, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                m_remotePeer = event.peer;
                m_isConnected = true;

                // Configure quicker ping/timeout for responsive LAN/localhost play
                enet_peer_ping_interval(event.peer, 250);
                enet_peer_timeout(event.peer, 16, 2000, 5000);

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
                        if (incoming.shoot) {
                            shootLatchedThisFrame = true;
                        }
                        m_latestClientInput = incoming;
                        if (shootLatchedThisFrame) {
                            m_latestClientInput.shoot = 1;
                        }
                    } else if (m_role == NetworkRole::Client &&
                               type == static_cast<uint8_t>(PacketType::GAME_STATE) &&
                               event.packet->dataLength == sizeof(GameState)) {

                        std::memcpy(&m_latestGameState, event.packet->data, sizeof(GameState));
                        m_hasReceivedState = true;
                    }
                }
                enet_packet_destroy(event.packet);
                break;
            }

            case ENET_EVENT_TYPE_DISCONNECT: {
                m_isConnected = false;
                m_remotePeer = nullptr;
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
}

void NetworkManager::sendInput(const InputPacket& input) {
    if (m_role != NetworkRole::Client || !m_host || !m_remotePeer || !m_isConnected) {
        return;
    }

    // Channel 0, unsequenced/unreliable low-latency UDP stream for 60Hz inputs
    ENetPacket* packet = enet_packet_create(&input, sizeof(InputPacket), 0);
    enet_peer_send(m_remotePeer, 0, packet);
    enet_host_flush(m_host);
}

void NetworkManager::broadcastGameState(const GameState& state) {
    if (m_role != NetworkRole::Host || !m_host || !m_isConnected) {
        return;
    }

    // Broadcast authoritative snapshot on Channel 0
    ENetPacket* packet = enet_packet_create(&state, sizeof(GameState), 0);
    enet_host_broadcast(m_host, 0, packet);
    enet_host_flush(m_host);
}
