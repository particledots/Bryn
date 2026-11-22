// BrynCheck: runs the plugin layer without a host. The build workflow runs it on every push and fails if any check
// fails. Adapted from Hillside's check.
//
//   1. The control list is exactly what 0.1.0 shipped (ids, order, ranges, defaults, menu lists): golden file
//      layout-<version>.txt for every version that shipped one.
//   2. Projects saved by every earlier version (golden saved states, every control away from its default) load
//      unchanged; the Trigger button never opens held.
//   3. It is a MIDI effect: no audio buses, MIDI in and out, no latency, no tail, 11 controls.
//   4. The MIDI path: everything passes unchanged and in order (notes keep channel, velocity and sample); the function
//      goes out on channel 14 as CC 80 / 81, the gates and EOC as CC 82, 83, 90, notes 45-47 or both; rest sent at
//      the start; the top reached exactly; Loop runs without notes.
//   5. Automation: every control moved through its range, with and without a held note: never more than one value
//      per block, no bursts from the gates, and changing Send as, the channel or a switch never starts a note or gate.
//   6. Transport stop, reset / releaseResources / prepareToPlay, All Notes Off, bypass and a long pause in Logic's
//      calls while a note is held (gates sent as CC and as notes): the function goes to rest, 0 and note-offs are
//      sent at once, and the key's later release does nothing. Loop starts again from 0 after a stop.
//   7. Stuck-gate and stuck-note fuzz through the real plugin: random notes, controllers, block sizes (including 0 and
//      1), control changes (all of them), stops, resets and bypass. After every key is released (AR): at rest, 0 last
//      sent on every channel used, no gate note left on.
//   8. The panel: opens at its design size, draws, resizes between 80% and 200% with locked proportions, every
//      control inside the window and moving only its own parameter, the Trigger button plays the function, the
//      lights follow the outputs, the moving point sits on the drawn curve. --snapshot <folder> writes pictures of it.

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <random>
#include <vector>

static int checks = 0, failures = 0;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        ++checks;                                                          \
        if (! (cond)) {                                                    \
            std::printf ("FAIL line %d: %s   ", __LINE__, #cond);          \
            std::printf (__VA_ARGS__);                                     \
            std::printf ("\n");                                            \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static constexpr int kNumControls = 11;
static const char* const kVersions[] = { "0.1.0" };   // every version that shipped a golden list and state

static std::vector<juce::RangedAudioParameter*> allParams (BrynProcessor& proc)
{
    std::vector<juce::RangedAudioParameter*> v;
    for (auto* base : proc.getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (base)) v.push_back (p);
    return v;
}
static juce::RangedAudioParameter* P (BrynProcessor& proc, const char* id)
{
    return dynamic_cast<juce::RangedAudioParameter*> (proc.getValueTreeState().getParameter (id));
}
static void setReal (BrynProcessor& proc, const char* id, float v)
{
    auto* p = P (proc, id);
    p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// A play head we can start and stop.
struct FakePlayHead final : juce::AudioPlayHead
{
    bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        return info;
    }
};

struct Out { int pos; juce::MidiMessage m; };

static std::vector<Out> run (BrynProcessor& proc, juce::MidiBuffer midi, int numSamples, bool bypassed = false)
{
    juce::AudioBuffer<float> audio (0, numSamples);
    if (bypassed) proc.processBlockBypassed (audio, midi);
    else          proc.processBlock (audio, midi);
    std::vector<Out> v;
    for (const auto meta : midi) v.push_back ({ meta.samplePosition, meta.getMessage() });
    return v;
}
static std::vector<Out> runEmpty (BrynProcessor& proc, int blocks, int n = 512)
{
    std::vector<Out> all;
    for (int i = 0; i < blocks; ++i) for (auto& o : run (proc, {}, n)) all.push_back (o);
    return all;
}

static bool isPulseCc (int n) { return n == 82 || n == 83 || n == 90; }
static bool isOurCc (const juce::MidiMessage& m)
{
    if (! m.isController()) return false;
    const int n = m.getControllerNumber();
    return n == 80 || n == 81 || isPulseCc (n);
}
static bool isGateNote (const juce::MidiMessage& m) { return m.isNoteOnOrOff() && m.getNoteNumber() >= 45 && m.getNoteNumber() <= 47; }

// What a mapper on each channel knows: the latest 14-bit value of the Function (CC 80 + 81).
struct Receiver
{
    int coarse[17] {};
    int value[17];
    Receiver() { for (auto& v : value) v = -1; }
    void take (const juce::MidiMessage& m)
    {
        if (! m.isController()) return;
        const int ch = m.getChannel(), n = m.getControllerNumber();
        if (n == 80) coarse[ch] = m.getControllerValue();
        if (n == 81) value[ch] = coarse[ch] * 128 + m.getControllerValue();
    }
    void take (const std::vector<Out>& v) { for (auto& o : v) take (o.m); }
};

// The Rise gate, Fall gate and EOC as a receiver sees them, per channel: CC 82, 83, 90 values and notes 45-47 on or
// off. Notes from elsewhere (the player's) are not counted: the checks play other note numbers.
struct PulseWatch
{
    int ccVal[17][128] {};
    bool noteOn[17][128] {};
    int doubled = 0, strayOff = 0, ccRepeat = 0, starts = 0;
    void take (const juce::MidiMessage& m)
    {
        const int ch = m.getChannel();
        if (m.isController() && isPulseCc (m.getControllerNumber()))
        {
            const int n = m.getControllerNumber(), v = m.getControllerValue();
            ccRepeat += ccVal[ch][n] == v ? 1 : 0;
            starts += v == 127 ? 1 : 0;
            ccVal[ch][n] = v;
        }
        if (isGateNote (m))
        {
            const int n = m.getNoteNumber();
            if (m.isNoteOn()) { doubled += noteOn[ch][n] ? 1 : 0; noteOn[ch][n] = true; ++starts; }
            else              { strayOff += noteOn[ch][n] ? 0 : 1; noteOn[ch][n] = false; }
        }
    }
    void take (const std::vector<Out>& v) { for (auto& o : v) take (o.m); }
    bool anythingOn() const
    {
        for (int ch = 0; ch <= 16; ++ch) for (int n = 0; n < 128; ++n) if (ccVal[ch][n] != 0 || noteOn[ch][n]) return true;
        return false;
    }
};

static void prepare (BrynProcessor& proc, double sr = 48000.0, int block = 512)
{
    proc.setRateAndBufferSizeDetails (sr, block);
    proc.prepareToPlay (sr, block);
}
static juce::MidiBuffer one (const juce::MidiMessage& m, int pos = 0) { juce::MidiBuffer b; b.addEvent (m, pos); return b; }

// ------------------------------------------------------------------------------------------------ 1. control list

static juce::StringArray dumpLayout (BrynProcessor& proc)
{
    juce::StringArray lines;
    int i = 0;
    for (auto* p : allParams (proc))
    {
        const auto& r = p->getNormalisableRange();
        juce::String line;
        line << i++ << " " << p->paramID << " v" << p->getVersionHint() << " | " << p->getName (100)
             << " | range " << juce::String (r.start, 3) << ".." << juce::String (r.end, 3) << " step " << juce::String (r.interval, 3)
             << " | default " << juce::String (p->convertFrom0to1 (p->getDefaultValue()), 3)
             << " | centre " << juce::String (p->convertFrom0to1 (0.5f), 3);
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p)) line << " | choices " << c->choices.joinIntoString (";");
        if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr) line << " | switch";
        lines.add (line);
    }
    return lines;
}

