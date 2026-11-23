// Minimal self-contained tests for TotalMixCore (no TotalMix FX needed).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "CommandSequencer.h"
#include "Link.h"
#include "Osc.h"
#include "Params.h"
#include "PortCheck.h"
#include "Scale.h"
#include "UdpSocket.h"
#include "VoiceCommand.h"

using namespace rme;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                 \
    } while (0)

static bool near(double a, double b, double eps = 1e-3) { return std::fabs(a - b) <= eps; }

static void testOscRoundTrip() {
    OscMessage m("/1/volume1");
    m.args.push_back(OscArg::makeFloat(0.5f));
    m.args.push_back(OscArg::makeInt(-3));
    m.args.push_back(OscArg::makeString("AN 1"));
    const Bytes b = encode(m);
    CHECK(b.size() % 4 == 0);
    // "/1/volume1\0\0" (12) + ",fis\0\0\0\0" (8) + 4 + 4 + "AN 1\0\0\0\0" (8)
    CHECK(b.size() == 36);
    std::vector<OscMessage> out;
    CHECK(decodePacket(b, out));
    CHECK(out.size() == 1);
    if (out.size() == 1) {
        CHECK(out[0].address == "/1/volume1");
        CHECK(out[0].args.size() == 3);
        CHECK(out[0].number() == 0.5f);
        CHECK(out[0].text() == "AN 1");
        CHECK(out[0].args[1].i == -3);
    }
    // No arguments (page selection): still has a type tag string ",".
    const Bytes p = encode(OscMessage(pageAddress(2)));
    CHECK(p.size() == 8);
    out.clear();
    CHECK(decodePacket(p, out) && out.size() == 1 && out[0].address == "/2" && out[0].args.empty());
    // Old-style message without a type tag string.
    const uint8_t old[] = {'/', '1', 0, 0};
    out.clear();
    CHECK(decodePacket(old, sizeof old, out) && out.size() == 1 && out[0].address == "/1");
    CHECK(toString(OscMessage::withFloat("/1/mute/1/2", 1.0f)) == "/1/mute/1/2 1");
}

static Bytes bundleOf(const std::vector<Bytes>& elements) {
    Bytes b = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0, 0, 0, 0, 0, 0, 0, 0, 1};
    for (const auto& e : elements) {
        const uint32_t n = static_cast<uint32_t>(e.size());
        b.push_back(static_cast<uint8_t>(n >> 24));
        b.push_back(static_cast<uint8_t>(n >> 16));
        b.push_back(static_cast<uint8_t>(n >> 8));
        b.push_back(static_cast<uint8_t>(n));
        b.insert(b.end(), e.begin(), e.end());
    }
    return b;
}

static void testOscBundles() {
    OscMessage a = OscMessage::withFloat("/1/volume1", 0.25f);
    OscMessage v("/1/volume1Val");
    v.args.push_back(OscArg::makeString("-12.3"));
    const Bytes inner = bundleOf({encode(v)});
    const Bytes b = bundleOf({encode(a), inner});
    std::vector<OscMessage> out;
    CHECK(decodePacket(b, out));
    CHECK(out.size() == 2);
    if (out.size() == 2) {
        CHECK(out[0].address == "/1/volume1" && out[0].number() == 0.25f);
        CHECK(out[1].address == "/1/volume1Val" && out[1].text() == "-12.3");
    }
    // Malformed: element longer than the packet, unterminated address, bad type.
    Bytes bad = b;
    bad[19] = 0xFF;
    out.clear();
    CHECK(!decodePacket(bad, out));
    const uint8_t unterminated[] = {'/', 'a', 'b', 'c'};
    out.clear();
    CHECK(!decodePacket(unterminated, sizeof unterminated, out));
    Bytes badType = encode(OscMessage::withFloat("/x", 1.0f));
    badType[5] = 'Q';  // ",f" -> ",Q"
    out.clear();
    CHECK(!decodePacket(badType, out));
    out.clear();
    CHECK(!decodePacket(nullptr, 0, out));
}

