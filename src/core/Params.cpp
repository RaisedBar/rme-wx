#include "Params.h"

#include <algorithm>

namespace rme {

std::string ParamDef::address() const {
    if (page == 0) return "/" + name;
    return "/" + std::to_string(page) + "/" + name;
}

int ParamDef::sliderSteps() const {
    switch (kind) {
        case Kind::Fader: return 142;  // 0.5 dB steps from -65 to +6 dB
        case Kind::Freq: return 200;   // about 1/20 octave per step
        case Kind::Lin:
            if (readout == Readout::Selection && choices.size() >= 2) return static_cast<int>(choices.size()) - 1;
            return std::max(1, steps);
        default: return 1;
    }
}

namespace {

using C = std::vector<std::string>;

const C kLowcutGrades = {"6 dB per octave", "12 dB per octave", "18 dB per octave", "24 dB per octave"};
const C kBand1Types = {"Bell", "Low Shelf", "High Pass", "Low Pass"};
const C kBand3Types = {"Bell", "High Shelf", "Low Pass", "High Pass"};
// Not in the RME OSC table; taken from the TotalMix FX reverb type list. TotalMix's own
// display string ("Val") is always what is shown and spoken once it arrives.
const C kReverbTypes = {"Small Room", "Medium Room", "Large Room", "Walls",     "Shorty",
                        "Attack",     "Swagger",     "Old School", "Echoistic", "8plus9",
                        "Grand Wide", "Thicker",     "Envelope",   "Gated",     "Space"};
const C kEchoTypes = {"Stereo Echo", "Stereo Cross", "Pong Echo"};

struct Builder {
    std::vector<ParamDef> v;
    int page = 0;
    std::string group;