// The normalised value every control gets in a golden project: away from its default. Each version its own pattern.
static float goldenValue (juce::RangedAudioParameter* p, int i, const juce::String& version)
{
    const int n = juce::jmax (2, p->getNumSteps() < 1000 ? p->getNumSteps() : 10);
    int k = version == "0.1.0" ? (i * 7 + 3) % n : (i * 5 + 2) % n;
    float v = (float) k / (float) (n - 1);
    if (std::abs (v - p->getDefaultValue()) < 1.0e-3f) v = (float) ((k + 1) % n) / (float) (n - 1);
    return p->convertTo0to1 (p->convertFrom0to1 (v));
}

static void testControlList (const juce::File& goldenDir, bool writeGolden)
{
    BrynProcessor proc;
    const auto now = dumpLayout (proc);
    if (writeGolden)
    {   // writes the golden files of THIS version only, and never overwrites a version that already shipped
        const juce::String version (JucePlugin_VersionString);
        const juce::File golden = goldenDir.getChildFile ("layout-" + version + ".txt");
        const juce::File state  = goldenDir.getChildFile ("state-" + version + ".xml");
        if (golden.existsAsFile() || state.existsAsFile()) { std::printf ("golden files for %s already exist: not touched\n", version.toRawUTF8()); return; }
        golden.replaceWithText (now.joinIntoString ("\n") + "\n");
        auto params = allParams (proc);
        for (int i = 0; i < (int) params.size(); ++i) params[(size_t) i]->setValueNotifyingHost (goldenValue (params[(size_t) i], i, version));
        juce::MemoryBlock mb;
        proc.getStateInformation (mb);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        state.replaceWithText (xml->toString());
        std::printf ("wrote %s (%d controls) and %s\n", golden.getFullPathName().toRawUTF8(), now.size(), state.getFullPathName().toRawUTF8());
        return;
    }
    for (const char* version : kVersions)
    {
        juce::StringArray old;
        old.addLines (goldenDir.getChildFile (juce::String ("layout-") + version + ".txt").loadFileAsString());
        old.removeEmptyStrings();
        CHECK (old.size() == kNumControls, "reference list %s has %d controls", version, old.size());
        CHECK (now.size() >= old.size(), "control count %d is smaller than %s's %d", now.size(), version, old.size());
        int same = 0;
        for (int i = 0; i < old.size() && i < now.size(); ++i)
        {
            if (old[i] == now[i]) ++same;
            else std::printf ("control %d differs from %s:\n   shipped: %s\n   now:     %s\n", i, version, old[i].toRawUTF8(), now[i].toRawUTF8());
        }
        CHECK (same == old.size(), "%d of %d controls identical to %s", same, old.size(), version);
        std::printf ("control list: %d controls, first %d identical to %s\n", now.size(), same, version);
    }
    for (int i = kNumControls; i < now.size(); ++i)
        CHECK (now[i].contains (" v2 "), "control added after 0.1.0 must be version 2: %s", now[i].toRawUTF8());
}

// ------------------------------------------------------------------------------------------------ 2. old projects

static void testOldProjects (const juce::File& goldenDir)
{
    for (const char* version : kVersions)
    {
        const auto xml = juce::parseXML (goldenDir.getChildFile (juce::String ("state-") + version + ".xml"));
        CHECK (xml != nullptr, "golden state %s readable", version);
        if (xml == nullptr) continue;
        CHECK (xml->getStringAttribute ("pluginVersion") == version, "golden state is from %s", version);
        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary (*xml, mb);
        BrynProcessor proc;
        proc.setStateInformation (mb.getData(), (int) mb.getSize());
        auto params = allParams (proc);
        int same = 0;
        for (int i = 0; i < kNumControls; ++i)
        {
            auto* p = params[(size_t) i];
            const float want = p->paramID == BrParams::trigger ? 0.0f : goldenValue (p, i, version);
            if (std::abs (p->getValue() - want) < 1.0e-4f) ++same;
            else std::printf ("  %s: %f, saved %f\n", p->paramID.toRawUTF8(), p->getValue(), want);
        }
        CHECK (same == kNumControls, "project saved by %s: %d of %d controls restored (the Trigger button released)", version, same, kNumControls);
        CHECK (P (proc, BrParams::trigger)->getValue() < 0.5f, "a project never opens with the Trigger button held");
        // Saving again and loading gives the same.
        juce::MemoryBlock again;
        proc.getStateInformation (again);
        BrynProcessor proc2;
        proc2.setStateInformation (again.getData(), (int) again.getSize());
        auto p2 = allParams (proc2);
        int same2 = 0;
        for (int i = 0; i < kNumControls; ++i) same2 += std::abs (p2[(size_t) i]->getValue() - params[(size_t) i]->getValue()) < 1.0e-6f ? 1 : 0;
        CHECK (same2 == kNumControls, "save and reopen: %d of %d", same2, kNumControls);
        // Playing after loading it ends at rest.
        prepare (proc);
        runEmpty (proc, 2);
        setReal (proc, BrParams::mode, (float) bryn::modeAr);
        run (proc, one (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)), 512);
        runEmpty (proc, 20);
        run (proc, one (juce::MidiMessage::noteOff (1, 60)), 512);
        runEmpty (proc, 2000);
        CHECK (proc.getDisplay().stage.load() == 0 && ! proc.getDisplay().gate.load(), "%s project: plays and ends at rest", version);
        std::printf ("old projects: %s restored (%d controls)\n", version, same);
    }
    // Garbage leaves everything at its defaults.
    BrynProcessor proc;
    const char junk[] = "not a saved state";
    proc.setStateInformation (junk, (int) sizeof (junk));
    for (auto* p : allParams (proc)) CHECK (juce::approximatelyEqual (p->getValue(), p->getDefaultValue()), "junk state ignored (%s)", p->paramID.toRawUTF8());
    // A project saved with the Trigger button held (by automation) opens with it released.
    BrynProcessor held;
    setReal (held, BrParams::trigger, 1.0f);
    juce::MemoryBlock mb;
    held.getStateInformation (mb);
    BrynProcessor reopened;
    reopened.setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK (P (reopened, BrParams::trigger)->getValue() < 0.5f, "Trigger button saved held reopens released");
}