static void testScale() {
    using namespace rme::scale;
    CHECK(near(faderToDb(0.0), -65.0));
    CHECK(near(faderToDb(1.0), 6.0, 0.01));
    // 0 dB is at fader position 836 (RME: TotalMix's unity gain).
    CHECK(near(dbToFader(0.0) * 1023.0, 836.0, 0.5));
    for (double db = -65.0; db <= 6.0; db += 0.5) CHECK(near(faderToDb(dbToFader(db)), db, 0.01));
    CHECK(dbToFader(-100.0) == 0.0 && dbToFader(100.0) == 1.0);
    CHECK(near(normToFreq(0.0), 20.0) && near(normToFreq(1.0), 20000.0, 0.01));
    CHECK(near(normToFreq(freqToNorm(1000.0)), 1000.0, 0.01));
    CHECK(formatDb(-65.0) == "-\xE2\x88\x9E dB");
    CHECK(formatDb(0.0) == "+0.0 dB");
    CHECK(formatDb(-5.55) == "-5.6 dB" || formatDb(-5.55) == "-5.5 dB");
    CHECK(formatFreq(120.0) == "120 Hz");
    CHECK(formatFreq(1250.0) == "1.25 kHz");
    CHECK(formatPan(0.5) == "Center");
    CHECK(formatPan(0.0) == "L100");
    CHECK(formatPan(1.0) == "R100");
    CHECK(formatPan(0.525) == "R5");
    CHECK(formatPercent(0.37) == "37%");
    CHECK(tidyDisplayText("  -oo ") == "-\xE2\x88\x9E");
    CHECK(tidyDisplayText("") == "");
}

static void testParams() {
    std::set<std::string> addresses;
    for (const auto& d : allParams()) {
        CHECK(d.page >= 1 && d.page <= 4);
        CHECK(addresses.insert(d.address()).second);  // unique
        CHECK(!d.label.empty() && !d.group.empty());
        CHECK(pageOf(d.address()) == d.page);
        if (d.readout == Readout::Selection) {
            CHECK(d.kind == Kind::Lin);
            CHECK(d.choices.size() >= 2);
            CHECK(d.sliderSteps() == static_cast<int>(d.choices.size()) - 1);
        }
        if (d.kind == Kind::Direct) CHECK(d.minValue < d.maxValue);
        std::string base;
        CHECK(!splitValSuffix(d.address(), base));  // no name ends in "Val"
    }
    // Spot checks against the RME table.
    CHECK(findParam(1, "mastervolume") && findParam(1, "mastervolume")->kind == Kind::Fader);
    CHECK(findParam(2, "eqFreq3") && findParam(2, "eqFreq3")->kind == Kind::Freq);
    CHECK(findParam(2, "refLevel") && findParam(2, "refLevel")->maxValue == 3);
    CHECK(findParam(3, "snapshots/8/1") && findParam(3, "snapshots/8/1")->label == "Snapshot 1");
    CHECK(findParam(3, "snapshots/1/1") && findParam(3, "snapshots/1/1")->label == "Snapshot 8");
    CHECK(findParam(3, "muteGroups/4/1") && findParam(3, "muteGroups/4/1")->label == "Mute group 1");
    CHECK(findParam(3, "faderGroups/1/1") && findParam(3, "faderGroups/1/1")->label == "Fader group 4");
    CHECK(findParam(4, "reqQ9") && findParam(4, "reqType8") && !findParam(4, "reqType2"));
    CHECK(paramsForPage(4).size() == 8 + 3 + 9 * 3);
    CHECK(groupsForPage(2).front() == "Row");

    CHECK(stripAddress("volume", 3) == "/1/volume3");
    CHECK(stripAddress("mute", 3) == "/1/mute/1/3");
    CHECK(stripAddress("phantom", 12) == "/1/phantom/1/12");
    CHECK(stripAddress("levelLeft", 2) == "/1/level2Left");
    CHECK(stripAddress("levelRight", 2) == "/1/level2Right");
    CHECK(stripAddress("trackname", 8) == "/1/trackname8");
    CHECK(stripAddress("micgain", 1) == "/1/micgain1");

    std::string base;
    CHECK(splitValSuffix("/1/volume1Val", base) && base == "/1/volume1");
    CHECK(splitValSuffix("/1/level2LeftVal", base) && base == "/1/level2Left");
    CHECK(!splitValSuffix("/1/volume1", base) && base == "/1/volume1");
    CHECK(pageOf("/3/reverbType") == 3 && pageOf("/setSubmix") == 0 && pageOf("/2") == 0);
}

