# Changelog

## 0.1.0 (finished, 2026-11-22)

- Tested in Logic on both Macs (Apple Silicon M4 and Intel), all 8 steps worked as described: install, the window as
  drafted, AR with short and long notes, the mapper moving a filter cutoff, ASR hold and release, Loop running on its
  own and turning round on a key during the fall, Send as Note (A1, A#1, B1) with nothing left hanging after Stop,
  and the settings and window size restored with a saved project. Milestone 2 was not needed.
- Automatic checks (GitHub run 45052297482): engine test 515 checks, mapper test 356, plugin-layer check 255, none
  failed; auval passed as a MIDI effect; pluginval passed at strictness 5 (AU and VST3); universal binary.

- First build: repo layout, build workflow (engine self-test, mapper test, plugin-layer check with window pictures,
  universal AU + VST3, ad-hoc signing, auval as a MIDI effect, pluginval, package with the shared installer,
  INSTALL.txt and the mapper, private draft release), all from Hillside 0.2.0.
- AU MIDI effect (type `aumi`), no audio buses, zero latency. Codes `Bryn` / `Pdot`.
- The engine (plain C++): Hillside's `Curve`, `Keys` and `CcStream` unchanged (namespace only); `Function` (from
  Hillside's `Envelope`): rise, hold, fall with exact stage timing at any sample rate and block size; AR, ASR and Loop
  modes; turning round during the fall; the same speed from part-way; Lin and Log curves (switching never jumps);
  `Pulses` with the Rise gate, Fall gate and EOC; rest on stop, reset, bypass and pauses (Loop restarts from 0).
- Outputs on channel 14 (changeable): Function as 14-bit CC 80 + 81; Rise gate, Fall gate and EOC as CC 82, 83, 90,
  notes 45-47 (A1, A#1, B1) or both (Send as). Each with its own switch.
- The window as approved in the draft: function display with the moving point and the in / rise / fall / EOC lights,
  Rise and Fall faders, Curve switch, Trigger button, Mode switch, outputs with switches, numbers, level bar and
  lights, Send as. Resizable 80% to 200%.
- Full control list fixed (11 controls). Golden control list and a 0.1.0 saved state in `check/golden/`.
- The Bryn Mapper Scripter script (8 lanes: source, Learn target, min, max; sources Off, Function, Rise gate, Fall
  gate, EOC); works alongside the Hillside, Ogdoad and Strange Drift mappers.