// ------------------------------------------------------------------------------------------------ 3. flags

static void testFlags()
{
    BrynProcessor proc;
    CHECK (proc.isMidiEffect() && proc.acceptsMidi() && proc.producesMidi(), "MIDI effect, MIDI in and out");
    CHECK (proc.getBusCount (true) == 0 && proc.getBusCount (false) == 0, "no audio buses (%d in, %d out)", proc.getBusCount (true), proc.getBusCount (false));
    CHECK (proc.getLatencySamples() == 0 && proc.getTailLengthSeconds() <= 0.0, "no latency, no tail");
    CHECK (proc.getName() == "Bryn", "name");
    CHECK ((int) allParams (proc).size() == kNumControls, "%d controls", (int) allParams (proc).size());
    for (const char* id : { BrParams::rise, BrParams::fall })
    {
        CHECK (juce::approximatelyEqual (P (proc, id)->convertFrom0to1 (0.0f), 3.0f) && std::abs (P (proc, id)->convertFrom0to1 (1.0f) - 10000.0f) < 0.5f, "%s 3 ms .. 10 s", id);
        CHECK (std::abs (P (proc, id)->convertFrom0to1 (0.5f) - 173.2f) < 0.5f, "%s centre 173 ms", id);
    }
    CHECK (P (proc, BrParams::rise)->getCurrentValueAsText() == "100 ms" && P (proc, BrParams::fall)->getCurrentValueAsText() == "400 ms",
           "defaults: %s, %s", P (proc, BrParams::rise)->getCurrentValueAsText().toRawUTF8(), P (proc, BrParams::fall)->getCurrentValueAsText().toRawUTF8());
    CHECK (std::abs (P (proc, BrParams::fall)->getValueForText ("1.2 s") - P (proc, BrParams::fall)->convertTo0to1 (1200.0f)) < 1e-4f, "typing 1.2 s");
    CHECK (P (proc, BrParams::mode)->getCurrentValueAsText() == "AR" && P (proc, BrParams::curve)->getCurrentValueAsText() == "Log"
           && P (proc, BrParams::sendAs)->getCurrentValueAsText() == "CC", "defaults: AR, Log, Send as CC");
    CHECK (juce::roundToInt (P (proc, BrParams::outChannel)->convertFrom0to1 (P (proc, BrParams::outChannel)->getValue())) == 14, "default MIDI out channel 14");
}

// ------------------------------------------------------------------------------------------------ 4. MIDI path