static void testLink() {
    Link l;
    CHECK(l.tick(0) == Link::Event::None);
    l.onReceive(100, 5, 5);
    CHECK(l.receiving());
    CHECK(l.tick(120) == Link::Event::Started);
    CHECK(l.tick(140) == Link::Event::None);
    // Occasional traffic only: silence is not reported.
    CHECK(l.tick(100000) == Link::Event::None);

    Link s;  // steady traffic (levels) then silence
    for (int64_t t = 0; t <= 3000; t += 100) s.onReceive(t, 10, 0);
    CHECK(s.tick(3000) == Link::Event::Started);
    CHECK(s.tick(5000) == Link::Event::None);
    CHECK(s.tick(6100) == Link::Event::Stopped);
    CHECK(!s.receiving());
    s.onReceive(7000, 1, 1);
    CHECK(s.tick(7000) == Link::Event::Started);

    Link c;
    c.beginCheck(0);
    CHECK(c.tick(1000) == Link::Event::None);
    CHECK(c.tick(1600) == Link::Event::CheckFailed);
    c.beginCheck(2000);
    c.onReceive(2100, 3, 3);
    CHECK(c.tick(2120) == Link::Event::CheckOk);
    CHECK(c.tick(2140) == Link::Event::None);  // "started" is covered by the check result

    // Only level meters during a check: TotalMix sends to us but did not get our request.
    Link m;
    m.beginCheck(0);
    for (int64_t t = 0; t <= 1600; t += 100) m.onReceive(t, 16, 0);
    Link::Event e = Link::Event::None;
    for (int64_t t = 0; t <= 1600 && e != Link::Event::CheckFailed; t += 20) e = m.tick(t);
    CHECK(e == Link::Event::CheckFailed);
    CHECK(m.lastCheckGotData());
    CHECK(m.receiving());
}

static void testUdpLoopback() {
    // Send to ourselves on the loopback interface.
    UdpSocket a;
    std::string err;
    uint16_t port = 0;
    for (uint16_t p = 47901; p < 47950 && !a.isOpen(); ++p)
        if (a.open(p, err)) port = p;
    CHECK(a.isOpen());
    if (!a.isOpen()) return;
    CHECK(a.setDestination("127.0.0.1", port, err));
    CHECK(a.send(encode(OscMessage::withFloat("/1/bank+", 1.0f)), err));
    std::vector<OscMessage> got;
    for (int i = 0; i < 200 && got.empty(); ++i) {
        a.receive([&](const uint8_t* d, size_t n) { decodePacket(d, n, got); });
    }
    CHECK(got.size() == 1 && got[0].address == "/1/bank+");
    UdpSocket b;
    CHECK(!b.open(port, err));  // already in use
    CHECK(!err.empty());
}

