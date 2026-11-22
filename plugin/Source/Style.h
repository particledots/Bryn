// Bryn's own look (as in the approved draft, 2026-11-22): dark slate panel, heather curve and switches, amber lights.
// Original; nothing taken from any module maker's artwork. Same structure as Hillside's Style.h.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace BrStyle
{
    inline const juce::Colour panel       { 0xff1c1d24 };
    inline const juce::Colour display     { 0xff14151a };
    inline const juce::Colour line        { 0xff2c2d38 };
    inline const juce::Colour line2       { 0xff3a3b48 };
    inline const juce::Colour knob        { 0xff25262f };
    inline const juce::Colour text        { 0xffc9c5d6 };
    inline const juce::Colour bright      { 0xffece9f2 };
    inline const juce::Colour dim         { 0xff8c889a };
    inline const juce::Colour faint       { 0xff6f6889 };
    inline const juce::Colour heather     { 0xffb8a6d9 };
    inline const juce::Colour heatherMid  { 0xff8e7fb0 };
    inline const juce::Colour heatherDark { 0xff4a3f66 };
    inline const juce::Colour amber       { 0xfff2b24c };
    inline const juce::Colour amberText   { 0xfff2d9a8 };
    inline const juce::Colour amberDeep   { 0xff3d3220 };

    inline juce::Font font (float size) { return juce::Font (juce::FontOptions (size)); }
}
