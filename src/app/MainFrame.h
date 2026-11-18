#pragma once

#include <wx/notebook.h>
#include <wx/splitter.h>
#include <wx/timer.h>
#include <wx/wx.h>

#include <cstdint>

#include "AppContext.h"
#include "ChangeAnnouncer.h"
#include "Link.h"
#include "SettingsStore.h"
#include "UdpSocket.h"

class MixerPanel;
class PagePanel;

class MainFrame : public wxFrame {
public:
    MainFrame();
    ~MainFrame() override;

private:
    void buildMenu();
    void openConnection();
    void onTimer(wxTimerEvent&);
    void dispatch(const rme::OscMessage& m);
    void send(const rme::OscMessage& m);
    void appendLog(const wxString& line);
    void flushLog();
    void announceStatus(const wxString& text, bool problem);
    void updateStatus();
    void showSettings();
    void checkConnection();
    // Why TotalMix FX did not answer, with what to do (inspects the ports on this computer).
    wxString noReplyMessage() const;
    void refreshPage();
    void onPageChanged();
    void showPage(int page);  // 1..4; moves the focus into the page
    void ensureMixer();
    void speakLevels();
    void loadQuickWorkspace();
    void showShortcuts();
    int currentPage() const;

    AppContext ctx_;
    Settings settings_;
    rme::UdpSocket sock_;
    rme::Link link_;
    ChangeAnnouncer announcer_;
    wxTimer timer_;

    wxSplitterWindow* splitter_ = nullptr;
    wxNotebook* book_ = nullptr;
    MixerPanel* mixer_ = nullptr;
    PagePanel* pages_[3] = {};  // pages 2, 3, 4
    wxTextCtrl* log_ = nullptr;

    wxString pendingLog_;
    int64_t quietUntil_ = 0;
    bool sendErrorShown_ = false;
    bool firstRun_ = false;
};
