// Connection settings: TotalMix FX address and ports, faders per bank, logging. Each label
// (with an Alt mnemonic) directly precedes its control in the tab order.
#pragma once

#include <wx/spinctrl.h>
#include <wx/wx.h>

#include "SettingsStore.h"

class OscSettingsDialog : public wxDialog {
public:
    OscSettingsDialog(wxWindow* parent, const Settings& s);
    Settings result() const;

private:
    Settings base_;
    wxTextCtrl* host_ = nullptr;
    wxSpinCtrl* sendPort_ = nullptr;
    wxSpinCtrl* receivePort_ = nullptr;
    wxSpinCtrl* bankSize_ = nullptr;
    wxCheckBox* logIncoming_ = nullptr;
    wxCheckBox* logLevels_ = nullptr;
};
