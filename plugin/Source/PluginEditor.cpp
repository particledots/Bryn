#include "PluginEditor.h"
#include "Parameters.h"

#include <cmath>

using namespace BrStyle;

namespace
{
    float param (const BrynProcessor& p, const char* id)
    {
        return const_cast<BrynProcessor&> (p).getValueTreeState().getRawParameterValue (id)->load();
    }
    int choice (const BrynProcessor& p, const char* id) { return juce::roundToInt (param (p, id)); }

    // A round light with a soft halo; level 0 to 1 decides how brightly it shines. (As Hillside's.)
    void drawLight (juce::Graphics& g, float cx, float cy, float r, juce::Colour c, float level)
    {
        level = juce::jlimit (0.0f, 1.0f, level);
        if (level > 0.15f)
        {
            g.setColour (c.withAlpha (0.12f * level));
            g.fillEllipse (cx - r - 4.0f, cy - r - 4.0f, 2.0f * (r + 4.0f), 2.0f * (r + 4.0f));
        }
        g.setColour (knob);
        g.fillEllipse (cx - r, cy - r, 2.0f * r, 2.0f * r);
        if (level <= 0.0f)
        {
            g.setColour (line2);
            g.drawEllipse (cx - r, cy - r, 2.0f * r, 2.0f * r, 1.0f);
        }
        g.setColour (c.withAlpha (0.12f + 0.88f * level));
        g.fillEllipse (cx - r, cy - r, 2.0f * r, 2.0f * r);
    }

    // Width of a stage on the display for its time: 30 px at 3 ms to 150 px at 10 s, on a log scale.
    float stageWidth (float ms)
    {
        const float x = std::log (juce::jlimit (3.0f, 10000.0f, ms) / 3.0f) / std::log (10000.0f / 3.0f);
        return 30.0f + 120.0f * x;
    }

    // The curve as drawn (from 0 to 1), and where a level sits on it.
    double shapeOf (double t, bool logCurve)
    {
        bryn::Curve c;
        c.setShape (logCurve ? 1.0 : 0.0);
        return c (t);
    }
    double timeFor (double level, bool logCurve)   // the inverse: 0..1 along the stage at which the curve is at this level
    {
        level = juce::jlimit (0.0, 1.0, level);
        if (! logCurve) return level;
        const double k = bryn::kShapeMaxK;
        return -std::log (1.0 - level * (1.0 - std::exp (-k))) / k;
    }

    const char* const noteNames[bryn::kNumPulseOutputs] = { "A1", "A#1", "B1" };
}

// ================================================================================================ look and feel
BrynLookAndFeel::BrynLookAndFeel()
{
    setColour (juce::ComboBox::textColourId, bright);
    setColour (juce::ComboBox::arrowColourId, dim);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff262731));
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, heatherDark);
    setColour (juce::PopupMenu::highlightedTextColourId, bright);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::textBoxTextColourId, dim);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void BrynLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                        juce::Slider::SliderStyle, juce::Slider&)
{
    const float cx = (float) x + (float) w * 0.5f;
    g.setColour (line);
    g.fillRoundedRectangle (cx - 4.0f, (float) y, 8.0f, (float) h, 4.0f);
    g.setColour (heatherDark);
    g.fillRoundedRectangle (cx - 2.0f, pos, 4.0f, (float) (y + h) - pos, 2.0f);
    g.setColour (heather);
    g.fillRoundedRectangle (cx - 20.0f, pos - 9.0f, 40.0f, 18.0f, 3.0f);
    g.setColour (panel);
    g.drawLine (cx - 12.0f, pos, cx + 12.0f, pos, 1.5f);
}

void BrynLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    const auto r = b.getLocalBounds().toFloat().withSizeKeepingCentre (30.0f, 18.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? heather : knob);
    g.fillRoundedRectangle (r, 9.0f);
    if (! on)
    {
        g.setColour (line2);
        g.drawRoundedRectangle (r, 9.0f, 1.0f);
    }
    const float kx = on ? r.getRight() - 9.0f : r.getX() + 9.0f;
    g.setColour (on ? panel : dim);
    g.fillEllipse (kx - 6.0f, r.getCentreY() - 6.0f, 12.0f, 12.0f);
}

void BrynLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (knob);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (line2);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 10.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 3.5f, ay - 2.0f, ax + 3.5f, ay - 2.0f, ax, ay + 2.5f);
    g.setColour (dim);
    g.fillPath (arrow);
}

void BrynLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (4, 1, box.getWidth() - 18, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

// ================================================================================================ function display
FunctionView::FunctionView (BrynProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (false, false);
    seenEocStarts = proc.getDisplay().pulseStarts[bryn::kEocIndex].load();
    startTimerHz (30);
}

FunctionView::Snap FunctionView::read() const
{
    const auto& d = proc.getDisplay();
    Snap s;
    s.stage = d.stage.load();
    s.level = std::round (d.level.load() * 400.0f) / 400.0f;
    s.gate = d.gate.load();
    s.cycles = d.cycles.load();
    s.r = param (proc, BrParams::rise); s.f = param (proc, BrParams::fall);
    s.curve = choice (proc, BrParams::curve); s.mode = choice (proc, BrParams::mode);
    s.eoc = eocHold > 0;
    return s;
}

void FunctionView::timerCallback()
{
    // The EOC light: on for about 100 ms after each end of cycle (also while the EOC output is switched off).
    const unsigned cyc = proc.getDisplay().cycles.load();
    if (cyc != seenEocStarts) { seenEocStarts = cyc; eocHold = 3; }
    else if (eocHold > 0) --eocHold;
    const Snap now = read();
    if (! (now == shown)) { shown = now; repaint(); }
}

std::vector<FunctionView::Segment> FunctionView::segments() const
{
    const float r = stageWidth (param (proc, BrParams::rise)), f = stageWidth (param (proc, BrParams::fall));
    const int mode = choice (proc, BrParams::mode);
    std::vector<Segment> v;
    float x = 20.0f;
    auto add = [&] (bryn::Stage s, float w) { v.push_back ({ s, x, w }); x += w; };
    add (bryn::Stage::rise, r);
    if (mode == bryn::modeAsr) add (bryn::Stage::hold, 80.0f);
    add (bryn::Stage::fall, f);
    if (mode == bryn::modeLoop) { add (bryn::Stage::rise, r); add (bryn::Stage::fall, f); }
    const float room = (float) kWidth - 40.0f, total = x - 20.0f;
    const float k = total > room ? room / total : 1.0f;
    for (auto& s : v) { s.x = 20.0f + (s.x - 20.0f) * k; s.w *= k; }
    return v;
}

juce::Point<float> FunctionView::pointFor (int stage, float level, unsigned cycles, bool gate) const
{
    const auto sg = segments();
    const bool logCurve = choice (proc, BrParams::curve) == 1;
    const int mode = choice (proc, BrParams::mode);
    const float base = 200.0f, top = 22.0f;
    const float y = base - level * (base - top);
    const int cycle = mode == bryn::modeLoop ? (int) (cycles % 2) : 0;
    auto nth = [&] (bryn::Stage s) -> const Segment* {
        int seen = 0;
        for (auto& x : sg) if (x.stage == s && seen++ == cycle) return &x;
        return nullptr;
    };
    switch ((bryn::Stage) stage)
    {
        case bryn::Stage::rise: if (auto* s = nth (bryn::Stage::rise)) return { s->x + s->w * (float) timeFor (level, logCurve), y }; break;
        case bryn::Stage::fall: if (auto* s = nth (bryn::Stage::fall)) return { s->x + s->w * (float) timeFor (1.0 - level, logCurve), y }; break;
        case bryn::Stage::hold: if (auto* s = nth (bryn::Stage::hold)) return { s->x + s->w * 0.5f, y }; break;
        case bryn::Stage::idle: if (gate && ! sg.empty()) return { sg.back().x + sg.back().w, base }; break;   // AR: resting with the key held
    }
    return { -1.0f, -1.0f };
}

void FunctionView::paint (juce::Graphics& g)
{
    const Snap s = read();
    const auto box = juce::Rectangle<float> (0.0f, 0.0f, (float) kWidth, (float) kBoxHeight);
    g.setColour (display);
    g.fillRoundedRectangle (box, 6.0f);
    g.setColour (line);
    g.drawRoundedRectangle (box.reduced (0.5f), 6.0f, 1.0f);

    const float base = 200.0f, top = 22.0f;
    auto yOf = [&] (double level) { return base - (float) level * (base - top); };
    g.setColour (line);
    g.drawHorizontalLine ((int) base, 10.0f, (float) kWidth - 10.0f);

    const auto sg = segments();
    const bool logCurve = s.curve == 1;
    // The rise and fall shaded.
    for (auto& seg : sg)
    {
        if (seg.stage == bryn::Stage::hold) continue;
        g.setColour (seg.stage == bryn::Stage::rise ? heather.withAlpha (0.07f) : amber.withAlpha (0.05f));
        g.fillRect (seg.x, top - 8.0f, seg.w, base - top + 8.0f);
    }
    auto build = [&] (bool curveLog)
    {
        juce::Path p;
        p.startNewSubPath (sg.front().x, yOf (0.0));
        for (auto& seg : sg)
        {
            if (seg.stage == bryn::Stage::hold) { p.lineTo (seg.x + seg.w, yOf (1.0)); continue; }
            for (int i = 1; i <= 48; ++i)
            {
                const double t = i / 48.0, f = shapeOf (t, curveLog);
                p.lineTo (seg.x + (float) t * seg.w, yOf (seg.stage == bryn::Stage::rise ? f : 1.0 - f));
            }
        }
        return p;
    };
    {
        juce::Path dashed;
        const float dashes[] = { 4.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, build (! logCurve), dashes, 2);
        g.setColour (faint);
        g.fillPath (dashed);
    }
    g.setColour (heather);
    g.strokePath (build (logCurve), juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Stage names under the first cycle, and the settings top right.
    g.setFont (font (11.0f));
    g.setColour (dim);
    for (size_t i = 0; i < sg.size() && i < 3; ++i)
    {
        if (i == 2 && sg[i].stage == bryn::Stage::rise) break;
        const char* name = sg[i].stage == bryn::Stage::rise ? "rise" : (sg[i].stage == bryn::Stage::hold ? "hold" : "fall");
        g.drawText (name, (int) sg[i].x, (int) base - 16, (int) sg[i].w, 14, juce::Justification::centred);
    }
    g.drawText (BrParams::modeNames[juce::jlimit (0, 2, s.mode)] + juce::String::fromUTF8 (" \xc2\xb7 ") + BrParams::curveNames[logCurve ? 1 : 0],
                kWidth - 130, 12, 120, 14, juce::Justification::centredRight);
    g.setColour (faint);
    g.drawText (juce::String ("dashed: ") + BrParams::curveNames[logCurve ? 0 : 1], kWidth - 130, 28, 120, 14, juce::Justification::centredRight);

    // The moving point.
    const auto pt = pointFor (s.stage, s.level, s.cycles, s.gate);
    if (pt.x >= 0.0f)
    {
        g.setColour (amber.withAlpha (0.25f));
        g.fillEllipse (pt.x - 10.0f, pt.y - 10.0f, 20.0f, 20.0f);
        g.setColour (amber);
        g.fillEllipse (pt.x - 6.0f, pt.y - 6.0f, 12.0f, 12.0f);
    }

    // The four lights: gate in, rise, fall, EOC.
    const float ly = (float) kBoxHeight + 18.0f;
    const float lx[4] = { 20.0f, 72.0f, 132.0f, 188.0f };
    const char* names[4] = { "in", "rise", "fall", "EOC" };
    const bool lit[4] = { s.gate, s.stage == (int) bryn::Stage::rise, s.stage == (int) bryn::Stage::fall, s.eoc };
    for (int i = 0; i < 4; ++i)
    {
        drawLight (g, lx[i], ly, 5.0f, amber, lit[i] ? 1.0f : 0.0f);
        g.setColour (lit[i] ? amberText : text);
        g.setFont (font (11.0f));
        g.drawText (names[i], (int) lx[i] + 10, (int) ly - 8, 44, 16, juce::Justification::centredLeft);
    }
}

// ================================================================================================ Trigger button
TriggerButton::TriggerButton (BrynProcessor& p)
    : proc (p), param (p.getValueTreeState().getParameter (BrParams::trigger))
{
    startTimerHz (30);
}

void TriggerButton::press (bool down)
{
    if (down == pressed) return;
    pressed = down;
    if (down) { param->beginChangeGesture(); param->setValueNotifyingHost (1.0f); }
    else      { param->setValueNotifyingHost (0.0f); param->endChangeGesture(); }
    repaint();
}

void TriggerButton::mouseDown (const juce::MouseEvent&) { press (true); }
void TriggerButton::mouseUp (const juce::MouseEvent&)   { press (false); }

void TriggerButton::timerCallback()
{
    const float lv = std::round (proc.getDisplay().level.load() * 100.0f) / 100.0f;
    if (! juce::exactlyEqual (lv, level)) { level = lv; repaint(); }
}

void TriggerButton::paint (juce::Graphics& g)
{
    const float glow = juce::jlimit (0.0f, 1.0f, proc.getDisplay().level.load());
    const float r = 34.0f, cx = 36.0f, cy = 36.0f;
    g.setColour (knob);
    g.fillEllipse (cx - r, cy - r, 2.0f * r, 2.0f * r);
    g.setColour (amber.withAlpha (0.45f + 0.55f * glow));
    g.drawEllipse (cx - r, cy - r, 2.0f * r, 2.0f * r, 2.0f);
    const float ri = 25.0f;
    g.setColour (pressed ? amberDeep.brighter (0.4f) : amberDeep.interpolatedWith (juce::Colour (0xff6a5024), glow));
    g.fillEllipse (cx - ri, cy - ri, 2.0f * ri, 2.0f * ri);
}

// ================================================================================================ segmented switch
Segmented::Segmented (juce::RangedAudioParameter& p, const juce::StringArray& n)
    : names (n), attachment (p, [this] (float v) { index = juce::roundToInt (v); repaint(); })
{
    attachment.sendInitialUpdate();
}

void Segmented::choose (int i)
{
    i = juce::jlimit (0, names.size() - 1, i);
    attachment.setValueAsCompleteGesture ((float) i);
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled()) return;
    choose ((int) ((float) e.x / ((float) getWidth() / (float) names.size())));
}

void Segmented::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = r.getHeight() * 0.5f;
    g.setColour (knob);
    g.fillRoundedRectangle (r, radius);
    g.setColour (line2);
    g.drawRoundedRectangle (r, radius, 1.0f);
    const float w = r.getWidth() / (float) names.size();
    for (int i = 0; i < names.size(); ++i)
    {
        const auto seg = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (2.0f);
        if (i == index)
        {
            g.setColour (heather);
            g.fillRoundedRectangle (seg, seg.getHeight() * 0.5f);
        }
        g.setColour (i == index ? panel : dim);
        g.setFont (font (12.0f));
        g.drawText (names[i], seg, juce::Justification::centred);
    }
}

