#include "MainFrame.h"

#include <wx/aboutdlg.h>
#include <wx/display.h>
#include <wx/numdlg.h>

#include "Accessibility.h"
#include "MixerPanel.h"
#include "OscSettingsDialog.h"
#include "PagePanel.h"
#include "Params.h"
#include "PortCheck.h"
#include "VoiceCommand.h"
#include "VoiceSettingsDialog.h"

namespace {
enum {
    ID_Settings = wxID_HIGHEST + 1,
    ID_Check,
    ID_Refresh,
    ID_QuickWorkspace,
    ID_RowInputs,
    ID_RowPlaybacks,
    ID_RowOutputs,
    ID_RowPrev,
    ID_RowNext,
    ID_BankPrev,
    ID_BankNext,
    ID_TrackPrev,
    ID_TrackNext,
    ID_SpeakLevels,
    ID_Page1,
    ID_Page4 = ID_Page1 + 3,
    ID_ShowLog,
    ID_ClearLog,
    ID_Shortcuts,
    ID_VoiceToggle,
    ID_VoiceLearn,
    ID_VoiceSettings,
    ID_VoiceCommands,
};
constexpr int64_t kVoiceQuietMs = 1000;  // after a voice command, while TotalMix re-sends the page
const char* kAppTitle = "TotalMix Access";
constexpr int64_t kQuietMs = 1500;
const char* const kPageNames[4] = {"Mixer", "Channel Settings", "FX and Snapshots", "Room EQ"};

int64_t nowMs() { return wxGetLocalTimeMillis().GetValue(); }
}  // namespace

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, kAppTitle, wxDefaultPosition, wxSize(1280, 860)), announcer_(this),
      timer_(this),
      seq_([this](const rme::OscMessage& m) { send(m); }) {
    {
        const int d = wxDisplay::GetFromWindow(this);
        const wxRect area = wxDisplay(d == wxNOT_FOUND ? 0u : static_cast<unsigned>(d)).GetClientArea();
        wxSize size = FromDIP(wxSize(1280, 860));
        size.DecTo(area.GetSize());
        SetSize(size);
        Centre();
    }
    wxString error;
    firstRun_ = !SettingsStore::load(settings_, error);
    announcer_.setDelayMs(static_cast<int>(settings_.announceDebounceSeconds * 1000.0 + 0.5));

    ctx_.send = [this](const rme::OscMessage& m) { send(m); };
    ctx_.log = [this](const wxString& s) { appendLog(s); };
    ctx_.announceChange = [this](const wxString& key, const wxString& text) {
        // A voice command walks TotalMix FX through pages and channels; only its result is spoken.
        if (seq_.busy() || nowMs() < voiceQuietUntil_) return;
        announcer_.post(key, text);
    };
    ctx_.announceStatus = [this](const wxString& text, bool problem) { announceStatus(text, problem); };
    ctx_.beginQuiet = [this]() { quietUntil_ = nowMs() + kQuietMs; };
    ctx_.quiet = [this]() { return seq_.busy() || nowMs() < quietUntil_; };

    splitter_ = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
    splitter_->SetMinimumPaneSize(FromDIP(60));
    splitter_->SetSashGravity(1.0);

    book_ = new wxNotebook(splitter_, wxID_ANY);
    a11y::setName(book_, "Pages");
    mixer_ = new MixerPanel(book_, ctx_, settings_.bankSize);
    book_->AddPage(mixer_, kPageNames[0], true);
    for (int p = 2; p <= 4; ++p) {
        pages_[p - 2] = new PagePanel(book_, ctx_, p);
        book_->AddPage(pages_[p - 2], kPageNames[p - 1]);
    }
    mixer_->onOpenSettings = [this](int strip, const wxString& name) {
        // setOffsetInBank selects the page 2 channel relative to the page 1 bank start.
        ctx_.sendFloat(rme::kSetOffsetInBank, static_cast<float>(strip));
        showPage(2);
        announcer_.post("page", "Channel Settings, " + name);
    };
    book_->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& e) {
        onPageChanged();
        e.Skip();
    });

    log_ = new wxTextCtrl(splitter_, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                          wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP | wxTE_RICH2);
    log_->SetFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE));
    a11y::setName(log_, "OSC log");
    splitter_->SplitHorizontally(book_, log_, -FromDIP(130));

    CreateStatusBar(4);
    int widths[] = {-3, -2, -1, -2};
    SetStatusWidths(4, widths);
    buildMenu();

    if (!error.empty()) appendLog(error);
    Bind(wxEVT_TIMER, &MainFrame::onTimer, this, timer_.GetId());
    timer_.Start(20);
    appendLog("TotalMix Access ready. TotalMix > OSC Settings (Ctrl+M) sets the connection; Help > Keyboard "
              "Shortcuts (F1) lists the shortcuts.");
    openConnection();
    updateVoiceStatus();
    if (settings_.voiceEnabled) CallAfter([this]() { startVoice(); });
    if (firstRun_) CallAfter([this]() { showSettings(); });
}

