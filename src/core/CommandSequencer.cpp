#include "CommandSequencer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "Params.h"
#include "Scale.h"

namespace rme::voice {

namespace {
const char* const kBusParams[3] = {"busInput", "busPlayback", "busOutput"};
const char* const kBusPlural[3] = {"input", "playback", "output"};
constexpr int kScanGiveUpRepeats = 4;
}  // namespace

std::optional<double> parseDisplayDb(const std::string& text) {
    if (text.find("-oo") != std::string::npos || text.find("\xE2\x88\x9E") != std::string::npos ||
        text.find("inf") != std::string::npos)
        return scale::kFaderMinDb;
    const char* s = text.c_str();
    while (*s && !(*s == '-' || *s == '+' || *s == '.' || (*s >= '0' && *s <= '9'))) ++s;
    if (!*s) return std::nullopt;
    char* end = nullptr;
    const double v = std::strtod(s, &end);
    if (end == s) return std::nullopt;
    return v;
}

// ---- Script building ---------------------------------------------------------------------

void CommandSequencer::begin(int userPage) {
    ops_.clear();
    inserted_.clear();
    rx_.clear();
    pageSeq_.clear();
    active_ = true;
    failed_ = false;
    result_.clear();
    userPage_ = userPage;
    tmPage_ = userPage;
    startSeq_ = seq_;
}

void CommandSequencer::push(Op op) {
    // Ops added while a Run op executes go right after it; otherwise at the end.
    if (inRun_)
        inserted_.push_back(std::move(op));
    else
        ops_.push_back(std::move(op));
}

void CommandSequencer::sendOp(OscMessage m) {
    Op op;
    op.type = Op::Type::Send;
    op.msg = std::move(m);
    push(std::move(op));
}

void CommandSequencer::sendFloat(const std::string& address, float v) { sendOp(OscMessage::withFloat(address, v)); }

void CommandSequencer::wait(int64_t ms) {
    Op op;
    op.type = Op::Type::Wait;
    op.ms = ms;
    push(std::move(op));
}

void CommandSequencer::expect(const std::string& address, int64_t ms, const std::string& failText, bool optional) {
    Op op;
    op.type = Op::Type::Expect;
    op.address = address;
    op.ms = ms;
    op.failText = failText;
    op.optional = optional;
    push(std::move(op));
}

void CommandSequencer::expectPage(int page, int64_t ms, const std::string& failText) {
    Op op;
    op.type = Op::Type::Expect;
    op.page = page;
    op.ms = ms;
    op.failText = failText;
    push(std::move(op));
}

void CommandSequencer::run(std::function<void()> fn) {
    Op op;
    op.type = Op::Type::Run;
    op.fn = std::move(fn);
    push(std::move(op));
}

void CommandSequencer::goToPage(int page) {
    // Selecting the page TotalMix FX is already on may not make it send the page again, so go
    // via another page then (not page 4, which switches TotalMix to the Outputs row).
    if (tmPage_ == page) sendOp(OscMessage(pageAddress(page == 1 ? 2 : 1)));
    sendOp(OscMessage(pageAddress(page)));
}

void CommandSequencer::restorePage(int tmPage) {
    if (userPage_ == tmPage) return;
    sendOp(OscMessage(pageAddress(userPage_)));
    wait(kSettleMs);
}

void CommandSequencer::fail(const std::string& text) {
    failed_ = true;
    result_ = text;
    ops_.clear();
    inserted_.clear();
    if (tmPage_ != userPage_ && send_) send_(OscMessage(pageAddress(userPage_)));
    tmPage_ = userPage_;
}

void CommandSequencer::cancel() {
    if (!active_) return;
    fail("Cancelled");
    active_ = false;
}

const CommandSequencer::Rx* CommandSequencer::received(const std::string& address, uint64_t since) const {
    const auto it = rx_.find(address);
    if (it == rx_.end() || it->second.seq <= std::max(since, startSeq_)) return nullptr;
    return &it->second;
}

void CommandSequencer::learn(Bus bus, int number, const std::string& name) {
    if (name.empty() || number < 1) return;
    auto& slot = learned_[{static_cast<int>(bus), number}];
    if (slot == name) return;
    slot = name;
    ++namesVersion_;
}

std::vector<NamedChannel> CommandSequencer::learnedNames() const {
    std::vector<NamedChannel> out;
    for (const auto& [key, name] : learned_) out.push_back({static_cast<Bus>(key.first), key.second, name});
    return out;
}

void CommandSequencer::setLearnedNames(const std::vector<NamedChannel>& names) {
    learned_.clear();
    for (const auto& n : names)
        if (n.number >= 1 && !n.name.empty()) learned_[{static_cast<int>(n.bus), n.number}] = n.name;
    ++namesVersion_;
}

// ---- Commands ----------------------------------------------------------------------------

bool CommandSequencer::start(const Command& c, const std::optional<ChannelTarget>& where, int userPage,
                             int64_t /*nowMs*/, std::string& error) {
    if (active_) {
        error = "Still working on the last command";
        return false;
    }
    if (c.action == Action::StopListening) {
        error = "Not a mixer command";
        return false;
    }
    if (c.isChannelAction()) {
        if (!where) {
            error = describeChannel(c.channel) + " is not known";
            return false;
        }
        if (c.action == Action::FxSend && where->bus == Bus::Output) {
            error = "Outputs have no effects send";
            return false;
        }
    }
    begin(userPage);
    if (c.isChannelAction())
        channelCommand(c, *where);
    else
        pageThreeCommand(c);
    return true;
}

void CommandSequencer::channelCommand(const Command& c, const ChannelTarget& where) {
    const int bus = static_cast<int>(where.bus);
    const std::string noReply = "No reply from TotalMix FX";
    // Selecting the row switches TotalMix FX to page 1, so the page 2 request below always
    // makes it send page 2.
    sendFloat(std::string("/1/") + kBusParams[bus], 1.0f);
    wait(kStepMs);
    if (where.inBank) {
        sendFloat(kSetOffsetInBank, static_cast<float>(where.index));
        wait(kStepMs);
        sendOp(OscMessage(pageAddress(2)));
        expect("/2/trackname", kReplyMs, noReply);
        wait(kSettleMs);
        run([this, c, where]() { channelAction(c, where); });
        return;
    }
    // TotalMix FX ignores /setBankStart for a number that is no channel (the right half of a
    // stereo channel, or past the last channel) and page 2 would still show the previous
    // channel. Select channel 1 first, so an ignored request shows up as channel 1's name.
    sendFloat(kSetBankStart, 0.0f);
    wait(kStepMs);
    sendOp(OscMessage(pageAddress(2)));
    expect("/2/trackname", kReplyMs, noReply);
    wait(kSettleMs);
    run([this, c, where, noReply]() {
        const Rx* first = received("/2/trackname");
        const std::string firstName = first ? first->text : std::string();
        learn(where.bus, 1, firstName);
        if (where.index == 0) {
            channelAction(c, where);
            return;
        }
        const uint64_t mark = seq_;
        sendOp(OscMessage(pageAddress(1)));
        sendFloat(kSetBankStart, static_cast<float>(where.index));
        wait(kStepMs);
        sendOp(OscMessage(pageAddress(2)));
        expect("/2/trackname", kReplyMs, noReply);
        wait(kSettleMs);
        run([this, c, where, firstName, mark]() {
            const Rx* now = received("/2/trackname", mark);
            if (!now || now->text == firstName) {
                fail(describeChannel(c.channel) +
                     " is not a channel in TotalMix FX. For a stereo channel use the number of its left channel.");
                return;
            }
            channelAction(c, where);
        });
    });
}

void CommandSequencer::channelAction(const Command& c, const ChannelTarget& where) {
    const Rx* nameRx = received("/2/trackname");
    const std::string name = nameRx && !nameRx->text.empty() ? nameRx->text : describeChannel(c.channel);
    if (!where.inBank) learn(where.bus, where.index + 1, name);

    switch (c.action) {
        case Action::Volume: {
            const double db = std::min(c.value, kMaxFaderDb);
            sendFloat("/2/volume", static_cast<float>(scale::dbToFader(db)));
            result_ = name + (db <= scale::kFaderMinDb ? " volume off" : " volume " + scale::formatDb(db));
            break;
        }
        case Action::VolumeRelative: {
            const Rx* cur = received("/2/volume");
            if (!cur || !cur->hasNumber) {
                fail("The volume of " + name + " is not known");
                return;
            }
            const double from = scale::faderToDb(cur->number);
            double to = std::max(from, scale::kFaderMinDb) + c.value;
            // Raising never goes above 0 dB unless the level is already there; say "plus".
            if (c.value > 0 && from <= 0.0) to = std::min(to, 0.0);
            to = std::clamp(to, scale::kFaderMinDb, kMaxFaderDb);
            sendFloat("/2/volume", static_cast<float>(scale::dbToFader(to)));
            result_ = name + " volume " + scale::formatDb(to);
            break;
        }
        case Action::Mute:
        case Action::Unmute: {
            const bool want = c.action == Action::Mute;
            const Rx* cur = received("/2/mute");
            const bool muted = cur && cur->hasNumber && cur->number >= 0.5f;
            if (muted == want) {
                result_ = name + (want ? " already muted" : " already unmuted");
                break;
            }
            sendFloat("/2/mute", 1.0f);  // a toggle
            result_ = name + (want ? " muted" : " unmuted");
            break;
        }
        case Action::Pan: {
            const double v = std::clamp((c.value + 100.0) / 200.0, 0.0, 1.0);
            sendFloat("/2/pan", static_cast<float>(v));
            const std::string p = scale::formatPan(v);
            result_ = name + " pan " + (p == "Center" ? std::string("center")
                                                      : (p[0] == 'L' ? "left " : "right ") + p.substr(1));
            break;
        }
        case Action::FxSend: {
            if (c.value <= scale::kFaderMinDb) {
                sendFloat("/2/reverbSend", 0.0f);
                result_ = name + " effects send off";
                break;
            }
            // The OSC table gives no curve for the FX send, so search for the value whose
            // display string TotalMix FX reports as the requested dB.
            result_ = name;
            const double target = std::min(c.value, kMaxFaderDb);
            fxSendSearch(target, 0.0, 1.0, scale::dbToFader(target), 0);
            break;
        }
        default: break;
    }
    restorePage(2);
}

void CommandSequencer::fxSendSearch(double targetDb, double lo, double hi, double guess, int step) {
    const uint64_t mark = seq_;
    sendFloat("/2/reverbSend", static_cast<float>(guess));
    expect("/2/reverbSendVal", kValueReplyMs, "", true);
    run([this, targetDb, lo, hi, guess, step, mark]() {
        const std::string name = result_;
        const Rx* val = received("/2/reverbSendVal", mark);
        const std::optional<double> got = val ? parseDisplayDb(val->text) : std::nullopt;
        if (!got) {  // no display string: keep the estimate
            result_ = name + " effects send " + scale::formatDb(targetDb);
            return;
        }
        if (std::fabs(*got - targetDb) <= 0.25 || step + 1 >= kMaxSearchSteps) {
            result_ = name + " effects send " + scale::formatDb(*got);
            return;
        }
        double nlo = lo, nhi = hi;
        if (*got < targetDb)
            nlo = guess;
        else
            nhi = guess;
        fxSendSearch(targetDb, nlo, nhi, (nlo + nhi) / 2.0, step + 1);
        // Keep the channel name for the next step's result.
        result_ = name;
    });
}

void CommandSequencer::pageThreeCommand(const Command& c) {
    const std::string noReply = "No reply from TotalMix FX";
    goToPage(3);
    switch (c.action) {
        case Action::Snapshot:
            expectPage(3, kReplyMs, noReply);
            wait(kSettleMs);
            sendFloat("/3/snapshots/" + std::to_string(9 - c.snapshot) + "/1", 1.0f);
            wait(2 * kSettleMs);
            result_ = "Snapshot " + std::to_string(c.snapshot) + " loaded";
            break;
        case Action::Reverb:
        case Action::Echo: {
            const std::string param = c.action == Action::Reverb ? "reverbEnable" : "echoEnable";
            const std::string label = c.action == Action::Reverb ? "Reverb" : "Echo";
            expect("/3/" + param, kReplyMs, noReply);
            wait(kSettleMs);
            run([this, c, param, label]() {
                const Rx* cur = received("/3/" + param);
                if (!cur || !cur->hasNumber) {
                    fail(label + " state is not known");
                    return;
                }
                if ((cur->number >= 0.5f) == c.on) {
                    result_ = label + (c.on ? " already on" : " already off");
                    return;
                }
                sendFloat("/3/" + param, 1.0f);  // a toggle
                wait(kSettleMs);
                result_ = label + (c.on ? " on" : " off");
            });
            break;
        }
        case Action::Undo:
        case Action::Redo:
            expectPage(3, kReplyMs, noReply);
            wait(kSettleMs);
            sendFloat(c.action == Action::Undo ? "/3/undo" : "/3/redo", 1.0f);
            wait(2 * kSettleMs);
            result_ = c.action == Action::Undo ? "Undo" : "Redo";
            break;
        default: break;
    }
    restorePage(3);
}

// ---- Name scan -----------------------------------------------------------------------------

bool CommandSequencer::startNameScan(int userPage, int64_t /*nowMs*/, std::string& error) {
    if (active_) {
        error = "Still working on the last command";
        return false;
    }
    begin(userPage);
    for (int& n : scanCount_) n = 0;
    scanBus(0);
    return true;
}

void CommandSequencer::scanBus(int bus) {
    if (bus > 2) {
        result_ = "Learned " + std::to_string(scanCount_[0]) + " input, " + std::to_string(scanCount_[1]) +
                  " playback and " + std::to_string(scanCount_[2]) + " output channel names";
        restorePage(2);
        return;
    }
    sendFloat(std::string("/1/") + kBusParams[bus], 1.0f);
    wait(kStepMs);
    scanChannel(bus, 0, 0, std::string());
}

void CommandSequencer::scanChannel(int bus, int index, int repeats, std::string previous) {
    const uint64_t mark = seq_;
    sendOp(OscMessage(pageAddress(1)));
    sendFloat(kSetBankStart, static_cast<float>(index));
    wait(kStepMs);
    sendOp(OscMessage(pageAddress(2)));
    expect("/2/trackname", kScanReplyMs, "", true);
    run([this, bus, index, repeats, previous, mark]() {
        const Rx* r = received("/2/trackname", mark);
        int rep = repeats;
        std::string prev = previous;
        if (!r || r->text.empty() || r->text == previous) {
            // Ignored (no channel at this number, or the right half of a stereo channel).
            ++rep;
            if (index == 0 && !r) {
                fail(std::string("No reply from TotalMix FX while learning ") + kBusPlural[bus] + " names");
                return;
            }
        } else {
            learn(static_cast<Bus>(bus), index + 1, r->text);
            ++scanCount_[bus];
            rep = 0;
            prev = r->text;
        }
        if (rep >= kScanGiveUpRepeats || index + 1 >= kMaxChannel)
            scanBus(bus + 1);
        else
            scanChannel(bus, index + 1, rep, prev);
    });
}

// ---- Running -------------------------------------------------------------------------------

void CommandSequencer::onReceive(const OscMessage& m) {
    if (!active_) return;
    Rx& r = rx_[m.address];
    r.seq = ++seq_;
    if (m.hasNumber()) {
        r.hasNumber = true;
        r.number = m.number();
    }
    if (m.hasString()) r.text = m.text();
    // Level meters arrive on their own and say nothing about a page change.
    if (m.address.find("/level") == std::string::npos) pageSeq_[pageOf(m.address)] = seq_;
}

std::optional<CommandSequencer::Finished> CommandSequencer::tick(int64_t nowMs) {
    if (!active_) return std::nullopt;
    while (!ops_.empty() && !failed_) {
        Op& op = ops_.front();
        switch (op.type) {
            case Op::Type::Send: {
                OscMessage m = std::move(op.msg);
                ops_.pop_front();
                if (const int p = m.args.empty() && m.address.size() == 2 ? m.address[1] - '0' : pageOf(m.address);
                    p >= 1 && p <= 4)
                    tmPage_ = p;
                if (send_) send_(m);
                continue;
            }
            case Op::Type::Wait:
                if (!op.started) {
                    op.started = true;
                    op.deadline = nowMs + op.ms;
                }
                if (nowMs < op.deadline) return std::nullopt;
                ops_.pop_front();
                continue;
            case Op::Type::Expect: {
                if (!op.started) {
                    op.started = true;
                    op.deadline = nowMs + op.ms;
                    op.since = seq_;
                }
                bool got = false;
                if (op.address.empty()) {
                    const auto it = pageSeq_.find(op.page);
                    got = it != pageSeq_.end() && it->second > op.since;
                } else {
                    got = received(op.address, op.since) != nullptr;
                }
                if (got) {
                    ops_.pop_front();
                    continue;
                }
                if (nowMs < op.deadline) return std::nullopt;
                if (op.optional) {
                    ops_.pop_front();
                    continue;
                }
                fail(op.failText);
                break;
            }
            case Op::Type::Run: {
                std::function<void()> fn = std::move(op.fn);
                ops_.pop_front();
                inRun_ = true;
                fn();
                inRun_ = false;
                ops_.insert(ops_.begin(), inserted_.begin(), inserted_.end());
                inserted_.clear();
                continue;
            }
        }
    }
    active_ = false;
    Finished f;
    f.ok = !failed_;
    f.text = result_;
    return f;
}

}  // namespace rme::voice
