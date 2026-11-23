// Runs a voice command against TotalMix FX's paged OSC interface.
//
// TotalMix FX only works on one page at a time, so a command is a short script: switch row,
// select the channel, switch to page 2 (one channel's fader, pan, mute and FX send), wait for
// TotalMix to send that page, set the value, and finally go back to the page the user was on.
// Snapshots, reverb / echo on-off and undo / redo use page 3 the same way.
//
// Like Link, time comes from the caller: feed every received message to onReceive() and call
// tick() regularly (e.g. every 20 ms). tick() reports when the command has finished.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "Osc.h"
#include "VoiceCommand.h"

namespace rme::voice {

// Where the channel of a channel command is.
struct ChannelTarget {
    Bus bus = Bus::Input;
    // true: 'index' is the strip offset (0-based) in the bank shown on page 1;
    // false: 'index' is the channel number - 1 (TotalMix's /setBankStart numbering).
    bool inBank = false;
    int index = 0;
};

class CommandSequencer {
public:
    using SendFn = std::function<void(const OscMessage&)>;

    static constexpr int64_t kReplyMs = 1500;     // a page must arrive within this
    static constexpr int64_t kSettleMs = 150;     // let the rest of a page arrive
    static constexpr int64_t kStepMs = 40;        // between related messages
    static constexpr int64_t kValueReplyMs = 600; // display string after a change
    static constexpr int64_t kScanReplyMs = 700;  // one channel during a name scan
    static constexpr int kMaxSearchSteps = 8;     // FX send dB search

    explicit CommandSequencer(SendFn send) : send_(std::move(send)) {}

    // Starts 'c' (not StopListening, which the caller handles). 'where' is required for channel
    // actions. 'userPage' (1..4) is restored at the end. Returns false with 'error' if busy.
    bool start(const Command& c, const std::optional<ChannelTarget>& where, int userPage, int64_t nowMs,
               std::string& error);
    // Visits every channel of every row on page 2 to learn the channel names.
    bool startNameScan(int userPage, int64_t nowMs, std::string& error);

    void onReceive(const OscMessage& m);

    struct Finished {
        bool ok = false;
        std::string text;  // spoken result: "AN 3 volume -10.0 dB", "No reply from TotalMix FX"
    };
    std::optional<Finished> tick(int64_t nowMs);

    bool busy() const { return active_; }
    void cancel();

    // Names learned while selecting channels by number and during name scans.
    std::vector<NamedChannel> learnedNames() const;
    void setLearnedNames(const std::vector<NamedChannel>& names);
    // Counts up each time learnedNames() changes.
    uint64_t namesVersion() const { return namesVersion_; }

private:
    struct Op {
        enum class Type { Send, Wait, Expect, Run } type = Type::Send;
        OscMessage msg;          // Send
        int64_t ms = 0;          // Wait / Expect timeout
        std::string address;     // Expect: exact address, or empty for any message of 'page'
        int page = 0;            // Expect
        bool optional = false;   // Expect: carry on after the timeout
        std::string failText;    // Expect
        std::function<void()> fn;  // Run
        // Running state.
        bool started = false;
        int64_t deadline = 0;
        uint64_t since = 0;
    };
    struct Rx {
        uint64_t seq = 0;
        bool hasNumber = false;
        float number = 0.0f;
        std::string text;
    };

    void begin(int userPage);
    void push(Op op);
    void sendOp(OscMessage m);
    void sendFloat(const std::string& address, float v);
    void wait(int64_t ms);
    void expect(const std::string& address, int64_t ms, const std::string& failText, bool optional = false);
    void expectPage(int page, int64_t ms, const std::string& failText);
    void run(std::function<void()> fn);
    // Makes TotalMix FX switch to 'page' and send all of it.
    void goToPage(int page);
    void restorePage(int tmPage);
    void fail(const std::string& text);

    // Received since the command started ('since' = sequence number to compare against).
    const Rx* received(const std::string& address, uint64_t since = 0) const;

    void channelCommand(const Command& c, const ChannelTarget& where);
    void channelAction(const Command& c, const ChannelTarget& where);
    void fxSendSearch(double targetDb, double lo, double hi, double guess, int step);
    void pageThreeCommand(const Command& c);
    void scanBus(int bus);
    void scanChannel(int bus, int index, int repeats, std::string previous);

    void learn(Bus bus, int number, const std::string& name);

    SendFn send_;
    std::deque<Op> ops_;
    std::vector<Op> inserted_;  // ops added while one runs; they go first, in order
    bool inRun_ = false;
    bool active_ = false;
    bool failed_ = false;
    std::string result_;
    int userPage_ = 1;
    // The page TotalMix FX is on: while building a script, the page it will be on when the
    // script reaches this point is not tracked; this is updated as page messages are sent.
    int tmPage_ = 0;
    uint64_t seq_ = 0;
    uint64_t startSeq_ = 0;
    std::map<std::string, Rx> rx_;
    std::map<int, uint64_t> pageSeq_;  // latest message per page

    std::map<std::pair<int, int>, std::string> learned_;  // (bus, number) -> name
    uint64_t namesVersion_ = 0;
    int scanCount_[3] = {};
};

// The dB value of a TotalMix FX display string ("-12.3", "+0.0 dB", "-oo"); nothing if not a
// number. Minus infinity comes back as kFaderMinDb.
std::optional<double> parseDisplayDb(const std::string& text);

}  // namespace rme::voice