MainFrame::~MainFrame() {
    timer_.Stop();
    voice_.reset();
    // The panels unregister their controls from ctx_ when destroyed, so destroy them while
    // ctx_ still exists.
    splitter_->Destroy();
    sock_.close();
}

void MainFrame::buildMenu() {
    auto* file = new wxMenu;
    file->Append(wxID_EXIT, "E&xit");

    auto* tm = new wxMenu;
    tm->Append(ID_Settings, "OSC &Settings...\tCtrl+M");
    tm->Append(ID_Check, "Check &Connection\tCtrl+K");
    tm->Append(ID_Refresh, "&Refresh Page\tF5");
    tm->AppendSeparator();
    tm->Append(ID_QuickWorkspace, "Load &Quick Workspace...\tCtrl+Shift+W");

    auto* mix = new wxMenu;
    mix->Append(ID_RowInputs, "&Inputs Row\tCtrl+Shift+I");
    mix->Append(ID_RowPlaybacks, "&Playbacks Row\tCtrl+Shift+P");
    mix->Append(ID_RowOutputs, "&Outputs Row\tCtrl+Shift+O");
    mix->Append(ID_RowPrev, "Previous &Row\tAlt+Up");
    mix->Append(ID_RowNext, "Next Ro&w\tAlt+Down");
    mix->AppendSeparator();
    mix->Append(ID_BankPrev, "Previous &Bank\tCtrl+Shift+Left");
    mix->Append(ID_BankNext, "&Next Bank\tCtrl+Shift+Right");
    mix->Append(ID_TrackPrev, "Previous &Channel\tAlt+Shift+Left");
    mix->Append(ID_TrackNext, "Next C&hannel\tAlt+Shift+Right");
    mix->AppendSeparator();
    mix->Append(ID_SpeakLevels, "Speak &Levels\tCtrl+Shift+L");

    auto* voice = new wxMenu;
    voiceItem_ = voice->AppendCheckItem(ID_VoiceToggle, "&Voice Control\tCtrl+Shift+V");
    voice->Append(ID_VoiceLearn, "&Learn Channel Names");
    voice->Append(ID_VoiceSettings, "Voice &Settings...");

    auto* view = new wxMenu;
    for (int p = 1; p <= 4; ++p)
        view->Append(ID_Page1 + p - 1, wxString::Format("&%d %s\tCtrl+%d", p, kPageNames[p - 1], p));
    view->AppendSeparator();
    view->AppendCheckItem(ID_ShowLog, "OSC &Log")->Check(true);
    view->Append(ID_ClearLog, "&Clear Log");

    auto* help = new wxMenu;
    help->Append(ID_Shortcuts, "&Keyboard Shortcuts\tF1");
    help->Append(ID_VoiceCommands, "&Voice Commands");
    help->Append(wxID_ABOUT, "&About");

    auto* bar = new wxMenuBar;
    bar->Append(file, "&File");
    bar->Append(tm, "&TotalMix");
    bar->Append(mix, "&Mixer");
    bar->Append(voice, "V&oice");
    bar->Append(view, "&View");
    bar->Append(help, "&Help");
    SetMenuBar(bar);

    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { showSettings(); }, ID_Settings);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { checkConnection(); }, ID_Check);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        refreshPage();
        announceStatus("Refreshing " + wxString(kPageNames[currentPage() - 1]), false);
    }, ID_Refresh);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { loadQuickWorkspace(); }, ID_QuickWorkspace);
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) {
        ensureMixer();
        const int row = e.GetId() - ID_RowInputs;
        mixer_->selectRow(row);
        announcer_.post("row", BusSelector::rowName(row) + " row");
    }, ID_RowInputs, ID_RowOutputs);
    // Alt+Up / Alt+Down: Inputs <-> Playbacks <-> Outputs, only while the Mixer tab is shown.
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) {
        if (currentPage() != 1) {
            announcer_.post("row", "Alt+Up and Alt+Down change the row on the Mixer tab");
            return;
        }
        const int row = std::clamp(mixer_->row() + (e.GetId() == ID_RowNext ? 1 : -1), 0, 2);
        if (row == mixer_->row()) {
            announcer_.post("row", BusSelector::rowName(row) + (row == 0 ? " row, first" : " row, last"));
            return;
        }
        mixer_->selectRow(row);
        announcer_.post("row", BusSelector::rowName(row) + " row");
    }, ID_RowPrev, ID_RowNext);
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) {
        ensureMixer();
        static const char* const names[] = {"bank-", "bank+", "track-", "track+"};
        quietUntil_ = nowMs() + kQuietMs;
        ctx_.sendFloat(std::string("/1/") + names[e.GetId() - ID_BankPrev], 1.0f);
        // The new channel names are announced when TotalMix FX sends them.
    }, ID_BankPrev, ID_TrackNext);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { speakLevels(); }, ID_SpeakLevels);
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) { showPage(e.GetId() - ID_Page1 + 1); }, ID_Page1, ID_Page4);
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) {
        if (e.IsChecked() && !splitter_->IsSplit())
            splitter_->SplitHorizontally(book_, log_, -FromDIP(130));
        else if (!e.IsChecked() && splitter_->IsSplit())
            splitter_->Unsplit(log_);
    }, ID_ShowLog);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { log_->Clear(); }, ID_ClearLog);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { showShortcuts(); }, ID_Shortcuts);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { setVoiceEnabled(!(voice_ && voice_->running())); }, ID_VoiceToggle);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { learnChannelNames(); }, ID_VoiceLearn);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { showVoiceSettings(); }, ID_VoiceSettings);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { showVoiceCommands(); }, ID_VoiceCommands);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        wxAboutDialogInfo info;
        info.SetName(kAppTitle);
        info.SetVersion("0.1");
        info.SetDescription("An accessible remote control for RME TotalMix FX, using its OSC interface.\n"
                            "Built with wxWidgets. Based on RME's OSC table for TotalMix FX 1.96.");
        info.SetCopyright("(c) 2026 Raised Bar. MIT License.");
        wxAboutBox(info, this);
    }, wxID_ABOUT);
}

