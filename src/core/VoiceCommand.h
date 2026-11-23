// Voice commands: the phrase language and its parser. No speech engine here; the recogniser
// (SAPI on Windows) builds its grammar from the same vocabulary and hands the recognised text
// to parseVoiceCommand(), so the language can be tested without a microphone.
//
// After the wake word ("TotalMix" by default; also heard as "total mix"):
//   snapshot <1-8>
//   reverb on | off,  echo on | off,  undo,  redo,  stop listening
//   <channel> volume minus | plus <dB> [point five] [dB]     (volume zero, volume off)
//   <channel> volume up | down <dB> [dB]
//   <channel> mute | unmute
//   <channel> pan left | right <1-100>,  <channel> pan center
//   <channel> effects send minus <dB> [dB]                   (effects send zero / off)
// <channel> is "input | playback | output <n>" or a channel name TotalMix FX sent.
// Numbers may be words ("twenty one") or digits ("21").
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace rme::voice {

enum class Bus { Input = 0, Playback = 1, Output = 2 };
const char* busName(Bus b);  // "Input", "Playback", "Output"

enum class Action {
    Snapshot,
    Reverb,
    Echo,
    Undo,
    Redo,
    StopListening,
    Volume,          // value = dB (kFaderMinDb or below = off)
    VolumeRelative,  // value = dB change
    Mute,
    Unmute,
    Pan,     // value = -100 (left) .. 100 (right)
    FxSend,  // value = dB (kFaderMinDb or below = off)
};

struct ChannelRef {
    Bus bus = Bus::Input;
    int number = 0;    // 1-based single channel number (stereo: the left channel); 0 = by name
    std::string name;  // as TotalMix FX sent it, when given by name
    bool byName() const { return number == 0; }
};

struct Command {
    Action action = Action::Snapshot;
    ChannelRef channel;  // channel actions only
    double value = 0.0;
    bool on = false;   // Reverb, Echo
    int snapshot = 0;  // 1..8
    bool isChannelAction() const;
};

// A channel name the parser and grammar know. 'number' is 0 if only the name is known.
struct NamedChannel {
    Bus bus = Bus::Input;
    int number = 0;
    std::string name;
};

// Limits (also used by the grammar).
constexpr int kMaxChannel = 96;
constexpr int kMaxSnapshot = 8;
constexpr int kMaxDbWord = 65;        // "minus sixty five"
constexpr double kMaxRelativeDb = 12.0;
constexpr double kMaxFaderDb = 6.0;
inline constexpr const char* kDefaultWakeWord = "TotalMix";

// 'text' is what the recogniser heard, with or without the wake word. Returns nothing for
// phrases outside the language or out of range.
std::optional<Command> parseVoiceCommand(const std::string& text, const std::vector<NamedChannel>& names,
                                         const std::string& wakeWord = kDefaultWakeWord);

// The ways a wake word can be heard, normalised: "TotalMix" -> {"total mix", "totalmix"}.
std::vector<std::string> wakeWordForms(const std::string& wakeWord);

// Spoken confirmation of a command before it runs: "Input 5 volume -10.0 dB", "Snapshot 3".
std::string describe(const Command& c);
std::string describeChannel(const ChannelRef& ch);

// ---- Vocabulary helpers shared with the grammar builder --------------------------------
// "twenty one" for 21, "one hundred" for 100 (0..100).
std::string numberWords(int n);
// How a channel name is spoken: lower case words, digits as number words, punctuation
// dropped. "AN 1/2" -> "an one two", "Vox-L" -> "vox l". Empty if nothing speakable.
std::string spokenName(const std::string& name);
// Lower case, punctuation removed, words separated by single spaces.
std::string normalizePhrase(const std::string& text);

}  // namespace rme::voice
