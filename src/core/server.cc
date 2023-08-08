// SPDX-License-Identifier: MIT
#include "core/server.h"
#include "core/common.h"
#include "core/message.h"
#include "utility/address.h"
#include "utility/uri.h"
#include <spdlog/fmt/bin_to_hex.h>
#include <spdlog/spdlog.h>

namespace Candy {

int Server::setWebSocketServer(const std::string &uri) {
    Uri parser(uri);
    if (!parser.isValid()) {
        spdlog::critical("Invalid websocket uri: {}", uri);
        return -1;
    }
    if (parser.scheme() != "ws") {
        spdlog::critical("websocket server only support ws. please use a proxy such as nginx to handle encryption");
        return -1;
    }
    // 服务端必须指定端口号
    if (parser.port().empty()) {
        spdlog::critical("websocket server must specify the listening port");
        return -1;
    }
    // 服务端必须指定 IP 地址和端口号,不能用域名
    Address address;
    if (address.ipStrUpdate(parser.host())) {
        spdlog::critical("Invalid websocket server ip: {}", parser.host());
        return -1;
    }
    this->ipStr = address.getIpStr();
    this->port = std::stoi(parser.port());
    return 0;
}

int Server::setPassword(const std::string &password) {
    this->password = password;
    return 0;
}

int Server::setDynamicAddressRange(const std::string &cidr) {
    if (this->dynamic.cidrUpdate(cidr)) {
        spdlog::critical("Dynamic address generator init failed");
        return -1;
    }
    this->dynamic.ipMaskUpdate(this->dynamic.getNet(), this->dynamic.getMask());
    this->dynamic.next();
    return 0;
}

int Server::run() {
    this->running = true;

    if (startWsThread()) {
        spdlog::critical("Start websocket server thread failed");
        return -1;
    }
    return 0;
}

int Server::shutdown() {
    if (!this->running) {
        return 0;
    }

    this->running = false;

    if (this->wsThread.joinable()) {
        this->wsThread.join();
    }

    this->ws.stop();
    return 0;
}

int Server::startWsThread() {
    if (this->ws.listen(this->ipStr, this->port)) {
        spdlog::critical("Websocket server listen failed");
        return -1;
    }

    if (this->ws.setTimeout(1)) {
        spdlog::critical("Websocket server set read write timeout failed");
        return -1;
    }

    this->wsThread = std::move(std::thread(&Server::handleWebSocketMessage, this));
    return 0;
}

void Server::handleWebSocketMessage() {
    int error;
    WebSocketMessage message;

    while (this->running) {
        error = this->ws.read(message);
        if (error == 0) {
            continue;
        }
        if (error < 0) {
            spdlog::error("WebSocket server read failed: error={0}", error);
            break;
        }

        if (message.type == WebSocketMessageType::Message) {
            if (message.buffer.front() == MessageType::TYPE_FORWARD) {
                handleForwardMessage(message);
                continue;
            }
            if (message.buffer.front() == MessageType::TYPE_AUTH) {
                handleAuthMessage(message);
                continue;
            }
            if (message.buffer.front() == MessageType::TYPE_DYNAMIC_ADDRESS) {
                handleDynamicAddressMessage(message);
                continue;
            }
            spdlog::warn("Unknown message type. type={0}", message.buffer.front());
            continue;
        }

        if (message.type == WebSocketMessageType::Close) {
            handleCloseMessage(message);
            continue;
        }
        if (message.type == WebSocketMessageType::Error) {
            spdlog::critical("WebSocket communication exception");
            break;
        }
    }
    Candy::shutdown();
    return;
}

void Server::handleAuthMessage(WebSocketMessage &message) {
    if (message.buffer.length() != sizeof(AuthHeader)) {
        spdlog::warn("Invalid auth package: len={}", message.buffer.length());
        return;
    }

    AuthHeader *header = (AuthHeader *)message.buffer.data();
    if (!header->check(this->password)) {
        spdlog::warn("Auth header check failed: buffer={:n}", spdlog::to_hex(message.buffer));
        return;
    }

    Address address;
    address.ipUpdate(Address::netToHost(header->ip));
    if (this->ipWsMap.contains(address.getIp())) {
        this->ws.close(ipWsMap[address.getIp()]);
        spdlog::info("{} conflict, old connection kicked out", address.getIpStr());
    }

    spdlog::info("{} connected", address.getIpStr());
    ipWsMap[address.getIp()] = message.conn;
    wsIpMap[message.conn] = address.getIp();
}

void Server::handleForwardMessage(WebSocketMessage &message) {
    if (message.buffer.length() < sizeof(ForwardHeader)) {
        spdlog::warn("Invalid forawrd package: len={}", message.buffer.length());
        return;
    }

    ForwardHeader *header = (ForwardHeader *)message.buffer.data();
    if (!this->ipWsMap.contains(Address::netToHost(header->iph.saddr))) {
        spdlog::warn("Source client not logged in");
        return;
    }

    if (this->ipWsMap[Address::netToHost(header->iph.saddr)] != message.conn) {
        spdlog::warn("Source client address does not match connection");
        return;
    }

    if (!this->ipWsMap.contains(Address::netToHost(header->iph.daddr))) {
        spdlog::warn("Destination client not logged in");
        return;
    }

    message.conn = ipWsMap[Address::netToHost(header->iph.daddr)];
    this->ws.write(message);
}

void Server::handleDynamicAddressMessage(WebSocketMessage &message) {
    if (message.buffer.length() != sizeof(DynamicAddressHeader)) {
        spdlog::warn("Invalid dynamic address package: len={}", message.buffer.length());
        return;
    }

    DynamicAddressHeader *header = (DynamicAddressHeader *)message.buffer.data();
    if (!header->check(this->password)) {
        spdlog::warn("Dynamic address header check failed: buffer={:n}", spdlog::to_hex(message.buffer));
        return;
    }

    Address address;
    address.cidrUpdate(header->cidr);

    // 期望使用的地址不再当前网络或已经被分配
    if (!dynamic.inSameNetwork(address) || this->ipWsMap.contains(address.getIp())) {
        // 生成下一个动态地址并检查是否可用
        uint32_t oldip = dynamic.getIp();
        uint32_t newip = oldip;
        while (this->ipWsMap.contains(newip)) {
            // 获取下一个地址失败,一般不会发生,除非输入的配置错误
            if (this->dynamic.next()) {
                spdlog::error("Unable to get next available address");
                return;
            }
            newip = dynamic.getIp();
            if (oldip == newip) {
                spdlog::warn("All addresses in the network are assigned");
                return;
            }
        }
        address.ipMaskUpdate(dynamic.getIp(), dynamic.getMask());
    }

    header->timestamp = Time::hostToNet(Time::unixTime());
    std::strcpy(header->cidr, address.getCidr().c_str());
    header->updateHash(this->password);

    this->ws.write(message);
}

void Server::handleCloseMessage(WebSocketMessage &message) {
    auto it = this->wsIpMap.find(message.conn);
    if (it == this->wsIpMap.end()) {
        return;
    }

    if (this->ipWsMap[it->second] == message.conn) {
        this->ipWsMap.erase(it->second);
    }

    this->wsIpMap.erase(message.conn);
}

}; // namespace Candy