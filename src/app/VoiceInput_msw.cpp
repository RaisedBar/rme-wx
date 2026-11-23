// Windows voice input: SAPI 5 in-process recogniser, command grammar, headset microphones only.
#include "VoiceInput.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <mmsystem.h>
#include <sapi.h>
#include <wrl/client.h>

#include <wx/app.h>

#include <algorithm>
#include <atomic>
#include <map>

#include "VoiceCommand.h"

using Microsoft::WRL::ComPtr;

namespace {

#ifndef DRV_QUERYFUNCTIONINSTANCEID
#define DRV_QUERYFUNCTIONINSTANCEID (DRV_RESERVED + 17)
#define DRV_QUERYFUNCTIONINSTANCEIDSIZE (DRV_RESERVED + 18)
#endif

// Defined here rather than through initguid.h / propsys.
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
const PROPERTYKEY kFormFactor = {{0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}}, 0};

std::wstring wide(const std::string& utf8) { return wxString::FromUTF8(utf8).ToStdWstring(); }

bool nameSaysHeadset(const wxString& name) {
    const wxString n = name.Lower();
    return n.Contains("headset") || n.Contains("hands-free") || n.Contains("handsfree") || n.Contains("headphone");
}

std::vector<VoiceInput::Device> enumerateCapture() {
    std::vector<VoiceInput::Device> out;
    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))) return out;
    ComPtr<IMMDeviceCollection> col;
    if (FAILED(en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &col))) return out;
    UINT count = 0;
    col->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> dev;
        if (FAILED(col->Item(i, &dev))) continue;
        VoiceInput::Device d;
        LPWSTR id = nullptr;
        if (SUCCEEDED(dev->GetId(&id))) {
            d.id = id;
            CoTaskMemFree(id);
        }
        ComPtr<IPropertyStore> ps;
        UINT formFactor = UnknownFormFactor;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(ps->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR) d.name = v.pwszVal;
            PropVariantClear(&v);
            if (SUCCEEDED(ps->GetValue(kFormFactor, &v)) && v.vt == VT_UI4) formFactor = v.ulVal;
            PropVariantClear(&v);
        }
        d.headset = formFactor == Headset || formFactor == Handset || nameSaysHeadset(d.name);
        out.push_back(d);
    }
    return out;
}

// The waveIn device number of a Core Audio capture endpoint (SAPI's audio input uses waveIn).
int waveInIdFor(const wxString& endpointId) {
    const UINT n = waveInGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        ULONG size = 0;
        const HWAVEIN h = reinterpret_cast<HWAVEIN>(static_cast<UINT_PTR>(i));
        if (waveInMessage(h, DRV_QUERYFUNCTIONINSTANCEIDSIZE, reinterpret_cast<DWORD_PTR>(&size), 0) != MMSYSERR_NOERROR ||
            size == 0)
            continue;
        std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
        if (waveInMessage(h, DRV_QUERYFUNCTIONINSTANCEID, reinterpret_cast<DWORD_PTR>(buf.data()), size) !=
            MMSYSERR_NOERROR)
            continue;
        if (wxString(buf.c_str()).IsSameAs(endpointId, false)) return static_cast<int>(i);
    }
    return -1;
}

