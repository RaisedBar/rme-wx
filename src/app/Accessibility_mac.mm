// macOS implementation of the accessibility helpers: sets AppKit NSAccessibility properties
// on the control's native NSView so VoiceOver announces the right label, help and value.
#include "Accessibility.h"

#import <AppKit/AppKit.h>

namespace a11y {

namespace {

NSString* toNS(const wxString& s) {
    NSString* r = [NSString stringWithUTF8String:s.utf8_str().data()];
    return r ? r : @"";
}

// Apply to the control's view and, for scroll views (lists, multi-line text), to the
// document view that actually receives focus.
template <class F>
void apply(wxWindow* w, F f) {
    if (!w) return;
    NSView* v = (NSView*)w->GetHandle();
    if (!v) return;
    f(v);
    if ([v isKindOfClass:[NSScrollView class]]) {
        NSView* doc = [(NSScrollView*)v documentView];
        if (doc) f(doc);
    }
}

}  // namespace

void setName(wxWindow* w, const wxString& name) {
    apply(w, [&](NSView* v) { [v setAccessibilityLabel:toNS(name)]; });
}

void setDescription(wxWindow* w, const wxString& description) {
    apply(w, [&](NSView* v) { [v setAccessibilityHelp:toNS(description)]; });
}

void setValue(wxWindow* w, const wxString& value) {
    // For sliders this replaces the raw number VoiceOver would otherwise read ("139")
    // with the displayed text ("+12").
    apply(w, [&](NSView* v) { [v setAccessibilityValueDescription:toNS(value)]; });
}

void setGraphic(wxWindow* w, const wxString& name, const wxString& value) {
    apply(w, [&](NSView* v) {
        [v setAccessibilityElement:YES];
        [v setAccessibilityRole:NSAccessibilityImageRole];
        [v setAccessibilityLabel:toNS(name)];
        [v setAccessibilityValue:toNS(value)];
        [v setAccessibilityValueDescription:toNS(value)];
    });
}

void announce(wxWindow*, const wxString& text) {
    if (text.empty()) return;
    NSDictionary* info = @{
        NSAccessibilityAnnouncementKey : toNS(text),
        NSAccessibilityPriorityKey : @(NSAccessibilityPriorityHigh),
    };
    NSAccessibilityPostNotificationWithUserInfo(NSApp, NSAccessibilityAnnouncementRequestedNotification, info);
}

}  // namespace a11y
