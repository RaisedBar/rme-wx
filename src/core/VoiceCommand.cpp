#include "VoiceCommand.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include "Scale.h"

namespace rme::voice {

namespace {

using Tokens = std::vector<std::string>;

const char* const kOnes[] = {"zero",    "one",     "two",       "three",    "four",     "five",    "six",
                             "seven",   "eight",   "nine",      "ten",      "eleven",   "twelve",  "thirteen",
                             "fourteen", "fifteen", "sixteen",  "seventeen", "eighteen", "nineteen"};
const char* const kTens[] = {"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};

Tokens split(const std::string& s) {
    Tokens t;
    std::string cur;
    for (char c : s) {
        if (c == ' ') {
            if (!cur.empty()) t.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) t.push_back(cur);
    return t;
}

bool isDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

int onesValue(const std::string& w) {
    for (int i = 0; i < 20; ++i)
        if (w == kOnes[i]) return i;
    return -1;
}

int tensValue(const std::string& w) {
    for (int i = 2; i < 10; ++i)
        if (w == kTens[i]) return i * 10;
    return -1;
}

class Cursor {
public:
    explicit Cursor(Tokens t) : t_(std::move(t)) {}
    bool done() const { return i_ >= t_.size(); }
    const std::string& peek(size_t ahead = 0) const {
        static const std::string empty;
        return i_ + ahead < t_.size() ? t_[i_ + ahead] : empty;
    }
    bool take(const std::string& w) {
        if (peek() != w) return false;
        ++i_;
        return true;
    }
    template <typename... W>
    bool takeAny(const W&... w) {
        return (take(w) || ...);
    }
    size_t pos() const { return i_; }
    void seek(size_t p) { i_ = p; }
    const Tokens& tokens() const { return t_; }

    // A whole number: digits, or words up to "one hundred". Optional "point <digit>" or a
    // decimal written with digits ("2.5").
    std::optional<double> number() {
        const size_t start = i_;
        std::optional<double> v;
        const std::string& w = peek();
        if (!w.empty() && (std::isdigit(static_cast<unsigned char>(w[0])))) {
            char* end = nullptr;
            const double d = std::strtod(w.c_str(), &end);
            if (end && *end == '\0') {
                ++i_;
                v = d;
            }
        } else if (take("hundred")) {
            v = 100;
        } else if ((peek() == "one" || peek() == "a") && peek(1) == "hundred") {
            i_ += 2;
            v = 100;
        } else if (int t = tensValue(w); t > 0) {
            ++i_;
            int n = t;
            if (int o = onesValue(peek()); o >= 1 && o <= 9) {
                ++i_;
                n += o;
            }
            v = n;
        } else if (int o = onesValue(w); o >= 0) {
            ++i_;
            v = o;
        }
        if (!v) {
            i_ = start;
            return std::nullopt;
        }
        if (peek() == "point") {
            const std::string& d = peek(1);
            int digit = onesValue(d);
            if (digit < 0 && d.size() == 1 && isDigits(d)) digit = d[0] - '0';
            if (digit >= 0 && digit <= 9) {
                i_ += 2;
                *v += digit / 10.0;
            }
        }
        return v;
    }

    std::optional<int> integer(int lo, int hi) {
        const size_t start = i_;
        const auto v = number();
        if (!v || *v != std::floor(*v) || *v < lo || *v > hi) {
            i_ = start;
            return std::nullopt;
        }
        return static_cast<int>(*v);
    }

    void skipDb() {
        while (takeAny("db", "dB", "decibel", "decibels")) {
        }
    }

private:
    Tokens t_;
    size_t i_ = 0;
};

std::optional<Bus> busWord(Cursor& c) {
    if (c.takeAny("input", "inputs", "in")) return Bus::Input;
    if (c.takeAny("playback", "playbacks", "play")) return Bus::Playback;
    if (c.takeAny("output", "outputs", "out")) return Bus::Output;
    return std::nullopt;
}

// Longest name match at the cursor; 'only' restricts it to one bus.
std::optional<ChannelRef> nameMatch(Cursor& c, const std::vector<NamedChannel>& names, std::optional<Bus> only) {
    const Tokens& t = c.tokens();
    size_t bestLen = 0;
    const NamedChannel* best = nullptr;
    for (const auto& n : names) {
        if (only && n.bus != *only) continue;
        const Tokens words = split(spokenName(n.name));
        if (words.empty() || words.size() <= bestLen || c.pos() + words.size() > t.size()) continue;
        if (std::equal(words.begin(), words.end(), t.begin() + static_cast<std::ptrdiff_t>(c.pos()))) {
            bestLen = words.size();
            best = &n;
        }
    }
    if (!best) return std::nullopt;
    c.seek(c.pos() + bestLen);
    ChannelRef r;
    r.bus = best->bus;
    r.number = best->number;
    r.name = best->name;
    // A known number is the reliable way to reach the channel, but keep the name for the
    // confirmation. Callers check byName() only when no number is known.
    return r;
}

std::optional<ChannelRef> channel(Cursor& c, const std::vector<NamedChannel>& names) {
    const size_t start = c.pos();
    if (auto bus = busWord(c)) {
        if (auto n = c.integer(1, kMaxChannel)) {
            ChannelRef r;
            r.bus = *bus;
            r.number = *n;
            return r;
        }
        if (auto r = nameMatch(c, names, bus)) return r;
        c.seek(start);
    }
    return nameMatch(c, names, std::nullopt);
}

// "minus 10", "plus 3", "zero", "off". 'allowPlus' up to kMaxFaderDb.
std::optional<double> levelValue(Cursor& c) {
    if (c.take("off")) return scale::kFaderMinDb;
    if (c.take("minus")) {
        const auto v = c.number();
        if (!v) return std::nullopt;
        c.skipDb();
        if (c.take("infinity")) return scale::kFaderMinDb;
        return *v >= -scale::kFaderMinDb ? scale::kFaderMinDb : -*v;
    }
    if (c.take("plus")) {
        const auto v = c.number();
        if (!v || *v > kMaxFaderDb) return std::nullopt;
        c.skipDb();
        return *v;
    }
    // Unsigned: only zero, so a misheard sign can never raise the level.
    const size_t start = c.pos();
    if (const auto v = c.number(); v && *v == 0.0) {
        c.skipDb();
        return 0.0;
    }
    c.seek(start);
    return std::nullopt;
}

std::optional<Command> channelAction(Cursor& c, const ChannelRef& ch) {
    Command cmd;
    cmd.channel = ch;
    if (c.takeAny("volume", "level", "fader")) {
        if (c.takeAny("up", "down")) {
            const bool up = c.tokens()[c.pos() - 1] == "up";
            const auto v = c.number();
            if (!v || *v <= 0) return std::nullopt;
            c.skipDb();
            cmd.action = Action::VolumeRelative;
            cmd.value = (up ? 1 : -1) * std::min(*v, kMaxRelativeDb);
            return cmd;
        }
        const auto v = levelValue(c);
        if (!v) return std::nullopt;
        cmd.action = Action::Volume;
        cmd.value = *v;
        return cmd;
    }
    if (c.take("mute")) {
        cmd.action = Action::Mute;
        return cmd;
    }
    if (c.take("unmute")) {
        cmd.action = Action::Unmute;
        return cmd;
    }
    if (c.take("pan")) {
        cmd.action = Action::Pan;
        if (c.takeAny("center", "centre", "middle")) return cmd;
        if (c.takeAny("left", "right")) {
            const bool left = c.tokens()[c.pos() - 1] == "left";
            const auto v = c.integer(0, 100);
            if (!v) return std::nullopt;
            c.takeAny("percent", "per");
            c.take("cent");
            cmd.value = left ? -*v : *v;
            return cmd;
        }
        return std::nullopt;
    }
    // "effects send", "effect send", "fx send", "f x send", "reverb send", "send".
    const size_t start = c.pos();
    if (c.take("f")) {
        if (!c.take("x")) c.seek(start);
    } else {
        c.takeAny("effects", "effect", "fx", "reverb");
    }
    if (c.take("send")) {
        const auto v = levelValue(c);
        if (!v) return std::nullopt;
        cmd.action = Action::FxSend;
        cmd.value = *v;
        return cmd;
    }
    c.seek(start);
    return std::nullopt;
}

std::optional<Command> command(Cursor& c, const std::vector<NamedChannel>& names) {
    Command cmd;
    if (c.take("snapshot")) {
        const auto n = c.integer(1, kMaxSnapshot);
        if (!n) return std::nullopt;
        cmd.action = Action::Snapshot;
        cmd.snapshot = *n;
        return cmd;
    }
    for (Action a : {Action::Reverb, Action::Echo}) {
        const size_t start = c.pos();
        if (c.take(a == Action::Reverb ? "reverb" : "echo")) {
            if (c.takeAny("on", "off")) {
                cmd.action = a;
                cmd.on = c.tokens()[c.pos() - 1] == "on";
                return cmd;
            }
            c.seek(start);  // "reverb send" is not a command of its own
        }
    }
    if (c.take("undo")) {
        cmd.action = Action::Undo;
        return cmd;
    }
    if (c.take("redo")) {
        cmd.action = Action::Redo;
        return cmd;
    }
    if (c.take("stop")) {
        if (!c.take("listening")) return std::nullopt;
        cmd.action = Action::StopListening;
        return cmd;
    }
    const auto ch = channel(c, names);
    if (!ch) return std::nullopt;
    return channelAction(c, *ch);
}

std::string dbText(double db) { return scale::formatDb(db); }

}  // namespace

const char* busName(Bus b) {
    switch (b) {
        case Bus::Input: return "Input";
        case Bus::Playback: return "Playback";
        case Bus::Output: return "Output";
    }
    return "";
}

bool Command::isChannelAction() const {
    switch (action) {
        case Action::Volume:
        case Action::VolumeRelative:
        case Action::Mute:
        case Action::Unmute:
        case Action::Pan:
        case Action::FxSend: return true;
        default: return false;
    }
}

std::string normalizePhrase(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const bool digitNext = i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1]));
        const bool digitPrev = i > 0 && std::isdigit(static_cast<unsigned char>(text[i - 1]));
        if (std::isalnum(c)) {
            out += static_cast<char>(std::tolower(c));
        } else if (c == '.' && digitPrev && digitNext) {
            out += '.';
        } else if ((c == '-' || c == '+') && digitNext && !digitPrev) {
            out += c == '-' ? " minus " : " plus ";
        } else if (c >= 0x80) {
            out += static_cast<char>(c);  // keep UTF-8 letters of names
        } else {
            out += ' ';
        }
    }
    std::string tidy;
    for (const auto& w : split(out)) tidy += (tidy.empty() ? "" : " ") + w;
    return tidy;
}