void MainFrame::showShortcuts() {
    wxMessageBox(
        "Pages:\n"
        "  Ctrl+1  Mixer       Ctrl+2  Channel Settings\n"
        "  Ctrl+3  FX and Snapshots       Ctrl+4  Room EQ\n"
        "  Ctrl+Tab / Ctrl+Shift+Tab  next / previous page\n"
        "\n"
        "Mixer:\n"
        "  Ctrl+Shift+I / P / O  Inputs / Playbacks / Outputs row\n"
        "  Alt+Up / Alt+Down  previous / next row\n"
        "  Ctrl+Shift+Left / Right  previous / next bank\n"
        "  Alt+Shift+Left / Right  previous / next channel\n"
        "  Ctrl+Shift+L  speak the level of the channel strip with the focus\n"
        "  Settings button in a strip: open that channel in Channel Settings\n"
        "\n"
        "Faders: Up / Down 0.5 dB, Page Up / Page Down 3 dB, Home / End maximum / minimum.\n"
        "\n"
        "TotalMix FX:\n"
        "  Ctrl+M  OSC settings      Ctrl+K  check connection\n"
        "  F5  refresh the page      Ctrl+Shift+W  load a Quick Workspace\n"
        "\n"
        "Voice:\n"
        "  Ctrl+Shift+V  voice control on / off (Help > Voice Commands lists the commands)\n",
        "Keyboard Shortcuts", wxOK | wxICON_INFORMATION, this);
}

