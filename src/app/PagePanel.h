// A TotalMix FX OSC page built from the parameter table: page 2 (channel settings), page 3
// (reverb, echo, snapshots, groups, recorder) or page 4 (Room EQ). Each group of the table
// is a static box (announced by screen readers as a grouping) with a 3-column grid:
// label, control, value.
#pragma once

#include <wx/scrolwin.h>

#include <memory>
#include <vector>

#include "AppContext.h"
#include "BusSelector.h"
#include "ParamControls.h"

class PagePanel : public wxScrolledWindow {
public:
    PagePanel(wxWindow* parent, AppContext& ctx, int page);
    ~PagePanel() override;

    int page() const { return page_; }
    void takeFocus();
    // "AN 1: level left -12.0 dB, right -14.5 dB", or empty if the page has no levels.
    wxString levelSummary() const;
    BusSelector* busSelector() const { return bus_.get(); }

private:
    AppContext& ctx_;
    int page_;
    std::unique_ptr<BusSelector> bus_;
    std::vector<std::unique_ptr<ParamControl>> controls_;
};
