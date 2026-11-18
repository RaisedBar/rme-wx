#include "ParamControls.h"

#include <algorithm>
#include <cmath>

#include "Accessibility.h"
#include "Scale.h"

using rme::Kind;
using rme::Readout;

namespace {
// A value received within this time after our own change is treated as its echo.
constexpr int64_t kEchoMs = 1000;
// Level meters arrive many times a second; the visible text is updated at most this often.
constexpr int64_t kLevelRefreshMs = 200;

int64_t nowMs() { return wxGetLocalTimeMillis().GetValue(); }

bool hasLetters(const wxString& s) {
    for (auto c : s)
        if (wxIsalpha(c)) return true;
    return false;
}
}  // namespace

ParamControl::ParamControl(wxWindow* parent, AppContext& ctx, rme::ParamDef def, std::string address, bool vertical)
    : ctx_(ctx), def_(std::move(def)), address_(address.empty() ? def_.address() : std::move(address)) {
    const wxString label = wxString::FromUTF8(def_.label);
    auto valueText = [&](int widthDip) {
        readout_ = new wxStaticText(parent, wxID_ANY, "", wxDefaultPosition, parent->FromDIP(wxSize(widthDip, -1)),
                                    wxST_NO_AUTORESIZE | (vertical ? wxALIGN_CENTRE_HORIZONTAL : 0));
    };
    switch (def_.kind) {
        case Kind::Trigger: {
            spacer_ = new wxStaticText(parent, wxID_ANY, "");
            auto* b = new wxButton(parent, wxID_ANY, label);
            b->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
                localMs_ = nowMs();
                if (ctx_.beginQuiet) ctx_.beginQuiet();
                send(1.0f);
            });
            ctrl_ = b;
            break;
        }
        case Kind::Toggle:
        case Kind::OnOff: {
            spacer_ = new wxStaticText(parent, wxID_ANY, "");
            check_ = new wxCheckBox(parent, wxID_ANY, label);
            check_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
                localMs_ = nowMs();
                // A toggle is switched by sending 1.0; TotalMix FX answers with the new state.
                send(def_.kind == Kind::Toggle ? 1.0f : (check_->GetValue() ? 1.0f : 0.0f));
            });
            ctrl_ = check_;
            break;
        }
        case Kind::ToggleButton: {
            spacer_ = new wxStaticText(parent, wxID_ANY, "");
            toggle_ = new wxToggleButton(parent, wxID_ANY, label);
            toggle_->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) {
                localMs_ = nowMs();
                if (ctx_.beginQuiet) ctx_.beginQuiet();  // e.g. a snapshot changes many values
                send(1.0f);
            });
            ctrl_ = toggle_;
            break;
        }
        case Kind::Fader:
        case Kind::Lin:
        case Kind::Freq: {
            label_ = new wxStaticText(parent, wxID_ANY, label);
            const int steps = def_.sliderSteps();
            long style = vertical ? (wxSL_VERTICAL | wxSL_INVERSE) : wxSL_HORIZONTAL;
            wxSize size = vertical ? parent->FromDIP(wxSize(-1, 160)) : parent->FromDIP(wxSize(160, -1));
            slider_ = new wxSlider(parent, wxID_ANY, 0, 0, steps, wxDefaultPosition, size, style);
            slider_->SetLineSize(1);
            if (def_.kind == Kind::Fader)
                slider_->SetPageSize(6);  // 3 dB
            else
                slider_->SetPageSize(std::max(1, steps / 10));
            // Start at a neutral position until TotalMix FX reports the value.
            if (def_.readout == Readout::Pan) slider_->SetValue(steps / 2);
            if (def_.kind == Kind::Fader) slider_->SetValue(positionFromValue(static_cast<float>(rme::scale::dbToFader(0.0))));
            slider_->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) { onLocalSlider(); });
            ctrl_ = slider_;
            valueText(vertical ? 70 : 90);
            break;
        }
        case Kind::Direct: {
            label_ = new wxStaticText(parent, wxID_ANY, label);
            spin_ = new wxSpinCtrl(parent, wxID_ANY, "", wxDefaultPosition, parent->FromDIP(wxSize(80, -1)),
                                   wxSP_ARROW_KEYS, def_.minValue, def_.maxValue, def_.minValue);
            spin_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) {
                localMs_ = nowMs();
                valFresh_ = false;
                value_ = static_cast<float>(spin_->GetValue());
                send(value_);
                updateReadout();
            });
            ctrl_ = spin_;
            valueText(90);
            break;
        }
        case Kind::Text: {
            label_ = new wxStaticText(parent, wxID_ANY, label);
            text_ = new wxTextCtrl(parent, wxID_ANY, "", wxDefaultPosition, parent->FromDIP(wxSize(150, -1)),
                                   wxTE_READONLY);
            ctrl_ = text_;
            break;
        }
        case Kind::Level: {
            valueText(vertical ? 90 : 150);
            ctrl_ = readout_;
            break;
        }
    }
    if (def_.kind != Kind::Level && def_.kind != Kind::Direct && def_.kind != Kind::Fader && def_.kind != Kind::Lin &&
        def_.kind != Kind::Freq)
        spacer2_ = new wxStaticText(parent, wxID_ANY, "");

    a11y::setName(ctrl_, accessibleName());
    if (!def_.help.empty() && def_.kind != Kind::Level) a11y::setDescription(ctrl_, wxString::FromUTF8(def_.help));
    updateReadout();

    ControlRegistry::Target t;
    t.value = [this](float v) { onValue(v); };
    t.text = [this](const std::string& s) { onText(s); };
    ctx_.registry.add(address_, std::move(t));
}

