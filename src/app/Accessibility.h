// Accessibility helpers: give native controls explicit accessible names, descriptions and
// values via Microsoft Active Accessibility "Dynamic Annotation" (IAccPropServices).
// Screen readers (JAWS, NVDA, Narrator) read these through MSAA and UI Automation.
#pragma once

#include <wx/window.h>

namespace a11y {

// Accessible name, e.g. "Oscillator 2, Frequency". Use for every control without its own text.
void setName(wxWindow* w, const wxString& name);
// Longer help text (e.g. range / NRPN number).
void setDescription(wxWindow* w, const wxString& description);
// Text reported as the control's value (e.g. "+12" for a bipolar slider at 139).
void setValue(wxWindow* w, const wxString& value);
// Mark a custom-drawn window as a graphic with a name and a textual value.
void setGraphic(wxWindow* w, const wxString& name, const wxString& value);

// Ask the screen reader to speak 'text' now, without moving focus.
// Windows: UI Automation notification event (Windows 10 1709+; Narrator, NVDA, recent JAWS).
// macOS: NSAccessibilityAnnouncementRequestedNotification (VoiceOver).
void announce(wxWindow* source, const wxString& text);

}  // namespace a11y
