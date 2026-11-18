// The TotalMix FX OSC parameter table (RME "OscTableTotalMix", TotalMix FX 1.96, 22.07.2024).
//
// TotalMix FX addresses its OSC controls like a TouchOSC "page" layout: /1/... is the mixer
// (a bank of channel strips), /2/... the settings of one channel, /3/... reverb, echo,
// snapshots and groups, /4/... the Room EQ of one output. A few commands have no page
// ("/setSubmix" etc.). TotalMix sends data for one page at a time; sending any message of
// another page (or just "/2") switches page and makes TotalMix re-send that whole page.
//
// Most parameters have a display string, sent only by TotalMix with the suffix "Val"
// (e.g. "/1/volume1Val" = "-5.2").
#pragma once

#include <string>
#include <vector>

namespace rme {

enum class Kind {
    Trigger,       // kOSCScaleNoSend: a button; send 1.0
    Toggle,        // kOSCScaleToggle: send 1.0 to toggle; TotalMix sends back 0.0 / 1.0
    OnOff,         // kOSCScaleOnOff: send / receive 0.0 or 1.0
    ToggleButton,  // like Toggle, shown as a push button with a pressed state (snapshots)
    Fader,         // kOSCScaleFader: 0..1 in the fader curve (dB)
    Lin,           // kOSCScaleLin01: 0..1 linear
    Freq,          // kOSCScaleFreq: 0..1 logarithmic 20 Hz..20 kHz
    Direct,        // KOSCScaleDirect: the value itself (integer range minValue..maxValue)
    Text,          // kOSCScaleString: received text
    Level,         // kOSCScaleLevel: level meter 0..1, with a dB display string
};

// What TotalMix FX sends as the additional "Val" display string.
enum class Readout { None, Db, Value, Pan, Selection };

// When a change received from TotalMix FX is spoken.
enum class Announce {
    Never,   // e.g. levels, recorder time
    Remote,  // when it changed in TotalMix FX (not as the echo of our own change, not during a refresh)
    Always,  // also during a refresh: channel names after navigation, recorder state
};

// Which rows (busses) of the mixer a page-1 strip control applies to.
enum BusMask { kBusInput = 1, kBusPlayback = 2, kBusOutput = 4, kBusAll = 7 };

struct ParamDef {
    int page = 0;          // 1..4, 0 = no page
    std::string name;      // OSC name below the page, e.g. "eqGain1"
    std::string label;     // accessible / visible name, e.g. "Band 1 gain"
    std::string group;
    Kind kind = Kind::Toggle;
    Readout readout = Readout::None;
    int steps = 100;                   // Lin: slider steps over 0..1
    std::vector<std::string> choices;  // Readout::Selection: values at 0, 1/(n-1), ... 1
    int minValue = 0, maxValue = 0;    // Direct
    std::string help;                  // remarks from the RME table
    Announce announce = Announce::Remote;

    std::string address() const;  // "/2/eqGain1", or "/setSubmix" for page 0
    // Slider steps for Lin / Freq / Fader controls.
    int sliderSteps() const;
};

// Every paged parameter of pages 1 (not the per-strip ones), 2, 3 and 4.
const std::vector<ParamDef>& allParams();
std::vector<const ParamDef*> paramsForPage(int page);
std::vector<std::string> groupsForPage(int page);
const ParamDef* findParam(int page, const std::string& name);

// ---- Page 1 channel strips ---------------------------------------------------------------
struct StripParamDef {
    std::string base;   // "volume", "mute", ...
    std::string label;  // "Volume"
    Kind kind;
    Readout readout;
    bool multiToggle;   // address "/1/mute/1/<n>" instead of "/1/volume<n>"
    int busMask;        // rows where TotalMix supports it
    std::string help;
};

constexpr int kMaxBankSize = 24;

// In the order they appear in a strip, top to bottom.
const std::vector<StripParamDef>& stripParams();
const StripParamDef* findStripParam(const std::string& base);
// Address of a strip parameter for strip 'n' (1-based): "/1/volume3", "/1/mute/1/3",
// "/1/level3Left" (base "levelLeft" / "levelRight"), "/1/trackname3".
std::string stripAddress(const std::string& base, int n);

// ---- No page --------------------------------------------------------------------------
// Value 0..submix count-1: select the submix (output) shown on the Input / Playback rows.
inline const char* kSetSubmix = "/setSubmix";
// Value 0..channel count-1: page 1 bank start / page 2 channel.
inline const char* kSetBankStart = "/setBankStart";
// Value 0..bank size-1: page 2 channel relative to the page 1 bank start.
inline const char* kSetOffsetInBank = "/setOffsetInBank";
// Value 1..30: load a Quick Workspace.
inline const char* kLoadQuickWorkspace = "/loadQuickWorkspace";

// Page selection message ("/1" .. "/4", no arguments), as sent by the TouchOSC template.
std::string pageAddress(int page);

// ---- Address helpers ------------------------------------------------------------------
// Page number of "/2/eqGain1" (2), or 0.
int pageOf(const std::string& address);
// Splits off the display-string suffix: "/1/volume1Val" -> "/1/volume1", true.
bool splitValSuffix(const std::string& address, std::string& base);

}  // namespace rme
