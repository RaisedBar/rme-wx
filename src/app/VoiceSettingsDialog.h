// Voice control settings: on / off, wake word, microphone and recognition confidence. Each
// label (with an Alt mnemonic) directly precedes its control in the tab order.
#pragma once

#include <wx/spinctrl.h>
#include <wx/wx.h>

#include <vector>

#include "SettingsStore.h"

class VoiceSettingsDialog : public wxDialog {
public:
    VoiceSettingsDialog(wxWindow* parent, const Settings& s);
    Settings result() const;

private:
    Settings base_;
    wxCheckBox* enabled_ = nullptr;
    wxTextCtrl* wakeWord_ = nullptr;
    wxChoice* device_ = nullptr;
    wxSpinCtrl* confidence_ = nullptr;
    std::vector<wxString> deviceNames_;  // per choice item; empty = any headset
};