int MainFrame::currentPage() const {
    const int sel = book_->GetSelection();
    return sel == wxNOT_FOUND ? 1 : sel + 1;
}

void MainFrame::showPage(int page) {
    if (page < 1 || page > 4) return;
    if (currentPage() != page) book_->SetSelection(static_cast<size_t>(page - 1));  // sends the page change
    if (page == 1)
        mixer_->takeFocus();
    else
        pages_[page - 2]->takeFocus();
}

void MainFrame::ensureMixer() {
    if (currentPage() != 1) showPage(1);
}

void MainFrame::onPageChanged() {
    if (seq_.busy()) {
        seq_.cancel();
        announceStatus("Voice command cancelled", false);
    }
    // TotalMix FX sends one page at a time; selecting a page makes it re-send that page.
    quietUntil_ = nowMs() + kQuietMs;
    send(rme::OscMessage(rme::pageAddress(currentPage())));
    updateStatus();
}

void MainFrame::refreshPage() {
    // Selecting the page TotalMix FX is already on may not re-send it, so go via another page.
    const int page = currentPage();
    quietUntil_ = nowMs() + kQuietMs;
    send(rme::OscMessage(rme::pageAddress(page == 1 ? 2 : 1)));
    send(rme::OscMessage(rme::pageAddress(page)));
}

void MainFrame::openConnection() {
    sock_.close();
    link_.reset();
    sendErrorShown_ = false;
    std::string err;
    if (!sock_.open(static_cast<uint16_t>(settings_.receivePort), err)) {
        announceStatus(wxString::FromUTF8(err) + ". Choose another Receive port in TotalMix > OSC Settings (and the "
                                                 "same 'Port outgoing' in TotalMix FX).",
                       true);
        updateStatus();
        return;
    }
    if (!sock_.setDestination(settings_.host.ToStdString(wxConvUTF8), static_cast<uint16_t>(settings_.sendPort), err)) {
        announceStatus(wxString::FromUTF8(err), true);
        updateStatus();
        return;
    }
    appendLog(wxString::Format("Listening on UDP port %d, sending to %s:%d", settings_.receivePort, settings_.host,
                               settings_.sendPort));
    updateStatus();
    checkConnection();
}

void MainFrame::checkConnection() {
    if (!sock_.isOpen() || !sock_.hasDestination()) {
        announceStatus("Not connected: check TotalMix > OSC Settings", true);
        return;
    }
    SetStatusText("Checking connection...", 0);
    link_.beginCheck(nowMs());
    refreshPage();
}

void MainFrame::send(const rme::OscMessage& m) {
    std::string err;
    if (!sock_.send(rme::encode(m), err)) {
        if (!sendErrorShown_) announceStatus("Could not send to TotalMix FX: " + wxString::FromUTF8(err), true);
        sendErrorShown_ = true;
        return;
    }
    sendErrorShown_ = false;
    appendLog("> " + wxString::FromUTF8(rme::toString(m)));
}

void MainFrame::dispatch(const rme::OscMessage& m) {
    const bool level = m.address.find("/level") != std::string::npos;
    if (settings_.logIncoming && (!level || settings_.logLevels)) appendLog("< " + wxString::FromUTF8(rme::toString(m)));
    seq_.onReceive(m);

    std::string base;
    const bool isVal = rme::splitValSuffix(m.address, base);
    const ControlRegistry::Target* t = ctx_.registry.find(base);
    if (!t && isVal) t = ctx_.registry.find(m.address);  // a name that really ends in "Val"
    if (!t) return;
    if (isVal || m.hasString()) {
        if (t->text) t->text(m.text());
    } else if (m.hasNumber()) {
        if (t->value) t->value(m.number());
    }
}