static void testPortCheck() {
    using R = SendPortCheck::Result;
    const std::vector<PortOwner> none;
    const std::vector<PortOwner> sentinel = {{5632, "sntlkeyssrvr.exe"}};
    const std::vector<PortOwner> totalmix = {{18460, "TotalMixFX_x64.exe"}};
    const std::vector<PortOwner> both = {{5632, "sntlkeyssrvr.exe"}, {18460, "TotalMixFX_x64.exe"}};
    const std::vector<PortOwner> self = {{42, "TotalMixAccess.exe"}};
    const std::vector<PortOwner> unnamed = {{77, ""}};
    CHECK(classifySendPort(false, true, sentinel, 42).result == R::NotChecked);  // TotalMix on another computer
    CHECK(classifySendPort(true, false, sentinel, 42).result == R::NotChecked);  // cannot inspect ports
    CHECK(classifySendPort(true, true, none, 42).result == R::Free);
    const auto s = classifySendPort(true, true, sentinel, 42);
    CHECK(s.result == R::Other && s.process == "sntlkeyssrvr.exe");
    CHECK(classifySendPort(true, true, totalmix, 42).result == R::TotalMix);
    CHECK(classifySendPort(true, true, both, 42).result == R::TotalMix);
    CHECK(classifySendPort(true, true, self, 42).result == R::Self);
    CHECK(classifySendPort(true, true, unnamed, 42).process == "process 77");

    CHECK(isLocalHost("127.0.0.1"));
    CHECK(isLocalHost("localhost"));
    CHECK(!isLocalHost("192.0.2.1"));  // TEST-NET-1: never assigned to this computer

    // A port we hold ourselves is reported with our process id.
    UdpSocket u;
    std::string err;
    uint16_t port = 0;
    for (uint16_t p = 47951; p < 48000 && !u.isOpen(); ++p)
        if (u.open(p, err)) port = p;
    if (u.isOpen()) {
        bool supported = false;
        const auto owners = udpPortOwners(port, supported);
#ifdef _WIN32
        CHECK(supported);
        CHECK(owners.size() == 1 && owners[0].pid == currentProcessId());
        CHECK(checkSendPort("127.0.0.1", port).result == R::Self);
#endif
    }
}

