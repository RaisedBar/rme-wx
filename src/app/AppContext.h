// Services shared by the main frame and the panels.
#pragma once

#include <wx/string.h>

#include <functional>
#include <string>
#include <unordered_map>

#include "Osc.h"

// Routes OSC messages received from TotalMix FX to the controls that show them. The key is
// the full address without the "Val" suffix ("/1/volume3"); 'value' gets numeric arguments,
// 'text' gets string arguments and the "...Val" display strings.
class ControlRegistry {
public:
    struct Target {
        std::function<void(float)> value;
        std::function<void(const std::string&)> text;
    };
    void add(const std::string& address, Target t) { map_[address] = std::move(t); }
    void remove(const std::string& address) { map_.erase(address); }
    const Target* find(const std::string& address) const {
        auto it = map_.find(address);
        return it == map_.end() ? nullptr : &it->second;
    }

private:
    std::unordered_map<std::string, Target> map_;
};

struct AppContext {
    ControlRegistry registry;
    std::function<void(const rme::OscMessage&)> send;
    std::function<void(const wxString&)> log;
    // Debounced screen reader announcement of a change ('key' identifies what changed).
    std::function<void(const wxString& key, const wxString& text)> announceChange;
    // Status message: log, status bar and screen reader.
    std::function<void(const wxString& text, bool problem)> announceStatus;
    // TotalMix FX re-sends a whole page after page or bank changes, snapshot loads, etc.
    // beginQuiet() starts such a period; quiet() is true during it, and changes received
    // then are not announced (except those marked Announce::Always).
    std::function<void()> beginQuiet;
    std::function<bool()> quiet;

    void sendFloat(const std::string& address, float v) {
        if (send) send(rme::OscMessage::withFloat(address, v));
    }
};
