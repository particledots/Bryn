# Bryn

An original AU MIDI effect for Logic Pro: notes in, a two-stage function (rise and fall) out as smooth 14-bit MIDI
CC, which a Scripter mapper moves onto any knob on the same track. Inspired by the Instruō cnōc function generator. It
is not a copy, contains no Instruō code or artwork, and is not affiliated with Instruō. The cnōc is an analog module
with no published firmware; its manual was used only to understand what the controls do. The function maths is our
own. Bryn is Welsh for "hill", as cnōc is Gaelic for a raised landscape. It follows Hillside (the cèis) and reuses its
engine.

Status: **version 0.1.0, milestone 1 of 2 built and passing every automatic check; waiting for Bryn's test in Logic.**

## What it does

Put Bryn in a **MIDI FX slot** above any instrument. Your notes sound as normal; each note also starts the function,
the panel shows it moving, and it goes out as MIDI CC. Defaults: Rise 100 ms, Fall 400 ms, Curve Log, Mode AR, MIDI
out channel 14, every output on, Send as CC.

- **Rise** and **Fall** (3 ms to 10 s each; the faders move the time on a logarithmic scale, centre 173 ms): the time
  of a whole climb from 0 to the top, and of a whole drop from the top to 0. From part-way a stage takes
  proportionally less (a rise from 60% takes 40% of Rise), as on an analog function generator. In Loop a cycle is
  Rise + Fall: 6 ms to 20 s.
- **Curve**: *Lin* straight lines; *Log* a logarithmic rise (fast, slowing near the top) and an exponential fall
  (quick drop, long tail). The curve never changes the times. Switching it while the function moves carries on from
  where it is, with no jump.
