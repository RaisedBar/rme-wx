#include "MixerPanel.h"

#include <algorithm>

#include "Accessibility.h"

using rme::Kind;

namespace {

rme::ParamDef stripDef(const rme::StripParamDef& sp) {
    rme::ParamDef d;
    d.page = 1;
    d.name = sp.base;
    d.label = sp.label;
    d.group = "Strip";
    d.kind = sp.kind;
    d.readout = sp.readout;
    d.help = sp.help;
    if (sp.readout == rme::Readout::Pan) d.steps = 200;
    if (sp.kind == Kind::Level) d.announce = rme::Announce::Never;
    return d;
}

int rowMask(int row) {
    switch (row) {
        case BusSelector::Inputs: return rme::kBusInput;
        case BusSelector::Playbacks: return rme::kBusPlayback;
        default: return rme::kBusOutput;
    }
}

}  // namespace

MixerPanel::MixerPanel(wxWindow* parent, AppContext& ctx, int bankSize) : wxPanel(parent, wxID_ANY), ctx_(ctx) {
    const int pad = FromDIP(4);
    auto* top = new wxBoxSizer(wxHORIZONTAL);

    // Row selector
    bus_ = std::make_unique<BusSelector>(this, ctx_, 1);
    bus_->onChanged = [this](int) { applyRow(); };
    top->Add(bus_->sizer(), 0, wxEXPAND | wxALL, pad);

    // Navigation
    auto* nav = new wxStaticBoxSizer(wxVERTICAL, this, "Navigation");
    wxWindow* navBox = nav->GetStaticBox();
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    for (const char* name : {"bank-", "bank+", "track-", "track+"}) {
        fixed_.push_back(std::make_unique<ParamControl>(navBox, ctx_, *rme::findParam(1, name)));
        buttons->Add(fixed_.back()->control(), 0, wxALL, FromDIP(2));
    }
    nav->Add(buttons, 0, wxALL, FromDIP(2));
    auto* navGrid = new wxFlexGridSizer(3, 0, 0);
    fixed_.push_back(std::make_unique<ParamControl>(navBox, ctx_, *rme::findParam(1, "labelSubmix")));
    fixed_.back()->addToGrid(navGrid);

    auto addSpin = [&](const wxString& label, const wxString& help, int max) {
        auto* st = new wxStaticText(navBox, wxID_ANY, label);
        auto* sp = new wxSpinCtrl(navBox, wxID_ANY, "", wxDefaultPosition, FromDIP(wxSize(80, -1)), wxSP_ARROW_KEYS, 1,
                                  max, 1);
        a11y::setName(sp, wxControl::RemoveMnemonics(label));
        a11y::setDescription(sp, help);
        navGrid->Add(st, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(2));
        navGrid->Add(sp, 0, wxALL, FromDIP(2));
        navGrid->Add(new wxStaticText(navBox, wxID_ANY, ""), 0, wxALL, FromDIP(2));
        return sp;
    };
    submix_ = addSpin("Su&bmix number", "Selects the submix (output) shown on the Inputs and Playbacks rows", 128);
    bankStart_ = addSpin("Bank &start", "First channel of the bank, counted in channels; stereo channels by their left channel", 256);
    submix_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) {
        if (ctx_.beginQuiet) ctx_.beginQuiet();
        ctx_.sendFloat(rme::kSetSubmix, static_cast<float>(submix_->GetValue() - 1));
    });
    bankStart_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) {
        if (ctx_.beginQuiet) ctx_.beginQuiet();
        ctx_.sendFloat(rme::kSetBankStart, static_cast<float>(bankStart_->GetValue() - 1));
    });
    nav->Add(navGrid, 0, wxALL, FromDIP(2));
    top->Add(nav, 0, wxEXPAND | wxALL, pad);

    // Channel strips
    stripsWin_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL | wxTAB_TRAVERSAL);
    stripsWin_->SetScrollRate(FromDIP(12), 0);
    stripsSizer_ = new wxBoxSizer(wxHORIZONTAL);
    stripsWin_->SetSizer(stripsSizer_);
    buildStrips(bankSize);

    // Control Room and Global, on the right as in TotalMix FX.
    auto* right = new wxBoxSizer(wxVERTICAL);
    auto* cr = new wxStaticBoxSizer(wxVERTICAL, this, "Control Room");
    for (const rme::ParamDef* d : rme::paramsForPage(1)) {
        if (d->group != "Control Room") continue;
        const bool fader = d->kind == Kind::Fader;
        fixed_.push_back(std::make_unique<ParamControl>(cr->GetStaticBox(), ctx_, *d, std::string(), fader));
        if (fader)
            fixed_.back()->addToColumn(cr);
        else
            cr->Add(fixed_.back()->control(), 0, wxALL, FromDIP(2));
    }
    right->Add(cr, 0, wxEXPAND | wxALL, pad);
    auto* global = new wxStaticBoxSizer(wxVERTICAL, this, "Global");
    for (const rme::ParamDef* d : rme::paramsForPage(1)) {
        if (d->group != "Global") continue;
        fixed_.push_back(std::make_unique<ParamControl>(global->GetStaticBox(), ctx_, *d));
        global->Add(fixed_.back()->control(), 0, wxALL, FromDIP(2));
    }
    right->Add(global, 0, wxEXPAND | wxALL, pad);

    auto* middle = new wxBoxSizer(wxHORIZONTAL);
    middle->Add(stripsWin_, 1, wxEXPAND | wxALL, pad);
    middle->Add(right, 0, wxEXPAND);

    auto* main = new wxBoxSizer(wxVERTICAL);
    main->Add(top, 0, wxEXPAND);
    main->Add(middle, 1, wxEXPAND);
    SetSizer(main);
    applyRow();
}

