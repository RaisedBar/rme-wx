// The mixer row selector (TotalMix FX "Inputs / Playbacks / Outputs"), as three native radio
// buttons. Sends busInput / busPlayback / busOutput of its page; TotalMix FX answers with
// 1.0 for the selected row.
#pragma once

#include <wx/wx.h>

#include <cstdint>
#include <functional>
#include <string>

#include "AppContext.h"

class BusSelector {
public:
    enum Row { Inputs = 0, Playbacks = 1, Outputs = 2 };

    BusSelector(wxWindow* parent, AppContext& ctx, int page);
    ~BusSelector();

    wxStaticBoxSizer* sizer() const { return box_; }
    wxWindow* focusWindow() const;
    int row() const { return row_; }
    void select(int row);  // as if the user clicked it
    static wxString rowName(int row);

    std::function<void(int row)> onChanged;

private:
    void onRemote(int row, float v);

    AppContext& ctx_;
    std::string addr_[3];
    wxRadioButton* radio_[3] = {};
    wxStaticBoxSizer* box_ = nullptr;
    int row_ = Inputs;
    int64_t localMs_ = -100000;
};