void MainFrame::onTimer(wxTimerEvent&) {
    int messages = 0, replies = 0;
    std::vector<rme::OscMessage> batch;
    sock_.receive([&](const uint8_t* data, size_t size) {
        batch.clear();
        rme::decodePacket(data, size, batch);
        for (const auto& m : batch) {
            dispatch(m);
            ++messages;
            // Level meters and the recorder clock arrive unasked; anything else answers us.
            if (m.address.find("/level") == std::string::npos && m.address.find("recordTime") == std::string::npos)
                ++replies;
        }
    });
    const int64_t now = nowMs();
    link_.onReceive(now, messages, replies);
    const wxString where = wxString::Format("%s:%d", settings_.host, settings_.sendPort);
    switch (link_.tick(now)) {
        case rme::Link::Event::Started: announceStatus("Receiving data from TotalMix FX", false); break;
        case rme::Link::Event::Stopped:
            announceStatus("TotalMix FX stopped sending. Check that it is running and OSC control is enabled.", true);
            break;
        case rme::Link::Event::CheckOk: announceStatus("TotalMix FX is responding at " + where, false); break;
        case rme::Link::Event::CheckFailed: announceStatus(noReplyMessage(), true); break;
        case rme::Link::Event::None: break;
    }
    if (const auto done = seq_.tick(now)) {
        voiceQuietUntil_ = now + kVoiceQuietMs;
        quietUntil_ = now + kQuietMs;
        announceStatus(wxString::FromUTF8(done->text), !done->ok);
        updateVoiceNames();
    }
    if (voice_ && voice_->running() && ++voiceNamesTick_ >= 50) {  // about once a second
        voiceNamesTick_ = 0;
        updateVoiceNames();
    }
    if (messages > 0 || !link_.receiving()) updateStatus();
    flushLog();
}

// ---- Voice control ---------------------------------------------------------------------------

void MainFrame::startVoice() {
    if (!VoiceInput::available()) {
        announceStatus("Voice control is only available on Windows so far", true);
        if (voiceItem_) voiceItem_->Check(false);
        return;
    }
    if (!voice_) {
        voice_ = std::make_unique<VoiceInput>(this);
        voice_->onPhrase = [this](const wxString& text, double confidence) { onVoicePhrase(text, confidence); };
        voice_->onStatus = [this](const wxString& text, bool problem) {
            announceStatus(text, problem);
            updateVoiceStatus();
        };
    }
    updateVoiceNames();
    VoiceInput::Options o;
    o.wakeWord = settings_.wakeWord;
    o.deviceName = settings_.voiceDevice;
    voice_->start(o);
    updateVoiceStatus();
}

void MainFrame::stopVoice() {
    if (voice_) voice_->stop();
    updateVoiceStatus();
}

void MainFrame::setVoiceEnabled(bool on) {
    settings_.voiceEnabled = on;
    wxString err;
    if (!SettingsStore::save(settings_, err)) appendLog(err);
    if (on) {
        startVoice();
    } else {
        stopVoice();
        announceStatus("Voice control off", false);
    }
}

void MainFrame::updateVoiceStatus() {
    const bool running = voice_ && voice_->running();
    if (voiceItem_) voiceItem_->Check(running);
    if (!GetStatusBar()) return;
    wxString text = "Voice: off";
    if (running) text = voice_->listening() ? "Voice: " + voice_->deviceName() : wxString("Voice: no headset");
    SetStatusText(text, 3);
}

std::vector<rme::voice::NamedChannel> MainFrame::voiceNames() const {
    std::vector<rme::voice::NamedChannel> out = seq_.learnedNames();
    const auto bus = static_cast<rme::voice::Bus>(mixer_->row());
    for (const wxString& n : mixer_->stripNames()) {
        if (n.empty()) continue;
        const std::string name = n.ToStdString(wxConvUTF8);
        bool known = false;
        for (const auto& o : out) known = known || (o.bus == bus && o.name == name);
        if (!known) out.push_back({bus, 0, name});
    }
    return out;
}