static void testMidiPath()
{
    BrynProcessor proc;
    prepare (proc);
    setReal (proc, BrParams::rise, 3.0f);
    auto first = run (proc, {}, 512);
    Receiver rx;
    rx.take (first);
    CHECK (first.size() == 2 && rx.value[14] == 0, "first block: rest (0) sent once, channel 14 (%d messages)", (int) first.size());
    CHECK (run (proc, {}, 512).empty(), "nothing more while at rest");

    juce::MidiBuffer in;
    in.addEvent (juce::MidiMessage::controllerEvent (3, 1, 64), 10);
    in.addEvent (juce::MidiMessage::pitchWheel (3, 9000), 20);
    in.addEvent (juce::MidiMessage::channelPressureChange (3, 50), 30);
    in.addEvent (juce::MidiMessage::programChange (3, 7), 40);
    in.addEvent (juce::MidiMessage::aftertouchChange (3, 60, 33), 50);
    const juce::uint8 sysex[] = { 0x7d, 0x01, 0x02 };
    in.addEvent (juce::MidiMessage::createSysExMessage (sysex, 3), 60);
    in.addEvent (juce::MidiMessage::noteOn (3, 60, (juce::uint8) 90), 100);
    in.addEvent (juce::MidiMessage::controllerEvent (3, 64, 127), 100);
    auto out = run (proc, in, 512);
    std::vector<Out> theirs, ours;
    for (auto& o : out) (isOurCc (o.m) && o.m.getChannel() == 14 ? ours : theirs).push_back (o);
    CHECK (theirs.size() == 8, "all 8 incoming messages passed on (%d)", (int) theirs.size());
    if (theirs.size() == 8)
    {
        CHECK (theirs[0].pos == 10 && theirs[0].m.isController() && theirs[0].m.getControllerValue() == 64, "CC 1 unchanged");
        CHECK (theirs[1].pos == 20 && theirs[1].m.getPitchWheelValue() == 9000, "pitch bend unchanged");
        CHECK (theirs[2].pos == 30 && theirs[2].m.getChannelPressureValue() == 50, "channel pressure unchanged");
        CHECK (theirs[3].pos == 40 && theirs[3].m.getProgramChangeNumber() == 7, "program change unchanged");
        CHECK (theirs[4].pos == 50 && theirs[4].m.isAftertouch(), "poly aftertouch unchanged");
        CHECK (theirs[5].pos == 60 && theirs[5].m.isSysEx() && theirs[5].m.getSysExDataSize() == 3, "sysex unchanged");
        CHECK (theirs[6].pos == 100 && theirs[6].m.isNoteOn() && theirs[6].m.getNoteNumber() == 60 && theirs[6].m.getChannel() == 3
               && theirs[6].m.getVelocity() == 90, "note: same note, channel 3, velocity 90, sample 100");
        CHECK (theirs[7].pos == 100 && theirs[7].m.isSustainPedalOn(), "pedal after the note, same sample, in order");
    }
    // The 3 ms rise reaches the top exactly in this block (the corner is sent at its sample: 100 + 144).
    rx.take (out);
    bool topAt = false, riseGate = false, fallGate = false;
    for (auto& o : ours)
    {
        if (o.m.getControllerNumber() == 81 && o.m.getControllerValue() == 127 && o.pos == 244) topAt = true;
        if (o.m.getControllerNumber() == 82 && o.m.getControllerValue() == 127 && o.pos == 100) riseGate = true;
        if (o.m.getControllerNumber() == 83 && o.m.getControllerValue() == 127 && o.pos == 244) fallGate = true;
    }
    CHECK (rx.value[14] == 16383 && topAt, "the top reached exactly, at sample 244 (3 ms after the note)");
    CHECK (riseGate && fallGate, "Rise gate (CC 82) on at the note, Fall gate (CC 83) on at the top");
    for (auto& o : ours) CHECK (o.m.getChannel() == 14, "Bryn's CC on channel 14");
    // AR: the fall runs while the key is still held, then rest; EOC at the end.
    PulseWatch w;
    w.take (out);
    for (int i = 0; i < 60; ++i) { auto o = run (proc, {}, 512); rx.take (o); w.take (o); }
    CHECK (proc.getDisplay().stage.load() == 0 && proc.getDisplay().gate.load() && rx.value[14] == 0, "AR: a whole cycle while the key is held, then rest at 0");
    CHECK (w.ccVal[14][90] == 0 && w.starts == 3, "EOC came and went (%d starts: rise gate, fall gate, EOC)", w.starts);
    // Release: velocity 0 counts as a note-off, and passes on unchanged.
    auto rel = run (proc, one (juce::MidiMessage (0x92, 60, 0), 5), 512);
    bool passed = false;
    for (auto& o : rel) if (o.m.isNoteOff() && o.m.getChannel() == 3 && o.pos == 5) passed = true;
    CHECK (passed && ! proc.getDisplay().gate.load(), "velocity-0 note-on passed on, and released the gate");
    // 0- and 1-sample blocks.
    run (proc, one (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100)), 0);
    out = run (proc, one (juce::MidiMessage::noteOff (1, 62)), 1);
    CHECK (out.size() >= 1 && out[0].pos == 0, "0- and 1-sample blocks");
    runEmpty (proc, 100);
    CHECK (proc.getDisplay().stage.load() == 0 && ! proc.getDisplay().gate.load(), "and the note in the 0-sample block was released");
    // Another channel set: everything goes there.
    setReal (proc, BrParams::outChannel, 7);
    Receiver rx2;
    run (proc, one (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)), 512);
    auto o2 = runEmpty (proc, 4);
    rx2.take (o2);
    bool only7 = true;
    for (auto& o : o2) if (isOurCc (o.m)) only7 = only7 && o.m.getChannel() == 7;
    CHECK (only7 && rx2.value[7] >= 0, "MIDI out channel 7: Bryn's MIDI goes out on channel 7");
    run (proc, one (juce::MidiMessage::noteOff (1, 60)), 512);
    runEmpty (proc, 200);

    // Send as Note and Both.
    for (int mode : { 1, 2 })
    {
        BrynProcessor p2;
        prepare (p2);
        runEmpty (p2, 2);
        setReal (p2, BrParams::sendAs, (float) mode);
        PulseWatch pw;
        int ccs = 0;
        auto o = run (p2, one (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 90), 100), 512);
        pw.take (o);
        bool noteAt = false, playedPassed = false;
        for (auto& x : o)
        {
            if (x.m.isNoteOn() && x.m.getNoteNumber() == 45 && x.m.getVelocity() == 100 && x.pos == 100 && x.m.getChannel() == 14) noteAt = true;
            if (x.m.isNoteOn() && x.m.getNoteNumber() == 60 && x.m.getVelocity() == 90 && x.m.getChannel() == 1 && x.pos == 100) playedPassed = true;
        }
        for (int i = 0; i < 80; ++i) for (auto& x : run (p2, {}, 512)) { pw.take (x.m); ccs += x.m.isController() && isPulseCc (x.m.getControllerNumber()) ? 1 : 0; }
        pw.take (run (p2, one (juce::MidiMessage::noteOff (1, 60)), 512));
        CHECK (noteAt && playedPassed, "Send as %s: gate note A1 (45) at the note's sample, velocity 100, channel 14; the played note passes unchanged", mode == 1 ? "Note" : "Both");
        CHECK ((mode == 1) == (ccs == 0), "Send as %s: %s", mode == 1 ? "Note" : "Both", mode == 1 ? "no gate CC" : "gate CC too");
        CHECK (! pw.anythingOn() && pw.doubled == 0 && pw.strayOff == 0 && pw.starts >= 3, "Send as %s: every gate note released, none doubled (%d starts)", mode == 1 ? "Note" : "Both", pw.starts);
    }
    // Loop runs without notes: a whole number of cycles, EOC each time.
    {
        BrynProcessor p2;
        prepare (p2);
        setReal (p2, BrParams::rise, 50.0f);
        setReal (p2, BrParams::fall, 50.0f);
        setReal (p2, BrParams::mode, (float) bryn::modeLoop);
        PulseWatch pw;
        int eoc = 0;
        for (int i = 0; i < 94; ++i) for (auto& x : run (p2, {}, 512)) { pw.take (x.m); eoc += x.m.isController() && x.m.getControllerNumber() == 90 && x.m.getControllerValue() == 127 ? 1 : 0; }
        CHECK (eoc == 10, "Loop 100 ms, 1 s with no notes: 10 cycles, 10 EOC (%d)", eoc);
    }
}

// ------------------------------------------------------------------------------------------------ 5. automation

