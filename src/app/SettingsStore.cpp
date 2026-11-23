#include "SettingsStore.h"

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/xml/xml.h>

#include <algorithm>

// File format (version 1):
//
// <?xml version="1.0" encoding="UTF-8"?>
// <TotalMixAccess version="1">
//   <Osc host="127.0.0.1" sendPort="7001" receivePort="9001"/>
//   <Mixer bankSize="8"/>                         <!-- 1 to 24 -->
//   <Log incoming="1" levels="0"/>
//   <Announcements debounceSeconds="0.05"/>      <!-- 0 to 5 seconds -->
//   <Voice enabled="0" wakeWord="TotalMix" minConfidence="0.5" device=""/>
// </TotalMixAccess>

namespace {

constexpr int kVersion = 1;
const char* const kRoot = "TotalMixAccess";

wxXmlNode* findChild(const wxXmlNode* parent, const wxString& name) {
    for (wxXmlNode* n = parent ? parent->GetChildren() : nullptr; n; n = n->GetNext())
        if (n->GetType() == wxXML_ELEMENT_NODE && n->GetName() == name) return n;
    return nullptr;
}

bool attrLong(const wxXmlNode* n, const wxString& name, long lo, long hi, int& out) {
    long v = 0;
    if (!n || !n->GetAttribute(name).Trim().Trim(false).ToLong(&v) || v < lo || v > hi) return false;
    out = static_cast<int>(v);
    return true;
}

bool attrBool(const wxXmlNode* n, const wxString& name, bool& out) {
    int v = 0;
    if (!attrLong(n, name, 0, 1, v)) return false;
    out = v != 0;
    return true;
}

wxXmlNode* addElement(wxXmlNode* parent, const wxString& name) {
    auto* n = new wxXmlNode(wxXML_ELEMENT_NODE, name);
    parent->AddChild(n);
    return n;
}

void addComment(wxXmlNode* parent, const wxString& text) {
    parent->AddChild(new wxXmlNode(wxXML_COMMENT_NODE, wxString(), text));
}

}  // namespace

wxString SettingsStore::filePath() {
    wxFileName fn(wxStandardPaths::Get().GetUserDataDir(), "settings.xml");
    return fn.GetFullPath();
}

bool SettingsStore::load(Settings& out, wxString& error) {
    error.clear();
    const wxString path = filePath();
    if (!wxFileName::FileExists(path)) return false;
    wxLogNull quiet;
    wxXmlDocument doc;
    if (!doc.Load(path)) {
        error = "Could not read " + path + " (not valid XML).";
        return false;
    }
    wxXmlNode* root = doc.GetRoot();
    if (!root || root->GetName() != kRoot) {
        error = path + " is not a TotalMix Access settings file.";
        return false;
    }
    long version = 0;
    root->GetAttribute("version", "0").ToLong(&version);
    if (version > kVersion) {
        error = path + " was written by a newer version of TotalMix Access.";
        return false;
    }
    Settings s;
    if (wxXmlNode* osc = findChild(root, "Osc")) {
        const wxString host = osc->GetAttribute("host").Trim().Trim(false);
        if (!host.empty()) s.host = host;
        attrLong(osc, "sendPort", 1, 65535, s.sendPort);
        attrLong(osc, "receivePort", 1, 65535, s.receivePort);
    }
    attrLong(findChild(root, "Mixer"), "bankSize", 1, 24, s.bankSize);
    if (wxXmlNode* log = findChild(root, "Log")) {
        attrBool(log, "incoming", s.logIncoming);
        attrBool(log, "levels", s.logLevels);
    }
    double v = 0;
    if (wxXmlNode* a = findChild(root, "Announcements"))
        if (a->GetAttribute("debounceSeconds").Trim().Trim(false).ToCDouble(&v) && v >= 0 &&
            v <= Settings::kMaxDebounceSeconds)
            s.announceDebounceSeconds = v;
    if (wxXmlNode* voice = findChild(root, "Voice")) {
        attrBool(voice, "enabled", s.voiceEnabled);
        const wxString wake = voice->GetAttribute("wakeWord").Trim().Trim(false);
        if (!wake.empty()) s.wakeWord = wake;
        if (voice->GetAttribute("minConfidence").Trim().Trim(false).ToCDouble(&v) && v >= 0 && v <= 1)
            s.voiceMinConfidence = v;
        s.voiceDevice = voice->GetAttribute("device").Trim().Trim(false);
    }
    out = s;
    return true;
}

bool SettingsStore::save(const Settings& in, wxString& error) {
    error.clear();
    const wxString path = filePath();
    wxFileName fn(path);
    if (!fn.DirExists() && !fn.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
        error = "Could not create the folder " + fn.GetPath();
        return false;
    }
    auto* root = new wxXmlNode(wxXML_ELEMENT_NODE, kRoot);
    root->AddAttribute("version", wxString::Format("%d", kVersion));
    wxXmlDocument doc;
    doc.SetRoot(root);

    addComment(root, " TotalMix FX address; sendPort = TotalMix 'Port incoming', receivePort = TotalMix 'Port outgoing'. ");
    wxXmlNode* osc = addElement(root, "Osc");
    osc->AddAttribute("host", in.host);
    osc->AddAttribute("sendPort", wxString::Format("%d", in.sendPort));
    osc->AddAttribute("receivePort", wxString::Format("%d", in.receivePort));
    addComment(root, " Faders per bank (1 to 24); must match 'Number of faders per bank' in TotalMix FX. ");
    addElement(root, "Mixer")->AddAttribute("bankSize", wxString::Format("%d", in.bankSize));
    wxXmlNode* log = addElement(root, "Log");
    log->AddAttribute("incoming", in.logIncoming ? "1" : "0");
    log->AddAttribute("levels", in.logLevels ? "1" : "0");
    addComment(root, " Seconds to wait after the last change from TotalMix FX before announcing it (0 to 5). ");
    addElement(root, "Announcements")->AddAttribute("debounceSeconds", wxString::FromCDouble(in.announceDebounceSeconds));
    addComment(root, " Voice control: wake word, minimum recognition confidence (0 to 1), microphone name (empty = "
                     "any headset). ");
    wxXmlNode* voice = addElement(root, "Voice");
    voice->AddAttribute("enabled", in.voiceEnabled ? "1" : "0");
    voice->AddAttribute("wakeWord", in.wakeWord);
    voice->AddAttribute("minConfidence", wxString::FromCDouble(in.voiceMinConfidence));
    voice->AddAttribute("device", in.voiceDevice);

    // Write to a temporary file and rename, so an interrupted save cannot corrupt the file.
    wxLogNull quiet;
    const wxString tmp = path + ".tmp";
    if (!doc.Save(tmp, 2)) {
        error = "Could not write " + tmp;
        return false;
    }
    if (!wxRenameFile(tmp, path, true)) {
        wxRemoveFile(tmp);
        error = "Could not replace " + path;
        return false;
    }
    return true;
}