std::string numberWords(int n) {
    if (n < 0 || n > 100) return std::to_string(n);
    if (n == 100) return "one hundred";
    if (n < 20) return kOnes[n];
    std::string s = kTens[n / 10];
    if (n % 10) s += std::string(" ") + kOnes[n % 10];
    return s;
}

std::string spokenName(const std::string& name) {
    // Digits become number words one group at a time ("AN 12" -> "an twelve").
    std::string spaced;
    for (size_t i = 0; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        const bool d = std::isdigit(c) != 0;
        const bool prevD = i > 0 && std::isdigit(static_cast<unsigned char>(name[i - 1])) != 0;
        if (i > 0 && d != prevD) spaced += ' ';
        spaced += static_cast<char>(c);
    }
    std::string out;
    for (const auto& w : split(normalizePhrase(spaced))) {
        std::string word = w;
        if (isDigits(w)) {
            const int n = std::atoi(w.c_str());
            if (w.size() > 3 || n > 100) continue;
            word = numberWords(n);
        }
        out += (out.empty() ? "" : " ") + word;
    }
    return out;
}

std::vector<std::string> wakeWordForms(const std::string& wakeWord) {
    // Split camel case: "TotalMix" -> "Total Mix".
    std::string split;
    for (size_t i = 0; i < wakeWord.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(wakeWord[i]);
        if (i > 0 && std::isupper(c) && std::islower(static_cast<unsigned char>(wakeWord[i - 1]))) split += ' ';
        split += static_cast<char>(c);
    }
    std::vector<std::string> out;
    for (const std::string& f : {normalizePhrase(split), normalizePhrase(wakeWord)})
        if (!f.empty() && std::find(out.begin(), out.end(), f) == out.end()) out.push_back(f);
    return out;  // the longer (split) form first
}

