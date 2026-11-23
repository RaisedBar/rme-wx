# TotalMix Access

An accessible remote control for RME **TotalMix FX**, using its OSC interface. Written in C++20 with
wxWidgets. Accessibility with screen readers (NVDA, JAWS, Narrator) comes first. Where that allows, the
layout follows the TotalMix FX window.

The protocol comes from RME's OSC table, `OscTableTotalMix_240722` (TotalMix FX 1.96, 22.07.2024).
The page-select message (`/1` to `/4` with no arguments) comes from RME's TouchOSC template
`TestTMOsc5_8.tosc`.

The structure follows the 3rd Wave / Prophet '08 editors (`..\3wx`, `..\p8wx`): a core / app split, the
same accessibility layer (`Accessibility.h`, UIA announcements, `ChangeAnnouncer`), `settings.xml` and the
same audit script.

## Building (Windows, Visual Studio 2022)

wxWidgets comes from **this computer's vcpkg installation in classic mode**. The build uses
`C:\Users\tim\vcpkg\installed\x64-windows`, which holds wxWidgets 3.3.3. It needs the MSBuild integration
(`vcpkg integrate install`, already done on this machine). On another computer, run:

```
vcpkg install wxwidgets:x64-windows
vcpkg integrate install
```

Then open `TotalMixAccess.sln`, pick `Debug|x64` or `Release|x64` and build. From the command line:

```
msbuild TotalMixAccess.sln /p:Configuration=Release /p:Platform=x64 /m
bin\x64\Release\TotalMixTests.exe
```

