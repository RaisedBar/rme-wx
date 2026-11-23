#include "VoiceSettingsDialog.h"

#include "Accessibility.h"
#include "VoiceInput.h"

VoiceSettingsDialog::VoiceSettingsDialog(wxWindow* parent, const Settings& s)
    : wxDialog(parent, wxID_ANY, "Voice Settings"), base_(s) {
    enabled_ = new wxCheckBox(this, wxID_ANY, "&Voice control on");
    enabled_->SetValue(s.voiceEnabled);
    a11y::setDescription(enabled_, "Listen for spoken commands on a headset microphone (Ctrl+Shift+V)");

    auto* grid = new wxFlexGridSizer(2, FromDIP(6), FromDIP(8));
    grid->AddGrowableCol(1);
    wxWindow* prev = enabled_;
    auto add = [&](const wxString& label, wxWindow* ctrl, const wxString& help) {
        auto* st = new wxStaticText(this, wxID_ANY, label);
        grid->Add(st, 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(ctrl, 1, wxEXPAND);
        st->MoveAfterInTabOrder(prev);
        ctrl->MoveAfterInTabOrder(st);
        prev = ctrl;
        a11y::setName(ctrl, wxControl::RemoveMnemonics(label).BeforeLast(':'));
        if (!help.empty()) a11y::setDescription(ctrl, help);
    };

    wakeWord_ = new wxTextCtrl(this, wxID_ANY, s.wakeWord);
    add("&Wake word:", wakeWord_, "Said before every command, for example: TotalMix snapshot 2");

    device_ = new wxChoice(this, wxID_ANY);
    device_->Append("Any headset (recommended)");
    deviceNames_.push_back(wxString());
    int sel = 0;
    for (const auto& d : VoiceInput::captureDevices()) {
        device_->Append(d.name + (d.headset ? " (headset)" : ""));
        deviceNames_.push_back(d.name);
        if (!s.voiceDevice.empty() && d.name.IsSameAs(s.voiceDevice, false)) sel = device_->GetCount() - 1;
    }
    if (!s.voiceDevice.empty() && sel == 0) {  // chosen before, not connected now
        device_->Append(s.voiceDevice + " (not connected)");
        deviceNames_.push_back(s.voiceDevice);
        sel = device_->GetCount() - 1;
    }
    device_->SetSelection(sel);
    add("&Microphone:", device_,
        "Any headset: the first headset microphone that is connected. A microphone that is not a headset also "
        "hears the monitors and the screen reader.");

    confidence_ = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 100,
                                 static_cast<int>(s.voiceMinConfidence * 100.0 + 0.5));
    add("Minimum &confidence (percent):", confidence_,
        "Commands recognised with less confidence are ignored. Raise it if commands are heard that you did not say; "
        "lower it if your commands are ignored.");

    auto* note = new wxStaticText(
        this, wxID_ANY,
        "Commands start with the wake word, for example:\n"
        "  TotalMix snapshot 3\n"
        "  TotalMix input 5 volume minus 10\n"
        "  TotalMix playback 1 pan left 30\n"
        "  TotalMix input 2 effects send minus 12\n"
        "Help > Voice Commands lists them all.");

    auto* main = new wxBoxSizer(wxVERTICAL);
    main->Add(enabled_, 0, wxALL, FromDIP(10));
    main->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
    main->Add(note, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
    main->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(10));
    SetSizerAndFit(main);
    CentreOnParent();
    enabled_->SetFocus();
}

Settings VoiceSettingsDialog::result() const {
    Settings s = base_;
    s.voiceEnabled = enabled_->GetValue();
    s.wakeWord = wakeWord_->GetValue().Trim().Trim(false);
    if (s.wakeWord.empty()) s.wakeWord = "TotalMix";
    const int sel = device_->GetSelection();
    s.voiceDevice = sel >= 0 && sel < static_cast<int>(deviceNames_.size()) ? deviceNames_[sel] : wxString();
    s.voiceMinConfidence = confidence_->GetValue() / 100.0;
    return s;
}