// ================================================================================================ level bar
LevelBar::LevelBar (BrynProcessor& p, int o, juce::Colour c) : proc (p), output (o), colour (c)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void LevelBar::timerCallback()
{
    const int v = proc.getDisplay().sent[output].load();
    if (v != shownValue) { shownValue = v; repaint(); }
}

void LevelBar::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (line);
    g.fillRoundedRectangle (r, 2.0f);
    const int v = proc.getDisplay().sent[output].load();
    const float level = v > 0 ? (float) v / 16383.0f : 0.0f;
    if (level > 0.0f)
    {
        g.setColour (colour);
        g.fillRoundedRectangle (r.withWidth (juce::jmax (2.0f, r.getWidth() * level)), 2.0f);
    }
}

// ================================================================================================ pulse lights
PulseLights::PulseLights (BrynProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (false, false);
    for (int k = 0; k < bryn::kNumPulseOutputs; ++k)
    {
        seenStarts[k] = proc.getDisplay().pulseStarts[k].load();
        shown[k] = proc.getDisplay().pulseOn[k].load();
    }
    startTimerHz (30);
}

void PulseLights::timerCallback()
{
    bool changed = false;
    for (int k = 0; k < bryn::kNumPulseOutputs; ++k)
    {
        const unsigned st = proc.getDisplay().pulseStarts[k].load();
        if (st != seenStarts[k]) { seenStarts[k] = st; hold[k] = 3; }
        else if (hold[k] > 0) --hold[k];
        const bool on = proc.getDisplay().pulseOn[k].load() || hold[k] > 0;
        if (on != shown[k]) { shown[k] = on; changed = true; }
    }
    if (changed) repaint();
    // The numbers beside the lights follow Send as (CC 82, A1, or both).
    const int mode = juce::roundToInt (proc.getValueTreeState().getRawParameterValue (BrParams::sendAs)->load());
    if (mode != sendAsShown)
    {
        sendAsShown = mode;
        if (auto* parent = getParentComponent()) parent->repaint (600, 236, 90, 90);
    }
}