    ParamDef& add(std::string name, std::string label, Kind kind, Readout r = Readout::None, std::string help = {}) {
        ParamDef d;
        d.page = page;
        d.name = std::move(name);
        d.label = std::move(label);
        d.group = group;
        d.kind = kind;
        d.readout = r;
        d.help = std::move(help);
        if (kind == Kind::Level || kind == Kind::Trigger) d.announce = Announce::Never;
        v.push_back(std::move(d));
        return v.back();
    }
    ParamDef& sel(std::string name, std::string label, const C& choices, std::string help = {}) {
        ParamDef& d = add(std::move(name), std::move(label), Kind::Lin, Readout::Selection, std::move(help));
        d.choices = choices;
        return d;
    }
    ParamDef& direct(std::string name, std::string label, int lo, int hi, std::string help = {}) {
        ParamDef& d = add(std::move(name), std::move(label), Kind::Direct, Readout::Value, std::move(help));
        d.minValue = lo;
        d.maxValue = hi;
        return d;
    }
    ParamDef& text(std::string name, std::string label, Announce a) {
        ParamDef& d = add(std::move(name), std::move(label), Kind::Text);
        d.announce = a;
        return d;
    }
};

std::vector<ParamDef> buildTable() {
    Builder b;

    // ---- Page 1: Mixer (the parts that are not per strip) ----
    b.page = 1;
    b.group = "Row";
    b.add("busInput", "Inputs", Kind::Toggle).announce = Announce::Always;
    b.add("busPlayback", "Playbacks", Kind::Toggle).announce = Announce::Always;
    b.add("busOutput", "Outputs", Kind::Toggle).announce = Announce::Always;
    b.group = "Navigation";
    b.add("bank-", "Previous bank", Kind::Trigger, Readout::None, "Moves the bank start back by the bank size");
    b.add("bank+", "Next bank", Kind::Trigger, Readout::None, "Moves the bank start on by the bank size");
    b.add("track-", "Previous channel", Kind::Trigger, Readout::None, "Moves the bank start back by one channel");
    b.add("track+", "Next channel", Kind::Trigger, Readout::None, "Moves the bank start on by one channel");
    b.text("labelSubmix", "Submix", Announce::Always);
    b.group = "Control Room";
    b.add("mastervolume", "Main out volume", Kind::Fader, Readout::Db);
    b.add("mainDim", "Dim", Kind::Toggle);
    b.add("mainRecall", "Recall", Kind::Toggle, Readout::None, "Recall the stored Main Out volume");
    b.add("mainMono", "Mono", Kind::Toggle);
    b.add("mainSpeakerB", "Speaker B", Kind::Toggle);
    b.add("speakerBLinked", "Speaker B linked", Kind::Toggle, Readout::None, "Speaker B volume linked to Main");
    b.add("mainMuteFx", "Mute FX", Kind::Toggle, Readout::None, "Mute the FX return on Main Out");
    b.add("mainExtIn", "External In", Kind::Toggle);
    b.add("mainTalkback", "Talkback", Kind::Toggle);
    b.group = "Global";
    b.add("globalMute", "Global mute", Kind::Toggle, Readout::None, "Mute enable (globally)");
    b.add("globalSolo", "Global solo", Kind::Toggle, Readout::None, "Solo enable (globally)");
    b.add("trim", "Trim gains", Kind::Toggle, Readout::None, "Trim mode for all channels");

    // ---- Page 2: Channel settings ----
    b.page = 2;
    b.group = "Row";
    b.add("busInput", "Inputs", Kind::Toggle).announce = Announce::Always;
    b.add("busPlayback", "Playbacks", Kind::Toggle).announce = Announce::Always;
    b.add("busOutput", "Outputs", Kind::Toggle).announce = Announce::Always;
    b.group = "Channel";
    b.add("track-", "Previous channel", Kind::Trigger);
    b.add("track+", "Next channel", Kind::Trigger);
    b.text("trackname", "Channel", Announce::Always);
    b.text("labelSubmix", "Submix", Announce::Always);
    b.add("levelLeft", "Level left", Kind::Level, Readout::Db);
    b.add("levelRight", "Level right", Kind::Level, Readout::Db);
    b.group = "Fader";
    b.add("volume", "Volume", Kind::Fader, Readout::Db);
    b.add("pan", "Pan", Kind::Lin, Readout::Pan).steps = 200;
    b.add("mute", "Mute", Kind::Toggle, Readout::None, "Only effective in TotalMix (monitoring)");
    b.add("solo", "Solo", Kind::Toggle, Readout::None, "PFL in PFL mode");
    b.group = "Settings";
    b.add("gain", "Gain", Kind::Lin, Readout::Value, "Microphone or digital input gain; scale depends on channel and device");
    b.add("gainRight", "Gain right", Kind::Lin, Readout::Value, "Sets the right channel of a stereo channel differently");
    b.add("phantom", "48V", Kind::Toggle, Readout::None, "Phantom power; certain inputs only (device dependent)");
    b.add("instrument", "Instrument", Kind::Toggle, Readout::None, "Certain inputs only (device dependent)");
    b.add("pad", "Pad", Kind::Toggle, Readout::None, "Certain inputs only (device dependent)");
    b.add("autoset", "AutoSet", Kind::Toggle, Readout::None, "AutoSet gain; certain inputs only (device dependent)");
    b.direct("refLevel", "Reference level", 0, 3, "Analog reference level, 0 to 3, device and channel dependent");
    b.add("phase", "Phase invert", Kind::Toggle, Readout::None, "Phase invert left or mono");
    b.add("phaseRight", "Phase invert right", Kind::Toggle);
    b.add("stereo", "Stereo", Kind::Toggle, Readout::None, "Set channel stereo or mono");
    b.add("msProc", "M/S processing", Kind::Toggle, Readout::None, "Inputs; only effective in TotalMix (monitoring)");
    b.add("width", "Width", Kind::Lin, Readout::Value, "Only effective in TotalMix (monitoring)").steps = 200;
    b.add("reverbSend", "FX send", Kind::Lin, Readout::Db, "Reverb / echo send; inputs and playbacks only");
    b.add("reverbReturn", "FX return", Kind::Lin, Readout::Db, "Outputs only");
    b.add("cue", "Cue", Kind::Toggle, Readout::None, "Outputs only");
    b.add("loopback", "Loopback", Kind::Toggle, Readout::None, "Outputs only");
    b.add("talkbackSel", "Talkback", Kind::Toggle, Readout::None, "Include channel in talkback; outputs only");
    b.add("noTrim", "No trim", Kind::Toggle, Readout::None, "Exclude channel from trim; outputs only");
    b.group = "Recorder";
    b.add("recordEnable", "Record enable", Kind::Toggle);
    b.direct("playChannel", "Play channel", 0, 64, "Playback track in the file for this channel; 0 is off");
    b.group = "Low Cut";
    b.add("lowcutEnable", "Low cut", Kind::Toggle);
    b.sel("lowcutGrade", "Slope", kLowcutGrades);
    b.add("lowcutFreq", "Frequency", Kind::Freq, Readout::Value);
    b.group = "EQ";
    b.add("eqEnable", "EQ", Kind::Toggle);
    b.sel("eqType1", "Band 1 type", kBand1Types, "Device dependent: Bell, Low Shelf, High Pass, (Low Pass)");
    b.add("eqGain1", "Band 1 gain", Kind::Lin, Readout::Db);
    b.add("eqFreq1", "Band 1 frequency", Kind::Freq, Readout::Value);
    b.add("eqQ1", "Band 1 Q", Kind::Lin, Readout::Value);
    b.add("eqGain2", "Band 2 gain", Kind::Lin, Readout::Db);
    b.add("eqFreq2", "Band 2 frequency", Kind::Freq, Readout::Value);
    b.add("eqQ2", "Band 2 Q", Kind::Lin, Readout::Value);
    b.sel("eqType3", "Band 3 type", kBand3Types, "Device dependent: Bell, High Shelf, (Low Pass, High Pass)");
    b.add("eqGain3", "Band 3 gain", Kind::Lin, Readout::Db);
    b.add("eqFreq3", "Band 3 frequency", Kind::Freq, Readout::Value);
    b.add("eqQ3", "Band 3 Q", Kind::Lin, Readout::Value);
    b.group = "Dynamics";
    b.add("compexpEnable", "Dynamics", Kind::Toggle, Readout::None, "Compressor / expander enable");
    b.add("compTrsh", "Compressor threshold", Kind::Lin, Readout::Db);
    b.add("compRatio", "Compressor ratio", Kind::Lin, Readout::Value);
    b.add("expTrsh", "Expander threshold", Kind::Lin, Readout::Db);
    b.add("expRatio", "Expander ratio", Kind::Lin, Readout::Value);
    b.add("compexpAttack", "Attack", Kind::Lin, Readout::Value);
    b.add("compexpRelease", "Release", Kind::Lin, Readout::Value);
    b.add("compexpGain", "Make-up gain", Kind::Lin, Readout::Db);
    b.group = "Auto Level";
    b.add("alevEnable", "Auto level", Kind::Toggle);
    b.add("alevMaxgain", "Max gain", Kind::Lin, Readout::Db);
    b.add("alevHeadroom", "Headroom", Kind::Lin, Readout::Db);
    b.add("alevRisetime", "Rise time", Kind::Lin, Readout::Value);

    // ---- Page 3: Reverb, Echo, Snapshots, Groups ----
    b.page = 3;
    b.group = "Reverb";
    b.add("reverbEnable", "Reverb", Kind::Toggle);
    b.sel("reverbType", "Type", kReverbTypes);
    b.add("reverbPredelay", "Pre delay", Kind::Lin, Readout::Value);
    b.add("reverbLowcut", "Low cut", Kind::Freq, Readout::Value);
    b.add("reverbHighcut", "High cut", Kind::Freq, Readout::Value);
    b.add("reverbRoomscale", "Room scale", Kind::Lin, Readout::Value);
    b.add("reverbAttack", "Attack", Kind::Lin, Readout::Value, "Envelope types only");
    b.add("reverbHold", "Hold", Kind::Lin, Readout::Value, "Envelope types only");
    b.add("reverbRelease", "Release", Kind::Lin, Readout::Value, "Envelope types only");
    b.add("reverbTime", "Reverb time", Kind::Lin, Readout::Value, "Space type only");
    b.add("reverbHighdamp", "High damp", Kind::Freq, Readout::Value, "Space type only");
    b.add("reverbSmooth", "Smooth", Kind::Lin, Readout::Value);
    b.add("reverbWidth", "Width", Kind::Lin, Readout::Value);
    b.add("reverbVolume", "Volume", Kind::Lin, Readout::Db);
    b.group = "Echo";
    b.add("echoEnable", "Echo", Kind::Toggle);
    b.sel("echoType", "Type", kEchoTypes);
    b.add("echoDelaytime", "Delay time", Kind::Lin, Readout::Value);
    b.add("echoFeedback", "Feedback", Kind::Lin, Readout::Value);
    b.add("echoWidth", "Width", Kind::Lin, Readout::Value);
    b.add("echoVolume", "Volume", Kind::Lin, Readout::Db);
    b.group = "Snapshots";
    // TouchOSC multi-push layout: snapshot 1 is "/3/snapshots/8/1", snapshot 8 "/3/snapshots/1/1".
    for (int s = 1; s <= 8; ++s)
        b.add("snapshots/" + std::to_string(9 - s) + "/1", "Snapshot " + std::to_string(s), Kind::ToggleButton,
              Readout::None, "Loads the snapshot");
    for (const char* g : {"mute", "solo", "fader"}) {
        const std::string G = std::string(1, static_cast<char>(g[0] - 'a' + 'A')) + (g + 1);
        b.group = G + " Groups";
        for (int n = 1; n <= 4; ++n)
            b.add(std::string(g) + "Groups/" + std::to_string(5 - n) + "/1", G + " group " + std::to_string(n),
                  Kind::Toggle, Readout::None, "Enable " + G + " group " + std::to_string(n));
    }
    b.group = "Recorder";
    b.add("recordRecordStart", "Record", Kind::Trigger, Readout::None, "Start recording (DURec)");
    b.add("recordPlayPause", "Play / Pause", Kind::Trigger, Readout::None, "Play or pause (DURec)");
    b.add("recordStop", "Stop", Kind::Trigger, Readout::None, "Stop playing or recording (DURec)");
    b.text("recordState", "State", Announce::Always);
    b.text("recordTime", "Time", Announce::Never);
    b.group = "Global";
    b.add("globalMute", "Global mute", Kind::Toggle);
    b.add("globalSolo", "Global solo", Kind::Toggle);
    b.add("undo", "Undo", Kind::Trigger);
    b.add("redo", "Redo", Kind::Trigger);

    // ---- Page 4: Room EQ (selecting page 4 selects the output row) ----
    b.page = 4;
    b.group = "Channel";
    b.add("track-", "Previous channel", Kind::Trigger);
    b.add("track+", "Next channel", Kind::Trigger);
    b.text("trackname", "Channel", Announce::Always);
    b.add("leftChannel", "Left channel", Kind::Toggle, Readout::None,
          "With both left and right on, values are shown from the left channel and sent to both");
    b.add("rightChannel", "Right channel", Kind::Toggle);
    b.add("reqEnable", "Room EQ", Kind::Toggle);
    b.add("reqDelay", "Delay", Kind::Lin, Readout::Value);
    b.add("reqVolumeCorr", "Volume correction", Kind::Lin, Readout::Value);
    for (int band = 1; band <= 9; ++band) {
        const std::string n = std::to_string(band);
        b.group = "Band " + n;
        if (band == 1) b.sel("reqType1", "Band 1 type", kBand1Types);
        if (band == 8) b.sel("reqType8", "Band 8 type", kBand3Types);
        if (band == 9) b.sel("reqType9", "Band 9 type", kBand3Types);
        b.add("reqGain" + n, "Band " + n + " gain", Kind::Lin, Readout::Db);
        b.add("reqFreq" + n, "Band " + n + " frequency", Kind::Freq, Readout::Value);
        b.add("reqQ" + n, "Band " + n + " Q", Kind::Lin, Readout::Value);
    }
    return b.v;
}

}  // namespace

const std::vector<ParamDef>& allParams() {
    static const std::vector<ParamDef> table = buildTable();
    return table;
}

std::vector<const ParamDef*> paramsForPage(int page) {
    std::vector<const ParamDef*> out;
    for (const auto& d : allParams())
        if (d.page == page) out.push_back(&d);
    return out;
}

std::vector<std::string> groupsForPage(int page) {
    std::vector<std::string> out;
    for (const auto& d : allParams())
        if (d.page == page && std::find(out.begin(), out.end(), d.group) == out.end()) out.push_back(d.group);
    return out;
}

const ParamDef* findParam(int page, const std::string& name) {
    for (const auto& d : allParams())
        if (d.page == page && d.name == name) return &d;
    return nullptr;
}

const std::vector<StripParamDef>& stripParams() {
    static const std::vector<StripParamDef> t = {
        {"trackname", "Name", Kind::Text, Readout::None, false, kBusAll, ""},
        {"micgain", "Gain", Kind::Lin, Readout::Db, false, kBusInput,
         "Microphone or digital input gain; certain inputs only (device dependent)"},
        {"phantom", "48V", Kind::OnOff, Readout::None, true, kBusInput, "Phantom power; certain inputs only"},
        {"pan", "Pan", Kind::Lin, Readout::Pan, false, kBusAll, "Pan / balance"},
        {"mute", "Mute", Kind::OnOff, Readout::None, true, kBusAll, "Only effective in TotalMix (monitoring)"},
        {"solo", "Solo", Kind::OnOff, Readout::None, true, kBusInput | kBusPlayback, "PFL in PFL mode"},
        {"cue", "Cue", Kind::OnOff, Readout::None, true, kBusOutput, "Cue on the Cue-To channel disables Cue"},
        {"select", "Select", Kind::OnOff, Readout::None, true, kBusAll, "Selection for an ad-hoc fader group"},
        {"volume", "Volume", Kind::Fader, Readout::Db, false, kBusAll, ""},
        {"levelLeft", "Level", Kind::Level, Readout::Db, false, kBusAll, ""},
        {"levelRight", "Level right", Kind::Level, Readout::Db, false, kBusAll, ""},
    };
    return t;
}

const StripParamDef* findStripParam(const std::string& base) {
    for (const auto& s : stripParams())
        if (s.base == base) return &s;
    return nullptr;
}

std::string stripAddress(const std::string& base, int n) {
    const std::string num = std::to_string(n);
    if (base == "levelLeft") return "/1/level" + num + "Left";
    if (base == "levelRight") return "/1/level" + num + "Right";
    const StripParamDef* s = findStripParam(base);
    if (s && s->multiToggle) return "/1/" + base + "/1/" + num;
    return "/1/" + base + num;
}

std::string pageAddress(int page) { return "/" + std::to_string(page); }

int pageOf(const std::string& address) {
    if (address.size() >= 3 && address[0] == '/' && address[1] >= '1' && address[1] <= '9' && address[2] == '/')
        return address[1] - '0';
    return 0;
}

bool splitValSuffix(const std::string& address, std::string& base) {
    if (address.size() > 4 && address.compare(address.size() - 3, 3, "Val") == 0) {
        base = address.substr(0, address.size() - 3);
        return true;
    }
    base = address;
    return false;
}

}  // namespace rme