// SAPI's audio input object for a waveIn device. A newly created SpMMAudioIn is already bound to
// the default microphone (SetDeviceId fails with SPERR_ALREADY_INITIALIZED), so use the audio
// input tokens SAPI lists, one per waveIn device, and match the device number; the token name
// (the waveIn name, cut to 31 characters) is the fallback.
ComPtr<ISpMMSysAudio> audioInputFor(UINT waveId, const wxString& endpointName) {
    ComPtr<ISpObjectTokenCategory> cat;
    ComPtr<IEnumSpObjectTokens> e;
    if (FAILED(CoCreateInstance(__uuidof(SpObjectTokenCategory), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&cat))) ||
        FAILED(cat->SetId(SPCAT_AUDIOIN, FALSE)) || FAILED(cat->EnumTokens(nullptr, nullptr, &e)))
        return nullptr;
    ComPtr<ISpMMSysAudio> byName;
    for (;;) {
        ComPtr<ISpObjectToken> tok;
        ULONG got = 0;
        if (e->Next(1, &tok, &got) != S_OK || got != 1) break;
        ComPtr<ISpMMSysAudio> audio;
        if (FAILED(tok->CreateInstance(nullptr, CLSCTX_ALL, IID_PPV_ARGS(&audio)))) continue;
        UINT id = 0;
        if (SUCCEEDED(audio->GetDeviceId(&id)) && id == waveId) return audio;
        LPWSTR desc = nullptr;
        if (!byName && SUCCEEDED(tok->GetStringValue(nullptr, &desc)) && desc) {
            const wxString d(desc);
            if (!d.empty() && endpointName.Lower().StartsWith(d.Lower())) byName = audio;
        }
        if (desc) CoTaskMemFree(desc);
    }
    return byName;
}

void clearEvent(SPEVENT& e) {
    switch (e.elParamType) {
        case SPET_LPARAM_IS_OBJECT:
            if (e.lParam) reinterpret_cast<IUnknown*>(e.lParam)->Release();
            break;
        case SPET_LPARAM_IS_POINTER:
        case SPET_LPARAM_IS_STRING:
            if (e.lParam) CoTaskMemFree(reinterpret_cast<void*>(e.lParam));
            break;
        default: break;
    }
    e = SPEVENT{};
}

// Reports endpoint changes (any thread) to a callback.
class DeviceWatcher : public IMMNotificationClient {
public:
    explicit DeviceWatcher(std::function<void()> changed) : changed_(std::move(changed)) {}
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = --ref_;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
            *out = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return notify(); }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return notify(); }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return notify(); }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    virtual ~DeviceWatcher() = default;
    HRESULT notify() {
        if (changed_) changed_();
        return S_OK;
    }
    std::atomic<ULONG> ref_{1};
    std::function<void()> changed_;
};

// Finds an English SAPI recogniser; returns its token and LANGID.
ComPtr<ISpObjectToken> findRecognizer(LANGID& lang, wxString& name) {
    ComPtr<ISpObjectTokenCategory> cat;
    if (FAILED(CoCreateInstance(__uuidof(SpObjectTokenCategory), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&cat))) ||
        FAILED(cat->SetId(SPCAT_RECOGNIZERS, FALSE)))
        return nullptr;
    // US, UK, Australia, Canada, India, Ireland, New Zealand, South Africa English.
    for (const wchar_t* l : {L"409", L"809", L"c09", L"1009", L"4009", L"1809", L"1409", L"1c09"}) {
        ComPtr<IEnumSpObjectTokens> e;
        const std::wstring req = std::wstring(L"Language=") + l;
        if (FAILED(cat->EnumTokens(req.c_str(), nullptr, &e))) continue;
        ComPtr<ISpObjectToken> tok;
        ULONG got = 0;
        if (e->Next(1, &tok, &got) == S_OK && got == 1) {
            lang = static_cast<LANGID>(wcstoul(l, nullptr, 16));
            LPWSTR desc = nullptr;
            if (SUCCEEDED(tok->GetStringValue(nullptr, &desc)) && desc) {
                name = desc;
                CoTaskMemFree(desc);
            }
            return tok;
        }
    }
    return nullptr;
}

}  // namespace

struct VoiceInput::Impl {
    VoiceInput* outer = nullptr;
    wxEvtHandler* owner = nullptr;
    Options opt;
    bool running = false;
    bool listening = false;
    wxString deviceId, deviceName;
    LANGID lang = 0x409;
    std::vector<std::string> names;
    std::shared_ptr<int> alive = std::make_shared<int>(0);

    ComPtr<ISpRecognizer> reco;
    ComPtr<ISpRecoContext> ctx;
    ComPtr<ISpRecoGrammar> grammar;
    ComPtr<IMMDeviceEnumerator> devices;
    DeviceWatcher* watcher = nullptr;

