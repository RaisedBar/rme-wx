// One TotalMix FX parameter shown as a native control: check box (toggles), push button
// (triggers), toggle button (snapshots), slider (faders, linear and frequency values), spin
// control (direct values), read-only text (names, states) or static text (level meters).
//
// Accessibility:
// - The accessible name is the parameter label, with an optional prefix (the channel name in
//   mixer strips, where every strip has the same controls).
// - A slider's accessible value is the displayed text ("-5.5 dB", "L20", "Bell"), not the
//   slider position. The text is computed locally where the scale is known and replaced by
//   TotalMix FX's own display string ("Val") when it arrives.
// - Changes received from TotalMix FX are announced (debounced), unless they are the echo of
//   our own change or part of a page refresh. After our own change of a value whose scale is
//   only known to TotalMix FX (e.g. compressor ratio), its display string is announced.
#pragma once

#include <wx/wx.h>
#include <wx/spinctrl.h>
#include <wx/tglbtn.h>

#include <cstdint>
#include <functional>
#include <string>

#include "AppContext.h"
#include "Params.h"

class ParamControl {
public:
    // 'address' defaults to def.address(). 'vertical' makes sliders vertical (faders).
    ParamControl(wxWindow* parent, AppContext& ctx, rme::ParamDef def, std::string address = {}, bool vertical = false);
    ~ParamControl();
    ParamControl(const ParamControl&) = delete;
    ParamControl& operator=(const ParamControl&) = delete;

    // Three cells (label, control, value) for a 3-column wxFlexGridSizer.
    void addToGrid(wxSizer* grid);
    // Label, control and value stacked vertically (mixer strips).
    void addToColumn(wxSizer* column);

    const rme::ParamDef& def() const { return def_; }
    const std::string& address() const { return address_; }
    wxWindow* control() const { return ctrl_; }
    bool owns(const wxWindow* w) const;

    void setNamePrefix(const wxString& prefix);
    wxString accessibleName() const;
    wxString displayText() const;
    void show(bool show);
    // Forget values received so far (e.g. the bank moved on to other channels).
    void clear();

private:
    void onValue(float v);
    void onText(const std::string& t);
    void onLocalSlider();
    void send(float v);
    int positionFromValue(float v) const;
    float valueFromPosition(int pos) const;
    wxString localText() const;
    wxString valDisplay() const;
    void updateReadout();
    bool echoing() const;
    bool localScaleExact() const;
    bool mayAnnounce() const;
    bool isSlider() const;

    AppContext& ctx_;
    rme::ParamDef def_;
    std::string address_;
    wxString prefix_;

    wxStaticText* label_ = nullptr;   // may be null (check boxes, buttons)
    wxWindow* spacer_ = nullptr;      // empty first grid cell for check boxes / buttons
    wxWindow* ctrl_ = nullptr;
    wxStaticText* readout_ = nullptr; // visible value text (sliders, spin controls, levels)
    wxWindow* spacer2_ = nullptr;     // empty third grid cell
    wxSlider* slider_ = nullptr;
    wxCheckBox* check_ = nullptr;
    wxToggleButton* toggle_ = nullptr;
    wxSpinCtrl* spin_ = nullptr;
    wxTextCtrl* text_ = nullptr;

    float value_ = 0.0f;
    bool haveValue_ = false;
    wxString valText_;        // TotalMix display string
    bool valFresh_ = false;   // valText_ belongs to the current value
    bool remoteAnnounced_ = false;  // a remote change was announced; its Val should replace it
    int64_t localMs_ = -100000;
    int64_t readoutMs_ = 0;
};
