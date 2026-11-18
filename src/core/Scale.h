// Value scaling used by TotalMix FX OSC (see "Fader Curve calculations" in the RME OSC table).
#pragma once

#include <string>

namespace rme::scale {

constexpr double kFaderMinDb = -65.0;  // fader position 0 (shown as -infinity)
constexpr double kFaderMaxDb = 6.0;    // fader position 1

// kOSCScaleFader: OSC value 0..1 <-> dB.
double faderToDb(double value);
double dbToFader(double db);

// kOSCScaleFreq: OSC value 0..1 <-> 20 Hz..20 kHz (logarithmic).
double normToFreq(double value);
double freqToNorm(double hz);

// "-5.5 dB", "+3.0 dB", "-∞ dB" (at or below kFaderMinDb).
std::string formatDb(double db);
// "120 Hz", "1.25 kHz".
std::string formatFreq(double hz);
// kOSCScaleLin01 pan/balance 0..1: "L100" .. "Center" .. "R100".
std::string formatPan(double value);
// "37%".
std::string formatPercent(double value);

// TotalMix FX writes minus infinity as "-oo"; screen readers read "-∞" as "minus infinity".
std::string tidyDisplayText(const std::string& s);

}  // namespace rme::scale