static void testAutomation()
{
    BrynProcessor proc;
    prepare (proc);
    runEmpty (proc, 2);
    auto params = allParams (proc);
    int notes = 0, nonZero = 0;
    // No notes held, not in Loop: moving any control (but Mode and the Trigger button, which play) sends at most the
    // rest value 0 (after a channel change or switching an output back on), never anything else.
    for (auto* p : params)
    {
        if (p->paramID == BrParams::trigger || p->paramID == BrParams::mode) continue;
        for (int i = 0; i <= 20; ++i)
        {
            p->setValueNotifyingHost ((float) i / 20.0f);
            for (auto& o : run (proc, {}, 256))
            {
                notes += o.m.isNoteOnOrOff() ? 1 : 0;
                if (isOurCc (o.m) && o.m.getControllerValue() != 0) ++nonZero;
            }
        }
        p->setValueNotifyingHost (p->getDefaultValue());
        runEmpty (proc, 2);
    }
    CHECK (notes == 0 && nonZero == 0, "moving controls with no note held: only rest values (%d notes, %d others)", notes, nonZero);

    // A note held: automate everything (Mode too) hard. Never more than one value per output per block; the gates
    // never more than two on and two off per output per block, never a doubled note.
    PulseWatch watch;
    run (proc, one (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)), 512);
    std::mt19937 rng (42);
    int bad = 0, burst = 0;
    for (int k = 0; k < 3000; ++k)
    {
        auto* p = params[rng() % params.size()];
        if (p->paramID == BrParams::trigger) continue;
        p->setValueNotifyingHost ((float) (rng() % 1001) / 1000.0f);
        const auto out = run (proc, {}, 64 + (int) (rng() % 512));
        watch.take (out);
        std::map<int, int> perChannelFine, perPulse;
        for (auto& o : out)
        {
            if (o.m.isController() && o.m.getControllerNumber() == 81) ++perChannelFine[o.m.getChannel()];
            if (o.m.isController() && isPulseCc (o.m.getControllerNumber())) ++perPulse[o.m.getControllerNumber()];
            if (isGateNote (o.m)) ++perPulse[o.m.getNoteNumber()];
            notes += o.m.isNoteOnOrOff() && ! isGateNote (o.m) ? 1 : 0;
        }
        for (auto& [key, count] : perChannelFine) bad += count > 1 ? 1 : 0;
        for (auto& [key, count] : perPulse) burst += count > 4 ? 1 : 0;
    }
    CHECK (notes == 0, "automation while held sends no other notes (%d)", notes);
    CHECK (bad == 0, "automation while held: never more than one value per output per block (%d blocks over)", bad);
    CHECK (burst == 0 && watch.doubled == 0 && watch.strayOff == 0, "automation while held: no gate bursts (%d), no doubled or stray gate notes (%d, %d)", burst, watch.doubled, watch.strayOff);
    for (auto* p : params) p->setValueNotifyingHost (p->getDefaultValue());
    watch.take (run (proc, one (juce::MidiMessage::noteOff (1, 60)), 512));
    for (int i = 0; i < 300; ++i) watch.take (run (proc, {}, 512));
    CHECK (proc.getDisplay().stage.load() == 0 && ! proc.getDisplay().gate.load(), "after automation and release (AR): at rest");
    CHECK (! watch.anythingOn(), "after automation and release: no gate left on, on any channel");

    // A long rise (Loop, 10 s), gates sent as both: Send as, the channel, the gate switches and the Function switch
    // changed over and over. These only ever end a gate; none of them starts one.
    {
        BrynProcessor p2;
        prepare (p2);
        setReal (p2, BrParams::sendAs, 2.0f);
        setReal (p2, BrParams::rise, 10000.0f);
        setReal (p2, BrParams::mode, (float) bryn::modeLoop);
        PulseWatch w;
        for (int i = 0; i < 20; ++i) w.take (run (p2, {}, 512));
        const int startsBefore = w.starts;
        CHECK (w.noteOn[14][45] && w.ccVal[14][82] == 127 && startsBefore == 2, "rising: the Rise gate on as a note and as CC");
        std::mt19937 r2 (7);
        const char* changeable[] = { BrParams::sendAs, BrParams::outChannel, BrParams::riseGateOn, BrParams::fallGateOn, BrParams::eocOn, BrParams::outFnOn };
        for (int k = 0; k < 600; ++k)
        {
            P (p2, changeable[r2() % 6])->setValueNotifyingHost ((float) (r2() % 1001) / 1000.0f);
            w.take (run (p2, {}, 32 + (int) (r2() % 600)));
        }
        CHECK (p2.getDisplay().stage.load() == (int) bryn::Stage::rise, "still rising");
        CHECK (w.starts == startsBefore && w.doubled == 0 && w.strayOff == 0, "changing Send as, channel and switches during the rise never starts a gate or a note (%d new)", w.starts - startsBefore);
        for (auto* p : allParams (p2)) p->setValueNotifyingHost (p->getDefaultValue());
        for (int i = 0; i < 2000; ++i) w.take (run (p2, {}, 512));
        CHECK (! w.anythingOn() && p2.getDisplay().stage.load() == 0, "back to AR: the cycle ends, nothing left on");
    }
}

// ------------------------------------------------------------------------------------------------ 6. stops

