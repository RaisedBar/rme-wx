#include "PagePanel.h"

#include <wx/wrapsizer.h>

#include "Accessibility.h"

PagePanel::PagePanel(wxWindow* parent, AppContext& ctx, int page)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxTAB_TRAVERSAL),
      ctx_(ctx),
      page_(page) {
    SetScrollRate(0, FromDIP(12));
    auto* wrap = new wxWrapSizer(wxHORIZONTAL);
    const auto params = rme::paramsForPage(page);
    for (const std::string& group : rme::groupsForPage(page)) {
        if (group == "Row") {
            bus_ = std::make_unique<BusSelector>(this, ctx_, page);
            wrap->Add(bus_->sizer(), 0, wxALL, FromDIP(4));
            continue;
        }
        auto* box = new wxStaticBoxSizer(wxVERTICAL, this, wxString::FromUTF8(group));
        wxWindow* sb = box->GetStaticBox();
        auto* grid = new wxFlexGridSizer(3, 0, 0);
        grid->AddGrowableCol(1);
        for (const rme::ParamDef* d : params) {
            if (d->group != group) continue;
            controls_.push_back(std::make_unique<ParamControl>(sb, ctx_, *d));
            controls_.back()->addToGrid(grid);
        }
        box->Add(grid, 1, wxEXPAND | wxALL, FromDIP(2));
        wrap->Add(box, 0, wxALL, FromDIP(4));
    }
    SetSizer(wrap);
    FitInside();
}

PagePanel::~PagePanel() = default;

void PagePanel::takeFocus() {
    if (bus_) {
        bus_->focusWindow()->SetFocus();
        return;
    }
    for (const auto& c : controls_)
        if (c->control()->AcceptsFocus()) {
            c->control()->SetFocus();
            return;
        }
}

wxString PagePanel::levelSummary() const {
    wxString name, left, right;
    for (const auto& c : controls_) {
        if (c->def().name == "trackname") name = c->displayText();
        if (c->def().name == "levelLeft") left = c->displayText();
        if (c->def().name == "levelRight") right = c->displayText();
    }
    if (left.empty()) return {};
    wxString s = (name.empty() ? wxString("Channel") : name) + ": level left " + left;
    if (!right.empty()) s += ", right " + right;
    return s;
}
