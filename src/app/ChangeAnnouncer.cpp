#include "ChangeAnnouncer.h"

#include <algorithm>

#include "Accessibility.h"

namespace {
// More changes than this in one burst (e.g. a program change sending many NRPNs) are
// summarised instead of read out one by one.
constexpr size_t kMaxSpoken = 4;
}  // namespace

ChangeAnnouncer::ChangeAnnouncer(wxWindow* source) : source_(source), timer_(this) {
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { flush(); }, timer_.GetId());
}

ChangeAnnouncer::~ChangeAnnouncer() { timer_.Stop(); }

void ChangeAnnouncer::setDelayMs(int ms) { delayMs_ = std::max(0, ms); }

void ChangeAnnouncer::post(const wxString& key, const wxString& text) {
    if (text.empty()) return;
    auto it = std::find_if(pending_.begin(), pending_.end(), [&](const auto& p) { return p.first == key; });
    if (it != pending_.end())
        it->second = text;
    else
        pending_.emplace_back(key, text);
    // Every message restarts the wait (a timer of 0 ms is not allowed, so use 1 ms).
    timer_.StartOnce(std::max(1, delayMs_));
}

void ChangeAnnouncer::flush() {
    if (pending_.empty()) return;
    wxString text;
    if (pending_.size() <= kMaxSpoken) {
        for (const auto& p : pending_) {
            if (!text.empty()) text += ". ";
            text += p.second;
        }
    } else {
        text = wxString::Format("%zu parameters changed. Last: %s", pending_.size(), pending_.back().second);
    }
    pending_.clear();
    a11y::announce(source_, text);
}