    void status(const wxString& text, bool problem) {
        if (outer->onStatus) outer->onStatus(text, problem);
    }

    static void __stdcall onNotify(WPARAM, LPARAM lp) { reinterpret_cast<Impl*>(lp)->drain(); }

    void start(const Options& o) {
        stop();
        opt = o;
        if (FAILED(CoCreateInstance(__uuidof(SpInprocRecognizer), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&reco)))) {
            status("Speech recognition (SAPI) is not available on this computer", true);
            return;
        }
        wxString engine;
        ComPtr<ISpObjectToken> tok = findRecognizer(lang, engine);
        if (!tok) {
            reco.Reset();
            status("No English speech recogniser is installed. Add English speech recognition in Windows "
                   "Settings, Time and language, Speech.",
                   true);
            return;
        }
        HRESULT hr = reco->SetRecognizer(tok.Get());
        if (SUCCEEDED(hr)) hr = reco->CreateRecoContext(&ctx);
        if (SUCCEEDED(hr)) hr = ctx->SetNotifyCallbackFunction(&Impl::onNotify, 0, reinterpret_cast<LPARAM>(this));
        if (SUCCEEDED(hr)) hr = ctx->SetInterest(SPFEI(SPEI_RECOGNITION), SPFEI(SPEI_RECOGNITION));
        if (SUCCEEDED(hr)) hr = ctx->SetAudioOptions(SPAO_NONE, nullptr, nullptr);
        if (SUCCEEDED(hr)) hr = ctx->CreateGrammar(1, &grammar);
        if (FAILED(hr) || !buildGrammar()) {
            status(wxString::Format("Could not start speech recognition (error 0x%08lX)", static_cast<unsigned long>(hr)),
                   true);
            release();
            return;
        }
        running = true;
        // Follow headsets being plugged in and removed.
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices)))) {
            std::weak_ptr<int> token = alive;
            wxEvtHandler* h = owner;
            watcher = new DeviceWatcher([token, h, this]() {
                h->CallAfter([token, this]() {
                    if (token.lock()) rebind();
                });
            });
            devices->RegisterEndpointNotificationCallback(watcher);
        }
        rebind();
    }

    void release() {
        if (devices && watcher) devices->UnregisterEndpointNotificationCallback(watcher);
        if (watcher) watcher->Release();
        watcher = nullptr;
        devices.Reset();
        if (reco) reco->SetRecoState(SPRST_INACTIVE);
        if (grammar) grammar->SetGrammarState(SPGS_DISABLED);
        grammar.Reset();
        if (ctx) ctx->SetNotifySink(nullptr);
        ctx.Reset();
        if (reco) reco->SetInput(nullptr, FALSE);
        reco.Reset();
    }

    void stop() {
        // Callbacks already queued for this session must not run.
        alive = std::make_shared<int>(0);
        release();
        running = false;
        listening = false;
        deviceId.clear();
        deviceName.clear();
    }

    // Binds the recogniser to the right microphone, or pauses it.
    void rebind() {
        if (!running || !reco) return;
        // Testing without a microphone: TOTALMIX_VOICE_TEST_WAV names a WAV file to recognise.
        wxString wav;
        if (wxGetEnv("TOTALMIX_VOICE_TEST_WAV", &wav) && !wav.empty()) {
            if (listening) return;
            ComPtr<ISpStream> stream;
            HRESULT hr = CoCreateInstance(__uuidof(SpStream), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&stream));
            if (SUCCEEDED(hr))
                hr = stream->BindToFile(wav.wc_str(), SPFM_OPEN_READONLY, &SPDFID_WaveFormatEx, nullptr, SPFEI_ALL_EVENTS);
            if (SUCCEEDED(hr)) hr = reco->SetInput(stream.Get(), TRUE);
            if (SUCCEEDED(hr)) hr = reco->SetRecoState(SPRST_ACTIVE);
            if (FAILED(hr)) {
                status(wxString::Format("Voice test: could not read %s (error 0x%08lX)", wav,
                                        static_cast<unsigned long>(hr)),
                       true);
                return;
            }
            listening = true;
            deviceName = "test file";
            status("Voice test: recognising " + wav, false);
            return;
        }
        const std::vector<Device> all = enumerateCapture();
        const Device* pick = nullptr;
        for (const auto& d : all) {
            if (opt.deviceName.empty() ? d.headset : d.name.IsSameAs(opt.deviceName, false)) {
                pick = &d;
                break;
            }
        }
        if (pick && listening && pick->id == deviceId) return;
        reco->SetRecoState(SPRST_INACTIVE);
        const bool wasListening = listening;
        listening = false;
        if (!pick) {
            deviceId.clear();
            deviceName.clear();
            if (opt.deviceName.empty())
                status(wasListening ? "Headset removed. Voice control is paused until a headset is connected."
                                    : "No headset microphone found. Voice control waits for a headset.",
                       true);
            else
                status(opt.deviceName + " is not connected. Voice control waits for it.", true);
            return;
        }
        const int waveId = waveInIdFor(pick->id);
        ComPtr<ISpMMSysAudio> audio;
        const char* step = "find its wave input";
        HRESULT hr = E_FAIL;
        if (waveId >= 0) {
            step = "find its SAPI audio input";
            audio = audioInputFor(static_cast<UINT>(waveId), pick->name);
            if (audio) hr = S_OK;
        }
        if (SUCCEEDED(hr)) {
            step = "connect it to the recogniser";
            hr = reco->SetInput(audio.Get(), TRUE);
        }
        if (SUCCEEDED(hr)) {
            step = "start the recogniser";
            hr = reco->SetRecoState(SPRST_ACTIVE);
        }
        if (FAILED(hr)) {
            status(wxString::Format("Could not listen on %s: could not %s (error 0x%08lX)", pick->name, step,
                                    static_cast<unsigned long>(hr)),
                   true);
            return;
        }
        deviceId = pick->id;
        deviceName = pick->name;
        listening = true;
        status("Voice control listening on " + deviceName + ". Say " + opt.wakeWord + " followed by a command.", false);
    }

    // ---- Grammar ---------------------------------------------------------------------------
    struct Builder {
        ISpRecoGrammar* g;
        std::map<std::pair<int, int>, SPSTATEHANDLE> numberRules;
        bool ok = true;
        int ruleId = 100;

        SPSTATEHANDLE rule(const wchar_t* name, DWORD attrs) {
            SPSTATEHANDLE h = nullptr;
            if (FAILED(g->GetRule(name, static_cast<DWORD>(ruleId++), attrs | SPRAF_Dynamic, TRUE, &h))) ok = false;
            return h;
        }
        SPSTATEHANDLE state(SPSTATEHANDLE from) {
            SPSTATEHANDLE s = nullptr;
            if (FAILED(g->CreateNewState(from, &s))) ok = false;
            return s;
        }
        void word(SPSTATEHANDLE from, SPSTATEHANDLE to, const std::string& words) {
            if (FAILED(g->AddWordTransition(from, to, wide(words).c_str(), L" ", SPWT_LEXICAL, 1.0f, nullptr)))
                ok = false;
        }
        void epsilon(SPSTATEHANDLE from, SPSTATEHANDLE to) {
            if (FAILED(g->AddWordTransition(from, to, nullptr, nullptr, SPWT_LEXICAL, 1.0f, nullptr))) ok = false;
        }
        void ref(SPSTATEHANDLE from, SPSTATEHANDLE to, SPSTATEHANDLE r) {
            if (FAILED(g->AddRuleTransition(from, to, r, 1.0f, nullptr))) ok = false;
        }
        SPSTATEHANDLE numbers(int lo, int hi) {
            auto it = numberRules.find({lo, hi});
            if (it != numberRules.end()) return it->second;
            const std::wstring name = L"n" + std::to_wstring(lo) + L"_" + std::to_wstring(hi);
            SPSTATEHANDLE r = rule(name.c_str(), 0);
            for (int n = lo; n <= hi; ++n) word(r, nullptr, rme::voice::numberWords(n));
            numberRules[{lo, hi}] = r;
            return r;
        }
        // Optional "decibels" to the end of the rule.
        void optionalDb(SPSTATEHANDLE s) {
            epsilon(s, nullptr);
            word(s, nullptr, "decibels");
        }
        // After a number: optional "point <digit>", then optional "decibels".
        void tail(SPSTATEHANDLE s) {
            SPSTATEHANDLE p = state(s), q = state(s);
            word(s, p, "point");
            ref(p, q, numbers(0, 9));
            optionalDb(q);
            optionalDb(s);
        }
    };

    bool buildGrammar() {
        if (!grammar) return false;
        grammar->SetGrammarState(SPGS_DISABLED);
        if (FAILED(grammar->ResetGrammar(lang))) return false;
        Builder b{grammar.Get()};

        // minus <0-65> | plus <0-6> | zero | off  (+ optional "point five", "decibels")
        SPSTATEHANDLE level = b.rule(L"level", 0);
        {
            SPSTATEHANDLE m = b.state(level), mv = b.state(level);
            b.word(level, m, "minus");
            b.ref(m, mv, b.numbers(0, rme::voice::kMaxDbWord));
            b.tail(mv);
            SPSTATEHANDLE p = b.state(level), pv = b.state(level);
            b.word(level, p, "plus");
            b.ref(p, pv, b.numbers(0, static_cast<int>(rme::voice::kMaxFaderDb)));
            b.tail(pv);
            SPSTATEHANDLE z = b.state(level);
            b.word(level, z, "zero");
            b.optionalDb(z);
            b.word(level, nullptr, "off");
        }

        SPSTATEHANDLE top = b.rule(L"command", SPRAF_TopLevel | SPRAF_Active);
        SPSTATEHANDLE cmd = b.state(top);
        // Only the first form ("total mix" for "TotalMix"): a joined word like "totalmix" has no
        // dictionary pronunciation, and its guessed one let "Mixer" match it.
        b.word(top, cmd, rme::voice::wakeWordForms(opt.wakeWord.ToStdString(wxConvUTF8)).front());

        SPSTATEHANDLE snap = b.state(top);
        b.word(cmd, snap, "snapshot");
        b.ref(snap, nullptr, b.numbers(1, rme::voice::kMaxSnapshot));
        for (const char* fx : {"reverb", "echo"}) {
            SPSTATEHANDLE s = b.state(top);
            b.word(cmd, s, fx);
            b.word(s, nullptr, "on");
            b.word(s, nullptr, "off");
        }
        b.word(cmd, nullptr, "undo");
        b.word(cmd, nullptr, "redo");
        b.word(cmd, nullptr, "stop listening");
        // Anything else after the wake word: a command grammar otherwise forces unknown words onto
        // the closest command (an unknown channel name heard as "redo"). The parser rejects this
        // path, so the user hears "not understood" instead of a wrong action.
        if (FAILED(grammar->AddRuleTransition(cmd, nullptr, SPRULETRANS_WILDCARD, 0.5f, nullptr))) b.ok = false;

        // Channel: input | playback | output <1-96>, or a channel name.
        SPSTATEHANDLE ch = b.state(top);
        for (const char* bus : {"input", "playback", "output"}) {
            SPSTATEHANDLE s = b.state(top);
            b.word(cmd, s, bus);
            b.ref(s, ch, b.numbers(1, rme::voice::kMaxChannel));
        }
        if (!names.empty()) {
            SPSTATEHANDLE n = b.rule(L"names", 0);
            for (const auto& name : names) b.word(n, nullptr, name);
            b.ref(cmd, ch, n);
        }
        // Channel actions.
        SPSTATEHANDLE vol = b.state(top);
        for (const char* w : {"volume", "level", "fader"}) b.word(ch, vol, w);
        b.ref(vol, nullptr, level);
        SPSTATEHANDLE ud = b.state(top), udv = b.state(top);
        b.word(vol, ud, "up");
        b.word(vol, ud, "down");
        b.ref(ud, udv, b.numbers(1, static_cast<int>(rme::voice::kMaxRelativeDb)));
        b.optionalDb(udv);
        b.word(ch, nullptr, "mute");
        b.word(ch, nullptr, "unmute");
        SPSTATEHANDLE pan = b.state(top), side = b.state(top);
        b.word(ch, pan, "pan");
        b.word(pan, side, "left");
        b.word(pan, side, "right");
        b.ref(side, nullptr, b.numbers(0, 100));
        b.word(pan, nullptr, "center");
        b.word(pan, nullptr, "centre");
        SPSTATEHANDLE send = b.state(top);
        b.word(ch, send, "effects send");
        b.word(ch, send, "send");
        b.ref(send, nullptr, level);

        if (!b.ok || FAILED(grammar->Commit(0))) return false;
        if (FAILED(grammar->SetRuleState(nullptr, nullptr, SPRS_ACTIVE))) return false;
        return SUCCEEDED(grammar->SetGrammarState(SPGS_ENABLED));
    }

    void setNames(const std::vector<std::string>& n) {
        std::vector<std::string> clean;
        for (const auto& s : n)
            if (!s.empty() && std::find(clean.begin(), clean.end(), s) == clean.end()) clean.push_back(s);
        if (clean == names) return;
        names = std::move(clean);
        if (running && !buildGrammar()) status("Could not update the voice command channel names", true);
    }

    // ---- Recognitions ----------------------------------------------------------------------
    void drain() {
        SPEVENT e{};
        ULONG fetched = 0;
        while (ctx && SUCCEEDED(ctx->GetEvents(1, &e, &fetched)) && fetched == 1) {
            if (e.eEventId == SPEI_RECOGNITION && e.elParamType == SPET_LPARAM_IS_OBJECT && e.lParam)
                handle(reinterpret_cast<ISpRecoResult*>(e.lParam));
            clearEvent(e);
            if (!ctx) break;  // stopped by a command ("stop listening")
        }
    }

    void handle(ISpRecoResult* r) {
        SPPHRASE* ph = nullptr;
        double confidence = 0.0;
        if (SUCCEEDED(r->GetPhrase(&ph)) && ph) {
            confidence = ph->Rule.SREngineConfidence;
            if (ph->Rule.Confidence == SP_LOW_CONFIDENCE) confidence = std::min(confidence, 0.25);
            CoTaskMemFree(ph);
        }
        LPWSTR text = nullptr;
        if (FAILED(r->GetText(static_cast<ULONG>(SP_GETWHOLEPHRASE), static_cast<ULONG>(SP_GETWHOLEPHRASE), TRUE, &text,
                              nullptr)) ||
            !text)
            return;
        const wxString t(text);
        CoTaskMemFree(text);
        if (outer->onPhrase) outer->onPhrase(t, confidence);
    }
};

VoiceInput::VoiceInput(wxEvtHandler* owner) : impl_(std::make_unique<Impl>()) {
    impl_->outer = this;
    impl_->owner = owner;
}

VoiceInput::~VoiceInput() { impl_->stop(); }

bool VoiceInput::available() { return true; }
std::vector<VoiceInput::Device> VoiceInput::captureDevices() { return enumerateCapture(); }

void VoiceInput::start(const Options& options) { impl_->start(options); }
void VoiceInput::stop() { impl_->stop(); }
bool VoiceInput::running() const { return impl_->running; }
bool VoiceInput::listening() const { return impl_->listening; }
wxString VoiceInput::deviceName() const { return impl_->deviceName; }
void VoiceInput::setChannelNames(const std::vector<std::string>& spokenNames) { impl_->setNames(spokenNames); }
