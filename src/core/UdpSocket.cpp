#include "UdpSocket.h"

#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#pragma comment(lib, "ws2_32.lib")
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
using socklen_t = int;
namespace {
constexpr uintptr_t kInvalid = static_cast<uintptr_t>(INVALID_SOCKET);
SOCKET S(uintptr_t s) { return static_cast<SOCKET>(s); }
int lastError() { return WSAGetLastError(); }
bool wouldBlock(int e) { return e == WSAEWOULDBLOCK; }
void closeSock(uintptr_t s) { closesocket(S(s)); }
struct WinsockInit {
    bool ok = false;
    WinsockInit() {
        WSADATA d;
        ok = WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }
    ~WinsockInit() {
        if (ok) WSACleanup();
    }
};
bool ensureWinsock() {
    static WinsockInit init;
    return init.ok;
}
}  // namespace
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
namespace {
constexpr uintptr_t kInvalid = ~uintptr_t(0);
int S(uintptr_t s) { return static_cast<int>(s); }
int lastError() { return errno; }
bool wouldBlock(int e) { return e == EWOULDBLOCK || e == EAGAIN; }
void closeSock(uintptr_t s) { ::close(S(s)); }
bool ensureWinsock() { return true; }
}  // namespace
#endif

namespace rme {

namespace {
std::string errorText(const char* what, int code) {
#ifdef _WIN32
    if (code == WSAEADDRINUSE) return std::string(what) + ": the port is already in use by another program";
    if (code == WSAEACCES) return std::string(what) + ": access denied";
#else
    if (code == EADDRINUSE) return std::string(what) + ": the port is already in use by another program";
#endif
    return std::string(what) + " failed (error " + std::to_string(code) + ")";
}
}  // namespace

UdpSocket::UdpSocket() : sock_(kInvalid) { buffer_.resize(65536); }

UdpSocket::~UdpSocket() { close(); }

bool UdpSocket::isOpen() const { return sock_ != kInvalid; }

bool UdpSocket::open(uint16_t localPort, std::string& error) {
    close();
    if (!ensureWinsock()) {
        error = "Could not start Windows networking";
        return false;
    }
    auto s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (static_cast<uintptr_t>(s) == kInvalid) {
        error = errorText("Creating the network socket", lastError());
        return false;
    }
    sock_ = static_cast<uintptr_t>(s);

    // A large receive buffer: TotalMix FX sends a burst of several hundred messages when a page
    // or bank changes.
    int rcv = 1 << 20;
    setsockopt(S(sock_), SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcv), sizeof rcv);

    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(localPort);
    if (::bind(S(sock_), reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        error = errorText(("Listening on UDP port " + std::to_string(localPort)).c_str(), lastError());
        close();
        return false;
    }
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(S(sock_), FIONBIO, &nb);
    // Without this, an ICMP "port unreachable" (TotalMix FX not running) makes the next
    // recvfrom() fail with WSAECONNRESET.
    BOOL report = FALSE;
    DWORD bytes = 0;
    WSAIoctl(S(sock_), SIO_UDP_CONNRESET, &report, sizeof report, nullptr, 0, &bytes, nullptr, nullptr);
#else
    fcntl(S(sock_), F_SETFL, fcntl(S(sock_), F_GETFL, 0) | O_NONBLOCK);
#endif
    localPort_ = localPort;
    return true;
}

void UdpSocket::close() {
    if (sock_ != kInvalid) closeSock(sock_);
    sock_ = kInvalid;
    localPort_ = 0;
}

bool UdpSocket::setDestination(const std::string& host, uint16_t port, std::string& error) {
    hasDest_ = false;
    if (!ensureWinsock()) {
        error = "Could not start Windows networking";
        return false;
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
        error = "Could not find the address \"" + host + "\"";
        return false;
    }
    sockaddr_in a{};
    std::memcpy(&a, res->ai_addr, sizeof a);
    freeaddrinfo(res);
    a.sin_port = htons(port);
    static_assert(sizeof a <= sizeof dest_, "sockaddr_in size");
    std::memcpy(dest_, &a, sizeof a);
    hasDest_ = true;
    return true;
}

bool UdpSocket::send(const std::vector<uint8_t>& data, std::string& error) {
    if (!isOpen()) {
        error = "The network socket is not open";
        return false;
    }
    if (!hasDest_) {
        error = "No TotalMix FX address set";
        return false;
    }
    const auto n = ::sendto(S(sock_), reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0,
                            reinterpret_cast<const sockaddr*>(dest_), sizeof(sockaddr_in));
    if (n < 0) {
        error = errorText("Sending", lastError());
        return false;
    }
    return true;
}

int UdpSocket::receive(const std::function<void(const uint8_t*, size_t)>& fn, int maxPackets) {
    if (!isOpen()) return 0;
    int count = 0;
    while (count < maxPackets) {
        sockaddr_in from{};
        socklen_t len = sizeof from;
        const auto n = ::recvfrom(S(sock_), reinterpret_cast<char*>(buffer_.data()), static_cast<int>(buffer_.size()), 0,
                                  reinterpret_cast<sockaddr*>(&from), &len);
        if (n < 0) {
            const int e = lastError();
            if (wouldBlock(e)) break;
#ifdef _WIN32
            if (e == WSAECONNRESET || e == WSAEMSGSIZE) continue;
#endif
            break;
        }
        ++count;
        if (fn) fn(buffer_.data(), static_cast<size_t>(n));
    }
    return count;
}

}  // namespace rme