static void testVoiceParse() {
    using namespace rme::voice;
    const std::vector<NamedChannel> names = {{Bus::Input, 0, "Vocal"}, {Bus::Playback, 3, "Kick Drum"},
                                             {Bus::Input, 2, "AN 1/2"}};
    auto p = [&](const char* t) { return parseVoiceCommand(t, names); };

    auto c = p("TotalMix snapshot three");
    CHECK(c && c->action == Action::Snapshot && c->snapshot == 3);
    CHECK(p("snapshot 8") && p("snapshot 8")->snapshot == 8);
    CHECK(!p("totalmix snapshot nine"));
    CHECK(!p("totalmix snapshot"));

    c = p("totalmix input five volume minus ten dB");
    CHECK(c && c->action == Action::Volume && c->channel.bus == Bus::Input && c->channel.number == 5 &&
          near(c->value, -10));
    c = p("totalmix playback twenty one volume minus 3 point five");
    CHECK(c && c->channel.bus == Bus::Playback && c->channel.number == 21 && near(c->value, -3.5));
    c = p("TotalMix output 2 volume -12.5 dB");
    CHECK(c && c->channel.bus == Bus::Output && near(c->value, -12.5));
    c = p("totalmix input 1 volume plus three");
    CHECK(c && near(c->value, 3));
    CHECK(!p("totalmix input 1 volume plus ten"));  // above +6 dB
    CHECK(!p("totalmix input 1 volume ten"));       // no sign: never raises by mistake
    c = p("totalmix input 1 volume zero");
    CHECK(c && c->action == Action::Volume && near(c->value, 0));
    c = p("totalmix input 1 volume off");
    CHECK(c && c->value <= scale::kFaderMinDb);
    c = p("totalmix input 1 volume minus seventy");
    CHECK(c && c->value <= scale::kFaderMinDb);
    c = p("totalmix input 4 volume up 3 dB");
    CHECK(c && c->action == Action::VolumeRelative && near(c->value, 3));
    c = p("totalmix input 4 volume down twenty");
    CHECK(c && near(c->value, -kMaxRelativeDb));
    CHECK(!p("totalmix input 0 volume zero"));
    CHECK(!p("totalmix input 97 volume zero"));

    c = p("totalmix input 3 mute");
    CHECK(c && c->action == Action::Mute && c->channel.number == 3);
    CHECK(p("totalmix input 3 unmute") && p("totalmix input 3 unmute")->action == Action::Unmute);
    c = p("totalmix input 3 pan left fifty");
    CHECK(c && c->action == Action::Pan && near(c->value, -50));
    c = p("totalmix input 3 pan right one hundred");
    CHECK(c && near(c->value, 100));
    c = p("totalmix input 3 pan centre");
    CHECK(c && near(c->value, 0));
    CHECK(!p("totalmix input 3 pan left 101"));

    c = p("totalmix input 6 effects send minus twelve");
    CHECK(c && c->action == Action::FxSend && near(c->value, -12));
    c = p("totalmix input 6 FX send off");
    CHECK(c && c->action == Action::FxSend && c->value <= scale::kFaderMinDb);
    c = p("totalmix input 6 send minus 6");
    CHECK(c && c->action == Action::FxSend);

    c = p("totalmix reverb off");
    CHECK(c && c->action == Action::Reverb && !c->on);
    c = p("totalmix echo on");
    CHECK(c && c->action == Action::Echo && c->on);
    CHECK(p("totalmix undo") && p("totalmix undo")->action == Action::Undo);
    CHECK(p("totalmix redo") && p("totalmix redo")->action == Action::Redo);
    CHECK(p("totalmix stop listening") && p("totalmix stop listening")->action == Action::StopListening);

    // Names.
    c = p("totalmix vocal volume minus six");
    CHECK(c && c->channel.byName() && c->channel.name == "Vocal" && c->channel.bus == Bus::Input);
    c = p("totalmix kick drum mute");
    CHECK(c && c->channel.number == 3 && c->channel.bus == Bus::Playback && c->channel.name == "Kick Drum");
    c = p("totalmix an one two pan center");
    CHECK(c && c->channel.name == "AN 1/2");
    c = p("totalmix playback kick drum unmute");
    CHECK(c && c->channel.name == "Kick Drum");
    CHECK(!p("totalmix output vocal mute"));  // Vocal is an input
    CHECK(!p("totalmix bass mute"));

    // Junk.
    CHECK(!p(""));
    CHECK(!p("mixer"));
    CHECK(!p("totalmix input 3 volume minus ten banana"));
    CHECK(!p("hello world"));

    // Wake word: "TotalMix" may be heard as one word or two.
    CHECK(p("total mix snapshot 2") && p("total mix snapshot 2")->snapshot == 2);
    CHECK(p("TotalMix, input 3 mute") && p("TotalMix, input 3 mute")->channel.number == 3);
    CHECK(!p("mixer snapshot 2"));  // the old wake word is no longer stripped
    {
        const auto forms = wakeWordForms("TotalMix");
        CHECK(forms.size() == 2 && forms[0] == "total mix" && forms[1] == "totalmix");
        CHECK(wakeWordForms("Mixer").size() == 1 && wakeWordForms("Mixer")[0] == "mixer");
        CHECK(wakeWordForms("Hey Desk").size() == 1);
        const auto c2 = parseVoiceCommand("hey desk reverb on", names, "Hey Desk");
        CHECK(c2 && c2->action == Action::Reverb && c2->on);
    }

    CHECK(numberWords(0) == "zero" && numberWords(21) == "twenty one" && numberWords(40) == "forty" &&
          numberWords(100) == "one hundred");
    CHECK(spokenName("AN 1/2") == "an one two");
    CHECK(spokenName("Vox-L") == "vox l");
    CHECK(spokenName("ADAT12") == "adat twelve");
    CHECK(spokenName("--").empty());

    CHECK(describe(*p("totalmix input 5 volume minus 10")) == "Input 5 volume -10.0 dB");
    CHECK(describe(*p("totalmix snapshot 2")) == "Snapshot 2");
    CHECK(describe(*p("totalmix input 5 pan left 30")) == "Input 5 pan left 30");
    CHECK(describe(*p("totalmix vocal mute")) == "Vocal mute");

    CHECK(parseDisplayDb("-12.3") && near(*parseDisplayDb("-12.3"), -12.3));
    CHECK(parseDisplayDb("+0.0 dB") && near(*parseDisplayDb("+0.0 dB"), 0.0));
    CHECK(parseDisplayDb("-oo") && *parseDisplayDb("-oo") <= scale::kFaderMinDb);
    CHECK(!parseDisplayDb("off"));
}