MixerPanel::~MixerPanel() { strips_.clear(); }

void MixerPanel::buildStrips(int count) {
    count = std::clamp(count, 1, rme::kMaxBankSize);
    const int pad = FromDIP(2);
    for (int n = 1; n <= count; ++n) {
        auto s = std::make_unique<Strip>();
        s->n = n;
        s->panel = new wxPanel(stripsWin_, wxID_ANY);
        s->box = new wxStaticBoxSizer(wxVERTICAL, s->panel, wxString::Format("Channel %d", n));
        s->panel->SetSizer(s->box);
        wxWindow* sb = s->box->GetStaticBox();
        for (const rme::StripParamDef& sp : rme::stripParams()) {
            if (sp.base == "trackname") continue;  // the strip's title
            const bool fader = sp.kind == Kind::Fader;
            s->controls.push_back(
                std::make_unique<ParamControl>(sb, ctx_, stripDef(sp), rme::stripAddress(sp.base, n), true));
            s->masks.push_back(sp.busMask);
            ParamControl& c = *s->controls.back();
            if (fader || c.def().kind == Kind::Lin || c.def().kind == Kind::Level)
                c.addToColumn(s->box);
            else
                s->box->Add(c.control(), 0, wxALL, pad);
            // Narrow horizontal sliders (pan, gain) so the strip stays narrow.
            if (c.def().kind == Kind::Lin) c.control()->SetMinSize(FromDIP(wxSize(90, -1)));
        }
        s->settings = new wxButton(sb, wxID_ANY, "Settings");
        a11y::setDescription(s->settings, "Opens this channel in the Channel Settings tab");
        Strip* raw = s.get();
        s->settings->Bind(wxEVT_BUTTON, [this, raw](wxCommandEvent&) {
            if (onOpenSettings) onOpenSettings(raw->n - 1, displayName(*raw));
        });
        s->box->Add(s->settings, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, pad);

        s->nameAddress = rme::stripAddress("trackname", n);
        ControlRegistry::Target t;
        t.text = [this, raw](const std::string& text) { setStripName(*raw, text); };
        ctx_.registry.add(s->nameAddress, std::move(t));

        stripsSizer_->Add(s->panel, 0, wxEXPAND | wxALL, pad);
        strips_.push_back(std::move(s));
        setStripName(*strips_.back(), "");
    }
}

void MixerPanel::destroyStrips() {
    for (auto& s : strips_) {
        ctx_.registry.remove(s->nameAddress);
        s->controls.clear();
        s->panel->Destroy();  // also removes it from stripsSizer_
    }
    strips_.clear();
}

void MixerPanel::setBankSize(int bankSize) {
    if (bankSize == this->bankSize()) return;
    destroyStrips();
    buildStrips(bankSize);
    applyRow();
}

std::vector<wxString> MixerPanel::stripNames() const {
    std::vector<wxString> out;
    for (const auto& s : strips_) out.push_back(s->name);
    return out;
}

wxString MixerPanel::displayName(const Strip& s) const {
    return s.name.empty() ? wxString::Format("Channel %d", s.n) : s.name;
}

void MixerPanel::setStripName(Strip& s, const std::string& text) {
    const wxString name = wxString::FromUTF8(text).Trim().Trim(false);
    const bool changed = name != s.name;
    s.name = name;
    const wxString shown = displayName(s);
    s.box->GetStaticBox()->SetLabel(shown);
    for (auto& c : s.controls) c->setNamePrefix(shown);
    a11y::setName(s.settings, shown + " Settings");
    if (changed && !text.empty()) announceBank();
}

void MixerPanel::announceBank() {
    // Said once the names have settled (the announcer is debounced): "Channels AN 1 to AN 8".
    wxString first, last;
    for (const auto& s : strips_) {
        if (s->name.empty()) continue;
        if (first.empty()) first = s->name;
        last = s->name;
    }
    if (first.empty() || !ctx_.announceChange) return;
    ctx_.announceChange("bank", first == last ? "Channel " + first : "Channels " + first + " to " + last);
}

void MixerPanel::applyRow() {
    const int mask = rowMask(bus_->row());
    wxWindow* focus = wxWindow::FindFocus();
    bool focusHidden = false;
    for (auto& s : strips_) {
        for (size_t i = 0; i < s->controls.size(); ++i) {
            const bool show = (s->masks[i] & mask) != 0;
            if (!show && s->controls[i]->owns(focus)) focusHidden = true;
            s->controls[i]->show(show);
        }
        if (focusHidden) {
            for (auto& c : s->controls)
                if (c->def().kind == Kind::Fader) c->control()->SetFocus();
            focusHidden = false;
        }
    }
    stripsWin_->Layout();
    stripsWin_->FitInside();
    Layout();
}

void MixerPanel::selectRow(int row) { bus_->select(row); }

void MixerPanel::takeFocus() {
    for (const auto& s : strips_)
        for (const auto& c : s->controls)
            if (c->def().kind == Kind::Fader) {
                c->control()->SetFocus();
                return;
            }
}

wxString MixerPanel::levelSummary(const wxWindow* focus) const {
    for (const auto& s : strips_) {
        if (!focus || !s->box->GetStaticBox()->IsDescendant(const_cast<wxWindow*>(focus))) continue;
        wxString left, right;
        for (const auto& c : s->controls) {
            if (c->def().name == "levelLeft") left = c->displayText();
            if (c->def().name == "levelRight") right = c->displayText();
        }
        return displayName(*s) + ": level left " + left + ", right " + right;
    }
    return {};
}
