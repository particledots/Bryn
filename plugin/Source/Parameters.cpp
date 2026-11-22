#include "Parameters.h"

#include <cmath>

namespace BrParams
{
const char* const pulseOnIds[bryn::kNumPulseOutputs] = { riseGateOn, fallGateOn, eocOn };
const juce::StringArray curveNames  { "Lin", "Log" };
const juce::StringArray modeNames   { "ASR", "AR", "Loop" };
const juce::StringArray sendAsNames { "CC", "Note", "Both" };

juce::String timeText (float ms)
{
    if (ms < 10.0f)    return juce::String (ms, 1) + " ms";
    if (ms < 1000.0f)  return juce::String (juce::roundToInt (ms)) + " ms";
    return juce::String (ms / 1000.0f, 2) + " s";
}

namespace
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

    // A time control: the fader position moves the time on a logarithmic scale (equal ratios for equal moves), so the
    // centre is 173 ms. (As Hillside's.)
    void addTime (Layout& l, const char* id, const char* name, float lo, float hi, float def)
    {
        juce::NormalisableRange<float> range (lo, hi,
            [] (float a, float b, float x) { return a * std::pow (b / a, juce::jlimit (0.0f, 1.0f, x)); },
            [] (float a, float b, float v) { return std::log (juce::jlimit (a, b, v) / a) / std::log (b / a); });
        l.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, def,
            juce::AudioParameterFloatAttributes()
                .withLabel ("ms")
                .withStringFromValueFunction ([] (float v, int) { return timeText (v); })
                .withValueFromStringFunction ([] (const juce::String& t) {
                    const float x = t.getFloatValue();
                    return t.containsIgnoreCase ("ms") || ! t.containsIgnoreCase ("s") ? x : x * 1000.0f; })));
    }
    void addBool (Layout& l, const char* id, const char* name, bool def)
    {
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
    }
}

Layout createLayout()
{
    Layout l;
    addTime (l, rise, "Rise", kTimeMinMs, kTimeMaxMs, 100.0f);
    addTime (l, fall, "Fall", kTimeMinMs, kTimeMaxMs, 400.0f);
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { curve, 1 }, "Curve", curveNames, 1));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { mode, 1 }, "Mode", modeNames, bryn::modeAr));
    addBool (l, trigger, "Trigger button", false);
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { outChannel, 1 }, "MIDI out channel", 1, 16, bryn::kDefaultChannel));
    addBool (l, outFnOn, "Function out", true);
    addBool (l, riseGateOn, "Rise gate out", true);
    addBool (l, fallGateOn, "Fall gate out", true);
    addBool (l, eocOn, "EOC out", true);
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { sendAs, 1 }, "Send as", sendAsNames, 0));
    return l;
}

bryn::Settings read (const juce::AudioProcessorValueTreeState& apvts)
{
    auto v = [&] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    bryn::Settings s;
    s.fn.rise     = v (rise) / 1000.0;
    s.fn.fall     = v (fall) / 1000.0;
    s.fn.logCurve = juce::roundToInt (v (curve)) == 1;
    s.fn.mode     = juce::jlimit (0, 2, juce::roundToInt (v (mode)));
    s.outChannel  = juce::jlimit (1, 16, juce::roundToInt (v (outChannel)));
    s.outOn[bryn::outFunction] = v (outFnOn) >= 0.5f;
    for (int k = 0; k < bryn::kNumPulseOutputs; ++k) s.pulseOn[k] = v (pulseOnIds[k]) >= 0.5f;
    s.sendAs = juce::jlimit (0, 2, juce::roundToInt (v (sendAs)));
    return s;
}
}
