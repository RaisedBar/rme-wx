#include "BusSelector.h"

#include "Accessibility.h"

namespace {
int64_t nowMs() { return wxGetLocalTimeMillis().GetValue(); }
const char* const kNames[3] = {"busInput", "busPlayback", "busOutput"};
}  // namespace

wxString BusSelector::rowName(int row) {
    static const char* const labels[3] = {"Inputs", "Playbacks", "Outputs"};
    return (row >= 0 && row < 3) ? labels[row] : "";
}

BusSelector::BusSelector(wxWindow* parent, AppContext& ctx, int page) : ctx_(ctx) {
    box_ = new wxStaticBoxSizer(wxVERTICAL, parent, "Row");
    wxWindow* sb = box_->GetStaticBox();
    for (int r = 0; r < 3; ++r) {
        addr_[r] = "/" + std::to_string(page) + "/" + kNames[r];
        radio_[r] = new wxRadioButton(sb, wxID_ANY, rowName(r), wxDefaultPosition, wxDefaultSize, r == 0 ? wxRB_GROUP : 0);
        a11y::setName(radio_[r], rowName(r));
        box_->Add(radio_[r], 0, wxALL, sb->FromDIP(3));
        radio_[r]->Bind(wxEVT_RADIOBUTTON, [this, r](wxCommandEvent&) { select(r); });
        ControlRegistry::Target t;
        t.value = [this, r](float v) { onRemote(r, v); };
        ctx_.registry.add(addr_[r], std::move(t));
    }
    radio_[Inputs]->SetValue(true);
}

BusSelector::~BusSelector() {
    for (const auto& a : addr_) ctx_.registry.remove(a);
}

wxWindow* BusSelector::focusWindow() const { return radio_[row_ >= 0 ? row_ : 0]; }

void BusSelector::select(int row) {
    if (row < 0 || row > 2) return;
    localMs_ = nowMs();
    if (!radio_[row]->GetValue()) radio_[row]->SetValue(true);
    if (ctx_.beginQuiet) ctx_.beginQuiet();
    ctx_.sendFloat(addr_[row], 1.0f);
    if (row != row_) {
        row_ = row;
        if (onChanged) onChanged(row_);
    }
}

void BusSelector::onRemote(int row, float v) {
    if (v < 0.5f || row == row_) {
        if (v >= 0.5f && !radio_[row]->GetValue()) radio_[row]->SetValue(true);
        return;
    }
    row_ = row;
    radio_[row]->SetValue(true);
    if (onChanged) onChanged(row_);
    // Changed in TotalMix FX (or by a shortcut): say so. A click on the radio button is spoken
    // by the screen reader itself.
    if (nowMs() - localMs_ > 1000 && ctx_.announceChange) ctx_.announceChange("row", rowName(row) + " row");
}