void PulseLights::paint (juce::Graphics& g)
{
    for (int k = 0; k < bryn::kNumPulseOutputs; ++k)
        drawLight (g, 10.0f, 10.0f + (float) (k * kRowStep), 5.0f, amber, shown[k] ? 1.0f : 0.0f);
}

// ================================================================================================ panel
BrynPanel::BrynPanel (BrynProcessor& p)
    : proc (p), functionView (p),
      curveSwitch (*p.getValueTreeState().getParameter (BrParams::curve), BrParams::curveNames),
      triggerButton (p),
      modeSwitch (*p.getValueTreeState().getParameter (BrParams::mode), BrParams::modeNames),
      fnBar (p, bryn::outFunction, heather),
      sendAs (*p.getValueTreeState().getParameter (BrParams::sendAs), BrParams::sendAsNames),
      pulseLights (p)
{
    setLookAndFeel (&lnf);
    auto& vts = proc.getValueTreeState();

    addAndMakeVisible (functionView);
    const char* faderIds[2] = { BrParams::rise, BrParams::fall };
    for (int i = 0; i < 2; ++i)
    {
        auto& f = faders[i];
        f.setSliderStyle (juce::Slider::LinearVertical);
        f.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        f.setName (faderIds[i]);
        f.onValueChange = [this] { repaint (20, 360, 300, 30); };
        addAndMakeVisible (f);
        faderAtt[i] = std::make_unique<SliderAttachment> (vts, faderIds[i], f);
    }
    addAndMakeVisible (curveSwitch);
    addAndMakeVisible (triggerButton);
    addAndMakeVisible (modeSwitch);

    for (int ch = 1; ch <= 16; ++ch) channelBox.addItem (juce::String (ch), ch);
    addAndMakeVisible (channelBox);
    channelAtt = std::make_unique<ComboAttachment> (vts, BrParams::outChannel, channelBox);

    addAndMakeVisible (fnOn);
    fnOnAtt = std::make_unique<ButtonAttachment> (vts, BrParams::outFnOn, fnOn);
    addAndMakeVisible (fnBar);
    for (int k = 0; k < bryn::kNumPulseOutputs; ++k)
    {
        addAndMakeVisible (pulseOn[k]);
        pulseOnAtt[k] = std::make_unique<ButtonAttachment> (vts, BrParams::pulseOnIds[k], pulseOn[k]);
    }
    addAndMakeVisible (sendAs);
    addAndMakeVisible (pulseLights);
}

