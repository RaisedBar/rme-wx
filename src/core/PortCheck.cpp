#include "PortCheck.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace rme {

namespace {

bool isTotalMixName(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("totalmixfx") != std::string::npos;  // TotalMixFX.exe, TotalMixFX_x64.exe
}

#ifdef _WIN32
std::string toUtf8(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::string processName(DWORD pid) {
    // A process snapshot gives names even for services we may not open (e.g. SYSTEM ones).
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return {};
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof pe;
    std::string name;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (pe.th32ProcessID == pid) {
            name = toUtf8(pe.szExeFile);
            break;
        }
    CloseHandle(snap);
    return name;
}

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
#endif

}  // namespace

uint32_t currentProcessId() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<uint32_t>(getpid());
#endif
}

namespace {
struct UdpRow {
    uint32_t pid;
    uint16_t port;
};

// Every IPv4 UDP socket on this computer. 'supported' is false if this cannot be read.
std::vector<UdpRow> udpTable(bool& supported) {
    std::vector<UdpRow> out;
    supported = false;
#ifdef _WIN32
    ULONG size = 0;
    if (GetExtendedUdpTable(nullptr, &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0) != ERROR_INSUFFICIENT_BUFFER)
        return out;
    std::vector<unsigned char> buf(size + 1024);
    size = static_cast<ULONG>(buf.size());
    if (GetExtendedUdpTable(buf.data(), &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0) != NO_ERROR) return out;
    supported = true;
    const auto* table = reinterpret_cast<const MIB_UDPTABLE_OWNER_PID*>(buf.data());
    for (DWORD i = 0; i < table->dwNumEntries; ++i)
        out.push_back({table->table[i].dwOwningPid, ntohs(static_cast<u_short>(table->table[i].dwLocalPort))});
#endif
    return out;
}

std::string nameOf(uint32_t pid) {
#ifdef _WIN32
    return processName(pid);
#else
    (void)pid;
    return {};
#endif
}
}  // namespace

std::vector<PortOwner> udpPortOwners(uint16_t port, bool& supported) {
    std::vector<PortOwner> out;
    for (const UdpRow& row : udpTable(supported)) {
        if (row.port != port) continue;
        if (std::any_of(out.begin(), out.end(), [&](const PortOwner& o) { return o.pid == row.pid; })) continue;
        out.push_back({row.pid, nameOf(row.pid)});
    }
    return out;
}

std::vector<uint16_t> totalMixListenPorts() {
    bool supported = false;
    std::vector<uint16_t> out;
    for (const UdpRow& row : udpTable(supported)) {
        // Below the dynamic range: ports TotalMix FX chose to listen on, not its sending socket.
        if (row.port == 0 || row.port >= 49152) continue;
        if (std::find(out.begin(), out.end(), row.port) != out.end()) continue;
        if (isTotalMixName(nameOf(row.pid))) out.push_back(row.port);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool isLocalHost(const std::string& host) {
#ifdef _WIN32
    static WinsockInit init;
    if (!init.ok) return false;
#endif
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    sockaddr_in a{};
    std::memcpy(&a, res->ai_addr, sizeof a);
    freeaddrinfo(res);
    if ((ntohl(a.sin_addr.s_addr) >> 24) == 127) return true;
    // An address of this computer is one we can bind to.
    a.sin_port = 0;
    auto s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) return false;
    const bool local = ::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0;
    closesocket(s);
#else
    if (s < 0) return false;
    const bool local = ::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0;
    ::close(s);
#endif
    return local;
}

SendPortCheck classifySendPort(bool hostIsLocal, bool supported, const std::vector<PortOwner>& owners, uint32_t selfPid) {
    SendPortCheck c;
    if (!hostIsLocal || !supported) return c;
    if (owners.empty()) {
        c.result = SendPortCheck::Result::Free;
        return c;
    }
    // TotalMix FX among the owners: it can receive (it may share the port with others).
    for (const auto& o : owners)
        if (o.pid != selfPid && isTotalMixName(o.process)) {
            c.result = SendPortCheck::Result::TotalMix;
            return c;
        }
    for (const auto& o : owners)
        if (o.pid != selfPid) {
            c.result = SendPortCheck::Result::Other;
            c.process = o.process.empty() ? "process " + std::to_string(o.pid) : o.process;
            return c;
        }
    c.result = SendPortCheck::Result::Self;
    return c;
}

SendPortCheck checkSendPort(const std::string& host, uint16_t port) {
    const bool local = isLocalHost(host);
    if (!local) return {};
    bool supported = false;
    const auto owners = udpPortOwners(port, supported);
    return classifySendPort(local, supported, owners, currentProcessId());
}

}  // namespace rme
