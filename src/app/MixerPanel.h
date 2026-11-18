// OSC page 1, laid out like the TotalMix FX mixer window: the row selector and bank
// navigation at the top, a bank of channel strips from left to right, and the Control Room
// section on the right.
//
// Each strip is a static box named after the channel. Top to bottom: gain and 48V (Inputs
// row only), pan, mute, solo (Inputs and Playbacks), cue (Outputs), select, the fader, the
// level and a Settings button (TotalMix's wrench), which opens the channel in the Channel
// Settings tab. Controls TotalMix FX does not support on the current row are hidden.
// Because every strip has the same controls, their accessible names start with the channel
// name ("AN 1 Volume").
#pragma once

#include <wx/scrolwin.h>
#include <wx/spinctrl.h>
#include <wx/wx.h>

#include <functional>
#include <memory>
#include <vector>

#include "AppContext.h"
#include "BusSelector.h"
#include "ParamControls.h"

class MixerPanel : public wxPanel {
public:
    MixerPanel(wxWindow* parent, AppContext& ctx, int bankSize);
    ~MixerPanel() override;

    void setBankSize(int bankSize);
    int bankSize() const { return static_cast<int>(strips_.size()); }
    void selectRow(int row);
    int row() const { return bus_->row(); }
    void takeFocus();
    // Level of the strip that contains 'focus' (empty if the focus is not in a strip).
    wxString levelSummary(const wxWindow* focus) const;

    // The Settings button of strip 'strip' (0-based offset in the bank) was pressed.
    std::function<void(int strip, const wxString& name)> onOpenSettings;

private:
    struct Strip {
        int n = 0;  // 1-based
        wxPanel* panel = nullptr;  // holds the strip; destroying it removes everything
        wxStaticBoxSizer* box = nullptr;
        wxString name;
        std::vector<std::unique_ptr<ParamControl>> controls;
        std::vector<int> masks;
        wxButton* settings = nullptr;
        std::string nameAddress;
    };

    void buildStrips(int count);
    void destroyStrips();
    void applyRow();
    void setStripName(Strip& s, const std::string& text);
    wxString displayName(const Strip& s) const;
    void announceBank();

    AppContext& ctx_;
    std::unique_ptr<BusSelector> bus_;
    std::vector<std::unique_ptr<ParamControl>> fixed_;  // navigation, Control Room, Global
    wxScrolledWindow* stripsWin_ = nullptr;
    wxBoxSizer* stripsSizer_ = nullptr;
    std::vector<std::unique_ptr<Strip>> strips_;
    wxSpinCtrl* submix_ = nullptr;
    wxSpinCtrl* bankStart_ = nullptr;
};