ParamControl::~ParamControl() { ctx_.registry.remove(address_); }

void ParamControl::addToGrid(wxSizer* grid) {
    const int pad = ctrl_->FromDIP(2);
    if (label_)
        grid->Add(label_, 0, wxALIGN_CENTER_VERTICAL | wxALL, pad);
    else if (spacer_)
        grid->Add(spacer_, 0, wxALL, pad);
    else
        grid->Add(new wxStaticText(ctrl_->GetParent(), wxID_ANY, ""), 0, wxALL, pad);  // levels
    grid->Add(ctrl_, 0, wxALIGN_CENTER_VERTICAL | wxEXPAND | wxALL, pad);
    if (readout_ && readout_ != ctrl_)
        grid->Add(readout_, 0, wxALIGN_CENTER_VERTICAL | wxALL, pad);
    else if (spacer2_)
        grid->Add(spacer2_, 0, wxALL, pad);
    else
        grid->Add(new wxStaticText(ctrl_->GetParent(), wxID_ANY, ""), 0, wxALL, pad);
}

void ParamControl::addToColumn(wxSizer* column) {
    const int pad = ctrl_->FromDIP(2);
    if (label_) column->Add(label_, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT | wxTOP, pad);
    column->Add(ctrl_, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, pad);
    if (readout_ && readout_ != ctrl_) column->Add(readout_, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT | wxBOTTOM, pad);
}

bool ParamControl::owns(const wxWindow* w) const {
    for (; w; w = w->GetParent())
        if (w == ctrl_) return true;
    return false;
}

void ParamControl::setNamePrefix(const wxString& prefix) {
    if (prefix == prefix_) return;
    prefix_ = prefix;
    a11y::setName(ctrl_, accessibleName());
}

wxString ParamControl::accessibleName() const {
    const wxString label = wxString::FromUTF8(def_.label);
    return prefix_.empty() ? label : prefix_ + " " + label;
}

void ParamControl::show(bool show) {
    for (wxWindow* w : {static_cast<wxWindow*>(label_), spacer_, ctrl_, static_cast<wxWindow*>(readout_), spacer2_})
        if (w) w->Show(show);
}

void ParamControl::clear() {
    haveValue_ = false;
    valText_.clear();
    valFresh_ = false;
    updateReadout();
}

bool ParamControl::isSlider() const { return slider_ != nullptr; }

int ParamControl::positionFromValue(float v) const {
    const int steps = def_.sliderSteps();
    if (def_.kind == Kind::Fader) {
        const double db = rme::scale::faderToDb(v);
        return std::clamp(static_cast<int>(std::lround((db - rme::scale::kFaderMinDb) * 2.0)), 0, steps);
    }
    return std::clamp(static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * steps)), 0, steps);
}

float ParamControl::valueFromPosition(int pos) const {
    if (def_.kind == Kind::Fader) return static_cast<float>(rme::scale::dbToFader(rme::scale::kFaderMinDb + pos * 0.5));
    return static_cast<float>(pos) / static_cast<float>(def_.sliderSteps());
}

wxString ParamControl::localText() const {
    if (slider_) {
        const int pos = slider_->GetValue();
        const double norm = static_cast<double>(pos) / def_.sliderSteps();
        std::string s;
        if (def_.kind == Kind::Fader)
            s = rme::scale::formatDb(rme::scale::kFaderMinDb + pos * 0.5);
        else if (def_.kind == Kind::Freq)
            s = rme::scale::formatFreq(rme::scale::normToFreq(norm));
        else if (def_.readout == Readout::Pan)
            s = rme::scale::formatPan(norm);
        else if (def_.readout == Readout::Selection && pos < static_cast<int>(def_.choices.size()))
            s = def_.choices[static_cast<size_t>(pos)];
        else
            s = rme::scale::formatPercent(norm);
        return wxString::FromUTF8(s);
    }
    if (spin_) return wxString::Format("%d", spin_->GetValue());
    return {};
}

wxString ParamControl::valDisplay() const {
    if (valText_.empty() || hasLetters(valText_)) return valText_;
    if (def_.readout == Readout::Db) return valText_ + " dB";
    if (def_.kind == Kind::Freq) return valText_ + " Hz";
    return valText_;
}

