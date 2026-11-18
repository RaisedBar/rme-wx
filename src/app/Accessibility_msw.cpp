#include "Accessibility.h"

#include <wx/access.h>

#include <windows.h>
#include <initguid.h>  // define the PROPID_ACC_* GUIDs in this translation unit
#include <oleacc.h>

#include <unordered_set>

// Two mechanisms are used together:
//
// 1. wxAccessible: the window answers WM_GETOBJECT with its own IAccessible, which overrides
//    name / description / value / role and forwards everything else to the standard Windows
//    implementation. Because the window then "natively" implements MSAA, UI Automation
//    clients (Narrator, newer screen reader code paths) see the same name as MSAA clients.
//    Without this, UIA's built-in proxies for trackbars, edits, list views and check boxes
//    ignore MSAA names entirely.
// 2. Dynamic annotation (IAccPropServices) as a fallback for MSAA clients, e.g. if a control
//    class bypasses wxWidgets' WM_GETOBJECT handling.

namespace a11y {

namespace {

// ---- wxAccessible override ------------------------------------------------------------
class NamedAccessible : public wxAccessible {
public:
    explicit NamedAccessible(wxWindow* w) : wxAccessible(w) {}

    wxString name, description, value;
    bool graphic = false;

    wxAccStatus GetName(int childId, wxString* out) override {
        if (childId != wxACC_SELF || name.empty()) return wxACC_NOT_IMPLEMENTED;
        *out = name;
        return wxACC_OK;
    }
    wxAccStatus GetDescription(int childId, wxString* out) override {
        if (childId != wxACC_SELF || description.empty()) return wxACC_NOT_IMPLEMENTED;
        *out = description;
        return wxACC_OK;
    }
    // Also exposed as help text, which UI Automation reports as HelpText.
    wxAccStatus GetHelpText(int childId, wxString* out) override {
        if (childId != wxACC_SELF || description.empty()) return wxACC_NOT_IMPLEMENTED;
        *out = description;
        return wxACC_OK;
    }
    wxAccStatus GetValue(int childId, wxString* out) override {
        if (childId != wxACC_SELF || value.empty()) return wxACC_NOT_IMPLEMENTED;
        *out = value;
        return wxACC_OK;
    }
    wxAccStatus GetRole(int childId, wxAccRole* role) override {
        if (childId != wxACC_SELF || !graphic) return wxACC_NOT_IMPLEMENTED;
        *role = wxROLE_SYSTEM_GRAPHIC;
        return wxACC_OK;
    }
};

NamedAccessible* accessibleFor(wxWindow* w) {
    if (auto* existing = dynamic_cast<NamedAccessible*>(w->GetAccessible())) return existing;
    auto* acc = new NamedAccessible(w);
    w->SetAccessible(acc);  // the window takes ownership
    return acc;
}

// ---- Dynamic annotation fallback ------------------------------------------------------
// OBJID_CLIENT is a negative LONG; the annotation API takes it as a DWORD.
constexpr DWORD kClient = static_cast<DWORD>(OBJID_CLIENT);

IAccPropServices* services() {
    static IAccPropServices* svc = [] {
        IAccPropServices* p = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(CAccPropServices), nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&p))))
            p = nullptr;
        return p;
    }();
    return svc;
}

// Windows we have annotated; their annotations are cleared when they are destroyed.
std::unordered_set<HWND>& annotated() {
    static std::unordered_set<HWND> s;
    return s;
}

void track(wxWindow* w, HWND hwnd) {
    if (!annotated().insert(hwnd).second) return;
    w->Bind(wxEVT_DESTROY, [hwnd](wxWindowDestroyEvent& e) {
        if (annotated().erase(hwnd)) {
            if (IAccPropServices* svc = services()) {
                MSAAPROPID props[] = {PROPID_ACC_NAME, PROPID_ACC_DESCRIPTION, PROPID_ACC_VALUE, PROPID_ACC_ROLE};
                svc->ClearHwndProps(hwnd, kClient, CHILDID_SELF, props, ARRAYSIZE(props));
            }
        }
        e.Skip();
    });
}

void annotate(wxWindow* w, const MSAAPROPID& prop, const wxString& text) {
    HWND hwnd = static_cast<HWND>(w->GetHWND());
    IAccPropServices* svc = services();
    if (!hwnd || !svc) return;
    svc->SetHwndPropStr(hwnd, kClient, CHILDID_SELF, prop, text.wc_str());
    track(w, hwnd);
}

}  // namespace

void setName(wxWindow* w, const wxString& name) {
    if (!w) return;
    accessibleFor(w)->name = name;
    annotate(w, PROPID_ACC_NAME, name);
}

void setDescription(wxWindow* w, const wxString& description) {
    if (!w) return;
    accessibleFor(w)->description = description;
    annotate(w, PROPID_ACC_DESCRIPTION, description);
}

void setValue(wxWindow* w, const wxString& value) {
    if (!w) return;
    accessibleFor(w)->value = value;
    annotate(w, PROPID_ACC_VALUE, value);
}

void setGraphic(wxWindow* w, const wxString& name, const wxString& value) {
    if (!w) return;
    accessibleFor(w)->graphic = true;
    setName(w, name);
    setValue(w, value);
    HWND hwnd = static_cast<HWND>(w->GetHWND());
    if (IAccPropServices* svc = services()) {
        VARIANT v;
        v.vt = VT_I4;
        v.lVal = ROLE_SYSTEM_GRAPHIC;
        svc->SetHwndProp(hwnd, kClient, CHILDID_SELF, PROPID_ACC_ROLE, v);
    }
}

}  // namespace a11y
