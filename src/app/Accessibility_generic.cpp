// Accessibility helpers for platforms without a specific implementation (e.g. GTK).
// Not yet implemented beyond tooltips: a GTK version would set ATK accessible names on the
// native widgets. Only Windows and macOS are supported targets.
#include "Accessibility.h"

#include <wx/tooltip.h>

namespace a11y {

void setName(wxWindow* w, const wxString& name) {
    if (w && w->GetToolTipText().empty()) w->SetToolTip(name);
}

void setDescription(wxWindow*, const wxString&) {}

void setValue(wxWindow*, const wxString&) {}

void setGraphic(wxWindow* w, const wxString& name, const wxString& value) {
    if (w) w->SetToolTip(name + ": " + value);
}

void announce(wxWindow*, const wxString&) {}

}  // namespace a11y