wxString ParamControl::displayText() const {
    if (check_) return check_->GetValue() ? "on" : "off";
    if (toggle_) return toggle_->GetValue() ? "on" : "off";
    if (text_) return text_->GetValue();
    if (def_.kind == Kind::Level) return valText_.empty() ? wxString("no data") : valDisplay();
    if (slider_ || spin_) {
        if (valFresh_ && !valText_.empty() && def_.readout != Readout::None) return valDisplay();
        return localText();
    }
    return {};
}

void ParamControl::updateReadout() {
    const wxString t = displayText();
    if (def_.kind == Kind::Level) {
        const wxString s = wxString::FromUTF8(def_.label) + " " + t;
        if (readout_->GetLabel() != s) readout_->SetLabel(s);
        return;
    }
    if (readout_ && readout_->GetLabel() != t) readout_->SetLabel(t);
    if (slider_) a11y::setValue(slider_, t);
}

bool ParamControl::echoing() const { return nowMs() - localMs_ < kEchoMs; }

bool ParamControl::localScaleExact() const {
    if (check_ || toggle_) return true;
    if (def_.kind == Kind::Fader || def_.kind == Kind::Freq) return true;
    return def_.readout == Readout::Pan || def_.readout == Readout::Selection || def_.readout == Readout::None;
}

bool ParamControl::mayAnnounce() const {
    switch (def_.announce) {
        case rme::Announce::Never: return false;
        case rme::Announce::Always: return true;
        case rme::Announce::Remote: return !(ctx_.quiet && ctx_.quiet());
    }
    return false;
}

void ParamControl::send(float v) { ctx_.sendFloat(address_, v); }

void ParamControl::onLocalSlider() {
    localMs_ = nowMs();
    valFresh_ = false;
    value_ = valueFromPosition(slider_->GetValue());
    send(value_);
    updateReadout();
}

void ParamControl::onValue(float v) {
    if (def_.kind == Kind::Level) {
        value_ = v;
        haveValue_ = true;
        return;
    }
    if (def_.kind == Kind::Text || def_.kind == Kind::Trigger) return;

    const bool echo = echoing();
    const bool first = !haveValue_;
    bool changed = false;
    if (check_) {
        const bool on = v >= 0.5f;
        changed = check_->GetValue() != on;
        check_->SetValue(on);
    } else if (toggle_) {
        const bool on = v >= 0.5f;
        changed = toggle_->GetValue() != on;
        toggle_->SetValue(on);
    } else if (slider_) {
        // While the user is moving the slider, the echoes would make it jump back.
        if (!echo) {
            const int p = positionFromValue(v);
            changed = p != slider_->GetValue();
            slider_->SetValue(p);
        }
    } else if (spin_) {
        if (!echo) {
            const int n = static_cast<int>(std::lround(v));
            changed = n != spin_->GetValue();
            spin_->SetValue(n);
        }
    }
    value_ = v;
    haveValue_ = true;
    if (changed && def_.readout != Readout::None) valFresh_ = false;
    updateReadout();

    remoteAnnounced_ = false;
    if (!changed || first) return;
    const wxString key = wxString::FromUTF8(address_);
    if (!echo && mayAnnounce()) {
        ctx_.announceChange(key, accessibleName() + " " + displayText());
        remoteAnnounced_ = true;
    } else if (echo && (check_ || toggle_)) {
        // TotalMix FX did not accept our change (e.g. 48V on a channel without phantom power).
        ctx_.announceChange(key, accessibleName() + " " + displayText());
    }
}

void ParamControl::onText(const std::string& t) {
    const wxString s = wxString::FromUTF8(rme::scale::tidyDisplayText(t));
    const wxString key = wxString::FromUTF8(address_);
    if (text_) {
        if (s == text_->GetValue()) return;
        text_->ChangeValue(s);
        if (mayAnnounce() && !s.empty())
            ctx_.announceChange(key, def_.name == "trackname" ? s : accessibleName() + " " + s);
        return;
    }
    if (def_.kind == Kind::Level) {
        valText_ = s;
        valFresh_ = true;
        const int64_t now = nowMs();
        if (now - readoutMs_ >= kLevelRefreshMs) {
            readoutMs_ = now;
            updateReadout();
        }
        return;
    }
    if (s == valText_ && valFresh_) return;
    valText_ = s;
    valFresh_ = true;
    updateReadout();
    if (s.empty()) return;
    if (echoing()) {
        // After our own change: speak the value when only TotalMix FX knows its scale.
        if (!localScaleExact() ||
            (def_.readout == Readout::Selection && valDisplay().CmpNoCase(localText()) != 0))
            ctx_.announceChange(key, valDisplay());
    } else if (remoteAnnounced_) {
        ctx_.announceChange(key, accessibleName() + " " + valDisplay());  // replaces the waiting one
    }
}
