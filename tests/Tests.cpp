// Minimal self-contained tests for TotalMixCore (no TotalMix FX needed).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "Link.h"
#include "Osc.h"
#include "Params.h"
#include "PortCheck.h"
#include "Scale.h"
#include "UdpSocket.h"

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

int main() {
    testPortCheck();
    testOscRoundTrip();
    testOscBundles();
    testScale();
    testParams();
    testLink();
    testUdpLoopback();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
