// A small non-blocking UDP socket (Winsock on Windows, BSD sockets elsewhere). Polled from
// the GUI timer, so there are no threads and no locking.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rme {

class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Binds to 'localPort' on all interfaces (TotalMix FX sends to this port).
    bool open(uint16_t localPort, std::string& error);
    void close();
    bool isOpen() const;
    uint16_t localPort() const { return localPort_; }

    // Resolves the destination (an IPv4 address or host name).
    bool setDestination(const std::string& host, uint16_t port, std::string& error);
    bool hasDestination() const { return hasDest_; }

    bool send(const std::vector<uint8_t>& data, std::string& error);

    // Calls 'fn' for each datagram waiting, up to 'maxPackets'. Returns the number read.
    int receive(const std::function<void(const uint8_t*, size_t)>& fn, int maxPackets = 1000);

private:
    uintptr_t sock_;
    uint16_t localPort_ = 0;
    bool hasDest_ = false;
    uint64_t dest_[2] = {};  // sockaddr_in (16 bytes, 8-byte aligned)
    std::vector<uint8_t> buffer_;
};

}  // namespace rme