// A fake TotalMix FX for the sequencer: it tracks page, row and selected channel, and answers
// page requests with that page's messages.
namespace {
struct FakeTotalMix {
    int page = 1, bus = 0, bankStart = 0, offset = 0;
    std::vector<std::string> names[3];  // per row; "" = no channel (right half of a stereo pair)
    float volume[3][16] = {}, pan[3][16] = {}, mute[3][16] = {}, send[3][16] = {};
    float reverb = 0, echo = 1;
    int snapshotLoaded = 0, undos = 0;
    bool silent = false;
    std::vector<OscMessage> outbox;
    std::vector<std::string> log;

    int channel() const { return bankStart + offset; }
    void say(const std::string& a, float v) { outbox.push_back(OscMessage::withFloat(a, v)); }
    void sayText(const std::string& a, const std::string& t) {
        OscMessage m(a);
        m.args.push_back(OscArg::makeString(t));
        outbox.push_back(m);
    }
    static std::string sendText(float v) {  // made-up FX send curve: -65 .. 0 dB, linear
        if (v <= 0.0f) return "-oo";
        char b[16];
        std::snprintf(b, sizeof b, "%.1f", -65.0 + 65.0 * v);
        return b;
    }
    void sendPage(int p) {
        if (silent) return;
        page = p;
        if (p == 1) {
            for (int k = 0; k < 8; ++k)
                sayText("/1/trackname" + std::to_string(k + 1),
                        bankStart + k < static_cast<int>(names[bus].size()) ? names[bus][bankStart + k] : "");
        } else if (p == 2) {
            const int c = channel();
            sayText("/2/trackname", names[bus][c]);
            say("/2/volume", volume[bus][c]);
            say("/2/pan", pan[bus][c]);
            say("/2/mute", mute[bus][c]);
            say("/2/levelLeft", 0.1f);
        } else if (p == 3) {
            say("/3/reverbEnable", reverb);
            say("/3/echoEnable", echo);
        }
    }
    bool valid(int c) const { return c >= 0 && c < static_cast<int>(names[bus].size()) && !names[bus][c].empty(); }
    void receive(const OscMessage& m) {
        log.push_back(toString(m));
        const std::string& a = m.address;
        const float v = m.number();
        const int c = channel();
        if (a == "/1" || a == "/2" || a == "/3") {
            sendPage(a[1] - '0');
        } else if (a.rfind("/1/bus", 0) == 0) {
            bus = a == "/1/busInput" ? 0 : a == "/1/busPlayback" ? 1 : 2;
            offset = 0;
            sendPage(1);
        } else if (a == "/setBankStart") {
            if (valid(static_cast<int>(v))) {
                bankStart = static_cast<int>(v);
                offset = 0;
                if (page == 1) sendPage(1);
            }
        } else if (a == "/setOffsetInBank") {
            offset = static_cast<int>(v);
        } else if (a == "/2/volume") {
            volume[bus][c] = v;
        } else if (a == "/2/pan") {
            pan[bus][c] = v;
        } else if (a == "/2/mute") {
            mute[bus][c] = mute[bus][c] > 0.5f ? 0.0f : 1.0f;
        } else if (a == "/2/reverbSend") {
            send[bus][c] = v;
            sayText("/2/reverbSendVal", sendText(v));
        } else if (a == "/3/reverbEnable") {
            reverb = reverb > 0.5f ? 0.0f : 1.0f;
        } else if (a == "/3/echoEnable") {
            echo = echo > 0.5f ? 0.0f : 1.0f;
        } else if (a.rfind("/3/snapshots/", 0) == 0) {
            snapshotLoaded = 9 - (a[13] - '0');
        } else if (a == "/3/undo") {
            ++undos;
        }
    }
};

struct SeqHarness {
    FakeTotalMix tm;
    voice::CommandSequencer seq{[this](const OscMessage& m) { tm.receive(m); }};
    int64_t now = 0;
    // Runs until the command finishes (or 'limitMs'), delivering TotalMix's replies.
    std::optional<voice::CommandSequencer::Finished> run(int64_t limitMs = 30000) {
        for (const int64_t end = now + limitMs; now < end; now += 20) {
            std::vector<OscMessage> in;
            in.swap(tm.outbox);
            for (const auto& m : in) seq.onReceive(m);
            if (auto f = seq.tick(now)) return f;
        }
        return std::nullopt;
    }
};
}  // namespace

