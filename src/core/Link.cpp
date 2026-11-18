#include "Link.h"

namespace rme {

namespace {
constexpr int64_t kSlotMs = 500;
constexpr int kSteadySlots = 4;  // 2 seconds of continuous data
}  // namespace

void Link::reset() { *this = Link(); }

void Link::onReceive(int64_t nowMs, int messages, int replies) {
    if (messages <= 0) return;
    total_ += static_cast<uint64_t>(messages);
    const int64_t slot = nowMs / kSlotMs;
    if (slot != slot_) {
        steadySlots_ = (slot == slot_ + 1) ? steadySlots_ + 1 : 1;
        slot_ = slot;
    }
    lastRx_ = nowMs;
    if (!receiving_) {
        receiving_ = true;
        startedPending_ = true;
    }
    if (checkStart_ >= 0 && replies > 0) checkGotData_ = true;
    if (checkStart_ >= 0) checkGotLevels_ = true;
}

void Link::beginCheck(int64_t nowMs) {
    checkStart_ = nowMs;
    checkGotData_ = false;
    checkGotLevels_ = false;
}

Link::Event Link::tick(int64_t nowMs) {
    if (checkStart_ >= 0 && checkGotData_) {
        checkStart_ = -1;
        startedPending_ = false;  // the check result says it
        return Event::CheckOk;
    }
    if (checkStart_ >= 0 && nowMs - checkStart_ >= kCheckMs) {
        checkStart_ = -1;
        return Event::CheckFailed;
    }
    if (startedPending_) {
        startedPending_ = false;
        return Event::Started;
    }
    if (receiving_ && steadySlots_ >= kSteadySlots && nowMs - lastRx_ >= kSilenceMs) {
        receiving_ = false;
        steadySlots_ = 0;
        return Event::Stopped;
    }
    return Event::None;
}

}  // namespace rme
