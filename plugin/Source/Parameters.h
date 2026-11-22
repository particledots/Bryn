// Every control of Bryn, as Logic sees it.
//
// IMPORTANT: these ids, their order, ranges and menu lists were fixed in the first build (0.1.0) and must never
// change. Saved projects and automation refer to them.
// If something must be added one day, add it at the END with ParameterID version 2.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <bryn/Engine.h>

namespace BrParams
{
    inline constexpr const char* rise       = "rise";        // 3 ms .. 10 s (a whole climb)
    inline constexpr const char* fall       = "fall";        // 3 ms .. 10 s (a whole drop)
    inline constexpr const char* curve      = "curve";       // Lin / Log
    inline constexpr const char* mode       = "mode";        // ASR / AR / Loop
    inline constexpr const char* trigger    = "trigger";     // the panel's Trigger button (never saved as held)
    inline constexpr const char* outChannel = "outChannel";  // 1 .. 16
    inline constexpr const char* outFnOn    = "outFnOn";     // Function output on
    inline constexpr const char* riseGateOn = "riseGateOn";  // Rise gate output on
    inline constexpr const char* fallGateOn = "fallGateOn";  // Fall gate output on
    inline constexpr const char* eocOn      = "eocOn";       // EOC output on
    inline constexpr const char* sendAs     = "sendAs";      // CC / Note / Both

    inline constexpr float kTimeMinMs = 3.0f, kTimeMaxMs = 10000.0f;

    extern const char* const pulseOnIds[bryn::kNumPulseOutputs];   // riseGateOn, fallGateOn, eocOn
    extern const juce::StringArray curveNames;    // Lin, Log
    extern const juce::StringArray modeNames;     // ASR, AR, Loop (the module's toggle: left, centre, right)
    extern const juce::StringArray sendAsNames;   // CC, Note, Both

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // The current values, for the engine.
    bryn::Settings read (const juce::AudioProcessorValueTreeState& apvts);

    juce::String timeText (float ms);   // "5.0 ms", "300 ms", "1.20 s"
}
