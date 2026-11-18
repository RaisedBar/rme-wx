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
| `src/core` (static lib, no GUI) | `Osc` (OSC 1.0 messages and bundles), `UdpSocket` (non-blocking UDP, polled), `Scale` (RME fader and frequency curves, value text), `Params` (the RME OSC table), `Link` (is TotalMix sending / answering?), `PortCheck` (which program holds a UDP port) |
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
| Mixer | `/1/` | Row (Inputs / Playbacks / Outputs), navigation, a bank of channel strips, Control Room, Global |
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

## Limitations / testing status

- Tested with a Fireface UFX+ only as far as the connection goes. These parts were tested:
  - the core, with unit tests;
  - the app, which was started, audited, and fed a simulated TotalMix bundle on port 9001. Strip names,
    the Val display string, pan text, row and submix all updated;
  - the connection diagnosis, against a real port conflict with the Sentinel Keys Server;
  - page selection: sending `/2` then `/1` (no arguments) made TotalMix FX re-send page 1.

  The mixer controls have not yet been used with the real device.
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
