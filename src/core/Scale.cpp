#include "Scale.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rme::scale {

double faderToDb(double value) {
    const double pos = std::clamp(value, 0.0, 1.0) * 1023.0;
    if (pos >= 649.0) return pos * 0.0320855615 - 26.8235294118;
    return (pos * pos) * (-1.0 / 11033.0) + pos * 0.1497326203 - 65.0;
}

double dbToFader(double db) {
    if (db <= kFaderMinDb) return 0.0;
    if (db >= kFaderMaxDb) return 1.0;
    double v;
    if (db >= -6.0)
        v = ((db + 26.8235294118) * (1.0 / 0.0320855615)) / 1023.0;
    else
        v = (826.0 - std::sqrt(std::max(0.0, -34869.0 - 11033.0 * db))) / 1023.0;
    return std::clamp(v, 0.0, 1.0);
}

double normToFreq(double value) {
    const double lo = std::log(20.0), hi = std::log(20000.0);
    return std::exp(std::clamp(value, 0.0, 1.0) * (hi - lo) + lo);
}

double freqToNorm(double hz) {
    const double lo = std::log(20.0), hi = std::log(20000.0);
    return std::clamp((std::log(std::clamp(hz, 20.0, 20000.0)) - lo) / (hi - lo), 0.0, 1.0);
}

std::string formatDb(double db) {
    if (db <= kFaderMinDb + 0.01) return "-\xE2\x88\x9E dB";  // -∞
    char buf[32];
    const double r = std::round(db * 10.0) / 10.0;
    std::snprintf(buf, sizeof buf, "%+.1f dB", r == 0.0 ? 0.0 : r);
    return buf;
}

std::string formatFreq(double hz) {
    char buf[32];
    if (hz >= 1000.0)
        std::snprintf(buf, sizeof buf, "%.2f kHz", hz / 1000.0);
    else
        std::snprintf(buf, sizeof buf, "%.0f Hz", hz);
    return buf;
}

std::string formatPan(double value) {
    const int p = static_cast<int>(std::lround((std::clamp(value, 0.0, 1.0) - 0.5) * 200.0));
    if (p == 0) return "Center";
    return (p < 0 ? "L" : "R") + std::to_string(std::abs(p));
}

std::string formatPercent(double value) {
    return std::to_string(static_cast<int>(std::lround(std::clamp(value, 0.0, 1.0) * 100.0))) + "%";
}

std::string tidyDisplayText(const std::string& s) {
    std::string t = s;
    // trim
    const auto b = t.find_first_not_of(" \t");
    if (b == std::string::npos) return {};
    t = t.substr(b, t.find_last_not_of(" \t") - b + 1);
    for (size_t p = t.find("-oo"); p != std::string::npos; p = t.find("-oo", p)) t.replace(p, 3, "-\xE2\x88\x9E");
    return t;
}

}  // namespace rme::scale