BrynPanel::~BrynPanel() { setLookAndFeel (nullptr); }

std::vector<BrynPanel::ControlInfo> BrynPanel::controls()
{
    std::vector<ControlInfo> v {
        { &faders[0], BrParams::rise }, { &faders[1], BrParams::fall }, { &curveSwitch, BrParams::curve },
        { &modeSwitch, BrParams::mode }, { &triggerButton, BrParams::trigger }, { &channelBox, BrParams::outChannel },
        { &fnOn, BrParams::outFnOn },
    };
    for (int k = 0; k < bryn::kNumPulseOutputs; ++k) v.push_back ({ &pulseOn[k], BrParams::pulseOnIds[k] });
    v.push_back ({ &sendAs, BrParams::sendAs });
    return v;
}

void BrynPanel::resized()
{
    functionView.setBounds (20, 58, FunctionView::kWidth, FunctionView::kHeight);
    const int fx[2] = { 92, 212 };
    for (int i = 0; i < 2; ++i) faders[i].setBounds (fx[i] - 20, 318, 40, 124);
    curveSwitch.setBounds (330, 360, 110, 26);

    triggerButton.setBounds (494, 66, 72, 72);
    modeSwitch.setBounds (590, 80, 170, 28);
    channelBox.setBounds (676, 15, 48, 24);

    fnOn.setBounds (486, kRowY[0] - 10, 34, 20);
    fnBar.setBounds (690, kRowY[0] - 5, 60, 10);
    for (int k = 0; k < bryn::kNumPulseOutputs; ++k) pulseOn[k].setBounds (486, kRowY[k + 1] - 10, 34, 20);
    sendAs.setBounds (552, 354, 198, 24);
    pulseLights.setBounds (PulseLights::kX, PulseLights::kY, 20, 20 + 2 * PulseLights::kRowStep);
}

