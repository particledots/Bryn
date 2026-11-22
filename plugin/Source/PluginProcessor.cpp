#include "PluginProcessor.h"
#include "PluginEditor.h"

BrynProcessor::BrynProcessor()
    : AudioProcessor (BusesProperties()),   // a MIDI effect: no audio buses
      apvts (*this, nullptr, "Bryn", BrParams::createLayout())
{
    manualParam = apvts.getRawParameterValue (BrParams::trigger);
    events.reserve (4096);
    out.ensureSize (65536);
    engine.prepare (48000.0);
}

bool BrynProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet().isDisabled() && layouts.getMainInputChannelSet().isDisabled();
}

// Logic may call these when it stops, resets or rebuilds the plugin. A MIDI effect cannot send MIDI from here, so the
// next block starts by sending the function to rest (0 if the output is not already at 0, every gate ended).
void BrynProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);      // at rest, everything sent afresh
    out.ensureSize (65536);
    pendingRest = true;
}
void BrynProcessor::releaseResources() { pendingRest = true; }
void BrynProcessor::reset()            { pendingRest = true; }

void BrynProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& m)          { process (m, b.getNumSamples()); }
void BrynProcessor::processBlock (juce::AudioBuffer<double>& b, juce::MidiBuffer& m)         { process (m, b.getNumSamples()); }
void BrynProcessor::processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer& m)  { processBypassed (m, b.getNumSamples()); }
void BrynProcessor::processBlockBypassed (juce::AudioBuffer<double>& b, juce::MidiBuffer& m) { processBypassed (m, b.getNumSamples()); }

// The whole MIDI path. Every incoming event passes on unchanged at its own sample; the notes are also read for the gate
// (velocity 0 counts as a release; All Notes Off / All Sound Off release that channel's keys). The engine adds its MIDI.
void BrynProcessor::process (juce::MidiBuffer& midi, int numSamples)
{
    const double now = clock();
    if (lastCall >= 0.0 && now - lastCall > kGapMs) pendingRest = true;
    lastCall = now;
    wasBypassed = false;

    if (pendingRest.exchange (false)) engine.panic();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            const bool playing = pos->getIsPlaying();
            if (wasPlaying && ! playing) engine.panic();   // transport stop: rest at the first stopped block (Loop restarts from 0)
            wasPlaying = playing;
        }

    engine.setSettings (BrParams::read (apvts));

    out.clear();
    events.clear();
    const int last = juce::jmax (0, numSamples - 1);
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int at = juce::jlimit (0, last, meta.samplePosition);
        out.addEvent (m, at);
        if (events.size() >= 4000) continue;
        const auto ch = (std::uint8_t) juce::jlimit (0, 15, m.getChannel() - 1);
        if (m.isNoteOn())
            events.push_back ({ at, bryn::InputEvent::noteOn, ch, (std::uint8_t) m.getNoteNumber() });
        else if (m.isNoteOff())   // includes velocity 0
            events.push_back ({ at, bryn::InputEvent::noteOff, ch, (std::uint8_t) m.getNoteNumber() });
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            events.push_back ({ at, bryn::InputEvent::allNotesOff, ch, 0 });
    }

    sink.out = &out;
    sink.last = last;
    engine.render (numSamples, events.data(), (int) events.size(), manualParam->load() >= 0.5f, sink);
    midi.swapWith (out);
    publish();
}

// Bypassed: incoming MIDI passes untouched and the keys are not read. The function goes to rest once (0 sent, every
// gate ended) and stays there (Loop too) until the bypass is lifted.
void BrynProcessor::processBypassed (juce::MidiBuffer& midi, int numSamples)
{
    lastCall = clock();
    if (! wasBypassed) engine.panic();
    wasBypassed = true;
    pendingRest = false;
    engine.setSettings (BrParams::read (apvts));
    sink.out = &midi;
    sink.last = juce::jmax (0, numSamples - 1);
    engine.flushOnly (numSamples, sink);
    publish();
}

void BrynProcessor::publish()
{
    const auto& fn = engine.function();
    display.level.store ((float) fn.level());
    display.progress.store ((float) fn.progress());
    display.stage.store ((int) fn.stage());
    display.cycles.store (fn.cycles());
    display.gate.store (engine.gate());
    for (int i = 0; i < bryn::kNumSmoothOutputs; ++i) display.sent[i].store (engine.ccStream().lastSent (i));
    for (int k = 0; k < bryn::kNumPulseOutputs; ++k)
    {
        display.pulseOn[k].store (engine.pulses().isOn (k));
        display.pulseStarts[k].store (engine.pulses().starts (k));
    }
    display.blocks.fetch_add (1);
}

void BrynProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
    {
        xml->setAttribute ("pluginVersion", JucePlugin_VersionString);
        copyXmlToBinary (*xml, destData);
    }
}

void BrynProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            // The Trigger button is a momentary button: a project never opens with it held.
            if (auto* p = apvts.getParameter (BrParams::trigger)) p->setValueNotifyingHost (0.0f);
        }
}

juce::AudioProcessorEditor* BrynProcessor::createEditor() { return new BrynEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BrynProcessor(); }
