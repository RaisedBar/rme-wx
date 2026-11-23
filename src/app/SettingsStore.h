// Settings kept in an XML file that users may also edit by hand.
//
// Location: <user data dir>/settings.xml
//   Windows: %APPDATA%\TotalMixAccess\settings.xml
//   macOS:   ~/Library/Application Support/TotalMixAccess/settings.xml
#pragma once

#include <wx/string.h>

struct Settings {
    // TotalMix FX: Options > Settings > OSC. "Remote Controller Address" must be this
    // computer; TotalMix's "Port incoming" is sendPort and "Port outgoing" is receivePort.
    wxString host = "127.0.0.1";
    int sendPort = 7001;
    int receivePort = 9001;
    // Must match "Number of faders per bank" in TotalMix FX's OSC settings.
    int bankSize = 8;
    bool logIncoming = true;
    bool logLevels = false;  // level meters arrive many times a second
    // Seconds to wait after the last change from TotalMix FX before announcing it.
    double announceDebounceSeconds = 0.05;
    static constexpr double kMaxDebounceSeconds = 5.0;

    // Voice control (Windows): listens on a headset microphone for "<wake word> <command>".
    bool voiceEnabled = false;
    wxString wakeWord = "TotalMix";
    // Recognitions with a lower engine confidence (0..1) are ignored.
    double voiceMinConfidence = 0.5;
    // A specific microphone by name; empty = any headset.
    wxString voiceDevice;

    bool sameConnection(const Settings& o) const {
        return host == o.host && sendPort == o.sendPort && receivePort == o.receivePort;
    }
};

class SettingsStore {
public:
    static wxString filePath();
    // Returns false (with 'error' empty) if the file does not exist yet. Missing or invalid
    // entries keep their defaults.
    static bool load(Settings& out, wxString& error);
    static bool save(const Settings& in, wxString& error);
};