void MainFrame::updateVoiceNames() {
    if (!voice_) return;
    std::vector<std::string> spoken;
    for (const auto& n : voiceNames()) spoken.push_back(rme::voice::spokenName(n.name));
    voice_->setChannelNames(spoken);  // rebuilds the grammar only when the list changed
}

std::optional<rme::voice::ChannelTarget> MainFrame::resolveChannel(const rme::voice::ChannelRef& ch,
                                                                   wxString& error) const {
    // A name in the bank shown: select it there, so the mixer bank stays where it is.
    if (!ch.name.empty() && mixer_->row() == static_cast<int>(ch.bus)) {
        const wxString name = wxString::FromUTF8(ch.name).Trim().Trim(false);
        const auto names = mixer_->stripNames();
        for (size_t k = 0; k < names.size(); ++k)
            if (names[k] == name) return rme::voice::ChannelTarget{ch.bus, true, static_cast<int>(k)};
    }
    if (ch.number > 0) return rme::voice::ChannelTarget{ch.bus, false, ch.number - 1};
    error = wxString::FromUTF8(ch.name) +
            " is not in the mixer bank shown. Say its number, or use Voice, Learn Channel Names first.";
    return std::nullopt;
}

void MainFrame::onVoicePhrase(const wxString& text, double confidence) {
    appendLog(wxString::Format("Voice: heard \"%s\" (confidence %.0f%%)", text, confidence * 100.0));
    if (confidence < settings_.voiceMinConfidence) {
        appendLog("Voice: ignored, below the minimum confidence");
        return;
    }
    // The grammar's catch-all after the wake word comes back as "...". Ordinary talk often lands
    // there with modest confidence; only a clear "<wake word> <unknown words>" is worth saying.
    if (text.Contains("...")) {
        if (confidence >= 0.9)
            announceStatus("Voice command not understood", true);
        else
            appendLog("Voice: ignored, not a command");
        return;
    }
    const auto cmd = rme::voice::parseVoiceCommand(text.ToStdString(wxConvUTF8), voiceNames(),
                                                   settings_.wakeWord.ToStdString(wxConvUTF8));
    if (!cmd) {
        announceStatus("Voice command not understood: " + text, true);
        return;
    }
    if (cmd->action == rme::voice::Action::StopListening) {
        // Not from inside the recogniser's callback.
        CallAfter([this]() { setVoiceEnabled(false); });
        return;
    }
    std::optional<rme::voice::ChannelTarget> target;
    if (cmd->isChannelAction()) {
        wxString error;
        target = resolveChannel(cmd->channel, error);
        if (!target) {
            announceStatus(error, true);
            return;
        }
    }
    // Recognising a test WAV (TOTALMIX_VOICE_TEST_WAV) only logs the command, unless
    // TOTALMIX_VOICE_TEST_RUN=1: a misrecognised test phrase must not change the mix.
    wxString testWav, testRun;
    if (wxGetEnv("TOTALMIX_VOICE_TEST_WAV", &testWav) && !testWav.empty() &&
        !(wxGetEnv("TOTALMIX_VOICE_TEST_RUN", &testRun) && testRun == "1")) {
        appendLog("Voice test: would run " + wxString::FromUTF8(rme::voice::describe(*cmd)));
        return;
    }
    std::string err;
    if (!seq_.start(*cmd, target, currentPage(), nowMs(), err)) {
        announceStatus(wxString::FromUTF8(err), true);
        return;
    }
    appendLog("Voice: " + wxString::FromUTF8(rme::voice::describe(*cmd)));
}

void MainFrame::learnChannelNames() {
    if (!sock_.isOpen() || !sock_.hasDestination()) {
        announceStatus("Not connected: check TotalMix > OSC Settings", true);
        return;
    }
    std::string err;
    if (!seq_.startNameScan(currentPage(), nowMs(), err)) {
        announceStatus(wxString::FromUTF8(err), true);
        return;
    }
    announceStatus("Learning channel names from TotalMix FX. This can take a minute.", false);
}