Output goes to `bin\x64\<Configuration>\`. vcpkg copies the wxWidgets DLLs next to the exe.

CMake also works (`cmake --preset windows-vs2022`, or `macos-xcode` on a Mac). Set `VCPKG_ROOT` first. The
presets turn vcpkg manifest mode off.

| Project | Contents |
|---|---|
| `src/core` (static lib, no GUI) | `Osc` (OSC 1.0 messages and bundles), `UdpSocket` (non-blocking UDP, polled), `Scale` (RME fader and frequency curves, value text), `Params` (the RME OSC table), `Link` (is TotalMix sending / answering?), `PortCheck` (which program holds a UDP port), `VoiceCommand` (voice command language and parser), `CommandSequencer` (runs a voice command across TotalMix's pages) |
| `src/app` | Main frame, `MixerPanel` (page 1), `PagePanel` (pages 2 to 4, built from the table), `BusSelector`, `ParamControls`, settings, accessibility layer |
| `tests` | Core tests: OSC encoding, bundles, malformed packets, scales, table, UDP loopback |
| `tools` | `a11y-audit.ps1`: checks that every control in the running app has an MSAA and UIA name |

## Setting up TotalMix FX

1. In TotalMix FX choose **Options > Enable OSC Control**.
2. Open **Options > Settings > OSC**. Tick **In Use** for one Remote Controller and set:
   - **Remote Controller Address**: the IP address of the computer running TotalMix Access
     (`127.0.0.1` on the same computer).
   - **Port incoming**: 7001. TotalMix Access sends to this port.
   - **Port outgoing**: 9001. TotalMix Access listens on this port.
   - **Number of faders per bank**: 8, or match it in TotalMix Access.
3. In TotalMix Access, open **TotalMix > OSC Settings** (Ctrl+M) if anything differs. The dialog opens
   by itself the first time.

Settings are saved in `%APPDATA%\TotalMixAccess\settings.xml`. The file also holds the announcement
delay (`debounceSeconds`).

## How it works

TotalMix FX's OSC is page based, in the style of a Mackie controller. TotalMix sends data for only
one page at a time, so each page is a tab here:

| Tab (Ctrl+1 to 4) | OSC page | Contents |
|---|---|---|
| TotalMix | `/1/` | Row (Inputs / Playbacks / Outputs), navigation, a bank of channel strips, Control Room, Global |
| Channel Settings | `/2/` | One channel: fader, settings (gain, 48V, phase, width, FX send, ...), low cut, EQ, dynamics, auto level |
| FX and Snapshots | `/3/` | Reverb, echo, snapshots 1 to 8, mute / solo / fader groups, recorder (DURec), undo / redo |
| Room EQ | `/4/` | One output: delay, volume correction, 9 bands. TotalMix switches to the Outputs row. |

Changing tabs sends `/1` to `/4`, and TotalMix FX re-sends the whole page. F5 refreshes the current page.

The Mixer tab looks like TotalMix FX. The row selector and bank navigation are at the top. The channel
strips run left to right, and Control Room is on the right. Each strip shows, top to bottom:

- gain and 48V (Inputs row only)
- pan
- mute
- solo (Inputs and Playbacks rows)
- cue (Outputs row)
- select
- a vertical fader
- the level
- a **Settings** button, TotalMix's wrench

Controls that TotalMix FX does not support on the current row are hidden. On the Inputs and Playbacks
rows the faders are the sends to the submix shown in **Submix**. Choose another submix with
**Submix number**.

## Accessibility

- Only native controls are used: check boxes, buttons, toggle buttons, sliders, spin controls, read-only
  edits, radio buttons and a notebook. Every interactive control has an explicit accessible name (MSAA
  and UI Automation). `tools\a11y-audit.ps1` checks this; the last run found 248 controls, all named.
- Every strip has the same controls, so their names start with the channel name: "AN 1 Volume",
  "AN 1 Mute". Each strip is also a group named after the channel. On pages 2 to 4 the group boxes give
  the context, and names are just the parameter ("Band 1 gain").
- A slider's accessible value is text, not a position: "-5.5 dB", "L20", "Bell", "1.25 kHz".
  - Faders move in 0.5 dB steps with Up / Down and 3 dB with Page Up / Page Down. Up is louder.
  - Fader, pan, frequency and type values are computed locally with RME's formulas. They are then
    replaced by TotalMix's own display string (the `...Val` messages).
  - For values whose scale only TotalMix knows (compressor ratio, attack, Q, gain, ...), the slider first
    reads a percentage. TotalMix's value, e.g. "2.5 ms", is spoken when it arrives.
- Changes made in TotalMix FX, or by another controller, are spoken after a short delay, e.g.
  "AN 3 Mute on". Bursts of more than four changes are summarised.
  - Echoes of your own changes are not spoken, and neither are page refreshes after a page change, bank
    change or snapshot load.
  - After navigating, the new bank is spoken ("Channels AN 1 to AN 8"), and so are the channel name on
    Channel Settings / Room EQ, row changes, the submix name and the recorder state.
  - If TotalMix FX refuses a change, e.g. 48V on a channel without phantom power, the real state is
    spoken.
- On the Mixer tab, Alt+Up / Alt+Down move to the previous / next row (Inputs, Playbacks, Outputs),
  and the new row is spoken. Ctrl+Shift+I / P / O jump to a row directly.
- Ctrl+Shift+L speaks the level of the strip with the focus, or of the channel on Channel Settings.
  Levels are never announced on their own.
- Connection problems and results go to the status bar and the OSC log, and are also spoken. These
  include: port in use, no reply from TotalMix FX (with the cause and what to do; see below), receiving
  data, and data stopping.
- Ctrl+K checks the connection. Every function is reachable from the keyboard; F1 lists the shortcuts.

### When TotalMix FX does not answer

A connection check passes only when TotalMix FX reacts to our request. Level meters don't count,
because TotalMix sends them whether or not it receives anything. If TotalMix FX runs on this computer
and the check fails, the app looks at the UDP ports to say why. It reports:

- **Another program holds the Send port.** For example: "UDP port 7001 on this computer is in use by
  sntlkeyssrvr.exe, not by TotalMix FX". The Sentinel Keys Server licence service uses 7001, which is
  TotalMix's default.
- **The port TotalMix FX really listens on**, if it differs from the Send port ("TotalMix FX is listening
  on port 7002: set the Send port to 7002"). If TotalMix isn't listening anywhere, a free port is
  suggested instead.
- **Nothing is listening on the Send port.**
- **TotalMix FX listens on the Send port but doesn't answer.** In this case, check its Remote Controller
  Address and Port outgoing.
- **The Send and Receive ports are the same.**
- **TotalMix FX is sending level meters but ignoring our requests.**

## Voice control (Windows)

**Voice > Voice Control** (Ctrl+Shift+V) listens for spoken commands. Every command starts with a wake
word, "TotalMix" by default, spoken as "total mix". A different wake word can be set in Voice Settings:

| Say | Does |
|---|---|
| TotalMix snapshot 3 | loads snapshot 3 (1 to 8) |
| TotalMix input 5 volume minus 10 | sets the fader to -10 dB (also: volume plus 3, volume zero, volume off) |
| TotalMix input 5 volume up 3 | raises it by 3 dB (up / down, at most 12 dB; raising stops at 0 dB) |
| TotalMix playback 2 pan left 30 | pan / balance (left / right 0 to 100, pan center) |
| TotalMix input 5 mute | mute / unmute (only toggles when needed) |
| TotalMix input 2 effects send minus 12 | FX (reverb / echo) send (also: effects send off; inputs and playbacks) |
| TotalMix reverb off | reverb / echo on or off |
| TotalMix undo / TotalMix redo | TotalMix undo / redo |
| TotalMix stop listening | turns voice control off |

Help > Voice Commands lists the commands.

- **Channels** are a row and a number ("input 5", "playback 2", "output 3"), or a channel name ("vocal").
  The number is TotalMix's channel number, counting every mono channel: a stereo pair is reached by its
  left number, and the right number is refused with a spoken explanation. Names of the bank shown on
  the Mixer tab always work. **Voice > Learn Channel Names** steps through every channel of every row,
  so all names work. Names are also learned whenever a channel is reached by number.
- **Microphone:** only a headset. That is a capture device whose Windows form factor is Headset, or whose
  name says headset or hands-free. The default microphone, webcams and the audio interface inputs are
  never used, because they hear the monitors and the screen reader. When the headset is unplugged,
  voice control pauses and says so. It resumes when a headset is connected. Voice > Voice Settings can
  name a specific microphone instead.
- **How it runs:** TotalMix FX's OSC works one page at a time. A channel command therefore:
  1. selects the row;
  2. selects the channel (`/setBankStart`, or `/setOffsetInBank` for a name in the bank shown);
  3. switches TotalMix to page 2 and checks the channel name it sends back;
  4. sets the value;
  5. returns TotalMix to the page you were on.

  Snapshots, reverb / echo and undo / redo use page 3 the same way. Only the result is spoken, e.g.
  "Taurus 3 pan center". The page traffic in between is not. A command by number moves the mixer bank
  to start at that channel.
- **FX send:** RME's table gives no curve for the FX send. The app tries values until TotalMix's own
  display string matches the requested dB, within 0.25 dB.
- **Recognition** uses the Windows SAPI 5 recogniser with a fixed command grammar, offline. It needs
  Windows' English speech recognition (US, UK and other English variants work). Anything else said
  after the wake word is caught and ignored, rather than forced onto the closest command. Results below
  the **minimum confidence** (Voice Settings, default 50%) are ignored. Every phrase heard is written to
  the OSC log with its confidence.
- **Testing without a microphone:** set `TOTALMIX_VOICE_TEST_WAV` to a WAV file before starting the app.
  It then recognises that file instead of a microphone and only logs what it would do. Also set
  `TOTALMIX_VOICE_TEST_RUN=1` to really send the commands.
- macOS: not available yet.

## Limitations / testing status

- Tested with a Fireface UFX+ only as far as the connection goes. These parts were tested:
  - the core, with unit tests;
  - the app, which was started, audited, and fed a simulated TotalMix bundle on port 9001. Strip names,
    the Val display string, pan text, row and submix all updated;
  - the connection diagnosis, against a real port conflict with the Sentinel Keys Server;
  - page selection: sending `/2` then `/1` (no arguments) made TotalMix FX re-send page 1.

  The mixer controls have not yet been used with the real device.
- Voice control was tested with Windows text-to-speech WAV files in two voices (UK and US English), not
  yet with a person speaking into the headset:
  - every command form was recognised at 96 to 99% confidence;
  - the old wake word "Mixer" no longer triggers anything, but "Total, snapshot four" (with "mix" left
    off) was accepted at 91%;
  - an unknown channel name and ordinary chatter were rejected;
  - "input 5 pan center" ran end to end against TotalMix FX;
  - a channel number that is the right half of a stereo pair was refused.

  Against the real TotalMix FX, `/setBankStart` and `/setOffsetInBank` sent as floats work.
- TotalMix FX also sends `/1/labelS1` to `labelS8`. These are not in RME's table, and the app ignores
  them.
- Uncertain points to check with TotalMix FX:
  - Value types: everything is sent as float, as TouchOSC does, including `/setSubmix`, `/setBankStart`,
    `/setOffsetInBank` and `/loadQuickWorkspace`.
  - Selection lists are device dependent. The reverb type list (15 types) and echo types are not in
    RME's table. The EQ and low-cut type lists assume four entries. If a device has fewer, the slider
    positions will not match. TotalMix's own display string is always shown and spoken, so you still hear
    the real value.
  - The step count of linear sliders (100, 200 for pan and width) is a guess. TotalMix quantises the
    value itself.
  - "TotalMix FX stopped sending" is only detected while level meter data was arriving. Without it,
    TotalMix only sends changes.
- Page 1 strips 9 to 24 need TotalMix's "Number of faders per bank" option and the same setting here.
- Snapshot names and channel counts are not available through OSC.

## Licence

MIT, see `LICENSE`. RME and TotalMix are trademarks of RME Audio. This project is not affiliated with RME.