static void testStops()
{
    struct Case { const char* name; std::function<void (BrynProcessor&, FakePlayHead&, double&)> stop; bool bypass; };
    const std::vector<Case> cases {
        { "transport stop", [] (BrynProcessor&, FakePlayHead& ph, double&) { ph.playing = false; }, false },
        { "reset", [] (BrynProcessor& p, FakePlayHead&, double&) { p.reset(); }, false },
        { "releaseResources", [] (BrynProcessor& p, FakePlayHead&, double&) { p.releaseResources(); }, false },
        { "prepareToPlay", [] (BrynProcessor& p, FakePlayHead&, double&) { p.prepareToPlay (44100.0, 256); }, false },
        { "All Notes Off", [] (BrynProcessor&, FakePlayHead&, double&) {}, false },
        { "bypass", [] (BrynProcessor&, FakePlayHead&, double&) {}, true },
        { "a 2 s pause in Logic's calls", [] (BrynProcessor&, FakePlayHead&, double& t) { t += 2000.0; }, false },
    };
    for (int mode : { 0, 2 })   // gates sent as CC, then as CC and notes
    for (const auto& c : cases)
    {
        BrynProcessor proc;
        FakePlayHead ph;
        ph.playing = true;
        proc.setPlayHead (&ph);
        double t = 0.0;
        proc.clock = [&t] { return t; };
        prepare (proc);
        setReal (proc, BrParams::sendAs, (float) mode);
        setReal (proc, BrParams::mode, (float) bryn::modeAsr);
        setReal (proc, BrParams::rise, 10000.0f);
        Receiver rx;
        PulseWatch w;
        auto step = [&] (juce::MidiBuffer m, bool bypassed = false) { t += 10.0; auto o = run (proc, m, 512, bypassed); rx.take (o); w.take (o); return o; };
        step ({});
        step (one (juce::MidiMessage::noteOn (2, 64, (juce::uint8) 100)));
        for (int i = 0; i < 40; ++i) step ({});
        CHECK (proc.getDisplay().stage.load() == (int) bryn::Stage::rise && rx.value[14] > 0, "%s: rising before", c.name);
        CHECK (w.ccVal[14][82] == 127 && w.noteOn[14][45] == (mode == 2), "%s: Rise gate on before (%s)", c.name, mode == 2 ? "CC and note" : "CC");
        c.stop (proc, ph, t);
        if (juce::String (c.name) == "All Notes Off")
        {
            step (one (juce::MidiMessage::allNotesOff (2)));
            CHECK (proc.getDisplay().stage.load() == (int) bryn::Stage::fall, "All Notes Off: ASR falls");
            for (int i = 0; i < 100; ++i) step ({});
        }
        else
        {
            const auto o = step ({}, c.bypass);
            int zeros = 0;
            for (auto& m : o) if (m.m.isController() && m.m.getControllerNumber() == 81 && m.m.getControllerValue() == 0 && m.pos == 0) ++zeros;
            CHECK (proc.getDisplay().stage.load() == 0 && ! proc.getDisplay().gate.load(), "%s: at rest at once", c.name);
            CHECK (zeros == 1 && rx.value[14] == 0, "%s: 0 sent at the first sample (%d)", c.name, zeros);
            int gateEnds = 0;
            for (auto& m : o)
                if (m.pos == 0 && ((m.m.isController() && m.m.getControllerNumber() == 82 && m.m.getControllerValue() == 0) || (m.m.isNoteOff() && m.m.getNoteNumber() == 45)))
                    ++gateEnds;
            CHECK (gateEnds == (mode == 2 ? 2 : 1) && ! w.anythingOn(), "%s: Rise gate ended at the first sample (%s)", c.name, mode == 2 ? "CC 0 and note-off" : "CC 0");
            if (c.bypass)
            {
                int ours = 0;
                for (int i = 0; i < 20; ++i) for (auto& m : step (one (juce::MidiMessage::noteOn (1, 70, (juce::uint8) 100)), true)) ours += isOurCc (m.m) || isGateNote (m.m) ? 1 : 0;
                CHECK (ours == 0, "bypassed: notes pass, nothing is sent (%d)", ours);
                step (one (juce::MidiMessage::noteOff (1, 70)), true);
            }
        }
        // The key's later release passes on and changes nothing; nothing more is sent.
        ph.playing = false;
        const auto o = step (one (juce::MidiMessage::noteOff (2, 64)));
        int ours = 0; bool offPassed = false;
        for (auto& m : o) { ours += isOurCc (m.m) ? 1 : 0; offPassed = offPassed || (m.m.isNoteOff() && m.m.getNoteNumber() == 64); }
        for (int i = 0; i < 20; ++i) for (auto& m : step ({})) ours += isOurCc (m.m) ? 1 : 0;
        CHECK (offPassed && ours == 0 && proc.getDisplay().stage.load() == 0, "%s: the key's release passes on, nothing else happens (%d)", c.name, ours);
        // And the next note plays normally.
        step (one (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)));
        CHECK (proc.getDisplay().stage.load() != 0, "%s: the next note plays", c.name);
        step (one (juce::MidiMessage::noteOff (1, 60)));
        for (int i = 0; i < 100; ++i) step ({});
        CHECK (proc.getDisplay().stage.load() == 0 && rx.value[14] == 0, "%s: and ends at rest", c.name);
        CHECK (! w.anythingOn() && w.doubled == 0 && w.strayOff == 0, "%s: no gate or gate note left on (%s)", c.name, mode == 2 ? "Both" : "CC");
        proc.setPlayHead (nullptr);
    }
    // Loop: a transport stop sends 0 and starts again from 0.
    {
        BrynProcessor proc;
        FakePlayHead ph;
        ph.playing = true;
        proc.setPlayHead (&ph);
        prepare (proc);
        setReal (proc, BrParams::mode, (float) bryn::modeLoop);
        setReal (proc, BrParams::rise, 2000.0f);
        Receiver rx;
        for (int i = 0; i < 40; ++i) rx.take (run (proc, {}, 512));
        const int before = rx.value[14];
        ph.playing = false;
        const auto o = run (proc, {}, 512);
        bool zeroFirst = false;
        for (auto& m : o) if (m.m.isController() && m.m.getControllerNumber() == 81 && m.pos == 0 && m.m.getControllerValue() == 0) zeroFirst = true;
        rx.take (o);
        CHECK (before > 1000 && zeroFirst && proc.getDisplay().stage.load() == (int) bryn::Stage::rise, "Loop, transport stop: 0 sent, rising again from 0");
        proc.setPlayHead (nullptr);
    }
}

// ------------------------------------------------------------------------------------------------ 7. fuzz

static void fuzz (unsigned seed, bool withBypass)
{
    BrynProcessor proc;
    FakePlayHead ph;
    proc.setPlayHead (&ph);
    double t = 0.0;
    proc.clock = [&t] { return t; };
    prepare (proc);
    std::mt19937 rng (seed);
    auto params = allParams (proc);
    Receiver rx;
    PulseWatch w;
    bool down[17][128] {};
    long long events = 0;
    for (int block = 0; block < 18000; ++block)
    {
        t += rng() % 400 == 0 ? 1500.0 : 10.0;
        const int n = rng() % 40 == 0 ? (int) (rng() % 2) : 1 + (int) (rng() % 1024);
        juce::MidiBuffer in;
        const int count = (int) (rng() % 6);
        for (int k = 0; k < count; ++k)
        {
            const int at = n > 0 ? (int) (rng() % (unsigned) n) : 0, ch = 1 + (int) (rng() % 3), note = 58 + (int) (rng() % 6);
            const unsigned kind = rng() % 24;
            if (kind == 0)      { in.addEvent (juce::MidiMessage::allNotesOff (ch), at); for (auto& d : down[ch]) d = false; }
            else if (kind < 2)  in.addEvent (juce::MidiMessage::controllerEvent (ch, 1, (int) (rng() % 128)), at);
            else if (kind < 12) { in.addEvent (juce::MidiMessage::noteOn (ch, note, (juce::uint8) (1 + rng() % 127)), at); down[ch][note] = true; }
            else if (kind < 14) { in.addEvent (juce::MidiMessage (0x90 + ch - 1, note, 0), at); down[ch][note] = false; }
            else                { in.addEvent (juce::MidiMessage::noteOff (ch, note), at); down[ch][note] = false; }
            ++events;
        }
        if (rng() % 30 == 0)
        {
            auto* p = params[rng() % params.size()];
            p->setValueNotifyingHost ((float) (rng() % 1001) / 1000.0f);
        }
        if (rng() % 200 == 0) ph.playing = ! ph.playing;
        if (rng() % 500 == 0) proc.reset();
        const bool bypassed = withBypass && (block / 300) % 4 == 3;
        const auto o = run (proc, in, n, bypassed);
        rx.take (o);
        w.take (o);
    }
    // Release everything (and the Trigger button), AR, the outputs on, then wait.
    juce::MidiBuffer offs;
    for (int ch = 1; ch <= 16; ++ch) for (int nt = 0; nt < 128; ++nt) if (down[ch][nt]) offs.addEvent (juce::MidiMessage::noteOff (ch, nt), 0);
    setReal (proc, BrParams::trigger, 0.0f);
    setReal (proc, BrParams::mode, (float) bryn::modeAr);
    setReal (proc, BrParams::outFnOn, 1.0f);
    for (const char* id : { BrParams::rise, BrParams::fall }) setReal (proc, id, 50.0f);
    t += 10.0;
    { const auto o = run (proc, offs, 512); rx.take (o); w.take (o); }
    for (int i = 0; i < 400; ++i) { t += 10.0; const auto o = run (proc, {}, 512); rx.take (o); w.take (o); }
    bool allZero = true;
    for (int ch = 1; ch <= 16; ++ch) allZero = allZero && rx.value[ch] <= 0;
    CHECK (proc.getDisplay().stage.load() == 0 && ! proc.getDisplay().gate.load(), "fuzz %u: at rest after every key is released", seed);
    CHECK (allZero, "fuzz %u: every channel's receiver at 0", seed);
    CHECK (! w.anythingOn() && w.doubled == 0 && w.strayOff == 0, "fuzz %u: no gate CC or gate note left on, none doubled (%d starts)", seed, w.starts);
    std::printf ("stuck-gate and stuck-note fuzz%s: %lld random events in 18,000 blocks, %d gate starts, nothing stuck\n", withBypass ? " with bypass" : "", events, w.starts);
    proc.setPlayHead (nullptr);
}