void BrynPanel::paint (juce::Graphics& g)
{
    g.fillAll (panel);

    // Header.
    g.setColour (bright);
    g.setFont (font (22.0f));
    g.drawText ("Bryn", 20, 12, 60, 26, juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (font (12.0f));
    g.drawText (juce::String::fromUTF8 ("particledots  \xc2\xb7  two-stage function"), 78, 17, 260, 18, juce::Justification::centredLeft);
    g.drawText ("MIDI out ch", 590, 17, 82, 18, juce::Justification::centredRight);
    g.setColour (line);
    g.drawHorizontalLine (46, 20.0f, 760.0f);

    // Fader captions and values.
    auto& vts = proc.getValueTreeState();
    const char* names[2] = { "Rise", "Fall" };
    const char* ids[2] = { BrParams::rise, BrParams::fall };
    const int fx[2] = { 92, 212 };
    for (int i = 0; i < 2; ++i)
    {
        g.setColour (bright);
        g.setFont (font (13.0f));
        g.drawText (names[i], fx[i] - 40, 446, 80, 16, juce::Justification::centred);
        g.setColour (dim);
        g.setFont (font (11.0f));
        g.drawText (vts.getParameter (ids[i])->getCurrentValueAsText(), fx[i] + 26, 372, 80, 16, juce::Justification::centredLeft);
    }
    g.setColour (bright);
    g.setFont (font (13.0f));
    g.drawText ("Curve", 330, 392, 110, 16, juce::Justification::centred);

    // Right column.
    g.setColour (line);
    g.drawVerticalLine (476, 58.0f, 452.0f);
    g.drawHorizontalLine (178, 490.0f, 760.0f);
    g.setColour (text);
    g.setFont (font (12.0f));
    g.drawText ("Trigger", 494, 142, 72, 16, juce::Justification::centred);
    g.drawText ("Mode", 590, 112, 170, 16, juce::Justification::centred);
    g.setColour (dim);
    g.drawText ("Outputs", 490, 186, 100, 16, juce::Justification::centredLeft);

    const int mode = juce::roundToInt (vts.getRawParameterValue (BrParams::sendAs)->load());
    const char* rowNames[4] = { "Function", "Rise gate", "Fall gate", "EOC" };
    for (int r = 0; r < 4; ++r)
    {
        g.setColour (bright);
        g.setFont (font (12.0f));
        g.drawText (rowNames[r], 530, kRowY[r] - 8, 76, 16, juce::Justification::centredLeft);
        g.setColour (dim);
        g.setFont (font (11.0f));
        juce::String number;
        if (r == 0) number = "CC 80 + 81";
        else
        {
            const int k = r - 1;
            const juce::String cc = "CC " + juce::String (bryn::kPulseCc[k]);
            if (mode == bryn::sendAsNote)      number = juce::String ("note ") + noteNames[k];
            else if (mode == bryn::sendAsBoth) number = cc + " + " + noteNames[k];
            else                               number = cc;
        }
        g.drawText (number, 606, kRowY[r] - 8, 84, 16, juce::Justification::centredLeft);
    }
    g.setColour (dim);
    g.setFont (font (12.0f));
    g.drawText ("Send as", 490, 358, 60, 16, juce::Justification::centredLeft);

    g.setColour (faint);
    g.setFont (font (11.0f));
    g.drawText (JucePlugin_VersionString, 700, 446, 60, 14, juce::Justification::centredRight);
}

// ================================================================================================ editor
BrynEditor::BrynEditor (BrynProcessor& p)
    : AudioProcessorEditor (&p), panel (p), proc (p)
{
    addAndMakeVisible (panel);
    constrainer.setFixedAspectRatio ((double) kWidth / (double) kHeight);
    constrainer.setSizeLimits (kMinWidth, heightForWidth (kMinWidth), kMaxWidth, heightForWidth (kMaxWidth));
    setConstrainer (&constrainer);
    setResizable (true, true);                       // with the drag corner

    const int saved = (int) proc.getValueTreeState().state.getProperty (widthProperty, kWidth);
    const int width = juce::jlimit (kMinWidth, kMaxWidth, saved);
    setSize (width, heightForWidth (width));
}

BrynEditor::~BrynEditor() = default;

void BrynEditor::paint (juce::Graphics& g) { g.fillAll (BrStyle::panel); }

void BrynEditor::resized()
{
    const double scale = juce::jmin ((double) getWidth() / kWidth, (double) getHeight() / kHeight);
    panel.setTransform (juce::AffineTransform::scale ((float) scale));
    panel.setBounds (0, 0, kWidth, kHeight);

    auto& state = proc.getValueTreeState().state;
    if ((int) state.getProperty (widthProperty, -1) != getWidth())
        state.setProperty (widthProperty, getWidth(), nullptr);
}
