// Speech input for voice commands.
//
// Windows: SAPI 5 in-process recogniser with a command grammar (VoiceInput_msw.cpp). It listens
// only to a headset microphone: a capture device whose form factor is Headset, or whose name
// says headset / hands-free. The default microphone and the audio interface inputs are never
// used, because they hear the monitors and the screen reader. When headsets are plugged in or
// removed it switches device, or pauses until one is there.
//
// Other platforms: not available yet (VoiceInput_generic.cpp).
#pragma once

#include <wx/event.h>
#include <wx/string.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class VoiceInput {
public:
    struct Device {
        wxString id;    // endpoint id
        wxString name;  // "Headset Microphone (Jabra Evolve 20)"
        bool headset = false;
    };
    struct Options {
        wxString wakeWord = "TotalMix";
        wxString deviceName;  // a specific microphone; empty = any headset
    };

    // 'owner' receives the callbacks on the main thread.
    explicit VoiceInput(wxEvtHandler* owner);
    ~VoiceInput();

    static bool available();
    static std::vector<Device> captureDevices();

    // Starts listening (or waiting for a headset). Status is reported through onStatus.
    void start(const Options& options);
    void stop();
    bool running() const;
    // Bound to a microphone and listening.
    bool listening() const;
    wxString deviceName() const;

    // Spoken forms of the channel names (rme::voice::spokenName) the grammar should accept.
    void setChannelNames(const std::vector<std::string>& spokenNames);

    // Recognised phrase (wake word included) and the engine's confidence 0..1.
    std::function<void(const wxString& text, double confidence)> onPhrase;
    // Device changes and problems.
    std::function<void(const wxString& text, bool problem)> onStatus;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};
