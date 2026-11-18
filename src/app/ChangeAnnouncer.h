// Announces parameter changes received from the synth (CC, RPN, NRPN, SysEx dumps) through
// the screen reader. Changes are debounced: nothing is spoken until no further change has
// arrived for the delay, so turning a knob announces only where it stopped.
#pragma once

#include <wx/event.h>
#include <wx/string.h>
#include <wx/timer.h>
#include <wx/window.h>

#include <utility>
#include <vector>

class ChangeAnnouncer : public wxEvtHandler {
public:
    explicit ChangeAnnouncer(wxWindow* source);
    ~ChangeAnnouncer() override;

    void setDelayMs(int ms);
    int delayMs() const { return delayMs_; }

    // 'key' identifies what changed (e.g. the NRPN and part); a newer text for the same key
    // replaces the waiting one, so only the latest value of each parameter is spoken.
    void post(const wxString& key, const wxString& text);

private:
    void flush();

    wxWindow* source_;
    wxTimer timer_;
    int delayMs_ = 50;
    std::vector<std::pair<wxString, wxString>> pending_;  // key, text (in order of first change)
};
