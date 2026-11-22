// The Bryn window, as approved in the draft (2026-11-22). Built the same way as Hillside's window.
//   left:  the function display (the curve for the current settings, the other curve dashed, the rise and fall shaded,
//          a moving point on the live function) with four lights under it (gate in, rise, fall, EOC); the Rise and
//          Fall faders and the Curve switch (Lin | Log);
//   right: the round Trigger button (glows with the function) and the Mode switch (ASR | AR | Loop), then the outputs:
//          Function with its switch, CC numbers and level bar; Rise gate, Fall gate and EOC with their switches,
//          numbers (which follow Send as) and lights; Send as (CC | Note | Both);
//   top:   the name and the MIDI out channel; bottom right: the version.
// The panel is laid out at 780 x 470 and scaled to the window (80% to 200%, proportions locked).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "Style.h"

class BrynLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BrynLookAndFeel();
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return BrStyle::font (12.5f); }
    juce::Font getPopupMenuFont() override { return BrStyle::font (13.0f); }
};

// The function display and the four lights under it.
class FunctionView : public juce::Component, private juce::Timer
{
public:
    explicit FunctionView (BrynProcessor& p);
    ~FunctionView() override { stopTimer(); }
    void paint (juce::Graphics&) override;

    static constexpr int kWidth = 440, kHeight = 250, kBoxHeight = 220;

    // The drawn stages for the current settings (for the checks too).
    struct Segment { bryn::Stage stage; float x, w; };
    std::vector<Segment> segments() const;
    juce::Point<float> pointFor (int stage, float level, unsigned cycles, bool gate) const;   // x < 0: no point
    bool eocLit() const { return eocHold > 0; }
    void refresh() { timerCallback(); }   // for the checks

private:
    void timerCallback() override;
    BrynProcessor& proc;
    struct Snap { int stage = -1; float level = -1; bool gate = false; unsigned cycles = 0; float r = 0, f = 0; int curve = -1, mode = -1; bool eoc = false;
                  bool operator== (const Snap& o) const
                  {
                      using juce::exactlyEqual;
                      return stage == o.stage && gate == o.gate && cycles == o.cycles && exactlyEqual (level, o.level)
                          && exactlyEqual (r, o.r) && exactlyEqual (f, o.f) && curve == o.curve && mode == o.mode && eoc == o.eoc;
                  } };
    Snap shown;
    Snap read() const;
    unsigned seenEocStarts = 0;
    int eocHold = 0;
};

// The round Trigger button: held = a held key (in every mode). Glows with the function, like the module's fader LEDs.
class TriggerButton : public juce::Component, private juce::Timer
{
public:
    explicit TriggerButton (BrynProcessor& p);
    ~TriggerButton() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void press (bool down);                         // also used by the checks

private:
    void timerCallback() override;
    BrynProcessor& proc;
    juce::RangedAudioParameter* param;
    bool pressed = false;
    float level = 0.0f;
};

// A row of segments for a menu control (Curve, Mode, Send as).
class Segmented : public juce::Component
{
public:
    Segmented (juce::RangedAudioParameter& p, const juce::StringArray& names);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void choose (int index);                        // also used by the checks
    int selected() const { return index; }

private:
    juce::StringArray names;
    int index = 0;
    juce::ParameterAttachment attachment;
};

// The level bar of the Function output (the value it last sent).
class LevelBar : public juce::Component, private juce::Timer
{
public:
    LevelBar (BrynProcessor& p, int output, juce::Colour c);
    ~LevelBar() override { stopTimer(); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    BrynProcessor& proc;
    int output;
    juce::Colour colour;
    int shownValue = -2;
};

// The lights of the three pulse outputs (Rise gate, Fall gate, EOC): lit while that output is on. A pulse shorter
// than the panel's refresh still flashes (each new start keeps the light on for about 100 ms).
class PulseLights : public juce::Component, private juce::Timer
{
public:
    explicit PulseLights (BrynProcessor& p);
    ~PulseLights() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    static constexpr int kX = 735, kY = 241, kRowStep = 30;   // light centres at x 745, y 251 + 30 k on the panel
    bool lit (int k) const { return shown[k]; }               // for the checks
    void refresh() { timerCallback(); }                      // for the checks

private:
    void timerCallback() override;
    BrynProcessor& proc;
    bool shown[bryn::kNumPulseOutputs] {};
    unsigned seenStarts[bryn::kNumPulseOutputs] {};
    int hold[bryn::kNumPulseOutputs] {};
    int sendAsShown = -1;
};

// The whole panel at its design size (780 x 470). The editor below scales it to the window.
class BrynPanel : public juce::Component
{
public:
    explicit BrynPanel (BrynProcessor&);
    ~BrynPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // For the offline checks: every control and the parameter it moves.
    struct ControlInfo { juce::Component* control; juce::String paramId; };
    std::vector<ControlInfo> controls();
    TriggerButton& getTriggerButton() { return triggerButton; }
    PulseLights& getPulseLights() { return pulseLights; }
    FunctionView& getFunctionView() { return functionView; }
    Segmented& getModeSwitch() { return modeSwitch; }

    static constexpr int kWidth = 780, kHeight = 470;
    static constexpr int kRowY[4] = { 221, 251, 281, 311 };   // Function, Rise gate, Fall gate, EOC

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    BrynProcessor& proc;
    BrynLookAndFeel lnf;

    FunctionView functionView;
    juce::Slider faders[2];                         // Rise, Fall
    Segmented curveSwitch;
    TriggerButton triggerButton;
    Segmented modeSwitch;
    juce::ComboBox channelBox;
    juce::ToggleButton fnOn;
    LevelBar fnBar;
    juce::ToggleButton pulseOn[bryn::kNumPulseOutputs];
    Segmented sendAs;
    PulseLights pulseLights;

    std::unique_ptr<SliderAttachment> faderAtt[2];
    std::unique_ptr<ComboAttachment> channelAtt;
    std::unique_ptr<ButtonAttachment> fnOnAtt, pulseOnAtt[bryn::kNumPulseOutputs];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrynPanel)
};

// The plugin window: resizable between 80% and 200% of the design size, proportions locked. The width is saved with
// the project.
class BrynEditor : public juce::AudioProcessorEditor
{
public:
    explicit BrynEditor (BrynProcessor&);
    ~BrynEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = BrynPanel::kWidth, kHeight = BrynPanel::kHeight;
    static constexpr int kMinWidth = 624, kMaxWidth = 1560;     // 80% and 200%
    static inline const juce::Identifier widthProperty { "editorWidth" };
    static int heightForWidth (int width) { return juce::roundToInt ((double) width * kHeight / kWidth); }

    BrynPanel panel;

private:
    BrynProcessor& proc;
    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrynEditor)
};
