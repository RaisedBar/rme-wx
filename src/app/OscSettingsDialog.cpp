#include "OscSettingsDialog.h"

#include "Accessibility.h"

OscSettingsDialog::OscSettingsDialog(wxWindow* parent, const Settings& s)
    : wxDialog(parent, wxID_ANY, "OSC Settings"), base_(s) {
    auto* grid = new wxFlexGridSizer(2, FromDIP(6), FromDIP(8));
    grid->AddGrowableCol(1);
    auto add = [&](const wxString& label, wxWindow* ctrl, const wxString& help) {
        auto* st = new wxStaticText(this, wxID_ANY, label);
        grid->Add(st, 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(ctrl, 1, wxEXPAND);
        ctrl->MoveAfterInTabOrder(st);
        a11y::setName(ctrl, wxControl::RemoveMnemonics(label));
        if (!help.empty()) a11y::setDescription(ctrl, help);
    };
    host_ = new wxTextCtrl(this, wxID_ANY, s.host);
    add("TotalMix FX &address:", host_, "IP address or name of the computer running TotalMix FX; 127.0.0.1 for this computer");
    sendPort_ = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 65535, s.sendPort);
    add("&Send to port:", sendPort_, "TotalMix FX 'Port incoming', normally 7001");
    receivePort_ = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 65535, s.receivePort);
    add("&Receive on port:", receivePort_, "TotalMix FX 'Port outgoing', normally 9001");
    bankSize_ = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 24, s.bankSize);
    add("&Faders per bank:", bankSize_, "Must match 'Number of faders per bank' in TotalMix FX");

    logIncoming_ = new wxCheckBox(this, wxID_ANY, "&Log messages from TotalMix FX");
    logIncoming_->SetValue(s.logIncoming);
    logLevels_ = new wxCheckBox(this, wxID_ANY, "Log level &meter messages");
    logLevels_->SetValue(s.logLevels);

    auto* note = new wxStaticText(
        this, wxID_ANY,
        "In TotalMix FX, choose Options > Enable OSC Control, then Options > Settings > OSC:\n"
        "tick 'In Use' for a Remote Controller, set its 'Remote Controller Address' to the IP address\n"
        "of this computer (127.0.0.1 if TotalMix FX runs here), 'Port incoming' to the Send port and\n"
        "'Port outgoing' to the Receive port above.");

    auto* main = new wxBoxSizer(wxVERTICAL);
    main->Add(grid, 0, wxEXPAND | wxALL, FromDIP(10));
    main->Add(logIncoming_, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
    main->Add(logLevels_, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
    main->Add(note, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
    main->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(10));
    SetSizerAndFit(main);
    CentreOnParent();
    host_->SetFocus();
    host_->SelectAll();
}

Settings OscSettingsDialog::result() const {
    Settings s = base_;
    s.host = host_->GetValue().Trim().Trim(false);
    if (s.host.empty()) s.host = "127.0.0.1";
    s.sendPort = sendPort_->GetValue();
    s.receivePort = receivePort_->GetValue();
    s.bankSize = bankSize_->GetValue();
    s.logIncoming = logIncoming_->GetValue();
    s.logLevels = logLevels_->GetValue();
    return s;
}
