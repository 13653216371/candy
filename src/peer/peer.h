// SPDX-License-Identifier: MIT
#ifndef CANDY_PEER_PEER_H
#define CANDY_PEER_PEER_H

#include <any>
#include <cstdint>
#include <string>

namespace Candy {

enum class PeerState {
    INIT,
    PERPARING,
    SYNCHRONIZING,
    CONNECTING,
    CONNECTED,
    WAITTING,
    FAILED,
};

class PeerInfo {
public:
    uint32_t tun;
    uint32_t ip;
    uint16_t port;
    uint8_t ack;

    uint32_t count;
    uint32_t retry;
    void reset();
    int updateKey(const std::string &password);
    std::string getKey() const;
    void updateState(PeerState state);
    PeerState getState() const;

    PeerInfo();

private:
    std::string getStateStr(PeerState state);
    PeerState state;
    std::string key;
};

class UdpMessage {
public:
    uint32_t ip;
    uint16_t port;
    std::string buffer;
};

class UdpHolder {
public:
    UdpHolder();
    ~UdpHolder();
    size_t read(UdpMessage &message);
    size_t write(const UdpMessage &message);

private:
    std::any socket;
};

}; // namespace Candy

#endif