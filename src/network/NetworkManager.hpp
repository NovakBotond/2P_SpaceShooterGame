#pragma once
// =============================================================================
// OWNER: Developer B (Networking & Gameplay Logic)
// FILE:  src/network/NetworkManager.hpp
// DESC:  ENet UDP Host & Client networking wrapper for 2-machine multiplayer.
//        Handles hosting on port 7777, connecting by IP address, sending
//        client InputPackets, and broadcasting authoritative GameState.
// =============================================================================

#include "shared/Constants.hpp"
#include "shared/Protocol.hpp"
#include <cstdint>
#include <string>

// Forward declarations to avoid leaking <enet/enet.h> (and Windows <winsock2.h>)
// into translation units that include <raylib.h>.
struct _ENetHost;
struct _ENetPeer;
using ENetHost = struct _ENetHost;
using ENetPeer = struct _ENetPeer;

enum class NetworkRole {
    None,
    Host,
    Client
};

class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();

    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    // Start an authoritative ENet server on the specified UDP port (default 7777)
    bool startHost(uint16_t port = Constants::DEFAULT_PORT);

    // Connect as an ENet client to a remote Host IP and UDP port
    bool connectToHost(const std::string& ipAddress, uint16_t port = Constants::DEFAULT_PORT);

    // Cleanly tear down active peer connection and ENet host
    void shutdown();

    // Non-blocking poll of queued ENet events; call once per frame
    void pollEvents();

    // Client -> Host: send local player input snapshot
    void sendInput(const InputPacket& input);

    // Host -> Client: broadcast authoritative simulation state snapshot
    void broadcastGameState(const GameState& state);

    // Accessors
    NetworkRole        getRole() const { return m_role; }
    bool               isConnected() const { return m_isConnected; }
    bool               hasReceivedState() const { return m_hasReceivedState; }
    const InputPacket& getLatestClientInput() const { return m_latestClientInput; }
    const GameState&   getLatestGameState() const { return m_latestGameState; }
    const std::string& getStatusMessage() const { return m_statusMessage; }

private:
    bool        m_enetInitialized  = false;
    NetworkRole m_role             = NetworkRole::None;
    ENetHost*   m_host             = nullptr;
    ENetPeer*   m_remotePeer       = nullptr;
    bool        m_isConnected      = false;
    bool        m_hasReceivedState = false;

    InputPacket m_latestClientInput{};
    GameState   m_latestGameState{};
    std::string m_statusMessage    = "Idle";
};