std::optional<Command> parseVoiceCommand(const std::string& text, const std::vector<NamedChannel>& names,
                                         const std::string& wakeWord) {
    Tokens t = split(normalizePhrase(text));
    for (const auto& form : wakeWordForms(wakeWord)) {
        const Tokens wake = split(form);
        if (!wake.empty() && t.size() >= wake.size() && std::equal(wake.begin(), wake.end(), t.begin())) {
            t.erase(t.begin(), t.begin() + static_cast<std::ptrdiff_t>(wake.size()));
            break;
        }
    }
    Cursor c(std::move(t));
    auto cmd = command(c, names);
    if (!cmd) return std::nullopt;
    c.skipDb();
    c.take("please");
    if (!c.done()) return std::nullopt;
    return cmd;
}

std::string describeChannel(const ChannelRef& ch) {
    if (!ch.name.empty()) return ch.name;
    return std::string(busName(ch.bus)) + " " + std::to_string(ch.number);
}

std::string describe(const Command& c) {
    const std::string ch = c.isChannelAction() ? describeChannel(c.channel) + " " : "";
    switch (c.action) {
        case Action::Snapshot: return "Snapshot " + std::to_string(c.snapshot);
        case Action::Reverb: return c.on ? "Reverb on" : "Reverb off";
        case Action::Echo: return c.on ? "Echo on" : "Echo off";
        case Action::Undo: return "Undo";
        case Action::Redo: return "Redo";
        case Action::StopListening: return "Stop listening";
        case Action::Volume:
            return ch + (c.value <= scale::kFaderMinDb ? std::string("volume off") : "volume " + dbText(c.value));
        case Action::VolumeRelative: {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.1f", std::fabs(c.value));
            return ch + (c.value >= 0 ? "volume up " : "volume down ") + buf + " dB";
        }
        case Action::Mute: return ch + "mute";
        case Action::Unmute: return ch + "unmute";
        case Action::Pan: {
            const int v = static_cast<int>(std::lround(c.value));
            if (v == 0) return ch + "pan center";
            return ch + (v < 0 ? "pan left " : "pan right ") + std::to_string(std::abs(v));
        }
        case Action::FxSend:
            return ch + (c.value <= scale::kFaderMinDb ? std::string("effects send off") : "effects send " + dbText(c.value));
    }
    return {};
}

}  // namespace rme::voice
