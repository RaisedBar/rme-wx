// Watches the traffic from TotalMix FX. OSC over UDP has no connection, so "connected" means
// "TotalMix FX is sending to us". Time comes from the caller, so this is testable.
//
// - Started: the first data arrives (or data arrives again after Stopped).
// - Stopped: TotalMix had been sending steadily (level meters arrive many times a second)
//   and then nothing came for kSilenceMs. Without level data TotalMix only sends on changes,
//   so silence alone is not reported.
// - A check (beginCheck) succeeds if a reply (anything but level meters) arrives within kCheckMs.
//   Level meters alone mean TotalMix FX sends to us but did not get our request.
#pragma once

#include <cstdint>

namespace rme {

class Link {
public:
    enum class Event { None, Started, Stopped, CheckOk, CheckFailed };
    static constexpr int64_t kSilenceMs = 3000;
    static constexpr int64_t kCheckMs = 1500;

    void reset();
    // replies: messages other than level meters (TotalMix sends levels unasked, so only
    // other messages show that it reacted to us).
    void onReceive(int64_t nowMs, int messages, int replies);
    void beginCheck(int64_t nowMs);
    bool checking() const { return checkStart_ >= 0; }
    // After CheckFailed: whether data (level meters) arrived during the check anyway.
    bool lastCheckGotData() const { return checkGotLevels_; }
    // Call regularly (e.g. every 20 ms). Returns at most one event per call.
    Event tick(int64_t nowMs);

    bool receiving() const { return receiving_; }
    int64_t lastReceiveMs() const { return lastRx_; }
    uint64_t messageCount() const { return total_; }

private:
    bool receiving_ = false;
    bool startedPending_ = false;
    int64_t lastRx_ = -1;
    int64_t checkStart_ = -1;
    bool checkGotData_ = false;
    bool checkGotLevels_ = false;
    uint64_t total_ = 0;
    // Steady traffic: data in each of the last few 500 ms slots.
    int64_t slot_ = -1;
    int steadySlots_ = 0;
};

}  // namespace rme