// ------------------------------------------------------------------------------------------------ 8. panel

static void testPanel()
{
    BrynProcessor proc;
    prepare (proc);
    runEmpty (proc, 2);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditorAndMakeActive());
    auto* editor = dynamic_cast<BrynEditor*> (ed.get());
    CHECK (editor != nullptr, "panel opens");
    if (editor == nullptr) return;
    CHECK (ed->getWidth() == BrynEditor::kWidth && ed->getHeight() == BrynEditor::kHeight, "design size %dx%d", ed->getWidth(), ed->getHeight());
    const auto image = ed->createComponentSnapshot (ed->getLocalBounds());
    CHECK (image.isValid() && image.getWidth() == BrynEditor::kWidth, "panel draws");

    // Every control inside the panel, live, moves only its own parameter.
    auto params = allParams (proc);
    CHECK ((int) editor->panel.controls().size() == kNumControls, "every control is on the panel (%d)", (int) editor->panel.controls().size());
    for (auto& c : editor->panel.controls())
    {
        CHECK (juce::Rectangle<int> (0, 0, BrynPanel::kWidth, BrynPanel::kHeight).contains (c.control->getBounds()), "%s inside the panel", c.paramId.toRawUTF8());
        CHECK (c.control->isEnabled() && c.control->isVisible(), "%s live", c.paramId.toRawUTF8());
        std::vector<float> before;
        for (auto* p : params) before.push_back (p->getValue());
        if (auto* s = dynamic_cast<juce::Slider*> (c.control))            s->setValue (s->getMinimum() + (s->getMaximum() - s->getMinimum()) * 0.71, juce::sendNotificationSync);
        else if (auto* b = dynamic_cast<juce::ToggleButton*> (c.control)) b->setToggleState (! b->getToggleState(), juce::sendNotificationSync);
        else if (auto* cb = dynamic_cast<juce::ComboBox*> (c.control))     cb->setSelectedId (5, juce::sendNotificationSync);
        else if (auto* sg = dynamic_cast<Segmented*> (c.control))          sg->choose (sg->selected() == 0 ? 1 : 0);
        else if (auto* tb = dynamic_cast<TriggerButton*> (c.control))      tb->press (true);
        int changed = 0; bool own = false;
        for (size_t i = 0; i < params.size(); ++i)
            if (std::abs (params[i]->getValue() - before[i]) > 1e-6f) { ++changed; own = own || params[i]->paramID == c.paramId; }
        CHECK (changed == 1 && own, "%s moves only its own parameter (%d changed)", c.paramId.toRawUTF8(), changed);
        if (auto* tb = dynamic_cast<TriggerButton*> (c.control)) tb->press (false);
        for (size_t i = 0; i < params.size(); ++i) params[i]->setValueNotifyingHost (params[i]->getDefaultValue());
    }
    runEmpty (proc, 100);

    // The Trigger button plays the function (ASR: held = rise and hold; let go = fall).
    editor->panel.getModeSwitch().choose (bryn::modeAsr);
    CHECK (juce::roundToInt (P (proc, BrParams::mode)->convertFrom0to1 (P (proc, BrParams::mode)->getValue())) == bryn::modeAsr, "Mode switch sets ASR");
    editor->panel.getTriggerButton().press (true);
    runEmpty (proc, 2);
    CHECK (proc.getDisplay().gate.load() && proc.getDisplay().stage.load() == (int) bryn::Stage::rise, "Trigger button pressed: the function rises");
    runEmpty (proc, 20);
    CHECK (proc.getDisplay().stage.load() == (int) bryn::Stage::hold, "Trigger button held (ASR): the top is held");
    editor->panel.getTriggerButton().press (false);
    runEmpty (proc, 1);
    CHECK (proc.getDisplay().stage.load() == (int) bryn::Stage::fall, "Trigger button let go: fall");
    runEmpty (proc, 100);
    editor->panel.getModeSwitch().choose (bryn::modeAr);

    // The lights and the moving point: AR, rise 500 ms, fall 500 ms.
    setReal (proc, BrParams::rise, 500.0f);
    setReal (proc, BrParams::fall, 500.0f);
    auto& lights = editor->panel.getPulseLights();
    auto& view = editor->panel.getFunctionView();
    run (proc, one (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)), 512);
    runEmpty (proc, 20);
    for (int i = 0; i < 5; ++i) { lights.refresh(); view.refresh(); }
    CHECK (lights.lit (0) && ! lights.lit (1) && ! lights.lit (2), "rising: only the Rise gate light is lit");
    {
        const auto& d = proc.getDisplay();
        const auto pt = view.pointFor (d.stage.load(), d.level.load(), d.cycles.load(), d.gate.load());
        const auto sg = view.segments();
        // Where the drawn curve is at that x.
        bryn::Curve cv; cv.setShape (1.0);
        const double t = (pt.x - sg[0].x) / sg[0].w;
        const float yCurve = 200.0f - (float) cv (t) * 178.0f;
        CHECK (pt.x > sg[0].x && pt.x < sg[0].x + sg[0].w && std::abs (pt.y - yCurve) < 0.5f, "the moving point sits on the drawn rise (%.1f, %.1f / %.1f)", pt.x, pt.y, yCurve);
    }
    runEmpty (proc, 50);
    for (int i = 0; i < 5; ++i) lights.refresh();
    CHECK (! lights.lit (0) && lights.lit (1), "falling: the Fall gate light is lit");
    runEmpty (proc, 50);
    lights.refresh(); view.refresh();
    CHECK (lights.lit (2) && view.eocLit(), "end of cycle: the EOC lights flash");
    setReal (proc, BrParams::fallGateOn, 0.0f);
    run (proc, one (juce::MidiMessage::noteOff (1, 60)), 512);
    run (proc, one (juce::MidiMessage::noteOn (1, 61, (juce::uint8) 100)), 512);
    runEmpty (proc, 60);
    for (int i = 0; i < 5; ++i) lights.refresh();
    CHECK (! lights.lit (1) && proc.getDisplay().stage.load() == (int) bryn::Stage::fall, "Fall gate switched off: its light stays dark while falling");
    setReal (proc, BrParams::fallGateOn, 1.0f);
    run (proc, one (juce::MidiMessage::noteOff (1, 61)), 512);
    runEmpty (proc, 200);

    // Resizing: 80% to 200%, proportions locked, size saved with the project.
    ed->setSize (BrynEditor::kWidth * 3 / 2, BrynEditor::kHeight * 3 / 2);
    CHECK (ed->getWidth() == BrynEditor::kWidth * 3 / 2, "panel resizes to 150%%");
    auto* cons = ed->getConstrainer();
    CHECK (cons != nullptr && cons->getMinimumWidth() == 624 && cons->getMaximumWidth() == 1560 && std::abs (cons->getFixedAspectRatio() - 780.0 / 470.0) < 1e-9,
           "size limits 624..1560 wide, proportions locked");
    const int savedWidth = (int) proc.getValueTreeState().state.getProperty (BrynEditor::widthProperty, -1);
    CHECK (savedWidth == BrynEditor::kWidth * 3 / 2, "window size saved with the project (%d)", savedWidth);
    proc.editorBeingDeleted (ed.get());
    ed.reset();
    juce::MemoryBlock mb;
    proc.getStateInformation (mb);
    BrynProcessor proc2;
    proc2.setStateInformation (mb.getData(), (int) mb.getSize());
    std::unique_ptr<juce::AudioProcessorEditor> ed2 (proc2.createEditorAndMakeActive());
    CHECK (ed2->getWidth() == BrynEditor::kWidth * 3 / 2, "reopened at the saved size (%d)", ed2->getWidth());
    proc2.editorBeingDeleted (ed2.get());
}

