// Finds out why TotalMix FX does not answer when it runs on this computer: is anything
// listening on the port we send to, and is it TotalMix FX? (Seen in practice: the Sentinel
// Keys Server licence service, sntlkeyssrvr.exe, holds UDP port 7001, TotalMix's default
// "Port incoming", so TotalMix cannot open its OSC port.)
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rme {

struct PortOwner {
    uint32_t pid = 0;
    std::string process;  // executable name, e.g. "TotalMixFX_x64.exe" (UTF-8); may be empty
};

// Processes on this computer with an IPv4 UDP socket on 'port'. 'supported' is set to false
// where this cannot be determined (platforms other than Windows, or an API failure).
std::vector<PortOwner> udpPortOwners(uint16_t port, bool& supported);

// UDP ports on which TotalMix FX listens on this computer (its OSC "Port incoming" for each
// remote controller in use). Ports in the dynamic range (49152 and up) are left out: those are
// its sending sockets. Empty if TotalMix FX is not running or this cannot be determined.
std::vector<uint16_t> totalMixListenPorts();

// True if 'host' (an address or name) is this computer.
bool isLocalHost(const std::string& host);

uint32_t currentProcessId();

struct SendPortCheck {
    enum class Result {
        NotChecked,  // TotalMix FX is on another computer, or ports cannot be inspected here
        Free,        // nothing listens: TotalMix FX's OSC is not active (or uses another port)
        TotalMix,    // TotalMix FX listens: the problem is on its sending side
        Self,        // this program (send port = receive port)
        Other,       // another program holds the port
    };
    Result result = Result::NotChecked;
    std::string process;  // for Other
};

// Pure classification (tested); the inputs come from isLocalHost / udpPortOwners.
SendPortCheck classifySendPort(bool hostIsLocal, bool supported, const std::vector<PortOwner>& owners, uint32_t selfPid);

// Convenience: classifySendPort with live data.
SendPortCheck checkSendPort(const std::string& host, uint16_t port);

}  // namespace rme