void MainFrame::showVoiceSettings() {
    VoiceSettingsDialog dlg(this, settings_);
    if (dlg.ShowModal() != wxID_OK) return;
    settings_ = dlg.result();
    wxString err;
    if (!SettingsStore::save(settings_, err)) announceStatus(err, true);
    if (settings_.voiceEnabled)
        startVoice();  // restarts with the new wake word and microphone
    else if (voice_ && voice_->running())
        setVoiceEnabled(false);
}

void MainFrame::showVoiceCommands() {
    const wxString w = settings_.wakeWord;
    wxMessageBox(
        "Voice control listens on a headset microphone. Start every command with the wake word, \"" + w +
            "\".\n"
            "\n"
            "  " + w + " snapshot 1 to 8\n"
            "  " + w + " reverb on / off,  " + w + " echo on / off\n"
            "  " + w + " undo,  " + w + " redo,  " + w + " stop listening\n"
            "\n"
            "Channels: input, playback or output and a number (input 5), or a channel name (vocal).\n"
            "Names in the bank shown always work; Voice > Learn Channel Names learns the rest.\n"
            "For a stereo channel, use the number of its left channel.\n"
            "\n"
            "  " + w + " input 5 volume minus 10\n"
            "  " + w + " input 5 volume plus 3   (up to plus 6)\n"
            "  " + w + " input 5 volume zero / volume off\n"
            "  " + w + " input 5 volume up 3 / volume down 6   (raising stops at 0 dB)\n"
            "  " + w + " input 5 mute / unmute\n"
            "  " + w + " input 5 pan left 30 / pan right 100 / pan center\n"
            "  " + w + " input 5 effects send minus 12 / effects send off\n"
            "\n"
            "Add \"point five\" for half decibels (minus 3 point 5). Each command is confirmed when it is done.",
        "Voice Commands", wxOK | wxICON_INFORMATION, this);
}

wxString MainFrame::noReplyMessage() const {
    using R = rme::SendPortCheck::Result;
    const int port = settings_.sendPort;
    const rme::SendPortCheck c = rme::checkSendPort(settings_.host.ToStdString(wxConvUTF8), static_cast<uint16_t>(port));

    wxString text = "No reply from TotalMix FX.";
    // Level meters arrive unasked, so TotalMix FX can be sending to us while not receiving.
    if (link_.lastCheckGotData()) text += " It is sending level meters to this program, but did not react to its request.";

    // Where TotalMix FX really listens (only known when it runs on this computer).
    wxString listenHint;
    if (c.result == R::Other || c.result == R::Free) {
        const std::vector<uint16_t> tm = rme::totalMixListenPorts();
        if (tm.size() == 1) {
            listenHint = wxString::Format(" TotalMix FX is listening on port %d: set the Send port to %d in TotalMix > "
                                          "OSC Settings (Ctrl+M).",
                                          tm[0], tm[0]);
        } else if (tm.size() > 1) {
            wxString list;
            for (uint16_t p : tm) list += (list.empty() ? "" : ", ") + wxString::Format("%d", p);
            listenHint = " TotalMix FX is listening on ports " + list +
                         ": set the Send port to the 'Port incoming' of the remote controller you use, in TotalMix > "
                         "OSC Settings (Ctrl+M).";
        }
    }

    switch (c.result) {
        case R::Other: {
            text += wxString::Format(" UDP port %d on this computer is in use by %s, not by TotalMix FX, so TotalMix FX "
                                     "cannot receive OSC on it.",
                                     port, wxString::FromUTF8(c.process));
            if (!listenHint.empty()) return text + listenHint;
            // Suggest a free port for TotalMix FX's "Port incoming".
            int freePort = 0;
            for (int p = port + 1; p < port + 100 && p <= 65535 && !freePort; ++p) {
                bool supported = false;
                if (p != settings_.receivePort && rme::udpPortOwners(static_cast<uint16_t>(p), supported).empty() &&
                    supported)
                    freePort = p;
            }
            return text + wxString::Format(
                              " In TotalMix FX, Options > Settings > OSC, set 'Port incoming' to a free port%s, turn "
                              "Options > Enable OSC Control off and on, then set the same Send port here in TotalMix > "
                              "OSC Settings (Ctrl+M).",
                              freePort ? wxString::Format(" such as %d", freePort) : wxString());
        }
        case R::Free:
            text += wxString::Format(" Nothing on this computer is listening on UDP port %d.", port);
            if (!listenHint.empty()) return text + listenHint;
            return text + " TotalMix FX's OSC control is not active or uses another 'Port incoming'. In TotalMix FX "
                          "check Options > Enable OSC Control and Options > Settings > OSC.";
        case R::TotalMix:
            return text + wxString::Format(
                              " TotalMix FX is listening on port %d. In TotalMix FX, Options > Settings > OSC, check "
                              "that 'Remote Controller Address' is this computer and 'Port outgoing' is %d.",
                              port, settings_.receivePort);
        case R::Self:
            return text + wxString::Format(" The Send port and Receive port are both %d; they must be different. "
                                           "Change them in TotalMix > OSC Settings (Ctrl+M).",
                                           port);
        case R::NotChecked: break;
    }
    return text + wxString::Format(" Sending to %s:%d. In TotalMix FX check Options > Enable OSC Control and the OSC "
                                   "settings (TotalMix > OSC Settings here shows what to set).",
                                   settings_.host, port);
}

