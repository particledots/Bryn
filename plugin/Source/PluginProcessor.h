// What Logic sees: a MIDI effect (AU type aumi) with no audio. Every incoming MIDI message passes through unchanged,
// at its own sample, with no delay. The notes are also read: they are the function's gate. The function goes out as
// 14-bit CC on the chosen channel (14 by default), and the Rise gate, Fall gate and EOC as CC and/or notes, added to
// the MIDI stream at the exact samples. Adapted from Hillside 0.2.0.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <bryn/Engine.h>
#include "Parameters.h"

#include <atomic>
#include <functional>
#include <vector>

// What the panel shows (written by the audio thread after every block, read by the panel).
struct BrynDisplay
{
    std::atomic<float> level { 0.0f }, progress { 0.0f };
    std::atomic<int> stage { 0 };                  // bryn::Stage
    std::atomic<bool> gate { false };
    std::atomic<unsigned> cycles { 0 };            // cycles completed (end of fall)
    std::atomic<int> sent[bryn::kNumSmoothOutputs] { { -1 } };            // the last 14-bit value the output sent
    std::atomic<bool> pulseOn[bryn::kNumPulseOutputs] {};                 // Rise gate, Fall gate, EOC: on now
    std::atomic<unsigned> pulseStarts[bryn::kNumPulseOutputs] {};         // how many times each has come on
    std::atomic<unsigned> blocks { 0 };
};

class BrynProcessor final : public juce::AudioProcessor
{
public:
    BrynProcessor();
    ~BrynProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Bryn"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }
    const BrynDisplay& getDisplay() const { return display; }

    // If Logic stops calling the plugin for this long and then calls again, the function is sent to rest first (a key
    // released while the plugin was not being run can never leave the gate held). Replaceable for the checks.
    static constexpr double kGapMs = 1000.0;
    std::function<double()> clock = [] { return juce::Time::getMillisecondCounterHiRes(); };

private:
    struct Sink final : bryn::MidiSink
    {
        juce::MidiBuffer* out = nullptr;
        int last = 0;
        void cc (int offset, int channel, int number, int value) override
        {
            out->addEvent (juce::MidiMessage::controllerEvent (channel, number, value), juce::jlimit (0, last, offset));
        }
        void note (int offset, int channel, int number, int velocity) override
        {
            const int at = juce::jlimit (0, last, offset);
            if (velocity > 0) out->addEvent (juce::MidiMessage::noteOn (channel, number, (juce::uint8) velocity), at);
            else              out->addEvent (juce::MidiMessage::noteOff (channel, number), at);
        }
    };

    void process (juce::MidiBuffer& midi, int numSamples);
    void processBypassed (juce::MidiBuffer& midi, int numSamples);
    void publish();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* manualParam = nullptr;
    bryn::Engine engine;            // used by the audio thread only
    std::vector<bryn::InputEvent> events;
    juce::MidiBuffer out;
    Sink sink;
    std::atomic<bool> pendingRest { false };
    bool wasPlaying = false, wasBypassed = false;
    double lastCall = -1.0;
    BrynDisplay display;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrynProcessor)
};