static void testSequencer() {
    using namespace rme::voice;
    auto setup = [](SeqHarness& h) {
        h.tm.names[0] = {"AN 1/2", "", "AN 3", "Vocal", "AN 5", "AN 6", "AN 7", "AN 8", "ADAT 1", "ADAT 2"};
        h.tm.names[1] = {"PB 1", "PB 2", "PB 3"};
        h.tm.names[2] = {"Main", "", "Phones"};
    };
    auto cmd = [](const char* text, const std::vector<NamedChannel>& names = {}) {
        return *parseVoiceCommand(text, names);
    };
    std::string err;

    {  // Snapshot from page 1: goes to page 3, loads, returns to page 1.
        SeqHarness h;
        setup(h);
        CHECK(h.seq.start(cmd("totalmix snapshot 4"), std::nullopt, 1, h.now, err));
        CHECK(h.seq.busy());
        const auto f = h.run();
        CHECK(f && f->ok && f->text == "Snapshot 4 loaded");
        CHECK(h.tm.snapshotLoaded == 4);
        CHECK(h.tm.page == 1);
        CHECK(!h.seq.busy());
    }
    {  // Snapshot while the user is on page 3 (TotalMix must still re-send page 3).
        SeqHarness h;
        setup(h);
        h.tm.page = 3;
        CHECK(h.seq.start(cmd("totalmix snapshot 1"), std::nullopt, 3, h.now, err));
        const auto f = h.run();
        CHECK(f && f->ok && h.tm.snapshotLoaded == 1 && h.tm.page == 3);
    }
    {  // Volume of input 9 by number (outside the first bank).
        SeqHarness h;
        setup(h);
        CHECK(h.seq.start(cmd("totalmix input 9 volume minus 10"), ChannelTarget{Bus::Input, false, 8}, 1, h.now, err));
        const auto f = h.run();
        CHECK(f && f->ok && f->text == "ADAT 1 volume -10.0 dB");
        CHECK(near(scale::faderToDb(h.tm.volume[0][8]), -10.0, 0.05));
        CHECK(h.tm.page == 1);
        // Learned on the way: channel 1 and channel 9.
        bool sawAdat = false, sawAn = false;
        for (const auto& n : h.seq.learnedNames()) {
            if (n.bus == Bus::Input && n.number == 9 && n.name == "ADAT 1") sawAdat = true;
            if (n.bus == Bus::Input && n.number == 1 && n.name == "AN 1/2") sawAn = true;
        }
        CHECK(sawAdat && sawAn);
    }
    {  // The right half of a stereo channel is refused, nothing changes.
        SeqHarness h;
        setup(h);
        CHECK(h.seq.start(cmd("totalmix input 2 volume minus 10"), ChannelTarget{Bus::Input, false, 1}, 1, h.now, err));
        const auto f = h.run();
        CHECK(f && !f->ok && f->text.find("not a channel") != std::string::npos);
        CHECK(h.tm.volume[0][0] == 0.0f && h.tm.volume[0][1] == 0.0f);
        CHECK(h.tm.page == 1);
    }
    {  // By name, in the bank shown on page 1: uses the bank offset; relative volume.
        SeqHarness h;
        setup(h);
        h.tm.volume[0][3] = static_cast<float>(scale::dbToFader(-20.0));
        CHECK(h.seq.start(cmd("totalmix vocal volume up 6", {{Bus::Input, 0, "Vocal"}}), ChannelTarget{Bus::Input, true, 3},
                          1, h.now, err));
        const auto f = h.run();
        CHECK(f && f->ok && f->text == "Vocal volume -14.0 dB");
        CHECK(near(scale::faderToDb(h.tm.volume[0][3]), -14.0, 0.05));
    }
    {  // A relative raise stops at 0 dB.
        SeqHarness h;
        setup(h);
        h.tm.volume[0][3] = static_cast<float>(scale::dbToFader(-2.0));
        CHECK(h.seq.start(cmd("totalmix vocal volume up 6", {{Bus::Input, 0, "Vocal"}}), ChannelTarget{Bus::Input, true, 3},
                          1, h.now, err));
        const auto f = h.run();
        CHECK(f && f->ok && near(scale::faderToDb(h.tm.volume[0][3]), 0.0, 0.05));
    }
    {  // Mute only toggles when needed; pan; a user on page 2 stays on page 2.
        SeqHarness h;
        setup(h);
        h.tm.page = 2;
        const ChannelTarget pb2{Bus::Playback, false, 1};
        CHECK(h.seq.start(cmd("totalmix playback 2 mute"), pb2, 2, h.now, err));
        auto f = h.run();
        CHECK(f && f->ok && f->text == "PB 2 muted" && h.tm.mute[1][1] > 0.5f);
        CHECK(h.seq.start(cmd("totalmix playback 2 mute"), pb2, 2, h.now, err));
        f = h.run();
        CHECK(f && f->ok && f->text == "PB 2 already muted" && h.tm.mute[1][1] > 0.5f);
        CHECK(h.seq.start(cmd("totalmix playback 2 pan left 40"), pb2, 2, h.now, err));
        f = h.run();
        CHECK(f && f->ok && f->text == "PB 2 pan left 40" && near(h.tm.pan[1][1], 0.3));
        CHECK(h.tm.page == 2);
    }
    {  // FX send: found through the display string.
        SeqHarness h;
        setup(h);
        CHECK(h.seq.start(cmd("totalmix input 3 effects send minus 13"), ChannelTarget{Bus::Input, false, 2}, 1, h.now, err));
        const auto f = h.run();
        CHECK(f && f->ok);
        CHECK(near(-65.0 + 65.0 * h.tm.send[0][2], -13.0, 0.3));
        CHECK(f && f->text.rfind("AN 3 effects send ", 0) == 0);
        CHECK(h.tm.page == 1);
        // Outputs have no FX send.
        CHECK(!h.seq.start(cmd("totalmix output 1 effects send minus 3"), ChannelTarget{Bus::Output, false, 0}, 1, h.now,
                           err));
    }
    {  // Reverb on; echo already on.
        SeqHarness h;
        setup(h);
        CHECK(h.seq.start(cmd("totalmix reverb on"), std::nullopt, 1, h.now, err));
        auto f = h.run();
        CHECK(f && f->ok && f->text == "Reverb on" && h.tm.reverb > 0.5f);
        CHECK(h.seq.start(cmd("totalmix echo on"), std::nullopt, 1, h.now, err));
        f = h.run();
        CHECK(f && f->ok && f->text == "Echo already on" && h.tm.echo > 0.5f);
    }
    {  // No reply: fails within the timeout and asks for the user's page again.
        SeqHarness h;
        setup(h);
        h.tm.silent = true;
        CHECK(h.seq.start(cmd("totalmix undo"), std::nullopt, 1, h.now, err));
        CHECK(!h.seq.start(cmd("totalmix undo"), std::nullopt, 1, h.now, err));  // busy
        const auto f = h.run(5000);
        CHECK(f && !f->ok && f->text == "No reply from TotalMix FX");
        CHECK(!h.tm.log.empty() && h.tm.log.back() == "/1");
        CHECK(h.tm.undos == 0);
    }
    {  // Name scan: stereo gaps are skipped; it stops after the last channel.
        SeqHarness h;
        setup(h);
        CHECK(h.seq.startNameScan(1, h.now, err));
        const auto f = h.run(120000);
        CHECK(f && f->ok && f->text == "Learned 9 input, 3 playback and 2 output channel names");
        CHECK(h.seq.learnedNames().size() == 14);
        CHECK(h.tm.page == 1);
    }
}

int main() {
    testPortCheck();
    testOscRoundTrip();
    testOscBundles();
    testScale();
    testParams();
    testLink();
    testVoiceParse();
    testSequencer();
    testUdpLoopback();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