static void writeSnapshots (const juce::File& dir)
{
    dir.createDirectory();
    auto shoot = [&] (const char* name, int width, std::function<void (BrynProcessor&)> setup) {
        BrynProcessor proc;
        prepare (proc);
        runEmpty (proc, 2);
        setup (proc);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditorAndMakeActive());
        ed->setSize (width, BrynEditor::heightForWidth (width));
        dynamic_cast<BrynEditor*> (ed.get())->panel.getFunctionView().refresh();
        dynamic_cast<BrynEditor*> (ed.get())->panel.getPulseLights().refresh();
        const auto image = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        juce::FileOutputStream os (dir.getChildFile (name));
        os.setPosition (0); os.truncate();
        png.writeImageToStream (image, os);
        proc.editorBeingDeleted (ed.get());
        std::printf ("wrote %s\n", dir.getChildFile (name).getFullPathName().toRawUTF8());
    };
    auto hold = [] (BrynProcessor& p, int blocks) {
        run (p, one (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)), 512);
        runEmpty (p, blocks);
    };
    shoot ("window-1-default.png", 780, [] (BrynProcessor&) {});
    shoot ("window-2-AR-rising.png", 780, [&] (BrynProcessor& p) { setReal (p, BrParams::rise, 2000.0f); hold (p, 60); });
    shoot ("window-3-AR-falling.png", 780, [&] (BrynProcessor& p) { setReal (p, BrParams::fall, 3000.0f); hold (p, 60); });
    shoot ("window-4-ASR-hold.png", 780, [&] (BrynProcessor& p) { setReal (p, BrParams::mode, (float) bryn::modeAsr); hold (p, 60); });
    shoot ("window-5-Loop-Lin.png", 780, [&] (BrynProcessor& p) {
        setReal (p, BrParams::mode, (float) bryn::modeLoop);
        setReal (p, BrParams::curve, 0.0f);
        setReal (p, BrParams::rise, 700.0f);
        setReal (p, BrParams::fall, 1500.0f);
        runEmpty (p, 250);
    });
    shoot ("window-6-small-80.png", 624, [&] (BrynProcessor& p) { hold (p, 3); });
    shoot ("window-7-send-as-note.png", 780, [&] (BrynProcessor& p) { setReal (p, BrParams::sendAs, 1.0f); setReal (p, BrParams::eocOn, 0.0f); hold (p, 3); });
    shoot ("window-8-send-as-both-ch3.png", 780, [&] (BrynProcessor& p) { setReal (p, BrParams::sendAs, 2.0f); setReal (p, BrParams::outChannel, 3.0f); hold (p, 3); });
    shoot ("window-9-large-200.png", 1560, [&] (BrynProcessor& p) { hold (p, 3); });
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    bool writeGolden = false;
    juce::File goldenDir (BRYN_GOLDEN_DIR);
    for (int i = 1; i < argc; ++i)
    {
        if (juce::String (argv[i]) == "--write-golden") writeGolden = true;
        if (juce::String (argv[i]) == "--snapshot" && i + 1 < argc) { writeSnapshots (juce::File (argv[i + 1])); return 0; }
    }
    testControlList (goldenDir, writeGolden);
    if (writeGolden) return 0;
    testOldProjects (goldenDir);
    testFlags();
    testMidiPath();
    testAutomation();
    testStops();
    fuzz (777, false);
    fuzz (4242, true);
    testPanel();
    std::printf ("plugin checks: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