void MainFrame::appendLog(const wxString& line) {
    pendingLog_ += wxDateTime::UNow().Format("%H:%M:%S.%l ") + line + "\n";
}

void MainFrame::flushLog() {
    if (pendingLog_.empty()) return;
    // Keep the log to a reasonable size.
    constexpr long kMaxChars = 400000;
    if (log_->GetLastPosition() > kMaxChars) log_->Remove(0, log_->GetLastPosition() - kMaxChars / 2);
    log_->AppendText(pendingLog_);
    pendingLog_.clear();
}

void MainFrame::announceStatus(const wxString& text, bool problem) {
    appendLog((problem ? "Problem: " : "") + text);
    SetStatusText(text, 0);
    a11y::announce(this, text);
}

void MainFrame::updateStatus() {
    if (!GetStatusBar()) return;
    SetStatusText(sock_.isOpen() ? wxString::Format("To %s:%d, from port %d", settings_.host, settings_.sendPort,
                                                    settings_.receivePort)
                                 : wxString("Not connected"),
                  1);
    SetStatusText(link_.receiving() ? "TotalMix FX: receiving" : "TotalMix FX: no data", 2);
}

void MainFrame::showSettings() {
    OscSettingsDialog dlg(this, settings_);
    if (dlg.ShowModal() != wxID_OK) {
        if (firstRun_) appendLog("Using the default settings. TotalMix > OSC Settings (Ctrl+M) changes them.");
        firstRun_ = false;
        return;
    }
    firstRun_ = false;
    const Settings old = settings_;
    settings_ = dlg.result();
    wxString err;
    if (!SettingsStore::save(settings_, err)) announceStatus(err, true);
    if (settings_.bankSize != old.bankSize) {
        mixer_->setBankSize(settings_.bankSize);
        if (currentPage() == 1) refreshPage();
    }
    if (!settings_.sameConnection(old) || !sock_.isOpen())
        openConnection();
    else
        announceStatus("Settings saved", false);
}

void MainFrame::speakLevels() {
    wxString text;
    switch (currentPage()) {
        case 1:
            text = mixer_->levelSummary(wxWindow::FindFocus());
            if (text.empty()) text = "Move to a channel strip to hear its level";
            break;
        case 2: text = pages_[0]->levelSummary(); break;
        default: text = "This page has no level meters"; break;
    }
    if (text.empty()) text = "No level data";
    SetStatusText(text, 0);
    a11y::announce(this, text);
}

void MainFrame::loadQuickWorkspace() {
    const long n = wxGetNumberFromUser("Number of the Quick Workspace to load (1 to 30):", "Quick Workspace:",
                                       "Load Quick Workspace", 1, 1, 30, this);
    if (n < 1) return;
    quietUntil_ = nowMs() + kQuietMs;
    ctx_.sendFloat(rme::kLoadQuickWorkspace, static_cast<float>(n));
    announceStatus(wxString::Format("Quick Workspace %ld requested", n), false);
}