- **Mode** (the module's toggle: left, centre, right):
  - *ASR*: rises while a key is held, holds the top, falls when you let go (at once, also during the rise).
  - *AR*: any note, however short, plays one whole rise and fall; the fall follows the rise even while the key is
    still held.
  - *Loop*: rises and falls over and over with no notes needed.
- **Turning round** (the module's "charge direction"): in every mode, a new note during the fall starts a new rise from
  where the function is, at that sample, with no jump. A note during the rise (or the hold) changes nothing.
- **The Trigger button** (the round button): acts as a held key: in AR a press plays a cycle, in ASR it holds the top
  while held, in Loop it turns the fall round. It glows with the function. It can be automated; a project never
  opens with it held.
- **Outputs**, each with its own switch:
  - *Function*: the function as 14-bit CC. Switched off it sends 0 once, then nothing.
  - *Rise gate*: high for the whole rise. *Fall gate*: high for the whole fall. A stage shorter than 20 ms gives a
    20 ms gate; a stage that starts again while its gate is still high merges into one longer gate (a fast Loop keeps
    both high).
  - *EOC*: a 20 ms pulse at the end of every cycle (the sample at which a fall reaches 0). A fall turned round by a
    new note is not the end of a cycle. Pulses closer than 20 ms merge.
  - **Send as**: *CC* (127 while high, then 0; for the mapper), *Note* (velocity 100, released when it falls; plays the
    instrument directly, for example drum pads, or the gate of a second Bryn below this one) or *Both*, on the MIDI
    out channel. The numbers beside each switch follow Send as, and each has a light.
- **The panel** shows, live: the curve for the current settings (the other curve dashed; Loop shows two cycles, ASR
  the hold), the rise and fall shaded, a moving point on the curve, and the lights *in* (gate), *rise*, *fall*, *EOC*.
- **Resizing**: drag the bottom-right corner; 80% to 200% (624 x 376 to 1560 x 940), proportions locked, size saved
  with the project.

## Notes and the gate

The same rules as Hillside (its `Keys` is used unchanged):

- The gate is high while **any** key is held (any channel), or while the Trigger button is held.
- **Overlapping notes**: a new key while another is held is not a new edge (nothing restarts). The ASR fall starts when
  the last key is up.
- **Back-to-back notes**: a note that starts at the exact sample the previous one ends is a new edge there.
- A key pressed twice without a release counts once; its first release lets it go (a missing note-off can never hold
  the gate). Velocity 0 is a release. All Notes Off / All Sound Off release that channel's keys.
- A zero-length note (on and off at the same sample) is a one-sample gate: in AR a whole cycle.
- **Rest**: transport stop, Logic resetting the plugin, bypass, and a pause of more than a second in Logic's calls send
  the function straight to 0 (0 is sent, every gate and gate note ends), release every key and forget the Trigger
  button until it is let go. A key's later release then does nothing. In Loop the function starts again from 0 at
  once (it runs whenever Logic runs the plugin; bypassed, it stays at 0).
- **Changes while it runs**: a new time changes the running stage's speed from that moment, with no jump. Mode: ASR
  holding the top and switched to AR or Loop falls at once; Loop switched to AR or ASR finishes the cycle it is in;
  AR or ASR switched to Loop starts at once. Changing the MIDI out channel sends 0 on the old channel, then carries on
  on the new one.
- **Gates and controls**: changing Send as or the channel, or switching an output off, ends whatever it had on at once
  (CC 0 and/or note-off, on the channel it was sent on). It then stays off until its stage next starts (or the next
  cycle ends): Send as, the channel and the switches never start a gate or a note.
- **No stuck gate notes**: rest (stop, reset, bypass, a pause in Logic's calls, a new sample rate) ends every gate and
  gate note at once. One limit: a plugin cannot send MIDI while it is being removed from its slot.
- Zero latency: every incoming message (notes, controllers, pitch bend, aftertouch, program changes, sysex) passes on
  unchanged, at its own sample, in order.

## How the function reaches a knob in Logic

Logic has no CV. The Function goes out as a 14-bit CC pair (16,384 steps instead of 128), the gates and EOC as a CC
(127 / 0), a note, or both, all on channel 14:

| Output | Function | Rise gate | Fall gate | EOC |
|---|---|---|---|---|
| coarse CC | 80 | 82 | 83 | 90 |
| fine CC | 81 | | | |
| note (Note / Both) | | 45 (A1) | 46 (A♯1) | 47 (B1) |
| from | 0.1.0 | 0.1.0 | 0.1.0 | 0.1.0 |

None of these numbers is used by Hillside (channel 16: CC 3, 35, 9, 41, 85-89, notes 40-44), Ogdoad (channel 16:
CC 12-31, 44-63, 102-105, notes 36-39) or Strange Drift (channel 15: CC 106-119), on any channel, so all four can
share a track (even all on one channel). CC 90 is the last CC that MIDI leaves without a job; CC 80-83 are the
"general purpose" controllers, which have no fixed effect. The coarse and fine parts are 80 and 81 (a direct pair, as
Strange Drift's), because every coarse CC with a standard fine partner (0-31) is already taken.

The **Bryn Mapper** (a Scripter script, in the download as `Bryn Mapper (paste into Scripter).txt`, source in
`tools/mapper/Bryn-Mapper.js`) sits in a MIDI FX slot below Bryn. It has 8 lanes; each lane has a Source (Off,
Function, Rise gate, Fall gate, EOC), a Target (Logic's *Learn plug-in parameter*), and Min % / Max %. It moves the
learnt knob on any plugin below it on the same channel strip and stops Bryn's CC from reaching the instrument;
everything else passes through, Hillside's, Ogdoad's and Strange Drift's CC included. The gate and EOC sources jump the
knob from Min to Max while high (Send as CC or Both). `INSTALL.txt` in the download has the steps.

**Unipolar and bipolar.** The module has a 0 to 10 V output and a ±5 V one. In MIDI the two would carry exactly the
same numbers; the difference is only where the knob sits, and the lane's Min and Max set that: Min 0 / Max 100 is the
unipolar output, Min 30 / Max 70 (either side of where the knob should rest) is the bipolar one, Max below Min is
upside down. So Bryn sends one stream, which halves the MIDI.

**With Hillside, Ogdoad and Strange Drift on the same track:** MIDI FX slots in the order Bryn, Hillside, Ogdoad,
Strange Drift, then the four mappers in any order. Plugins only hear notes from above them, so Ogdoad's trigger notes
cannot reach Bryn or Hillside; with Send as Note or Both, Bryn's gate notes do reach Hillside and can play its envelope.

**Smoothness and CPU.** As Hillside (its `CcStream` is used unchanged): a value is sent once it has moved at least
0.2% and 6 ms have passed, or after 100 ms for smaller moves, and at most once per audio block; nothing while it stands
still. The top and 0 are exact: sent at their exact sample. At Logic's usual 256-sample buffer, stages of about
**50 ms or longer** move a knob smoothly; shorter ones step but land on the right values. A Loop of 1 s (Rise 400 ms,
Fall 600 ms) sends about 245 MIDI messages a second with every output on (measured in the engine test). The engine
uses well under 0.1% of one core.

## The manual, and how it was read

1. **Unipolar and bipolar outputs**: one stream; the mapper's Min / Max make either (above).
2. **Stage Gate toggle**: MIDI can carry both gates at once, so Bryn has a Rise gate and a Fall gate instead of one
   gate with a toggle.
3. **Trigger / Charge Direction button**: the manual says it triggers the function and gives "a manual soft
   synchronization of the function's charge direction". Bryn: the button is a held key; a press during the fall
   turns the function round into a rise from where it is (in every mode, notes do the same); during the rise nothing
   changes; in ASR holding it holds the top.
4. **Speed from part-way**: not described. Bryn keeps the speed (a rise from part-way takes Rise x the distance, a
   fall Fall x the level), as a capacitor charging at a set rate would.
5. **Loop**: free-running, as on the module. In Logic it runs whenever Logic runs the plugin; stop sends it to 0 and it
   starts again from 0.
6. **EOC and gate lengths**: the module's EOC is 1 ms. MIDI receivers need longer, so EOC is 20 ms and a gate at least
   20 ms (as Ogdoad's and Hillside's triggers).
7. **Times**: the module's loop runs from about 1 ms to 20 s a cycle. Bryn: 3 ms to 10 s per stage (6 ms to 20 s a
   cycle); MIDI cannot carry anything faster anyway.
8. **The curve** is not published. Bryn's Log curve is Hillside's Shape at 100% (`engine/include/bryn/Curve.h`).

## Left out

- The Signal input as a CV slew limiter (the module's portamento patch): there is no CV in Logic to slew.
- The Attack and Release CV inputs: use Logic's automation instead.
- Times below 3 ms.

## Later (not planned; only built if asked)

- A mod-wheel (or other CC) slew: the function follows an incoming controller at the Rise and Fall speeds.
- Loop synced to the project tempo.
- Velocity scaling the function's height.

## Folders

| Folder | What it is |
|---|---|
| `engine/` | The engine. Plain C++, no plugin code. From Hillside 0.2.0, unchanged except the namespace: `Curve`, `Keys`, `CcStream`. Adapted from Hillside: `Function` (from `Envelope`: rise, hold, fall; AR, ASR, Loop), `Pulses` (Rise gate, Fall gate, EOC), `Engine`, `Outputs.h` (the MIDI numbers). |
| `plugin/` | The plugin wrapper: what Logic sees (an AU MIDI effect), the list of controls (`Parameters`), and the panel (`PluginEditor`, `Style`). |
| `check/` | `BrynCheck`: a console program that tests the plugin layer without a host; `golden/` holds the control list and a saved state of each version. |
| `tools/mapper/` | The Scripter mapper script. |
| `tools/install/` | `Install-Unofficial-Plugin.command`, the same installer as the other particledots plugins (Audio Unit only). |
| `.github/workflows/` | The automatic build. |

## Checks run on every push

- **Engine self-test** (Linux): the curve; timing: every stage edge exact at 44.1, 48 and 96 kHz, Lin and Log, with
  blocks of 32 to 1024 samples and random sizes, AR cycles and Loop cycles exactly Rise + Fall samples, the function
  identical sample for sample at every block size; the modes (AR with long notes, ASR hold and release during the
  rise, Loop without notes), turning round during the fall in every mode at the same speed with no jump, notes during
  the rise changing nothing, mode changes while running; Lin straight and Log bending the right way, the times
  independent of the curve; no jump in random sessions with every setting changing; notes (All Notes Off,
  back-to-back, zero-length) and the Trigger button; rest (0 at the first sample; Loop restarting from 0); the CC
  rules over random playing (one value per block, every value equal to the function at that sample, coarse before
  fine only when it changed, corners exact, nothing at rest); the gates and EOC (exact edges at every rate and block
  size, 20 ms stretching and merging, no EOC for a turned fall, CC / Note / Both, changes never starting anything,
  rest ending everything); a stuck-gate and stuck-note fuzz of about 180,000 random events; CPU and MIDI rate.
- **Mapper test**: the real mapper script in a stand-in for Scripter, fed by the engine's real MIDI stream: each lane
  follows the Function value for value, Min / Max (also around the middle), upside down, 14-bit detail, the gate and
  EOC sources following the engine's real gate stream (and gate notes passing to the instrument in Note and Both), Off
  lanes, the channel setting, notes and everyone else's CC passing, and the four mappers (Bryn, Hillside, Ogdoad,
  Strange Drift) chained in all 24 orders, with Bryn on its own channel and on Hillside's.
- **Plugin-layer check** (`BrynCheck`): golden control list and saved state; MIDI-effect flags, no audio buses, no
  latency; the MIDI path (everything passes unchanged and in order, channel 14, rest at the start, the top exactly at
  its sample, the gates and EOC, velocity 0, 0- and 1-sample blocks, another out channel, Note and Both, Loop without
  notes); automation of every control with and without a held note; transport stop, reset, releaseResources,
  prepareToPlay, All Notes Off, bypass and a pause in Logic's calls during a rise, with gates as CC and as notes; Loop
  after a stop; two stuck-gate and stuck-note fuzzes through the real plugin (one with bypass); the panel (design
  size, every control live and moving only its own parameter, the Trigger button, the lights, the moving point on the
  drawn curve, resizing limits, saved size). The run uploads pictures of the window (artifact
  **Bryn-window-pictures**).
- **macOS build**: universal binary (Apple Silicon + Intel), ad-hoc signing, Apple's `auval -v aumi Bryn Pdot`,
  pluginval at strictness 5 (AU and VST3), and the package (installer identical to the shared one, INSTALL.txt with
  the mapper steps, the mapper).

## Getting the plugin

1. Open the **Actions** tab of this repo and click the latest **Build Bryn** run.
2. At the bottom, under **Artifacts**, download **Bryn-macOS** and unzip it (twice: the download is a zip containing
   `Bryn-macOS.zip`).
3. Inside the `Bryn-macOS` folder are `Bryn.component`, `Bryn.vst3`, `INSTALL.txt`, `auval.log`, the pluginval logs,
   `Bryn Mapper (paste into Scripter).txt` and `Install-Unofficial-Plugin.command`.
4. In Terminal, once, clear the download block: `xattr -dr com.apple.quarantine ~/Downloads/Bryn-macOS/*`
   (adjust the path if you unzipped elsewhere).
5. **Easy install (Logic, Audio Unit):** double-click `Install-Unofficial-Plugin.command`. It asks for your Mac
   password once, installs the AU for all users and restarts the AU registrar.
6. In Logic it is a MIDI effect: MIDI FX slot of a Software Instrument track, **Audio Units > particledots > Bryn**.

## Fixed names

Plugin code `Bryn`, manufacturer `Pdot`, bundle id `com.particledots.bryn`, AU type MIDI effect (`aumi`), target
`Bryn`, engine library `bryn_engine`. Parameter ids, in order: `rise`, `fall`, `curve`, `mode`, `trigger`,
`outChannel`, `outFnOn`, `riseGateOn`, `fallGateOn`, `eocOn`, `sendAs`. Saved with the project besides the
parameters: `editorWidth`. The CC and note numbers above and the mapper's Source menu order are fixed too. These never
change, so saved projects keep finding the plugin and its settings.
